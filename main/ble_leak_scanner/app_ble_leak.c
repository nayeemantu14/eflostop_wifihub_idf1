/****************************************************
 *  MODULE:   BLE Leak Sensor Scanner
 *  PURPOSE:  Passive BLE scanner for "eleak" leak sensors.
 *            Uses extended scanning (ble_gap_ext_disc) to receive
 *            both legacy 1M and Coded PHY advertisements, enabling
 *            support for STM32WB (legacy) and STM32WBA (long range)
 *            leak sensors simultaneously.
 *            Parses manufacturer-specific advertising data
 *            (company ID 0x0030) for leak status and battery.
 *            Commissioned sensors are whitelisted by MAC from
 *            the provisioning manager (Azure C2D).
 ****************************************************/

#include "app_ble_leak.h"
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "provisioning_manager/provisioning_manager.h"
#include "health_engine/health_engine.h"

/* ---------------------------------------------------------
 * Constants
 * --------------------------------------------------------- */
#define BLE_LEAK_TAG            "BLE_LEAK"
#define ELEAK_COMPANY_ID        0x0030      // ST Microelectronics
#define ELEAK_MFG_DATA_LEN      4           // 2B company ID + 1B leak + 1B battery
#define ELEAK_DEVICE_NAME       "eleak"
#define ELEAK_DEVICE_NAME_LEN   5
#define MAX_TRACKED_SENSORS     MAX_BLE_LEAK_SENSORS
#define SCAN_RESTART_DELAY_MS   500
#define WHITELIST_RELOAD_MS     10000       // Re-check provisioning every 10s
#define BLE_LEAK_HEARTBEAT_MS   (5 * 60 * 1000)  // 5-min TELEMETRY heartbeat: how often an
                                                 // unchanged sensor still produces a D2C event.
                                                 // This used to gate the health-engine check-in
                                                 // too — see health_post_ble_leak_checkin() below
                                                 // for why that was wrong.
#define HEALTH_CHECKIN_MIN_MS   5000             // Min spacing between health check-ins per
                                                 // sensor. A burst is several advertisements over
                                                 // a second or two; the health engine needs one
                                                 // of them, not all of them.

/* ---------------------------------------------------------
 * Internal types
 * --------------------------------------------------------- */
// Per-sensor state for delta/dedup tracking. Keyed by MAC (find-or-allocate), NOT by
// whitelist index: a removal shifts the whitelist, and an index key then handed one
// sensor's seen/last_leak history to its neighbour.
typedef struct {
    bool in_use;            // slot holds a whitelisted sensor's state
    uint8_t mac[6];
    uint8_t last_battery;
    bool last_leak;
    char last_fw_version[12];
    bool seen;              // true after first advertisement received
    TickType_t last_event_tick;    // last telemetry event (drives BLE_LEAK_HEARTBEAT_MS)
    TickType_t last_health_tick;   // last health check-in (drives HEALTH_CHECKIN_MIN_MS)
} sensor_state_t;

/* ---------------------------------------------------------
 * Static variables
 * --------------------------------------------------------- */
QueueHandle_t ble_leak_rx_queue = NULL;

static TaskHandle_t ble_leak_task_handle = NULL;
static volatile bool s_scan_restart_needed = false;

// Cached whitelist from provisioning manager
static uint8_t s_whitelist[MAX_TRACKED_SENSORS][6];
static uint8_t s_whitelist_count = 0;

// Per-sensor tracking for dedup
static sensor_state_t s_sensors[MAX_TRACKED_SENSORS];

/* Guards s_whitelist[], s_whitelist_count and s_sensors[].
 *
 * The whitelist is rewritten by the scan task (every 10 s) while the NimBLE host task
 * reads it for every advertisement, and 2.1.3 published the new count BEFORE the new
 * entries — an advertisement in between matched a half-written list. A spinlock rather
 * than a mutex because the reader is the NimBLE host task, which must never block on the
 * scan task. Sections hold for microseconds: NO logging, NO queue sends, and no calls
 * other than memcmp/memcpy/memset and the *_locked helpers below. */
static portMUX_TYPE s_wl_lock = portMUX_INITIALIZER_UNLOCKED;

/* ---------------------------------------------------------
 * Helper: parse MAC string "XX:XX:XX:XX:XX:XX" to 6-byte array
 * NimBLE stores addresses LSB-first, so we reverse the byte order.
 * "00:80:E1:27:9A:E6" → [0xE6, 0x9A, 0x27, 0xE1, 0x80, 0x00]
 * Returns false (out untouched) unless all six fields parsed.
 * --------------------------------------------------------- */
