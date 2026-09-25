#ifndef HEALTH_ENGINE_H
#define HEALTH_ENGINE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

#define HEALTH_LORA_TIMEOUT_MS       (10 * 60 * 1000)   // 10 min
#define HEALTH_BLE_LEAK_TIMEOUT_MS   (10 * 60 * 1000)   // 10 min
#define HEALTH_TICK_INTERVAL_MS      (30 * 1000)         // 30s evaluation cycle
#define HEALTH_ALERT_DEBOUNCE_MS     (60 * 1000)         // 60s min between alerts per device
#define HEALTH_BATTERY_WARN_PCT      20
#define HEALTH_BATTERY_GOOD_PCT      35
/* The VALVE's battery bands: its own FW 2.2.0 thresholds. At <=10 % the valve auto-closes
 * and refuses to open, so the hub rates it CRITICAL ("Valve battery critical"); 11-20 % is
 * the valve's Low band (WARNING). VALVE ONLY - the sensor bands above are unchanged by
 * product decision: a low sensor battery never closes the valve, so it never reads
 * CRITICAL. */
#define HEALTH_VALVE_BATTERY_CRIT_PCT 10
#define HEALTH_VALVE_BATTERY_WARN_PCT 20
#define HEALTH_RSSI_WARN_DBM         (-90)
#define HEALTH_RSSI_GOOD_DBM         (-80)
#define HEALTH_VALVE_DISC_TIMEOUT_MS (3 * 60 * 1000)      // 3-min grace before CRITICAL
#define HEALTH_BOOT_SYNC_TIMEOUT_MS  (3 * 60 * 1000)     // 3-min boot window for sensor check-ins.
                                                          // Was 2 min, which was SHORTER than the interval
                                                          // at which check-ins actually reached this engine
                                                          // (the BLE scanner gated them behind its own 5-min
                                                          // telemetry heartbeat), so "Boot sync: timeout" was
                                                          // guaranteed on every boot and the roll-up latched
                                                          // CRITICAL on a healthy hub. The gate is now removed
                                                          // (app_ble_leak.c posts every burst, ~100 s), and
                                                          // this covers one burst cycle with margin.
/* How long a device that has NEVER been heard stays EXCLUDED from the system roll-up.
 *
 * DELIBERATELY DECOUPLED from HEALTH_BOOT_SYNC_TIMEOUT_MS above. That constant gates
 * the boot SNAPSHOT and wants to be short (report promptly); this one gates the
 * roll-up EXCLUSION and wants to be long (don't cry wolf). One constant serving both
 * is what produced the 2026-09-17 bench capture: sensors first heard at 56 / 90 / 199 /
 * 265.6 s, so the 180 s window closed early and the hub spent 81 s RED and published
 * "1 sensor offline" with all four sensors perfectly healthy. Raising the shared
 * constant to 240 s would not have covered that boot either.
 *
 * Aligned with HEALTH_BLE_LEAK_TIMEOUT_MS rather than picked: 600 s is already the
 * deadline at which this engine is willing to declare a PREVIOUSLY-SEEN sensor
 * offline, so it is the principled point at which "haven't heard yet" becomes
 * "offline". A genuinely absent device is still escalated — at the same deadline
 * steady-state uses, not a shorter guess. Nothing is hidden meanwhile: the snapshot
 * reports connected:false / last_seen_age_s:null for it from the first boot snapshot
 * onward, and a LEAKING device is never excluded at all. */
#define HEALTH_ROLLUP_UNHEARD_MS     HEALTH_BLE_LEAK_TIMEOUT_MS   // 600 s
#define HEALTH_COMMISSION_SYNC_TIMEOUT_MS (150 * 1000)    // 2.5-min window after a provision/commission.
                                                          // ~1.5x the dry sensor's ~100 s burst cadence:
                                                          // long enough to catch an in-range sensor in a
                                                          // single clean snapshot, short enough to report a
                                                          // genuinely-absent device promptly. Late/far
                                                          // sensors are still picked up by the incremental
                                                          // refresh snapshot (app_iothub.c), so the window
                                                          // need not cover the worst case.
