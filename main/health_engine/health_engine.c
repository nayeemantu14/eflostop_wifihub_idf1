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
    health_rating_t   prev_rating;
    int64_t           last_seen_ms;     // Monotonic: esp_timer_get_time()/1000
    uint8_t           last_battery;     // 0xFF = unknown
    int8_t            last_rssi;        // 0 = unknown
    int64_t           last_alert_ms;    // Last alert timestamp (debounce)
    bool              ever_seen;        // false until first check-in this uptime
    bool              leaking;          // last reported wet/dry — forces CRITICAL
    bool              crit_is_leak;     // the CRITICAL we are sitting at is leak-driven,
                                        // so the matching recovery must stay silent too
    int64_t           disconnect_ms;    // Valve only: disconnect timestamp, 0 = connected
} health_device_t;

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
static uint32_t s_boot_sync_timeout_ms = HEALTH_BOOT_SYNC_TIMEOUT_MS;  // window length, set on reload
/* Second, LONGER deadline measured from the same s_boot_start_ms: the point at which a
 * device we have never heard from stops being excused and starts counting toward the
 * roll-up. Kept as a latched flag rather than recomputed per call so the transition has a
 * single observable edge to hang a recalc + log on — see check_boot_sync_locked(). */
static bool     s_rollup_grace_done = false;
/* Monotonic count of sensor check-ins the engine has processed. Written ONLY by
 * health_engine_task (single writer), read lock-free by iothub_task via
 * health_get_checkin_seq(). See that declaration for why this exists. */
static volatile uint32_t s_checkin_seq = 0;
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
static health_rating_t compute_sensor_rating(const health_device_t *dev, int64_t now)
{
    if (dev->leaking) {
        return HEALTH_CRITICAL;
    }

    // Check connectivity timeout
    uint32_t timeout_ms = (dev->dev_type == HEALTH_DEV_LORA)
                          ? HEALTH_LORA_TIMEOUT_MS
                          : HEALTH_BLE_LEAK_TIMEOUT_MS;

    if (dev->last_seen_ms == 0 || (now - dev->last_seen_ms) > timeout_ms) {
        return HEALTH_CRITICAL;
    }

    // Online — evaluate battery and signal
    if (dev->last_battery != 0xFF && dev->last_battery <= HEALTH_BATTERY_WARN_PCT) {
        return HEALTH_WARNING;
    }
    if (dev->last_rssi != 0 && dev->last_rssi <= HEALTH_RSSI_WARN_DBM) {
        return HEALTH_WARNING;
    }
    if (dev->last_battery != 0xFF && dev->last_battery <= HEALTH_BATTERY_GOOD_PCT) {
        return HEALTH_GOOD;
    }
    if (dev->last_rssi != 0 &&
        dev->last_rssi > HEALTH_RSSI_WARN_DBM &&
        dev->last_rssi <= HEALTH_RSSI_GOOD_DBM) {
        return HEALTH_GOOD;
    }

    return HEALTH_EXCELLENT;
}

static health_rating_t compute_valve_rating(const health_device_t *dev, int64_t now)
{
    // The valve's own flood probe — same precedence as a sensor leak.
    if (dev->leaking) {
        return HEALTH_CRITICAL;
    }

    // Disconnected: check grace period
    if (dev->disconnect_ms > 0) {
        if ((now - dev->disconnect_ms) >= HEALTH_VALVE_DISC_TIMEOUT_MS)
            return HEALTH_CRITICAL;
        return HEALTH_WARNING;  // Grace period — not yet CRITICAL
    }

    // Never connected this uptime
    if (dev->last_seen_ms == 0) {
        return HEALTH_CRITICAL;
    }

    // Connected — evaluate battery
    if (dev->last_battery != 0xFF && dev->last_battery <= HEALTH_BATTERY_WARN_PCT) {
        return HEALTH_WARNING;
    }
    if (dev->last_battery != 0xFF && dev->last_battery <= HEALTH_BATTERY_GOOD_PCT) {
        return HEALTH_GOOD;
    }

    return HEALTH_EXCELLENT;
}

