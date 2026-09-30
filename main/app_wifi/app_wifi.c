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
 * already linked stays linked, with its commands. Leak protection still outranks the portal:
 * while a leak response (RMLEAK or CLOSE) is pended for an unlinked valve, the valve hunt and
 * its connect run anyway until the valve takes it (app_ble_valve.c). The health engine holds
 * the BLE sensors' and the valve's timeouts meanwhile (health_set_ble_scan_paused()), except
 * after such a leak-response hunt: the valve's hold then ends 180 s after that hunt started,
 * with setup still running (health_note_valve_leak_hunt()). No time cap before setup (product
 * decision).
 *
 * The window closes when the setup AP stops (cb_ap_stopped()), not when the STA gets its IP:
 * the phone that submitted the credentials is still on the SoftAP, which wifi_manager keeps up
 * for WIFI_MANAGER_SHUTDOWN_AP_TIMER (60 s) after the IP so the portal page can load its success
 * status, and resuming the dual-PHY scan at the IP starved the SoftAP again. BLE must never stay
 * paused while the hub is on Wi-Fi, so two nets back that up:
 *   - the STA loses that Wi-Fi before the AP stops: wifi_manager stops its AP-shutdown timer
 *     and the SoftAP stays up as a router-fallback portal with the credentials saved, which
 *     keeps BLE scanning, so cb_connection_lost() closes the window;
 *   - the window is still open PORTAL_AP_STOP_MARGIN_MS past that timer (wifi_manager starts
 *     the timer only if its AP_STARTED bit is set at the IP): wifi_task sends the STOP_AP
 *     itself (portal_priority_net()).
 *
 * NOT for the fallback AP that wifi_manager opens after failed retries while credentials are
 * still saved (router outage): that is the field case BLE-from-boot leak protection is for,
 * so BLE keeps scanning there, apart from the few-second Wi-Fi radio holds below.
 *
 * Every transition runs on the wifi_manager task (the START_AP, STOP_AP, GOT_IP and
 * STA_DISCONNECTED callbacks), which is therefore the only writer of the flag and of
 * s_setup_ok_tick; wifi_task only reads them. They only set flags: nothing here blocks, calls
 * provisioning or touches NimBLE. */
static volatile bool s_portal_priority = false;

/* The tick of the GOT_IP that set Wi-Fi up in the open window (forced non-zero), 0 = none.
 * Set by cb_connection_ok(); cleared when the window opens or closes, and by a requested
 * disconnect in the window. */
static volatile TickType_t s_setup_ok_tick = 0;

/* How long past WIFI_MANAGER_SHUTDOWN_AP_TIMER the safety net waits for the setup AP to stop on
 * its own: wifi_task checks every 5 s, so BLE resumes at most about 80 s after the IP. */
#define PORTAL_AP_STOP_MARGIN_MS 15000

/* For the window the wifi_manager task runs at PORTAL_TASK_PRIORITY: above the app tasks (5),
 * far below lwIP (18) and the Wi-Fi and BT tasks (20-23). Insurance only: the portal was short
 * of radio time, not CPU. Only this task is raised. The HTTP and DNS server tasks ("httpd",
 * "dns_server") keep their priority: their handles are private to the managed component, and
 * the one lookup by name, xTaskGetHandle(), is not linked in this image and is IRAM-resident
 * (CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH is off). Linking it would add about 0.4 KB of
 * IRAM, which on the ESP32-S3 moves the IRAM/DRAM split up by 512 B of heap.
 * Closing the window restores WIFI_MANAGER_TASK_PRIORITY, the priority wifi_manager.c creates
 * the task with, not a sampled value: uxTaskPriorityGet() returns the effective priority,
 * which can include one inherited through a mutex, and restoring that would make it the
 * task's base priority for good. */
#define PORTAL_TASK_PRIORITY 8
static TaskHandle_t s_wm_task = NULL;   // the raised wifi_manager task; NULL = none raised

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
        vTaskPrioritySet(self, PORTAL_TASK_PRIORITY);
    }
    ESP_LOGI(WIFI_TAG, "portal priority: wifi_manager task prio %u -> %u (httpd, dns_server not raised)",
             (unsigned)prio, (unsigned)uxTaskPriorityGet(self));
}