#define HEALTH_MAX_DEVICES           33                   // 1 valve + 16 LoRa + 16 BLE leak

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

typedef enum {
    HEALTH_EXCELLENT = 0,
    HEALTH_GOOD,
    HEALTH_WARNING,
    HEALTH_CRITICAL
} health_rating_t;

typedef enum {
    HEALTH_DEV_VALVE = 0,
    HEALTH_DEV_LORA,
    HEALTH_DEV_BLE_LEAK
} health_dev_type_t;

/* WHY a device sits at its current rating. Set together with the rating by the engine, so
 * consumers (the snapshot reason string, the alert model) read the cause instead of
 * re-deriving it from connected/battery - a re-derivation that could not tell a
 * battery-critical valve from an offline one. NONE only accompanies EXCELLENT. */
typedef enum {
    HEALTH_CAUSE_NONE = 0,
    HEALTH_CAUSE_LEAK,
    HEALTH_CAUSE_LINK,       // offline / never heard / valve disconnect grace
    HEALTH_CAUSE_BATTERY,
    HEALTH_CAUSE_SIGNAL
} health_cause_t;

typedef enum {
    HEALTH_EVT_LORA_CHECKIN = 0,
    HEALTH_EVT_BLE_LEAK_CHECKIN,
    HEALTH_EVT_VALVE_CONNECTED,
    HEALTH_EVT_VALVE_DISCONNECTED,
    HEALTH_EVT_VALVE_BATTERY,
    HEALTH_EVT_VALVE_LEAK,          // valve's own flood probe changed state
    HEALTH_EVT_TICK,
    HEALTH_EVT_VALVE_RESYNC         // re-check the live valve link against its table entry
} health_event_type_t;

// Input event: posted by modules, consumed by health engine task
typedef struct {
    health_event_type_t type;
    union {
        struct {
            uint32_t sensor_id;
            uint8_t  battery;
            int8_t   rssi;
            float    snr;
            bool     leaking;
        } lora;
        struct {
            char    mac_str[18];
            uint8_t battery;
            int8_t  rssi;
            bool    leaking;
        } ble_leak;
        struct {
            uint8_t battery;
            bool    leaking;
        } valve;
    };
} health_event_t;

// Output alert: produced by health engine task, consumed by IoT Hub task
typedef struct {
    health_dev_type_t dev_type;
    char              dev_id[18];
    health_rating_t   new_rating;
    health_rating_t   old_rating;
    uint8_t           battery;
    int8_t            rssi;
    bool              offline;          // true = device_offline, false = device_recovered.
                                        // Explicit: never inferred from new_rating, since a
                                        // recovered device can still be CRITICAL (leak or
                                        // battery). Sits in padding: no size change.
    uint32_t          offline_duration_s;
} health_alert_t;

// Read-only snapshot of one device's health state (for cross-task queries)
typedef struct {
    bool              in_use;
    health_dev_type_t dev_type;
    char              dev_id[18];
    health_rating_t   rating;
    bool              connected;        // ever_seen AND not timed out / BLE up
    bool              ever_seen;        // true after first check-in this uptime
    bool              leaking;          // last reported wet/dry for this device
    uint8_t           last_battery;     // 0xFF = unknown
    int8_t            last_rssi;        // 0 = unknown
    bool              excused;          // not yet heard AND still inside its own roll-up
                                        // excuse window (the "Syncing" set). Sits in the
                                        // padding before last_seen_age_s: no size change.
    uint8_t           cause;            // health_cause_t: why `rating` is what it is. Also
                                        // in that padding: no size change.
    uint32_t          last_seen_age_s;  // UINT32_MAX = never seen
} health_device_status_t;

// Outcome of health_engine_reconcile_devices().
typedef struct {
    uint8_t total;     // devices in the table after the reconcile
    uint8_t added;     // newly provisioned devices appended
    uint8_t removed;   // devices dropped because they are no longer provisioned
} health_reconcile_result_t;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * @brief Initialize health engine: create task, queue, timer.
 *        Loads provisioned device list. Call after provisioning_init().
 */
void health_engine_init(void);