/* Is this device currently EXCLUDED from the system roll-up purely because we have not
 * heard from it yet? Call with s_mutex held.
 *
 * Every device is seeded CRITICAL by health_engine_reload_devices() and only leaves that
 * state on its first check-in, so an unfiltered max() reported the whole fleet CRITICAL
 * from boot until the LAST sensor's first burst. On the current event-driven sensors that
 * is minutes — one field capture sat RED for 14 min on a hub where all four sensors were
 * fine. "Haven't heard from it yet" is not the same claim as "it is offline", and only
 * the second one should drive a red light.
 *
 * The per-device rating is deliberately left at CRITICAL: that is the honest answer to
 * "what do we know about this device", the snapshot still reports connected:false for it,
 * and once the grace expires it counts again — so a genuinely absent device is still
 * escalated, just not prematurely.
 *
 * TWO CLOCKS, not one. The exclusion holds while the short snapshot window is open AND
 * then for the remainder of HEALTH_ROLLUP_UNHEARD_MS. Using only the snapshot window
 * (the pre-2.1.3 behaviour) meant a healthy sensor that happened to beacon after 180 s
 * turned the LED red and published "1 sensor offline" — reproduced on the bench with a
 * first contact at 265.6 s. See HEALTH_ROLLUP_UNHEARD_MS for why 600 s specifically.
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
    /* No time argument: BOTH deadlines are latched into flags by
     * check_boot_sync_locked(), which every caller reaches first. That keeps the
     * decision a pure function of state, so the roll-up and health_is_rollup_syncing()
     * cannot straddle a deadline and return different answers in one pass. */
    if (!dev->in_use)   return false;
    if (dev->ever_seen) return false;
    if (dev->leaking)   return false;
    if (!s_boot_sync_done) return true;

    /* THE VALVE DOES NOT GET THE EXTENDED GRACE — it ends at the snapshot window edge,
     * exactly as it did before 2.1.3.
     *
     * HEALTH_ROLLUP_UNHEARD_MS is calibrated for the advertising sensors: 600 s covers
     * a ~100 s burst cadence with margin, and it is justified by being the same
     * deadline at which a previously-seen SENSOR is declared offline
     * (HEALTH_BLE_LEAK_TIMEOUT_MS). Neither half of that reasoning transfers to the
     * valve. It sits on a continuous BLE link rather than a burst cadence, so there is
     * no interval to accommodate, and its own offline deadline is
     * HEALTH_VALVE_DISC_TIMEOUT_MS (180 s) — a third of the sensor one.
     *
     * Extending the excuse to the valve would mask the single device the entire leak
     * response depends on: handle_valve_event() sets ever_seen only on CONNECTED, so a
     * valve that is powered off or out of range keeps ever_seen==false and would be
     * excluded from the roll-up for a full 10 minutes — the fleet LED showing WHITE
     * "syncing" and every snapshot publishing rating:"excellent" while the hub has no
     * way to shut the water off. And it re-arms on every provision, i.e. precisely
     * during commissioning, when someone is standing there deciding whether the install
     * works.
     *
     * Note the asymmetry this restores is the right way round: a valve that links once
     * and then drops stamps disconnect_ms, is not excluded at all, and escalates via
     * compute_valve_rating()'s own 180 s grace. Never-connected must not be treated
     * more leniently than connected-then-lost. */
    if (dev->dev_type == HEALTH_DEV_VALVE) return false;

    return !s_rollup_grace_done;
}

static void recalc_system_rating(void)
{
    health_rating_t worst = HEALTH_EXCELLENT;

    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (!s_devices[i].in_use) continue;

        if (rollup_unheard_locked(&s_devices[i])) continue;

        if (s_devices[i].rating > worst) {
            worst = s_devices[i].rating;
        }
    }

    /* WARNING floor while the hub is holding the valve shut on a latched incident.
     * Once the water dries, every device reads healthy again and the roll-up would
     * otherwise go straight back to EXCELLENT/green — while the valve is still
     * closed and the taps are still dry. Amber is the honest middle state: nothing
     * is leaking, but the system has not returned to normal either. */
    if (s_interlock_held && worst < HEALTH_WARNING) {
        worst = HEALTH_WARNING;
    }

    s_system_rating = worst;
}

