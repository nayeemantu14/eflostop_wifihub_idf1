#include "health_engine.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "provisioning_manager.h"
#include "app_ble_valve.h"  // ble_valve_is_ready() / ble_valve_get_mac() — valve link resync
#include "rules_engine.h"   // leak_source_to_str() / leak_identity_key() — shared wire vocabulary
#include "telemetry/telemetry_v2.h"   // telemetry_v2_wake_snapshot() — wake the publisher on alert

#define HEALTH_TAG "HEALTH_ENGINE"

// ---------------------------------------------------------------------------
// Internal per-device state
// ---------------------------------------------------------------------------
typedef struct {
    bool              in_use;
    health_dev_type_t dev_type;
    char              dev_id[18];       // MAC string or "0xHEXID"
    health_rating_t   rating;
    uint32_t          added_s;          // monotonic seconds when this device entered the table
    uint16_t          excuse_s;         // length of this device's roll-up excuse window (s)
    bool              excuse_done;      // excuse window elapsed (latched)
    bool              offline_alerted;  // the last alert SENT for this device was
                                        // device_offline (so a recovery is owed)
    int64_t           last_seen_ms;     // Monotonic: esp_timer_get_time()/1000
    uint8_t           last_battery;     // 0xFF = unknown
    int8_t            last_rssi;        // 0 = unknown
    int64_t           last_alert_ms;    // Last alert timestamp (debounce)
    bool              ever_seen;        // false until first check-in this uptime
    bool              leaking;          // last reported wet/dry — forces CRITICAL
    uint8_t           cause;            // health_cause_t: why `rating` is what it is
    bool              alert_retry;      // an alert was debounced; the tick owes the
                                        // trailing edge (see evaluate_timeouts())
    int64_t           disconnect_ms;    // Valve only: disconnect timestamp, 0 = connected
} health_device_t;

/* The per-device excuse and alert fields live in the padding the old (dead) prev_rating
 * field and the int64 alignment left behind, so the table does not grow. */
_Static_assert(sizeof(health_device_t) <= 80,
               "health_device_t grew: s_devices[] is .bss, every byte is heap on this board");

// ---------------------------------------------------------------------------
// Static state
// ---------------------------------------------------------------------------
static QueueHandle_t  s_health_queue  = NULL;   // Input: health events
static QueueHandle_t  s_alert_queue   = NULL;   // Output: alerts for IoT Hub
static TimerHandle_t  s_tick_timer    = NULL;
static health_device_t s_devices[HEALTH_MAX_DEVICES];
static volatile health_rating_t s_system_rating = HEALTH_EXCELLENT;
static bool s_initialized = false;
static SemaphoreHandle_t s_mutex = NULL;
// Set by health_post_valve_event() when a valve DISCONNECTED could not be queued;
// drained by health_engine_task(). See the recovery block there for why only this
// one event needs a fallback.
volatile bool   g_health_valve_disc_pending = false;
static bool     s_boot_sync_done = false;
static int64_t  s_boot_start_ms  = 0;
static uint32_t s_boot_sync_timeout_ms = HEALTH_BOOT_SYNC_TIMEOUT_MS;  // window length, set when devices are added
/* The roll-up excuse (how long a never-heard device is kept out of the roll-up) is no
 * longer a global flag here: it is PER DEVICE (added_s / excuse_s / excuse_done), so a
 * provision or removal can never re-excuse a device that was already counting. See
 * rollup_unheard_locked(). */
/* Monotonic count of sensor check-ins the engine has processed. Written ONLY by
 * health_engine_task (single writer), read lock-free by iothub_task via
 * health_get_checkin_seq(). See that declaration for why this exists. */
static volatile uint32_t s_checkin_seq = 0;
/* Bumped on a system roll-up change and on a valve rating change to/from a battery-driven
 * state. Every write happens under s_mutex; read lock-free by iothub_task via
 * health_get_rating_seq(). See that declaration for why this exists. */
static volatile uint32_t s_rating_seq = 0;
// True while the hub is holding the valve shut on a latched leak incident. Written
// lock-free by health_set_interlock_held() from the rules engine; read by
// recalc_system_rating() (where it raises a WARNING floor) and by
// health_is_interlock_held() (so telemetry can name it as the cause). Deliberately
// NOT under s_mutex: the writer already holds the rules-engine mutex, and taking
// s_mutex there would create the one lock ordering this module has otherwise avoided.
static volatile bool s_interlock_held = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int64_t now_ms(void)
{
    return (int64_t)(esp_timer_get_time() / 1000);
}

// Seconds resolution is plenty for excuse windows of 150..600 s, and keeps added_s
// 32-bit so it fits in the old padding (see the _Static_assert above).
static uint32_t now_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

const char *health_rating_to_str(health_rating_t rating)
{
    switch (rating) {
        case HEALTH_EXCELLENT: return "excellent";
        case HEALTH_GOOD:      return "good";
        case HEALTH_WARNING:   return "warning";
        case HEALTH_CRITICAL:  return "critical";
        default:               return "unknown";
    }
}

// Wire vocabulary for device types. Health events emit this under the key
// `source_type` — the SAME key and the SAME spellings as leak and auto_close
// events, so the backend has one device-type vocabulary across every outbound
// message. (Health called the key `dev_type` before 2.0.0, which made the same
// concept a third name alongside outbound source_type and inbound sensor_type.)
//
// The strings themselves are not repeated here: each arm delegates to
// leak_source_to_str() in rules_engine.c, which is now the single definition.
// The mapping stays explicit rather than casting between the two enums — they
// are declared in a different order (HEALTH_DEV_VALVE=0 vs LEAK_SOURCE_BLE=0),
// so an index cast would silently mislabel every device.
//
// The identity KEY that accompanies it comes from leak_identity_key(), the
// matching single definition — see health_alert_to_json().
static const char *dev_type_to_source_type(health_dev_type_t dt)
{
    switch (dt) {
        case HEALTH_DEV_VALVE:    return leak_source_to_str(LEAK_SOURCE_VALVE);
        case HEALTH_DEV_LORA:     return leak_source_to_str(LEAK_SOURCE_LORA);
        case HEALTH_DEV_BLE_LEAK: return leak_source_to_str(LEAK_SOURCE_BLE);
        default:                  return "unknown";
    }
}

// ---------------------------------------------------------------------------
// Device lookup
// ---------------------------------------------------------------------------

static health_device_t *find_device(health_dev_type_t dt, const char *dev_id)
{
    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (s_devices[i].in_use &&
            s_devices[i].dev_type == dt &&
            strcasecmp(s_devices[i].dev_id, dev_id) == 0) {
            return &s_devices[i];
        }
    }
    return NULL;
}

static health_device_t *find_valve(void)
{
    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (s_devices[i].in_use && s_devices[i].dev_type == HEALTH_DEV_VALVE) {
            return &s_devices[i];
        }
    }
    return NULL;
}

// Forward declaration (defined after evaluate_timeouts)
static void check_boot_sync_locked(void);

// ---------------------------------------------------------------------------
// Rating calculation
// ---------------------------------------------------------------------------

