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
#include "esp_random.h"
#include "nvs.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "provisioning_manager/provisioning_manager.h"
#include "nvs_store/nvs_store.h"
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
static volatile int s_scan_end_reason = 0;     // ... with its reason,
static volatile uint8_t s_scan_end_gen = 0;    // ... the generation of the scan it ends (its
                                               //     callback argument),
static volatile TickType_t s_scan_end_tick = 0;    // ... and when; the executor takes them all

// Cached whitelist from provisioning manager
static uint8_t s_whitelist[MAX_TRACKED_SENSORS][6];
static uint8_t s_whitelist_count = 0;

// Per-sensor tracking for dedup
static sensor_state_t s_sensors[MAX_TRACKED_SENSORS];

// The burst log's times for s_sensors[0 .. BURST_LOG_SLOTS - 1]
static burst_times_t s_burst_times[BURST_LOG_SLOTS];

// Adverts heard per tracking slot since the executor's last 60 s summary (WP6), saturating.
static uint8_t s_adv_n[MAX_TRACKED_SENSORS];

/* Each whitelisted sensor's primary PHY (2.1.4 WP6; plan §4.1, decision D1: some sensors in the
 * field advertise on 1M, the rest on Coded). Index = the sensor's whitelist index. Learned from
 * every advert's prim_phy (process_leak_adv()), so it changes only when the sensor is heard on the
 * other PHY: no time decay. PHY_UNKNOWN = never heard since it was provisioned; the first advert
 * sets it. A known PHY changes once PHY_FLIP_ADVERTS adverts in a row came on the other PHY (any
 * advert on its own PHY starts the count over): a sensor heard on both PHYs then keeps one, rather
 * than switching on every advert, which would flip N_MIXED and N_CODED at each scan's end, print
 * a mode line each time and rewrite the table every minute. The count lives in the entry's upper
 * bits (PHY_OF() reads the PHY). A sensor known to be on 1M makes the executor scan 1M in windows
 * of its own (N_MIXED). Persisted in NVS (phy_save(), at most 112 B, written only when it
 * changed), carried across whitelist reloads by MAC (phy_carry()), and dropped with a sensor that
 * leaves the whitelist (decommissioned). */
#define PHY_UNKNOWN     0
#define PHY_1M          1
#define PHY_CODED       2
#define PHY_MASK        0x03    // s_wl_phy[]: the PHY in bits 0-1 ...
#define PHY_OTHER_SHIFT 2       // ... and adverts in a row on the other PHY from bit 2
#define PHY_OF(v)       ((uint8_t)((v) & PHY_MASK))
#define PHY_FLIP_ADVERTS 4
_Static_assert(((PHY_FLIP_ADVERTS - 1) << PHY_OTHER_SHIFT) <= 0xFF, "the count fits the entry");
static uint8_t s_wl_phy[MAX_TRACKED_SENSORS];
static volatile bool s_phy_dirty = false;   // RAM differs from what was last saved
static bool s_phy_loaded = false;           // executor task: NVS read at the first reload
static uint16_t s_phy_flips = 0;            // known PHYs that changed since the last summary
static TickType_t s_phy_flip_logged = 0;    // host task: the last change line between known PHYs
#define PHY_FLIP_LOG_MS 10000
static const char *const k_phy_name[3] = { "unknown", "1M", "Coded" };

/* Guards s_whitelist[], s_whitelist_count, s_sensors[], s_burst_times[], s_adv_n[], s_wl_phy[]
 * and s_phy_flips.
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
            s_adv_n[i] = 0;
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

/* ---------------------------------------------------------
 * The PHY table in NVS (WP6): one blob of 7 B records (MAC as NimBLE stores it, PHY), known
 * PHYs only, at most MAX_TRACKED_SENSORS of them (112 B, plan §4.1), in the commissioning
 * partition so a Wi-Fi reset keeps it. Executor task only, each in a frame of its own.
 * --------------------------------------------------------- */
#define PHY_NVS_NS      "ble_phy"
#define PHY_NVS_KEY     "phy"
#define PHY_SAVE_MIN_MS 60000   // at most one write a minute, however often a sensor changes PHY
typedef struct __attribute__((packed)) {
    uint8_t mac[6];
    uint8_t phy;
} phy_rec_t;
_Static_assert(sizeof(phy_rec_t) * MAX_TRACKED_SENSORS <= 112, "plan 4.1: the PHY table takes at most 112 B of NVS");

static int wl_find(const uint8_t wl[][6], uint8_t count, const uint8_t *mac)
{
    for (int i = 0; i < count; i++) {
        if (memcmp(wl[i], mac, 6) == 0) {
            return i;
        }
    }
    return -1;
}

// The PHYs the new list keeps from the list in RAM, by MAC, each with its count of adverts on the
// other PHY (the reload runs every 10 s: a count that started over then could never reach
// PHY_FLIP_ADVERTS from a few adverts per burst). True when a known PHY's sensor left.
static __attribute__((noinline)) bool phy_carry(const uint8_t wl[][6], uint8_t count, uint8_t phy[])
{
    uint8_t old_wl[MAX_TRACKED_SENSORS][6];
    uint8_t old_phy[MAX_TRACKED_SENSORS];
    taskENTER_CRITICAL(&s_wl_lock);
    uint8_t old_n = s_whitelist_count;
    memcpy(old_wl, s_whitelist, sizeof(old_wl));
    memcpy(old_phy, s_wl_phy, sizeof(old_phy));
    taskEXIT_CRITICAL(&s_wl_lock);

    memset(phy, PHY_UNKNOWN, MAX_TRACKED_SENSORS);
    bool dropped = false;
    for (int j = 0; j < old_n; j++) {
        int i = wl_find(wl, count, old_wl[j]);
        if (i >= 0) {
            phy[i] = old_phy[j];
        } else if (PHY_OF(old_phy[j]) != PHY_UNKNOWN) {
            dropped = true;
        }
    }
    return dropped;
}

// The PHYs the list gets from NVS at the first reload since boot. True when the stored table holds
// a sensor no longer listed, or cannot be used as it is: it is then saved again.
static __attribute__((noinline)) bool phy_load(const uint8_t wl[][6], uint8_t count, uint8_t phy[])
{
    phy_rec_t rec[MAX_TRACKED_SENSORS];
    size_t len = sizeof(rec);
    memset(phy, PHY_UNKNOWN, MAX_TRACKED_SENSORS);

    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PROV_PARTITION, PHY_NVS_NS, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_blob(h, PHY_NVS_KEY, rec, &len);
        nvs_close(h);
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return false;   // nothing learned and saved yet
    }
    if (err != ESP_OK || len % sizeof(phy_rec_t) != 0) {
        ESP_LOGW(BLE_LEAK_TAG, "PHY table not read from NVS (%s, %u B) - every sensor's PHY is unknown until heard",
                 esp_err_to_name(err), (unsigned)len);
        return err == ESP_OK || err == ESP_ERR_NVS_INVALID_LENGTH;
    }

    unsigned n = len / sizeof(phy_rec_t), known = 0, on_1m = 0;
    bool dropped = false;
    for (unsigned k = 0; k < n; k++) {
        int i = wl_find(wl, count, rec[k].mac);
        if (i >= 0 && (rec[k].phy == PHY_1M || rec[k].phy == PHY_CODED)) {
            phy[i] = rec[k].phy;
            known++;
            if (rec[k].phy == PHY_1M) on_1m++;
        } else {
            dropped = true;
        }
    }
    ESP_LOGI(BLE_LEAK_TAG, "PHY table loaded: %u of %u sensor(s) known, %u on 1M", known, (unsigned)count, on_1m);
    return dropped;
}