static bool mac_str_to_bytes(const char *str, uint8_t *out)
{
    unsigned int b[6];
    if (sscanf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        out[i] = (uint8_t)b[5 - i];
    }
    return true;
}

/* ---------------------------------------------------------
 * Helper: format NimBLE 6-byte MAC (LSB-first) to string "XX:XX:XX:XX:XX:XX"
 * [0xE6, 0x9A, 0x27, 0xE1, 0x80, 0x00] → "00:80:E1:27:9A:E6"
 * --------------------------------------------------------- */
static void mac_bytes_to_str(const uint8_t *mac, char *out)
{
    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
}

/* ---------------------------------------------------------
 * Lookups — call with s_wl_lock held
 * --------------------------------------------------------- */

// Index of mac in the whitelist, or -1.
static int whitelist_find_locked(const uint8_t *mac)
{
    for (int i = 0; i < s_whitelist_count; i++) {
        if (memcmp(s_whitelist[i], mac, 6) == 0) {
            return i;
        }
    }
    return -1;
}

// Index of the tracking slot for mac, or -1.
static int sensor_find_locked(const uint8_t *mac)
{
    for (int i = 0; i < MAX_TRACKED_SENSORS; i++) {
        if (s_sensors[i].in_use && memcmp(s_sensors[i].mac, mac, 6) == 0) {
            return i;
        }
    }
    return -1;
}

// Claim a free slot for mac (fresh state, seen=false), or -1 if none. Cannot fail for a
// whitelisted MAC: slots are pruned to whitelisted MACs in the same section that swaps the
// list, and the list holds at most MAX_TRACKED_SENSORS entries.
static int sensor_alloc_locked(const uint8_t *mac)
{
    for (int i = 0; i < MAX_TRACKED_SENSORS; i++) {
        if (!s_sensors[i].in_use) {
            memset(&s_sensors[i], 0, sizeof(s_sensors[i]));
            memcpy(s_sensors[i].mac, mac, 6);
            s_sensors[i].in_use = true;
            return i;
        }
    }
    return -1;
}

/* ---------------------------------------------------------
 * Reload whitelist from provisioning manager
 * --------------------------------------------------------- */
static void reload_whitelist(void)
{
    /* The atomic device-set read, not provisioning_get_ble_leak_sensors(): that getter
     * returns false for BOTH "no sensors" and "mutex busy", and the old code treated
     * false as "no sensors" — one busy mutex blanked the whitelist and deafened the hub
     * to every leak sensor for up to 10 s. A failed read now keeps the current list. */
    prov_device_set_t set;
    if (!provisioning_get_device_set(&set)) {
        ESP_LOGW(BLE_LEAK_TAG, "Whitelist reload skipped (provisioning busy) - keeping %d sensor(s)",
                 (int)s_whitelist_count);
        return;
    }

    // Convert OUTSIDE the lock (sscanf is not a critical-section call).
    uint8_t wl[MAX_TRACKED_SENSORS][6];
    memset(wl, 0, sizeof(wl));
    uint8_t count = 0;
    for (int i = 0; i < set.ble_count && count < MAX_TRACKED_SENSORS; i++) {
        if (!mac_str_to_bytes(set.ble_macs[i], wl[count])) {
            ESP_LOGW(BLE_LEAK_TAG, "Whitelist: invalid MAC '%s' skipped", set.ble_macs[i]);
            continue;
        }
        count++;
    }

    uint32_t sum = count;
    for (int i = 0; i < count; i++) {
        for (int b = 0; b < 6; b++) sum = sum * 31u + wl[i][b];
    }

    /* Swap the list and prune the tracking of every MAC no longer on it, in ONE section,
     * so an advertisement can never see the new count with the old entries, nor find a
     * slot left over from a removed sensor (a re-added sensor starts from seen=false). */
    taskENTER_CRITICAL(&s_wl_lock);
    memcpy(s_whitelist, wl, sizeof(s_whitelist));
    s_whitelist_count = count;
    for (int i = 0; i < MAX_TRACKED_SENSORS; i++) {
        if (s_sensors[i].in_use && whitelist_find_locked(s_sensors[i].mac) < 0) {
            memset(&s_sensors[i], 0, sizeof(s_sensors[i]));
        }
    }
    taskEXIT_CRITICAL(&s_wl_lock);

    // This runs every 10 s; only log when the whitelist actually changes so
    // the trace isn't flooded with identical "reloaded" lines.
    static uint32_t s_prev_wl_sum = 0xFFFFFFFFu;
    if (sum != s_prev_wl_sum) {
        s_prev_wl_sum = sum;
        ESP_LOGI(BLE_LEAK_TAG, "Whitelist reloaded: %d sensor(s)", count);
    }
}