/* A wet device is CRITICAL, full stop. This is checked FIRST — ahead of even the
 * staleness test — so a sensor that reported a leak and then went quiet stays
 * CRITICAL-because-leaking rather than being reclassified as merely offline. The
 * distinction matters for the reason string the snapshot carries: "Leak detected"
 * is actionable, "1 sensor offline" sends the installer looking for a dead battery.
 *
 * Nothing here consults the water-access override window, and that is deliberate:
 * an override means the USER accepted the risk of water staying on, not that the
 * leak stopped being a leak. Health must still read critical. */
static health_rating_t compute_sensor_rating(const health_device_t *dev, int64_t now,
                                             health_cause_t *cause)
{
    if (dev->leaking) {
        *cause = HEALTH_CAUSE_LEAK;
        return HEALTH_CRITICAL;
    }

    // Check connectivity timeout
    uint32_t timeout_ms = (dev->dev_type == HEALTH_DEV_LORA)
                          ? HEALTH_LORA_TIMEOUT_MS
                          : HEALTH_BLE_LEAK_TIMEOUT_MS;

    if (dev->last_seen_ms == 0 || (now - dev->last_seen_ms) > timeout_ms) {
        *cause = HEALTH_CAUSE_LINK;
        return HEALTH_CRITICAL;
    }

    // Online — evaluate battery and signal
    if (dev->last_battery != 0xFF && dev->last_battery <= HEALTH_BATTERY_WARN_PCT) {
        *cause = HEALTH_CAUSE_BATTERY;
        return HEALTH_WARNING;
    }
    if (dev->last_rssi != 0 && dev->last_rssi <= HEALTH_RSSI_WARN_DBM) {
        *cause = HEALTH_CAUSE_SIGNAL;
        return HEALTH_WARNING;
    }
    if (dev->last_battery != 0xFF && dev->last_battery <= HEALTH_BATTERY_GOOD_PCT) {
        *cause = HEALTH_CAUSE_BATTERY;
        return HEALTH_GOOD;
    }
    if (dev->last_rssi != 0 &&
        dev->last_rssi > HEALTH_RSSI_WARN_DBM &&
        dev->last_rssi <= HEALTH_RSSI_GOOD_DBM) {
        *cause = HEALTH_CAUSE_SIGNAL;
        return HEALTH_GOOD;
    }

    *cause = HEALTH_CAUSE_NONE;
    return HEALTH_EXCELLENT;
}

/* Valve bands are the valve's OWN (HEALTH_VALVE_BATTERY_*), not the sensors': at <=10 %
 * the valve auto-closes and refuses to open, which is CRITICAL by any reading. 2.1.3 had
 * no battery->CRITICAL branch at all and rated a dying valve WARNING (BUG-1). There is no
 * GOOD band for the valve, and an UNKNOWN battery (0xFF) is never rated — it used to be
 * stored as 0 and rated as an empty battery.
 *
 * Cause precedence at CRITICAL is LEAK > LINK (past the grace) > BATTERY. Inside the
 * disconnect grace a known <=10 % reading still wins over the WARNING grace: the last real
 * reading is still <=10 %, and letting the grace demote it would dip the rating
 * CRITICAL -> WARNING -> CRITICAL (a spurious device_recovered under the 2.1.3 alert rule)
 * for a valve that never got better. */
static health_rating_t compute_valve_rating(const health_device_t *dev, int64_t now,
                                            health_cause_t *cause)
{
    // The valve's own flood probe — same precedence as a sensor leak.
    if (dev->leaking) {
        *cause = HEALTH_CAUSE_LEAK;
        return HEALTH_CRITICAL;
    }

    bool batt_known = (dev->last_battery != 0xFF);

    // Offline past the grace outranks the battery at CRITICAL: unreachable is the
    // actionable fact, and it is what the reachability alert reports.
    if (dev->disconnect_ms > 0 &&
        (now - dev->disconnect_ms) >= HEALTH_VALVE_DISC_TIMEOUT_MS) {
        *cause = HEALTH_CAUSE_LINK;
        return HEALTH_CRITICAL;
    }

    // Battery critical — also while disconnected inside the grace (see above).
    if (batt_known && dev->last_battery <= HEALTH_VALVE_BATTERY_CRIT_PCT) {
        *cause = HEALTH_CAUSE_BATTERY;
        return HEALTH_CRITICAL;
    }

    // Disconnected: grace period — not yet CRITICAL
    if (dev->disconnect_ms > 0) {
        *cause = HEALTH_CAUSE_LINK;
        return HEALTH_WARNING;
    }

    // Never connected this uptime
    if (dev->last_seen_ms == 0) {
        *cause = HEALTH_CAUSE_LINK;
        return HEALTH_CRITICAL;
    }

    // Connected — the valve's Low band
    if (batt_known && dev->last_battery <= HEALTH_VALVE_BATTERY_WARN_PCT) {
        *cause = HEALTH_CAUSE_BATTERY;
        return HEALTH_WARNING;
    }

    *cause = HEALTH_CAUSE_NONE;
    return HEALTH_EXCELLENT;
}

/* Is this device currently EXCLUDED from the system roll-up purely because we have not
 * heard from it yet? Call with s_mutex held.
 *
 * Every device is seeded CRITICAL by health_engine_reconcile_devices() when it enters the
 * table and only leaves that state on its first check-in, so an unfiltered max() reported
 * the whole fleet CRITICAL from boot until the LAST sensor's first burst. On the current
 * event-driven sensors that is minutes — one field capture sat RED for 14 min on a hub
 * where all four sensors were fine. "Haven't heard from it yet" is not the same claim as
 * "it is offline", and only the second one should drive a red light.
 *
 * The per-device rating is deliberately left at CRITICAL: that is the honest answer to
 * "what do we know about this device", the snapshot still reports connected:false for it,
 * and once its excuse expires it counts again — so a genuinely absent device is still
 * escalated, just not prematurely.
 *
 * THE EXCUSE IS PER DEVICE, measured from when THAT device entered the table (added_s),
 * for its own length (excuse_s), and latched into excuse_done by check_boot_sync_locked():
 *   - sensors: HEALTH_ROLLUP_UNHEARD_MS (600 s). Using only the short snapshot window
 *     (the pre-2.1.3 behaviour) meant a healthy sensor that happened to beacon after
 *     180 s turned the LED red and published "1 sensor offline" — reproduced on the bench
 *     with a first contact at 265.6 s. See HEALTH_ROLLUP_UNHEARD_MS for why 600 s;
 *   - the valve: only its own sync window (HEALTH_BOOT_SYNC_TIMEOUT_MS at boot,
 *     HEALTH_COMMISSION_SYNC_TIMEOUT_MS after a provision) — the same values as 2.1.3.
 *
 * It used to be ONE global grace re-armed by every reload, so a single `decommission`
 * re-excused every unheard device on the hub — including one that had been missing for
 * 1100 s — and flipped a RED hub to WHITE/"excellent" (2.1.3 field log, 1193 s). Now a
 * provision excuses only the devices it adds, and a removal excuses nobody.
 *
 * THE VALVE DOES NOT GET THE EXTENDED GRACE. HEALTH_ROLLUP_UNHEARD_MS is calibrated for
 * the advertising sensors: 600 s covers a ~100 s burst cadence with margin, and it is
 * justified by being the same deadline at which a previously-seen SENSOR is declared
 * offline (HEALTH_BLE_LEAK_TIMEOUT_MS). Neither half of that reasoning transfers to the
 * valve. It sits on a continuous BLE link rather than a burst cadence, so there is no
 * interval to accommodate, and its own offline deadline is HEALTH_VALVE_DISC_TIMEOUT_MS
 * (180 s) — a third of the sensor one.
 *
 * Extending the excuse to the valve would mask the single device the entire leak response
 * depends on: handle_valve_event() sets ever_seen only on CONNECTED, so a valve that is
 * powered off or out of range keeps ever_seen==false and would be excluded from the
 * roll-up for a full 10 minutes — the fleet LED showing WHITE "syncing" and every snapshot
 * publishing rating:"excellent" while the hub has no way to shut the water off. And it
 * would apply on every provision, i.e. precisely during commissioning, when someone is
 * standing there deciding whether the install works.
 *
 * Note the asymmetry this keeps is the right way round: a valve that links once and then
 * drops stamps disconnect_ms, is not excluded at all, and escalates via
 * compute_valve_rating()'s own 180 s grace. Never-connected must not be treated more
 * leniently than connected-then-lost.
 *
 * A device reporting a LEAK is never excluded, whatever ever_seen says. If we know it is
 * wet then we have plainly heard from it, and suppressing that to keep the boot LED tidy
 * would be the one trade this release must not make. This matters for the valve in
 * particular: handle_valve_leak() owns only the flood probe and deliberately does not
 * touch ever_seen, so a wet probe read during setup would otherwise sit excluded behind
 * a WHITE "syncing" LED.
 *
 * Shared by recalc_system_rating() (which skips these devices) and
 * health_is_rollup_syncing() (which reports them), so the rating, the fleet LED and the
 * telemetry reason string can never disagree about who is still syncing. */
