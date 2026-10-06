#include "app_wifi.h"
#include "wifi_manager.h"
#include "http_app.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "net_status/net_status.h"
#include "esp_wifi.h"

#include "app_iothub.h"
#include "app_ble_valve.h"
#include "provisioning_manager/provisioning_manager.h"
#include "hub_identity/hub_identity.h"
#include "radio_policy/radio_policy.h"
#include "radio_policy/radio_lab.h"

TaskHandle_t wifiTaskHandle = NULL;

void cb_connection_ok(void *pvParameter);
void cb_connection_lost(void *pvParameter);
void wifi_task(void *pvParameter);

/* ---- The radio and the setup portal (2.1.4 WP8; plan sections 4 and 5, decision D2) -------
 * The ESP32-S3 has one 2.4 GHz radio for Wi-Fi and BLE. Until 2.1.4 WP8 the setup portal took it
 * from BLE by pausing the leak scan: for as long as the SoftAP was up with no Wi-Fi credentials
 * saved (the portal priority window: on the 2026-09-29 bench a phone could not join beside the
 * continuous 1M + Coded scan), and for a few seconds around each Wi-Fi scan and connect attempt
 * (the radio holds, chained in 12 s periods while a setup page was in use). Leak protection was
 * off in the reset portal, with no time cap, and the health engine held the sensors' timeouts.
 * The radio policy (main/radio_policy) shares the radio instead, from the facts this file gives it
 * (below). With the SoftAP up and the STA not connected, BLE runs short Coded windows between
 * Wi-Fi slots (AP_IDLE, or SERVE while a setup page is in use: plan I8), and stops only for a
 * pulse of at most 2.8 s followed by 1.2 s of Coded scanning, within 12 s per minute (I2, I2b): a
 * station's join (JOIN_ASSIST), the page's Connect (SUBMIT), the router retry (RETRY) and the
 * network list's scan (LIST). So the no-credential portal after a 10 s reset keeps scanning for
 * the provisioned sensors and the valve (D2); first setup, with nothing provisioned, runs no BLE
 * scan at all (BLE_IDLE, or NimBLE not started). With the STA connected the SoftAP's tail runs
 * NORMAL.
 *
 * The router retry's deferral keys on the setup page in use (page_in_use()): it or one of its API
 * calls was requested less than PAGE_OPEN_MS ago, the page's own polls included (GET /ap.json?bg=1
 * every 3.8 s while the user is active), not status.json (portal_activity(), the httpd task). A
 * stale tick reads as recent for PAGE_OPEN_MS once every 2^32 ticks, which only defers a retry. */
#define PAGE_OPEN_MS 10000   // the page counts as in use this long after its last request

static volatile bool s_sta_connected = false;   // the STA has its IP; wifi_manager task only
static volatile TickType_t s_page_tick = 0;     // httpd task only

static bool page_in_use(TickType_t now)
{
    TickType_t t = s_page_tick;
    return t != 0 && now - t < pdMS_TO_TICKS(PAGE_OPEN_MS);
}

/* ---- Router retry --------------------------------------------------------------------------
 * wifi_manager retries a lost router 3 times, then opens the SoftAP as a router-fallback portal,
 * and its START_AP stops the retry timer (the LOCAL PATCH in wifi_manager.c, against a scan race);
 * while the SoftAP is up it starts none (its C5): nothing tried the router again, and on the
 * 2026-09-29 bench the hub never rejoined once the router was back. wifi_manager can also leave
 * the STA idle with the SoftAP down: after a Connect's attempt that fails it starts no retry and no
 * AP. So whenever the STA is down with a network in use (credentials in wifi_manager's RAM copy:
 * router_fallback()), SoftAP up or not, wifi_task asks wifi_manager for
 * an APP_RETRY (wifi_manager_retry_async()) once no attempt has started or ended for
 * ROUTER_RETRY_MS plus a jitter of up to ROUTER_RETRY_JITTER_MS, drawn again for each attempt
 * (plan 4.4: 30 s + U(0, 5) s), and none is in flight; never before wifi_manager's own first
 * attempt (its restore at boot, or a portal submit: s_attempt_tick still 0).
 * The BLE side (2.1.4 WP8, plan 4.4's RETRY pulse): the retry asks the radio policy for a RETRY
 * pulse and waits for its answer, RP_GRANT_WAIT_MS (2 s) at most, holding nothing. The policy
 * grants it at the end of a Coded window, with room in the blind budget and the pulse-rate limit
 * (I2, I2b): BLE stops for RP_RETRY_MS (1.5 s), the time of the connect's scan for the router,
 * from the moment the order is sent; its deadline ends it, and BLE resumes with a Coded recovery
 * window. With the SoftAP down, or no BLE scan running, the answer is FREE at once. Not granted
 * (refused, or no answer in 2 s): the order is sent all the same, with a W line, and the attempt
 * runs beside BLE. While a station joins the SoftAP (no lease yet, or its join assist: plan I7,
 * radio_policy_join_settling()), before the request and again after its wait, the retry waits,
 * ROUTER_RETRY_JOIN_MAX_MS at most, so the SoftAP stays on its channel for that station's DHCP.
 * The network tried is the one in use: since 2.1.4 C8 a page's Connect writes it only once its
 * candidate has an IP, so after a mistyped password, or a Connect to another network that fails,
 * the retries go on with the working network and rejoin it when the router is back.
 * wifi_manager's own retries, its first three after a link loss with the SoftAP down, come about
 * every 10 s, so the 30 s rule adds none beside them.
 *
 * Counted from an attempt's end too, not only its start: every lost-link disconnect with the
 * SoftAP down arms wifi_manager's one-shot retry timer (WIFI_MANAGER_RETRY_TIMER, 5 s) before our
 * STA_DISCONNECTED callback runs, and only START_AP stops it. A link loss can come long after
 * the last attempt started (a link up for minutes); ROUTER_RETRY_MS after the last disconnect the
 * timer has fired (its attempt then counts) or START_AP has stopped it. A retry therefore comes
 * ROUTER_RETRY_MS after the previous one failed, on the fallback AP or with the SoftAP down:
 * about every 33-36 s.
 *
 * wifi_manager owns every attempt (2.1.4 C8): a retry that meets an attempt in flight, the STA
 * connected, or a Connect's candidate starts nothing (its callback says so, and the retry counts
 * from it), and a page's Connect never meets the retry's attempt: it waits for it, 8 s at most,
 * then ends it. The retry still waits while the page is in use (page_in_use(): its requests and
 * polls, 2.1.4 C10b), for at most ROUTER_RETRY_PAGE_MAX_MS since the last attempt, so that a
 * Connect rarely waits at all, and a page that keeps asking cannot keep the hub off its router.
 *
 * Attempts are tracked on the wifi_manager task: its CONNECT_STA callback starts one
 * (s_attempt_tick, forced non-zero, 0 = none; s_attempt_in_flight), or stamps s_attempt_tick only
 * for an order that started none; its STA_DISCONNECTED callback ends it and stamps s_attempt_tick
 * again (a lost link too), and its GOT_IP callback ends it. An attempt that associates but gets no
 * IP keeps the retry off until its disconnect (wifi_manager ends a Connect's after 25 s). An order
 * not queued within WIFI_MANAGER_POST_WAIT_MS (C6) is sent again on a later pass. */