/* ---------------------------------------------------------
 * Common advertisement processing for leak sensors.
 * Called from both legacy (BLE_GAP_EVENT_DISC) and extended
 * (BLE_GAP_EVENT_EXT_DISC) event handlers.
 * --------------------------------------------------------- */
static void process_leak_adv(const ble_addr_t *addr, int8_t rssi,
                             const uint8_t *data, uint8_t data_len)
{
    // Parse advertisement fields
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, data, data_len) != 0) {
        return;
    }

    // Check device name matches "eleak" (case-insensitive)
    if (fields.name == NULL || fields.name_len != ELEAK_DEVICE_NAME_LEN) {
        return;
    }
    if (strncasecmp((const char *)fields.name, ELEAK_DEVICE_NAME, ELEAK_DEVICE_NAME_LEN) != 0) {
        return;
    }

    // Get advertiser MAC (NimBLE stores as addr.val[6], byte 0 = LSB)
    const uint8_t *adv_mac = addr->val;

    // Verify manufacturer-specific data
    if (fields.mfg_data == NULL || fields.mfg_data_len < ELEAK_MFG_DATA_LEN) {
        return;
    }

    // Verify company ID (little-endian: 0x30, 0x00 = 0x0030)
    uint16_t company_id = (uint16_t)fields.mfg_data[0] | ((uint16_t)fields.mfg_data[1] << 8);
    if (company_id != ELEAK_COMPANY_ID) {
        return;
    }

    // Extract payload
    uint8_t leak_status = fields.mfg_data[2];
    uint8_t battery     = fields.mfg_data[3];
    bool leak = (leak_status != 0);

    // Parse firmware version from extended mfg data bytes [4..6] if present
    char fw_ver[12] = {0};
    if (fields.mfg_data_len >= 7) {
        snprintf(fw_ver, sizeof(fw_ver), "%u.%u.%u",
                 fields.mfg_data[4], fields.mfg_data[5], fields.mfg_data[6]);
    }

    /* Whitelist check + tracking-slot lookup in ONE short critical section, then work on
     * a local copy: the scan task may swap the whitelist and prune slots at any moment,
     * so s_sensors[] is never read or written here without the lock. */
    sensor_state_t snap;
    memset(&snap, 0, sizeof(snap));
    bool listed;
    int slot = -1;
    taskENTER_CRITICAL(&s_wl_lock);
    listed = (whitelist_find_locked(adv_mac) >= 0);
    if (listed) {
        slot = sensor_find_locked(adv_mac);
        if (slot < 0) slot = sensor_alloc_locked(adv_mac);
        if (slot >= 0) memcpy(&snap, &s_sensors[slot], sizeof(snap));
    }
    taskEXIT_CRITICAL(&s_wl_lock);
    if (!listed || slot < 0) {
        return;  // Not a commissioned sensor
    }
    const sensor_state_t *s = &snap;

    char mac_str[18];
    mac_bytes_to_str(adv_mac, mac_str);

    /* ---- HEALTH CHECK-IN: before the telemetry delta gate, deliberately ----
     *
     * Liveness and telemetry are different questions and must not share a gate.
     * This call used to sit at the very bottom of the function, behind the
     * `!data_changed && !heartbeat_due` early return below, so the health engine
     * only learned the sensor was alive when a D2C event was also due — once every
     * BLE_LEAK_HEARTBEAT_MS (5 min), even though a dry sensor bursts roughly every
     * 100 s and we hear it every time.
     *
     * Against HEALTH_BLE_LEAK_TIMEOUT_MS (10 min) that left barely 2x margin on a
     * signal we were throttling ourselves: ONE missed 5-minute post and a perfectly
     * healthy sensor was declared offline, which took the whole roll-up CRITICAL and
     * the fleet LED red. It also made HEALTH_BOOT_SYNC_TIMEOUT_MS unsatisfiable,
     * because the boot window was shorter than the gate it was waiting on.
     *
     * Posting every burst gives ~6x margin instead, and makes last_seen_age_s mean
     * "when we last heard it" rather than "when we last talked about it".
     *
     * The telemetry gate below is untouched — no change to what goes on the wire.
     *
     * THE THROTTLE MUST NEVER GATE THE WET/DRY EDGE. It exists only to decimate
     * redundant liveness posts within a single burst. Since `leaking` now rides along
     * on this same helper, a purely time-based gate would drop the dry->wet
     * transition itself: a sensor that bursts dry at t0 and is wetted at t0+1.5 s
     * emits its whole wet burst inside the 5 s window, so the health engine would
     * keep leaking=false and the roll-up would keep reporting excellent/GREEN for up
     * to a full burst cycle (~100 s) — reintroducing exactly the false-green this
     * release exists to remove. `s->last_leak` still holds the PREVIOUS value here
     * (it is committed further down, below the telemetry gate), so it is the correct
     * edge reference. */
    TickType_t now_tick = xTaskGetTickCount();
    bool leak_edge = !s->seen || (leak != s->last_leak);
    if (leak_edge || (now_tick - s->last_health_tick) >= pdMS_TO_TICKS(HEALTH_CHECKIN_MIN_MS)) {
        health_post_ble_leak_checkin(mac_str, battery, rssi, leak);
        // Commit to the live slot only if it still belongs to this sensor: a whitelist
        // reload may have pruned (and even re-used) it since the lookup above.
        taskENTER_CRITICAL(&s_wl_lock);
        if (s_sensors[slot].in_use && memcmp(s_sensors[slot].mac, adv_mac, 6) == 0) {
            s_sensors[slot].last_health_tick = now_tick;
        }
        taskEXIT_CRITICAL(&s_wl_lock);
    }

    // Delta check: skip if unchanged from last report (unless heartbeat due)
    bool data_changed = !s->seen || s->last_leak != leak || s->last_battery != battery
                        || strcmp(s->last_fw_version, fw_ver) != 0;
    bool heartbeat_due = s->seen &&
        ((now_tick - s->last_event_tick) >= pdMS_TO_TICKS(BLE_LEAK_HEARTBEAT_MS));
    if (!data_changed && !heartbeat_due) {
        return;  // No change and heartbeat not due, skip the D2C event
    }

    // Build event and enqueue
    ble_leak_event_t evt;
    memcpy(evt.sensor_mac, adv_mac, 6);
    strncpy(evt.sensor_mac_str, mac_str, sizeof(evt.sensor_mac_str) - 1);
    evt.sensor_mac_str[sizeof(evt.sensor_mac_str) - 1] = '\0';
    evt.battery = battery;
    evt.leak_detected = leak;
    evt.rssi = rssi;
    strncpy(evt.fw_version, fw_ver, sizeof(evt.fw_version) - 1);
    evt.fw_version[sizeof(evt.fw_version) - 1] = '\0';

    ESP_LOGI(BLE_LEAK_TAG, "eleak %s — leak=%d batt=%d%% rssi=%d fw=%s",
             evt.sensor_mac_str, leak, battery, evt.rssi,
             fw_ver[0] ? fw_ver : "n/a");

    /* COMMIT THE TRACKED STATE ONLY IF THE EVENT WAS ACTUALLY ENQUEUED.
     *
     * This used to update s->last_leak/battery/fw/seen BEFORE the send, and ignore the
     * send's return. A full ble_leak_rx_queue therefore consumed the dry->wet delta and
     * threw the event away: every later advertisement in the burst compared equal to the
     * just-committed state, so no retry happened and rules_engine_evaluate_leak() — the
     * only thing that closes the valve for a SENSOR leak — did not run until the 5-minute
     * telemetry heartbeat forced an event through. Five minutes of water, while the health
     * roll-up and the fleet LED (fed by the separate check-in above, which is not queued
     * behind this) already showed the leak. The indicator would have been right and the
     * valve still open.
     *
     * Leaving the state uncommitted makes the very next advertisement in the same burst
     * (~300 ms later) look like a fresh delta and retry. */
    if (xQueueSend(ble_leak_rx_queue, &evt, 0) != pdTRUE) {
        ESP_LOGW(BLE_LEAK_TAG, "ble_leak_rx_queue FULL — %s event held for retry on next advertisement",
                 evt.sensor_mac_str);
        return;   /* deliberately do NOT commit s->* — the delta must survive */
    }

    // Update tracked state — only now that the event is safely queued, and only if the
    // slot still belongs to this sensor (a reload may have pruned it meanwhile; then the
    // re-added sensor correctly starts again from seen=false).
    _Static_assert(sizeof(fw_ver) == sizeof(((sensor_state_t *)0)->last_fw_version),
                   "fw_ver and last_fw_version must match for the memcpy below");
    taskENTER_CRITICAL(&s_wl_lock);
    sensor_state_t *live = &s_sensors[slot];
    if (live->in_use && memcmp(live->mac, adv_mac, 6) == 0) {
        live->last_leak = leak;
        live->last_battery = battery;
        memcpy(live->last_fw_version, fw_ver, sizeof(live->last_fw_version));
        live->seen = true;
        live->last_event_tick = now_tick;
    }
    taskEXIT_CRITICAL(&s_wl_lock);
    /* The health check-in already happened above, ahead of the delta gate. */
}