static bool rollup_unheard_locked(const health_device_t *dev)
{
    /* No time argument: the per-device deadline is latched into excuse_done by
     * check_boot_sync_locked(), which every caller reaches first. That keeps the
     * decision a pure function of state, so the roll-up and health_is_rollup_syncing()
     * cannot straddle a deadline and return different answers in one pass. */
    return dev->in_use && !dev->ever_seen && !dev->leaking && !dev->excuse_done;
}

static void recalc_system_rating(void)
{
    health_rating_t worst = HEALTH_EXCELLENT;
    bool any_device = false;

    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (!s_devices[i].in_use) continue;
        any_device = true;

        if (rollup_unheard_locked(&s_devices[i])) continue;

        if (s_devices[i].rating > worst) {
            worst = s_devices[i].rating;
        }
    }

    /* WARNING floor while the hub is holding the valve shut on a latched incident.
     * Once the water dries, every device reads healthy again and the roll-up would
     * otherwise go straight back to EXCELLENT/green — while the valve is still
     * closed and the taps are still dry. Amber is the honest middle state: nothing
     * is leaking, but the system has not returned to normal either.
     *
     * Only with at least one device in the table. An empty hub has nothing to hold
     * closed, and the floor there published "warning / Leak interlock latched" while
     * the fleet LED (which tests total==0 first) showed WHITE (F4). */
    if (s_interlock_held && any_device && worst < HEALTH_WARNING) {
        worst = HEALTH_WARNING;
    }

    /* A roll-up change is itself news: a grace expiry, for one, changes it with no device
     * edge and no alert, and the 2.1.3 field log sat RED from 678 s with nothing published
     * until the 969 s heartbeat (N10). Every caller holds s_mutex. */
    if (worst != s_system_rating) {
        s_system_rating = worst;
        s_rating_seq++;
    }
}

// ---------------------------------------------------------------------------
// Alert generation
// ---------------------------------------------------------------------------

/* HEALTH ALERTS REPORT REACHABILITY ONLY: device_offline on ENTERING the offline state
 * (CRITICAL because of the link, having been heard), device_recovered on LEAVING it — and
 * only if that device_offline was actually sent (offline_alerted).
 *
 * A leak- or battery-driven CRITICAL raises no alert (Phase B: suppressed like a leak).
 * Both event names are reachability claims: device_offline for a device that is
 * demonstrably online — in a payload carrying its healthy RSSI — is false, and the
 * recovery that follows would be a second false claim. The leak has its own
 * leak_detected / leak_cleared events; the battery state is carried by the snapshot's
 * rating and reason, which a rating-seq snapshot publishes within seconds
 * (health_get_rating_seq()). Inventing a new event name would also add a value to a
 * vocabulary the cloud validates against.
 *
 * Keyed on the STATE (rating + cause), not on rating edges: 2.1.3 alerted on every
 * CRITICAL edge and named it from the rating alone, so a battery-critical valve that
 * disconnected would have published a device_recovered on entering the WARNING grace. */
static bool is_offline_state(health_rating_t r, uint8_t cause)
{
    return r == HEALTH_CRITICAL && cause == HEALTH_CAUSE_LINK;
}

// Build and queue one alert. Returns false ONLY when the queue rejected it.
static bool enqueue_alert(health_device_t *dev, bool offline, health_rating_t new_rating,
                          int64_t now)
{
    health_alert_t alert;
    memset(&alert, 0, sizeof(alert));
    alert.dev_type    = dev->dev_type;
    strncpy(alert.dev_id, dev->dev_id, sizeof(alert.dev_id) - 1);
    alert.new_rating  = new_rating;
    alert.old_rating  = dev->rating;
    alert.battery     = dev->last_battery;
    alert.rssi        = dev->last_rssi;
    alert.offline     = offline;

    if (offline && dev->last_seen_ms > 0) {
        alert.offline_duration_s = (uint32_t)((now - dev->last_seen_ms) / 1000);
    }

    const char *kind = offline ? "device_offline" : "device_recovered";

    if (xQueueSend(s_alert_queue, &alert, 0) != pdTRUE) {
        ESP_LOGE(HEALTH_TAG, "ALERT QUEUE FULL — %s %s %s -> %s (%s) held for retry",
                 dev_type_to_source_type(dev->dev_type), dev->dev_id,
                 health_rating_to_str(dev->rating),
                 health_rating_to_str(new_rating), kind);
        return false;
    }

    ESP_LOGW(HEALTH_TAG, "ALERT: %s %s %s -> %s (%s)",
             dev_type_to_source_type(dev->dev_type), dev->dev_id,
             health_rating_to_str(dev->rating),
             health_rating_to_str(new_rating), kind);

    // Wake the publisher. The alert queue is NOT a member of iothub_task's
    // QueueSet, so without this the alert waits for whatever wakes that loop next
    // — up to the 30 s idle cap. The wake reuses the snapshot trigger queue, which
    // IS in the set and IS always consumed; iothub_task drains alerts in its PUBLISH
    // phase, behind dps_maintain()/sas_maintain(), either way.
    telemetry_v2_wake_snapshot();
    return true;
}

