#include "app_wifi.h"
#include "wifi_manager.h"
#include "http_app.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
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
 * so BLE keeps scanning there, apart from the Wi-Fi radio holds below (a few seconds each, or
 * up to 8 s at a time while a setup page is in use, about 20 s once around its Connect).
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
 * "dns_server") keep their priority: their handles are private to the portal component, and
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
 * the SSID. The scan holds of a setup page in use chain, though, with BLE windows between them
 * (below).
 *
 * A hold is a deadline (a tick, 0 = none), never a flag, so it always ends. Each has one writer:
 *   - s_scan_until (wifi_manager task): from a Wi-Fi scan order (the portal page's GET /ap.json
 *     asks for one about every 3.8 s while it is used) until RADIO_HOLD_SCAN_TAIL_MS after its
 *     SCAN_DONE, at most RADIO_HOLD_SCAN_MS, and never into the page's next BLE window. The tail
 *     outlasts the page's next request, so the holds chain while the page is used and its list
 *     fills; with no page in use, no scan is asked for.
 *   - s_connect_until (wifi_manager task): from a CONNECT_STA order until its disconnect, or its
 *     IP: at most RADIO_HOLD_CONNECT_MS, or RADIO_HOLD_SUBMIT_MS for a portal submit
 *     (s_connect_submit, below).
 *   - s_retry_until (wifi_task): the router retry's, from RADIO_HOLD_RETRY_LEAD_MS before its
 *     connect order, RADIO_HOLD_RETRY_MS in all, its attempt's start included (router_retry()).
 * A connect attempt's hold is capped at 2.5 s, the router retry's lead included, which covers the
 * attempt's scan for the SSID and its join; the rest of the attempt runs with BLE on. With the
 * leak scanner's 500 ms loop, which can resume its scan up to half a second late, BLE is then off
 * about 3 s at most: less than the 4 s burst a BLE leak sensor sends when it gets wet. Holds never
 * run back to back either, a page's scans and its submit apart: none starts while another is on
 * or ended less than RADIO_HOLD_GAP_MS ago (radio_hold_near()), so BLE hears at least about 1 s
 * between two, and at least about 1 s of that burst. A connect attempt started then takes no hold
 * of its own: the router retry's covers the retry's attempt. One that starts in the gap just after
 * a hold ended (wifi_manager's own retry just after the page closed, say) runs with BLE on: the
 * price of keeping holds apart. The valve hunt's 1 s poll can still reach into the start of the
 * router retry's attempt.
 * A page's BLE windows. The holds of a page in use form a chain from its first (s_chain_start),
 * which lasts as long as the page keeps asking (page_open()). Its schedule runs from that first
 * hold in periods of RADIO_HOLD_PERIOD_MS whose last RADIO_HOLD_WINDOW_MS are BLE's window, with
 * no hold of any kind but a portal submit's (app_wifi_radio_hold_active()). No scan hold reaches
 * into a window (scan_hold_set()), and a scan the page asks for in one, or less than
 * RADIO_HOLD_QUIET_MS before one, is stopped at once (cb_scan_start()): no Wi-Fi scan shares a
 * window with BLE, and the page's list keeps what the last full scan found. BLE is then off 8 s
 * at a time at most, and listens at least 4 s of every 12. A wet BLE sensor's heartbeat, a 2.5 s
 * burst every 15 s, meets the 12 s periods in the same four phases, 3 s apart, every 60 s, and in
 * one of them at least 1 s of the burst (2 or more of its adverts, 0.31-0.44 s apart) falls in a
 * window after the leak scanner's restart (up to 0.5 s): with a page in use the sensor is heard
 * at least about every 60 s. A dry sensor's 100 s heartbeat has no such cycle against the periods
 * (it can miss every window for as long as a chain lasts), so after RADIO_HOLD_LONG_AFTER_MS of a
 * chain BLE listens RADIO_HOLD_LONG_MS in one go, longer than that heartbeat and its burst, before
 * the schedule starts over: a page that keeps asking leaves a dry sensor unheard for three of its
 * heartbeats (about 300 s) at most, half the health engine's 600 s. The page (code.js) stops
 * asking 60 s after it was last used, so a chain normally ends long before that; the long listen
 * is for a copy that does not (an older page, another client).
 * A portal submit: a CONNECT_STA that is not the router retry's (s_retry_sent) while the page is
 * open is the user's Connect. Its hold starts whatever else holds, and runs through the windows
 * until its disconnect or its IP, RADIO_HOLD_SUBMIT_MS at most: the user is waiting, and in a
 * crowded band an attempt beside the leak scan ends NO_AP_FOUND. BLE can then be off about 20 s
 * once (a period's holds, its window, the next period's holds). wifi_manager's own retries come
 * right after a link loss, normally with no page open, and keep the 2.5 s cap; one that comes
 * while a page is open counts as a submit.
 * None while the STA is connected (it has its air time then, and BLE never pauses for nothing):
 * app_wifi_radio_hold_active() reads false and the callbacks set nothing. None is set in the
 * portal window either, where BLE is paused already. Unlike the window: no health hold (the
 * sensors' and the valve's timeouts, minutes long, keep running through about 20 s at most), no
 * priority raise, and in the BLE modules no [PORTAL] lines and no valve go-red stamp. They
 * otherwise treat a hold like the window, the valve's leak-response exception included
 * (app_ble_leak.c, app_ble_valve.c). wifi_task prints each hold's start and end, and a page's
 * chain as a whole (radio_hold_log()). The callbacks only store ticks and flags, and stop a
 * page's scan. */
#define RADIO_HOLD_SCAN_MS         6000    // a Wi-Fi scan until its SCAN_DONE (a cap: ~2 s is usual)
#define RADIO_HOLD_SCAN_TAIL_MS    4000    // after the SCAN_DONE: past the page's next request
#define RADIO_HOLD_CONNECT_MS      2500    // a connect attempt until its disconnect or IP (a cap)
#define RADIO_HOLD_SUBMIT_MS       7000    // a portal submit's attempt, the same way (a cap)
#define RADIO_HOLD_RETRY_LEAD_MS   500     // the router retry: BLE paused this long before its order
#define RADIO_HOLD_RETRY_MS        RADIO_HOLD_CONNECT_MS   // the router retry's, its lead included
#define RADIO_HOLD_GAP_MS          1500    // no new hold this soon after one (a page's scans apart)
#define RADIO_HOLD_PERIOD_MS       12000   // a page's chain: its schedule's period, from its first hold
#define RADIO_HOLD_WINDOW_MS       4000    // the last this long of each period: BLE's window
#define RADIO_HOLD_QUIET_MS        2000    // no page scan starts this soon before a window
#define RADIO_HOLD_LONG_AFTER_MS   180000  // 15 periods, then BLE listens in one go ...
#define RADIO_HOLD_LONG_MS         105000  // ... this long: past a dry sensor's 100 s heartbeat
#define RADIO_HOLD_MAX_MS          RADIO_HOLD_SUBMIT_MS    // the furthest deadline ever set
#define PAGE_OPEN_MS               10000   // the page counts as open this long after a scan order

_Static_assert(RADIO_HOLD_LONG_AFTER_MS % RADIO_HOLD_PERIOD_MS == 0,
               "the long listen follows a whole period, its window included");

static volatile bool s_sta_connected = false;     // the STA has its IP; wifi_manager task only
static volatile TickType_t s_scan_until = 0;      // wifi_manager task only
static volatile TickType_t s_connect_until = 0;   // wifi_manager task only
static volatile bool s_connect_submit = false;    // that hold is a portal submit's; wifi_manager task only
static volatile TickType_t s_retry_until = 0;     // wifi_task only
static volatile TickType_t s_chain_start = 0;     // a page's chain's first hold, 0 = none; wifi_manager task only
static volatile TickType_t s_scan_asked = 0;      // the last scan order; wifi_manager task only
static volatile uint16_t s_page_stops = 0;        // the chain's scans stopped for BLE; wifi_manager task only
static volatile uint8_t s_submits = 0;            // portal submits, for the log; wifi_manager task only
static volatile uint8_t s_retry_sent = 0;         // router retries sent; wifi_task only
static volatile uint8_t s_retry_seen = 0;         // those taken by cb_connect_sta(); wifi_manager task only

// The deadline is set and still ahead, by at most RADIO_HOLD_MAX_MS. Without that bound a deadline
// left unchanged for 2^31 ticks (248 days at 100 Hz) would read as ahead again, for 248 days; with
// it, for at most RADIO_HOLD_MAX_MS once every 2^32 ticks.
static bool hold_running(TickType_t until, TickType_t now)
{
    int32_t left = (int32_t)(until - now);
    return until != 0 && left > 0 && left <= (int32_t)pdMS_TO_TICKS(RADIO_HOLD_MAX_MS);
}

// The deadline is still ahead (bounded as above) or passed less than RADIO_HOLD_GAP_MS ago.
static bool hold_near(TickType_t until, TickType_t now)
{
    int32_t left = (int32_t)(until - now);
    return until != 0 && left > -(int32_t)pdMS_TO_TICKS(RADIO_HOLD_GAP_MS) &&
           left <= (int32_t)pdMS_TO_TICKS(RADIO_HOLD_MAX_MS);
}

// A hold is on or ended less than RADIO_HOLD_GAP_MS ago: no new one starts (see above). A stale
// read of the other task's deadline costs one hold, or one hold not taken.
static bool radio_hold_near(TickType_t now)
{
    return hold_near(s_scan_until, now) || hold_near(s_connect_until, now) ||
           hold_near(s_retry_until, now);
}

static TickType_t hold_deadline(uint32_t ms)
{
    TickType_t t = xTaskGetTickCount() + pdMS_TO_TICKS(ms);
    return (t != 0) ? t : 1;   // 0 means "no hold"
}

// The portal page is open: it asked for a scan less than PAGE_OPEN_MS ago. It asks about every
// 3.8 s while it is used, at once when it is used again, and nothing while it is idle (60 s after
// it was last used), hidden or closed (code.js). A stale order reads as recent again for
// PAGE_OPEN_MS once every 2^32 ticks, which only defers a router retry or joins a chain.
static bool page_open(TickType_t now)
{
    TickType_t asked = s_scan_asked;
    return asked != 0 && now - asked < pdMS_TO_TICKS(PAGE_OPEN_MS);
}

// Ticks into the schedule of the chain that started at start (its periods, then its long listen).
// Unsigned, so a chain that runs for 2^32 ticks (497 days at 100 Hz) only shifts its schedule once.
static TickType_t chain_phase(TickType_t start, TickType_t now)
{
    return (now - start) % pdMS_TO_TICKS(RADIO_HOLD_LONG_AFTER_MS + RADIO_HOLD_LONG_MS);
}

// Ticks from now to that chain's next BLE window: 0 in one (the last RADIO_HOLD_WINDOW_MS of a
// period, or the long listen).
static TickType_t ble_window_in(TickType_t start, TickType_t now)
{
    TickType_t x = chain_phase(start, now);
    if (x >= pdMS_TO_TICKS(RADIO_HOLD_LONG_AFTER_MS))
        return 0;
    TickType_t p = x % pdMS_TO_TICKS(RADIO_HOLD_PERIOD_MS);
    TickType_t hold = pdMS_TO_TICKS(RADIO_HOLD_PERIOD_MS - RADIO_HOLD_WINDOW_MS);
    return (p >= hold) ? 0 : hold - p;
}

// wifi_manager task: a page's scan hold, ms ahead but not into its chain's next BLE window.
static void scan_hold_set(TickType_t now, uint32_t ms)
{
    TickType_t ahead = pdMS_TO_TICKS(ms);
    TickType_t start = s_chain_start;
    if (start != 0)
    {
        TickType_t to_window = ble_window_in(start, now);
        if (to_window < ahead)
            ahead = to_window;
    }
    TickType_t until = now + ahead;
    s_scan_until = (until != 0) ? until : 1;
}

bool app_wifi_radio_hold_active(void)
{
    if (s_sta_connected)
        return false;
    TickType_t now = xTaskGetTickCount();
    // A portal submit's attempt holds through the page's BLE windows (see above).
    if (s_connect_submit && hold_running(s_connect_until, now))
        return true;
    TickType_t start = s_chain_start;
    if (start != 0 && page_open(now) && ble_window_in(start, now) == 0)
        return false;
    return hold_running(s_scan_until, now) || hold_running(s_connect_until, now) ||
           hold_running(s_retry_until, now);
}

// For the log: the hold that is on, the router retry's first, then the connect attempt's.
static const char *radio_hold_reason(TickType_t now)
{
    if (hold_running(s_retry_until, now))
        return "router retry";
    if (hold_running(s_connect_until, now))
        return s_connect_submit ? "portal submit" : "connect attempt";
    return "Wi-Fi scan";
}

/* ---- Router retry --------------------------------------------------------------------------
 * wifi_manager retries a lost router 3 times, then opens the SoftAP as a router-fallback portal,
 * and its START_AP stops the retry timer (the LOCAL PATCH in wifi_manager.c, against a scan race):
 * nothing tried the router again, and on the 2026-09-29 bench the hub never rejoined once the
 * router was back. wifi_manager can also leave the STA idle with the SoftAP down: after a user
 * connect that fails (a portal submit, or this retry's own, below) it starts no retry timer and no
 * AP. So whenever the STA is down with credentials in its config, outside the portal window
 * (router_fallback()), SoftAP up or not, wifi_task asks wifi_manager for a connect once no attempt
 * has started or ended for ROUTER_RETRY_MS and none is in flight; never before wifi_manager's own
 * first attempt (its restore at boot, or a portal submit: s_attempt_tick still 0). BLE is paused
 * RADIO_HOLD_RETRY_LEAD_MS ahead (s_retry_until), so the leak scanner (500 ms loop) is off the
 * radio when the connect's scan for the router starts; the valve hunt (1 s poll) may still be on.
 * That hold lasts RADIO_HOLD_RETRY_MS in all, the attempt's included (the radio hold above). No
 * retry starts in a page's BLE window, or so close before one that its hold would reach into it,
 * where its attempt would run with BLE on: it waits, 6.5 s at most (RADIO_HOLD_WINDOW_MS +
 * RADIO_HOLD_RETRY_MS), or out a chain's long listen, after the page deferral below too. The order
 * is wifi_manager_connect_async() (CONNECTION_REQUEST_USER): a failure starts no retry timer and
 * no AP, it only marks the portal's status failed (UPDATE_FAILED_ATTEMPT), and an IP saves the
 * config only if it changed. The config tried is the one in RAM: the saved one, unless a portal
 * submit replaced it, one that failed or one wifi_manager ignored because the STA was connected
 * (one the driver refused is dropped: wifi_manager's C2c): then what was typed, until a reboot
 * reloads the saved one. So after a submit that does not match the router (a mistyped password,
 * another SSID) the retries fail, router back or not, until a reboot or a new submit; with the
 * SoftAP down, with no SoftAP to submit from, until a reboot (or the 10 s reset).
 * wifi_manager's own retries, its first three after a link loss with the SoftAP down, come about
 * every 10 s, so the 30 s rule adds none beside them. With the SoftAP up (the fallback portal, or
 * the setup AP left up by "Wi-Fi lost after setup") it starts none (its C5), and this retry is
 * the only one.
 *
 * Counted from an attempt's end too, not only its start: every lost-link disconnect with the
 * SoftAP down arms wifi_manager's one-shot retry timer (WIFI_MANAGER_RETRY_TIMER, 5 s) before our
 * STA_DISCONNECTED callback runs, and only START_AP stops it. A link loss can come long after
 * the last attempt started (a link up for minutes), and a retry sent then would still be
 * connecting when that timer's CONNECT_STA arrives, which then fails to start (below).
 * ROUTER_RETRY_MS after the last disconnect the timer has fired (its attempt then counts) or
 * START_AP has stopped it. A retry therefore comes ROUTER_RETRY_MS after the previous one
 * failed, on the fallback AP or with the SoftAP down: about every 33-36 s.
 *
 * Never a second connect while one is in flight: wifi_manager's CONNECT_STA would then call
 * esp_wifi_set_config() on a connecting STA, which fails ("sta is connecting, cannot set config").
 * That was an ESP_ERROR_CHECK and a reboot; since its C2c wifi_manager counts the request as a
 * failed attempt that did not start (a submit's status reads failed, and the STA_DISCONNECTED
 * callback runs with WIFI_REASON_CONNECTION_FAIL). A portal submit (POST /connect.json) sends its
 * own, which the app cannot see coming, so no retry is sent while the portal page is open
 * (page_open(): it asked for a scan less than PAGE_OPEN_MS ago, as it does about every 3.8 s while
 * it is used, and at once when it is used again; not the scan hold, which a BLE window ends while
 * the page is still open), for at most ROUTER_RETRY_PAGE_MAX_MS since the last attempt, so a page
 * that keeps asking cannot keep the hub off its router. The retry looks at the page again after
 * its lead, and the page holds a Connect until it has been asking for 8 s, longer than an attempt
 * the retry may have started just before (code.js). A submit can still land in a retry's attempt,
 * as in one of wifi_manager's own retries, and then reads failed: after a retry that waited out
 * ROUTER_RETRY_PAGE_MAX_MS on a page in use (one every 5 min then), or when the page's requests do
 * not reach the hub.
 *
 * Attempts are tracked on the wifi_manager task: its CONNECT_STA callback starts one
 * (s_attempt_tick, forced non-zero, 0 = none; s_attempt_in_flight), its STA_DISCONNECTED callback
 * ends it and stamps s_attempt_tick again (a lost link too, and an attempt that did not start:
 * wifi_manager calls that callback right after the CONNECT_STA one then), and its GOT_IP callback
 * ends it. An attempt that neither fails nor gets its IP (associated, with no DHCP answer) keeps
 * the retry off until the STA's next disconnect, since a second connect would not start; its radio
 * hold still ends at the cap. A retry sent stays pending until wifi_manager takes it (its
 * CONNECT_STA callback restamps s_attempt_tick, or finds the STA connected), however long that
 * takes: a second order queued behind it would reach a connecting STA. s_retry_sent, counted up
 * just before the order and matched in that callback (s_retry_seen), tells the retry's CONNECT_STA
 * from a portal submit's, which comes from the same wifi_manager_connect_async(). */
#define ROUTER_RETRY_MS           30000    // a retry once no attempt has started or ended this long
#define ROUTER_RETRY_PAGE_MAX_MS  300000   // an open portal page defers one at most this long

static volatile bool s_attempt_in_flight = false;   // wifi_manager task only
static volatile TickType_t s_attempt_tick = 0;      // an attempt's start or end; wifi_manager task only

/* ---- Wi-Fi channels (the G0 bench baseline) -------------------------------------------------
 * The radio's channel at each SoftAP start, STA IP and STA link loss, with the router's: whether
 * the router-fallback SoftAP follows the router's channel (plan section 4.5), and which channel a
 * phone on the SoftAP had to follow. esp_wifi_get_channel() gives the radio's current channel,
 * the SoftAP's while it is up; the router's comes from esp_wifi_sta_get_ap_info() at the IP. Log
 * only, and all on the wifi_manager task (its callbacks), like wifi_manager's own esp_wifi_*
 * calls. */
static uint8_t s_router_channel = 0;   // the router's channel at the last IP, 0 = none yet; wifi_manager task only

// The radio's primary channel now, 0 if the driver does not say.
static unsigned radio_channel(void)
{
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&primary, &second) != ESP_OK)
        return 0;
    return primary;
}

