/****************************************************
 *  MODULE:   BLE Leak Sensor Scanner and the BLE scan executor
 *  PURPOSE:  Passive BLE scanner for "eleak" leak sensors.
 *            Uses extended scanning (ble_gap_ext_disc) to receive
 *            both legacy 1M and Coded PHY advertisements, enabling
 *            support for STM32WB (legacy) and STM32WBA (long range)
 *            leak sensors simultaneously.
 *            Since 2.1.4 (WP5) its task is the only code that starts
 *            or stops a BLE scan; the valve module's hunt for its
 *            valve runs on these scans (see "The BLE scan executor").
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
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "provisioning_manager/provisioning_manager.h"
#include "health_engine/health_engine.h"
#include "app_wifi/portal_priority.h"
#include "ble_valve/app_ble_valve.h"

/* ---------------------------------------------------------
 * Constants
 * --------------------------------------------------------- */
#define BLE_LEAK_TAG            "BLE_LEAK"
#define ELEAK_COMPANY_ID        0x0030      // ST Microelectronics
#define ELEAK_MFG_DATA_LEN      4           // 2B company ID + 1B leak + 1B battery
#define ELEAK_DEVICE_NAME       "eleak"
#define ELEAK_DEVICE_NAME_LEN   5
#define MAX_TRACKED_SENSORS     MAX_BLE_LEAK_SENSORS
#define SCAN_RESTART_DELAY_MS   500         // a scan that ended by itself is started again this
                                            // long after (none ends by itself in WP5's geometries)
#define EXEC_POLL_MS            500         // the executor's longest wait: the portal window and
                                            // the Wi-Fi radio holds are polled at least this often,
                                            // as the old 500 ms loop did
#define SCAN_RETRY_MS           50          // a scan start that failed is retried this soon
#define SCAN_FAIL_LOG_EVERY     200         // ... and logged again every this many failures (10 s)
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
#define BURST_GAP_MS            1500             // An advert burst ends after this long with none
                                                 // (the burst log, burst_log())...
#define BURST_MAX_MS            30000            // ... or once it spans this long (a sensor that
                                                 // never pauses); below the 65.5 s burst clock wrap.
#define BURST_PHY_1M            0x01             // burst_phy bits: the primary PHYs heard
#define BURST_PHY_CODED         0x02
#define BURST_PHY_OTHER         0x04
#define BURST_LOG_SLOTS         4                // tracking slots with a burst log: the first
                                                 // sensors heard (a bench has a few), 8 B each

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
    // The advert burst being counted, for the burst log only (burst_log()). The count and the
    // PHYs sit in what was padding before the ticks; the times are in s_burst_times[], for the
    // first BURST_LOG_SLOTS slots only.
    uint8_t burst_n;        // adverts heard in it so far (stops at 255); 0 = no burst open
    uint8_t burst_phy;      // BURST_PHY_* bits of their primary PHYs
    TickType_t last_event_tick;    // last telemetry event (drives BLE_LEAK_HEARTBEAT_MS)
    TickType_t last_health_tick;   // last health check-in (drives HEALTH_CHECKIN_MIN_MS)
} sensor_state_t;

// The times of a slot's open burst (burst log only). A slot's burst_n of 0 marks them stale: a
// claimed or pruned slot starts from 0, and the next advert sets them all.
typedef struct {
    uint16_t first_ms;      // burst clock (burst_now_ms()) at its first advert
    uint16_t last_ms;       // ... and at its last
    uint16_t dt_min;        // shortest and longest gap between two of its adverts, ms
    uint16_t dt_max;
} burst_times_t;
_Static_assert(BURST_LOG_SLOTS <= MAX_TRACKED_SENSORS, "burst log slots are tracking slots");

/* ---------------------------------------------------------
 * Static variables
 * --------------------------------------------------------- */
QueueHandle_t ble_leak_rx_queue = NULL;

static TaskHandle_t ble_leak_task_handle = NULL;