// Commit a new rating + cause, raising the reachability alert the change warrants (see
// is_offline_state()). Call with s_mutex held.
static void apply_rating(health_device_t *dev, health_rating_t new_rating,
                         health_cause_t new_cause, int64_t now)
{
    bool old_off = is_offline_state(dev->rating, dev->cause);
    bool new_off = is_offline_state(new_rating, (uint8_t)new_cause);

    // !offline_alerted: never a second device_offline without the recovery between them
    // (reachable only if a debounced recovery's trailing edge kept failing to enqueue).
    bool want_offline   = new_off && !old_off && dev->ever_seen && !dev->offline_alerted;
    bool want_recovered = !new_off && old_off && dev->offline_alerted;

    if (want_offline || want_recovered) {
        if (dev->last_alert_ms != 0 &&
            (now - dev->last_alert_ms) < HEALTH_ALERT_DEBOUNCE_MS) {
            // Debounced, not dropped: the state is committed below and the tick emits the
            // trailing edge once the debounce has passed (evaluate_timeouts()). 2.1.3
            // dropped it outright, so a recovery inside the debounce left the cloud
            // believing the device was still offline.
            dev->alert_retry = true;
        } else if (!enqueue_alert(dev, want_offline, new_rating, now)) {
            // Queue full: hold the old rating so the next evaluation re-detects the same
            // transition and retries. Committing would consume the transition, and the
            // alert would be lost permanently, not merely delayed.
            return;
        } else {
            dev->offline_alerted = want_offline;
            dev->last_alert_ms   = now;
            dev->alert_retry     = false;
        }
    }

    // A valve rating change to or from a battery-driven state raises no alert, so it asks
    // for a snapshot instead (health_get_rating_seq()).
    bool valve_batt_edge = dev->dev_type == HEALTH_DEV_VALVE &&
                           (dev->rating != new_rating || dev->cause != (uint8_t)new_cause) &&
                           (dev->cause == HEALTH_CAUSE_BATTERY ||
                            new_cause == HEALTH_CAUSE_BATTERY);

    dev->rating = new_rating;
    dev->cause  = (uint8_t)new_cause;
    if (valve_batt_edge) s_rating_seq++;
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

static void handle_lora_checkin(const health_event_t *evt)
{
    char id_str[16];
    snprintf(id_str, sizeof(id_str), "0x%08lX", (unsigned long)evt->lora.sensor_id);

    health_device_t *dev = find_device(HEALTH_DEV_LORA, id_str);
    if (!dev) return;  // Not provisioned

    int64_t now = now_ms();
    dev->last_seen_ms  = now;
    dev->last_battery  = evt->lora.battery;
    dev->last_rssi     = evt->lora.rssi;
    dev->leaking       = evt->lora.leaking;

    health_cause_t cause;
    health_rating_t new_rating = compute_sensor_rating(dev, now, &cause);
    apply_rating(dev, new_rating, cause, now);
    dev->ever_seen = true;
    check_boot_sync_locked();
}

static void handle_ble_leak_checkin(const health_event_t *evt)
{
    health_device_t *dev = find_device(HEALTH_DEV_BLE_LEAK, evt->ble_leak.mac_str);
    if (!dev) return;

    int64_t now = now_ms();
    dev->last_seen_ms  = now;
    dev->last_battery  = evt->ble_leak.battery;
    dev->last_rssi     = evt->ble_leak.rssi;
    dev->leaking       = evt->ble_leak.leaking;

    health_cause_t cause;
    health_rating_t new_rating = compute_sensor_rating(dev, now, &cause);
    apply_rating(dev, new_rating, cause, now);
    dev->ever_seen = true;
    check_boot_sync_locked();
}

static void handle_valve_event(bool connected)
{
    health_device_t *dev = find_valve();
    if (!dev) return;

    int64_t now = now_ms();

    if (connected) {
        dev->last_seen_ms = now;
        dev->disconnect_ms = 0;   // Clear grace period
    } else if (dev->disconnect_ms == 0) {
        // LATCH the stamp. This was an unconditional `dev->disconnect_ms = now`,
        // and compute_valve_rating() measures the grace ONLY from this field — so
        // every repeat DISCONNECTED restarted the clock. A valve that keeps
        // linking and keeps failing setup re-stamps faster than the grace expires
        // and NEVER reaches CRITICAL: the most degraded valve in the fleet was the
        // one the design guaranteed would stay silent.
        //
        // It also fixes a second bug. Once the valve was CRITICAL, a repeat
        // DISCONNECTED re-stamped, compute_valve_rating() returned WARNING again,
        // and the alert path took the out-of-critical branch with ever_seen
        // already true — publishing `device_recovered` for a valve that never came
        // back. With the latch, a repeat recomputes the same CRITICAL and
        // apply_rating() has no transition to alert on.
        //
        // The grace now means "time since the link was last usable", which is what
        // the rating already claimed it meant.
        dev->disconnect_ms = now;  // Start grace period (keep last_seen_ms)
    }

    health_cause_t cause;
    health_rating_t new_rating = compute_valve_rating(dev, now, &cause);
    apply_rating(dev, new_rating, cause, now);
    if (connected) {
        dev->ever_seen = true;
        check_boot_sync_locked();
    }
}

// Valve battery update (every BLE battery read/notify, plus a resync at link-up).
// Refreshes the stored battery and re-rates the valve so a low battery is
// reflected. Owns ONLY the battery — connectivity (last_seen_ms/disconnect_ms)
// stays with the connect/disconnect events. Without this the valve path never fed
// a battery, so compute_valve_rating saw last_battery==0xFF and always returned
// EXCELLENT.
static void handle_valve_battery(uint8_t battery)
{
    health_device_t *dev = find_valve();
    if (!dev) return;
    // Unknown (no characteristic, failed read, setup not done) — ignore, so
    // last_battery keeps the last REAL reading across a reconnect.
    if (battery == 0xFF) return;

    dev->last_battery = battery;

    int64_t now = now_ms();
    health_cause_t cause;
    health_rating_t new_rating = compute_valve_rating(dev, now, &cause);
    apply_rating(dev, new_rating, cause, now);
}

// Valve flood-probe state change. Owns ONLY `leaking`; connectivity and battery
// stay with their own events, matching how handle_valve_battery() is scoped.
static void handle_valve_leak(bool leaking)
{
    health_device_t *dev = find_valve();
    if (!dev) return;

    dev->leaking = leaking;

    int64_t now = now_ms();
    health_cause_t cause;
    health_rating_t new_rating = compute_valve_rating(dev, now, &cause);
    apply_rating(dev, new_rating, cause, now);
}

/* A valve whose link came up BEFORE its table entry existed (health_request_valve_resync()).
 * The C2D provision handler sets the BLE target and connects on the esp-mqtt task, but the
 * entry is appended later, by the reconcile on iothub_task. A CONNECTED processed in between
 * found no entry and was dropped, and the next one is posted only by a valve notify that
 * CHANGES a value — so the valve sat CRITICAL "never connected" and, once its excuse ran
 * out, turned the hub RED "Valve offline" while it was connected.
 *
 * The live link state is read HERE, on this task, when the event is processed. Events are
 * processed FIFO and the valve module clears its ready bits before posting DISCONNECTED: a
 * DISCONNECTED posted before this event has already been applied, and one posted after it
 * is applied after it. Reading is_ready on iothub_task and posting a CONNECTED from there
 * could land a stale CONNECTED after a DISCONNECTED.
 *
 * Only the link is resynced; the battery is NOT re-fed. The deleted
 * reseed_valve_health_if_connected() fed the unknown battery as 0 % (the BUG-1 trap), and
 * the valve's battery reaches this engine by itself from on_notify() on every read/notify,
 * with 0xFF ignored by handle_valve_battery(). Call with s_mutex held. */
static void handle_valve_resync(void)
{
    if (!ble_valve_is_ready()) return;

    health_device_t *dev = find_valve();
    if (!dev) return;

    // The entry already reflects a live link: nothing was lost.
    if (dev->last_seen_ms != 0 && dev->disconnect_ms == 0) return;

    // Only a link to THIS valve: the valve module can still hold a link to another one
    // (it can relink a valve by name), and this handler asserts the provisioned one is up.
    char live_mac[18];
    if (!ble_valve_get_mac(live_mac) || strcasecmp(live_mac, dev->dev_id) != 0) return;

    ESP_LOGI(HEALTH_TAG, "Valve link resync: link was already up when its table entry was added");
    handle_valve_event(true);
}

static void evaluate_timeouts(void)
{
    int64_t now = now_ms();

    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        health_device_t *dev = &s_devices[i];
        if (!dev->in_use) continue;

        // Valve: disconnect grace expiry (WARNING → CRITICAL), and re-rate while
        // connected too.
        //
        // The connected case used to be skipped entirely, on the reasoning that a
        // connected valve's rating only ever changes when an event arrives. That is no
        // longer true in a useful way: `leaking` and `last_battery` both arrive by
        // non-blocking queue post, and apply_rating() also declines to commit when the
        // alert queue is full. Either of those loses the transition permanently,
        // because nothing else re-rated a connected valve — so a WET flood probe could
        // stay invisible to the roll-up for the whole episode. Re-rating here is the
        // self-healing path every other device already had.
        //
        // A CAUSE change alone counts too: a battery-critical valve whose disconnect
        // grace expires stays CRITICAL but becomes offline (BATTERY -> LINK), and that
        // is exactly the edge that owes a device_offline.
        health_cause_t cause;
        if (dev->dev_type == HEALTH_DEV_VALVE) {
            health_rating_t new_rating = compute_valve_rating(dev, now, &cause);
            if (new_rating != dev->rating || (uint8_t)cause != dev->cause) {
                apply_rating(dev, new_rating, cause, now);
            }
            continue;
        }

        // Sensors: skip devices never seen (already CRITICAL from init)
        if (dev->last_seen_ms == 0) continue;

        health_rating_t new_rating = compute_sensor_rating(dev, now, &cause);

        if (new_rating != dev->rating || (uint8_t)cause != dev->cause) {
            apply_rating(dev, new_rating, cause, now);
        }
    }

    /* Trailing edge of a debounced alert. apply_rating() commits the state even when the
     * alert is debounced; this emits the one alert that makes the cloud's view match the
     * state once the debounce has passed, so the event stream converges within one
     * debounce + one tick (<= 90 s). Nothing is sent if the device has since returned to
     * the state last reported. */
    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        health_device_t *dev = &s_devices[i];
        if (!dev->in_use || !dev->alert_retry) continue;
        if ((now - dev->last_alert_ms) < HEALTH_ALERT_DEBOUNCE_MS) continue;

        bool desired = is_offline_state(dev->rating, dev->cause);
        if (desired != dev->offline_alerted && (desired ? dev->ever_seen : true)) {
            if (enqueue_alert(dev, desired, dev->rating, now)) {
                dev->offline_alerted = desired;
                dev->last_alert_ms   = now;
                dev->alert_retry     = false;
            }
            // Queue full: alert_retry stays set and the next tick retries.
        } else {
            dev->alert_retry = false;
        }
    }

    check_boot_sync_locked();
}