// The router's channel from the STA's AP record, 0 if the STA is not associated. In a frame of
// its own: the record is about 90 B.
static __attribute__((noinline)) uint8_t router_channel(void)
{
    wifi_ap_record_t ap;
    return (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.primary : 0;
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
    // Router fallback: BLE scanning stays on apart from the radio holds, and wifi_task keeps
    // retrying the router, as it does whenever the STA is down (router_retry()).
    else if (!s_portal_priority)
        ESP_LOGI(WIFI_TAG, "SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on");

    // The channels (see above).
    if (s_router_channel != 0)
        ESP_LOGI(WIFI_TAG, "Wi-Fi channel at AP start: radio %u (SoftAP configured %u), router last seen on %u",
                 radio_channel(), (unsigned)wifi_settings.ap_channel, (unsigned)s_router_channel);
    else
        ESP_LOGI(WIFI_TAG, "Wi-Fi channel at AP start: radio %u (SoftAP configured %u), router not joined since boot",
                 radio_channel(), (unsigned)wifi_settings.ap_channel);
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
// attempt started, unless the STA already has its IP (wifi_manager then connects nothing). The
// router retry counts from it, in the window too; the radio hold is outside the window only: a
// portal submit's whatever else holds, wifi_manager's own only with no hold on or just over (the
// router retry's own covers its attempt).
static void cb_connect_sta(void *pvParameter)
{
    (void)pvParameter;
    // The router retry's order, if wifi_task has sent one that this task has not taken yet.
    bool retry = (s_retry_seen != s_retry_sent);
    s_retry_seen = s_retry_sent;
    if (s_sta_connected)
        return;
    TickType_t now = xTaskGetTickCount();
    s_attempt_tick = (now != 0) ? now : 1;   // 0 means "no attempt yet"
    s_attempt_in_flight = true;
    s_connect_submit = false;
    if (s_portal_priority)
        return;
    if (!retry && page_open(now))
    {
        // The user's Connect on the page (see the radio hold above).
        s_connect_submit = true;
        s_connect_until = hold_deadline(RADIO_HOLD_SUBMIT_MS);
        s_submits++;
    }
    else if (!radio_hold_near(now))
        s_connect_until = hold_deadline(RADIO_HOLD_CONNECT_MS);
}

// WM_ORDER_START_WIFI_SCAN (wifi_manager task): the portal page asked for the network list, and
// wifi_manager has started a scan, or skipped it (one already running, or its start failed: the
// cap ends the hold then). The order joins the page's chain, or starts one unless another hold is
// on or just over. In the chain's BLE window, or less than RADIO_HOLD_QUIET_MS before it, the scan
// is stopped at once and the hold ends (see the radio hold above).
static void cb_scan_start(void *pvParameter)
{
    (void)pvParameter;
    TickType_t now = xTaskGetTickCount();
    bool open = page_open(now);            // the page's last order came recently: its chain goes on
    s_scan_asked = (now != 0) ? now : 1;   // the router retry's page_open(), whatever the hold
    if (s_sta_connected || s_portal_priority)
    {
        s_chain_start = 0;
        return;
    }
    if (s_chain_start == 0 || !open)
    {
        s_chain_start = 0;
        if (radio_hold_near(now))
            return;   // not right behind another hold: the page asks again in about 3.8 s
        s_chain_start = (now != 0) ? now : 1;
        s_page_stops = 0;
    }
    if (ble_window_in(s_chain_start, now) < pdMS_TO_TICKS(RADIO_HOLD_QUIET_MS))
    {
        // BLE's turn. The stopped scan ends failed (its SCAN_DONE clears wifi_manager's scan bit,
        // and wifi_manager keeps its list), so the page shows the last full scan's. Never in a
        // connect attempt: wifi_manager could not start a scan then, and esp_wifi_scan_stop()
        // refuses a connecting STA's (ESP_ERR_WIFI_STATE) anyway.
        if (!s_attempt_in_flight)
            esp_wifi_scan_stop();
        s_page_stops++;
        if (hold_running(s_scan_until, now))
            s_scan_until = (now != 0) ? now : 1;
        return;
    }
    scan_hold_set(now, RADIO_HOLD_SCAN_MS);
}

// WM_EVENT_SCAN_DONE (wifi_manager task), once the list is rebuilt (or kept, for a scan that
// failed or was stopped): only the tail is left, and not into the chain's next BLE window.
static void cb_scan_done(void *pvParameter)
{
    (void)pvParameter;
    TickType_t now = xTaskGetTickCount();
    if (hold_running(s_scan_until, now))
        scan_hold_set(now, RADIO_HOLD_SCAN_TAIL_MS);
}

/* ---- The portal's forget ------------------------------------------------------------------
 * The portal page's Disconnect (DELETE /connect.json) and the 10 s reset send wifi_manager a
 * DISCONNECT_STA: it sets its user-disconnect bit and calls esp_wifi_disconnect(). Only its
 * STA_DISCONNECTED handler acts on that bit: it zeroes the STA config in RAM, saves it (zero SSID
 * and password blobs over the saved ones, under its NVS lock, which it gives back) and sends
 * START_AP, whose callback opens the portal window (cb_ap_started()). A connected STA gets there.
 * An idle one (on the router outage's fallback portal, between attempts) posts no disconnect
 * event: nothing was erased, the router retry went on with the old credentials, and the bit
 * stayed set, so the next disconnect that was not a user connect's (a router outage, maybe days
 * after a new setup) would have erased whatever was saved then and reopened the portal, with BLE
 * paused until someone sets Wi-Fi up again. So for an idle STA this callback posts that event
 * itself (forget_post()): wifi_manager runs the same erase, save and START_AP as for a connected
 * STA, the bit is used up, and the hub is where the 10 s reset leaves it, without the reboot.
 * Other handlers see the event too: the default netif handler takes the STA interface down, which
 * it already is, and cb_connection_lost() prints "WiFi Disconnected. Reason: 8", as for a
 * connected STA's forget. With an attempt in flight, or the STA connected, the driver's own
 * disconnect event follows, and wifi_manager erases unless that attempt was a user connect (a
 * portal submit or the router retry, one that started: wifi_manager's C2c), whose failure branch
 * it takes first, leaving the bit set: cb_connection_lost() then posts the event, the STA being
 * idle by then (s_forget_pending). The window, the router retry (no credentials: none) and the
 * page (its scan view) then behave as after the reset. wifi_manager task only. */
static volatile bool s_forget_pending = false;   // a forget waits for its disconnect; wifi_manager task only

static void forget_post(void)
{
    wifi_event_sta_disconnected_t ev = { 0 };
    ev.reason = WIFI_REASON_ASSOC_LEAVE;   // what a connected STA's forget reports
    // A bounded wait: the event loop can itself be waiting for room in this task's queue.
    esp_err_t err = esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &ev, sizeof(ev),
                                   pdMS_TO_TICKS(100));
    if (err == ESP_OK)
        ESP_LOGW(WIFI_TAG, "Wi-Fi forget with the STA not connected - wifi_manager erases the saved network");
    else
        ESP_LOGE(WIFI_TAG, "Wi-Fi forget: disconnect event not posted (%s) - the saved network is kept",
                 esp_err_to_name(err));
}

// WM_ORDER_DISCONNECT_STA (wifi_manager task), after wifi_manager's esp_wifi_disconnect(): the
// portal's forget or the 10 s reset (see above).
static void cb_disconnect_sta(void *pvParameter)
{
    (void)pvParameter;
    if (s_sta_connected || s_attempt_in_flight)
        s_forget_pending = true;
    else
        forget_post();
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

/* ---- Portal client log (the G0 bench baseline) --------------------------------------------
 * When each phone on the SoftAP joined, got its DHCP lease, and first asked the captive DNS, got
 * the captive-probe 302, loaded the page and called its API: join -> lease -> first DNS -> first
 * 302 -> page for every phone, without a line per request. Store and log only: nothing here
 * changes what the portal or the radio does.
 *
 * One entry per client, AP_CLIENTS_MAX of them (the SoftAP takes DEFAULT_AP_MAX_CONNECTIONS
 * stations). A station's join claims one by MAC and stamps the time, and starts its "first" marks
 * again; its lease adds its address; its leave keeps both, so its late requests still find it.
 * The portal's activity hook (portal_activity(): the httpd task per request, the dns_server task
 * per DNS query) finds the entry by address and prints the first request of each kind. A client
 * not seen joining (one that joined before this log was registered, or a home-LAN client of the
 * STA address: the portal's HTTP server answers there too) gets an entry of its own while one is
 * free. Each SoftAP start begins a new session with an empty table.
 * Written by the default event loop task (the SoftAP start, join, leave and lease events) and by
 * the httpd and dns_server tasks, under s_ap_clients_lock: a spinlock held for one table scan,
 * never while logging. */
#define AP_CLIENTS_MAX DEFAULT_AP_MAX_CONNECTIONS
_Static_assert(HTTP_APP_ACT_COUNT <= 8, "one bit per activity kind in ap_client_t.logged");

typedef enum
{
    AP_CLIENT_FREE = 0,
    AP_CLIENT_JOINED,      // a station on the SoftAP now
    AP_CLIENT_LEFT,        // a station that left: kept for its late requests until the entry is needed
    AP_CLIENT_ADDR_ONLY,   // a client seen only by its address
} ap_client_state_t;

typedef struct
{
    uint8_t mac[6];
    uint8_t state;      // ap_client_state_t
    uint8_t logged;     // one bit per http_app_activity_t printed since the join
    uint32_t ip;        // network byte order, 0 = none yet
    uint32_t join_ms;   // ap_now_ms() at the join, 0 = not seen joining
} ap_client_t;

static ap_client_t s_ap_clients[AP_CLIENTS_MAX];
static portMUX_TYPE s_ap_clients_lock = portMUX_INITIALIZER_UNLOCKED;

static const char *const k_activity_names[HTTP_APP_ACT_COUNT] = {
    [HTTP_APP_ACT_DNS] = "DNS query",
    [HTTP_APP_ACT_PROBE_302] = "captive probe (302 sent)",
    [HTTP_APP_ACT_PAGE] = "page request",
    [HTTP_APP_ACT_API_USER] = "Connect/Disconnect request",
    [HTTP_APP_ACT_API_BG] = "network list request",
    [HTTP_APP_ACT_STATUS] = "status request",
};

// Milliseconds since boot, never 0 (0 means "not seen joining"). Wraps after 49 days, which
// garbles only a delta across the wrap.
static uint32_t ap_now_ms(void)
{
    uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
    return (ms != 0) ? ms : 1;
}

// Under the lock: the entry that holds this station; else, with claim, a free one first, then the
// first that holds no station on the SoftAP now, cleared for it. -1 if none.
static int ap_client_by_mac_locked(const uint8_t *mac, bool claim)
{
    int spare = -1;
    for (int i = 0; i < AP_CLIENTS_MAX; i++)
    {
        const ap_client_t *c = &s_ap_clients[i];
        if ((c->state == AP_CLIENT_JOINED || c->state == AP_CLIENT_LEFT) && memcmp(c->mac, mac, 6) == 0)
            return i;
        if (c->state == AP_CLIENT_FREE)
        {
            if (spare < 0 || s_ap_clients[spare].state != AP_CLIENT_FREE)
                spare = i;
        }
        else if (c->state != AP_CLIENT_JOINED && spare < 0)
            spare = i;
    }
    if (!claim || spare < 0)
        return -1;
    memset(&s_ap_clients[spare], 0, sizeof(s_ap_clients[spare]));
    memcpy(s_ap_clients[spare].mac, mac, 6);
    return spare;
}

// WIFI_EVENT_AP_START: a new portal session.
static void ap_clients_reset(void)
{
    taskENTER_CRITICAL(&s_ap_clients_lock);
    memset(s_ap_clients, 0, sizeof(s_ap_clients));
    taskEXIT_CRITICAL(&s_ap_clients_lock);
}

static void ap_client_joined(const uint8_t *mac)
{
    uint32_t now = ap_now_ms();
    taskENTER_CRITICAL(&s_ap_clients_lock);
    int i = ap_client_by_mac_locked(mac, true);
    if (i >= 0)
    {
        s_ap_clients[i].state = AP_CLIENT_JOINED;
        s_ap_clients[i].logged = 0;
        s_ap_clients[i].join_ms = now;
    }
    taskEXIT_CRITICAL(&s_ap_clients_lock);
}

static void ap_client_left(const uint8_t *mac)
{
    taskENTER_CRITICAL(&s_ap_clients_lock);
    int i = ap_client_by_mac_locked(mac, false);
    if (i >= 0)
        s_ap_clients[i].state = AP_CLIENT_LEFT;
    taskEXIT_CRITICAL(&s_ap_clients_lock);
}

// The station's lease: the address is its own from now on. Returns its join time, 0 if its join
// was not seen.
static uint32_t ap_client_leased(const uint8_t *mac, uint32_t ip)
{
    uint32_t join_ms = 0;
    taskENTER_CRITICAL(&s_ap_clients_lock);
    for (int k = 0; k < AP_CLIENTS_MAX; k++)
    {
        ap_client_t *c = &s_ap_clients[k];
        if (c->state != AP_CLIENT_FREE && c->ip == ip && memcmp(c->mac, mac, 6) != 0)
        {
            c->ip = 0;   // an earlier holder of the address
            if (c->state == AP_CLIENT_ADDR_ONLY)
                c->state = AP_CLIENT_FREE;
        }
    }
    int i = ap_client_by_mac_locked(mac, true);
    if (i >= 0)
    {
        s_ap_clients[i].state = AP_CLIENT_JOINED;
        s_ap_clients[i].ip = ip;
        join_ms = s_ap_clients[i].join_ms;
    }
    taskEXIT_CRITICAL(&s_ap_clients_lock);
    return join_ms;
}

// The portal's activity hook (http_app_set_activity_hook()), on the httpd task per request and
// the dns_server task per DNS query: prints a client's first request of each kind (see above).
// Never blocks but on the log's own lock, once per kind and client.
static void portal_activity(http_app_activity_t kind, uint32_t client_ip)
{
    if (client_ip == 0 || (unsigned)kind >= HTTP_APP_ACT_COUNT)
        return;
    uint32_t now = ap_now_ms();
    uint32_t join_ms = 0;
    bool first = false;
    taskENTER_CRITICAL(&s_ap_clients_lock);
    int i = -1;
    int spare = -1;
    for (int k = 0; k < AP_CLIENTS_MAX && i < 0; k++)
    {
        if (s_ap_clients[k].state == AP_CLIENT_FREE)
        {
            if (spare < 0)
                spare = k;
        }
        else if (s_ap_clients[k].ip == client_ip)
            i = k;
    }
    if (i < 0 && spare >= 0)
    {
        i = spare;
        memset(&s_ap_clients[i], 0, sizeof(s_ap_clients[i]));
        s_ap_clients[i].state = AP_CLIENT_ADDR_ONLY;
        s_ap_clients[i].ip = client_ip;
    }
    if (i >= 0 && !(s_ap_clients[i].logged & (1u << kind)))
    {
        s_ap_clients[i].logged |= (uint8_t)(1u << kind);
        join_ms = s_ap_clients[i].join_ms;
        first = true;
    }
    taskEXIT_CRITICAL(&s_ap_clients_lock);
    if (!first)
        return;
    esp_ip4_addr_t ip = { .addr = client_ip };
    if (join_ms != 0)
        ESP_LOGI(WIFI_TAG, "portal client " IPSTR ": first %s, %lu ms after joining",
                 IP2STR(&ip), k_activity_names[kind], (unsigned long)(now - join_ms));
    else
        ESP_LOGI(WIFI_TAG, "portal client " IPSTR ": first %s (no SoftAP join seen)",
                 IP2STR(&ip), k_activity_names[kind]);
}

// SoftAP station join/leave, so the bench can see a phone associate (wifi_manager silences the
// driver's own "wifi" log). MAC, AID and reason only: none of it is a credential. It also feeds
// the portal client log (above): a SoftAP start clears it, a join stamps the station's time.
// Runs on the default event loop task.
static void ap_station_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_AP_START)
    {
        ap_clients_reset();
        return;
    }
    if (data == NULL)
        return;
    if (id == WIFI_EVENT_AP_STACONNECTED)
    {
        const wifi_event_ap_staconnected_t *e = (const wifi_event_ap_staconnected_t *)data;
        ap_client_joined(e->mac);
        ESP_LOGI(WIFI_TAG, "SoftAP: station %02X:%02X:%02X:%02X:%02X:%02X joined, AID=%u",
                 e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5],
                 (unsigned)e->aid);
    }
    else if (id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        const wifi_event_ap_stadisconnected_t *e = (const wifi_event_ap_stadisconnected_t *)data;
        ap_client_left(e->mac);
        ESP_LOGI(WIFI_TAG, "SoftAP: station %02X:%02X:%02X:%02X:%02X:%02X left, AID=%u, reason=%u",
                 e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5],
                 (unsigned)e->aid, (unsigned)e->reason);
    }
}