/**
 * @brief Reconcile the device table against provisioning, keyed by (type, id).
 *        Call when provisioning changes (add/remove devices).
 *
 *   - devices no longer provisioned are DROPPED;
 *   - NEW devices are APPENDED: unheard, CRITICAL until first contact, and their own
 *     roll-up excuse window starts now;
 *   - SURVIVORS KEEP EVERY FIELD (last seen, battery, RSSI, rating, leak, alert state,
 *     the valve's disconnect stamp). Removing one device never resets another.
 *
 * The snapshot sync window (sync_window_ms) is re-armed ONLY when at least one device
 * was added; an empty table counts as sync-complete.
 *
 * Returns false with the table UNTOUCHED if provisioning or the health mutex is
 * unavailable — the caller retries. Never call it from the esp-mqtt task: the single
 * owner of device-set changes is iothub_task (apply_device_set_change()).
 *
 * @param sync_window_ms  Snapshot window to arm if devices were added
 *                        (HEALTH_BOOT_SYNC_TIMEOUT_MS at boot,
 *                        HEALTH_COMMISSION_SYNC_TIMEOUT_MS after a provision). A new
 *                        valve's roll-up excuse is this same window.
 * @param out             Counts after the reconcile; may be NULL.
 */
bool health_engine_reconcile_devices(uint32_t sync_window_ms, health_reconcile_result_t *out);

/**
 * @brief Get how many provisioned devices have been heard at least once this
 *        sync cycle (seen) and how many are provisioned (total). Thread-safe.
 *        Used by the iothub loop to publish an incremental commission snapshot
 *        when a late device is first heard. Returns false on mutex timeout.
 */
bool health_get_sync_counts(uint8_t *seen, uint8_t *total);

/**
 * @brief Post a health event (thread-safe, non-blocking).
 * @return true if event was enqueued, false if queue full.
 */
bool health_post_event(const health_event_t *evt);

/**
 * @brief Ask the engine to re-check the live valve link against the valve's table entry.
 *        Non-blocking post; the link state is read on the health task when the event is
 *        processed, not here. Call after a reconcile that ADDED devices: a valve whose
 *        link came up before its entry existed had its CONNECTED dropped.
 * @return false if the event queue rejected the request.
 */
bool health_request_valve_resync(void);

/**
 * @brief Get the system health roll-up. Lock-free (volatile read), safe from any task.
 *
 * NOT a plain max() over the per-device ratings any more — two deliberate departures,
 * so do not expect it to equal the worst `rating` in health_get_device_status_all():
 *   - devices never heard from are EXCLUDED while their own roll-up excuse window is
 *     open (unless they are leaking), so a device can read CRITICAL
 *     individually while the roll-up reads EXCELLENT. "Not heard from yet" is not the
 *     same claim as "offline";
 *   - a latched leak interlock raises a WARNING floor, so the roll-up can be worse than
 *     every device in it (see health_set_interlock_held()).
 */
health_rating_t health_get_system_rating(void);

/**
 * @brief Dequeue one pending alert (non-blocking).
 * @param out  Pointer to alert struct to populate.
 * @return true if an alert was dequeued, false if queue empty.
 */
bool health_pop_alert(health_alert_t *out);

/**
 * @brief Convert a health_alert_t to a cJSON-formatted string.
 *        Caller must free() the returned string.
 * @return JSON string or NULL on failure.
 */
char *health_alert_to_json(const health_alert_t *alert);

/**
 * @brief Convert health_rating_t to string.
 */
const char *health_rating_to_str(health_rating_t rating);

/**
 * @brief Copy status of ALL provisioned devices into caller-supplied array.
 *        Thread-safe (acquires internal mutex). Call from iothub_task for snapshot.
 *
 * The system rating and the syncing flag are sampled under the SAME lock as the table
 * copy, after the window deadlines are evaluated, so the three can never disagree
 * (a separate rating read could land either side of a deadline and publish a rating
 * that does not match the table beside it).
 *
 * @param out            Array of HEALTH_MAX_DEVICES entries.
 * @param count_out      Number of valid (in_use) entries written.
 * @param sys_rating_out System roll-up at the moment of the copy; may be NULL.
 * @param syncing_out    true if any copied device has `excused` set; may be NULL.
 * @return true on success, false if mutex timeout or not initialized.
 */