// ---------------------------------------------------------------------------
// Window deadline evaluation (call with s_mutex held)
//
// Evaluates TWO kinds of deadline:
//   1. s_boot_sync_timeout_ms from s_boot_start_ms -> s_boot_sync_done — the global
//      SNAPSHOT gate (re-armed only when devices are ADDED);
//   2. each device's own excuse_s from its added_s  -> excuse_done — the per-device
//      ROLL-UP exclusion (see rollup_unheard_locked()).
// They are separate because they answer different questions; see
// HEALTH_ROLLUP_UNHEARD_MS in the header for the bench capture that forced the split.
// ---------------------------------------------------------------------------
static void check_boot_sync_locked(void)
{
    /* NO early return on s_boot_sync_done: the per-device excuses outlive the snapshot
     * gate, so this function has to keep being reachable after the gate has closed.
     * Every latch has its own one-shot guard below, so there is no repeated work. */
    bool edge = false;
    uint32_t t_s = now_s();

    if (!s_boot_sync_done) {
        bool all_seen = true;
        for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
            if (s_devices[i].in_use && !s_devices[i].ever_seen) {
                all_seen = false;
                break;
            }
        }

        if (all_seen) {
            s_boot_sync_done = true;
            ESP_LOGI(HEALTH_TAG, "Boot sync: all devices seen");
        } else if ((now_ms() - s_boot_start_ms) >= s_boot_sync_timeout_ms) {
            s_boot_sync_done = true;
            /* State the REMAINING excuse, not a constant: the longest time any unheard
             * device is still kept out of the roll-up. A bench capture once read "still
             * excused for 600 s" at t=435 s when the real deadline was t=885 s; a
             * diagnostic line that has to be corrected by hand is worse than no line. */
            uint32_t further_s = 0;
            for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
                const health_device_t *d = &s_devices[i];
                if (!rollup_unheard_locked(d)) continue;
                uint32_t elapsed_s = t_s - d->added_s;
                uint32_t window_s  = (uint32_t)d->excuse_s;
                uint32_t left_s = (elapsed_s < window_s) ? (window_s - elapsed_s) : 0;
                if (left_s > further_s) further_s = left_s;
            }
            ESP_LOGW(HEALTH_TAG, "Boot sync: timeout (%lu s) — snapshot gate open; "
                     "unheard devices still excused for a further %lld s",
                     (unsigned long)(s_boot_sync_timeout_ms / 1000),
                     (long long)further_s);
        }
        if (s_boot_sync_done) edge = true;
    }

    /* Per-device excuse expiry. Needs an edge for the same reason the gate does (see
     * below): nothing else recalcs when a deadline passes, so without this the roll-up
     * would keep excluding a genuinely absent device — reporting EXCELLENT / GREEN —
     * until the next 30 s tick happened to come round. */
    int newly_counted = 0;
    uint32_t counted_window_s = 0;
    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        health_device_t *d = &s_devices[i];
        if (!d->in_use || d->excuse_done) continue;
        if ((uint32_t)(t_s - d->added_s) < (uint32_t)d->excuse_s) continue;

        bool was_unheard = rollup_unheard_locked(d);
        d->excuse_done = true;
        if (was_unheard) {
            newly_counted++;
            if ((uint32_t)d->excuse_s > counted_window_s) counted_window_s = d->excuse_s;
        }
    }

    /* Announce ONLY when an expiry actually changes something, i.e. some device is still
     * unheard and is about to start counting. On a healthy hub every device has been heard
     * long before its deadline; an unconditional line printed "unheard devices now count"
     * at WARNING level after every boot AND every provision with nothing unheard — both
     * false and alarming, in the channel people scan first when something is wrong.
     * Observed on the 2.1.3 bench capture at t=885 s with all four sensors healthy. The
     * latch itself is set regardless (it is a deadline, not an event). */
    if (newly_counted > 0) {
        ESP_LOGW(HEALTH_TAG,
                 "Roll-up grace expired (%lu s) — %d unheard device(s) now count",
                 (unsigned long)counted_window_s, newly_counted);
        edge = true;
    }

    /* Re-roll immediately if a gate or an excuse JUST closed — the one-shot guards above
     * mean a latch can only be set here by the call that sets it, so this is a transition
     * edge, not a per-call cost.
     *
     * Needed because recalc_system_rating() EXCLUDES not-yet-heard devices until their
     * excuse latches, so a latch flipping changes the roll-up's inputs — and this
     * function is reached from health_is_boot_sync_complete(),
     * health_is_rollup_syncing() and health_get_device_status_all(), i.e. from the fleet
     * LED / iothub poll / snapshot, which do NOT otherwise recalc.
     *
     * Without this, a window that times out with a genuinely absent sensor left
     * s_system_rating at its stale EXCELLENT: callers saw "sync complete" and
     * "everything excellent" at the same time, so the LED showed GREEN — and any
     * snapshot in that gap reported excellent — for a hub that was missing a device.
     * Exactly the false-green the exclusion rule exists to avoid, just moved to a
     * different 30 s.
     *
     * Safe under every caller: all hold s_mutex, and recalc only reads s_devices[] and
     * writes s_system_rating. Redundant on the task path (which recalcs at the end of
     * the iteration anyway) and idempotent, so that is free. */
    if (edge) {
        recalc_system_rating();
    }
}