// IP_EVENT_AP_STAIPASSIGNED: the SoftAP's DHCP server gave a station its address, printed with
// the time since the station's join. Runs on the default event loop task.
static void ap_lease_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id != IP_EVENT_AP_STAIPASSIGNED || data == NULL)
        return;
    const ip_event_ap_staipassigned_t *e = (const ip_event_ap_staipassigned_t *)data;
    uint32_t now = ap_now_ms();
    uint32_t join_ms = ap_client_leased(e->mac, e->ip.addr);
    if (join_ms != 0)
        ESP_LOGI(WIFI_TAG, "SoftAP: station %02X:%02X:%02X:%02X:%02X:%02X got " IPSTR ", %lu ms after joining",
                 e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5],
                 IP2STR(&e->ip), (unsigned long)(now - join_ms));
    else
        ESP_LOGI(WIFI_TAG, "SoftAP: station %02X:%02X:%02X:%02X:%02X:%02X got " IPSTR " (join not seen)",
                 e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5],
                 IP2STR(&e->ip));
}

void app_wifi_start()
{
    /* Override the default AP SSID with a unique name derived from MAC.
     * hub_identity_init() must have been called first. */
    const char *sid = hub_identity_get_short_id();
    snprintf((char *)wifi_settings.ap_ssid, MAX_SSID_SIZE, "WiFi-Hub-%s", sid);
    ESP_LOGI(WIFI_TAG, "AP SSID: %s", (char *)wifi_settings.ap_ssid);

    // The portal client log's activity hook: a plain store, so before the start, ahead of the
    // portal's first request.
    http_app_set_activity_hook(&portal_activity);
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
    // The portal's forget and the 10 s reset. Nothing else registers it.
    wifi_manager_set_callback(WM_ORDER_DISCONNECT_STA, &cb_disconnect_sta);
#if CONFIG_APP_BENCH_DIAG
    // Bench build (main/Kconfig.projbuild): wifi_manager_start() turned the Wi-Fi driver's log
    // off; back to INFO, so the bench log has its channel switch and CSA (csa_count) lines. The
    // warning marks the log of a bench image: not for release or the production tool.
    esp_log_level_set("wifi", ESP_LOG_INFO);
    ESP_LOGW(WIFI_TAG, "bench build (APP_BENCH_DIAG): Wi-Fi driver log at INFO - not for release");
#endif
    xTaskCreate(&wifi_task, "wifi_task", 4096, NULL, 5, &wifiTaskHandle);
}

