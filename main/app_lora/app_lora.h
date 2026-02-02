#ifndef APP_LORA_H
#define APP_LORA_H
#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// Data structure to send to IoT Hub / Main logic
typedef struct {
    uint32_t sensorId;
    uint8_t batteryPercentage;
    uint8_t leakStatus;
    uint16_t frameSent;
    uint16_t frameAck;
    int8_t rssi;
    float snr;
    uint64_t timestamp; // Changed to 64-bit for system time
} lora_packet_t;

// Queue handle to retrieve received packets
// Other tasks can extern this to read data
extern QueueHandle_t lora_rx_queue;

// Main entry point to configure and start LoRa services
void configurelora(void);

#ifdef __cplusplus
}
#endif

#endif // APP_LORA_H