// ---------------------------------------------------------------------------
// Tick timer callback (runs in timer daemon context)
// ---------------------------------------------------------------------------
static void tick_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    health_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = HEALTH_EVT_TICK;
    xQueueSend(s_health_queue, &evt, 0);
}

// ---------------------------------------------------------------------------
// Main task
// ---------------------------------------------------------------------------
static void health_engine_task(void *param)
{
    (void)param;
    health_event_t evt;

    ESP_LOGI(HEALTH_TAG, "Task started");

    while (1) {
        if (xQueueReceive(s_health_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        xSemaphoreTake(s_mutex, portMAX_DELAY);

        switch (evt.type) {
            /* Both sensor check-ins bump s_checkin_seq — this task is its single
             * writer. Valve events deliberately do NOT: they already couple their own
             * snapshot in iothub_task via the valve_linked delta gate, so counting them
             * here would double-publish on every link edge. */
            case HEALTH_EVT_LORA_CHECKIN:
                handle_lora_checkin(&evt);
                s_checkin_seq++;
                break;
            case HEALTH_EVT_BLE_LEAK_CHECKIN:
                handle_ble_leak_checkin(&evt);
                s_checkin_seq++;
                break;
            case HEALTH_EVT_VALVE_CONNECTED:
                handle_valve_event(true);
                break;
            case HEALTH_EVT_VALVE_DISCONNECTED:
                handle_valve_event(false);
                break;
            case HEALTH_EVT_VALVE_BATTERY:
                handle_valve_battery(evt.valve.battery);
                break;
            case HEALTH_EVT_VALVE_LEAK:
                handle_valve_leak(evt.valve.leaking);
                break;
            case HEALTH_EVT_TICK:
                evaluate_timeouts();
                break;
            case HEALTH_EVT_VALVE_RESYNC:
                handle_valve_resync();
                break;
        }

        // Recover a valve DISCONNECTED that never made it onto the queue.
        //
        // health_post_valve_event() posts non-blocking from the NimBLE host task.
        // If that send fails the event is gone, and for a DISCONNECTED that is
        // UNRECOVERABLE by any other path: NOTHING except a DISCONNECTED event ever
        // stamps disconnect_ms, so compute_valve_rating() keeps returning a
        // connected-valve rating however many ticks run. The rating pins at its last
        // healthy value — a green valve that is not there.
        //
        // NB the premise here used to be "evaluate_timeouts() gates the valve re-rate
        // on disconnect_ms > 0, so no tick can ever notice". That gate was removed in
        // 2.1.2 (the valve is now re-rated every tick), but the fallback is still
        // REQUIRED for the reason above. Do not read the missing gate as evidence that
        // this is dead code and delete it — that restores the silently-green absent
        // valve, which is the one failure mode this whole release exists to remove.
        //
        // MUST run AFTER the switch, not before. The flag is only ever set when
        // the 16-deep queue was FULL, so a backlog of strictly OLDER events sits
        // ahead of it — and notify_hub_update() posts VALVE_CONNECTED for every
        // data-bearing valve notify, so a stale CONNECTED is the normal queue
        // content. Draining first let that older event run second and clear
        // disconnect_ms straight back to 0, making this whole fallback a no-op.
        // Applying it last is what makes the newest truth win.
        //
        // The asymmetry is the point: a dropped CONNECTED self-heals on the next
        // notify, a dropped DISCONNECTED does not. So the fallback is biased
        // toward applying the disconnect — if it is stale, the next notify
        // corrects it, which is the safe direction to be wrong in.
        if (g_health_valve_disc_pending) {
            g_health_valve_disc_pending = false;
            ESP_LOGW(HEALTH_TAG, "Recovering dropped valve DISCONNECTED event");
            handle_valve_event(false);
        }

        recalc_system_rating();

        xSemaphoreGive(s_mutex);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Is this table entry still in the provisioned set? Keyed by (type, id). Pure.
static bool device_in_set(const prov_device_set_t *set, const health_device_t *dev)
{
    switch (dev->dev_type) {
        case HEALTH_DEV_VALVE:
            return set->has_valve && strcasecmp(set->valve_mac, dev->dev_id) == 0;
        case HEALTH_DEV_LORA:
            for (int i = 0; i < set->lora_count; i++) {
                char id[16];
                snprintf(id, sizeof(id), "0x%08lX", (unsigned long)set->lora_ids[i]);
                if (strcasecmp(id, dev->dev_id) == 0) return true;
            }
            return false;
        case HEALTH_DEV_BLE_LEAK:
            for (int i = 0; i < set->ble_count; i++) {
                if (strcasecmp(set->ble_macs[i], dev->dev_id) == 0) return true;
            }
            return false;
        default:
            return false;
    }
}

/* Append one provisioned device unless it is already in the table (a survivor, or a
 * duplicate inside the set). Call with s_mutex held, after compaction, so the first free
 * slot is the end of the used run and survivors keep their order.
 * Returns 1 = appended, 0 = already present, -1 = table full. */
static int append_device_locked(health_dev_type_t type, const char *id,
                                uint16_t excuse_s, uint32_t added_s)
{
    if (find_device(type, id)) return 0;

    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        health_device_t *dev = &s_devices[i];
        if (dev->in_use) continue;

        // memset seeds every "unknown" field: last_seen_ms/last_alert_ms/disconnect_ms 0,
        // last_rssi 0, ever_seen/leaking/excuse_done false, and no alert owed
        // (offline_alerted/alert_retry false: its first contact is not a recovery).
        memset(dev, 0, sizeof(*dev));
        dev->in_use       = true;
        dev->dev_type     = type;
        strncpy(dev->dev_id, id, sizeof(dev->dev_id) - 1);
        dev->dev_id[sizeof(dev->dev_id) - 1] = '\0';
        dev->rating       = HEALTH_CRITICAL;   // until first contact
        dev->cause        = HEALTH_CAUSE_LINK; // ...because it has not been heard
        dev->offline_alerted = false;
        dev->alert_retry     = false;
        dev->last_battery = 0xFF;              // unknown
        dev->added_s      = added_s;
        dev->excuse_s     = excuse_s;
        return 1;
    }
    return -1;
}

bool health_engine_reconcile_devices(uint32_t sync_window_ms, health_reconcile_result_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!s_mutex) return false;

    /* Fetch the set BEFORE taking s_mutex: no provisioning call may run while the health
     * table is locked (the old reload made three of them, 1 s timeout each, under it). */
    prov_device_set_t set;
    if (!provisioning_get_device_set(&set)) {
        ESP_LOGE(HEALTH_TAG, "Reconcile deferred: provisioning unavailable");
        return false;
    }

    // Checked take: never touch the table, and never give, without holding the mutex.
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(HEALTH_TAG, "Reconcile deferred: health table busy");
        return false;
    }

    uint8_t removed = 0, added = 0, dropped = 0;

    /* 1. Drop what is no longer provisioned.
     *
     * NOTHING ELSE IS RESET. The 2.1.3 reload wiped the whole table, so removing one
     * sensor made every survivor unseen again (null battery/RSSI, "Syncing") and handed
     * an offline valve a fresh excuse by losing its disconnect stamp. Survivors now keep
     * every field, which also keeps a wet survivor's `leaking` without the old carry
     * table, and a removed device's leak is correctly forgotten with it. */
    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        health_device_t *dev = &s_devices[i];
        if (!dev->in_use) continue;
        if (!device_in_set(&set, dev)) {
            memset(dev, 0, sizeof(*dev));
            removed++;
        }
    }

    // 2. Compact in place, preserving the survivors' relative order.
    int w = 0;
    for (int r = 0; r < HEALTH_MAX_DEVICES; r++) {
        if (!s_devices[r].in_use) continue;
        if (r != w) {
            s_devices[w] = s_devices[r];
            memset(&s_devices[r], 0, sizeof(s_devices[r]));
        }
        w++;
    }

    /* 3. Append what is new, valve then LoRa then BLE (the old reload's order). The valve's
     * excuse is only its sync window; a sensor gets the full HEALTH_ROLLUP_UNHEARD_MS — see
     * rollup_unheard_locked() for why the two differ. */
    uint32_t t_s = now_s();
    int rc;
    if (set.has_valve) {
        rc = append_device_locked(HEALTH_DEV_VALVE, set.valve_mac,
                                  (uint16_t)(sync_window_ms / 1000), t_s);
        if (rc > 0) added++; else if (rc < 0) dropped++;
    }
    for (int i = 0; i < set.lora_count; i++) {
        char id[16];
        snprintf(id, sizeof(id), "0x%08lX", (unsigned long)set.lora_ids[i]);
        rc = append_device_locked(HEALTH_DEV_LORA, id,
                                  (uint16_t)(HEALTH_ROLLUP_UNHEARD_MS / 1000), t_s);
        if (rc > 0) added++; else if (rc < 0) dropped++;
    }
    for (int i = 0; i < set.ble_count; i++) {
        rc = append_device_locked(HEALTH_DEV_BLE_LEAK, set.ble_macs[i],
                                  (uint16_t)(HEALTH_ROLLUP_UNHEARD_MS / 1000), t_s);
        if (rc > 0) added++; else if (rc < 0) dropped++;
    }

    uint8_t total = 0;
    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (s_devices[i].in_use) total++;
    }

    /* 4. Re-arm the snapshot gate ONLY when something was added: the new devices have
     * genuinely not been heard, and the "wait for all commissioned devices" budget must be
     * anchored to this commission, not to power-on. A pure removal leaves an open window
     * running unchanged and never opens a closed one — it was re-arming here that made
     * every 2.1.3 removal print "Boot sync: timeout" 150 s later and publish an extra boot
     * snapshot. An empty table has nothing to wait for. */
    if (added > 0) {
        s_boot_sync_done       = false;
        s_boot_sync_timeout_ms = sync_window_ms;
        s_boot_start_ms        = now_ms();
    }
    if (total == 0) s_boot_sync_done = true;

    /* 5. Recompute the roll-up NOW so s_system_rating reflects the new set immediately,
     * instead of staying at its stale prior value until the first 30 s tick (a hub with an
     * offline valve would otherwise show GREEN for ~30 s after boot). */
    recalc_system_rating();

    xSemaphoreGive(s_mutex);

    // 6. Logs outside the lock. Keep the "Device table loaded: N device(s)" prefix: it is
    //    the bench grep anchor.
    ESP_LOGI(HEALTH_TAG, "Device table loaded: %d device(s) (+%u added, -%u removed)",
             (int)total, (unsigned)added, (unsigned)removed);
    if (dropped > 0) {
        ESP_LOGE(HEALTH_TAG, "Device table full (%d): %u provisioned device(s) not tracked",
                 HEALTH_MAX_DEVICES, (unsigned)dropped);
    }

    if (out) {
        out->total   = total;
        out->added   = added;
        out->removed = removed;
    }
    return true;
}

