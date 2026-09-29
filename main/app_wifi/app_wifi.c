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
#include "health_engine/health_engine.h"
#include "portal_priority.h"

TaskHandle_t wifiTaskHandle = NULL;

void cb_connection_ok(void *pvParameter);
void cb_connection_lost(void *pvParameter);
void wifi_task(void *pvParameter);

/* ---- Portal priority window --------------------------------------------------------------
 * While the Wi-Fi setup portal is up and NO Wi-Fi credentials are saved (first setup, or
 * after the 10 s reset erased them), BLE scanning is paused so the SoftAP gets the radio.
 *
 * Why: the ESP32-S3 has one 2.4 GHz radio, and under software coexistence Bluetooth gets it
 * while the STA is idle. 2.1.4 starts BLE at boot, and its continuous 1M + Coded leak scan (on
 * a valve hub also the valve hunt and its 100 %-duty connect initiator) left the SoftAP too
 * little air time: on the 2026-09-29 bench a phone saw the SSID but could not join, and no
 * DHCP lease was ever given. 2.1.3 never started BLE in AP mode, so its portal had the radio.
 *
 * In the window the leak scanner and the valve hunt start nothing new, and each cancels its
 * own scan and connect attempt on its own task (both read the flag at every start: at boot,
 * START_AP and the BLE start come in no fixed order). NimBLE stays initialised, and a valve
 * already linked stays linked, with its commands. The health engine holds the BLE sensors'
 * timeouts meanwhile (health_set_ble_scan_paused()). No time cap (product decision): the
 * window lasts until Wi-Fi is set up (the STA gets an IP) or the AP stops.
 *
 * NOT for the fallback AP that wifi_manager opens after failed retries while credentials are
 * still saved (router outage): that is the field case BLE-from-boot leak protection is for,
 * so BLE keeps scanning there.
 *
 * Every transition runs on the wifi_manager task (the START_AP / STOP_AP callbacks and
 * cb_connection_ok), which is therefore the flag's only writer. They only set flags: nothing
 * here blocks, calls provisioning or touches NimBLE. */
static volatile bool s_portal_priority = false;

/* For the window the wifi_manager task runs at PORTAL_TASK_PRIORITY: above the app tasks (5),
 * far below lwIP (18) and the Wi-Fi and BT tasks (20-23). Insurance only: the portal was short
 * of radio time, not CPU. Only this task is raised. The HTTP and DNS server tasks ("httpd",
 * "dns_server") keep their priority: their handles are private to the managed component, and
 * the one lookup by name, xTaskGetHandle(), is not linked in this image and is IRAM-resident
 * (CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH is off). Linking it would add about 0.4 KB of
 * IRAM, which on the ESP32-S3 moves the IRAM/DRAM split up by 512 B of heap. */
#define PORTAL_TASK_PRIORITY 8
static TaskHandle_t s_wm_task = NULL;   // the raised wifi_manager task; NULL = none raised
static UBaseType_t  s_wm_prio = 0;      // its priority before the raise

bool app_wifi_portal_priority_active(void)
{
    return s_portal_priority;
}

// wifi_manager task only (see above). The health hold goes on before the flag, and its resume
// is stamped before the flag clears, so it always covers the time the scans are paused.
static void portal_priority_open(void)
{
    if (s_portal_priority)
        return;
    health_set_ble_scan_paused(true);
    s_portal_priority = true;
    ESP_LOGW(WIFI_TAG, "portal priority ON (no Wi-Fi credentials) - BLE scanning paused");

    // The callbacks run on the wifi_manager task itself, so it raises its own priority.
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    UBaseType_t prio = uxTaskPriorityGet(self);
    if (prio < PORTAL_TASK_PRIORITY)
    {
        s_wm_task = self;
        s_wm_prio = prio;
        vTaskPrioritySet(self, PORTAL_TASK_PRIORITY);
    }
    ESP_LOGI(WIFI_TAG, "portal priority: wifi_manager task prio %u -> %u (httpd, dns_server not raised)",
             (unsigned)prio, (unsigned)uxTaskPriorityGet(self));
}

static void portal_priority_close(const char *reason)
{
    if (!s_portal_priority)
        return;
    health_set_ble_scan_paused(false);   // stamps the resume: each BLE sensor's timeout restarts
    s_portal_priority = false;
    // Before anything else the caller does: cb_connection_ok's MQTT and iothub work runs at the
    // task's own priority again.
    if (s_wm_task != NULL)
    {
        vTaskPrioritySet(s_wm_task, s_wm_prio);
        s_wm_task = NULL;
    }
    ESP_LOGI(WIFI_TAG, "portal priority OFF (%s) - BLE scanning resumed", reason);
}