// ---------------------------------------------------------------------------
// Alert generation
// ---------------------------------------------------------------------------

// Returns true when it is SAFE for the caller to commit the new rating: either no
// alert was warranted, the alert was deliberately suppressed (debounce / boot), or
// the alert was successfully enqueued.
//
// Returns FALSE only when an alert was genuinely required and the queue rejected
// it. That distinction matters: the alert fires on a TRANSITION, so if the caller
// commits the rating anyway the transition is consumed and the alert can never be
// re-detected — it is lost permanently, not merely delayed. See apply_rating().
static bool maybe_enqueue_alert(health_device_t *dev, health_rating_t new_rating, int64_t now)
{
    health_rating_t old_rating = dev->rating;

    // Only alert on Critical transitions (into or out of)
    bool into_critical  = (new_rating == HEALTH_CRITICAL && old_rating != HEALTH_CRITICAL);
    bool out_of_critical = (new_rating != HEALTH_CRITICAL && old_rating == HEALTH_CRITICAL);

    if (!into_critical && !out_of_critical) {
        return true;                 // nothing to raise — commit freely
    }

    // LEAK-DRIVEN CRITICAL RAISES NO HEALTH ALERT.
    //
    // health_alert_to_json() names the event purely from the rating: CRITICAL means
    // "device_offline". That was sound while CRITICAL could only mean staleness or a
    // valve disconnect. Now that a leak forces CRITICAL ahead of the staleness test,
    // the same code would publish `device_offline` for a device that is demonstrably
    // online — in a payload carrying its healthy battery and strong RSSI — and then
    // `device_recovered` when it dried, for a device that never went away.
    //
    // The health channel is about reachability. A leak already has its own D2C event
    // (leak_detected / leak_cleared), its own coupled snapshot, and now the rating and
    // reason string too, so there is nothing left for an alert to add. Suppressing is
    // also the wire-compatible choice: inventing a `device_leak` event would add a
    // value to a vocabulary the cloud validates against.
    //
    // Returning true still commits the rating (see apply_rating) — only the alert is
    // withheld. crit_is_leak remembers WHY we went critical so the matching
    // out-of-critical edge stays silent as well; without it the dry-out would publish
    // a bare `device_recovered` for an offline event that was never sent.
    // crit_is_leak is READ here and MAINTAINED in apply_rating() — deliberately, because
    // it must describe the STATE we are sitting at, not the edge that got us there. Set
    // on the into-critical edge only, it was never set when a leak arrived at a device
    // that was ALREADY critical (seeded CRITICAL after a reload, or genuinely offline),
    // so the eventual dry-out took the out_of_critical branch with the flag false and
    // published a bare `device_recovered` for an offline event that was never sent.
    if (into_critical && dev->leaking)      return true;   // leak: no alert
    if (out_of_critical && dev->crit_is_leak) return true; // its recovery: also silent

    // Suppress boot-time "recovered" alerts — first check-in is not a real recovery
    if (out_of_critical && !dev->ever_seen) {
        return true;                 // deliberately suppressed, not dropped
    }

    // Debounce check
    if (dev->last_alert_ms != 0 &&
        (now - dev->last_alert_ms) < HEALTH_ALERT_DEBOUNCE_MS) {
        return true;                 // deliberately suppressed, not dropped
    }

    // Build alert
    health_alert_t alert;
    memset(&alert, 0, sizeof(alert));
    alert.dev_type    = dev->dev_type;
    strncpy(alert.dev_id, dev->dev_id, sizeof(alert.dev_id) - 1);
    alert.new_rating  = new_rating;
    alert.old_rating  = old_rating;
    alert.battery     = dev->last_battery;
    alert.rssi        = dev->last_rssi;

    if (into_critical && dev->last_seen_ms > 0) {
        alert.offline_duration_s = (uint32_t)((now - dev->last_seen_ms) / 1000);
    }

    if (xQueueSend(s_alert_queue, &alert, 0) != pdTRUE) {
        ESP_LOGE(HEALTH_TAG, "ALERT QUEUE FULL — %s %s %s -> %s held for retry",
                 dev_type_to_source_type(dev->dev_type), dev->dev_id,
                 health_rating_to_str(old_rating),
                 health_rating_to_str(new_rating));
        return false;                // caller must NOT advance dev->rating
    }

    dev->last_alert_ms = now;
    ESP_LOGW(HEALTH_TAG, "ALERT: %s %s %s -> %s",
             dev_type_to_source_type(dev->dev_type), dev->dev_id,
             health_rating_to_str(old_rating),
             health_rating_to_str(new_rating));

    // Wake the publisher. The alert queue is NOT a member of iothub_task's
    // QueueSet, so without this the alert waits for whatever wakes that loop next
    // — up to the 30 s idle cap.
    //
    // Adding the queue to the set instead would be wrong: iothub_task drains
    // alerts in its PUBLISH phase, which sits behind `if (!provisioned) continue;`
    // and behind dps_maintain()/sas_maintain(). On an unprovisioned hub the set
    // would signal on a queue nothing ever reads, and xQueueSelectFromSet would
    // return it immediately every iteration — a busy loop. This wake reuses the
    // snapshot trigger queue, which IS in the set and IS always consumed.
    telemetry_v2_wake_snapshot();
    return true;
}