bool health_get_device_status_all(health_device_status_t out[HEALTH_MAX_DEVICES],
                                  uint8_t *count_out, health_rating_t *sys_rating_out,
                                  bool *syncing_out);

/**
 * @brief Check whether boot sync is complete.
 *        Complete when all provisioned devices have checked in once,
 *        or the boot window (HEALTH_BOOT_SYNC_TIMEOUT_MS) has elapsed.
 *        Fails OPEN (returns true) if the engine is not up or the mutex is busy,
 *        so a caller can never get stuck in the "syncing" state.
 *
 * THIS IS THE SNAPSHOT GATE ONLY. It answers "may we publish a snapshot that claims
 * to be complete", on the short 180 s clock. It is NOT the right question for the
 * fleet LED or the reason string — use health_is_rollup_syncing() for those.
 */
bool health_is_boot_sync_complete(void);

/**
 * @brief Whether any provisioned device is still excluded from the system roll-up
 *        purely because it has not been heard from yet.
 *
 * The user-facing half of the boot-sync split. True means "the rating you are about to
 * read does not cover every device yet", which is what the fleet LED renders as WHITE
 * "syncing" and what the telemetry reason string reports as "syncing N devices".
 *
 * Goes false when every device has been heard OR each unheard device's OWN excuse
 * window has expired (sensors: HEALTH_ROLLUP_UNHEARD_MS from when they were added;
 * the valve: its sync window), at which point it starts counting and the rating
 * escalates on its own. A LEAKING device is never counted as syncing — see
 * HEALTH_ROLLUP_UNHEARD_MS.
 *
 * Fails CLOSED (returns false) if the engine is not up or the mutex is busy, so a
 * transient cannot latch the LED white; the caller falls through to its rating-based
 * answer, which is the safe direction.
 */
bool health_is_rollup_syncing(void);

/**
 * @brief Monotonic counter of sensor check-ins processed by the engine.
 *
 * Bumped once per LoRa packet / BLE-leak advertisement that reaches the engine — and
 * ONLY for those two; valve events are excluded because they already couple their own
 * snapshot in iothub_task. Lets a poller detect "a sensor was heard" without a queue
 * of its own, which matters because a repeat packet from an unchanged sensor is dropped
 * by the scanner's telemetry delta gate and never reaches iothub_task at all; this
 * engine is the only module that sees every burst.
 *
 * Lock-free: single writer (the health task), 32-bit aligned. Wraps at 2^32 — compare
 * with != , never with <.
 */
uint32_t health_get_checkin_seq(void);

/**
 * @brief Monotonic counter of rating changes the cloud must see promptly.
 *
 * Bumped when the system roll-up rating changes, and when the valve's rating changes to
 * or from a battery-driven state. iothub_task polls it and requests an EVENT snapshot, so
 * a rating change that raises no alert (valve battery-critical is deliberately silent,
 * and a roll-up grace expiry has no device edge at all) reaches the cloud within seconds
 * instead of at the next heartbeat.
 *
 * Lock-free read; every write happens under the engine's mutex. Wraps at 2^32 — compare
 * with != , never with <.
 */
uint32_t health_get_rating_seq(void);

/**
 * @brief Whether the provisioned valve's last REAL battery reading is at or below
 *        HEALTH_VALVE_BATTERY_CRIT_PCT (the level at which the valve refuses to open).
 *
 * The reading is kept across reconnects: a valve that dropped at 8 % is still at 8 %.
 * Returns false when no valve is provisioned, the battery is unknown, or the engine's
 * mutex is busy (100 ms) — the valve itself still refuses the open, so failing open here
 * costs only the explanatory error ack.
 */
bool health_is_valve_battery_critical(void);