void health_engine_init(void)
{
    if (s_initialized) return;

    s_health_queue = xQueueCreate(16, sizeof(health_event_t));
    if (!s_health_queue) {
        ESP_LOGE(HEALTH_TAG, "Failed to create health event queue");
        return;
    }

    // Depth 8, was 4. An alert fires on a TRANSITION, so a rejected send used to
    // lose it permanently (the caller advanced dev->rating regardless, consuming
    // the edge). apply_rating() now holds the rating back on failure so the next
    // tick retries — this depth is the first line of defence, that is the second.
    s_alert_queue = xQueueCreate(8, sizeof(health_alert_t));
    if (!s_alert_queue) {
        ESP_LOGE(HEALTH_TAG, "Failed to create alert queue");
        return;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(HEALTH_TAG, "Failed to create mutex");
        return;
    }

    s_tick_timer = xTimerCreate("health_tick",
                                pdMS_TO_TICKS(HEALTH_TICK_INTERVAL_MS),
                                pdTRUE,   // auto-reload
                                NULL,
                                tick_timer_cb);
    if (!s_tick_timer) {
        ESP_LOGE(HEALTH_TAG, "Failed to create tick timer");
        return;
    }

    health_engine_reconcile_devices(HEALTH_BOOT_SYNC_TIMEOUT_MS, NULL);   // boot window for every device it adds

    xTaskCreate(health_engine_task, "health_engine", 3072, NULL, 2, NULL);
    xTimerStart(s_tick_timer, 0);

    s_initialized = true;
    ESP_LOGI(HEALTH_TAG, "Initialized (tick=%ds, sensor_timeout=%ds)",
             HEALTH_TICK_INTERVAL_MS / 1000,
             HEALTH_LORA_TIMEOUT_MS / 1000);
}

bool health_post_event(const health_event_t *evt)
{
    if (!s_health_queue || !evt) return false;
    return xQueueSend(s_health_queue, evt, 0) == pdTRUE;
}

bool health_request_valve_resync(void)
{
    health_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = HEALTH_EVT_VALVE_RESYNC;
    return health_post_event(&evt);
}

health_rating_t health_get_system_rating(void)
{
    return s_system_rating;
}