// Raise whatever alert the transition warrants, then commit the rating — but only
// if the alert survived. Holding the old rating on failure is what allows the next
// evaluation to re-detect the same transition and retry; committing would consume
// it forever.
static void apply_rating(health_device_t *dev, health_rating_t new_rating, int64_t now)
{
    if (!maybe_enqueue_alert(dev, new_rating, now)) return;
    dev->prev_rating = dev->rating;
    dev->rating      = new_rating;
    /* Maintained from the STATE, every commit — so it is correct however we arrived at
     * CRITICAL, including CRITICAL -> CRITICAL where there is no edge for
     * maybe_enqueue_alert() to observe. Reads "the critical I am currently at is
     * leak-driven, so its eventual recovery must stay silent too". */
    dev->crit_is_leak = (new_rating == HEALTH_CRITICAL) ? dev->leaking : false;
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

    health_rating_t new_rating = compute_sensor_rating(dev, now);
    apply_rating(dev, new_rating, now);
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

    health_rating_t new_rating = compute_sensor_rating(dev, now);
    apply_rating(dev, new_rating, now);
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
        // and maybe_enqueue_alert() took the out_of_critical branch with ever_seen
        // already true — publishing `device_recovered` for a valve that never came
        // back. With the latch, a repeat recomputes CRITICAL and falls out of
        // maybe_enqueue_alert() silently.
        //
        // The grace now means "time since the link was last usable", which is what
        // the rating already claimed it meant.
        dev->disconnect_ms = now;  // Start grace period (keep last_seen_ms)
    }

    health_rating_t new_rating = compute_valve_rating(dev, now);
    apply_rating(dev, new_rating, now);
    if (connected) {
        dev->ever_seen = true;
        check_boot_sync_locked();
    }
}

// Valve battery update (from a BLE battery NOTIFY / connect). Refreshes the
// stored battery and re-rates the valve so a low battery is reflected. Owns
// ONLY the battery — connectivity (last_seen_ms/disconnect_ms) stays with the
// connect/disconnect events. Without this the valve path never fed a battery,
// so compute_valve_rating saw last_battery==0xFF and always returned EXCELLENT.
static void handle_valve_battery(uint8_t battery)
{
    health_device_t *dev = find_valve();
    if (!dev) return;
    if (battery == 0xFF) return;   // unknown — ignore

    dev->last_battery = battery;

    int64_t now = now_ms();
    health_rating_t new_rating = compute_valve_rating(dev, now);
    apply_rating(dev, new_rating, now);
}

