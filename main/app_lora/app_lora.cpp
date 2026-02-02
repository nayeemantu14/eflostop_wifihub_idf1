#include "app_lora.h"
#include "lora.h"
#include "rgb/rgb.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" // Required for Mutex
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/uart.h"

static const char *TAG = "APP_LORA";

// -----------------------------------------------------------------------------
// Hardware Pinout
// -----------------------------------------------------------------------------
#define PIN_NUM_MISO    13
#define PIN_NUM_MOSI    11
#define PIN_NUM_CLK     12
#define PIN_NUM_CS      10
#define PIN_NUM_DIO     2   
#define RESET_PIN       9   
#define PIN_NUM_BUSY    14  

// -----------------------------------------------------------------------------
// LoRa Configuration
// -----------------------------------------------------------------------------
#define LORA_FREQ_HZ        915000000L
#define LORA_SF             7
#define LORA_BW_HZ          125000L
#define LORA_CR_DEN         5
#define LORA_PREAMBLE_LEN   8
#define LORA_CRC_ON         true
#define STM32_PAYLOAD_LEN   64

// -----------------------------------------------------------------------------
// Global Resources
// -----------------------------------------------------------------------------
static LoRa* lora_driver = nullptr;
QueueHandle_t lora_rx_queue = NULL;
static SemaphoreHandle_t lora_mutex = NULL; // Protects access to lora_driver

// Runtime State
static struct {
    uint8_t syncWord;
    bool sendAck;
    uint16_t ackDelayMs;
    uint32_t rxCount;
    uint32_t ackSentCount;
    uint32_t lastRxTimeMs;
    bool triedAltSync;
    uint64_t startTime;
} lora_state = {
    .syncWord = 0x12,
    .sendAck = true,
    .ackDelayMs = 100,
    .rxCount = 0,
    .ackSentCount = 0,
    .lastRxTimeMs = 0,
    .triedAltSync = false,
    .startTime = 0
};

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static uint32_t get_millis() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

// -----------------------------------------------------------------------------
// Core Logic
// -----------------------------------------------------------------------------

static bool decode_frame(const uint8_t* buf, int len, lora_packet_t* out_packet) {
    if (len < 10) {
        ESP_LOGE(TAG, "Packet too short (%d bytes)", len);
        return false;
    }

    // Decode Payload
    out_packet->sensorId = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
    out_packet->batteryPercentage = buf[4];
    out_packet->leakStatus = buf[5];
    out_packet->frameSent = (buf[6] << 8) | buf[7];
    out_packet->frameAck = (buf[8] << 8) | buf[9];
    out_packet->timestamp = esp_timer_get_time(); // High res timestamp

    ESP_LOGI(TAG, "Decoded: ID=0x%lX, Batt=%d%%, Leak=0x%X", 
             out_packet->sensorId, out_packet->batteryPercentage, out_packet->leakStatus);

    return true;
}

static void send_ack(const lora_packet_t* packet) {
    if (!lora_state.sendAck || !lora_driver) return;

    ESP_LOGD(TAG, "Sending ACK...");
    vTaskDelay(pdMS_TO_TICKS(lora_state.ackDelayMs));

    // SAFETY: We assume the caller holds the Mutex, or this is called from the main task
    // Since this is called from lora_task (which holds the mutex logic implicitly by ownership), 
    // we need to be careful if we move it. Currently, it's fine.

    lora_driver->beginPacket(0);

    uint8_t ackBuffer[20];
    int idx = 0;

    ackBuffer[idx++] = 0xAA; // Header
    ackBuffer[idx++] = (packet->sensorId >> 24) & 0xFF;
    ackBuffer[idx++] = (packet->sensorId >> 16) & 0xFF;
    ackBuffer[idx++] = (packet->sensorId >> 8) & 0xFF;
    ackBuffer[idx++] = packet->sensorId & 0xFF;
    ackBuffer[idx++] = packet->batteryPercentage;
    ackBuffer[idx++] = packet->leakStatus;
    ackBuffer[idx++] = (packet->frameSent >> 8) & 0xFF;
    ackBuffer[idx++] = packet->frameSent & 0xFF;
    ackBuffer[idx++] = (packet->frameAck >> 8) & 0xFF;
    ackBuffer[idx++] = packet->frameAck & 0xFF;

    uint16_t rssiAbs = abs(packet->rssi);
    ackBuffer[idx++] = (rssiAbs >> 8) & 0xFF;
    ackBuffer[idx++] = rssiAbs & 0xFF;

    int8_t snrScaled = (int8_t)(packet->snr * 10);
    ackBuffer[idx++] = (uint8_t)snrScaled;
    ackBuffer[idx++] = 0x01; // ACK Status

    uint32_t ts = get_millis() - (uint32_t)(lora_state.startTime / 1000);
    ackBuffer[idx++] = (ts >> 24) & 0xFF;
    ackBuffer[idx++] = (ts >> 16) & 0xFF;
    ackBuffer[idx++] = (ts >> 8) & 0xFF;
    ackBuffer[idx++] = ts & 0xFF;

    uint8_t checksum = 0;
    for(int i=0; i<19; i++) checksum ^= ackBuffer[i];
    ackBuffer[idx++] = checksum;

    lora_driver->write(ackBuffer, 20);
    lora_driver->endPacket(false);
    lora_driver->receive(0); // Return to RX

    lora_state.ackSentCount++;
}