void cb_connection_ok(void *pvParameter)
{
    // The STA's IPv4 address: wifi_manager passes it as the parameter itself (wifi_manager.h).
    esp_ip4_addr_t ip = { .addr = (uint32_t)(uintptr_t)pvParameter };
    char str_ip[16];
    esp_ip4addr_ntoa(&ip, str_ip, IP4ADDR_STRLEN_MAX);

    ESP_LOGI(WIFI_TAG, "Connected! IP: %s", str_ip);

    // The channels (see above): the router's is kept for the next SoftAP start and link loss.
    uint8_t router = router_channel();
    if (router != 0)
        s_router_channel = router;
    ESP_LOGI(WIFI_TAG, "Wi-Fi channel at IP: radio %u, router %u", radio_channel(), (unsigned)router);

    // The STA has its air time now: no Wi-Fi radio hold, and BLE resumes at once. The attempt
    // that got here is over, and so is a page's chain.
    s_sta_connected = true;
    s_attempt_in_flight = false;
    s_scan_until = 0;
    s_connect_until = 0;
    s_connect_submit = false;
    s_chain_start = 0;

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
    // The disconnect reason: wifi_manager passes it as the parameter itself, 0 if none was given
    // (wifi_manager.h).
    int reason = (int)(uintptr_t)pvParameter;
    if (reason != 0)
        ESP_LOGW(WIFI_TAG, "WiFi Disconnected. Reason: %d", reason);
    // The channels (see above), for a link that was up: not for each failed connect attempt.
    if (s_sta_connected)
        ESP_LOGI(WIFI_TAG, "Wi-Fi channel at link loss: radio %u, router was on %u",
                 radio_channel(), (unsigned)s_router_channel);

    // The link was lost, or a connect attempt ended, and its radio hold with it (the deadline stays
    // for RADIO_HOLD_GAP_MS). The router retry counts from here too: this disconnect may have armed
    // wifi_manager's own retry timer (see the router retry above).
    s_sta_connected = false;
    s_attempt_in_flight = false;
    TickType_t now = xTaskGetTickCount();
    s_attempt_tick = (now != 0) ? now : 1;   // 0 means "no attempt yet"
    if (hold_running(s_connect_until, now))
        s_connect_until = s_attempt_tick;
    s_connect_submit = false;

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

    // A forget sent while an attempt ran or the STA was connected: if wifi_manager took a user
    // connect's branch for this disconnect, it erased nothing and its disconnect bit is still set,
    // so the forget's event is posted now, the STA being idle (see the portal's forget above).
    if (s_forget_pending)
    {
        s_forget_pending = false;
        const wifi_config_t *sta = wifi_manager_get_wifi_sta_config();
        if (sta != NULL && sta->sta.ssid[0] != '\0')
            forget_post();
    }

    // Network LED -> "no internet" (ramp red). This also clears the MQTT flag
    // inside net_status so a later reconnect shows "connecting" first.
    net_status_set_wifi(false);

    // Stop the MQTT client so it doesn't thrash TLS handshakes (fragmenting the
    // heap the SoftAP captive portal needs) while STA is down / in AP mode.
    iothub_suspend_mqtt();
}