/* ---------------------------------------------------------
 * GAP event callback for extended scanning.
 * Handles both legacy (1M PHY) and extended (Coded PHY)
 * advertising reports from a single ble_gap_ext_disc() session.
 * --------------------------------------------------------- */
static int ble_leak_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

    case BLE_GAP_EVENT_DISC:
        // Legacy advertisement received (fallback path)
        process_leak_adv(&event->disc.addr, event->disc.rssi,
                         event->disc.data, event->disc.length_data);
        break;

#if MYNEWT_VAL(BLE_EXT_ADV)
    case BLE_GAP_EVENT_EXT_DISC: {
        const struct ble_gap_ext_disc_desc *ext = &event->ext_disc;

        // Only process complete advertising data
        if (ext->data_status != BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE) {
            break;
        }

        process_leak_adv(&ext->addr, ext->rssi, ext->data, ext->length_data);
        break;
    }
#endif

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGW(BLE_LEAK_TAG, "Scan complete (reason=%d) — will restart", event->disc_complete.reason);
        s_scan_restart_needed = true;
        break;

    default:
        break;
    }

    return 0;
}

/* ---------------------------------------------------------
 * Start passive BLE scan using extended scanning API.
 * Scans on both 1M PHY (legacy WB leak sensors) and
 * Coded PHY (long-range WBA leak sensors) simultaneously.
 * --------------------------------------------------------- */
