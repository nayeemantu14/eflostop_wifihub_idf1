#include "app_wifi.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "rgb.h"            
#include "freertos/queue.h" 
#include "esp_wifi.h"

// We need this to see the 'extern' handle for the Azure task
#include "app_iothub.h" 

TaskHandle_t wifiTaskHandle = NULL;

// Flag to ensure we only wake up the Azure task once per boot cycle
static bool has_notified_azure = false; 

// Forward declarations
void cb_connection_ok(void *pvParameter);
void cb_connection_lost(void *pvParameter);
void wifi_task(void *pvParameter);

void app_wifi_start()
{
    /* start the wifi manager */
    wifi_manager_start();

    /* Register callback for SUCCESS (Blue LED) */
    wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_connection_ok);

    /* Register callback for DISCONNECTION (Red LED) */
    wifi_manager_set_callback(WM_EVENT_STA_DISCONNECTED, &cb_connection_lost);

    /* create a task (optional now that we have callbacks, but kept for logic) */
    xTaskCreate(&wifi_task, "wifi_task", 4096, NULL, 5, &wifiTaskHandle);
}

// Called when we get an IP (Connected)
void cb_connection_ok(void *pvParameter)
{
    ip_event_got_ip_t *param = (ip_event_got_ip_t *)pvParameter;

    char str_ip[16];
    esp_ip4addr_ntoa(&param->ip_info.ip, str_ip, IP4ADDR_STRLEN_MAX);

    ESP_LOGI(WIFI_TAG, "I have a connection and my IP is %s!", str_ip);
    uint8_t mac[6];
    esp_wifi_get_mac(ESP_IF_WIFI_STA, mac); 
    ESP_LOGI(WIFI_TAG, "My MAC address is %02X:%02X:%02X:%02X:%02X:%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    // --- AZURE WAKE UP LOGIC ---
    // If the Azure task is waiting (sleeping) and we haven't woken it yet:
    if (!has_notified_azure && iothub_task_handle != NULL) {
        ESP_LOGI(WIFI_TAG, "Wi-Fi Connected! Waking up Azure IoT Task...");
        
        // This unblocks the ulTaskNotifyTake() in app_iothub.c
        xTaskNotifyGive(iothub_task_handle);
        
        has_notified_azure = true;
    }
    // ---------------------------

    // --- WIFI CONNECTED: Set LED to Blue Beat ---
    if (ledQueue != NULL)
    {
        uint8_t msg = 'B';
        xQueueSend(ledQueue, &msg, 0);
    }
}

// Called when we lose connection (Disconnected)
void cb_connection_lost(void *pvParameter)
{
    // Safety check before accessing structure members
    if (pvParameter != NULL) {
        wifi_event_sta_disconnected_t* wifi_event = (wifi_event_sta_disconnected_t*) pvParameter;
        ESP_LOGW(WIFI_TAG, "WiFi Disconnected. Reason: %d", wifi_event->reason);
    } else {
        ESP_LOGW(WIFI_TAG, "WiFi Disconnected (Unknown Reason)");
    }

    ESP_LOGW(WIFI_TAG, "WiFi Connection Lost!");

    // --- WIFI DISCONNECTED: Set LED to Red Ramp ---
    if (ledQueue != NULL)
    {
        uint8_t msg = 'R';
        xQueueSend(ledQueue, &msg, 0);
    }
}

void wifi_task(void *pvParameter)
{
    (void)pvParameter;

    // Set initial state to Red (Scanning/Starting)
    if (ledQueue != NULL)
    {
        uint8_t msg = 'R';
        xQueueSend(ledQueue, &msg, 0);
    }

    while (1)
    {
        // No need to poll status anymore! The callbacks handle it.
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    vTaskDelete(NULL);
}