#define ROUTER_RETRY_MS           30000    // a retry once no attempt has started or ended this long ...
#define ROUTER_RETRY_JITTER_MS    5000     // ... plus U(0, this), drawn per attempt (plan 4.4)
#define ROUTER_RETRY_PAGE_MAX_MS  300000   // an open portal page defers one at most this long
#define ROUTER_RETRY_JOIN_MAX_MS  10000    // a station joining the SoftAP defers one at most this long (I7)

static volatile bool s_attempt_in_flight = false;   // wifi_manager task only
static volatile TickType_t s_attempt_tick = 0;      // an attempt's start or end; wifi_manager task only

/* ---- The Wi-Fi side's facts for the radio policy (2.1.4 WP8; plan 4.1) ----------------------
 * The wifi_manager task is the one writer of each (its callbacks; they only store and wake):
 *   - the SoftAP up and the STA's IP (radio_policy_note_wifi()): START_AP, STOP_AP, GOT_IP and
 *     STA_DISCONNECTED; wifi_task cross-checks the SoftAP's against the driver's mode on every
 *     pass and only logs a disagreement (ap_fact_check());
 *   - a STA attempt in flight (radio_policy_note_sta_attempt()): from the CONNECT_STA callback of
 *     an attempt that started to its IP or its disconnect;
 *   - the setup page's Connect in flight (radio_policy_note_submit()): from the CONNECT_STA
 *     callback of a USER attempt that started (2.1.4 C8) to that attempt's IP, its disconnect, or
 *     the start of an attempt of another kind (s_submit_noted). It asks for the SUBMIT pulse,
 *     which the policy always grants, within the blind budget and the pulse-rate limit (I2, I2b),
 *     so no pattern of Connects on the open SoftAP blinds BLE beyond them.
 * The default event loop gives the stations (joins, leaves and leases: ap_station_event_handler(),
 * ap_lease_event_handler()), wifi_task prunes them against the driver's list, and the httpd task
 * the page's activity (portal_activity()). */
static volatile bool s_rp_ap_up = false;        // the SoftAP as last told to the policy; wifi_manager task only
static volatile bool s_submit_noted = false;    // a SUBMIT asked for, not yet ended; wifi_manager task only

// wifi_manager task: the SoftAP and the STA's IP, after s_sta_connected is set.
static void rp_note_wifi(bool ap_up)
{
    s_rp_ap_up = ap_up;
    radio_policy_note_wifi(ap_up, s_sta_connected);
}

// wifi_manager task: the attempt in flight is over (its IP or its disconnect), and with it the
// setup page's Connect, if it was one.
static void rp_attempt_over(void)
{
    radio_policy_note_sta_attempt(false);
    if (s_submit_noted)
    {
        s_submit_noted = false;
        radio_policy_note_submit(false);
    }
}

/* ---- The SoftAP's tail after an IP (2.1.4 WP2; plan section 4.6) ---------------------------
 * The SoftAP stays up a while after the STA gets its IP, so that a phone on it can read the
 * result, and the cloud waits for it to stop: no TLS while the SoftAP is up (iothub_task's
 * cloud admission, plan I4). How long is decided from facts, at the IP and on each wifi_task
 * pass (one a second while a tail runs):
 *   - an automatic rejoin with no station on the SoftAP: it stops AP_TAIL_AUTO_EMPTY_MS (0.5 s)
 *     after the IP;
 *   - an automatic rejoin with a station on it: AP_TAIL_AUTO_MS (20 s) after the IP, or
 *     AP_TAIL_AUTO_LEFT_MS (10 s) after the last station left, whichever comes first;
 *   - a setup-page Connect: AP_TAIL_SUBMIT_MS (60 s) after the IP, or AP_TAIL_SUBMIT_LEFT_MS
 *     (15 s) after the last station left, never sooner than AP_TAIL_SUBMIT_MIN_MS (15 s) after
 *     the IP: a phone the SoftAP's channel switch dropped has that long to re-join and read the
 *     result;
 *   - the page's Finish (2.1.4 C12, POST /finish.json, in any tail): max(IP +
 *     AP_TAIL_FINISH_MIN_MS, Finish + AP_TAIL_FINISH_MS), i.e. 5 s after the IP at the soonest and
 *     2 s after the tap, when that is sooner than the rules above (portal_finish()). The phone's
 *     sign-in window closes when the SoftAP goes, and the phone returns to its own Wi-Fi.
 * "Left" is when wifi_task first saw the SoftAP with no station: the driver's station list
 * (esp_wifi_ap_get_sta_list()), read on each pass, so at most about a second late. A station
 * that joins again moves the stop back to the 20 s or 60 s cap. cb_connection_ok() sets the
 * first stop at the IP (ap_tail_start()), and wifi_task moves it (ap_tail_maintain()), both
 * through wifi_manager_ap_stop_in() (the portal's C12), which re-arms wifi_manager's one
 * AP-shutdown timer; its STOP_AP stops the AP only with the STA connected. A STA lost in the
 * tail stops that timer, and the SoftAP stays up as a router-fallback portal (no retry timer of
 * wifi_manager's own with the SoftAP up, its C5: the router retry below owns the retries); the
 * next IP starts a tail of its own.
 * The backstop: the SoftAP still up with the STA connected AP_TAIL_BACKSTOP_MS (75 s) after the
 * IP, whatever kept it up (a timer command that was not taken, wifi_manager's AP_STARTED bit set
 * only after its GOT_IP, so that it armed no timer, a SoftAP that came up after the IP):
 * wifi_task sends one STOP_AP for that IP.
 *
 * Automatic or a Connect: the attempt that got the IP decides. wifi_manager starts every attempt
 * itself, one at a time, and its CONNECT_STA callback gives the kind of each one it starts (2.1.4
 * C8): CONNECTION_REQUEST_USER is the setup page's Connect (s_attempt_submit), any other kind is
 * automatic. A Connect never lands in another attempt any more: it waits for it, at most 8 s, or
 * ends it, and gets an attempt of its own. A disconnect ends the attempt and clears the mark;
 * every attempt that starts writes it again. */
