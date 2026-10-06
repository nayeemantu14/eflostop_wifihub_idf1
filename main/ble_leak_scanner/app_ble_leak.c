/****************************************************
 *  MODULE:   BLE Leak Sensor Scanner and the BLE scan executor
 *  PURPOSE:  Passive BLE scanner for "eleak" leak sensors.
 *            Uses extended scanning (ble_gap_ext_disc) to receive
 *            both legacy 1M and Coded PHY advertisements, enabling
 *            support for STM32WB (legacy) and STM32WBA (long range)
 *            leak sensors simultaneously.
 *            Since 2.1.4 (WP5) its task is the only code that starts
 *            or stops a BLE scan; the valve module's hunt for its
 *            valve runs on these scans, and since WP8 the scans and
 *            pulses are the radio policy's (radio_policy.c; see "The
 *            BLE scan executor").
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
#include "ble_valve/app_ble_valve.h"
#include "radio_policy/radio_policy.h"

/* ---------------------------------------------------------
 * Constants
 * --------------------------------------------------------- */
#define BLE_LEAK_TAG            "BLE_LEAK"
#define ELEAK_COMPANY_ID        0x0030      // ST Microelectronics
#define ELEAK_MFG_DATA_LEN      4           // 2B company ID + 1B leak + 1B battery
#define ELEAK_DEVICE_NAME       "eleak"
#define ELEAK_DEVICE_NAME_LEN   5
#define MAX_TRACKED_SENSORS     MAX_BLE_LEAK_SENSORS
#define EXEC_POLL_MS            500         // the executor's longest wait: every fact is read
                                            // again at least this often (I6)
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
    TickType_t heard_at;           // its last advert (never 0 once heard): the starvation guard's
                                   // fact (starve_scan()); app_ble_leak_reset_tracking() keeps it
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