// Valve flood-probe state change. Owns ONLY `leaking`; connectivity and battery
// stay with their own events, matching how handle_valve_battery() is scoped.
static void handle_valve_leak(bool leaking)
{
    health_device_t *dev = find_valve();
    if (!dev) return;

    dev->leaking = leaking;

    int64_t now = now_ms();
    health_rating_t new_rating = compute_valve_rating(dev, now);
    apply_rating(dev, new_rating, now);
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
        if (dev->dev_type == HEALTH_DEV_VALVE) {
            health_rating_t new_rating = compute_valve_rating(dev, now);
            if (new_rating != dev->rating) {
                apply_rating(dev, new_rating, now);
            }
            continue;
        }

        // Sensors: skip devices never seen (already CRITICAL from init)
        if (dev->last_seen_ms == 0) continue;

        health_rating_t new_rating = compute_sensor_rating(dev, now);

        if (new_rating != dev->rating) {
            apply_rating(dev, new_rating, now);
        }
    }

    check_boot_sync_locked();
}

// ---------------------------------------------------------------------------
// Window deadline evaluation (call with s_mutex held)
//
// Evaluates BOTH sync deadlines measured from s_boot_start_ms:
//   1. s_boot_sync_timeout_ms  (180 s) -> s_boot_sync_done    — the SNAPSHOT gate
//   2. HEALTH_ROLLUP_UNHEARD_MS (600 s) -> s_rollup_grace_done — the ROLL-UP exclusion
// They are separate because they answer different questions; see
// HEALTH_ROLLUP_UNHEARD_MS in the header for the bench capture that forced the split.
// ---------------------------------------------------------------------------
static void check_boot_sync_locked(void)
{
    /* NO early return on s_boot_sync_done any more: deadline 2 is LONGER than deadline
     * 1, so this function has to keep being reachable after the first one has closed.
     * Each flag still has its own one-shot guard below, so there is no repeated work. */
    bool edge = false;

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
            /* State the REMAINING excuse, not the constant. Deadline 2 is measured from
             * the same s_boot_start_ms, so after a commission (150 s window) only 450 s
             * of the 600 s is left — a bench capture read "still excused for 600 s" at
             * t=435 s when the real deadline was t=885 s. A diagnostic line that has to
             * be corrected by hand is worse than no line. */
            ESP_LOGW(HEALTH_TAG, "Boot sync: timeout (%lu s) — snapshot gate open; "
                     "unheard devices still excused for a further %lld s",
                     (unsigned long)(s_boot_sync_timeout_ms / 1000),
                     (long long)(((int64_t)HEALTH_ROLLUP_UNHEARD_MS
                                  - (now_ms() - s_boot_start_ms)) / 1000));
        }
        if (s_boot_sync_done) edge = true;
    }

    /* Deadline 2. Needs its own edge for exactly the same reason deadline 1 does (see
     * below): nothing else recalcs when it passes, so without this the roll-up would
     * keep excluding a genuinely absent device — reporting EXCELLENT / GREEN — until
     * the next 30 s tick happened to come round. */
    if (s_boot_sync_done && !s_rollup_grace_done &&
        (now_ms() - s_boot_start_ms) >= (int64_t)HEALTH_ROLLUP_UNHEARD_MS) {
        s_rollup_grace_done = true;

        /* Announce ONLY when the expiry actually changes something, i.e. some device is
         * still unheard and is about to start counting. On a healthy hub every device has
         * been heard long before this deadline, so the unconditional version printed
         * "unheard devices now count" at WARNING level, 600 s after every boot AND every
         * provision, when there were no unheard devices at all — a line that is both
         * false and alarming, in the channel people scan first when something is wrong.
         * Observed on the 2.1.3 bench capture at t=885 s with all four sensors healthy.
         *
         * The latch itself is still set unconditionally (it is a deadline, not an event)
         * and the recalc below still runs, so behaviour is unchanged — only the noise. */
        int unheard = 0;
        for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
            if (s_devices[i].in_use && !s_devices[i].ever_seen) unheard++;
        }
        if (unheard > 0) {
            ESP_LOGW(HEALTH_TAG,
                     "Roll-up grace expired (%lu s) — %d unheard device(s) now count",
                     (unsigned long)(HEALTH_ROLLUP_UNHEARD_MS / 1000), unheard);
        } else {
            ESP_LOGD(HEALTH_TAG, "Roll-up grace expired (%lu s) — nothing unheard",
                     (unsigned long)(HEALTH_ROLLUP_UNHEARD_MS / 1000));
        }
        edge = true;
    }

    /* Re-roll immediately if either window JUST closed — the one-shot guards above mean
     * a flag can only be set here by the call that sets it, so this is a transition
     * edge, not a per-call cost.
     *
     * Needed because recalc_system_rating() EXCLUDES not-yet-heard devices while these
     * flags are clear, so a flag flipping changes the roll-up's inputs — and this
     * function is reached from health_is_boot_sync_complete() and
     * health_is_rollup_syncing(), i.e. from the fleet LED / iothub poll, which do NOT
     * otherwise recalc.
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

void health_engine_reload_devices(uint32_t sync_window_ms)
{
    bool have_mutex = (s_mutex != NULL);
    if (have_mutex) xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000));

    /* CARRY LEAK STATE ACROSS THE RELOAD.
     *
     * The memset below wipes `leaking` along with everything else, and that single bit
     * is load-bearing now: recalc_system_rating() exempts leaking devices from the
     * not-yet-heard exclusion, so wiping it makes that exemption inert exactly when it
     * is needed. A `provision` or per-device `decommission` issued while a sensor is
     * standing in water would drop that sensor out of the roll-up until its next burst
     * (~100 s for BLE, longer for LoRa) — and with auto_close disabled there is no
     * interlock floor to catch it either, so the hub would publish rating:"excellent" /
     * "Syncing - waiting for N devices" with a wet sensor on the floor. That is the
     * exact D3/D4 failure this release exists to remove.
     *
     * Re-seeding from a producer cache was the alternative, but it depends on another
     * module's cache surviving the reload and on walking two different ones. Carrying
     * the bit here is self-contained and cannot be skipped by a caller.
     *
     * Only `leaking` and `crit_is_leak` are carried. Everything else SHOULD reset: the
     * point of a reload is to re-establish liveness from scratch. */
    typedef struct { health_dev_type_t t; char id[18]; bool leaking; bool crit_is_leak; } leak_carry_t;
    leak_carry_t carry[HEALTH_MAX_DEVICES];
    int carry_n = 0;
    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (!s_devices[i].in_use || !s_devices[i].leaking) continue;
        carry[carry_n].t = s_devices[i].dev_type;
        memcpy(carry[carry_n].id, s_devices[i].dev_id, sizeof(carry[carry_n].id));
        carry[carry_n].leaking = true;
        carry[carry_n].crit_is_leak = s_devices[i].crit_is_leak;
        carry_n++;
    }

    // Clear all entries
    memset(s_devices, 0, sizeof(s_devices));
    int idx = 0;

    // Valve
    char valve_mac[18];
    if (provisioning_get_valve_mac(valve_mac)) {
        s_devices[idx].in_use   = true;
        s_devices[idx].dev_type = HEALTH_DEV_VALVE;
        strncpy(s_devices[idx].dev_id, valve_mac, sizeof(s_devices[idx].dev_id) - 1);
        s_devices[idx].rating      = HEALTH_CRITICAL;  // Until connected
        s_devices[idx].prev_rating = HEALTH_CRITICAL;
        s_devices[idx].last_battery = 0xFF;
        idx++;
    }

    // LoRa sensors
    uint32_t lora_ids[MAX_LORA_SENSORS];
    uint8_t lora_count = 0;
    if (provisioning_get_lora_sensors(lora_ids, &lora_count)) {
        for (int i = 0; i < lora_count && idx < HEALTH_MAX_DEVICES; i++) {
            s_devices[idx].in_use   = true;
            s_devices[idx].dev_type = HEALTH_DEV_LORA;
            snprintf(s_devices[idx].dev_id, sizeof(s_devices[idx].dev_id),
                     "0x%08lX", (unsigned long)lora_ids[i]);
            s_devices[idx].rating      = HEALTH_CRITICAL;  // Until first packet
            s_devices[idx].prev_rating = HEALTH_CRITICAL;
            s_devices[idx].last_battery = 0xFF;
            idx++;
        }
    }

    // BLE leak sensors
    char ble_macs[MAX_BLE_LEAK_SENSORS][18];
    uint8_t ble_count = 0;
    if (provisioning_get_ble_leak_sensors(ble_macs, &ble_count)) {
        for (int i = 0; i < ble_count && idx < HEALTH_MAX_DEVICES; i++) {
            s_devices[idx].in_use   = true;
            s_devices[idx].dev_type = HEALTH_DEV_BLE_LEAK;
            strncpy(s_devices[idx].dev_id, ble_macs[i], sizeof(s_devices[idx].dev_id) - 1);
            s_devices[idx].rating      = HEALTH_CRITICAL;  // Until first advertisement
            s_devices[idx].prev_rating = HEALTH_CRITICAL;
            s_devices[idx].last_battery = 0xFF;
            idx++;
        }
    }

    s_boot_sync_done = false;  // Reset boot sync on reload
    /* Re-open the roll-up grace too. Both deadlines are anchored to s_boot_start_ms
     * below, so a `provision` restarts BOTH — which is what we want: the newly
     * commissioned devices have genuinely not been heard yet and deserve the same
     * excuse a cold boot gets. Forgetting this reset would leave the grace latched
     * closed from a previous cycle and put a freshly provisioned hub straight to RED. */
    s_rollup_grace_done = false;
    s_boot_sync_timeout_ms = sync_window_ms;  // window length for this cycle (boot vs commission)
    s_boot_start_ms  = now_ms();  // Restart the sync window from THIS reload (boot OR a
                                  // `provision` command) so the "wait for all commissioned
                                  // devices to be heard" budget is anchored to the
                                  // commission event, not to power-on. Without this a
                                  // provision later than one window after boot would fire
                                  // the snapshot immediately with devices not yet re-heard.

    /* Restore carried leak state onto any device that is STILL provisioned. A device
     * that was removed by this reload simply does not match and is correctly forgotten.
     * Re-rate it immediately so the roll-up below sees CRITICAL rather than the
     * seeded value. */
    for (int c = 0; c < carry_n; c++) {
        health_device_t *dev = find_device(carry[c].t, carry[c].id);
        if (!dev) continue;
        dev->leaking      = true;
        dev->crit_is_leak = carry[c].crit_is_leak;
        dev->rating       = HEALTH_CRITICAL;
        dev->prev_rating  = HEALTH_CRITICAL;
        ESP_LOGW(HEALTH_TAG, "Reload: carried active leak for %s", dev->dev_id);
    }

    ESP_LOGI(HEALTH_TAG, "Device table loaded: %d device(s)", idx);

    // Recompute the roll-up NOW so s_system_rating reflects the freshly-loaded
    // device set immediately, instead of staying at its stale prior value until
    // the first 30 s tick. Without this, every device is CRITICAL "until seen"
    // here but the worst-of roll-up read by the fleet LED / snapshot
    // (health_get_system_rating) still reports the power-on default EXCELLENT,
    // so a hub with an offline valve shows GREEN for ~30 s before flipping RED.
    // Safe under the held mutex: recalc_system_rating only reads s_devices[].
    recalc_system_rating();

    if (have_mutex) xSemaphoreGive(s_mutex);
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

    health_engine_reload_devices(HEALTH_BOOT_SYNC_TIMEOUT_MS);   // boot window; also stamps s_boot_start_ms

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

    bool is_offline = (alert->new_rating == HEALTH_CRITICAL);
    cJSON_AddStringToObject(root, "event",
                            is_offline ? "device_offline" : "device_recovered");

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
                                  uint8_t *count_out)
{
    if (!out || !count_out || !s_mutex) return false;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }

    int64_t now = now_ms();
    uint8_t count = 0;

    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        health_device_status_t *dst = &out[i];
        const health_device_t  *src = &s_devices[i];

        dst->in_use = src->in_use;
        if (!src->in_use) continue;

        dst->dev_type     = src->dev_type;
        memcpy(dst->dev_id, src->dev_id, sizeof(dst->dev_id));
        dst->rating       = src->rating;
        dst->ever_seen    = src->ever_seen;
        dst->leaking      = src->leaking;
        dst->last_battery = src->last_battery;
        dst->last_rssi    = src->last_rssi;

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