static void portal_priority_close(const char *reason)
{
    if (!s_portal_priority)
        return;
    // Stamps the resume: the sensors' and the valve's timeouts restart, the valve's hold still
    // ending 180 s after a leak-response hunt in the window if that is sooner (see the top).
    health_set_ble_scan_paused(false);
    s_portal_priority = false;
    s_setup_ok_tick = 0;
    // The raise lasts as long as the window, so cb_connection_ok()'s work (the LED, the MQTT
    // resume, the iothub wake) runs at PORTAL_TASK_PRIORITY when Wi-Fi is set up in it. That is
    // harmless: none of it spins. Where it can block, on the net_status or the MQTT control
    // mutex, the holder inherits the raised priority until it gives the mutex, so the wait is
    // no longer than at the task's own priority. The rest are flags and non-blocking queue
    // posts, and at most one esp_mqtt_client_start(), which creates the MQTT task at its own
    // priority.
    if (s_wm_task != NULL)
    {
        vTaskPrioritySet(s_wm_task, WIFI_MANAGER_TASK_PRIORITY);
        s_wm_task = NULL;
    }
    ESP_LOGI(WIFI_TAG, "portal priority OFF (%s) - BLE scanning resumed", reason);
}

/* ---- Wi-Fi radio hold ---------------------------------------------------------------------
 * While the STA is not connected, BLE scanning also stops for a few seconds around each Wi-Fi
 * scan and each STA connect attempt, so they get the radio: the window's starvation on a small
 * scale. On the 2026-09-29 bench (router off, then on again) the router-fallback AP's page never
 * got past "Scanning for networks..." with many networks in range, and the connect attempts
 * ended NO_AP_FOUND (201): beside the continuous 1M + Coded leak scan (on a valve hub also the
 * valve hunt) a Wi-Fi scan hears next to nothing, and a connect attempt starts with a scan for
 * the SSID.
 *
 * A hold is a deadline (a tick, 0 = none), never a flag, so it always ends. Each has one writer:
 *   - s_scan_until (wifi_manager task): from a Wi-Fi scan order (the portal page's GET /ap.json
 *     asks for one about every 3.8 s) until RADIO_HOLD_SCAN_TAIL_MS after its SCAN_DONE, at most
 *     RADIO_HOLD_SCAN_MS. The tail outlasts the page's next request, so BLE stays paused while
 *     the page is open and its list fills; with no page open, no scan is asked for.
 *   - s_connect_until (wifi_manager task): from a CONNECT_STA order (wifi_manager's own retries,
 *     a portal submit) until RADIO_HOLD_CONNECT_TAIL_MS after its disconnect, or its IP, at most
 *     RADIO_HOLD_CONNECT_MS.
 * None while the STA is connected (it has its air time then, and BLE never pauses for nothing):
 * app_wifi_radio_hold_active() reads false and the callbacks set nothing. None is set in the
 * portal window either, where BLE is paused already. Unlike the window: no health hold (the
 * sensors' and the valve's timeouts, minutes long, keep running through a few seconds), no
 * priority raise, and in the BLE modules no [PORTAL] lines and no valve go-red stamp. They
 * otherwise treat a hold like the window, the valve's leak-response exception included
 * (app_ble_leak.c, app_ble_valve.c). wifi_task prints each hold's start and end
 * (radio_hold_log()). The callbacks only store ticks and flags. */
#define RADIO_HOLD_SCAN_MS         6000    // a Wi-Fi scan until its SCAN_DONE (a cap: ~2 s is usual)
#define RADIO_HOLD_SCAN_TAIL_MS    4000    // after the SCAN_DONE: past the page's next request
#define RADIO_HOLD_CONNECT_MS      10000   // a connect attempt until its disconnect or IP (a cap)
#define RADIO_HOLD_CONNECT_TAIL_MS 1000    // after the attempt's disconnect
#define RADIO_HOLD_MAX_MS          RADIO_HOLD_CONNECT_MS   // the furthest deadline ever set

static volatile bool s_sta_connected = false;     // the STA has its IP; wifi_manager task only
static volatile TickType_t s_scan_until = 0;      // wifi_manager task only
static volatile TickType_t s_connect_until = 0;   // wifi_manager task only

// The deadline is set and still ahead, by at most RADIO_HOLD_MAX_MS. Without that bound a deadline
// left unchanged for 2^31 ticks (248 days at 100 Hz) would read as ahead again, for 248 days; with
// it, for at most RADIO_HOLD_MAX_MS once every 2^32 ticks.
static bool hold_running(TickType_t until, TickType_t now)
{
    int32_t left = (int32_t)(until - now);
    return until != 0 && left > 0 && left <= (int32_t)pdMS_TO_TICKS(RADIO_HOLD_MAX_MS);
}

static TickType_t hold_deadline(uint32_t ms)
{
    TickType_t t = xTaskGetTickCount() + pdMS_TO_TICKS(ms);
    return (t != 0) ? t : 1;   // 0 means "no hold"
}