#define AP_TAIL_AUTO_EMPTY_MS    500      // an automatic rejoin with no station on the SoftAP
#define AP_TAIL_AUTO_MS          20000    // an automatic rejoin with a station: at the latest ...
#define AP_TAIL_AUTO_LEFT_MS     10000    // ... or this long after the last station left
#define AP_TAIL_SUBMIT_MS        60000    // a setup-page Connect: at the latest ...
#define AP_TAIL_SUBMIT_LEFT_MS   15000    // ... or this long after the last station left,
#define AP_TAIL_SUBMIT_MIN_MS    15000    //     never sooner than this after the IP
#define AP_TAIL_BACKSTOP_MS      75000    // still up this long after the IP: wifi_task stops it
#define AP_TAIL_FINISH_MS        2000     // the page's Finish: the SoftAP stops this long after it ...
#define AP_TAIL_FINISH_MIN_MS    5000     // ... and never sooner than this after the IP (plan 4.6)

static volatile bool s_attempt_submit = false;   // the tracked attempt is the page's Connect; wifi_manager task only
static volatile TickType_t s_ip_tick = 0;        // the STA's last IP (forced non-zero), 0 = none since a loss; wifi_manager task only
static volatile bool s_tail_submit = false;      // that IP's tail follows the page's Connect; wifi_manager task only
static volatile uint32_t s_tail_cap_ms = 0;      // its first stop, ms after the IP; 0 = the SoftAP was down then; wifi_manager task only
static volatile bool s_tail_armed = false;       // that first stop was taken (wifi_manager_ap_stop_in()); wifi_manager task only
static TickType_t s_ap_stop_seen = 0;            // the IP whose SoftAP stop was printed; wifi_manager task only
static volatile TickType_t s_finish_stop = 0;    // the page's Finish: its stop, in ticks after the IP; httpd task only
static volatile TickType_t s_finish_ip = 0;      // the IP (s_ip_tick) that Finish was for, 0 = none; httpd task only

// wifi_task's tail_stop for a first stop ap_tail_start() could not set: no real stop has this
// value, so the next pass sets it.
#define AP_TAIL_STOP_UNSET       ((TickType_t)-1)

// The SoftAP is up: the Wi-Fi mode has the AP in it (a fact, read from the driver; false when it
// cannot say).
static bool softap_up(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    return esp_wifi_get_mode(&mode) == ESP_OK && (mode == WIFI_MODE_APSTA || mode == WIFI_MODE_AP);
}

// The stations on the SoftAP now (the driver's list, a fact), -1 when the driver cannot say. In a
// frame of its own: the list is about 0.2 KB.
static __attribute__((noinline)) int ap_station_count(void)
{
    wifi_sta_list_t list;
    return (esp_wifi_ap_get_sta_list(&list) == ESP_OK) ? list.num : -1;
}

// cb_connection_ok() (wifi_manager task), at each IP: the tail's kind and first stop (see above),
// with the SoftAP up. wifi_manager's GOT_IP has just armed its own default stop (60 s) if its
// AP_STARTED bit was set; this one replaces it, from the mode itself. A count the driver cannot
// give counts as a station: the longer tail.
static void ap_tail_start(void)
{
    bool submit = s_attempt_submit;
    uint32_t cap_ms = 0;
    int stations = 0;
    if (softap_up())
    {
        stations = ap_station_count();
        cap_ms = submit ? AP_TAIL_SUBMIT_MS : (stations != 0) ? AP_TAIL_AUTO_MS : AP_TAIL_AUTO_EMPTY_MS;
    }
    TickType_t now = xTaskGetTickCount();
    // Set before s_ip_tick, which publishes the tail to wifi_task. A failure prints its own
    // line, and wifi_task sets the stop on its next pass (ap_tail_maintain()).
    bool armed = (cap_ms != 0) && wifi_manager_ap_stop_in(cap_ms);
    s_tail_submit = submit;
    s_tail_cap_ms = cap_ms;
    s_tail_armed = armed;
    s_ip_tick = (now != 0) ? now : 1;   // last: wifi_task reads it first
    if (cap_ms == 0)
        return;
    if (submit)
        ESP_LOGI(WIFI_TAG, "SoftAP tail after a setup-page Connect (stations on it: %d) - it stops %d s after the IP, or %d s after the last station leaves (not before %d s)",
                 stations, AP_TAIL_SUBMIT_MS / 1000, AP_TAIL_SUBMIT_LEFT_MS / 1000, AP_TAIL_SUBMIT_MIN_MS / 1000);
    else if (stations != 0)
        ESP_LOGI(WIFI_TAG, "SoftAP tail after an automatic rejoin (stations on it: %d) - it stops %d s after the IP, or %d s after the last station leaves",
                 stations, AP_TAIL_AUTO_MS / 1000, AP_TAIL_AUTO_LEFT_MS / 1000);
    else
        ESP_LOGI(WIFI_TAG, "SoftAP tail after an automatic rejoin (no station on it) - it stops %d.%d s after the IP",
                 AP_TAIL_AUTO_EMPTY_MS / 1000, (AP_TAIL_AUTO_EMPTY_MS % 1000) / 100);
}

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

// Appended to the three channel lines (2.1.4 WP1): while a scan wifi_manager started is in flight
// (the portal page's network list), the radio's channel is maybe the scan's, not the AP's or the
// router's (the CP5 bench read "radio 10, router 2" at an IP). Nothing otherwise: the lines are
// then as before.
static const char *scan_note(void)
{
    return wifi_manager_scan_in_flight() ? ", Wi-Fi scan in flight" : "";
}

