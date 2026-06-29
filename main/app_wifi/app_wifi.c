#include "app_wifi.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "net_status/net_status.h"
#include "esp_wifi.h"

#include "app_iothub.h"
#include "app_ble_valve.h"
#include "provisioning_manager/provisioning_manager.h"
#include "hub_identity/hub_identity.h"

TaskHandle_t wifiTaskHandle = NULL;
static bool has_notified_azure = false;

void cb_connection_ok(void *pvParameter);
void cb_connection_lost(void *pvParameter);
void wifi_task(void *pvParameter);

void app_wifi_start()
{
    /* Override the default AP SSID with a unique name derived from MAC.
     * hub_identity_init() must have been called first. */
    const char *sid = hub_identity_get_short_id();
    snprintf((char *)wifi_settings.ap_ssid, MAX_SSID_SIZE, "WiFi-Hub-%s", sid);
    ESP_LOGI(WIFI_TAG, "AP SSID: %s", (char *)wifi_settings.ap_ssid);

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

    // 2. BLE will be started by IoT Hub task after provisioning is checked
    // No BLE start here - provisioning manager initializes after this callback

    // 3. Network LED -> "connecting" (beat blue). The MQTT handler promotes it
    //    to "connected" (ramp blue) once the IoT Hub session is up.
    net_status_set_wifi(true);

    // 4. Restart MQTT if it was stopped while STA was down. No-op on first connect
    //    (the client isn't created until the IoT Hub task runs after provisioning).
    iothub_resume_mqtt();
}

void cb_connection_lost(void *pvParameter)
{
    if (pvParameter != NULL)
    {
        wifi_event_sta_disconnected_t *wifi_event = (wifi_event_sta_disconnected_t *)pvParameter;
        ESP_LOGW(WIFI_TAG, "WiFi Disconnected. Reason: %d", wifi_event->reason);
    }

    // Network LED -> "no internet" (ramp red). This also clears the MQTT flag
    // inside net_status so a later reconnect shows "connecting" first.
    net_status_set_wifi(false);

    // Stop the MQTT client so it doesn't thrash TLS handshakes (fragmenting the
    // heap the SoftAP captive portal needs) while STA is down / in AP mode.
    iothub_suspend_mqtt();
}

void wifi_task(void *pvParameter)
{
    (void)pvParameter;
    // Initial "no internet" state is latched by net_status_init() at boot.
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    vTaskDelete(NULL);
}