// Saves the known PHYs if they differ from what NVS holds (read and compared first: written only
// on change). A failure leaves the table marked for the next try.
static __attribute__((noinline)) void phy_save(void)
{
    phy_rec_t rec[MAX_TRACKED_SENSORS];
    phy_rec_t old[MAX_TRACKED_SENSORS];
    unsigned n = 0, on_1m = 0;

    s_phy_dirty = false;   // a change from here on marks it again
    taskENTER_CRITICAL(&s_wl_lock);
    for (int i = 0; i < s_whitelist_count; i++) {
        if (PHY_OF(s_wl_phy[i]) != PHY_UNKNOWN) {
            memcpy(rec[n].mac, s_whitelist[i], 6);
            rec[n].phy = PHY_OF(s_wl_phy[i]);
            n++;
        }
    }
    taskEXIT_CRITICAL(&s_wl_lock);
    for (unsigned k = 0; k < n; k++) {
        if (rec[k].phy == PHY_1M) on_1m++;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PROV_PARTITION, PHY_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        s_phy_dirty = true;
        ESP_LOGW(BLE_LEAK_TAG, "PHY table not saved (%s) - retried later", esp_err_to_name(err));
        return;
    }
    size_t olen = sizeof(old);
    esp_err_t gerr = nvs_get_blob(h, PHY_NVS_KEY, old, &olen);
    bool same = (gerr == ESP_OK) ? (olen == n * sizeof(phy_rec_t) && memcmp(old, rec, olen) == 0)
                                 : (gerr == ESP_ERR_NVS_NOT_FOUND && n == 0);
    if (!same) {
        err = (n > 0) ? nvs_set_blob(h, PHY_NVS_KEY, rec, n * sizeof(phy_rec_t))
                      : nvs_erase_key(h, PHY_NVS_KEY);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
    }
    nvs_close(h);
    if (err != ESP_OK) {
        s_phy_dirty = true;
        ESP_LOGW(BLE_LEAK_TAG, "PHY table not saved (%s) - retried later", esp_err_to_name(err));
    } else if (!same) {
        ESP_LOGI(BLE_LEAK_TAG, "PHY table saved: %u sensor(s) known, %u on 1M", n, on_1m);
    }
}