// wifi_task's own state for the Wi-Fi radio hold's log lines and the router retry.
typedef struct
{
    bool hold_on;            // a hold's ON line is printed, its OFF line not yet
    TickType_t hold_since;   // when that ON line was printed
    bool page_on;            // a page chain's first line is printed, its last not yet
    TickType_t page_since;   // when that first line was printed
    bool long_on;            // that chain is in its long listen, and its line is printed
    uint8_t submits;         // s_submits as last seen
    TickType_t retry_mark;   // s_attempt_tick when the last router retry was set up
    bool retry_pending;      // that retry is sent and wifi_manager has not taken it yet
    unsigned retries;        // router retries sent since the fallback began
    bool defer_logged;       // the "retry deferred" line is printed for the page open now
} wifi_task_state_t;

// Prints each Wi-Fi radio hold's start and end as wifi_task sees them. It looks on every pass,
// every second while the STA is down (the only time a hold can run), so a line is at most about
// a second late, and holds that chain print one pair. A page's chain, whose holds stop for a BLE
// window every 12 s, prints one line as it starts and one as it ends instead, with one for its
// long listen and one for each portal submit in it.
static void radio_hold_log(wifi_task_state_t *st)
{
    TickType_t now = xTaskGetTickCount();
    uint8_t submits = s_submits;
    TickType_t start = s_chain_start;   // one read: the wifi_manager task can end the chain
    if (start != 0 && page_open(now) && !s_sta_connected && !s_portal_priority)
    {
        if (!st->page_on)
        {
            st->page_on = true;
            st->page_since = now;
            st->hold_on = false;   // the chain's lines stand for its holds
            st->long_on = false;
            ESP_LOGI(WIFI_TAG, "Wi-Fi setup page in use - its scans pause BLE scanning, BLE listens %d s of every %d s",
                     RADIO_HOLD_WINDOW_MS / 1000, RADIO_HOLD_PERIOD_MS / 1000);
        }
        if (submits != st->submits)
            ESP_LOGI(WIFI_TAG, "Wi-Fi setup page: Connect sent - BLE scanning paused for its attempt (%d s at most)",
                     RADIO_HOLD_SUBMIT_MS / 1000);
        st->submits = submits;
        bool long_listen = chain_phase(start, now) >= pdMS_TO_TICKS(RADIO_HOLD_LONG_AFTER_MS);
        if (long_listen && !st->long_on)
            ESP_LOGI(WIFI_TAG, "Wi-Fi setup page in use %u s - BLE listens %d s with none of its scans",
                     (unsigned)((now - st->page_since) / configTICK_RATE_HZ), RADIO_HOLD_LONG_MS / 1000);
        st->long_on = long_listen;
        return;
    }
    st->submits = submits;
    if (st->page_on)
    {
        // Done: the STA got its IP, or the portal's forget opened the window, which pauses BLE.
        st->page_on = false;
        ESP_LOGI(WIFI_TAG, "Wi-Fi setup page idle, closed or done after %u s (%u of its scans stopped for BLE) - %s",
                 (unsigned)((now - st->page_since) / configTICK_RATE_HZ), (unsigned)s_page_stops,
                 s_portal_priority ? "BLE stays paused for the setup portal" : "BLE scanning resumed");
    }
    bool on = app_wifi_radio_hold_active();
    if (on == st->hold_on)
        return;
    st->hold_on = on;
    if (on)
    {
        st->hold_since = now;
        ESP_LOGI(WIFI_TAG, "Wi-Fi radio hold ON (%s) - BLE scanning paused", radio_hold_reason(now));
    }
    else
    {
        ESP_LOGI(WIFI_TAG, "Wi-Fi radio hold OFF after %u s - BLE scanning resumed",
                 (unsigned)((now - st->hold_since + configTICK_RATE_HZ / 2) / configTICK_RATE_HZ));
    }
}