static void switch_sync_word(uint8_t newSync) {
    if (xSemaphoreTake(lora_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        ESP_LOGI(TAG, "Switching Sync Word: 0x%02X -> 0x%02X", lora_state.syncWord, newSync);
        lora_state.syncWord = newSync;
        lora_driver->idle();
        lora_driver->setSyncWord(newSync);
        lora_driver->receive(0);
        xSemaphoreGive(lora_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take mutex for SyncWord switch");
    }
}

// -----------------------------------------------------------------------------
// Tasks
// -----------------------------------------------------------------------------

void uart_command_task(void *pvParameters) {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_NUM_0, 2048, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &uart_config);

    uint8_t dtmp[1];
    while(1) {
        int len = uart_read_bytes(UART_NUM_0, dtmp, 1, pdMS_TO_TICKS(100));
        if(len > 0) {
            char cmd = (char)dtmp[0];
            
            // MUTEX PROTECTION: Don't touch LoRa while the other task might be reading/writing
            if (xSemaphoreTake(lora_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                switch(cmd) {
                    case 's': 
                        ESP_LOGI(TAG, "Sending Test Packet");
                        lora_driver->beginPacket(0);
                        lora_driver->write((uint8_t*)"Test", 4);
                        lora_driver->endPacket(false);
                        lora_driver->receive(0);
                        break;
                    case 'r': 
                        ESP_LOGI(TAG, "Restarting RX...");
                        lora_driver->receive(0); 
                        break;
                    case 'd':
                        ESP_LOGI(TAG, "Stats: RX=%ld, ACKs=%ld, LastRSSI=%d", 
                                 lora_state.rxCount, lora_state.ackSentCount, 0);
                        break;
                }
                xSemaphoreGive(lora_mutex);
            }
            
            // Non-LoRa commands (Thread safe)
            if (cmd == 'a') {
                lora_state.sendAck = !lora_state.sendAck;
                ESP_LOGI(TAG, "ACK %s", lora_state.sendAck ? "ENABLED" : "DISABLED");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

extern "C" void lora_task(void* param)
{
    // 1. Initialize Objects
    lora_mutex = xSemaphoreCreateMutex();
    lora_rx_queue = xQueueCreate(10, sizeof(lora_packet_t)); // Holds 10 packets
    
    // 2. Hardware Init
    ESP_LOGI(TAG, "Initializing LoRa Driver...");
    
    // Choose your driver here (SX1262 vs SX1276 logic handled inside LoRa class via #define)
    // lora_driver = new LoRa(PIN_NUM_MOSI, PIN_NUM_MISO, PIN_NUM_CLK, PIN_NUM_CS, RESET_PIN, PIN_NUM_DIO, PIN_NUM_BUSY, 17);
    lora_driver = new LoRa(PIN_NUM_MOSI, PIN_NUM_MISO, PIN_NUM_CLK, PIN_NUM_CS, RESET_PIN, PIN_NUM_DIO, 17);

    lora_driver->setFrequency(LORA_FREQ_HZ);
    lora_driver->setSpreadingFactor(LORA_SF);
    lora_driver->setSignalBandwidth(LORA_BW_HZ);
    lora_driver->setCodingRate4(LORA_CR_DEN);
    lora_driver->setPreambleLength(LORA_PREAMBLE_LEN);
    lora_driver->setSyncWord(lora_state.syncWord);
    lora_driver->setCRC(LORA_CRC_ON);
    lora_driver->disableInvertIQ();
    
    lora_driver->receive(0);
    lora_state.startTime = esp_timer_get_time();

    // 3. Start Aux Task
    xTaskCreate(uart_command_task, "uart_cmd_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "LoRa Task Started. Listening...");

    uint8_t buffer[256];
    
    while (1) {
        // LOCK: Protect driver access
        if (xSemaphoreTake(lora_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            
            int packetSize = lora_driver->parsePacket(0);

            if (packetSize) {
                lora_state.rxCount++;
                lora_state.lastRxTimeMs = get_millis();

                // 1. Collect Packet Metadata
                lora_packet_t packet = {};
                packet.rssi = (int8_t)lora_driver->getPacketRssi();
                packet.snr = lora_driver->getPacketSnr();
                
                ESP_LOGI(TAG, "Packet Received. Size: %d, RSSI: %d, SNR: %.1f", 
                         packetSize, packet.rssi, packet.snr);

                // 2. Read Payload
                int idx = 0;
                while(lora_driver->available() && idx < sizeof(buffer)) {
                    buffer[idx++] = (uint8_t)lora_driver->read();
                }

                // 3. Decode & Process
                if (decode_frame(buffer, idx, &packet)) {
                    // Send Physical ACK immediately (Time critical)
                    send_ack(&packet);

                    // Send Data to IoT Task via Queue
                    if (xQueueSend(lora_rx_queue, &packet, 0) != pdTRUE) {
                        ESP_LOGW(TAG, "Rx Queue Full! Packet dropped.");
                    }

                    // Trigger LED
                    uint8_t ledCmd = 'G';
                    if (ledQueue != NULL) xQueueSend(ledQueue, &ledCmd, 0);
                }
            }
            
            // UNLOCK
            xSemaphoreGive(lora_mutex);
        }

        // Auto-switch Sync Word Logic (Timeout Check)
        if (lora_state.rxCount == 0 && !lora_state.triedAltSync) {
            uint32_t elapsed = (get_millis() - (uint32_t)(lora_state.startTime / 1000));
            if (elapsed > 30000) {
                lora_state.triedAltSync = true;
                switch_sync_word((lora_state.syncWord == 0x12) ? 0x34 : 0x12);
            }
        }

        // Yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void configurelora(void)
{
    xTaskCreate(lora_task, "lora_task", 10240, NULL, 5, NULL);
}