static void start_passive_scan(void)
{
#if MYNEWT_VAL(BLE_EXT_ADV)
    // 1M PHY params — catches legacy WB leak sensors
    struct ble_gap_ext_disc_params uncoded_params = {0};
    uncoded_params.itvl = 160;      // 100ms interval
    uncoded_params.window = 80;     // 50ms window (50% duty)
    uncoded_params.passive = 1;

    // Coded PHY params — catches long-range WBA leak sensors
    struct ble_gap_ext_disc_params coded_params = {0};
    coded_params.itvl = 160;        // 100ms interval
    coded_params.window = 80;       // 50ms window (50% duty)
    coded_params.passive = 1;

    int rc = ble_gap_ext_disc(
        BLE_OWN_ADDR_PUBLIC,
        0,                          // duration: 0 = continuous
        0,                          // period: 0 = no periodic restart
        0,                          // filter_duplicates: disabled for fast change detection
        0,                          // filter_policy: accept all
        0,                          // limited: disabled
        &uncoded_params,            // 1M PHY scan params
        &coded_params,              // Coded PHY scan params
        ble_leak_gap_event,
        NULL
    );

    if (rc == 0) {
        ESP_LOGI(BLE_LEAK_TAG, "Extended passive scan started (1M + Coded PHY)");
    } else if (rc == BLE_HS_EALREADY) {
        ESP_LOGD(BLE_LEAK_TAG, "Scan already active (valve scanning?), will retry");
        s_scan_restart_needed = true;
    } else {
        ESP_LOGW(BLE_LEAK_TAG, "Failed to start ext scan: %d, will retry", rc);
        s_scan_restart_needed = true;
    }
#else
    // Fallback: legacy scanning (1M PHY only)
    struct ble_gap_disc_params disc_params = {0};
    disc_params.passive = 1;
    disc_params.filter_duplicates = 0;
    disc_params.itvl = 160;
    disc_params.window = 80;

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER,
                          &disc_params, ble_leak_gap_event, NULL);
    if (rc == 0) {
        ESP_LOGI(BLE_LEAK_TAG, "Passive scan started (1M only, ext_adv disabled)");
    } else if (rc == BLE_HS_EALREADY) {
        ESP_LOGD(BLE_LEAK_TAG, "Scan already active (valve scanning?), will retry");
        s_scan_restart_needed = true;
    } else {
        ESP_LOGW(BLE_LEAK_TAG, "Failed to start scan: %d, will retry", rc);
        s_scan_restart_needed = true;
    }