// The STA is down with credentials in its config, outside the portal window, SoftAP up or not:
// wifi_manager's fallback AP after its retries, a SoftAP left up by "Wi-Fi lost after setup", or
// the STA left idle with no AP (see the router retry above). The flags are the wifi_manager
// task's; a stale read costs one pass.
static bool router_fallback(void)
{
    if (s_sta_connected || s_portal_priority)
        return false;
    const wifi_config_t *sta = wifi_manager_get_wifi_sta_config();
    return sta != NULL && sta->sta.ssid[0] != '\0';
}

// The router retry (see above), on every wifi_task pass: every second while the STA is down.
// wifi_manager_connect_async() waits for room in wifi_manager's queue, so it is sent from here,
// never from a wifi_manager callback.
static void router_retry(wifi_task_state_t *st)
{
    TickType_t now = xTaskGetTickCount();
    // Taken: its attempt started (or another one started or ended), or the STA has its IP.
    if (st->retry_pending && (s_sta_connected || s_attempt_tick != st->retry_mark))
        st->retry_pending = false;

    if (!router_fallback())
    {
        st->retries = 0;
        st->defer_logged = false;
        return;
    }
    // Since the last attempt started or ended. None before wifi_manager's first: at boot its
    // restore's CONNECT_STA can still be queued, and a retry then would reach a connecting STA.
    if (s_attempt_tick == 0)
        return;
    TickType_t age = now - s_attempt_tick;
    if (s_attempt_in_flight || st->retry_pending || age < pdMS_TO_TICKS(ROUTER_RETRY_MS))
        return;
    if (page_open(now) && age < pdMS_TO_TICKS(ROUTER_RETRY_PAGE_MAX_MS))
    {
        if (!st->defer_logged)
        {
            st->defer_logged = true;
            ESP_LOGI(WIFI_TAG, "router fallback: retry deferred - the Wi-Fi setup page is open");
        }
        return;
    }
    st->defer_logged = false;
    // Not in a page's BLE window, nor so close before one that the hold would reach into it: the
    // window masks every hold but a submit's, so the attempt's scan for the router would run beside
    // the leak scan. It waits, 6.5 s at most, or out a long listen (see the router retry above).
    TickType_t start = s_chain_start;
    if (start != 0 && page_open(now) && ble_window_in(start, now) < pdMS_TO_TICKS(RADIO_HOLD_RETRY_MS))
        return;
    // Not right behind another hold (see the radio hold above), unless a page's scans hold BLE
    // now: a retry that has waited out ROUTER_RETRY_PAGE_MAX_MS joins them.
    if (!hold_running(s_scan_until, now) && radio_hold_near(now))
        return;

    st->retry_mark = s_attempt_tick;
    s_retry_until = hold_deadline(RADIO_HOLD_RETRY_MS);
    radio_hold_log(st);   // its ON line comes before the retry's
    vTaskDelay(pdMS_TO_TICKS(RADIO_HOLD_RETRY_LEAD_MS));
    // Again after the pause: an attempt may have started (a portal submit) or ended meanwhile, the
    // page may be in use again (its first scan order after an idle spell), or the STA, the window
    // or the config changed. Then nothing is sent, and the hold runs out: it covers an attempt that
    // started in it, which took no hold of its own.
    now = xTaskGetTickCount();
    if (!router_fallback() || s_attempt_in_flight || s_attempt_tick != st->retry_mark ||
        (page_open(now) && now - s_attempt_tick < pdMS_TO_TICKS(ROUTER_RETRY_PAGE_MAX_MS)))
        return;
    st->retry_pending = true;
    st->retries++;
    // "configured", not "saved": after a portal submit that failed, or one made while the STA was
    // connected, the STA config in RAM holds what was typed (see the router retry above).
    ESP_LOGI(WIFI_TAG, "router fallback: retrying the configured network (attempt %u)", st->retries);
    s_retry_sent++;   // before the order: cb_connect_sta() tells it from a portal submit
    wifi_manager_connect_async();
}