// True when a listed sensor is known to advertise on 1M: the executor then runs N_MIXED.
static bool phy_any_1m(void)
{
    bool any = false;
    taskENTER_CRITICAL(&s_wl_lock);
    for (int i = 0; i < s_whitelist_count; i++) {
        if (PHY_OF(s_wl_phy[i]) == PHY_1M) {
            any = true;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_wl_lock);
    return any;
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

    // Each listed sensor keeps the PHY learned for it (WP6): from the list in RAM, or, at the
    // first reload since boot, from NVS. A known PHY dropped with its sensor is saved as gone.
    uint8_t phy[MAX_TRACKED_SENSORS];
    bool phy_dropped;
    if (!s_phy_loaded) {
        phy_dropped = phy_load(wl, count, phy);
        s_phy_loaded = true;
    } else {
        phy_dropped = phy_carry(wl, count, phy);
    }

    /* Swap the list and prune the tracking of every MAC no longer on it, in ONE section,
     * so an advertisement can never see the new count with the old entries, nor find a
     * slot left over from a removed sensor (a re-added sensor starts from seen=false).
     * A PHY learned between phy_carry()'s copy and this swap is lost here and learned again
     * from that sensor's next advert. */
    taskENTER_CRITICAL(&s_wl_lock);
    memcpy(s_whitelist, wl, sizeof(s_whitelist));
    s_whitelist_count = count;
    memcpy(s_wl_phy, phy, sizeof(s_wl_phy));
    for (int i = 0; i < MAX_TRACKED_SENSORS; i++) {
        if (s_sensors[i].in_use && whitelist_find_locked(s_sensors[i].mac) < 0) {
            memset(&s_sensors[i], 0, sizeof(s_sensors[i]));
            s_adv_n[i] = 0;
        }
    }
    taskEXIT_CRITICAL(&s_wl_lock);
    if (phy_dropped) {
        s_phy_dirty = true;
    }

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
    // The sensor's primary PHY (WP6): learned from its first advert, changed after
    // PHY_FLIP_ADVERTS in a row on the other PHY (see s_wl_phy). Any other PHY teaches nothing.
    uint8_t phy_heard = (prim_phy == BLE_HCI_LE_PHY_1M)    ? PHY_1M
                      : (prim_phy == BLE_HCI_LE_PHY_CODED) ? PHY_CODED
                                                           : PHY_UNKNOWN;
    uint8_t phy_was = PHY_UNKNOWN;
    bool phy_learned = false;
    taskENTER_CRITICAL(&s_wl_lock);
    int wi = whitelist_find_locked(adv_mac);
    listed = (wi >= 0);
    if (listed) {
        uint8_t v = s_wl_phy[wi];
        uint8_t known = PHY_OF(v);
        if (phy_heard != PHY_UNKNOWN && known != phy_heard) {
            uint8_t other = (uint8_t)((v >> PHY_OTHER_SHIFT) + 1);
            if (known == PHY_UNKNOWN || other >= PHY_FLIP_ADVERTS) {
                phy_was = known;
                s_wl_phy[wi] = phy_heard;
                phy_learned = true;
                if (phy_was != PHY_UNKNOWN && s_phy_flips < UINT16_MAX) s_phy_flips++;
            } else {
                s_wl_phy[wi] = (uint8_t)(known | (other << PHY_OTHER_SHIFT));
            }
        } else if (phy_heard == known && v != known) {
            s_wl_phy[wi] = known;   // heard on its own PHY again: the count starts over
        }
        slot = sensor_find_locked(adv_mac);
        if (slot < 0) slot = sensor_alloc_locked(adv_mac);
        if (slot >= 0) {
            if (s_adv_n[slot] < UINT8_MAX) s_adv_n[slot]++;
            if (slot < BURST_LOG_SLOTS) {
                burst_note_locked(&s_sensors[slot], &s_burst_times[slot], burst_ms, phy_bit);
            }
            memcpy(&snap, &s_sensors[slot], sizeof(snap));
        }
    }
    taskEXIT_CRITICAL(&s_wl_lock);
    if (phy_learned) {
        // The executor saves it (rate-limited) and picks its profile again (N_MIXED on 1M). A
        // sensor whose known PHY keeps changing is logged at most every PHY_FLIP_LOG_MS; the
        // summary counts every change.
        s_phy_dirty = true;
        TickType_t tnow = xTaskGetTickCount();
        if (phy_was == PHY_UNKNOWN || (tnow - s_phy_flip_logged) >= pdMS_TO_TICKS(PHY_FLIP_LOG_MS)) {
            if (phy_was != PHY_UNKNOWN) {
                s_phy_flip_logged = tnow;
            }
            ESP_LOGI(BLE_LEAK_TAG, "eleak %02X:%02X:%02X:%02X:%02X:%02X PHY learned: %s (was %s)",
                     adv_mac[5], adv_mac[4], adv_mac[3], adv_mac[2], adv_mac[1], adv_mac[0],
                     k_phy_name[phy_heard], k_phy_name[phy_was]);
        }
        app_ble_leak_kick();
    }
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
 * The BLE scan executor (2.1.4 WP5 and WP6; plan §4.1, §4.2 rows 3-4, §4.4, §4.7; I1-I3, I5)
 *
 * This task is the ONLY code that starts or stops a BLE scan (ble_gap_ext_disc,
 * ble_gap_disc_cancel). Until WP5 the valve module ran a scan of its own to hunt for its valve,
 * and the two owners raced: each cancelled the other's scan, and a start could find the other's
 * already running (BLE_HS_EALREADY, "[SCAN] ble_gap_disc rc=2"), which left the hunt dead. Now
 * the valve module only says what it wants (ble_valve_hunt_wanted(), ble_valve_claim_wanted(),
 * ble_valve_lr_active()), and this task decides. Every advert of every scan goes to the leak
 * sensors (process_leak_adv()) and to the valve's MAC hook (ble_valve_note_adv()): the valve hunt
 * is passive and runs on whatever scan covers 1M.
 *
 * Modes (first match wins; WP8's radio policy adds the AP modes and replaces the two holds):
 *   CLAIM      a claim's connect is in flight (the CONNECT pulse): no scan can run.
 *   PAUSED     the portal priority window or a Wi-Fi radio hold has the radio: no scan, exactly
 *              as before WP5 ...
 *   HOLD_HUNT  ... unless a leak response is pended: then the valve hunt's scan runs, as before
 *              (it covers the sensors too), and a claim is granted at a hunt scan's end.
 *   RECOVERY   1.2 s of Coded after a claim pulse (I2), before the profile resumes.
 *   NORMAL_LR  a leak response is pending and the valve is not linked, at most 10 min per
 *              incident (the valve module's overlay): [1M 1.0 s][Coded 0.6 s] in turn.
 *   N_MIXED    a listed sensor is known to advertise on 1M: [1M 1.0 s][Coded 1.0 s] in turn.
 *   N_CODED    otherwise: 1M 160/32 + Coded 160/128 (20 % / 80 %) in 1 s scans.
 * NORMAL's scans last a fixed time (ext_disc's duration) and each next one starts after a random
 * 0-100 ms (the 1 s dither, from DISC_COMPLETE): before WP6 one continuous scan at 100 ms could
 * stay phase-locked to a sensor's heartbeat bursts, so about 1.5 % of sensor timings were never
 * heard (plan §5.1). A claim is granted at the end of a scan that covered Coded, so the pulse is
 * the only Coded gap (at most BLE_VALVE_CLAIM_LR_MS, I2), and RECOVERY follows it. And only as
 * often as the pulse-rate limit allows (I2b): at least 6 s + U(0, 1 s) of profile time since the
 * last pulse, and at most 12 s of pulses in any 60 s, a leak response included (the valve's claim
 * back-off does not apply then, so this is what bounds a valve that is heard but never links).
 *
 * Every scan is timed, the hold's hunt too: a cancel that meets a scan's own timeout can leave
 * NimBLE's state idle while the controller still scans, and every start then fails until that
 * scan ends by itself, which a scan with no duration never would.
 *
 * A scan's own end races a cancel: the controller's LE Scan Timeout can already be queued for the
 * host task when ble_gap_disc_cancel() returns, and ble_gap_disc_complete() then resets whatever
 * GAP procedure NimBLE runs when it is processed. A connect issued in the pass that cancelled
 * would be lost with no CONNECT event (and the valve with it); a scan started then would be reset
 * while the controller scans on, with NimBLE dropping its adverts. So:
 *   - a claim is granted only at a scan's own end (its DISC_COMPLETE, after which no end of that
 *     scan can come), in a hold's hunt as in NORMAL; never right after a cancel;
 *   - nothing starts in the pass that cancels: the next start is SCAN_SETTLE_MS later, so a
 *     queued end is delivered first, while no scan runs, and dropped (each scan's callback
 *     argument is its generation, and an end is taken only for the running scan's);
 *   - an end that still comes after the next scan started carries that scan's generation (NimBLE
 *     keeps one callback argument), but comes well before its duration: the executor then takes
 *     it as reset, waits out that scan's time, and grants no claim on it.
 *
 * The GAP handler only stores and notifies (DISC_COMPLETE). The task is woken by it, by the valve
 * module (app_ble_leak_kick()) and at least every EXEC_POLL_MS. A start that fails is retried
 * SCAN_RETRY_MS later. Priority 6 (was 4), so the next scan starts within milliseconds of an
 * edge. Its slow chores (whitelist reload, PHY save, summary) run right after a scan of at least
 * CHORES_MIN_SCAN_MS starts, while the controller scans on: never after NORMAL_LR's 0.6 s Coded
 * scan. They take milliseconds. One that waits longer than the scan runs (the whitelist's
 * provisioning read waits up to 1 s while that mutex is held; an NVS write can meet a flash erase)
 * still starts the next scan late, which widens that one gap (I1 for N_MIXED and NORMAL_LR, whose
 * slots leave about 0.2 s of slack); a timed provisioning read would remove it. No claim is granted
 * after such a late pass: the claim's freshness counts from the scan's end that the GAP handler
 * stamped, so the pulse still follows its Coded scan at once (I2).
 * --------------------------------------------------------- */

/* The sensor-firmware timings the profiles depend on (FW 1.1.0, unchanged in this release; plan
 * §4.8, documented hub assumptions) and the invariants checked against them. */
#define ADV_SMAX_MS     448     // longest advert spacing: Ta max 437.5 ms + advDelay 10 ms.
                                // PROVISIONAL until G0 measures Ta per sensor (the burst lines'
                                // shortest dT): a longer Ta raises L_MS, and the asserts below
                                // then say which profile no longer covers a burst.
#define JITTER_MS       100     // the start dither: U(0, JITTER_MS) before each NORMAL scan
#define BURST_MIN_MS   2500     // a heartbeat burst
#define BURST_EDGE_MS  4000     // a leak-edge burst
#define L_MS           (ADV_SMAX_MS + JITTER_MS)        // 548
#define W_MIN_MS       (L_MS + 50)                      // 598: shortest covering window
#define GAP_MAX_MS     (BURST_MIN_MS - 2 * L_MS)        // 1404: longest gap plus jitter
#define BLIND_MAX_MS   2800     // I2: longest span with no Coded scan
#define RECOVERY_MS    1200     // I2: the Coded window after a pulse
#define N_SCAN_MS      1000     // N_CODED's scans: the dither period D. PROVISIONAL: G4 (24 h
                                // NORMAL soak, about 86,000 restarts a day) decides 1 s or 3 s
                                // (plan §2.4, §11 WP6 fallback); the hold's hunt uses it too.
#define MIX_SLOT_MS    1000     // N_MIXED's 1M and Coded scans
#define LR_1M_MS       1000     // NORMAL_LR's 1M scan
#define LR_CODED_MS     600     // NORMAL_LR's Coded scan
_Static_assert(BLIND_MAX_MS + RECOVERY_MS <= BURST_EDGE_MS, "I2: a pulse and its recovery fit in an edge burst");
_Static_assert(RECOVERY_MS >= 2 * L_MS, "I2: the recovery spans two advert intervals");
_Static_assert(BLE_VALVE_CLAIM_LR_MS <= BLIND_MAX_MS && BLE_VALVE_CLAIM_MS <= BLE_VALVE_CLAIM_LR_MS,
               "I2: a claim pulse fits the blind budget");
_Static_assert(N_SCAN_MS >= W_MIN_MS && JITTER_MS <= GAP_MAX_MS, "I1: N_CODED");
_Static_assert(MIX_SLOT_MS >= W_MIN_MS && MIX_SLOT_MS + 2 * JITTER_MS <= GAP_MAX_MS, "I1: N_MIXED, both PHYs");
_Static_assert(LR_CODED_MS >= W_MIN_MS && LR_1M_MS + 2 * JITTER_MS <= GAP_MAX_MS, "I1: NORMAL_LR's Coded");

// The pulse-rate limit (plan §3 I2b; SUBMIT, WP8's, is exempt from the spacing). Only the valve
// claim's CONNECT pulse exists before WP8.
#define I2B_SPACING_MS     6000    // profile time (a mode that scans) between two pulses, at least ...
#define I2B_JITTER_MS      1000    // ... plus U(0, this): every re-arm jittered (the size is ours)
#define I2B_BLIND_MAX_MS  12000    // pulses in any rolling ...
#define I2B_WINDOW_MS     60000    // ... 60 s
#define I2B_RING             10    // pulses kept: one per I2B_SPACING_MS of the window
_Static_assert(I2B_RING * I2B_SPACING_MS >= I2B_WINDOW_MS, "I2b: the ring holds every pulse of a window");
_Static_assert(BLE_VALVE_CLAIM_LR_MS <= I2B_BLIND_MAX_MS, "I2b: a pulse fits the blind budget");

#define SCAN_GONE_MS        1000    // a scan NimBLE dropped with no DISC_COMPLETE is restarted after this
#define SCAN_SETTLE_MS      50      // nothing starts this soon after a cancel (a queued end is delivered first)
#define SCAN_EARLY_MS       100     // an end this much before a scan's duration is a stopped scan's late end
#define SCAN_OVERDUE_MS     1000    // a scan with no end this long after its duration is stopped and restarted
#define CHORES_LATE_MS      5000    // the executor's slow chores run at least this often
#define CHORES_MIN_SCAN_MS  1000    // ... and otherwise only right after a scan this long starts
#define SUMMARY_MS          60000   // the summary line's period
#define DUTY_WARN_PCT       80      // the duty watchdog: BLE scanning below this share of the
#define DUTY_WARN_RUNS      2       // ... expected time in this many summaries in a row warns

// A scan geometry. Interval and window in 0.625 ms units (both 0: that PHY is not scanned);
// duration in 10 ms units (0: until cancelled). Every scan is passive with filter_duplicates
// off (scan_start()).
typedef struct {
    uint16_t itvl_1m, win_1m;
    uint16_t itvl_c, win_c;
    uint16_t dur;
} scan_geo_t;

enum {
    GEO_NONE = 0, GEO_N_CODED, GEO_1M, GEO_CODED_MIX, GEO_CODED_LR, GEO_RECOVERY, GEO_HUNT, GEO_COUNT
};

static const scan_geo_t k_scan_geo[GEO_COUNT] = {
    [GEO_NONE]      = { 0, 0, 0, 0, 0 },
    // N_CODED: 20 ms of 1M and 80 ms of Coded in every 100 ms, for 1 s.
    [GEO_N_CODED]   = { 160, 32, 160, 128, N_SCAN_MS / 10 },
    // One PHY at full duty: N_MIXED's and NORMAL_LR's 1M scan (the valve, 1M sensors) ...
    [GEO_1M]        = { 160, 160, 0, 0, MIX_SLOT_MS / 10 },
    // ... N_MIXED's and NORMAL_LR's Coded scans, and the recovery window.
    [GEO_CODED_MIX] = { 0, 0, 160, 160, MIX_SLOT_MS / 10 },
    [GEO_CODED_LR]  = { 0, 0, 160, 160, LR_CODED_MS / 10 },
    [GEO_RECOVERY]  = { 0, 0, 160, 160, RECOVERY_MS / 10 },
    // The valve hunt in a hold (as before WP6): 110 ms / 55 ms on each PHY, in 1 s scans.
    // Deliberately NOT 100 ms: the valve advertises every 500-700 ms, and 100 ms against 500 ms
    // is a 5:1 harmonic lock in which escape depends solely on the 0-10 ms per-event advDelay
    // drifting the phase; 110 ms breaks the lock at the same duty (50 %). Passive (it was active
    // on 1M until WP5): the valve is matched by its MAC alone.
    [GEO_HUNT]      = { 176, 88, 176, 88, N_SCAN_MS / 10 },
};
_Static_assert(LR_1M_MS == MIX_SLOT_MS, "GEO_1M serves N_MIXED and NORMAL_LR");

typedef enum {
    MODE_PAUSED = 0, MODE_HOLD_HUNT, MODE_N_CODED, MODE_N_MIXED, MODE_NORMAL_LR, MODE_RECOVERY,
    MODE_CLAIM, MODE_COUNT
} scan_mode_t;

typedef struct {
    const uint8_t *geo;     // the scans, in turn
    uint8_t n;
} scan_prof_t;

static const uint8_t k_p_hunt[] = { GEO_HUNT };
static const uint8_t k_p_n_coded[] = { GEO_N_CODED };
static const uint8_t k_p_n_mixed[] = { GEO_1M, GEO_CODED_MIX };
static const uint8_t k_p_lr[] = { GEO_1M, GEO_CODED_LR };
static const uint8_t k_p_recovery[] = { GEO_RECOVERY };

static const scan_prof_t k_prof[MODE_COUNT] = {
    [MODE_PAUSED]    = { NULL, 0 },
    [MODE_HOLD_HUNT] = { k_p_hunt, 1 },
    [MODE_N_CODED]   = { k_p_n_coded, 1 },
    [MODE_N_MIXED]   = { k_p_n_mixed, 2 },
    [MODE_NORMAL_LR] = { k_p_lr, 2 },
    [MODE_RECOVERY]  = { k_p_recovery, 1 },
    [MODE_CLAIM]     = { NULL, 0 },
};

static bool geo_has_coded(uint8_t geo)
{
    return k_scan_geo[geo].win_c != 0;
}

// NORMAL's modes, and RECOVERY: switched only at a scan's end.
static bool mode_is_normal(uint8_t mode)
{
    return mode == MODE_N_CODED || mode == MODE_N_MIXED || mode == MODE_NORMAL_LR ||
           mode == MODE_RECOVERY;
}

// The executor's own state, on its stack: only its task reads or writes it.
typedef struct {
    bool scan_on;               // our scan runs (started, no DISC_COMPLETE or cancel since)
    bool slot_ended;            // the last scan ran to its end: the profile's next is due
    bool coded_last;            // the last scan that ended covered Coded
    bool recovery;              // a claim pulse ended: RECOVERY is due
    bool claim_inflight;        // a granted claim's connect may still be in flight
    bool just_started;          // this pass started a scan of CHORES_MIN_SCAN_MS or more: chores
    bool paused;                // the window or a radio hold has the radio
    bool portal_paused;         // ... the window itself, for its log lines
    bool i2b_noted;             // a claim waits for I2b: logged once per wait
    uint8_t geo;                // the geometry running, or that ran last
    uint8_t mode;               // the mode now
    uint8_t slot_mode;          // the mode whose profile the last scan belonged to (MODE_COUNT: none)
    uint8_t slot;               // ... and its place in that profile
    uint8_t announced;          // the scanning mode last announced (MODE_COUNT: none)
    uint8_t low_duty;           // summaries in a row below DUTY_WARN_PCT
    uint8_t gen;                // the running (or last) scan's generation: its callback argument
    uint8_t gen_next;           // ... the last one handed out (every start attempt takes one)
    uint8_t pulse_i;            // the next slot of the pulse ring (I2b)
    uint16_t start_fails;       // scan starts failed in a row
    uint16_t claims;            // claim pulses since the last summary
    TickType_t retry_at;        // no scan start before this tick
    TickType_t started_at;      // when the running (or last) scan started
    TickType_t coded_end_at;    // when the last scan that covered Coded ended
    TickType_t gone_at;         // our scan was first seen gone without its DISC_COMPLETE (0: no)
    TickType_t paused_since;    // when the pause began, for the heartbeat line
    TickType_t acct_at;         // time accounted up to here
    uint32_t mode_ms[MODE_COUNT];   // time per mode since the last summary
    uint32_t want_ms;           // ... in modes that should scan
    uint32_t on_ms;             // ... of it with our scan running
    TickType_t pulse_at;        // when the claim pulse in flight began
    uint32_t prof_ms;           // profile time since the last pulse (I2b), up to I2B_WINDOW_MS ...
    uint32_t space_ms;          // ... and the spacing the next pulse needs
    TickType_t pulse_end[I2B_RING]; // the last pulses (I2b): when each ended ...
    uint16_t pulse_ms[I2B_RING];    // ... and how long it was (0: none)
} exec_t;

/* ---------------------------------------------------------
 * GAP event callback of every scan the executor starts.
 * Handles both legacy (1M PHY) and extended (Coded PHY) advertising reports from a single
 * ble_gap_ext_disc() session, and the session's end. Stores and notifies only: the adverts go to
 * the leak sensors' state and the valve's MAC hook, and the end is left to the executor.
 * --------------------------------------------------------- */
static int ble_leak_gap_event(struct ble_gap_event *event, void *arg)
{
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
        s_scan_end_gen = (uint8_t)(uintptr_t)arg;
        s_scan_end_tick = xTaskGetTickCount();
        s_scan_ended = true;   // last: the executor reads the others after it
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
 * Start a scan of geometry `geo` (executor task only): 0, or the NimBLE error. `gen` is the
 * scan's generation, its GAP callback argument (see the executor's notes).
 *
 * filter_duplicates is DISABLED, and that is a safety fix, not a latency one. The controller's
 * duplicate cache keys on ADDRESS ONLY and never refreshes (CONFIG_BT_CTRL_SCAN_DUPL_TYPE=0,
 * CONFIG_BT_CTRL_DUPL_SCAN_CACHE_REFRESH_PERIOD=0), so with the filter on, a leak sensor whose
 * payload changes from dry to WET is suppressed: the address has already been seen. Until 2.1.x
 * the valve hunt ran with the filter on, and the hub was deaf to its leak sensors for as long as
 * it hunted a dead valve. The cost of turning it off: duplicate valve reports, which the valve
 * module's claim guard absorbs (ble_valve_note_adv()).
 * --------------------------------------------------------- */
static int scan_start(uint8_t geo, uint8_t gen)
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
        (void *)(uintptr_t)gen
    );
#else
    // Fallback: legacy scanning (1M PHY only)
    struct ble_gap_disc_params disc_params = {0};
    disc_params.passive = 1;
    disc_params.filter_duplicates = 0;
    disc_params.itvl = g->itvl_1m ? g->itvl_1m : g->itvl_c;
    disc_params.window = g->win_1m ? g->win_1m : g->win_c;
    return ble_gap_disc(BLE_OWN_ADDR_PUBLIC, g->dur ? (int32_t)g->dur * 10 : BLE_HS_FOREVER,
                        &disc_params, ble_leak_gap_event, (void *)(uintptr_t)gen);
#endif
}