// The setup page's Finish (POST /finish.json; the httpd task; 2.1.4 C12, plan 4.6's Finish row):
// the SoftAP stops at max(IP + AP_TAIL_FINISH_MIN_MS, now + AP_TAIL_FINISH_MS), or sooner if the
// tail's own rules say so. Only with the STA connected and the SoftAP up after an IP: false
// otherwise, and the page gets 409. The stop is set here (wifi_manager_ap_stop_in() never waits)
// and recorded for wifi_task (s_finish_stop, then s_finish_ip), whose next pass, woken here, keeps
// it whatever the stations do (ap_tail_maintain()). A stop the timer did not take is false too:
// nothing is recorded, and the page can tap Finish again.
static bool portal_finish(void)
{
    TickType_t ip = s_ip_tick;
    if (ip == 0 || !s_sta_connected || !softap_up())
        return false;
    TickType_t since = xTaskGetTickCount() - ip;
    TickType_t stop = since + pdMS_TO_TICKS(AP_TAIL_FINISH_MS);
    if (stop < pdMS_TO_TICKS(AP_TAIL_FINISH_MIN_MS))
        stop = pdMS_TO_TICKS(AP_TAIL_FINISH_MIN_MS);
    if (!wifi_manager_ap_stop_in((uint32_t)(stop - since) * portTICK_PERIOD_MS))
        return false;
    s_finish_stop = stop;
    s_finish_ip = ip;   // last: wifi_task reads it first
    uint32_t stop_ms = (uint32_t)stop * portTICK_PERIOD_MS;
    ESP_LOGI(WIFI_TAG, "Wi-Fi setup page: Finish - the SoftAP stops %lu.%lu s after the IP",
             (unsigned long)(stop_ms / 1000), (unsigned long)((stop_ms % 1000) / 100));
    if (wifiTaskHandle != NULL)
        xTaskNotifyGive(wifiTaskHandle);
    return true;
}

/* ---- The network list's scan: the LIST pulse (2.1.4 WP8; plan 4.4, I7) ---------------------
 * wifi_manager scans for the setup page's list only when the page orders it (its load with an
 * empty or stale list, its Rescan: C10b), and calls this gate right before each scan, on its own
 * task (wifi_manager_set_scan_gate()). It holds no lock there. The gate:
 *   - refuses the scan below LIST_DMA_MIN_FREE of internal DMA-capable heap (a scan's records and
 *     the driver's buffers come from it; plan 4.4), with a W line at most once a minute;
 *   - refuses it while a STA connect attempt is in flight (the router retry's, a Connect's): the
 *     driver refuses a scan while the STA connects (ESP_ERR_WIFI_STATE), and a pulse asked for one
 *     that cannot start would only make this task wait and restart the pulse spacing;
 *   - refuses it while a station joins the SoftAP (plan I7: one joined less than 10 s ago with no
 *     lease yet, or its join assist), before it asks and again after the wait (a join meanwhile),
 *     so the SoftAP stays on its channel for that station's DHCP;
 *   - asks the radio policy for a LIST pulse and waits for its answer, RP_GRANT_WAIT_MS (2 s) at
 *     most: granted at the end of a Coded window with room for RP_LIST_MAX_MS (2.5 s) in the
 *     blind budget and the pulse-rate limit; BLE then stops until the scan's SCAN_DONE
 *     (cb_scan_done()) or 2.5 s. FREE (no BLE scan runs) lets it run at once. Not granted (often
 *     right after a join assist, whose pulse spacing, 6-7 s, has not run out), the scan runs
 *     beside BLE, with a W line.
 * A refused scan counts as one that did not start (wifi_manager_scan_failed(): the page orders it
 * again 10 s later while it has no list, or at its Rescan). */
#define LIST_DMA_MIN_FREE   (24 * 1024)   // PROVISIONAL (plan 4.4; G0 re-derives it): internal DMA-capable
                                          // heap free for a list scan
#define LIST_LOW_LOG_MS     60000         // the low-heap refusal's line at most this often
static TickType_t s_list_low_logged = 0;  // wifi_manager task only

static bool list_scan_gate(void)
{
    TickType_t now = xTaskGetTickCount();
    size_t dma = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (dma < LIST_DMA_MIN_FREE)
    {
        if (s_list_low_logged == 0 || now - s_list_low_logged >= pdMS_TO_TICKS(LIST_LOW_LOG_MS))
        {
            s_list_low_logged = (now != 0) ? now : 1;
            ESP_LOGW(WIFI_TAG, "Wi-Fi list scan not started: internal DMA free %u B (needs %u) - the page asks again later",
                     (unsigned)dma, (unsigned)LIST_DMA_MIN_FREE);
        }
        return false;
    }
    if (s_attempt_in_flight)
    {
        ESP_LOGI(WIFI_TAG, "Wi-Fi list scan not started: a connect attempt is in flight - the page asks again later");
        return false;
    }
    rp_grant_t g = RP_GRANT_IDLE;
    bool joining = radio_policy_join_settling();
    if (!joining)
    {
        radio_policy_pulse_request(RP_PULSE_LIST);
        g = radio_policy_pulse_wait(RP_PULSE_LIST, RP_GRANT_WAIT_MS);
        joining = radio_policy_join_settling();   // a join during the wait
    }
    if (joining)
    {
        radio_policy_pulse_end(RP_PULSE_LIST);   // its pulse, granted or pending, ends
        ESP_LOGI(WIFI_TAG, "Wi-Fi list scan not started: a station is joining the SoftAP - the page asks again later");
        return false;
    }
    if (g == RP_GRANT_ON || g == RP_GRANT_FREE)
        return true;
    radio_policy_pulse_end(RP_PULSE_LIST);   // a request still pending is withdrawn
    ESP_LOGW(WIFI_TAG, "Wi-Fi list scan without a BLE pulse (%s) - it runs beside BLE scanning",
             (g == RP_GRANT_REFUSED) ? "not granted" : "no answer in 2 s");
    return true;
}