// The executor's cross-task flags (the rest of its state lives on its own stack, exec_t).
static volatile bool s_nimble_ready = false;   // app_ble_leak_signal_start() ran
static volatile bool s_scan_ended = false;     // DISC_COMPLETE: the GAP handler stores it...
static volatile int s_scan_end_reason = 0;     // ... with its reason; the executor takes both

// Cached whitelist from provisioning manager
static uint8_t s_whitelist[MAX_TRACKED_SENSORS][6];
static uint8_t s_whitelist_count = 0;

// Per-sensor tracking for dedup
static sensor_state_t s_sensors[MAX_TRACKED_SENSORS];

// The burst log's times for s_sensors[0 .. BURST_LOG_SLOTS - 1]
static burst_times_t s_burst_times[BURST_LOG_SLOTS];

/* Guards s_whitelist[], s_whitelist_count, s_sensors[] and s_burst_times[].
 *
 * The whitelist is rewritten by the scan task (every 10 s) while the NimBLE host task
 * reads it for every advertisement, and 2.1.3 published the new count BEFORE the new
 * entries — an advertisement in between matched a half-written list. A spinlock rather
 * than a mutex because the reader is the NimBLE host task, which must never block on the
 * scan task. Sections hold for microseconds: NO logging, NO queue sends, and no calls
 * other than memcmp/memcpy/memset, the *_locked helpers below and the burst clock
 * (burst_now_ms()). */
static portMUX_TYPE s_wl_lock = portMUX_INITIALIZER_UNLOCKED;

/* ---------------------------------------------------------
 * Helper: parse MAC string "XX:XX:XX:XX:XX:XX" to 6-byte array
 * NimBLE stores addresses LSB-first, so we reverse the byte order.
 * "00:80:E1:27:9A:E6" → [0xE6, 0x9A, 0x27, 0xE1, 0x80, 0x00]
 * Returns false (out untouched) unless all six fields parsed.
 *
 * Hand-parsed for the one format provisioning stores (validate_mac_string(), upper-cased
 * by provisioning_get_device_set()): sscanf costs several hundred bytes of stack on the
 * 3072 B scan task. sscanf stays as the fallback for anything else, so nothing the old
 * parser accepted is newly rejected — a rejected MAC is a sensor the hub stops hearing.
 * --------------------------------------------------------- */