void wifi_task(void *pvParameter)
{
    (void)pvParameter;
    // Initial "no internet" state is latched by net_status_init() at boot.
    // The SoftAP station log (ap_station_event_handler) and its lease line (ap_lease_event_handler)
    // are registered here, not in a wifi_manager callback: registering takes the event loop's
    // lock, which the loop holds while it runs wifi_manager's handler, and that handler can block
    // posting to the wifi_manager task. The wifi_manager task creates the default loop, so a first
    // try can find none (ESP_ERR_INVALID_STATE, silent): retried every second until each is in.
    // Each pass also runs the portal priority window's safety net (portal_priority_net()), the
    // router retry (router_retry()) and the Wi-Fi radio hold's log (radio_hold_log()): every
    // second while the STA is down or a hold's OFF line or a page chain's last line is still due,
    // else every 5 s.
    bool ap_log_on = false;
    bool lease_log_on = false;
    TickType_t stop_sent_for = 0;   // the GOT_IP tick the net already sent a STOP_AP for
    wifi_task_state_t st = { 0 };
    while (1)
    {
        bool fast = !ap_log_on || !lease_log_on || !s_sta_connected || st.hold_on || st.page_on;
        vTaskDelay(pdMS_TO_TICKS(fast ? 1000 : 5000));
        if (!ap_log_on)
            ap_log_on = (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                    &ap_station_event_handler, NULL) == ESP_OK);
        if (!lease_log_on)
            lease_log_on = (esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED,
                                                       &ap_lease_event_handler, NULL) == ESP_OK);
        portal_priority_net(&stop_sent_for);
        router_retry(&st);
        radio_hold_log(&st);
    }
    vTaskDelete(NULL);
}