void health_set_interlock_held(bool held)
{
    if (s_interlock_held == held) return;   // no transition, nothing to re-roll

    s_interlock_held = held;
    ESP_LOGI(HEALTH_TAG, "Interlock %s — system rating floor %s",
             held ? "HELD (hub holding valve closed)" : "released",
             held ? "WARNING" : "removed");

    /* Nudge the task so the roll-up picks this up now instead of at the next 30 s
     * tick — the fleet LED reads the roll-up, and a third of a minute of stale
     * colour after a leak clears is exactly when someone is watching it.
     *
     * Non-blocking post, and TICK is idempotent (it only re-evaluates timeouts), so
     * a full queue costs nothing: the next real tick re-rolls anyway. Deliberately
     * does NOT take s_mutex — the caller is holding the rules-engine mutex. */
    health_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = HEALTH_EVT_TICK;
    health_post_event(&evt);
}

bool health_is_interlock_held(void)
{
    return s_interlock_held;
}

bool health_pop_alert(health_alert_t *out)
{
    if (!s_alert_queue || !out) return false;
    return xQueueReceive(s_alert_queue, out, 0) == pdTRUE;
}

char *health_alert_to_json(const health_alert_t *alert)
{
    if (!alert) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "category", "health");

    // The kind is carried explicitly. It used to be inferred from new_rating, which
    // stopped being possible once a device can recover INTO a leak- or battery-driven
    // CRITICAL (see is_offline_state()).
    cJSON_AddStringToObject(root, "event",
                            alert->offline ? "device_offline" : "device_recovered");

    cJSON_AddStringToObject(root, "source_type", dev_type_to_source_type(alert->dev_type));
    // valve_id / sensor_id, per the device type — the same key, in the same
    // position, that the snapshot and the leak events use for this device. Health
    // kept a generic device_id until 2.1.0 on the argument that it iterates one
    // heterogeneous table; the branch costs one comparison and removes the last
    // place on the D2C plane where the identity key had a third spelling.
    cJSON_AddStringToObject(root, leak_identity_key(alert->dev_type == HEALTH_DEV_VALVE),
                            alert->dev_id);
    cJSON_AddStringToObject(root, "rating", health_rating_to_str(alert->new_rating));
    cJSON_AddStringToObject(root, "prev_rating", health_rating_to_str(alert->old_rating));

    if (alert->battery != 0xFF) {
        cJSON_AddNumberToObject(root, "battery", alert->battery);
    }
    if (alert->rssi != 0) {
        cJSON_AddNumberToObject(root, "rssi", alert->rssi);
    }
    if (alert->offline_duration_s > 0) {
        cJSON_AddNumberToObject(root, "offline_duration_s", alert->offline_duration_s);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

bool health_get_device_status_all(health_device_status_t out[HEALTH_MAX_DEVICES],
                                  uint8_t *count_out, health_rating_t *sys_rating_out,
                                  bool *syncing_out)
{
    if (!out || !count_out || !s_mutex) return false;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }

    /* Deadlines FIRST, then the copy, then the rating — all under this one hold. The
     * snapshot used to take the table, the syncing flag and the rating in three separate
     * calls, so a deadline could close between them and publish a rating that did not
     * match the "Syncing" text or the device array beside it (L15). */
    check_boot_sync_locked();

    int64_t now = now_ms();
    uint8_t count = 0;
    bool syncing = false;

    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        health_device_status_t *dst = &out[i];
        const health_device_t  *src = &s_devices[i];

        dst->in_use = src->in_use;
        if (!src->in_use) continue;

        dst->dev_type     = src->dev_type;
        memcpy(dst->dev_id, src->dev_id, sizeof(dst->dev_id));
        dst->rating       = src->rating;
        dst->cause        = src->cause;
        dst->ever_seen    = src->ever_seen;
        dst->leaking      = src->leaking;
        dst->last_battery = src->last_battery;
        dst->last_rssi    = src->last_rssi;
        // Same predicate the roll-up uses, so "Syncing" can never disagree with the rating.
        dst->excused      = rollup_unheard_locked(src);
        if (dst->excused) syncing = true;

        // Compute connected status
        if (src->dev_type == HEALTH_DEV_VALVE) {
            dst->connected = src->ever_seen && (src->disconnect_ms == 0);
        } else {
            // Sensor: connected if ever_seen and within timeout
            if (!src->ever_seen || src->last_seen_ms == 0) {
                dst->connected = false;
            } else {
                uint32_t timeout = (src->dev_type == HEALTH_DEV_LORA)
                                    ? HEALTH_LORA_TIMEOUT_MS
                                    : HEALTH_BLE_LEAK_TIMEOUT_MS;
                dst->connected = ((now - src->last_seen_ms) <= timeout);
            }
        }

        // Compute last_seen_age_s
        if (!src->ever_seen || src->last_seen_ms == 0) {
            dst->last_seen_age_s = UINT32_MAX;
        } else {
            dst->last_seen_age_s = (uint32_t)((now - src->last_seen_ms) / 1000);
        }

        count++;
    }

    *count_out = count;
    if (sys_rating_out) *sys_rating_out = s_system_rating;
    if (syncing_out)    *syncing_out    = syncing;
    xSemaphoreGive(s_mutex);
    return true;
}

bool health_is_boot_sync_complete(void)
{
    if (!s_mutex) return true;  // Fail-open if not initialized

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return true;  // Fail-open to avoid stalling iothub
    }

    // Evaluate the deadline on read so the window can complete promptly (within
    // the caller's poll cadence) instead of only at the next 30 s health tick —
    // this bounds the worst-case commission snapshot latency to the window length.
    check_boot_sync_locked();
    bool done = s_boot_sync_done;
    xSemaphoreGive(s_mutex);
    return done;
}

bool health_is_rollup_syncing(void)
{
    /* Fails CLOSED, unlike health_is_boot_sync_complete() which fails open. Both
     * choices avoid latching the caller into "syncing": there it means returning
     * "done", here it means returning "not syncing". The caller then falls through to
     * its rating-based answer, which is the safe direction to be wrong in — a mutex
     * hiccup must never hold the fleet LED white over a real fault. */
    if (!s_mutex) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;

    /* Evaluate the deadlines on read, same as health_is_boot_sync_complete(), so the
     * grace expiry is noticed within the caller's poll cadence rather than at the next
     * 30 s tick. This is what stops a ~30 s GREEN flash before a genuinely absent
     * device escalates to RED. */
    check_boot_sync_locked();

    bool syncing = false;
    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (rollup_unheard_locked(&s_devices[i])) {
            syncing = true;
            break;
        }
    }
    xSemaphoreGive(s_mutex);
    return syncing;
}

uint32_t health_get_checkin_seq(void)
{
    return s_checkin_seq;   /* lock-free: single writer, 32-bit aligned */
}

uint32_t health_get_rating_seq(void)
{
    return s_rating_seq;    /* lock-free read: written only under s_mutex, 32-bit aligned */
}

bool health_is_valve_battery_critical(void)
{
    if (!s_mutex) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;

    const health_device_t *v = find_valve();
    bool crit = v && v->last_battery != 0xFF &&
                v->last_battery <= HEALTH_VALVE_BATTERY_CRIT_PCT;
    xSemaphoreGive(s_mutex);
    return crit;
}

bool health_get_sync_counts(uint8_t *seen, uint8_t *total)
{
    if (!s_mutex) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;

    uint8_t s = 0, t = 0;
    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (s_devices[i].in_use) {
            t++;
            if (s_devices[i].ever_seen) s++;
        }
    }
    if (seen)  *seen  = s;
    if (total) *total = t;
    xSemaphoreGive(s_mutex);
    return true;
}