// WM_ORDER_START_AP (wifi_manager task), once the SoftAP, HTTP and DNS servers are up. The STA
// config is what LOAD_AND_RESTORE read from NVS (all zero when nothing is saved), or what a
// requested disconnect zeroed and saved just before sending this START_AP. This task writes
// it; the portal's POST handler writes it too, but only once the AP is already up.
static void cb_ap_started(void *pvParameter)
{
    (void)pvParameter;
    const wifi_config_t *sta = wifi_manager_get_wifi_sta_config();
    if (sta == NULL || sta->sta.ssid[0] == '\0')
        portal_priority_open();
    else if (!s_portal_priority)
        ESP_LOGI(WIFI_TAG, "SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on");
}

// WM_ORDER_STOP_AP (wifi_manager task). wifi_manager runs it only with the STA connected.
static void cb_ap_stopped(void *pvParameter)
{
    (void)pvParameter;
    portal_priority_close("AP stopped");
}

// SoftAP station join/leave, so the bench can see a phone associate (wifi_manager silences the
// driver's own "wifi" log). MAC, AID and reason only: none of it is a credential.
// Runs on the default event loop task.
static void ap_station_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (data == NULL)
        return;
    if (id == WIFI_EVENT_AP_STACONNECTED)
    {
        const wifi_event_ap_staconnected_t *e = (const wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(WIFI_TAG, "SoftAP: station %02X:%02X:%02X:%02X:%02X:%02X joined, AID=%u",
                 e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5],
                 (unsigned)e->aid);
    }
    else if (id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        const wifi_event_ap_stadisconnected_t *e = (const wifi_event_ap_stadisconnected_t *)data;
        ESP_LOGI(WIFI_TAG, "SoftAP: station %02X:%02X:%02X:%02X:%02X:%02X left, AID=%u, reason=%u",
                 e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5],
                 (unsigned)e->aid, (unsigned)e->reason);
    }
}

void app_wifi_start()
{
    /* Override the default AP SSID with a unique name derived from MAC.
     * hub_identity_init() must have been called first. */
    const char *sid = hub_identity_get_short_id();
    snprintf((char *)wifi_settings.ap_ssid, MAX_SSID_SIZE, "WiFi-Hub-%s", sid);
    ESP_LOGI(WIFI_TAG, "AP SSID: %s", (char *)wifi_settings.ap_ssid);

    wifi_manager_start();
    // The portal priority window's callbacks first: with no credentials saved, START_AP comes
    // about 0.7 s after the start (network and Wi-Fi init, the HTTP server), while these calls
    // take microseconds. Not before the start: wifi_manager_start() allocates the callback
    // table, and a callback set before it is silently dropped.
    wifi_manager_set_callback(WM_ORDER_START_AP, &cb_ap_started);
    wifi_manager_set_callback(WM_ORDER_STOP_AP, &cb_ap_stopped);
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

    // Wi-Fi is set up: close the portal priority window, so BLE scanning resumes. A no-op
    // when the window is not open (a router reconnect, or a boot with saved credentials).
    portal_priority_close("Wi-Fi connected");

    // BLE is not started here, and never waits for Wi-Fi: iothub_task starts it at boot
    // from the provisioned device set, together with leak protection.

    // 1. Network LED -> "connecting" (beat blue). The MQTT handler promotes it
    //    to "connected" (ramp blue) once the IoT Hub session is up.
    net_status_set_wifi(true);

    // 2. Restart MQTT if it was stopped while STA was down. No-op until iothub_task has
    //    built the client (DPS done, with Wi-Fi up and a valid clock).
    iothub_resume_mqtt();

    // 3. Tell iothub_task the network is up: its loop starts SNTP and the cloud bring-up.
    //    Last, so the wake finds a reconnect's MQTT suspend already lifted.
    iothub_on_wifi_connected();
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
    // The SoftAP station log (ap_station_event_handler) is registered here, not in a
    // wifi_manager callback: registering takes the event loop's lock, which the loop holds
    // while it runs wifi_manager's handler, and that handler can block posting to the
    // wifi_manager task. The wifi_manager task creates the default loop, so a first try can
    // find none (ESP_ERR_INVALID_STATE, silent): retried every second until it is in.
    bool ap_log_on = false;
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(ap_log_on ? 5000 : 1000));
        if (!ap_log_on)
            ap_log_on = (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                    &ap_station_event_handler, NULL) == ESP_OK);
    }
    vTaskDelete(NULL);
}