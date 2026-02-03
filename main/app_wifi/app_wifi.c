#include "app_wifi.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "rgb.h"
#include "freertos/queue.h"
#include "esp_wifi.h"

#include "app_iothub.h"
#include "app_ble_valve.h"

TaskHandle_t wifiTaskHandle = NULL;
static bool has_notified_azure = false;

void cb_connection_ok(void *pvParameter);
void cb_connection_lost(void *pvParameter);
void wifi_task(void *pvParameter);

void app_wifi_start()
{
    wifi_manager_start();
    wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_connection_ok);
    wifi_manager_set_callback(WM_EVENT_STA_DISCONNECTED, &cb_connection_lost);
    xTaskCreate(&wifi_task, "wifi_task", 4096, NULL, 5, &wifiTaskHandle);
}

void cb_connection_ok(void *pvParameter)
{
    ip_event_got_ip_t *param = (ip_event_got_ip_t *)pvParameter;
    char str_ip[16];
    esp_ip4addr_ntoa(&param->ip_info.ip, str_ip, IP4ADDR_STRLEN_MAX);

    ESP_LOGI(WIFI_TAG, "Connected! IP: %s", str_ip);

    // 1. Wake up Azure IoT Task
    if (!has_notified_azure && iothub_task_handle != NULL)
    {
        ESP_LOGI(WIFI_TAG, "Waking up Azure IoT Task...");
        xTaskNotifyGive(iothub_task_handle);
        has_notified_azure = true;
    }

    // 2. Trigger BLE Start
    ESP_LOGI(WIFI_TAG, "Triggering BLE Start...");
    app_ble_valve_signal_start();

    // 3. LED Blue
    if (ledQueue != NULL)
    {
        uint8_t msg = 'B';
        xQueueSend(ledQueue, &msg, 0);
    }
}

void cb_connection_lost(void *pvParameter)
{
    if (pvParameter != NULL)
    {
        wifi_event_sta_disconnected_t *wifi_event = (wifi_event_sta_disconnected_t *)pvParameter;
        ESP_LOGW(WIFI_TAG, "WiFi Disconnected. Reason: %d", wifi_event->reason);
    }

    if (ledQueue != NULL)
    {
        uint8_t msg = 'R';
        xQueueSend(ledQueue, &msg, 0);
    }
}

void wifi_task(void *pvParameter)
{
    (void)pvParameter;
    if (ledQueue != NULL)
    {
        uint8_t msg = 'R';
        xQueueSend(ledQueue, &msg, 0);
    }
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    vTaskDelete(NULL);
}