bool app_wifi_radio_hold_active(void)
{
    if (s_sta_connected)
        return false;
    TickType_t now = xTaskGetTickCount();
    return hold_running(s_scan_until, now) || hold_running(s_connect_until, now);
}

// For the log: the hold that is on, the connect attempt's first.
static const char *radio_hold_reason(TickType_t now)
{
    return hold_running(s_connect_until, now) ? "connect attempt" : "Wi-Fi scan";
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
    {
        s_setup_ok_tick = 0;   // also a re-open after the portal's forget: no setup in it yet
        portal_priority_open();
    }
    else if (!s_portal_priority)
        ESP_LOGI(WIFI_TAG, "SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on");
}

// WM_ORDER_STOP_AP (wifi_manager task). wifi_manager runs it only with the STA connected. This
// is where Wi-Fi setup ends the window: about 60 s after the IP, from wifi_manager's own timer,
// or from the safety net below.
static void cb_ap_stopped(void *pvParameter)
{
    (void)pvParameter;
    portal_priority_close("AP stopped");
}

// WM_ORDER_CONNECT_STA (wifi_manager task), after wifi_manager's esp_wifi_connect(): a connect
// attempt started, unless the STA already has its IP (wifi_manager then connects nothing).
static void cb_connect_sta(void *pvParameter)
{
    (void)pvParameter;
    if (s_sta_connected)
        return;
    if (!s_portal_priority)
        s_connect_until = hold_deadline(RADIO_HOLD_CONNECT_MS);
}

// WM_ORDER_START_WIFI_SCAN (wifi_manager task): the portal page asked for the network list. Also
// when wifi_manager skipped the scan (one already running, or its start failed): the cap ends it.
static void cb_scan_start(void *pvParameter)
{
    (void)pvParameter;
    if (!s_sta_connected && !s_portal_priority)
        s_scan_until = hold_deadline(RADIO_HOLD_SCAN_MS);
}

// WM_EVENT_SCAN_DONE (wifi_manager task), once the list is rebuilt: only the tail is left.
static void cb_scan_done(void *pvParameter)
{
    (void)pvParameter;
    if (hold_running(s_scan_until, xTaskGetTickCount()))
        s_scan_until = hold_deadline(RADIO_HOLD_SCAN_TAIL_MS);
}

/* Safety net, on wifi_task: the window is open, Wi-Fi was set up in it, and the setup AP has not
 * stopped PORTAL_AP_STOP_MARGIN_MS past WIFI_MANAGER_SHUTDOWN_AP_TIMER. wifi_manager starts no
 * AP-shutdown timer if its AP_STARTED bit was clear at the IP, and its xTimerStart() does not
 * wait for room in the timer queue. Sends the STOP_AP once per GOT_IP tick (*stop_sent_for,
 * wifi_task's own). On the wifi_manager task it stops the AP if the STA is connected, and
 * cb_ap_stopped() closes the window; if the STA is not, its disconnect reaches
 * cb_connection_lost(), which closes the window or clears the tick. Nothing is written here:
 * every window write stays on the wifi_manager task. */
static void portal_priority_net(TickType_t *stop_sent_for)
{
    TickType_t setup = s_setup_ok_tick;
    if (setup == 0 || setup == *stop_sent_for || !s_portal_priority)
        return;
    TickType_t elapsed = xTaskGetTickCount() - setup;
    if (elapsed <= pdMS_TO_TICKS(WIFI_MANAGER_SHUTDOWN_AP_TIMER + PORTAL_AP_STOP_MARGIN_MS))
        return;
    *stop_sent_for = setup;
    ESP_LOGW(WIFI_TAG, "portal priority: setup AP still up %u s after Wi-Fi connected - stopping it",
             (unsigned)(elapsed / configTICK_RATE_HZ));
    wifi_manager_send_message(WM_ORDER_STOP_AP, NULL);
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
    // The Wi-Fi radio hold's. Nothing else registers these three.
    wifi_manager_set_callback(WM_ORDER_CONNECT_STA, &cb_connect_sta);
    wifi_manager_set_callback(WM_ORDER_START_WIFI_SCAN, &cb_scan_start);
    wifi_manager_set_callback(WM_EVENT_SCAN_DONE, &cb_scan_done);
    xTaskCreate(&wifi_task, "wifi_task", 4096, NULL, 5, &wifiTaskHandle);
}