// Stops our scan before its end (executor task only): the profile then restarts with Coded, and
// nothing starts before SCAN_SETTLE_MS (its own end may already be queued, see the executor's
// notes). A cancel the controller refuses leaves it marked running, and the next pass tries again.
static void scan_stop(exec_t *x, TickType_t now)
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
    x->slot_ended = false;
    x->coded_last = false;
    x->slot_mode = MODE_COUNT;
    x->retry_at = now + pdMS_TO_TICKS(SCAN_SETTLE_MS);
}

// Our scan ended (executor task only): its DISC_COMPLETE, delivered at `at` (the GAP handler's
// stamp), or (lost) NimBLE dropped it with none. A timed scan's own end makes the profile's next
// one due after the dither from that end, and lets a claim be granted (executor_pass()). A
// continuous one (none in WP6's geometries) is not expected to end, and starts again
// SCAN_RESTART_DELAY_MS later.
// Three ends are no scan's own: a lost one, one with a reason (a host reset), and one that comes
// well before the scan's duration, which is a stopped scan's late end that reset this one (see
// the executor's notes). They grant no claim and restart the profile with its Coded scan, as after
// a stop; after the third the controller scans on to this scan's end, so nothing starts before.
static void scan_ended(exec_t *x, TickType_t now, TickType_t at, bool lost)
{
    x->scan_on = false;
    x->gone_at = 0;
    uint32_t dur_ms = (uint32_t)k_scan_geo[x->geo].dur * 10u;
    if (dur_ms == 0) {
        if (!lost) {
            ESP_LOGW(BLE_LEAK_TAG, "Scan complete (reason=%d) — will restart", s_scan_end_reason);
        }
        x->retry_at = now + pdMS_TO_TICKS(SCAN_RESTART_DELAY_MS);
        x->coded_last = false;
        return;
    }
    TickType_t ran = at - x->started_at;
    bool early = !lost && s_scan_end_reason == 0 &&
                 ran + pdMS_TO_TICKS(SCAN_EARLY_MS) < pdMS_TO_TICKS(dur_ms);
    if (lost || early || s_scan_end_reason != 0) {
        x->slot_ended = false;
        x->coded_last = false;
        x->slot_mode = MODE_COUNT;
        x->retry_at = now + pdMS_TO_TICKS(SCAN_SETTLE_MS);
        if (early) {
            ESP_LOGW(BLE_LEAK_TAG, "Scan ended after %lu of its %lu ms: a stopped scan's late end - the next starts after this one's time",
                     (unsigned long)(ran * portTICK_PERIOD_MS), (unsigned long)dur_ms);
            x->retry_at = x->started_at + pdMS_TO_TICKS(dur_ms + SCAN_SETTLE_MS);
        }
        return;
    }
    x->slot_ended = true;
    x->coded_last = geo_has_coded(x->geo);
    if (x->coded_last) {
        x->coded_end_at = at;
    }
    if (x->geo == GEO_RECOVERY) {
        x->recovery = false;
    }
    x->retry_at = at + pdMS_TO_TICKS(esp_random() % (JITTER_MS + 1));
}

