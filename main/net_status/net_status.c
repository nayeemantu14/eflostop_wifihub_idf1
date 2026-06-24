#include "net_status.h"
#include "rgb.h"            /* ledQueue + LED_CMD_* codes */

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"  /* SemaphoreHandle_t — NOT pulled in by rgb.h */
#include "freertos/queue.h"
#include "esp_log.h"

static const char *NET_TAG = "NET_STATUS";

static SemaphoreHandle_t s_mutex = NULL;
static bool s_wifi_up = false;   /* STA has an IP            */
static bool s_mqtt_up = false;   /* Azure IoT Hub MQTT up    */

/* Derive the LED command from the current (wifi, mqtt) truth and push it to the
 * led_task queue. Caller must hold s_mutex. WiFi is checked first, so the
 * unreachable (wifi=false, mqtt=true) combination still maps to NO_INTERNET. */
static void net_status_apply_locked(void)
{
    uint8_t code;
    if (!s_wifi_up) {
        code = LED_CMD_NO_INTERNET;   /* ramp red  */
    } else if (!s_mqtt_up) {
        code = LED_CMD_CONNECTING;    /* beat blue */
    } else {
        code = LED_CMD_CONNECTED;     /* ramp blue */
    }
    if (ledQueue != NULL) {
        xQueueSend(ledQueue, &code, 0);
    }
}

void net_status_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    s_wifi_up = false;
    s_mqtt_up = false;
    if (s_mutex != NULL) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        net_status_apply_locked();   /* boot state: no internet (ramp red) */
        xSemaphoreGive(s_mutex);
    }
}

void net_status_set_wifi(bool up)
{
    if (s_mutex == NULL) {
        return;   /* setter raced ahead of net_status_init() — ignore */
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_wifi_up = up;
    if (!up) {
        /* WiFi down => the MQTT/TLS session is gone regardless of whether
         * esp-mqtt has fired MQTT_EVENT_DISCONNECTED yet. Clear it so a later
         * WiFi-restore shows CONNECTING (blue beat) until MQTT actually
         * re-establishes, instead of a false CONNECTED (blue ramp). */
        s_mqtt_up = false;
    }
    net_status_apply_locked();
    bool w = s_wifi_up, m = s_mqtt_up;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(NET_TAG, "wifi=%d mqtt=%d", (int)w, (int)m);
}

void net_status_set_mqtt(bool up)
{
    if (s_mutex == NULL) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_mqtt_up = up;
    net_status_apply_locked();
    bool w = s_wifi_up, m = s_mqtt_up;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(NET_TAG, "wifi=%d mqtt=%d", (int)w, (int)m);
}