// WM_ORDER_START_AP (wifi_manager task), once the SoftAP, HTTP and DNS servers are up. The STA
// config is the network in use: what LOAD_AND_RESTORE read from NVS (all zero when nothing is
// saved), or what a forget zeroed and saved just before this START_AP. Only the wifi_manager task
// writes it (since 2.1.4 C8 a page's Connect changes it only at its candidate's IP).
static void cb_ap_started(void *pvParameter)
{
    (void)pvParameter;
    // The radio policy runs its AP modes from here (see the top): BLE leak scanning goes on beside
    // the SoftAP, with or without saved credentials (D2). In a router fallback wifi_task keeps
    // retrying the router, as it does whenever the STA is down (router_retry()).
    const wifi_config_t *sta = wifi_manager_get_wifi_sta_config();
    if (sta == NULL || sta->sta.ssid[0] == '\0')
        ESP_LOGI(WIFI_TAG, "SoftAP up with no saved Wi-Fi credentials (setup portal) - BLE scanning, if any, stays on beside it");
    else
        ESP_LOGI(WIFI_TAG, "SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on");
    rp_note_wifi(true);

    // The channels (see above). The SoftAP is configured with DEFAULT_AP_CHANNEL (11 since 2.1.4,
    // D7) when wifi_manager starts; wifi_settings.ap_channel is overwritten afterwards by the
    // settings blob of NVS, which on a hub set up before keeps the old value and configures nothing.
    if (s_router_channel != 0)
        ESP_LOGI(WIFI_TAG, "Wi-Fi channel at AP start: radio %u (SoftAP configured %u), router last seen on %u%s",
                 radio_channel(), (unsigned)DEFAULT_AP_CHANNEL, (unsigned)s_router_channel, scan_note());
    else
        ESP_LOGI(WIFI_TAG, "Wi-Fi channel at AP start: radio %u (SoftAP configured %u), router not joined since boot%s",
                 radio_channel(), (unsigned)DEFAULT_AP_CHANNEL, scan_note());
}

// WM_ORDER_STOP_AP (wifi_manager task). wifi_manager runs it only with the STA connected, once
// the SoftAP and its servers are stopped: at the stop the SoftAP's tail set, or from its
// backstop (see there). The radio policy leaves the SoftAP's tail (NORMAL either way).
// Idempotent: a second STOP_AP for the same IP (the backstop's after the timer's, or the other
// way round) finds the SoftAP down already, and prints nothing more.
// Parameter 1: wifi_manager could not leave APSTA mode (its E line says why), so the SoftAP and
// its servers stay up and it tries the stop again in 5 s: no fact changes, no "SoftAP stopped" line.
static void cb_ap_stopped(void *pvParameter)
{
    if ((uintptr_t)pvParameter != 0)
        return;   // the SoftAP is still up
    rp_note_wifi(false);
    TickType_t ip = s_ip_tick;
    if (ip != 0 && ip != s_ap_stop_seen)
    {
        s_ap_stop_seen = ip;
        uint32_t ms = (uint32_t)(xTaskGetTickCount() - ip) * portTICK_PERIOD_MS;
        ESP_LOGI(WIFI_TAG, "SoftAP stopped (its servers too) %lu.%lu s after the IP",
                 (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 100));
    }
}

// WM_ORDER_CONNECT_STA (wifi_manager task): wifi_manager decided an order (2.1.4 C8). The
// parameter is its kind, with WIFI_MANAGER_CONNECT_NOT_STARTED when it started no attempt (the STA
// connected, an attempt under way, a Connect's candidate first, or the driver refused it): the
// router retry counts from that too, and nothing else changes. An attempt that started: the router
// retry counts from it, the SoftAP tail's kind is marked, and the radio policy is told (an attempt
// in flight; a Connect's asks for the SUBMIT pulse).
static void cb_connect_sta(void *pvParameter)
{
    uint32_t info = (uint32_t)(uintptr_t)pvParameter;
    bool user = (info & WIFI_MANAGER_CONNECT_KIND_MASK) == CONNECTION_REQUEST_USER;
    TickType_t now = xTaskGetTickCount();
    if (info & WIFI_MANAGER_CONNECT_NOT_STARTED)
    {
        if (user)
            ESP_LOGI(WIFI_TAG, "Wi-Fi setup page: Connect - no attempt (the network in use, refused or replaced)");
        if (!s_attempt_in_flight)
            s_attempt_tick = (now != 0) ? now : 1;
        return;
    }
    s_attempt_tick = (now != 0) ? now : 1;   // 0 means "no attempt yet"
    s_attempt_in_flight = true;
    s_attempt_submit = user;   // the SoftAP tail's kind, if this attempt gets the IP (see there)
    if (user)
        ESP_LOGI(WIFI_TAG, "Wi-Fi setup page: Connect - attempt started");
    // The radio policy (see its facts above): an attempt in flight; a Connect's asks for the SUBMIT
    // pulse, and one of another kind ends a Connect that ended with no callback of its own.
    radio_policy_note_sta_attempt(true);
    if (user || s_submit_noted)
    {
        s_submit_noted = user;
        radio_policy_note_submit(user);
    }
}

// WM_ORDER_START_WIFI_SCAN (wifi_manager task): the portal page asked for the network list, and
// wifi_manager has started a scan, or skipped it (one already running, or its gate or the driver
// refused it). A scan that did not start ends the LIST pulse its gate may have been granted
// (list_scan_gate()). Nothing here stops a scan any more (2.1.4 WP8): the LIST pulse gives it the
// radio, and BLE its Coded windows around it.
static void cb_scan_start(void *pvParameter)
{
    (void)pvParameter;
    if (!wifi_manager_scan_in_flight())
        radio_policy_pulse_end(RP_PULSE_LIST);
}

// WM_EVENT_SCAN_DONE (wifi_manager task), once the list is rebuilt (or kept, for a scan that
// failed or was stopped): the LIST pulse ends, and BLE resumes with a Coded recovery window.
static void cb_scan_done(void *pvParameter)
{
    (void)pvParameter;
    radio_policy_pulse_end(RP_PULSE_LIST);
}