// The lines for a scanning mode, when its first scan starts. The leak scan's start line is the one
// it always printed (after a pause, at boot); a NORMAL mode prints its own once per change. The
// transient RECOVERY and CLAIM print nothing.
static void exec_announce(exec_t *x, uint8_t mode)
{
    if (mode == x->announced || mode == MODE_RECOVERY || mode == MODE_CLAIM) {
        return;
    }
    if (mode == MODE_HOLD_HUNT) {
        ESP_LOGI(BLE_LEAK_TAG, "Valve hunt scan started (1M + Coded PHY, passive)");
    } else {
        if (!mode_is_normal(x->announced)) {
            ESP_LOGI(BLE_LEAK_TAG, "Extended passive scan started (1M + Coded PHY)");
        }
        if (mode == MODE_N_CODED) {
            ESP_LOGI(BLE_LEAK_TAG, "Scan mode N_CODED: 1M 20 %% + Coded 80 %%, 1 s scans, each next after 0-%d ms",
                     JITTER_MS);
        } else if (mode == MODE_N_MIXED) {
            ESP_LOGI(BLE_LEAK_TAG, "Scan mode N_MIXED (a sensor is on 1M): 1 s on 1M and 1 s on Coded in turn, each next after 0-%d ms",
                     JITTER_MS);
        } else {
            ESP_LOGI(BLE_LEAK_TAG, "Scan mode NORMAL_LR (leak response, valve not linked): 1 s on 1M and 0.6 s on Coded in turn, each next after 0-%d ms",
                     JITTER_MS);
        }
    }
    x->announced = mode;
}