void cb_connection_ok(void *pvParameter)
{
    ip_event_got_ip_t *param = (ip_event_got_ip_t *)pvParameter;
    char str_ip[16];
    esp_ip4addr_ntoa(&param->ip_info.ip, str_ip, IP4ADDR_STRLEN_MAX);

    ESP_LOGI(WIFI_TAG, "Connected! IP: %s", str_ip);

    // The STA has its air time now: no Wi-Fi radio hold, and BLE resumes at once.
    s_sta_connected = true;
    s_scan_until = 0;
    s_connect_until = 0;

    // Wi-Fi is set up, but the portal priority window stays open until the setup AP stops
    // (cb_ap_stopped()): the phone that submitted the credentials is still on the SoftAP and
    // loads the portal's success status from it. Only the time of this IP is kept, for
    // cb_connection_lost() and the safety net. Nothing when the window is not open (a router
    // reconnect, or a boot with saved credentials).
    if (s_portal_priority)
    {
        TickType_t now = xTaskGetTickCount();
        s_setup_ok_tick = (now != 0) ? now : 1;   // 0 means "no setup in this window"
        ESP_LOGI(WIFI_TAG, "portal priority: Wi-Fi connected - BLE scanning stays paused until the setup AP stops (about %d s)",
                 WIFI_MANAGER_SHUTDOWN_AP_TIMER / 1000);
    }

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

    // The link was lost, or a connect attempt ended: its radio hold keeps only its tail.
    s_sta_connected = false;
    if (hold_running(s_connect_until, xTaskGetTickCount()))
        s_connect_until = hold_deadline(RADIO_HOLD_CONNECT_TAIL_MS);

    // The STA lost the Wi-Fi it was set up with in this window, before the setup AP stopped.
    // wifi_manager has just stopped its AP-shutdown timer, so the SoftAP stays up as a
    // router-fallback portal with the credentials saved, which keeps BLE scanning: close the
    // window, before the MQTT stop below (it can wait). Not after a requested disconnect (the
    // 10 s reset, the portal's forget): wifi_manager zeroes the STA config before this callback
    // and sends START_AP next, so the window stays open for the next setup.
    if (s_portal_priority && s_setup_ok_tick != 0)
    {
        const wifi_config_t *sta = wifi_manager_get_wifi_sta_config();
        if (sta != NULL && sta->sta.ssid[0] != '\0')
            portal_priority_close("Wi-Fi lost after setup");
        else
            s_setup_ok_tick = 0;
    }

    // Network LED -> "no internet" (ramp red). This also clears the MQTT flag
    // inside net_status so a later reconnect shows "connecting" first.
    net_status_set_wifi(false);

    // Stop the MQTT client so it doesn't thrash TLS handshakes (fragmenting the
    // heap the SoftAP captive portal needs) while STA is down / in AP mode.
    iothub_suspend_mqtt();
}

// wifi_task's own state for the Wi-Fi radio hold's log lines.
typedef struct
{
    bool hold_on;            // a hold's ON line is printed, its OFF line not yet
    TickType_t hold_since;   // when that ON line was printed
} wifi_task_state_t;

// Prints each Wi-Fi radio hold's start and end as wifi_task sees them. It looks on every pass,
// every second while the STA is down (the only time a hold can run), so a line is at most about
// a second late, and holds that chain (an open portal page's scans) print one pair.
static void radio_hold_log(wifi_task_state_t *st)
{
    bool on = app_wifi_radio_hold_active();
    if (on == st->hold_on)
        return;
    TickType_t now = xTaskGetTickCount();
    st->hold_on = on;
    if (on)
    {
        st->hold_since = now;
        ESP_LOGI(WIFI_TAG, "Wi-Fi radio hold ON (%s) - BLE scanning paused", radio_hold_reason(now));
    }
    else
        ESP_LOGI(WIFI_TAG, "Wi-Fi radio hold OFF after %u s - BLE scanning resumed",
                 (unsigned)((now - st->hold_since + configTICK_RATE_HZ / 2) / configTICK_RATE_HZ));
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
    // Each pass also runs the portal priority window's safety net (portal_priority_net()) and the
    // Wi-Fi radio hold's log (radio_hold_log()): every second while the STA is down or a hold's
    // OFF line is still due, else every 5 s.
    bool ap_log_on = false;
    TickType_t stop_sent_for = 0;   // the GOT_IP tick the net already sent a STOP_AP for
    wifi_task_state_t st = { 0 };
    while (1)
    {
        bool fast = !ap_log_on || !s_sta_connected || st.hold_on;
        vTaskDelay(pdMS_TO_TICKS(fast ? 1000 : 5000));
        if (!ap_log_on)
            ap_log_on = (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                    &ap_station_event_handler, NULL) == ESP_OK);
        portal_priority_net(&stop_sent_for);
        radio_hold_log(&st);
    }
    vTaskDelete(NULL);
}