/**
 * @brief Tell the health engine whether the hub is currently holding the valve
 *        closed on a latched leak incident.
 *
 * Raises a WARNING floor on the system rating for as long as it is true, so the
 * roll-up (and therefore the fleet LED) reads amber rather than green while the
 * water is still shut off — even once every sensor reports dry and every device is
 * individually healthy. Cleared when the incident clears (30 s all-dry auto-clear,
 * LEAK_RESET, or a physical override).
 *
 * Implemented as a lock-free volatile store, so it is safe to call from any task
 * and with any other lock held; it cannot participate in a lock cycle. Also nudges
 * the engine to re-roll immediately rather than waiting up to one 30 s tick.
 */
void health_set_interlock_held(bool held);

/**
 * @brief Whether the interlock WARNING floor is currently raised.
 *        Lock-free (volatile read). Used by the telemetry reason builder so it can
 *        name the interlock as the cause instead of inferring it from the absence of
 *        any other explanation.
 */
bool health_is_interlock_held(void);

// ---------------------------------------------------------------------------
// Convenience inline helpers (for hook sites)
// ---------------------------------------------------------------------------

/* `leaking` rides along on the check-in rather than getting its own event: the
 * wet/dry bit arrives in the very same LoRa packet / BLE advertisement as the
 * battery and RSSI, so carrying it here costs nothing and halves the queue traffic
 * a separate leak event would add. */
static inline void health_post_lora_checkin(uint32_t sensor_id, uint8_t battery,
                                             int8_t rssi, float snr, bool leaking)
{
    health_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = HEALTH_EVT_LORA_CHECKIN;
    evt.lora.sensor_id = sensor_id;
    evt.lora.battery   = battery;
    evt.lora.rssi      = rssi;
    evt.lora.snr       = snr;
    evt.lora.leaking   = leaking;
    health_post_event(&evt);
}

static inline void health_post_ble_leak_checkin(const char *mac_str, uint8_t battery,
                                                 int8_t rssi, bool leaking)
{
    health_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = HEALTH_EVT_BLE_LEAK_CHECKIN;
    strncpy(evt.ble_leak.mac_str, mac_str, sizeof(evt.ble_leak.mac_str) - 1);
    evt.ble_leak.battery = battery;
    evt.ble_leak.rssi    = rssi;
    evt.ble_leak.leaking = leaking;
    health_post_event(&evt);
}

/**
 * @brief Sticky fallback for a valve DISCONNECTED that the input queue rejected.
 *
 * Every other dropped health event self-heals: the next check-in or notify posts
 * the same truth again. A dropped valve DISCONNECTED does not. evaluate_timeouts()
 * re-rates the valve on every tick (since 2.1.2), but NOTHING except a DISCONNECTED
 * event stamps disconnect_ms, so every re-rate still sees a connected valve and the
 * rating stays at its last healthy value indefinitely. This fallback is therefore
 * still required. Written by health_post_valve_event(), drained by health_engine_task().
 */
extern volatile bool g_health_valve_disc_pending;

static inline void health_post_valve_event(bool connected)
{
    health_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = connected ? HEALTH_EVT_VALVE_CONNECTED : HEALTH_EVT_VALVE_DISCONNECTED;

    bool queued = health_post_event(&evt);

    if (connected) {
        // A CONNECTED that actually landed supersedes any pending disconnect —
        // without this, a drop followed by a fast relink would replay a stale
        // disconnect over the top of the recovery.
        if (queued) g_health_valve_disc_pending = false;
    } else if (!queued) {
        g_health_valve_disc_pending = true;
    }
}

static inline void health_post_valve_battery(uint8_t battery)
{
    health_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = HEALTH_EVT_VALVE_BATTERY;
    evt.valve.battery = battery;
    health_post_event(&evt);
}

/* The valve's own flood probe. Needs its own event (unlike the sensors) because the
 * flood characteristic notifies independently of battery and of connect/disconnect,
 * so there is no existing check-in to ride along on. */
static inline void health_post_valve_leak(bool leaking)
{
    health_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = HEALTH_EVT_VALVE_LEAK;
    evt.valve.leaking = leaking;
    health_post_event(&evt);
}

#ifdef __cplusplus
}
#endif

#endif // HEALTH_ENGINE_H
