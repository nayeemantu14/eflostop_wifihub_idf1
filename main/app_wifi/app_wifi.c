#include "app_wifi.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "rgb.h"            
#include "freertos/queue.h" 
#include "esp_wifi.h"


TaskHandle_t wifiTaskHandle = NULL;

// Forward declarations
void cb_connection_ok(void *pvParameter);
void cb_connection_lost(void *pvParameter); // <--- NEW CALLBACK
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
    //Optional: You can read the disconnect reason if needed
    wifi_event_sta_disconnected_t* wifi_event = (wifi_event_sta_disconnected_t*) pvParameter;
    ESP_LOGW(WIFI_TAG, "WiFi Disconnected. Reason: %d", wifi_event->reason);

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
        // You can keep this task alive for other monitoring or delete it.
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    vTaskDelete(NULL);
}