#endif
}

/* ---------------------------------------------------------
 * Main scanner task
 * --------------------------------------------------------- */
static void ble_leak_scan_task(void *param)
{
    (void)param;
    ESP_LOGI(BLE_LEAK_TAG, "Task started, waiting for NimBLE...");

    // Block until NimBLE is initialized
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ESP_LOGI(BLE_LEAK_TAG, "NimBLE ready, initializing scanner");

    // Load whitelist
    reload_whitelist();
    // Under the lock: the valve module's GAP handler can already be feeding
    // app_ble_leak_process_adv() now that the whitelist is populated.
    taskENTER_CRITICAL(&s_wl_lock);
    memset(s_sensors, 0, sizeof(s_sensors));
    taskEXIT_CRITICAL(&s_wl_lock);

    // Initial scan start (with small delay to let valve module connect first)
    vTaskDelay(pdMS_TO_TICKS(2000));
    start_passive_scan();

    TickType_t last_whitelist_reload = xTaskGetTickCount();
    TickType_t last_heartbeat_log = xTaskGetTickCount();

    for (;;) {
        // Handle scan restart if needed
        if (s_scan_restart_needed) {
            s_scan_restart_needed = false;
            vTaskDelay(pdMS_TO_TICKS(SCAN_RESTART_DELAY_MS));
            start_passive_scan();
        }
        // Self-healing: detect when our scan was cancelled externally
        // (e.g., valve module's scan/connect sequence) without a
        // BLE_GAP_EVENT_DISC_COMPLETE reaching our callback.
        else if (!ble_gap_disc_active()) {
            ESP_LOGW(BLE_LEAK_TAG, "Scan not active (external cancel?), restarting");
            start_passive_scan();
        }

        // Periodically reload whitelist (handles runtime commissioning)
        if ((xTaskGetTickCount() - last_whitelist_reload) >= pdMS_TO_TICKS(WHITELIST_RELOAD_MS)) {
            reload_whitelist();
            last_whitelist_reload = xTaskGetTickCount();
        }

        // Periodic scan-alive heartbeat (every 60s)
        if ((xTaskGetTickCount() - last_heartbeat_log) >= pdMS_TO_TICKS(60000)) {
            ESP_LOGI(BLE_LEAK_TAG, "[HEARTBEAT] Scanner alive, whitelist=%d sensors", s_whitelist_count);
            last_heartbeat_log = xTaskGetTickCount();
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ---------------------------------------------------------
 * Public API
 * --------------------------------------------------------- */
void app_ble_leak_init(void)
{
    ESP_LOGI(BLE_LEAK_TAG, "Initializing BLE leak scanner module");

    ble_leak_rx_queue = xQueueCreate(10, sizeof(ble_leak_event_t));
    if (ble_leak_rx_queue == NULL) {
        ESP_LOGE(BLE_LEAK_TAG, "Failed to create event queue");
        return;
    }

    xTaskCreate(ble_leak_scan_task, "ble_leak_scan", 3072, NULL, 4, &ble_leak_task_handle);
}

void app_ble_leak_signal_start(void)
{
    if (ble_leak_task_handle != NULL) {
        xTaskNotifyGive(ble_leak_task_handle);
    }
}

void app_ble_leak_reset_tracking(void)
{
    // Forget what was reported (seen + ticks) but keep each slot's MAC binding, so the
    // next advertisement from every sensor produces a fresh event. Under the lock: this
    // runs on iothub_task while the NimBLE host task may be mid-advertisement.
    taskENTER_CRITICAL(&s_wl_lock);
    for (int i = 0; i < MAX_TRACKED_SENSORS; i++) {
        s_sensors[i].seen             = false;
        s_sensors[i].last_event_tick  = 0;
        s_sensors[i].last_health_tick = 0;
    }
    taskEXIT_CRITICAL(&s_wl_lock);
    ESP_LOGI(BLE_LEAK_TAG, "Sensor tracking reset");
}

void app_ble_leak_process_adv(const void *addr, int8_t rssi,
                              const uint8_t *data, uint8_t data_len)
{
    if (ble_leak_rx_queue == NULL || s_whitelist_count == 0) {
        return;
    }
    process_leak_adv((const ble_addr_t *)addr, rssi, data, data_len);
}