static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool mac_str_to_bytes(const char *str, uint8_t *out)
{
    uint8_t b[6] = {0};
    bool ok = (strlen(str) == 17);
    for (int i = 0; ok && i < 6; i++) {
        int hi = hex_nibble(str[3 * i]);
        int lo = hex_nibble(str[3 * i + 1]);
        if (hi < 0 || lo < 0 || (i < 5 && str[3 * i + 2] != ':')) {
            ok = false;
        } else {
            b[i] = (uint8_t)((hi << 4) | lo);
        }
    }
    if (!ok) {
        unsigned int u[6];
        if (sscanf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
                   &u[0], &u[1], &u[2], &u[3], &u[4], &u[5]) != 6) {
            return false;
        }
        for (int i = 0; i < 6; i++) b[i] = (uint8_t)u[i];
    }
    for (int i = 0; i < 6; i++) {
        out[i] = b[5 - i];
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
 * Burst log (2.1.4 G0 bench baseline)
 * A leak sensor (FW 1.1.0) sends its adverts in bursts: a 2.5 s one every 15 s while wet and
 * about every 100 s dry, a 4 s one at a leak edge. For each sensor in the first BURST_LOG_SLOTS
 * tracking slots (slots go to sensors in the order they are first heard) the hub counts the
 * adverts it hears in a burst, the shortest and longest gap between two of them, and the
 * primary PHYs they came on, and prints one line once the burst is over (BURST_GAP_MS with
 * none): the sensor's real advert interval (the shortest gap) and how many adverts a radio mode
 * loses. The NimBLE host task counts, in process_leak_adv()'s lookup section
 * (burst_note_locked()); the scan task closes a burst and prints it outside the lock
 * (burst_log()). Log only: nothing here changes what the scanner reports or its delta filter.
 * --------------------------------------------------------- */

// The burst clock: milliseconds since boot, low 16 bits (bursts and their gaps last seconds).
// Safe in a critical section.
static uint16_t burst_now_ms(void)
{
    return (uint16_t)(esp_timer_get_time() / 1000);
}

// Counts one advert into the sensor's open burst, or opens one; t is its slot's times. Call with
// s_wl_lock held.
static void burst_note_locked(sensor_state_t *s, burst_times_t *t, uint16_t now_ms, uint8_t phy_bit)
{
    if (s->burst_n == 0) {
        t->first_ms = now_ms;
        t->dt_min = UINT16_MAX;
        t->dt_max = 0;
        s->burst_phy = 0;
    } else {
        uint16_t dt = (uint16_t)(now_ms - t->last_ms);
        if (dt < t->dt_min) t->dt_min = dt;
        if (dt > t->dt_max) t->dt_max = dt;
    }
    t->last_ms = now_ms;
    s->burst_phy |= phy_bit;
    if (s->burst_n < UINT8_MAX) s->burst_n++;
}

/* ---------------------------------------------------------
 * Reload whitelist from provisioning manager
 * --------------------------------------------------------- */
/* Read the provisioned BLE MACs and convert them, in a frame of its OWN (noinline), so the
 * ~376 B device set is dead before reload_whitelist() logs anything. Logging is the deep
 * call on the 3072 B scan task, and 2.1.3 logged with its 288 B string copy still live.
 * Not static either: a static would take the same bytes out of the heap for good, and the
 * field's lowest free heap was 2972 B. No logging and no locks in here.
 * Returns false only when provisioning could not be read. */
static __attribute__((noinline)) bool read_whitelist(uint8_t wl[][6], uint8_t *count,
                                                     uint8_t *invalid)
{
    /* The atomic device-set read, not provisioning_get_ble_leak_sensors(): that getter
     * returns false for BOTH "no sensors" and "mutex busy", and the old code treated
     * false as "no sensors" — one busy mutex blanked the whitelist and deafened the hub
     * to every leak sensor for up to 10 s. */
    prov_device_set_t set;
    if (!provisioning_get_device_set(&set)) return false;

    *count = 0;
    *invalid = 0;
    for (int i = 0; i < set.ble_count && *count < MAX_TRACKED_SENSORS; i++) {
        if (mac_str_to_bytes(set.ble_macs[i], wl[*count])) {
            (*count)++;
        } else {
            (*invalid)++;
        }
    }
    return true;
}

static void reload_whitelist(void)
{
    // Converted OUTSIDE the lock (the parser is not a critical-section call).
    uint8_t wl[MAX_TRACKED_SENSORS][6];
    memset(wl, 0, sizeof(wl));
    uint8_t count = 0, invalid = 0;
    if (!read_whitelist(wl, &count, &invalid)) {
        // A failed read keeps the current list.
        ESP_LOGW(BLE_LEAK_TAG, "Whitelist reload skipped (provisioning busy) - keeping %d sensor(s)",
                 (int)s_whitelist_count);
        return;
    }
    if (invalid > 0) {
        ESP_LOGW(BLE_LEAK_TAG, "Whitelist: %u invalid MAC(s) skipped", (unsigned)invalid);
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
 * Scan task, every pass: prints and closes each sensor's burst that is over, BURST_GAP_MS
 * after its last advert, or once it spans BURST_MAX_MS (see the burst log above). The clock is
 * read inside the section, so an advert the host task counts just before cannot look old.
 * --------------------------------------------------------- */
static const char *const k_burst_phy[8] = {
    "?", "1M", "Coded", "1M+Coded", "other", "1M+other", "Coded+other", "1M+Coded+other"
};

static void burst_log(void)
{
    for (int i = 0; i < BURST_LOG_SLOTS; i++) {
        uint8_t mac[6] = {0};
        uint8_t n = 0, phy = 0;
        uint16_t span = 0, dt_min = 0, dt_max = 0;
        taskENTER_CRITICAL(&s_wl_lock);
        sensor_state_t *s = &s_sensors[i];
        const burst_times_t *t = &s_burst_times[i];
        if (s->in_use && s->burst_n > 0) {
            span = (uint16_t)(t->last_ms - t->first_ms);
            if ((uint16_t)(burst_now_ms() - t->last_ms) > BURST_GAP_MS || span >= BURST_MAX_MS) {
                memcpy(mac, s->mac, 6);
                n = s->burst_n;
                phy = s->burst_phy;
                dt_min = t->dt_min;
                dt_max = t->dt_max;
                s->burst_n = 0;
            }
        }
        taskEXIT_CRITICAL(&s_wl_lock);
        if (n == 0) {
            continue;
        }
        // The MAC as mac_bytes_to_str() prints it: NimBLE stores it LSB first.
        if (n == 1) {
            ESP_LOGI(BLE_LEAK_TAG, "eleak %02X:%02X:%02X:%02X:%02X:%02X burst: n=1, phy=%s",
                     mac[5], mac[4], mac[3], mac[2], mac[1], mac[0], k_burst_phy[phy & 7]);
        } else {
            ESP_LOGI(BLE_LEAK_TAG, "eleak %02X:%02X:%02X:%02X:%02X:%02X burst: n=%u in %u.%02u s, dT %u-%u ms, phy=%s",
                     mac[5], mac[4], mac[3], mac[2], mac[1], mac[0], (unsigned)n,
                     (unsigned)(span / 1000), (unsigned)((span % 1000) / 10),
                     (unsigned)dt_min, (unsigned)dt_max, k_burst_phy[phy & 7]);
        }
    }
}

/* ---------------------------------------------------------
 * Common advertisement processing for leak sensors.
 * Called from both legacy (BLE_GAP_EVENT_DISC) and extended
 * (BLE_GAP_EVENT_EXT_DISC) event handlers. prim_phy is the report's
 * primary PHY (BLE_HCI_LE_PHY_*), for the burst log only.
 * --------------------------------------------------------- */
static void process_leak_adv(const ble_addr_t *addr, int8_t rssi,
                             const uint8_t *data, uint8_t data_len, uint8_t prim_phy)
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
    // The burst log's count rides in the same section (burst_note_locked()), for the first
    // BURST_LOG_SLOTS slots.
    uint16_t burst_ms = burst_now_ms();
    uint8_t phy_bit = (prim_phy == BLE_HCI_LE_PHY_1M)    ? BURST_PHY_1M
                    : (prim_phy == BLE_HCI_LE_PHY_CODED) ? BURST_PHY_CODED
                                                         : BURST_PHY_OTHER;
    taskENTER_CRITICAL(&s_wl_lock);
    listed = (whitelist_find_locked(adv_mac) >= 0);
    if (listed) {
        slot = sensor_find_locked(adv_mac);
        if (slot < 0) slot = sensor_alloc_locked(adv_mac);
        if (slot >= 0) {
            if (slot < BURST_LOG_SLOTS) {
                burst_note_locked(&s_sensors[slot], &s_burst_times[slot], burst_ms, phy_bit);
            }
            memcpy(&snap, &s_sensors[slot], sizeof(snap));
        }
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
 * The BLE scan executor (2.1.4 WP5; plan §4.7, invariants I3 and I5)
 *
 * This task is the ONLY code that starts or stops a BLE scan (ble_gap_ext_disc,
 * ble_gap_disc_cancel). Until WP5 the valve module ran a scan of its own to hunt for its valve,
 * and the two owners raced: each cancelled the other's scan, and a start could find the other's
 * already running (BLE_HS_EALREADY, "[SCAN] ble_gap_disc rc=2"), which left the hunt dead. Now
 * the valve module only says what it wants, and this task decides:
 *   - a claim (ble_valve_claim_wanted(): its valve was heard) is granted at once: the scan is
 *     stopped and ble_valve_claim_start() issues the connect on this task, so no scan can start
 *     between the grant and the connect;
 *   - no scan starts while a connect is in flight (it would fail with BLE_HS_EBUSY);
 *   - the valve hunt's geometry while a hunt is wanted (ble_valve_hunt_wanted()), the leak scan
 *     otherwise, and none while the portal priority window or a Wi-Fi radio hold has the radio,
 *     exactly as before WP5 (a hunt still runs in them while a leak response is pended);
 *   - every advert of every scan goes to the leak sensors (process_leak_adv()) and to the
 *     valve's MAC hook (ble_valve_note_adv()), so the hunt is passive and costs the sensors
 *     nothing: its scan covers them as the leak scan does.
 * The GAP handler only stores and notifies (DISC_COMPLETE). The task is woken by it, by the valve
 * module (app_ble_leak_kick()) and at least every EXEC_POLL_MS. A start that fails is retried
 * SCAN_RETRY_MS later. Priority 6 (was 4), so its starts land within milliseconds of an edge.
 * --------------------------------------------------------- */

// A scan geometry. Interval and window in 0.625 ms units (both 0: that PHY is not scanned);
// duration in 10 ms units (0: until cancelled). Every scan is passive with filter_duplicates
// off (scan_start()).
typedef struct {
    uint16_t itvl_1m, win_1m;
    uint16_t itvl_c, win_c;
    uint16_t dur;
} scan_geo_t;

enum { SCAN_GEO_NONE = 0, SCAN_GEO_LEAK, SCAN_GEO_HUNT, SCAN_GEO_COUNT };

static const scan_geo_t k_scan_geo[SCAN_GEO_COUNT] = {
    [SCAN_GEO_NONE] = { 0, 0, 0, 0, 0 },
    // The leak scan: 1M (WB sensors, WBA sensors in 1M mode, the valve) and Coded (WBA long
    // range), each 100 ms interval / 50 ms window.
    [SCAN_GEO_LEAK] = { 160, 80, 160, 80, 0 },
    // The valve hunt (the valve module's own scan until WP5): 110 ms / 55 ms on each PHY.
    // Deliberately NOT 100 ms: the valve advertises every 500-700 ms, and 100 ms against
    // 500 ms is a 5:1 harmonic lock in which escape depends solely on the 0-10 ms per-event
    // advDelay drifting the phase; 110 ms breaks the lock at the same duty (50 %). Passive now
    // (it was active on 1M): the valve is matched by its MAC alone, so no scan request is
    // needed, and the leak sensors are covered as by the leak scan.
    [SCAN_GEO_HUNT] = { 176, 88, 176, 88, 0 },
};

// The executor's own state, on its stack: only its task reads or writes it.
typedef struct {
    bool scan_on;               // our scan runs (started, no DISC_COMPLETE or cancel since)
    uint8_t geo;                // ... with this geometry
    bool paused;                // the window or a radio hold has the radio (leak scan held)
    bool portal_paused;         // ... the window itself, for its log lines
    uint16_t start_fails;       // scan starts failed in a row
    TickType_t retry_at;        // no scan start before this tick
    TickType_t paused_since;    // when the pause began, for the heartbeat line
} exec_t;

/* ---------------------------------------------------------
 * GAP event callback of every scan the executor starts.
 * Handles both legacy (1M PHY) and extended (Coded PHY) advertising reports from a single
 * ble_gap_ext_disc() session, and the session's end. Stores and notifies only: the adverts go to
 * the leak sensors' state and the valve's MAC hook, and the end is left to the executor.
 * --------------------------------------------------------- */
static int ble_leak_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {

    case BLE_GAP_EVENT_DISC:
        // Legacy advertisement received (fallback path): always on the 1M PHY
        process_leak_adv(&event->disc.addr, event->disc.rssi,
                         event->disc.data, event->disc.length_data, BLE_HCI_LE_PHY_1M);
        ble_valve_note_adv(&event->disc.addr);
        break;

#if MYNEWT_VAL(BLE_EXT_ADV)
    case BLE_GAP_EVENT_EXT_DISC: {
        const struct ble_gap_ext_disc_desc *ext = &event->ext_disc;

        // Only process complete advertising data
        if (ext->data_status != BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE) {
            break;
        }

        process_leak_adv(&ext->addr, ext->rssi, ext->data, ext->length_data, ext->prim_phy);
        ble_valve_note_adv(&ext->addr);
        break;
    }
#endif

    case BLE_GAP_EVENT_DISC_COMPLETE: {
        s_scan_end_reason = event->disc_complete.reason;
        s_scan_ended = true;
        TaskHandle_t t = ble_leak_task_handle;
        if (t != NULL) {
            xTaskNotifyGive(t);
        }
        break;
    }

    default:
        break;
    }

    return 0;
}

/* ---------------------------------------------------------
 * Start a scan of geometry `geo` (executor task only): 0, or the NimBLE error.
 *
 * filter_duplicates is DISABLED, and that is a safety fix, not a latency one. The controller's
 * duplicate cache keys on ADDRESS ONLY and never refreshes (CONFIG_BT_CTRL_SCAN_DUPL_TYPE=0,
 * CONFIG_BT_CTRL_DUPL_SCAN_CACHE_REFRESH_PERIOD=0), so with the filter on, a leak sensor whose
 * payload changes from dry to WET is suppressed: the address has already been seen. Until 2.1.x
 * the valve hunt ran with the filter on, and the hub was deaf to its leak sensors for as long as
 * it hunted a dead valve. The cost of turning it off: duplicate valve reports, which the valve
 * module's claim guard absorbs (ble_valve_note_adv()).
 * --------------------------------------------------------- */
static int scan_start(uint8_t geo)
{
    const scan_geo_t *g = &k_scan_geo[geo];
#if MYNEWT_VAL(BLE_EXT_ADV)
    struct ble_gap_ext_disc_params p1m = {0};
    struct ble_gap_ext_disc_params pcoded = {0};
    p1m.itvl = g->itvl_1m;
    p1m.window = g->win_1m;
    p1m.passive = 1;
    pcoded.itvl = g->itvl_c;
    pcoded.window = g->win_c;
    pcoded.passive = 1;
    return ble_gap_ext_disc(
        BLE_OWN_ADDR_PUBLIC,
        g->dur,                         // duration, 10 ms units: 0 = until cancelled
        0,                              // period: 0 = no periodic restart
        0,                              // filter_duplicates: DISABLED, see above
        0,                              // filter_policy: accept all
        0,                              // limited: disabled
        g->win_1m ? &p1m : NULL,        // 1M PHY scan params
        g->win_c ? &pcoded : NULL,      // Coded PHY scan params
        ble_leak_gap_event,
        NULL
    );
#else
    // Fallback: legacy scanning (1M PHY only)
    struct ble_gap_disc_params disc_params = {0};
    disc_params.passive = 1;
    disc_params.filter_duplicates = 0;
    disc_params.itvl = g->itvl_1m ? g->itvl_1m : g->itvl_c;
    disc_params.window = g->win_1m ? g->win_1m : g->win_c;
    return ble_gap_disc(BLE_OWN_ADDR_PUBLIC, g->dur ? (int32_t)g->dur * 10 : BLE_HS_FOREVER,
                        &disc_params, ble_leak_gap_event, NULL);
#endif
}

// Stops our scan (executor task only). A cancel the controller refuses leaves it marked running,
// and the next pass tries again.
static void scan_stop(exec_t *x)
{
    if (!x->scan_on) {
        return;
    }
    int rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(BLE_LEAK_TAG, "Scan cancel failed: %d, will retry", rc);
        return;
    }
    x->scan_on = false;
}

// The line for a scan start: the leak scan's is the line it always printed.
static void scan_started_log(uint8_t geo)
{
    if (geo == SCAN_GEO_HUNT) {
        ESP_LOGI(BLE_LEAK_TAG, "Valve hunt scan started (1M + Coded PHY, passive)");
    } else {
        ESP_LOGI(BLE_LEAK_TAG, "Extended passive scan started (1M + Coded PHY)");
    }
}

/* ---------------------------------------------------------
 * One pass of the executor: every fact is read again (I6), so a missed edge costs at most one
 * pass. Returns how long it may sleep before the next one.
 * --------------------------------------------------------- */
static TickType_t executor_pass(exec_t *x)
{
    TickType_t now = xTaskGetTickCount();

    // A scan that ended by itself (DISC_COMPLETE). WP5's geometries run until cancelled, so this
    // is not expected: logged, and started again SCAN_RESTART_DELAY_MS later as before.
    if (s_scan_ended) {
        s_scan_ended = false;
        if (x->scan_on) {
            x->scan_on = false;
            ESP_LOGW(BLE_LEAK_TAG, "Scan complete (reason=%d) — will restart", s_scan_end_reason);
            x->retry_at = now + pdMS_TO_TICKS(SCAN_RESTART_DELAY_MS);
        }
    }
    // Self-healing: our scan is gone with no DISC_COMPLETE (a NimBLE host reset ends it so).
    if (x->scan_on && !ble_gap_disc_active()) {
        x->scan_on = false;
        ESP_LOGW(BLE_LEAK_TAG, "Scan not active (external cancel?), restarting");
    }

    // Portal priority window or a Wi-Fi radio hold (app_wifi.c): no leak scan while either is on,
    // as before WP5. A hold (a few seconds around a Wi-Fi scan or connect attempt, up to 20 s
    // while a setup page is in use) logs nothing here: app_wifi.c prints its start and end.
    bool portal = app_wifi_portal_priority_active();
    bool hold = portal || app_wifi_radio_hold_active();
    if (hold) {
        if (portal && !x->portal_paused) {
            x->portal_paused = true;
            ESP_LOGI(BLE_LEAK_TAG, "Scan paused - Wi-Fi setup portal has the radio");
        }
        if (!x->paused) {
            x->paused_since = now;
        }
        x->paused = true;
    } else if (x->paused) {
        x->paused = false;
        if (x->portal_paused) {
            x->portal_paused = false;
            ESP_LOGI(BLE_LEAK_TAG, "Scan resumed - Wi-Fi setup portal closed");
        }
    }

    // The valve module's claim: granted at once, as before WP5, when the connect was issued
    // from the report itself. Its connect then holds every scan off until its CONNECT event,
    // which wakes this task.
    if (ble_valve_claim_wanted()) {
        scan_stop(x);
        if (!x->scan_on) {
            ble_valve_claim_start();
        }
    }

    uint8_t want = SCAN_GEO_NONE;
    if (!ble_hs_synced() || ble_gap_conn_active()) {
        want = SCAN_GEO_NONE;           // not synced yet, or a connect in flight
    } else if (ble_valve_hunt_wanted()) {
        want = SCAN_GEO_HUNT;           // also in a hold, while a leak response is pended
    } else if (!hold) {
        want = SCAN_GEO_LEAK;
    }

    if (x->scan_on && x->geo != want) {
        scan_stop(x);
    }
    if (!x->scan_on && want != SCAN_GEO_NONE && (int32_t)(now - x->retry_at) >= 0) {
        int rc = scan_start(want);
        if (rc == 0) {
            x->scan_on = true;
            x->geo = want;
            if (x->start_fails > 0) {
                ESP_LOGI(BLE_LEAK_TAG, "Scan started after %u failed attempt(s)", (unsigned)x->start_fails);
                x->start_fails = 0;
            }
            scan_started_log(want);
        } else {
            // BLE_HS_EALREADY: a scan runs that this task does not count as its own (it is the
            // only owner, so its count is wrong): stop it and start ours.
            if (rc == BLE_HS_EALREADY) {
                (void)ble_gap_disc_cancel();
            }
            if (x->start_fails % SCAN_FAIL_LOG_EVERY == 0) {
                ESP_LOGW(BLE_LEAK_TAG, "Failed to start ext scan: %d, will retry", rc);
            }
            if (x->start_fails < UINT16_MAX) {
                x->start_fails++;
            }
            x->retry_at = now + pdMS_TO_TICKS(SCAN_RETRY_MS);
        }
    }

    TickType_t wait = pdMS_TO_TICKS(EXEC_POLL_MS);
    if (!x->scan_on && want != SCAN_GEO_NONE) {
        int32_t until = (int32_t)(x->retry_at - now);
        if (until < 1) {
            until = 1;
        }
        if ((TickType_t)until < wait) {
            wait = (TickType_t)until;
        }
    }
    return wait;
}

// The scan-alive heartbeat line. While the leak scan is held it says so, for how long and for what
// (2.1.4 WP1: until WP8 the no-credential portal's pause has no time cap); otherwise it is as it
// always was. In a frame of its own, so the task's loop frame does not carry its arguments.
static __attribute__((noinline)) void heartbeat_log(bool paused, bool portal, TickType_t paused_since)
{
    if (paused) {
        ESP_LOGI(BLE_LEAK_TAG, "[HEARTBEAT] Scanner alive, whitelist=%d sensors, scanning paused for %lu s (%s)",
                 s_whitelist_count,
                 (unsigned long)((xTaskGetTickCount() - paused_since) / configTICK_RATE_HZ),
                 portal ? "Wi-Fi setup portal" : "Wi-Fi radio hold");
    } else {
        ESP_LOGI(BLE_LEAK_TAG, "[HEARTBEAT] Scanner alive, whitelist=%d sensors", s_whitelist_count);
    }
}

/* ---------------------------------------------------------
 * Main scanner task: the BLE scan executor
 * --------------------------------------------------------- */
static void ble_leak_scan_task(void *param)
{
    (void)param;
    ESP_LOGI(BLE_LEAK_TAG, "Task started, waiting for NimBLE...");

    // Block until NimBLE is initialized. A wake from app_ble_leak_kick() before then is not it.
    while (!s_nimble_ready) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    ESP_LOGI(BLE_LEAK_TAG, "NimBLE ready, initializing scanner");

    // Load whitelist
    reload_whitelist();
    // Under the lock: the GAP handler can already be feeding process_leak_adv() now that the
    // whitelist is populated.
    taskENTER_CRITICAL(&s_wl_lock);
    memset(s_sensors, 0, sizeof(s_sensors));
    taskEXIT_CRITICAL(&s_wl_lock);

    // The first scan starts on the first pass, once NimBLE is synced: no delay any more. The 2 s
    // the old loop waited let the valve module's own scan start first, so that the two did not
    // race; the hunt is now this task's own scan.
    exec_t x = {0};
    TickType_t last_whitelist_reload = xTaskGetTickCount();
    TickType_t last_heartbeat_log = last_whitelist_reload;
    x.retry_at = last_whitelist_reload;

    for (;;) {
        TickType_t wait = executor_pass(&x);

        // Periodically reload whitelist (handles runtime commissioning)
        if ((xTaskGetTickCount() - last_whitelist_reload) >= pdMS_TO_TICKS(WHITELIST_RELOAD_MS)) {
            reload_whitelist();
            last_whitelist_reload = xTaskGetTickCount();
        }

        // The burst log: a line for each sensor's burst that is over (paused or not).
        burst_log();

        // Periodic scan-alive heartbeat (every 60s), with the pause if the leak scan is held
        if ((xTaskGetTickCount() - last_heartbeat_log) >= pdMS_TO_TICKS(60000)) {
            heartbeat_log(x.paused, x.portal_paused, x.paused_since);
            last_heartbeat_log = xTaskGetTickCount();
        }

        ulTaskNotifyTake(pdTRUE, wait);
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

    // Priority 6 (was 4, plan §4.7): the executor's scan starts and claim grants land within
    // milliseconds. Its passes are short; whitelist reloads and log lines are as before. Stack
    // unchanged: the deepest frames are still the whitelist read and the log calls, and a NimBLE
    // connect (the claim) costs about what the scan start beside it does.
    xTaskCreate(ble_leak_scan_task, "ble_leak_scan", 3072, NULL, 6, &ble_leak_task_handle);
}

void app_ble_leak_signal_start(void)
{
    s_nimble_ready = true;
    if (ble_leak_task_handle != NULL) {
        xTaskNotifyGive(ble_leak_task_handle);
    }
}

void app_ble_leak_kick(void)
{
    TaskHandle_t t = ble_leak_task_handle;
    if (t != NULL) {
        xTaskNotifyGive(t);
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