/* ---- The portal's forget ------------------------------------------------------------------
 * The portal page's Disconnect (DELETE /connect.json, kept: D9) and the 10 s reset send
 * wifi_manager a DISCONNECT_STA. Since 2.1.4 C8 wifi_manager does the whole forget itself, in
 * every state of the STA: idle, it erases at once (zero SSID and password over the saved ones,
 * and START_AP: the no-credential setup portal, cb_ap_started()); connecting or connected,
 * the STA_DISCONNECTED that follows erases before anything else it does, and one that never comes
 * erases 2 s later. A forget also drops a Connect's candidate. The app's own forget (an event it
 * posted for an idle STA, and a forget it kept pending through a user connect) is gone with it:
 * nothing of it is left waiting for a later link loss. */

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
 * not seen joining (one that joined before this log was registered) gets an entry of its own
 * while one is free. The portal reports only clients in the SoftAP's subnet (its C3: a home-LAN
 * client gets 403 or no DNS reply, and no call). Each SoftAP start begins a new session with an
 * empty table.
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
    [HTTP_APP_ACT_API_USER] = "user request (list, Rescan, Connect, Disconnect or Finish)",
    [HTTP_APP_ACT_API_BG] = "background poll (list)",
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
    // The radio policy first (2.1.4 WP8): the page in use (SERVE), a hot event, a station's first
    // page or 302 (its join assist's end). Stores and wakes only.
    radio_policy_portal_activity(kind, client_ip);
    // The page in use, for the router retry (page_in_use()): the page, its API, its polls.
    if (kind == HTTP_APP_ACT_PAGE || kind == HTTP_APP_ACT_API_USER || kind == HTTP_APP_ACT_API_BG)
    {
        TickType_t t = xTaskGetTickCount();
        s_page_tick = (t != 0) ? t : 1;
    }
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
        radio_policy_station_joined(e->mac);   // its join assist, if its spacing allows
        ESP_LOGI(WIFI_TAG, "SoftAP: station %02X:%02X:%02X:%02X:%02X:%02X joined, AID=%u",
                 e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5],
                 (unsigned)e->aid);
    }
    else if (id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        const wifi_event_ap_stadisconnected_t *e = (const wifi_event_ap_stadisconnected_t *)data;
        ap_client_left(e->mac);
        radio_policy_station_left(e->mac);
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
    radio_policy_station_leased(e->mac, e->ip.addr);   // its join assist's end rule; provisional SERVE
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
    // The page's Finish (2.1.4 C12): the AP-tail policy's, below. A plain store too.
    http_app_set_finish_hook(&portal_finish);
    // The network list's scan gate (2.1.4 WP8: the LIST pulse, above). A plain store too.
    wifi_manager_set_scan_gate(&list_scan_gate);
#if CONFIG_APP_RADIO_LAB
    // The G1 lab image (main/Kconfig.projbuild): its settings, before the Wi-Fi task starts.
    radio_lab_init();
#endif
    wifi_manager_start();
    // The SoftAP's and the STA's callbacks first (the radio policy's facts): with no credentials
    // saved, START_AP comes about 0.7 s after the start (network and Wi-Fi init, the HTTP server),
    // while these calls take microseconds. Not before the start: wifi_manager_start() allocates
    // the callback table, and a callback set before it is silently dropped.
    wifi_manager_set_callback(WM_ORDER_START_AP, &cb_ap_started);
    wifi_manager_set_callback(WM_ORDER_STOP_AP, &cb_ap_stopped);
    wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_connection_ok);
    wifi_manager_set_callback(WM_EVENT_STA_DISCONNECTED, &cb_connection_lost);
    // The attempts' and the network list's scan (the SUBMIT and LIST pulses). Nothing else
    // registers these three.
    wifi_manager_set_callback(WM_ORDER_CONNECT_STA, &cb_connect_sta);
    wifi_manager_set_callback(WM_ORDER_START_WIFI_SCAN, &cb_scan_start);
    wifi_manager_set_callback(WM_EVENT_SCAN_DONE, &cb_scan_done);
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
    ESP_LOGI(WIFI_TAG, "Wi-Fi channel at IP: radio %u, router %u%s", radio_channel(), (unsigned)router,
             scan_note());

    // The attempt that got here is over.
    s_sta_connected = true;
    s_attempt_in_flight = false;
    // The radio policy: the STA has its IP (with the SoftAP up, its tail runs NORMAL), the attempt
    // and a Connect's SUBMIT are over.
    rp_attempt_over();
    rp_note_wifi(softap_up());

    // The SoftAP's tail, with the SoftAP up (see there): its kind and its first stop.
    ap_tail_start();

    // BLE is not started here, and never waits for Wi-Fi: iothub_task starts it at boot
    // from the provisioned device set, together with leak protection.

    // 1. Network LED -> "connecting" (beat blue). The MQTT handler promotes it
    //    to "connected" (ramp blue) once the IoT Hub session is up.
    net_status_set_wifi(true);

    // 2. Tell iothub_task the network is up: a flag and a wake (2.1.4 WP2). Its loop starts
    //    SNTP, and MQTT, DPS and the SAS mint once its cloud admission lets them in: with
    //    the SoftAP down (after the AP tail) and internal heap to spare. This task no longer
    //    starts MQTT itself (TLS beside the SoftAP, E4).
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
        ESP_LOGI(WIFI_TAG, "Wi-Fi channel at link loss: radio %u, router was on %u%s",
                 radio_channel(), (unsigned)s_router_channel, scan_note());

    // No IP any more: no SoftAP tail and no backstop (a SoftAP that is up stays up, as a
    // router-fallback portal). The attempt's mark ends with it (see the SoftAP's tail above).
    s_ip_tick = 0;
    s_attempt_submit = false;

    // The link was lost, or a connect attempt ended. The router retry counts from here too: this
    // disconnect may have armed wifi_manager's own retry timer (see the router retry above).
    s_sta_connected = false;
    s_attempt_in_flight = false;
    TickType_t now = xTaskGetTickCount();
    s_attempt_tick = (now != 0) ? now : 1;   // 0 means "no attempt yet"
    // The radio policy: no IP (with the SoftAP up the AP modes run), the attempt and a Connect's
    // SUBMIT are over.
    rp_attempt_over();
    rp_note_wifi(softap_up());

    // Network LED -> "no internet" (ramp red). This also clears the MQTT flag
    // inside net_status so a later reconnect shows "connecting" first.
    net_status_set_wifi(false);

    // Tell iothub_task: a flag, a count and a wake (2.1.4 WP2). Its next pass withdraws the
    // cloud admission and asks wifi_task to stop the MQTT client, so it doesn't thrash TLS
    // handshakes (fragmenting the heap the SoftAP captive portal needs) while STA is down /
    // in AP mode. This task no longer waits on the MQTT control mutex and
    // esp_mqtt_client_stop() here.
    iothub_on_wifi_lost();
}