// Time spent since the last pass goes to the mode it was spent in (for the summary and the duty
// watchdog). The state is the one the last pass left, which is what ran meanwhile.
static void exec_account(exec_t *x, TickType_t now)
{
    uint32_t dt = (uint32_t)(now - x->acct_at) * portTICK_PERIOD_MS;
    x->acct_at = now;
    x->mode_ms[x->mode] += dt;
    if (x->mode == MODE_HOLD_HUNT || mode_is_normal(x->mode)) {
        x->want_ms += dt;
        x->prof_ms = (x->prof_ms + dt < I2B_WINDOW_MS) ? x->prof_ms + dt : I2B_WINDOW_MS;
        if (x->scan_on) {
            x->on_ms += dt;
        }
    }
}

// The pulse-rate limit for the next claim (I2b): enough profile time since the last pulse, and room
// in the rolling window for the next one at its longest (BLE_VALVE_CLAIM_LR_MS while a leak
// response is pending), counting the pulses that end in it. Logs once per wait.
static bool i2b_allows(exec_t *x, TickType_t now)
{
    uint32_t next_ms = ble_valve_lr_pending() ? BLE_VALVE_CLAIM_LR_MS : BLE_VALVE_CLAIM_MS;
    TickType_t from = now + pdMS_TO_TICKS(next_ms) - pdMS_TO_TICKS(I2B_WINDOW_MS);
    uint32_t blind = 0;
    for (int i = 0; i < I2B_RING; i++) {
        if (x->pulse_ms[i] != 0 && (int32_t)(x->pulse_end[i] - from) > 0) {
            blind += x->pulse_ms[i];
        }
    }
    if (x->prof_ms >= x->space_ms && blind + next_ms <= I2B_BLIND_MAX_MS) {
        return true;
    }
    if (!x->i2b_noted) {
        x->i2b_noted = true;
        ESP_LOGI(BLE_LEAK_TAG, "[CLAIM] Valve claim waits for the pulse-rate limit (I2b): %lu.%lu of %lu.%lu s scanned since the last pulse, %lu.%lu s of pulses in the last 60 s",
                 (unsigned long)(x->prof_ms / 1000), (unsigned long)(x->prof_ms % 1000 / 100),
                 (unsigned long)(x->space_ms / 1000), (unsigned long)(x->space_ms % 1000 / 100),
                 (unsigned long)(blind / 1000), (unsigned long)(blind % 1000 / 100));
    }
    return false;
}

/* ---------------------------------------------------------
 * One pass of the executor: every fact is read again (I6), so a missed edge costs at most one
 * pass. Returns how long it may sleep before the next one.
 * --------------------------------------------------------- */