// The executor task's facts about the list for the radio policy (exec_facts()): when the list last
// gained a sensor, or was first loaded (0: not yet), which times B3's 1M share for a PHY not known
// yet and discovery's back-off; and the starvation guard's fact, from the chores (starve_scan()).
static TickType_t s_listed_at = 0;
static uint8_t s_starved = RP_STARVED_NONE;

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
// PHY_FLIP_ADVERTS from a few adverts per burst). True when a known PHY's sensor left. *grew: the
// new list holds a sensor the old one did not (a provisioning).
static __attribute__((noinline)) bool phy_carry(const uint8_t wl[][6], uint8_t count, uint8_t phy[],
                                                bool *grew)
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
    *grew = false;
    for (int i = 0; i < count && !*grew; i++) {
        *grew = (wl_find((const uint8_t (*)[6])old_wl, old_n, wl[i]) < 0);
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
    bool grew;
    if (!s_phy_loaded) {
        phy_dropped = phy_load(wl, count, phy);
        s_phy_loaded = true;
        grew = (count > 0);
    } else {
        phy_dropped = phy_carry(wl, count, phy, &grew);
    }
    // A sensor provisioned (or the first load since boot): B3's 1M share while its PHY is unknown,
    // and discovery's back-off, count from now (the radio policy, through exec_facts()).
    if (grew) {
        TickType_t t = xTaskGetTickCount();
        s_listed_at = t ? t : 1;
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
    TickType_t heard = xTaskGetTickCount();
    heard = heard ? heard : 1;
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
            s_sensors[slot].heard_at = heard;
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
 * The BLE scan executor (2.1.4 WP5, WP6 and WP8's core; plan §4.2-§4.8; I1-I3, I5, I6)
 *
 * This task is the ONLY code that starts or stops a BLE scan (ble_gap_ext_disc,
 * ble_gap_disc_cancel). It runs what the radio policy says (radio_policy.c, the single source of
 * the radio mode): a row of its profile table, one scan pattern whose slots run in turn (a Coded
 * scan, a 1M scan, both, or a Wi-Fi slot with no scan), and the pulses it grants: BLE off for a
 * station's join assist, a setup-page Connect, a router retry, a network-list scan, or a valve
 * claim's connect (the one this task issues itself, through the valve module). The valve module only
 * says what it wants (ble_valve_hunt_wanted(), ble_valve_claim_wanted()). Every advert of every scan
 * goes to the leak sensors (process_leak_adv()) and to the valve's MAC hook (ble_valve_note_adv()).
 *
 * What runs, first match, decided again on every pass from every fact (I6):
 *   a pulse         no scan until it ends: its requester's end, its own rule (a join assist's lease
 *                   and first page), a CONNECT event, or its deadline, which this task expires itself
 *                   (I3); then RECOVERY, 1.2 s of Coded, when its blind span was longer than the
 *                   row's own Coded gap (I2);
 *   not synced      no scan;
 *   RECOVERY        due after a pulse (above);
 *   the mode's row  radio_policy_exec_row(): N_CODED, N_HUNT (B2: the valve hunted in NORMAL),
 *                   N_MIXED (a sensor on 1M, or B3's unknown PHY), NORMAL_LR, an AP mode's row
 *                   (AP_IDLE, SERVE, LR_AP: the SoftAP up and the STA not connected), or none
 *                   (BLE_IDLE).
 * Nothing else pauses BLE for Wi-Fi: since 2.1.4 WP8 app_wifi.c's portal priority window and Wi-Fi
 * radio holds are gone, and the no-credential setup portal keeps scanning in the AP modes (D2).
 * A row changes only at a slot boundary (a pulse excepted), and a new row starts at its
 * Coded window, or right after it when the slot that just ended was a Coded window, so no switch
 * widens a Coded gap beyond a row's own (I1); a row that holds I8 starts with a 0.3 s Wi-Fi slot
 * when a 1M slot just ended, if I1 allows it. NORMAL's rows start each scan U(0, 100 ms) after the
 * last one ended (the 1 s dither: before WP6 one continuous scan could stay phase-locked to a
 * sensor's heartbeat bursts, plan §5.1). A Wi-Fi slot is timed from the end of the slot before it,
 * so a late pass shortens it rather than widening the Coded gap after it.
 *
 * Pulses (radio_policy_exec_wifi_grant(), radio_policy_exec_connect_len()): each is at most the
 * blind budget, 2.8 s from the end of the last Coded window (I2), and the policy's I2b spacing and
 * per-minute budget hold every kind (SUBMIT is exempt from the spacing only). A SUBMIT or a JOIN
 * stops the running scan at once (a Coded scan that has not covered RP_L_MS does not count, and the
 * pulse is clipped to the budget from the last one that did; with less than RP_UNALIGNED_MIN_MS of
 * it left the scan covers its interval first); a RETRY or LIST is granted only right after a Coded
 * window's own end, and a CONNECT only right
 * after a scan's own end, each when its whole length fits the budget. While a leak response is
 * pending the valve's claim goes first (its RMLEAK / CLOSE), then SUBMIT, JOIN, RETRY, LIST, and a
 * claim that is due holds them back until it has run (one Wi-Fi pulse may go between two claims);
 * otherwise SUBMIT, JOIN, RETRY, LIST, then the claim.
 *
 * Every scan is timed: a cancel that meets a scan's own timeout can leave
 * NimBLE's state idle while the controller still scans, and every start then fails until that scan
 * ends by itself, which a scan with no duration never would.
 *
 * A scan's own end races a cancel: the controller's LE Scan Timeout can already be queued for the
 * host task when ble_gap_disc_cancel() returns, and ble_gap_disc_complete() then resets whatever
 * GAP procedure NimBLE runs when it is processed. A connect issued in the pass that cancelled would
 * be lost with no CONNECT event (and the valve with it); a scan started then would be reset while
 * the controller scans on, with NimBLE dropping its adverts. So:
 *   - a claim is granted only after a scan's own end (its DISC_COMPLETE, after which no end of that
 *     scan can come) with no scan started or cancelled since (own_end), never after a cancel;
 *   - nothing starts in the pass that cancels: the next start is SCAN_SETTLE_MS later, so a queued
 *     end is delivered first, while no scan runs, and dropped (each scan's callback argument is its
 *     generation, and an end is taken only for the running scan's);
 *   - an end that still comes after the next scan started carries that scan's generation (NimBLE
 *     keeps one callback argument), but comes well before its duration: the executor then takes it
 *     as reset, waits out that scan's time, and grants no claim on it.
 *
 * The GAP handler only stores and notifies (DISC_COMPLETE). The task is woken by it, by the valve
 * module and the radio policy (app_ble_leak_kick()), and at least every EXEC_POLL_MS; it sleeps no
 * longer than the next slot edge or pulse deadline. A start that fails is retried SCAN_RETRY_MS
 * later, from the row's Coded window. Priority 6, so the next scan starts within milliseconds of an
 * edge. Its slow chores (whitelist reload, PHY save, logs, summary) run right after a scan that can
 * absorb them starts (one of 1 s or more, or an AP row's Coded window before its Wi-Fi slot), while
 * the controller scans on, or while nothing is to scan; never in a pulse. They take milliseconds.
 * One that waits longer than its scan runs (the whitelist's provisioning read waits up to 1 s while
 * that mutex is held; an NVS write can meet a flash erase) still starts the next scan late, which
 * widens that one gap; a timed provisioning read would remove it (HANDOFF 15s residual 10).
 * --------------------------------------------------------- */

#define SCAN_GONE_MS        1000    // a scan NimBLE dropped with no DISC_COMPLETE is restarted after this
#define SCAN_SETTLE_MS      50      // nothing starts this soon after a cancel (a queued end is delivered first)
#define SCAN_EARLY_MS       100     // an end this much before a scan's duration is a stopped scan's late end
#define SCAN_OVERDUE_MS     1000    // a scan with no end this long after its duration is stopped and restarted
#define CHORES_LATE_MS      5000    // the executor's slow chores run at least this often
#define CHORES_MIN_SCAN_MS  1000    // ... and otherwise right after a scan this long starts
#define SUMMARY_MS          60000   // the summary line's period
#define CLAIM_OVERRUN_MS    2000    // a claim's connect still in flight this long past its pulse: the valve
                                    // module ends it and resets the host (HANDOFF 15s residual 3)
#define STARVE_MS           250000  // the sensor-starvation guard's fact: a listed sensor unheard this long
#define LISTED_CAP_MS       86400000u   // the facts' "listed for" saturates here (no 32-bit overflow)
#define BLE_LEAK_TASK_PRIO  6       // the executor's priority (plan §4.7)
#define SUMMARY_PRIO        4       // ... and while it prints the summary, below iothub_task's 5 (15t I-5)

// A Coded scan that covered an advert interval (RP_L_MS, I1 and I2), in whole ticks: rounded up,
// plus one for the tick a start stamp falls in (a scan stamped late in its tick has run up to one
// tick less than its stamps say). pdMS_TO_TICKS() truncates: 548 ms gave 54 ticks, and a scan cut
// for a pulse after about 531 ms counted as a covering window.
#define L_TICKS             (pdMS_TO_TICKS(RP_L_MS + portTICK_PERIOD_MS - 1) + 1)

// A slot's scan geometry, by kind (radio_policy.h): interval and window in 0.625 ms units (both 0:
// that PHY is not scanned); its duration is the slot's. Every scan is passive with
// filter_duplicates off (scan_start()).
typedef struct {
    uint16_t itvl_1m, win_1m;
    uint16_t itvl_c, win_c;
} scan_geo_t;

static const scan_geo_t k_kind_geo[RP_K_W + 1] = {
    // Coded at full duty: AP rows' and N_MIXED's Coded windows, NORMAL_LR's, the recovery.
    [RP_K_C] = { 0, 0, 160, 160 },
    // 1M at full duty: the valve, 1M sensors, discovery.
    [RP_K_M] = { 160, 160, 0, 0 },
    // N_CODED: 20 ms of 1M and 80 ms of Coded in every 100 ms.
    [RP_K_N] = { 160, 32, 160, 128 },
};

// A Coded window: the blind budget (I2) counts from its end.
static bool kind_coded(uint8_t k)
{
    return k == RP_K_C || k == RP_K_N;
}

static uint32_t ticks_ms(TickType_t t)
{
    return (uint32_t)t * portTICK_PERIOD_MS;
}

// The executor's own state, on its stack: only its task reads or writes it.
typedef struct {
    bool scan_on;               // our scan runs (started, no DISC_COMPLETE or cancel since)
    bool slot_done;             // the current slot ran to its end: the row's next is due
    bool own_end;               // the last scan ended by itself, none started or cancelled since
    bool coded_last;            // the slot that ended last was a Coded window
    bool recovery;              // a pulse ended: RECOVERY is due, or runs
    bool just_started;          // this pass started a scan the chores may follow
    bool idle;                  // nothing is to scan (BLE_IDLE, not synced): chores any time
    bool want;                  // the accounting: a scan slot is due or runs
    bool overrun_asked;         // the running claim's overrun went to the valve module
    bool lead;                  // a Wi-Fi slot runs before the row's Coded window (I8, row_begin())
    uint8_t pulse;              // the pulse running (rp_pulse_t)
    uint8_t row;                // the row running (RP_ROW_NONE: none)
    uint8_t slot;               // ... its slot ...
    uint8_t left;               // ... and the slots left in its period
    uint8_t kind;               // the running (or last) slot's kind
    uint8_t announced;          // the scanning row last announced (RP_ROW_NONE: none since a pause)
    uint8_t gen;                // the running (or last) scan's generation: its callback argument
    uint8_t gen_next;           // ... the last one handed out (every start attempt takes one)
    uint16_t slot_ms;           // the running (or last) slot's length
    uint16_t start_fails;       // scan starts failed in a row
    TickType_t retry_at;        // no scan start before this tick
    TickType_t started_at;      // when the running (or last) slot started
    TickType_t ended_at;        // when the last slot ended (a scan's end stamp, or a Wi-Fi slot's end)
    TickType_t w_end_at;        // the running Wi-Fi slot's end
    TickType_t coded_end_at;    // when the last Coded window ended (0: none yet)
    TickType_t gap_from;        // the I2 monitor: the Coded end it times from (0: a pause since)
    TickType_t gone_at;         // our scan was first seen gone without its DISC_COMPLETE (0: no)
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
 * Start a scan of slot kind `kind` for `ms` (executor task only): 0, or the NimBLE error. `gen` is
 * the scan's generation, its GAP callback argument (see the executor's notes).
 *
 * filter_duplicates is DISABLED, and that is a safety fix, not a latency one. The controller's
 * duplicate cache keys on ADDRESS ONLY and never refreshes (CONFIG_BT_CTRL_SCAN_DUPL_TYPE=0,
 * CONFIG_BT_CTRL_DUPL_SCAN_CACHE_REFRESH_PERIOD=0), so with the filter on, a leak sensor whose
 * payload changes from dry to WET is suppressed: the address has already been seen. Until 2.1.x
 * the valve hunt ran with the filter on, and the hub was deaf to its leak sensors for as long as
 * it hunted a dead valve. The cost of turning it off: duplicate valve reports, which the valve
 * module's claim guard absorbs (ble_valve_note_adv()).
 * --------------------------------------------------------- */
static int scan_start(uint8_t kind, uint16_t ms, uint8_t gen)
{
    const scan_geo_t *g = &k_kind_geo[kind];
    uint16_t dur = (uint16_t)(ms / 10);   // 10 ms units; every row's slot is timed
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
        dur,                            // duration, 10 ms units
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
    return ble_gap_disc(BLE_OWN_ADDR_PUBLIC, (int32_t)dur * 10,
                        &disc_params, ble_leak_gap_event, (void *)(uintptr_t)gen);
#endif
}

// Stops our scan before its end (executor task only): the row then starts again at its Coded
// window, and nothing starts before SCAN_SETTLE_MS (its own end may already be queued, see the
// executor's notes). A Coded scan that covered an advert interval counts as a Coded window ending
// now (the blind budget, I2); one that did not counts for nothing, and the I2 monitor times the
// blind span from the last one that did. A cancel the controller refuses leaves it marked running,
// and the next pass tries again.
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
    if (kind_coded(x->kind)) {
        if ((now - x->started_at) >= L_TICKS) {
            x->coded_end_at = now ? now : 1;
        }
        x->gap_from = x->coded_end_at;
    }
    x->scan_on = false;
    x->slot_done = false;
    x->coded_last = false;
    x->own_end = false;
    x->row = RP_ROW_NONE;
    x->ended_at = now;
    x->retry_at = now + pdMS_TO_TICKS(SCAN_SETTLE_MS);
}

// Our scan ended (executor task only): its DISC_COMPLETE, delivered at `at` (the GAP handler's
// stamp), or (lost) NimBLE dropped it with none. Its own end makes the row's next slot due (after
// the dither in NORMAL's rows), and lets a claim follow (own_end).
// Three ends are no scan's own: a lost one, one with a reason (a host reset), and one that comes
// well before the scan's duration, which is a stopped scan's late end that reset this one (see the
// executor's notes). They grant no claim and restart the row at its Coded window, as after a stop;
// after the third the controller scans on to this scan's end, so nothing starts before.
static void scan_ended(exec_t *x, TickType_t now, TickType_t at, bool lost)
{
    x->scan_on = false;
    x->gone_at = 0;
    uint32_t dur_ms = x->slot_ms;
    TickType_t ran = at - x->started_at;
    bool early = !lost && s_scan_end_reason == 0 &&
                 ran + pdMS_TO_TICKS(SCAN_EARLY_MS) < pdMS_TO_TICKS(dur_ms);
    if (lost || early || s_scan_end_reason != 0) {
        x->slot_done = false;
        x->coded_last = false;
        x->own_end = false;
        x->row = RP_ROW_NONE;
        x->ended_at = now;
        x->retry_at = now + pdMS_TO_TICKS(SCAN_SETTLE_MS);
        if (early) {
            ESP_LOGW(BLE_LEAK_TAG, "Scan ended after %lu of its %lu ms: a stopped scan's late end - the next starts after this one's time",
                     (unsigned long)ticks_ms(ran), (unsigned long)dur_ms);
            x->retry_at = x->started_at + pdMS_TO_TICKS(dur_ms + SCAN_SETTLE_MS);
        }
        return;
    }
    x->slot_done = true;
    x->own_end = true;
    x->ended_at = at;
    x->coded_last = kind_coded(x->kind);
    if (x->coded_last) {
        x->coded_end_at = at ? at : 1;
        x->gap_from = x->coded_end_at;
    }
    if (x->row == RP_ROW_RECOVERY) {
        x->recovery = false;
    }
    bool dither = x->row < RP_ROW_COUNT && (radio_policy_row(x->row)->flags & RP_F_DITHER);
    x->retry_at = at + (dither ? pdMS_TO_TICKS(esp_random() % (RP_JITTER_MS + 1)) : 0);
}

// The lines for a scanning row, when its first scan starts. The leak scan's start line is the one it
// always printed (after a pause, at boot); NORMAL's rows print their own once per change, and its
// backed-off valve hunt (N_CODED and N_HUNT in turn) one line for both. The transient RECOVERY
// prints nothing; the AP modes' rows print nothing either (the radio policy prints each mode, and
// their discovery rows alternate every period).
#define ANNOUNCE_HUNT_BO 0xFE   // x->announced: NORMAL's backed-off valve hunt (no row has this id)
_Static_assert(RP_ROW_COUNT < ANNOUNCE_HUNT_BO, "the backed-off hunt's announce key is no row's");
static void exec_announce(exec_t *x)
{
    uint8_t row = x->row;
    if (row == RP_ROW_RECOVERY) {
        return;
    }
    uint8_t key = row;
    if ((row == RP_ROW_N_CODED || row == RP_ROW_N_HUNT) && radio_policy_exec_hunt_backed_off()) {
        key = ANNOUNCE_HUNT_BO;
    }
    if (key == x->announced) {
        return;
    }
    if (x->announced == RP_ROW_NONE) {
        ESP_LOGI(BLE_LEAK_TAG, "Extended passive scan started (1M + Coded PHY)");
    }
    if (key == ANNOUNCE_HUNT_BO) {
        unsigned m = radio_policy_row(RP_ROW_N_HUNT)->ms[1];
        ESP_LOGI(BLE_LEAK_TAG, "Scan mode N_CODED with a valve hunt, backed off (valve unlinked 10 min or more): 1 s of 1M 20 %% + Coded 80 %%, and every %d periods %u.%02u s more on 1M, each next after 0-%d ms",
                 RP_HUNT_EVERY_BO, m / 1000, (m % 1000) / 10, RP_JITTER_MS);
    } else if (row == RP_ROW_N_CODED) {
        ESP_LOGI(BLE_LEAK_TAG, "Scan mode N_CODED: 1M 20 %% + Coded 80 %%, 1 s scans, each next after 0-%d ms",
                 RP_JITTER_MS);
    } else if (row == RP_ROW_N_HUNT) {
        unsigned m = radio_policy_row(RP_ROW_N_HUNT)->ms[1];
        ESP_LOGI(BLE_LEAK_TAG, "Scan mode N_CODED with a valve hunt (valve not linked): 1 s of 1M 20 %% + Coded 80 %%, then %u.%02u s on 1M, each next after 0-%d ms",
                 m / 1000, (m % 1000) / 10, RP_JITTER_MS);
    } else if (row == RP_ROW_N_MIXED) {
        ESP_LOGI(BLE_LEAK_TAG, "Scan mode N_MIXED (a sensor on 1M, or a sensor's PHY not known yet): 1 s on 1M and 1 s on Coded in turn, each next after 0-%d ms",
                 RP_JITTER_MS);
    } else if (row == RP_ROW_NORMAL_LR) {
        ESP_LOGI(BLE_LEAK_TAG, "Scan mode NORMAL_LR (leak response, valve not linked): 1 s on 1M and 0.6 s on Coded in turn, each next after 0-%d ms",
                 RP_JITTER_MS);
    }
    x->announced = key;
}

// I2's budget left now: RP_BLIND_MAX_MS from the end of the last Coded window. A Coded scan running
// that covered an advert interval counts as a window ending now (a pulse stops it). 0: none.
static uint32_t coded_budget(const exec_t *x, TickType_t now)
{
    TickType_t ref = x->coded_end_at;
    if (x->scan_on && kind_coded(x->kind) && (now - x->started_at) >= L_TICKS) {
        ref = now;
    }
    if (ref == 0) {
        return 0;
    }
    uint32_t since = ticks_ms(now - ref);
    return since >= RP_BLIND_MAX_MS ? 0 : RP_BLIND_MAX_MS - since;
}

// The facts for the radio policy (every pass, I6): the listed sensors' PHYs, the starvation guard's
// fact (from the chores), the valve.
static void exec_facts(rp_ble_facts_t *f, bool synced, TickType_t now)
{
    memset(f, 0, sizeof(*f));
    f->synced = synced;
    uint8_t n = 0;
    taskENTER_CRITICAL(&s_wl_lock);
    n = s_whitelist_count;
    for (int i = 0; i < n; i++) {
        uint8_t p = PHY_OF(s_wl_phy[i]);
        if (p == PHY_1M) {
            f->any_1m = true;
        } else if (p == PHY_UNKNOWN) {
            f->any_unknown = true;
        }
    }
    taskEXIT_CRITICAL(&s_wl_lock);
    f->sensors = n;
    TickType_t listed = (s_listed_at != 0) ? now - s_listed_at : 0;
    if (listed > pdMS_TO_TICKS(LISTED_CAP_MS)) {
        listed = pdMS_TO_TICKS(LISTED_CAP_MS);
    }
    f->listed_ms = ticks_ms(listed);
    f->starved = s_starved;
    f->valve = ble_valve_has_target_mac();
    // BLE_IDLE needs the link verified by ble_gap_conn_find() (plan §4.2); with sensors listed the
    // handle is enough (the valve module closes a stale one within a second, link_poll()).
    f->valve_linked = (n == 0) ? ble_valve_link_verified() : ble_valve_is_connected();
    f->valve_hunt = ble_valve_hunt_wanted();
    f->valve_slot = f->valve_hunt && ble_valve_hunt_slot_wanted();
}

// Starts a row (a switch, a restart, or the first): at its Coded window, or right after it when
// the slot that just ended was a Coded window (I1). In a row that holds I8, a 1M slot that just
// ended would run straight into that Coded window: a switch after a discovery row's, AP_K1M's or
// LR_AP's M slot (the page back in use, B3's end, the guard, the LR overlay's end) gave BLE runs
// of 0.9-1.2 s while a phone may wait for its reply. A Wi-Fi slot of RP_I8_WIFI_MIN_MS then leads
// the row (x->lead), when the Coded gap keeps I1 with it; I1 comes first.
static void row_begin(exec_t *x, uint8_t row, TickType_t now)
{
    const rp_row_t *r = radio_policy_row(row);
    bool just = (now - x->ended_at) <= pdMS_TO_TICKS(RP_JITTER_MS) + 1;
    bool fresh = x->coded_last && just;
    x->row = row;
    x->slot = fresh ? (uint8_t)((r->coded + 1) % r->n) : r->coded;
    x->left = r->n;
    x->lead = !fresh && just && x->kind == RP_K_M && (r->flags & RP_F_I8) && x->coded_end_at != 0 &&
              ticks_ms(now - x->coded_end_at) + RP_I8_WIFI_MIN_MS + RP_JITTER_MS <= RP_GAP_MAX_MS;
}

// Starts the current slot of the current row: a scan, or a Wi-Fi slot, timed from the end of the
// slot before it, however late this pass is: the lateness is Wi-Fi time already, so it shortens
// the slot (one whose end has passed is over on the next pass) and never widens the Coded gap
// after it. A scan that fails to start restarts the row at its Coded window SCAN_RETRY_MS later,
// so a geometry the controller refuses never stalls the row's Coded windows.
static void slot_start(exec_t *x, TickType_t now)
{
    const rp_row_t *r = radio_policy_row(x->row);
    uint8_t k = x->lead ? RP_K_W : r->kind[x->slot];
    uint16_t ms = x->lead ? RP_I8_WIFI_MIN_MS : r->ms[x->slot];
    x->slot_done = false;
    x->kind = k;
    x->slot_ms = ms;
    if (k == RP_K_W) {
        TickType_t from = (x->ended_at != 0) ? x->ended_at : now;
        x->started_at = from;
        x->w_end_at = from + pdMS_TO_TICKS(ms);
        x->coded_last = false;
        return;
    }
    uint8_t gen = ++x->gen_next;
    int rc = scan_start(k, ms, gen);
    if (rc == 0) {
        if (kind_coded(k) && x->gap_from != 0) {
            radio_policy_exec_gap(ticks_ms(now - x->gap_from));   // the I2 monitor
            x->gap_from = 0;
        }
        x->scan_on = true;
        x->gen = gen;
        x->started_at = now;
        x->own_end = false;
        x->just_started = (ms >= CHORES_MIN_SCAN_MS) ||
                          (k == RP_K_C && r->kind[(x->slot + 1) % r->n] == RP_K_W);
        if (x->start_fails > 0) {
            ESP_LOGI(BLE_LEAK_TAG, "Scan started after %u failed attempt(s)", (unsigned)x->start_fails);
            x->start_fails = 0;
        }
        exec_announce(x);
        return;
    }
    // BLE_HS_EALREADY: a scan runs that this task does not count as its own (it is the only owner,
    // so its count is wrong): stop it and start ours.
    if (rc == BLE_HS_EALREADY) {
        (void)ble_gap_disc_cancel();
    }
    if (x->start_fails % SCAN_FAIL_LOG_EVERY == 0) {
        ESP_LOGW(BLE_LEAK_TAG, "Failed to start ext scan: %d, will retry", rc);
    }
    if (x->start_fails < UINT16_MAX) {
        x->start_fails++;
    }
    x->row = RP_ROW_NONE;
    x->own_end = false;
    x->retry_at = now + pdMS_TO_TICKS(SCAN_RETRY_MS);
}

// The next slot (no slot runs, none is pending a start): the row's next in turn, the next period's
// row (the mode's sequence advances), or the wanted row from its Coded window.
static void exec_next_slot(exec_t *x, TickType_t now, uint8_t want_row)
{
    if (x->row == want_row && x->slot_done) {
        const rp_row_t *r = radio_policy_row(x->row);
        if (x->lead) {
            x->lead = false;   // the lead Wi-Fi slot is over: the row's Coded window (row_begin())
        } else if (x->left <= 1) {
            uint8_t next = (want_row == RP_ROW_RECOVERY) ? want_row : radio_policy_exec_row(now, true);
            if (next != x->row) {
                row_begin(x, next, now);
            } else {
                x->slot = (uint8_t)((x->slot + 1) % r->n);
                x->left = r->n;
            }
        } else {
            x->slot = (uint8_t)((x->slot + 1) % r->n);
            x->left--;
        }
    } else {
        row_begin(x, want_row, now);
    }
    slot_start(x, now);
}

// A valve claim, when the valve module wants one and the radio policy allows it now (executor task):
// right after a scan's own end with no scan running (see the race notes), for a length that fits
// the blind budget. True when the connect is in flight.
static bool exec_claim(exec_t *x, TickType_t now, bool boundary, uint32_t budget)
{
    if (!boundary || !x->own_end || !ble_valve_claim_wanted()) {
        return false;
    }
    uint32_t len = radio_policy_exec_connect_len(now, budget);
    if (len == 0) {
        return false;
    }
    ble_valve_claim_start(len);
    if (!ble_gap_conn_active()) {
        return false;   // refused or not started: the valve module asked for the hunt again
    }
    radio_policy_exec_pulse_begin(RP_PULSE_CONNECT, now, len);
    x->pulse = RP_PULSE_CONNECT;
    x->overrun_asked = false;
    x->slot_done = false;
    x->coded_last = false;
    x->own_end = false;
    x->row = RP_ROW_NONE;
    return true;
}

// A Wi-Fi pulse the radio policy grants now (any of `kinds`): the running scan stops at once.
static bool exec_wifi_pulse(exec_t *x, TickType_t now, uint32_t kinds, bool at_coded_end, uint32_t budget,
                            bool claim_due)
{
    bool young = x->scan_on && kind_coded(x->kind) && (now - x->started_at) < L_TICKS;
    uint32_t len = 0;
    rp_pulse_t k = radio_policy_exec_wifi_grant(now, kinds, at_coded_end, budget, young, claim_due, &len);
    if (k == RP_PULSE_NONE) {
        return false;
    }
    if (x->scan_on) {
        scan_stop(x, now);
        if (x->scan_on) {
            // The controller refused the cancel: BLE stays on. A RETRY or LIST is refused (its
            // requester may have read ON already), a JOIN or SUBMIT waits again, and the next pass
            // tries the cancel again.
            radio_policy_exec_pulse_retract(k);
            return false;
        }
    }
    radio_policy_exec_pulse_begin(k, now, len);
    x->pulse = (uint8_t)k;
    x->slot_done = false;
    x->coded_last = false;
    x->row = RP_ROW_NONE;
    return true;
}

/* ---------------------------------------------------------
 * One pass of the executor: every fact is read again (I6), so a missed edge costs at most one
 * pass. Returns how long it may sleep before the next one.
 * --------------------------------------------------------- */
static TickType_t executor_pass(exec_t *x)
{
    TickType_t now = xTaskGetTickCount();
    radio_policy_exec_account(now, x->row != RP_ROW_NONE && x->pulse == RP_PULSE_NONE, x->want, x->scan_on,
                              (rp_pulse_t)x->pulse, x->row == RP_ROW_RECOVERY);
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
    // DISC_COMPLETE (a lost LE Scan Timeout). Every scan ends by its controller timeout, tens of
    // thousands a day; without this a lost one would leave the hub deaf to every BLE sensor with
    // NimBLE's state, and the duty figures (which count this task's view), saying it scans.
    if (x->scan_on && (now - x->started_at) >= pdMS_TO_TICKS((uint32_t)x->slot_ms + SCAN_OVERDUE_MS)) {
        ESP_LOGW(BLE_LEAK_TAG, "Scan overdue: no end %d ms after its %u ms - stopping and restarting it",
                 SCAN_OVERDUE_MS, (unsigned)x->slot_ms);
        (void)ble_gap_disc_cancel();
        scan_ended(x, now, now, true);
    }
    // A Wi-Fi slot's time is up.
    if (!x->scan_on && x->row != RP_ROW_NONE && x->kind == RP_K_W && !x->slot_done &&
        (int32_t)(now - x->w_end_at) >= 0) {
        x->slot_done = true;
        x->coded_last = false;
        x->ended_at = x->w_end_at;
    }

    bool synced = ble_hs_synced();
    bool conn = ble_gap_conn_active();

    // The facts and the mode (I6), before a pulse's end: the recovery after it is decided on the row
    // the mode runs now, not on the last pass's (a mode that changed in between would otherwise
    // resume with no recovery).
    rp_ble_facts_t f;
    exec_facts(&f, synced, now);
    (void)radio_policy_exec_mode(&f, now);

    // The running pulse's end. A claim's ends at its CONNECT event; one that NimBLE has not ended
    // CLAIM_OVERRUN_MS past its pulse goes to the valve module (HANDOFF 15s residual 3).
    if (x->pulse != RP_PULSE_NONE) {
        bool over;
        if (x->pulse == RP_PULSE_CONNECT) {
            over = !conn;
            TickType_t dl = radio_policy_exec_pulse_deadline();
            if (!over && !x->overrun_asked && dl != 0 &&
                (int32_t)(now - dl) >= (int32_t)pdMS_TO_TICKS(CLAIM_OVERRUN_MS)) {
                x->overrun_asked = true;
                ble_valve_claim_overrun();
            }
        } else {
            over = radio_policy_exec_pulse_over(now);
        }
        if (over) {
            uint32_t blind = x->coded_end_at ? ticks_ms(now - x->coded_end_at) : UINT32_MAX;
            radio_policy_exec_pulse_end(now);
            x->pulse = RP_PULSE_NONE;
            x->overrun_asked = false;
            x->slot_done = false;
            x->coded_last = false;
            x->row = RP_ROW_NONE;
            // I2: every blind span longer than the row's own Coded gap is followed by the recovery.
            uint8_t resume = radio_policy_exec_row(now, false);
            if (synced && resume != RP_ROW_NONE &&
                blind > radio_policy_row_gap_ms(resume) + RP_JITTER_MS) {
                x->recovery = true;
            }
            if ((int32_t)(x->retry_at - now) < 0) {
                x->retry_at = now;
            }
        }
    } else if (conn) {
        // A connect this task did not grant (NimBLE's own re-attempt, which sdkconfig turns off, I5):
        // no scan runs beside it; it counts as a pulse, with the recovery after it.
        radio_policy_exec_pulse_begin(RP_PULSE_CONNECT, now, RP_CONNECT_LR_MS);
        x->pulse = RP_PULSE_CONNECT;
        x->overrun_asked = false;
    }

    // Grants, between pulses, after their recovery. While a leak response is pending the valve's
    // claim goes first (its RMLEAK / CLOSE outranks a Connect: a stranger's Connects on the open
    // SoftAP must not hold it off), then SUBMIT, JOIN, RETRY and LIST, and a due claim that cannot
    // go yet (its boundary, its pulse spacing, I2b's room) holds them back unless the last pulse
    // was a claim; otherwise SUBMIT, JOIN, RETRY, LIST, then the claim. With no BLE scan to pause
    // (BLE_IDLE, or not synced: PAUSED) the radio policy answers Wi-Fi requests FREE, so a
    // requester never waits out its 2 s for nothing; no claim then (radio_policy_exec_connect_len()
    // answers 0).
    if (x->pulse == RP_PULSE_NONE && !x->recovery) {
        bool boundary = !x->scan_on && (x->row == RP_ROW_NONE || x->slot_done);
        bool at_coded_end = boundary && x->slot_done && x->coded_last &&
                            (now - x->ended_at) <= pdMS_TO_TICKS(RP_JITTER_MS) + 1;
        uint32_t budget = coded_budget(x, now);
        bool lr = radio_policy_lr_pending();
        bool done = lr && exec_claim(x, now, boundary, budget);
        if (!done) {
            bool claim_due = lr && ble_valve_claim_wanted();
            done = exec_wifi_pulse(x, now, (1u << RP_PULSE_SUBMIT) | (1u << RP_PULSE_JOIN) |
                                   (1u << RP_PULSE_RETRY) | (1u << RP_PULSE_LIST), at_coded_end, budget,
                                   claim_due);
        }
        if (!done && !lr) {
            (void)exec_claim(x, now, boundary, budget);
        }
    }

    // What runs now: nothing (a pulse, not synced, BLE_IDLE), the recovery, or the mode's row. A
    // scan stops now only for a pulse or when NimBLE lost its sync; rows otherwise change at a slot
    // boundary (a scan of a row nothing wants any more, BLE_IDLE's, runs to its end).
    uint8_t want_row;
    if (x->pulse != RP_PULSE_NONE || !synced) {
        want_row = RP_ROW_NONE;
        if (x->scan_on) {
            scan_stop(x, now);
        }
    } else {
        want_row = x->recovery ? RP_ROW_RECOVERY : radio_policy_exec_row(now, false);
    }
    x->idle = (want_row == RP_ROW_NONE && x->pulse == RP_PULSE_NONE);
    if (x->idle) {
        x->gap_from = 0;
        if (!x->scan_on && x->row != RP_ROW_NONE) {
            x->row = RP_ROW_NONE;   // a Wi-Fi slot or a finished scan: the next row starts afresh
        }
        if (!synced || x->row == RP_ROW_NONE) {
            x->announced = RP_ROW_NONE;   // scanning resumes with its start line
        }
    }
    bool active = x->scan_on || (x->row != RP_ROW_NONE && x->kind == RP_K_W && !x->slot_done);
    if (!active && x->pulse == RP_PULSE_NONE && want_row != RP_ROW_NONE &&
        (int32_t)(now - x->retry_at) >= 0) {
        exec_next_slot(x, now, want_row);
    }
    x->want = (x->pulse == RP_PULSE_NONE && want_row != RP_ROW_NONE &&
               !(x->row != RP_ROW_NONE && x->kind == RP_K_W && !x->slot_done));

    // The next wake: the pulse's deadline (I3: the executor expires every pause itself) or a join
    // assist's own end, the Wi-Fi slot's end, the next start, a young Coded scan covering an advert
    // interval (a pending JOIN or SUBMIT waits for it), a lost scan's confirmation; at most
    // EXEC_POLL_MS.
    TickType_t wait = pdMS_TO_TICKS(EXEC_POLL_MS);
    TickType_t until = 0;
    bool timed = false;
    if (x->pulse != RP_PULSE_NONE) {
        until = radio_policy_exec_pulse_wake();
        timed = (radio_policy_exec_pulse_deadline() != 0);
        if (x->pulse == RP_PULSE_CONNECT) {
            if (!x->overrun_asked) {
                until += pdMS_TO_TICKS(CLAIM_OVERRUN_MS);
            } else {
                timed = false;   // the host reset is asked: every EXEC_POLL_MS until the connect is
                                 // gone, not every tick against a deadline long past
            }
        }
    } else if (!x->scan_on && x->row != RP_ROW_NONE && x->kind == RP_K_W && !x->slot_done) {
        until = x->w_end_at;
        timed = true;
    } else if (!x->scan_on && want_row != RP_ROW_NONE) {
        until = x->retry_at;
        timed = true;
    } else if (x->scan_on && kind_coded(x->kind) && (now - x->started_at) < L_TICKS) {
        until = x->started_at + L_TICKS;
        timed = true;
    }
    if (timed) {
        int32_t left = (int32_t)(until - now);
        if (left < 1) {
            left = 1;
        }
        if ((TickType_t)left < wait) {
            wait = (TickType_t)left;
        }
    }
    if (x->gone_at != 0 && pdMS_TO_TICKS(SCAN_GONE_MS) < wait) {
        wait = pdMS_TO_TICKS(SCAN_GONE_MS);
    }
    return wait;
}

// The 60 s summary (plan §4.7): the radio policy prints it, with the adverts heard per sensor since
// the last one. In a frame of its own: its line buffer is off the loop's frame.
static __attribute__((noinline)) void exec_summary(void)
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
    // Printed below iothub_task's priority (15t I-5); this task holds no mutex here, so its base
    // priority comes back as it was.
    vTaskPrioritySet(NULL, SUMMARY_PRIO);
    radio_policy_exec_summary(k ? adv : "none heard yet", flips);
    vTaskPrioritySet(NULL, BLE_LEAK_TASK_PRIO);
}

// The sensor-starvation guard's fact (plan §4.2), from the chores: a listed sensor unheard for
// STARVE_MS or more (never heard: since it was listed), and whether one of them is on 1M or has its
// PHY unknown (the guard then runs AP_K1M, not the Coded-dense AP_IDLE).
static void starve_scan(TickType_t now)
{
    uint8_t kind = RP_STARVED_NONE;
    TickType_t listed = s_listed_at;
    taskENTER_CRITICAL(&s_wl_lock);
    for (int i = 0; i < s_whitelist_count; i++) {
        int slot = sensor_find_locked(s_whitelist[i]);
        TickType_t heard = (slot >= 0) ? s_sensors[slot].heard_at : 0;
        TickType_t from = heard ? heard : listed;
        if (from != 0 && (now - from) >= pdMS_TO_TICKS(STARVE_MS)) {
            uint8_t k = (PHY_OF(s_wl_phy[i]) == PHY_CODED) ? RP_STARVED_CODED : RP_STARVED_K1M;
            if (k > kind) {
                kind = k;
            }
        }
    }
    taskEXIT_CRITICAL(&s_wl_lock);
    s_starved = kind;
}

// The scan-alive heartbeat line, as it always was (no pause to report since 2.1.4 WP8). In a frame
// of its own, so the task's loop frame does not carry the log call.
static __attribute__((noinline)) void heartbeat_log(void)
{
    ESP_LOGI(BLE_LEAK_TAG, "[HEARTBEAT] Scanner alive, whitelist=%d sensors", s_whitelist_count);
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

    // The radio policy's boot self-test (plan §4.8), before its first decision.
    radio_policy_init();

    // Load whitelist (and, the first time, each sensor's PHY from NVS)
    reload_whitelist();
    // Under the lock: the GAP handler can already be feeding process_leak_adv() now that the
    // whitelist is populated.
    taskENTER_CRITICAL(&s_wl_lock);
    memset(s_sensors, 0, sizeof(s_sensors));
    memset(s_adv_n, 0, sizeof(s_adv_n));
    taskEXIT_CRITICAL(&s_wl_lock);

    // The first scan starts on the first pass, once NimBLE is synced: no delay.
    exec_t x;
    memset(&x, 0, sizeof(x));
    TickType_t t0 = xTaskGetTickCount();
    TickType_t last_whitelist_reload = t0;
    TickType_t last_heartbeat_log = t0;
    TickType_t last_summary = t0;
    TickType_t phy_saved_at = 0;
    TickType_t last_chores = t0;
    x.retry_at = t0;
    x.row = RP_ROW_NONE;
    x.announced = RP_ROW_NONE;

    for (;;) {
        TickType_t wait = executor_pass(&x);

        // The slow chores: right after a scan that can absorb them starts (the controller scans on
        // meanwhile; see the executor's notes for a chore that outlasts it), while nothing is to
        // scan, or once they are CHORES_LATE_MS overdue (scan starts that keep failing). Never in a
        // pulse (HANDOFF 15s residual 4) or between two slots.
        TickType_t tnow = xTaskGetTickCount();
        if (x.just_started || x.idle ||
            (x.pulse == RP_PULSE_NONE && (tnow - last_chores) >= pdMS_TO_TICKS(CHORES_LATE_MS))) {
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

            // The burst log: a line for each sensor's burst that is over.
            burst_log();

            // The starvation guard's fact.
            starve_scan(now);

            // Periodic scan-alive heartbeat (every 60s)
            if ((now - last_heartbeat_log) >= pdMS_TO_TICKS(60000)) {
                heartbeat_log();
                last_heartbeat_log = now;
            }

            // The 60 s summary and the duty watchdog (the radio policy's).
            if ((now - last_summary) >= pdMS_TO_TICKS(SUMMARY_MS)) {
                radio_policy_exec_account(now, x.row != RP_ROW_NONE && x.pulse == RP_PULSE_NONE, x.want,
                                          x.scan_on, (rp_pulse_t)x.pulse, x.row == RP_ROW_RECOVERY);
                exec_summary();
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
    xTaskCreate(ble_leak_scan_task, "ble_leak_scan", 3072, NULL, BLE_LEAK_TASK_PRIO, &ble_leak_task_handle);
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