// wifi_task's own state: the router retry, the SoftAP's tail and the SoftAP fact's cross-check.
typedef struct
{
    unsigned retries;        // router retries sent since the fallback began
    bool defer_logged;       // the "retry deferred" line is printed for the page open now
    TickType_t retry_mark;   // the attempt (s_attempt_tick) the retry's jitter was drawn for
    uint32_t retry_jitter_ms;   // ... and that jitter
    TickType_t join_defer_at;   // the retry has waited for a station joining the SoftAP since (0: not)
    uint8_t ap_mismatch;     // passes in a row the SoftAP fact and the driver's mode disagreed
    TickType_t tail_ip;      // the IP (s_ip_tick) whose SoftAP tail is followed, 0 = none
    TickType_t tail_stop;    // that tail's stop as last set, in ticks after the IP
    TickType_t tail_empty;   // ticks after the IP when the SoftAP was first seen with no station, 0 = one on it
    TickType_t backstop_for; // the IP the backstop sent its STOP_AP for
} wifi_task_state_t;

// The STA is down with credentials in its config, SoftAP up or not: wifi_manager's fallback AP
// after its retries, a SoftAP left up after a link lost in its tail, or the STA left idle with no
// AP (see the router retry above). The flags are the wifi_manager task's; a stale read costs one
// pass.
static bool router_fallback(void)
{
    if (s_sta_connected)
        return false;
    const wifi_config_t *sta = wifi_manager_get_wifi_sta_config();
    return sta != NULL && sta->sta.ssid[0] != '\0';
}

// Plan I7: a station joining the SoftAP (no lease yet, or its join assist) holds the router retry
// back, so the SoftAP stays on its channel for that station's DHCP; ROUTER_RETRY_JOIN_MAX_MS at
// most per retry, so stations that keep joining cannot hold the hub off its router. True: wait
// (the next pass tries again).
static bool retry_join_wait(wifi_task_state_t *st, TickType_t now)
{
    if (!radio_policy_join_settling())
        return false;
    if (st->join_defer_at == 0)
        st->join_defer_at = (now != 0) ? now : 1;
    return now - st->join_defer_at < pdMS_TO_TICKS(ROUTER_RETRY_JOIN_MAX_MS);
}

// The router retry (see above), on every wifi_task pass: every second while the STA is down.
// wifi_manager_retry_async() waits up to WIFI_MANAGER_POST_WAIT_MS for room in wifi_manager's
// queue, so it is sent from here, never from a wifi_manager callback.
static void router_retry(wifi_task_state_t *st)
{
    TickType_t now = xTaskGetTickCount();
    if (!router_fallback())
    {
        st->retries = 0;
        st->defer_logged = false;
        return;
    }
    // Since the last attempt started or ended, with a jitter drawn for it. None before
    // wifi_manager's first: at boot its restore's CONNECT_STA can still be queued (a retry then
    // would start nothing).
    TickType_t mark = s_attempt_tick;
    if (mark == 0)
        return;
    if (mark != st->retry_mark)
    {
        st->retry_mark = mark;
        st->retry_jitter_ms = esp_random() % (ROUTER_RETRY_JITTER_MS + 1);
        st->join_defer_at = 0;
    }
    TickType_t age = now - mark;
    if (s_attempt_in_flight || age < pdMS_TO_TICKS(ROUTER_RETRY_MS + st->retry_jitter_ms))
        return;
    if (page_in_use(now) && age < pdMS_TO_TICKS(ROUTER_RETRY_PAGE_MAX_MS))
    {
        if (!st->defer_logged)
        {
            st->defer_logged = true;
            ESP_LOGI(WIFI_TAG, "router fallback: retry deferred - the Wi-Fi setup page is open");
        }
        return;
    }
    st->defer_logged = false;
    if (retry_join_wait(st, now))
        return;

    // The RETRY pulse (see above): asked for, then its answer, 2 s at most. This task holds no lock.
    radio_policy_pulse_request(RP_PULSE_RETRY);
    rp_grant_t g = radio_policy_pulse_wait(RP_PULSE_RETRY, RP_GRANT_WAIT_MS);
    // Again after the wait: an attempt may have started (a portal submit) or ended meanwhile, the
    // page may be in use again, a station may have started joining, or the STA or the config
    // changed. Then nothing is sent, and the pulse, if granted, is ended.
    now = xTaskGetTickCount();
    if (!router_fallback() || s_attempt_in_flight || s_attempt_tick != mark ||
        (page_in_use(now) && now - mark < pdMS_TO_TICKS(ROUTER_RETRY_PAGE_MAX_MS)) ||
        retry_join_wait(st, now))
    {
        radio_policy_pulse_end(RP_PULSE_RETRY);
        return;
    }
    if (g != RP_GRANT_ON && g != RP_GRANT_FREE)
    {
        radio_policy_pulse_end(RP_PULSE_RETRY);   // a request still pending is withdrawn
        ESP_LOGW(WIFI_TAG, "router fallback: retry without a BLE pulse (%s) - its connect runs beside BLE scanning",
                 (g == RP_GRANT_REFUSED) ? "not granted" : "no answer in 2 s");
    }
    // Not queued within WIFI_MANAGER_POST_WAIT_MS (2.1.4 C6): tried again on a later pass.
    if (!wifi_manager_retry_async())
    {
        radio_policy_pulse_end(RP_PULSE_RETRY);
        ESP_LOGW(WIFI_TAG, "router fallback: retry not sent (wifi_manager queue full)");
        return;
    }
    st->retries++;
    // The network in use (2.1.4 C8: never what a failed Connect typed); "configured" kept for the
    // bench logs.
    ESP_LOGI(WIFI_TAG, "router fallback: retrying the configured network (attempt %u)", st->retries);
}