static TickType_t executor_pass(exec_t *x)
{
    TickType_t now = xTaskGetTickCount();
    exec_account(x, now);
    x->just_started = false;

    if (s_scan_ended) {
        s_scan_ended = false;
        // Only the running scan's end: a late end of a scan already stopped carries that scan's
        // generation and is dropped.
        uint8_t gen = s_scan_end_gen;
        TickType_t at = s_scan_end_tick;
        if (x->scan_on && gen == x->gen) {
            scan_ended(x, now, at, false);
        }
    }
    // Self-healing: our scan is gone with no DISC_COMPLETE. NimBLE delivers one for every end
    // but our own cancel (a host reset included), and clears its state just before it does, so
    // only a scan still gone SCAN_GONE_MS later counts as lost.
    if (x->scan_on && !ble_gap_disc_active()) {
        if (x->gone_at == 0) {
            x->gone_at = now ? now : 1;
        } else if ((now - x->gone_at) >= pdMS_TO_TICKS(SCAN_GONE_MS)) {
            ESP_LOGW(BLE_LEAK_TAG, "Scan not active (external cancel?), restarting");
            scan_ended(x, now, now, true);
        }
    } else {
        x->gone_at = 0;
    }
    // ... and the reverse: NimBLE still runs our scan SCAN_OVERDUE_MS past its duration, with no
    // DISC_COMPLETE (a lost LE Scan Timeout). Every scan ends by its controller timeout, about
    // 86,000 a day; without this a lost one would leave the hub deaf to every BLE sensor with
    // NimBLE's state, and the duty figures (which count this task's view), saying it scans.
    if (x->scan_on && k_scan_geo[x->geo].dur != 0 &&
        (now - x->started_at) >= pdMS_TO_TICKS((uint32_t)k_scan_geo[x->geo].dur * 10u + SCAN_OVERDUE_MS)) {
        ESP_LOGW(BLE_LEAK_TAG, "Scan overdue: no end %d ms after its %u ms - stopping and restarting it",
                 SCAN_OVERDUE_MS, (unsigned)k_scan_geo[x->geo].dur * 10u);
        (void)ble_gap_disc_cancel();
        scan_ended(x, now, now, true);
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

    // A claim pulse that ended (its CONNECT event wakes this task): the Coded recovery is due.
    bool conn = ble_gap_conn_active();
    if (x->claim_inflight && !conn) {
        x->claim_inflight = false;
        x->coded_last = false;
        // The pulse, into I2b's window.
        uint32_t ms = (uint32_t)(now - x->pulse_at) * portTICK_PERIOD_MS;
        x->pulse_end[x->pulse_i] = now;
        x->pulse_ms[x->pulse_i] = (uint16_t)(ms == 0 ? 1 : (ms > UINT16_MAX ? UINT16_MAX : ms));
        x->pulse_i = (uint8_t)((x->pulse_i + 1) % I2B_RING);
        if (!hold) {
            x->recovery = true;
            x->retry_at = now;
        }
    }

    uint8_t mode;
    if (conn) {
        mode = MODE_CLAIM;              // a connect in flight: no scan can start
    } else if (!ble_hs_synced()) {
        mode = MODE_PAUSED;
    } else if (hold) {
        mode = ble_valve_hunt_wanted() ? MODE_HOLD_HUNT : MODE_PAUSED;
    } else if (x->recovery) {
        mode = MODE_RECOVERY;
    } else if (ble_valve_lr_active()) {
        mode = MODE_NORMAL_LR;
    } else if (phy_any_1m()) {
        mode = MODE_N_MIXED;
    } else {
        mode = MODE_N_CODED;
    }

    // The valve module's claim: in the dither right after a scan that covered Coded ended by
    // itself, so the pulse is the only Coded gap (I2), in NORMAL and in a hold's hunt alike, when
    // the pulse-rate limit allows (I2b). Never right after a cancel (see the executor's notes).
    // Not after a pause, whose last scan ended long before (the profile scans first).
    if (ble_valve_claim_wanted()) {
        bool fresh = !x->scan_on && x->slot_ended && x->coded_last &&
                     (now - x->coded_end_at) <= pdMS_TO_TICKS(JITTER_MS) + 1;
        if (fresh && (mode == MODE_HOLD_HUNT || mode_is_normal(mode)) && i2b_allows(x, now)) {
            ble_valve_claim_start();
            if (ble_gap_conn_active()) {
                x->claim_inflight = true;
                x->slot_ended = false;
                x->coded_last = false;
                x->slot_mode = MODE_COUNT;
                if (x->claims < UINT16_MAX) {
                    x->claims++;
                }
                x->pulse_at = now;
                x->prof_ms = 0;
                x->space_ms = I2B_SPACING_MS + esp_random() % (I2B_JITTER_MS + 1);
                x->i2b_noted = false;
                mode = MODE_CLAIM;
            }
        }
    } else {
        x->i2b_noted = false;
    }

    if (mode != x->mode) {
        if (mode == MODE_PAUSED) {
            x->announced = MODE_PAUSED;
        }
        x->mode = mode;
    }
    // A scan the mode has no part in stops now: in a hold, its hunt, or a claim. NORMAL's modes
    // switch at a scan's end. On every pass, so a cancel the controller refused is tried again.
    if (x->scan_on && x->slot_mode != mode && !(mode_is_normal(mode) && mode_is_normal(x->slot_mode))) {
        scan_stop(x, now);
    }

    const scan_prof_t *p = &k_prof[mode];
    if (!x->scan_on && p->n > 0 && (int32_t)(now - x->retry_at) >= 0) {
        uint8_t slot = 0;
        if (x->slot_mode == mode) {
            slot = x->slot_ended ? (uint8_t)((x->slot + 1) % p->n) : x->slot;
        } else if (!x->coded_last) {
            // A new profile after a scan with no Coded (or none): its Coded scan first (I1).
            while (slot < p->n - 1 && !geo_has_coded(p->geo[slot])) {
                slot++;
            }
        }
        uint8_t geo = p->geo[slot];
        uint8_t gen = ++x->gen_next;
        int rc = scan_start(geo, gen);
        if (rc == 0) {
            x->scan_on = true;
            x->geo = geo;
            x->gen = gen;
            x->started_at = now;
            x->slot = slot;
            x->slot_mode = mode;
            x->slot_ended = false;
            x->just_started = ((uint32_t)k_scan_geo[geo].dur * 10u >= CHORES_MIN_SCAN_MS);
            if (x->start_fails > 0) {
                ESP_LOGI(BLE_LEAK_TAG, "Scan started after %u failed attempt(s)", (unsigned)x->start_fails);
                x->start_fails = 0;
            }
            exec_announce(x, mode);
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
    if (!x->scan_on && p->n > 0) {
        int32_t until = (int32_t)(x->retry_at - now);
        if (until < 1) {
            until = 1;
        }
        if ((TickType_t)until < wait) {
            wait = (TickType_t)until;
        }
    }
    if (x->gone_at != 0 && pdMS_TO_TICKS(SCAN_GONE_MS) < wait) {
        wait = pdMS_TO_TICKS(SCAN_GONE_MS);
    }
    return wait;
}

// The 60 s summary (plan §4.7, the parts there are before the radio policy) and the duty
// watchdog. In a frame of its own: its line buffer is off the loop's frame.
static __attribute__((noinline)) void exec_summary(exec_t *x)
{
    // Adverts heard per sensor since the last summary, with its PHY: "9AE6=12C" (the MAC's last
    // two bytes as printed elsewhere; C Coded, M 1M, ? unknown).
    uint8_t mac[MAX_TRACKED_SENSORS][2];
    uint8_t cnt[MAX_TRACKED_SENSORS];
    uint8_t phy[MAX_TRACKED_SENSORS];
    int k = 0;
    taskENTER_CRITICAL(&s_wl_lock);
    for (int i = 0; i < MAX_TRACKED_SENSORS; i++) {
        if (s_sensors[i].in_use) {
            mac[k][0] = s_sensors[i].mac[1];
            mac[k][1] = s_sensors[i].mac[0];
            cnt[k] = s_adv_n[i];
            int w = whitelist_find_locked(s_sensors[i].mac);
            phy[k] = (w >= 0) ? PHY_OF(s_wl_phy[w]) : PHY_UNKNOWN;
            k++;
        }
        s_adv_n[i] = 0;
    }
    uint16_t flips = s_phy_flips;
    s_phy_flips = 0;
    taskEXIT_CRITICAL(&s_wl_lock);

    char adv[MAX_TRACKED_SENSORS * 10 + 1];
    int at = 0;
    adv[0] = '\0';
    for (int i = 0; i < k && at < (int)sizeof(adv) - 1; i++) {
        at += snprintf(adv + at, sizeof(adv) - at, "%s%02X%02X=%u%c", i ? " " : "", mac[i][0], mac[i][1],
                       (unsigned)cnt[i], phy[i] == PHY_CODED ? 'C' : (phy[i] == PHY_1M ? 'M' : '?'));
    }

    // No stack figure: uxTaskGetStackHighWaterMark() is linked nowhere else, and with
    // CONFIG_FREERTOS_IN_IRAM it would land in IRAM (I10: DIRAM .text stays as it is). The bench's
    // task dump (T6-11) measures this task's stack.
    unsigned duty = x->want_ms ? (unsigned)((uint64_t)x->on_ms * 100u / x->want_ms) : 100u;
    ESP_LOGI(BLE_LEAK_TAG, "[SUMMARY] modes N_CODED %lu s, N_MIXED %lu s, NORMAL_LR %lu s, hold hunt %lu s, paused %lu s; "
             "BLE scanning %lu.%lu of %lu.%lu s (%u %%); claims %u: pulses %lu.%lu s, recovery %lu.%lu s; "
             "PHY changes %u; adverts %s",
             (unsigned long)(x->mode_ms[MODE_N_CODED] / 1000), (unsigned long)(x->mode_ms[MODE_N_MIXED] / 1000),
             (unsigned long)(x->mode_ms[MODE_NORMAL_LR] / 1000), (unsigned long)(x->mode_ms[MODE_HOLD_HUNT] / 1000),
             (unsigned long)(x->mode_ms[MODE_PAUSED] / 1000),
             (unsigned long)(x->on_ms / 1000), (unsigned long)(x->on_ms % 1000 / 100),
             (unsigned long)(x->want_ms / 1000), (unsigned long)(x->want_ms % 1000 / 100), duty,
             (unsigned)x->claims,
             (unsigned long)(x->mode_ms[MODE_CLAIM] / 1000), (unsigned long)(x->mode_ms[MODE_CLAIM] % 1000 / 100),
             (unsigned long)(x->mode_ms[MODE_RECOVERY] / 1000), (unsigned long)(x->mode_ms[MODE_RECOVERY] % 1000 / 100),
             (unsigned)flips, k ? adv : "none heard yet");

    // The duty watchdog: judged on minutes with at least half of them meant for scanning.
    if (x->want_ms >= SUMMARY_MS / 2 && duty < DUTY_WARN_PCT) {
        if (x->low_duty < UINT8_MAX) {
            x->low_duty++;
        }
        if (x->low_duty >= DUTY_WARN_RUNS && (x->low_duty - DUTY_WARN_RUNS) % 5 == 0) {
            ESP_LOGW(BLE_LEAK_TAG, "Duty watchdog: BLE scanning ran %u %% of its expected time for %u min (below %d %%)",
                     duty, (unsigned)x->low_duty, DUTY_WARN_PCT);
        }
    } else {
        x->low_duty = 0;
    }

    memset(x->mode_ms, 0, sizeof(x->mode_ms));
    x->want_ms = 0;
    x->on_ms = 0;
    x->claims = 0;

    // I2b's ring: pulses out of the window are dropped, so a wrapped tick count cannot bring one back.
    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < I2B_RING; i++) {
        if (x->pulse_ms[i] != 0 && (now - x->pulse_end[i]) >= pdMS_TO_TICKS(I2B_WINDOW_MS)) {
            x->pulse_ms[i] = 0;
        }
    }
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

    // Load whitelist (and, the first time, each sensor's PHY from NVS)
    reload_whitelist();
    // Under the lock: the GAP handler can already be feeding process_leak_adv() now that the
    // whitelist is populated.
    taskENTER_CRITICAL(&s_wl_lock);
    memset(s_sensors, 0, sizeof(s_sensors));
    memset(s_adv_n, 0, sizeof(s_adv_n));
    taskEXIT_CRITICAL(&s_wl_lock);

    // The first scan starts on the first pass, once NimBLE is synced: no delay any more. The 2 s
    // the old loop waited let the valve module's own scan start first, so that the two did not
    // race; the hunt is now this task's own scan.
    exec_t x;
    memset(&x, 0, sizeof(x));
    TickType_t t0 = xTaskGetTickCount();
    TickType_t last_whitelist_reload = t0;
    TickType_t last_heartbeat_log = t0;
    TickType_t last_summary = t0;
    TickType_t phy_saved_at = 0;
    TickType_t last_chores = t0;
    x.retry_at = t0;
    x.acct_at = t0;
    x.mode = MODE_PAUSED;
    x.slot_mode = MODE_COUNT;
    x.announced = MODE_COUNT;

    for (;;) {
        TickType_t wait = executor_pass(&x);

        // The slow chores: right after a scan of CHORES_MIN_SCAN_MS or more starts (the controller
        // scans on meanwhile; see the executor's notes for a chore that outlasts it), while the
        // mode scans nothing, or once they are CHORES_LATE_MS overdue (scan starts that keep
        // failing). Never in the dither between two scans.
        TickType_t tnow = xTaskGetTickCount();
        if (x.just_started || k_prof[x.mode].n == 0 ||
            (tnow - last_chores) >= pdMS_TO_TICKS(CHORES_LATE_MS)) {
            TickType_t now = tnow;
            last_chores = now;

            // Periodically reload whitelist (handles runtime commissioning)
            if ((now - last_whitelist_reload) >= pdMS_TO_TICKS(WHITELIST_RELOAD_MS)) {
                reload_whitelist();
                last_whitelist_reload = xTaskGetTickCount();
            }

            // The PHY table: saved when it changed, at most once a minute.
            if (s_phy_dirty && (phy_saved_at == 0 || (now - phy_saved_at) >= pdMS_TO_TICKS(PHY_SAVE_MIN_MS))) {
                phy_save();
                phy_saved_at = now ? now : 1;
            }

            // The burst log: a line for each sensor's burst that is over (paused or not).
            burst_log();

            // Periodic scan-alive heartbeat (every 60s), with the pause if the leak scan is held
            if ((now - last_heartbeat_log) >= pdMS_TO_TICKS(60000)) {
                heartbeat_log(x.paused, x.portal_paused, x.paused_since);
                last_heartbeat_log = now;
            }

            // The 60 s summary and the duty watchdog.
            if ((now - last_summary) >= pdMS_TO_TICKS(SUMMARY_MS)) {
                exec_account(&x, now);
                exec_summary(&x);
                last_summary = now;
            }
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

    // NimBLE's own log is at WARN by sdkconfig (CONFIG_BT_NIMBLE_LOG_LEVEL_WARNING, guarded in
    // main.c; plan §4.7): NORMAL starts a scan every second, and NimBLE printed "GAP procedure
    // initiated: extended discovery" at INFO for each, about 86,000 lines a day. Compiled out, it
    // needs no esp_log_level_set() tag node on the heap (I10).

    // Priority 6 (was 4, plan §4.7): the executor's scan starts and claim grants land within
    // milliseconds. Its passes are short; whitelist reloads and log lines are as before. Stack
    // unchanged: the deepest frames are still the whitelist read and the log calls, and a NimBLE
    // connect (the claim) costs about what the scan start beside it does. The PHY table's NVS
    // read and write run from a shallow frame (about 1.5-2 KB deep with NVS's own frames).
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