// The SoftAP's tail (see there), on every wifi_task pass: one a second while a tail is followed.
// Moves the stop by the stations on the SoftAP, sets a first stop ap_tail_start() could not set
// (the timer task's queue full), and sends the backstop's STOP_AP. Writes only its
// own state: the stop goes through wifi_manager_ap_stop_in() (never waits), the backstop's order
// through wifi_manager's queue, as the router retry's does.
static void ap_tail_maintain(wifi_task_state_t *st)
{
    TickType_t ip = s_ip_tick;   // first: the rest of this IP's tail is written before it
    if (ip == 0 || !s_sta_connected || !softap_up())
    {
        st->tail_ip = 0;
        return;
    }
    TickType_t since = xTaskGetTickCount() - ip;

    // The backstop, once per IP. wifi_manager stops the SoftAP only with the STA connected.
    if (since >= pdMS_TO_TICKS(AP_TAIL_BACKSTOP_MS))
    {
        // The order waits WIFI_MANAGER_POST_WAIT_MS at most (2.1.4 C6); one not taken is sent
        // again on the next pass.
        if (st->backstop_for != ip &&
            wifi_manager_send_message_wait(WM_ORDER_STOP_AP, NULL, pdMS_TO_TICKS(WIFI_MANAGER_POST_WAIT_MS)) == pdPASS)
        {
            st->backstop_for = ip;
            ESP_LOGW(WIFI_TAG, "SoftAP still up %u s after Wi-Fi connected - stopping it",
                     (unsigned)(since / configTICK_RATE_HZ));
        }
        return;
    }

    uint32_t cap_ms = s_tail_cap_ms;
    if (cap_ms == 0)
        return;   // the SoftAP was down at the IP and came up later: the backstop only
    TickType_t cap = (TickType_t)(cap_ms / portTICK_PERIOD_MS);
    if (st->tail_ip != ip)
    {
        st->tail_ip = ip;
        st->tail_stop = s_tail_armed ? cap : AP_TAIL_STOP_UNSET;   // as ap_tail_start() set it, or not
        st->tail_empty = 0;
    }
    int stations = ap_station_count();
    if (stations < 0)
        return;   // the driver cannot say: the next pass decides

    // The stop: the cap, or sooner once no station is left (never before the Connect's minimum).
    TickType_t stop = cap;
    // The page's Finish for this IP (portal_finish()), when it is sooner; no station rule below
    // moves a stop later than it. Its IP first: the stop is written before it.
    TickType_t finish_stop = 0;
    if (s_finish_ip == ip)
    {
        finish_stop = s_finish_stop;
        if (finish_stop < stop)
            stop = finish_stop;
    }
    if (stations > 0)
        st->tail_empty = 0;
    else
    {
        if (st->tail_empty == 0)
            st->tail_empty = (since != 0) ? since : 1;
        TickType_t by_left = st->tail_empty +
                             (s_tail_submit ? pdMS_TO_TICKS(AP_TAIL_SUBMIT_LEFT_MS) : pdMS_TO_TICKS(AP_TAIL_AUTO_LEFT_MS));
        if (s_tail_submit && by_left < pdMS_TO_TICKS(AP_TAIL_SUBMIT_MIN_MS))
            by_left = pdMS_TO_TICKS(AP_TAIL_SUBMIT_MIN_MS);
        if (by_left < stop)
            stop = by_left;
    }
    if (stop == st->tail_stop)
        return;
    const char *why = (st->tail_stop == AP_TAIL_STOP_UNSET) ? "its stop not set at the IP, set now" :
                      (finish_stop != 0 && stop == finish_stop) ? "Finish on the setup page" :
                      (stations > 0) ? "a station on it again" : "no station left on it";
    TickType_t in = (stop > since) ? stop - since : 0;
    if (!wifi_manager_ap_stop_in((uint32_t)in * portTICK_PERIOD_MS))
        return;   // tried again on the next pass
    st->tail_stop = stop;
    uint32_t stop_ms = (uint32_t)stop * portTICK_PERIOD_MS;
    ESP_LOGI(WIFI_TAG, "SoftAP tail: %s - it stops %lu.%lu s after the IP",
             why, (unsigned long)(stop_ms / 1000), (unsigned long)((stop_ms % 1000) / 100));
}

// The SoftAP fact the wifi_manager task gave the radio policy, against the driver's mode (plan
// 4.1's cross-check, on every wifi_task pass): logged only, once a disagreement has lasted
// AP_FACT_PASSES passes (a mode switch and its callback are microseconds apart), and once when it
// ends. The callbacks stay the fact's one writer.
#define AP_FACT_PASSES 3
static void ap_fact_check(wifi_task_state_t *st)
{
    bool driver = softap_up();
    bool told = s_rp_ap_up;
    if (driver == told)
    {
        if (st->ap_mismatch >= AP_FACT_PASSES)
            ESP_LOGI(WIFI_TAG, "radio policy: the SoftAP fact agrees with the Wi-Fi mode again");
        st->ap_mismatch = 0;
        return;
    }
    if (st->ap_mismatch < UINT8_MAX)
        st->ap_mismatch++;
    if (st->ap_mismatch == AP_FACT_PASSES)
        ESP_LOGW(WIFI_TAG, "radio policy: SoftAP %s by the Wi-Fi mode but %s by the wifi_manager callbacks - the radio modes follow the callbacks",
                 driver ? "up" : "down", told ? "up" : "down");
}

// The radio policy's stations, pruned against the driver's list (plan 4.1): a station that left
// with no event of its own. In a frame of its own: the list is about 0.2 KB.
static __attribute__((noinline)) void ap_stations_prune(void)
{
    wifi_sta_list_t list;
    if (!softap_up())
        radio_policy_stations_prune(NULL);
    else if (esp_wifi_ap_get_sta_list(&list) == ESP_OK)
        radio_policy_stations_prune(&list);
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
    // Each pass also runs the SoftAP's tail and its backstop (ap_tail_maintain()), the SoftAP
    // fact's cross-check (ap_fact_check()), the radio policy's stations against the driver's list
    // (ap_stations_prune()) and the router retry (router_retry()): every second while the STA is
    // down or a tail is followed, else every 5 s.
    // First on each pass, the MQTT client's stop when iothub_task has asked for one (2.1.4: a
    // link loss, a SoftAP start, a SAS renewal). iothub_task evaluates the leaks, so it never
    // waits in esp_mqtt_client_stop(); this task can: about 1 s in a session, up to 5 s
    // between esp-mqtt's reconnects, 10-30 s with a connect in flight, once per outage or
    // renewal (iothub_mqtt_stop_service()). First, so it never falls between a router retry's
    // grant and its order. iothub_task's ask wakes the pass (a task notification); every other
    // wake is harmless, as each step reads its own facts and ticks.
    bool ap_log_on = false;
    bool lease_log_on = false;
    wifi_task_state_t st = { 0 };
    while (1)
    {
        bool fast = !ap_log_on || !lease_log_on || !s_sta_connected || st.tail_ip != 0;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(fast ? 1000 : 5000));
        iothub_mqtt_stop_service();
        if (!ap_log_on)
            ap_log_on = (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                    &ap_station_event_handler, NULL) == ESP_OK);
        if (!lease_log_on)
            lease_log_on = (esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED,
                                                       &ap_lease_event_handler, NULL) == ESP_OK);
        ap_tail_maintain(&st);
        ap_fact_check(&st);
        ap_stations_prune();
        router_retry(&st);
    }
    vTaskDelete(NULL);
}