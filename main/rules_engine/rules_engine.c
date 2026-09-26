#include "rules_engine.h"
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "nvs.h"
#include "nvs_store/nvs_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "provisioning_manager.h"
#include "app_ble_valve.h"
#include "sensor_meta.h"
#include "health_engine.h"   /* health_set_interlock_held — YELLOW floor while holding the valve shut */

#define RULES_TAG "RULES_ENGINE"
#define AUTO_CLOSE_COOLDOWN_MS 10000   // 10s cooldown between auto-closes
#define AUTO_CLEAR_TIMEOUT_MS  (30 * 1000)  // 30s all-clear before RMLEAK auto-reset
#define RMLEAK_GRACE_PERIOD_MS 5000    // 5s grace after RMLEAK write before checking valve state

// ─── 24-Hour Water Access Override Window ────────────────────────────────────
// When the user physically overrides the valve (clears RMLEAK via button press),
// all automatic valve closures are blocked for 24 hours. This gives the user
// guaranteed water access while still reporting leaks to the cloud. The window
// can be cancelled early by a C2D "override_cancel" command or LEAK_RESET.
/* Production = 24h. A bench/debug build may shorten this to test window expiry
 * by compiling with -D OVERRIDE_WINDOW_DURATION_S=<seconds>. */
#ifndef OVERRIDE_WINDOW_DURATION_S
#define OVERRIDE_WINDOW_DURATION_S   (24 * 60 * 60)  // 24 hours
#endif
#define OVERRIDE_BLOCKED_COOLDOWN_MS 60000            // Rate-limit "blocked" telemetry to 1/min
/* Bounded BLE reconnect attempt for a remote override_enable: the valve must be
 * reachable before the window is committed (window starts at execution, not
 * receipt). Sized to stay within the app's 20s cmd_ack timeout. */
#define OVERRIDE_CONNECT_TIMEOUT_MS  10000
#define NVS_OVERRIDE_NAMESPACE       "rules_eng"
#define NVS_KEY_OVR_STATE            "ovr_state"
#define NVS_KEY_OVR_EXPIRY           "ovr_expiry"
/* Persist the incident latch so a hub reboot doesn't lose the fact that
 * a leak incident was active. Combined with the valve's persisted RMLEAK,
 * this lets us detect an off-line physical override on reconnect. */
#define NVS_KEY_INCIDENT             "incident"

// Minimum epoch value to consider time "synced" (2024-01-01 00:00:00 UTC)
#define EPOCH_VALID_THRESHOLD        1704067200

// ─── Override State Machine ──────────────────────────────────────────────────
// OVERRIDE_STATE_INACTIVE:  Normal auto-close behavior. All rules apply.
// OVERRIDE_STATE_ACTIVE:    24h override window active. Auto-close blocked.
//                           Leaks still tracked + reported to cloud.
typedef enum {
    OVERRIDE_STATE_INACTIVE = 0,
    OVERRIDE_STATE_ACTIVE   = 1,
} override_state_t;

static bool g_initialized = false;
static bool g_auto_close_triggered = false;
static bool g_leak_incident_active = false;  // Latched: stays true until explicit LEAK_RESET
/* Last value written to / read from NVS for the incident latch. -1 = unknown, so
 * the first save always writes. Lets incident_save_to_nvs() skip redundant flash
 * traffic on paths that re-latch an already-latched incident. */
static int8_t g_incident_persisted = -1;
static TickType_t g_last_auto_close_tick = 0;
/* Separate cooldown stamp for the reconnect-reconciliation auto_close event.
 * Deliberately NOT shared with g_last_auto_close_tick: evaluate_leak() stamps that
 * one when it first detects the leak, so sharing it would suppress the event on the
 * FIRST reconnect after a leak found while the valve was offline — the one reconnect
 * event that actually carries new information. */
static TickType_t g_last_reconnect_close_tick = 0;
static TickType_t g_rmleak_assert_tick = 0;  // When RMLEAK was last written — grace period for override check
static SemaphoreHandle_t g_mutex = NULL;

// 24h override window state
static override_state_t g_override_state = OVERRIDE_STATE_INACTIVE;
static time_t g_override_window_expiry = 0;  // Unix epoch when window expires (0 = inactive)
static TickType_t g_last_blocked_event_tick = 0;
/* Uptime basis for a window stamped while the wall clock was not yet synced. RAM only:
 * ovr_state / ovr_expiry keep their exact meaning, so a rollback reads them as before.
 * Set = this boot started (or refreshed) the window from an unsynced clock, so
 * g_override_start_uptime_s is exact and the tick re-bases the window at the first
 * valid clock. A window restored unsynced from NVS leaves it clear, start uptime 0. */
static bool g_override_unsynced_this_boot = false;
static uint32_t g_override_start_uptime_s = 0;  // esp_timer seconds at window start

// Tracks valve ready-state transitions to detect reconnect in tick()
static bool g_valve_was_ready = false;

// Pending auto-close telemetry (built by rules engine, consumed by IoT Hub)
static char *g_pending_telemetry = NULL;

// Active leak source tracking for auto-clear timeout
#define MAX_ACTIVE_LEAK_SOURCES 16
static char g_active_leak_ids[MAX_ACTIVE_LEAK_SOURCES][18];
static uint8_t g_active_leak_count = 0;
static TickType_t g_all_clear_since = 0;  // 0 = not yet all clear

// ─── Override Window Time Basis ──────────────────────────────────────────────
// Before the first SNTP sync after a power-on, time() is roughly seconds since boot,
// so a window started then is stamped with an expiry in 1970. Against a synced clock
// that expiry has long passed (the window ended the moment SNTP landed), and with no
// internet the tick never saw a valid clock to expire it at all. Such a window is
// timed on esp_timer uptime instead, until the tick re-bases it to the synced clock.
// A real-epoch window restored after a power-on that lost the clock has the same
// problem the other way round, and is timed from that power-on until the sync.

static uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

// True while the ACTIVE window's expiry was stamped from an unsynced clock and has not
// been re-based yet. A real-epoch expiry never is. Must be called with g_mutex held
// (or from init).
static bool override_on_uptime_basis(void)
{
    return g_override_state == OVERRIDE_STATE_ACTIVE &&
           g_override_window_expiry < EPOCH_VALID_THRESHOLD;
}

// Remaining seconds of an uptime-basis window, clamped at 0. Must be called with
// g_mutex held (or from init), and only when override_on_uptime_basis().
//  - Started this boot: exact, from the start uptime.
//  - Restored from NVS: its elapsed time is unknown, so it counts from this boot's
//    power-on (start uptime 0), capped by the unsynced clock itself. That clock
//    survives a software reset (expiry - now is exact across one) and only overstates
//    after a power-on; without the cap a hub with no internet that restarted more often
//    than every 24h would hold the window open forever.
//  - Restored, once the clock is valid: 0. The next tick expires it, which fails toward
//    auto-close exactly as 2.1.3 did.
static int32_t override_uptime_remaining_s(time_t now)
{
    if (!g_override_unsynced_this_boot && now >= EPOCH_VALID_THRESHOLD) {
        return 0;
    }
    uint32_t elapsed = uptime_s() - g_override_start_uptime_s;
    int32_t remaining = (elapsed >= (uint32_t)OVERRIDE_WINDOW_DURATION_S)
        ? 0 : (int32_t)((uint32_t)OVERRIDE_WINDOW_DURATION_S - elapsed);
    if (!g_override_unsynced_this_boot) {
        time_t left = g_override_window_expiry - now;
        if (left < remaining) {
            remaining = (left > 0) ? (int32_t)left : 0;
        }
    }
    return remaining;
}

// Remaining seconds of an ACTIVE window with a REAL-epoch expiry while the clock is not
// synced, clamped at 0. Only a window restored across a power-on (or brownout) that lost
// the clock gets here: start_override_window() never stamps a real epoch unsynced, and a
// synced clock survives every other reset. Its expiry cannot be compared with anything
// until SNTP lands, and with no internet that used to block auto-close for as long as the
// outage lasted. So it is timed from that power-on, like a restored uptime-basis window.
// That fails toward auto-close without ending the window early: it started before the
// power-on. Elapsed is the larger of this boot's uptime and the unsynced clock, which
// counts from the same power-on and survives a software reset, so a hub that restarts
// more often than every 24h cannot hold the window open for ever either. Must be called
// with g_mutex held (or from init), unsynced, and only when !override_on_uptime_basis().
static int32_t override_unsynced_epoch_remaining_s(time_t now)
{
    uint32_t elapsed = uptime_s();
    if (now > 0 && (uint32_t)now > elapsed) {
        elapsed = (uint32_t)now;
    }
    return (elapsed >= (uint32_t)OVERRIDE_WINDOW_DURATION_S)
        ? 0 : (int32_t)((uint32_t)OVERRIDE_WINDOW_DURATION_S - elapsed);
}

// ─── NVS Persistence for Override Window ─────────────────────────────────────

static void override_clear_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PROV_PARTITION, NVS_OVERRIDE_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return;
    nvs_erase_key(h, NVS_KEY_OVR_STATE);
    nvs_erase_key(h, NVS_KEY_OVR_EXPIRY);
    nvs_commit(h);
    nvs_close(h);
}

static void override_save_to_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PROV_PARTITION, NVS_OVERRIDE_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(RULES_TAG, "NVS open failed for override save: %s", esp_err_to_name(err));
        return;
    }
    nvs_set_u8(h, NVS_KEY_OVR_STATE, (uint8_t)g_override_state);
    // time_t is 32-bit on ESP32 — store as u32 (valid until 2038)
    nvs_set_u32(h, NVS_KEY_OVR_EXPIRY, (uint32_t)g_override_window_expiry);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(RULES_TAG, "NVS: override state=%d expiry=%ld saved",
             g_override_state, (long)g_override_window_expiry);
}

/* Persist the incident latch, and mirror it to the health engine.
 *
 * This is deliberately the ONE place both of those happen. Every runtime
 * `g_leak_incident_active = ...` site in this file but one is immediately followed by
 * a call to this function (14 assignments / 14 callsites at 2.1.4), so routing the
 * health mirror through here means a future mutation cannot silently skip it the
 * way it could with a dozen parallel call pairs.
 *
 * The one deliberate exception is rules_engine_reset_all(): it ERASES the key instead
 * of writing 0, and releases the health floor itself. (incident_load_from_nvs() also
 * assigns the latch, but it is the read side; rules_engine_init() mirrors the restored
 * value to health.)
 *
 * The health side is the YELLOW floor: while the hub is holding the valve closed,
 * system health reads WARNING even though every device is individually fine, so the
 * fleet LED shows amber rather than green while the water is still off. */
static void incident_save_to_nvs(void)
{
    /* Lock-free volatile store on the health side — safe to call with g_mutex
     * held, and it cannot participate in a lock cycle (health_engine.c never
     * calls back into the rules engine). */
    health_set_interlock_held(g_leak_incident_active);

    /* Skip the flash round trip when the stored value already matches. The
     * previous comment here promised the write rate was bounded because
     * "incidents transition at most a few times per leak event" — a 2.1.1 field
     * capture recorded ~40 commits/minute from a reconcile path that re-latched an
     * already-latched incident on every pass. The invariant needed a guard rather
     * than a promise; this also removes the nvs_open/commit/close churn, which
     * costs far more than the cell write it was protecting. */
    int8_t want = g_leak_incident_active ? 1 : 0;
    if (g_incident_persisted == want) {
        return;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PROV_PARTITION, NVS_OVERRIDE_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(RULES_TAG, "NVS open failed for incident save: %s", esp_err_to_name(err));
        return;   /* leave g_incident_persisted stale so the next call retries */
    }
    esp_err_t serr = nvs_set_u8(h, NVS_KEY_INCIDENT, (uint8_t)want);
    esp_err_t cerr = nvs_commit(h);
    nvs_close(h);
    if (serr != ESP_OK || cerr != ESP_OK) {
        /* Do NOT advance the cache on a failed write — doing so would convince every
         * later call that the value was already persisted and permanently stop
         * retrying, silently losing the incident latch across the next reboot. */
        ESP_LOGE(RULES_TAG, "NVS: incident save failed (set=%s commit=%s) — will retry",
                 esp_err_to_name(serr), esp_err_to_name(cerr));
        return;
    }
    g_incident_persisted = want;
    ESP_LOGD(RULES_TAG, "NVS: incident=%d saved", g_leak_incident_active);
}

static void incident_load_from_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PROV_PARTITION, NVS_OVERRIDE_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        g_leak_incident_active = false;
        return;
    }
    uint8_t v = 0;
    err = nvs_get_u8(h, NVS_KEY_INCIDENT, &v);
    nvs_close(h);
    g_leak_incident_active = (err == ESP_OK && v != 0);
    /* Seed the write-skip cache so the first save after boot doesn't rewrite the
     * value we just read. Only when the read actually succeeded — a missing key
     * leaves it unknown so the first real transition is definitely written. */
    if (err == ESP_OK) {
        g_incident_persisted = (v != 0) ? 1 : 0;
    }
    if (g_leak_incident_active) {
        ESP_LOGW(RULES_TAG, "NVS: restored incident latch — pending reconcile with valve");
    }
}

static void override_load_from_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PROV_PARTITION, NVS_OVERRIDE_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        // No stored data — start fresh
        g_override_state = OVERRIDE_STATE_INACTIVE;
        g_override_window_expiry = 0;
        return;
    }

    uint8_t state = 0;
    uint32_t expiry = 0;
    err = nvs_get_u8(h, NVS_KEY_OVR_STATE, &state);
    if (err != ESP_OK) state = 0;
    err = nvs_get_u32(h, NVS_KEY_OVR_EXPIRY, &expiry);
    if (err != ESP_OK) expiry = 0;
    nvs_close(h);

    if (state == OVERRIDE_STATE_ACTIVE && expiry > 0) {
        time_t now;
        time(&now);

        if (now >= EPOCH_VALID_THRESHOLD && (time_t)expiry <= now) {
            // Window already expired — clear it
            ESP_LOGW(RULES_TAG, "NVS: override window expired during power-off (expiry=%lu, now=%ld)",
                     (unsigned long)expiry, (long)now);
            if ((time_t)expiry < EPOCH_VALID_THRESHOLD) {
                ESP_LOGW(RULES_TAG, "NVS: that window was stamped before a clock sync - elapsed time unknown, not restored");
            }
            g_override_state = OVERRIDE_STATE_INACTIVE;
            g_override_window_expiry = 0;
            override_clear_nvs();
        } else {
            // Window still active (or time not synced yet — defer check to tick)
            g_override_state = OVERRIDE_STATE_ACTIVE;
            g_override_window_expiry = (time_t)expiry;
            // Stamped unsynced in an earlier boot (expiry below the threshold): not this
            // boot's, so it is timed from this boot's power-on (start uptime 0).
            g_override_unsynced_this_boot = false;
            g_override_start_uptime_s = 0;
            int32_t remaining = (now >= EPOCH_VALID_THRESHOLD)
                ? (int32_t)(g_override_window_expiry - now) : -1;
            if (override_on_uptime_basis()) {
                remaining = override_uptime_remaining_s(now);
            } else if (now < EPOCH_VALID_THRESHOLD) {
                remaining = override_unsynced_epoch_remaining_s(now);  // clock lost at power-on
            }
            ESP_LOGW(RULES_TAG, "NVS: restored override window (expiry=%lu, remaining=%lds)",
                     (unsigned long)expiry, (long)remaining);
        }
    } else {
        g_override_state = OVERRIDE_STATE_INACTIVE;
        g_override_window_expiry = 0;
    }
}

// Start or refresh the 24h override window.  Must be called with g_mutex held.
// `trigger` records the activation source in telemetry:
//   "button"      — physical valve button (tick / reconnect detection paths)
//   "c2d_command" — remote app-initiated override_enable command
static void start_override_window(const char *trigger)
{
    time_t now;
    time(&now);
    bool synced = (now >= EPOCH_VALID_THRESHOLD);

    override_state_t prev_state = g_override_state;
    g_override_state = OVERRIDE_STATE_ACTIVE;
    g_override_window_expiry = now + OVERRIDE_WINDOW_DURATION_S;
    // Unsynced clock: the expiry above is seconds-since-boot based, so also time the
    // window on uptime. The tick re-bases it at the first valid clock (RAM only).
    g_override_unsynced_this_boot = !synced;
    g_override_start_uptime_s = synced ? 0 : uptime_s();

    override_save_to_nvs();

    if (prev_state == OVERRIDE_STATE_INACTIVE) {
        ESP_LOGW(RULES_TAG, "OVERRIDE WINDOW STARTED: auto-close blocked for 24h (expiry=%ld)",
                 (long)g_override_window_expiry);
    } else {
        ESP_LOGW(RULES_TAG, "OVERRIDE WINDOW REFRESHED: timer reset to 24h (expiry=%ld)",
                 (long)g_override_window_expiry);
    }
    if (!synced) {
        ESP_LOGW(RULES_TAG, "Override window stamped before clock sync - timed on uptime (start=%lus) until the clock syncs",
                 (unsigned long)g_override_start_uptime_s);
    }

    // Build telemetry event
    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root, "event", "water_access_override_enabled");
        cJSON_AddStringToObject(root, "trigger", trigger ? trigger : "button");
        // expires_ts is an absolute epoch: never from an unsynced clock, where it would
        // name an instant in 1970. remaining_s still carries the duration.
        if (synced) {
            cJSON_AddNumberToObject(root, "expires_ts", (double)g_override_window_expiry);
        }
        cJSON_AddNumberToObject(root, "remaining_s", (double)OVERRIDE_WINDOW_DURATION_S);
        if (g_pending_telemetry) free(g_pending_telemetry);
        g_pending_telemetry = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
    }
}

// Cancel the override window and return to normal auto-close. Must be called with g_mutex held.
// Returns the remaining seconds at time of cancellation (for telemetry).
static int32_t cancel_override_window(void)
{
    int32_t remaining = 0;
    if (g_override_state == OVERRIDE_STATE_ACTIVE) {
        time_t now;
        time(&now);
        if (override_on_uptime_basis()) {
            remaining = override_uptime_remaining_s(now);
        } else if (now >= EPOCH_VALID_THRESHOLD && g_override_window_expiry > now) {
            remaining = (int32_t)(g_override_window_expiry - now);
        } else if (now < EPOCH_VALID_THRESHOLD) {
            remaining = override_unsynced_epoch_remaining_s(now);  // clock lost at power-on
        }
    }

    g_override_state = OVERRIDE_STATE_INACTIVE;
    g_override_window_expiry = 0;
    g_override_unsynced_this_boot = false;
    override_clear_nvs();

    ESP_LOGW(RULES_TAG, "OVERRIDE WINDOW CANCELLED (remaining_s=%ld)",
             (long)remaining);
    return remaining;
}

// ─── Helpers ────────────────────────────────────────────────────────────────

static uint8_t source_to_trigger_bit(leak_source_t source)
{
    switch (source) {
        case LEAK_SOURCE_BLE:         return RULES_TRIGGER_BLE_LEAK;
        case LEAK_SOURCE_LORA:        return RULES_TRIGGER_LORA;
        case LEAK_SOURCE_VALVE:       return RULES_TRIGGER_VALVE_FLOOD;
        default:                      return 0;
    }
}

// Wire spelling of a water-detection source — the ONLY definition of the
// device-type vocabulary. Public (declared in rules_engine.h) so telemetry_v2.c
// and health_engine.c emit these exact strings rather than keeping copies that
// can drift; every outbound message carries them under the same key, source_type.
const char *leak_source_to_str(leak_source_t source)
{
    switch (source) {
        case LEAK_SOURCE_BLE:   return "ble_leak_sensor";
        case LEAK_SOURCE_LORA:  return "lora";
        case LEAK_SOURCE_VALVE: return "valve";
        default:                return "unknown";
    }
}

// Wire spelling of the identity KEY — the ONLY definition, shared with
// telemetry_v2.c (leak events, snapshots, valve_state_changed) and
// health_engine.c (device_offline / device_recovered). See rules_engine.h for
// why this takes a bool rather than a leak_source_t.
const char *leak_identity_key(bool is_valve)
{
    return is_valve ? "valve_id" : "sensor_id";
}

// Wire identity for a leak source.
//
// Sensors are tracked internally under their real id, so they pass straight
// through. The valve is tracked under the stable pseudo-id VALVE_SOURCE_ID (the
// active-leak table matches by string and must survive a BLE dropout), but the
// cloud joins events to devices on the id VALUE — so on the wire the valve must
// report the same MAC that its leak event, the snapshot (data.valve.valve_id)
// and its health alerts report. Without this an auto_close would name a device
// that appears nowhere else in the incident.
//
// The KEY is chosen by the same valve-vs-sensor test, via leak_identity_key().
// Until 2.1.0 these hub-generated events kept a generic device_id, on the
// argument that the rules engine reasons about a heterogeneous source and should
// not adopt a type-specific key. That argument served the emitter, not the
// consumer: it left the backend switching on the message name to know which key
// held the identity, and it put a third spelling of one concept on the wire
// beside valve_id and sensor_id. The emitter already performs the test — it must,
// to resolve the MAC at all — so naming the key from it costs nothing.
//
// The valve's wire identity is the PROVISIONED MAC only (L18). It used to prefer
// ble_valve_get_mac(), the MAC of whatever valve was linked — which until 2.1.4 could
// be a neighbour's valve linked by name, so an event could name a device this hub does
// not own. provisioning_get_valve_mac() also works while disconnected (an auto_close
// can fire around a dropout) and returns the same upper-case normalised string every
// other message carries, so one incident names the valve the same way throughout.
#define WIRE_DEVICE_ID_BUF  18   // "XX:XX:XX:XX:XX:XX" + NUL

// Returns NULL when no real identity exists, so callers OMIT the key rather than
// shipping a placeholder. It used to fall back to the literal "valve", which put
// a device on the wire that matches nothing in any snapshot, twin or other event
// — worse for joining than no identity at all, because it looks like a real id.
static const char *wire_device_id(const char *source_id, char *buf)
{
    if (source_id && strcmp(source_id, VALVE_SOURCE_ID) == 0) {
        if (provisioning_get_valve_mac(buf))
            return buf;
        return NULL;                  // no valve provisioned (or provisioning busy)
    }
    return source_id;                 // sensors carry their own id (may be NULL)
}

// valve_id / sensor_id, emitted only when the identity resolves. Keeps every call
// site honest without repeating the NULL check or the key selection.
static void add_device_id(cJSON *root, const char *source_id, char *buf)
{
    bool is_valve  = (source_id && strcmp(source_id, VALVE_SOURCE_ID) == 0);
    const char *id = wire_device_id(source_id, buf);
    if (id) {
        cJSON_AddStringToObject(root, leak_identity_key(is_valve), id);
    } else if (is_valve) {
        // Say so. On a valve-less hub this is expected, but the SAME NULL comes
        // back if provisioning_get_valve_mac() merely timed out on its mutex —
        // and then a valve-equipped hub ships an unattributable event, which is
        // the failure this release exists to remove. Without this line the two
        // cases are indistinguishable from the log.
        ESP_LOGW(RULES_TAG, "No valve identity available — event emitted without valve_id");
    }
}

// Identity for the RMLEAK interlock events, which are always about the valve —
// so they always carry valve_id.
//
// Deliberately NO source_type. Everywhere else on the wire source_type answers
// "what kind of device DETECTED the water" — and the incident these events close
// may well have been latched by a BLE or LoRa sensor. Emitting source_type:"valve"
// here would make one key mean two different things, and a backend grouping an
// incident on (source_type, identity) would split it in half. Since 2.1.0 the key
// itself names the device type, so dropping source_type costs no information: an
// event carrying valve_id is about the valve whether or not source_type says so.
//
// Omitted entirely on a hub provisioned with sensors and no valve, which is a
// supported configuration in which these events are still reachable.
static void add_interlock_device_id(cJSON *root)
{
    char idbuf[WIRE_DEVICE_ID_BUF];
    add_device_id(root, VALVE_SOURCE_ID, idbuf);
}

// A false return from ble_valve_connect / _close / _open / _set_rmleak means either that
// no valve is provisioned (the valve module then refuses every command: P0-a/c) or that
// its command queue is full. Say which: the first is routine on a sensors-only hub, the
// second never is.
static void valve_cmd_not_sent(const char *what, const char *consequence)
{
    if (!ble_valve_has_target_mac())
        ESP_LOGW(RULES_TAG, "%s not sent - no provisioned valve", what);
    else
        ESP_LOGE(RULES_TAG, "%s enqueue FAILED — %s", what, consequence);
}

static sensor_type_t source_to_sensor_type(leak_source_t source)
{
    switch (source) {
        case LEAK_SOURCE_BLE:  return SENSOR_TYPE_BLE_LEAK;
        case LEAK_SOURCE_LORA: return SENSOR_TYPE_LORA;
        default:               return SENSOR_TYPE_BLE_LEAK;  // valve has no metadata
    }
}

// Must be called with g_mutex held
static void track_leak_source(const char *source_id, bool leak_active)
{
    if (!source_id) return;

    if (leak_active) {
        // Add to tracking table if not already present
        for (int i = 0; i < g_active_leak_count; i++) {
            if (strcmp(g_active_leak_ids[i], source_id) == 0) return;
        }
        if (g_active_leak_count < MAX_ACTIVE_LEAK_SOURCES) {
            strncpy(g_active_leak_ids[g_active_leak_count], source_id, 17);
            g_active_leak_ids[g_active_leak_count][17] = '\0';
            g_active_leak_count++;
        }
        g_all_clear_since = 0;  // Reset timer whenever any source reports leak
    } else {
        // Remove from tracking table
        for (int i = 0; i < g_active_leak_count; i++) {
            if (strcmp(g_active_leak_ids[i], source_id) == 0) {
                for (int j = i; j < g_active_leak_count - 1; j++) {
                    strcpy(g_active_leak_ids[j], g_active_leak_ids[j + 1]);
                }
                g_active_leak_count--;
                break;
            }
        }
        if (g_active_leak_count == 0) {
            // Cancel any pending auto-close commands that haven't been applied yet.
            // This prevents stale CLOSE/RMLEAK from firing on reconnect after the
            // leak has already resolved. Only cancels if auto-close set them.
            if (g_auto_close_triggered) {
                ble_valve_cancel_pending_close();
                ESP_LOGI(RULES_TAG, "All leaks resolved — pending auto-close cancelled");
                g_auto_close_triggered = false;
            }
            // Note: override window is NOT lifted when sensors clear — it is purely
            // time-based (24h) and only cleared by expiry, C2D override_cancel, or LEAK_RESET.

            // If incident is active, start auto-clear timer
            if (g_leak_incident_active && g_all_clear_since == 0) {
                g_all_clear_since = xTaskGetTickCount();
                if (g_all_clear_since == 0) g_all_clear_since = 1;  // Avoid sentinel confusion
                ESP_LOGI(RULES_TAG, "All sensors clear — auto-clear timer started (%ds)",
                         AUTO_CLEAR_TIMEOUT_MS / 1000);
            }
        }
    }
}

// rmleak_issued: whether the valve was actually reachable, so the RMLEAK+close
// writes are genuinely being sent. It used to be a hardcoded true, emitted from
// here — which runs BEFORE the connectivity test — so an auto_close raised while
// the valve was offline still told the cloud the interlock had been applied. On
// the bench that produced an auto_close claiming rmleak_asserted:true alongside a
// snapshot 300 ms later reporting connected:false. The false claim is the one an
// alerting rule reads as success, which is the worst way to be wrong.
static void build_auto_close_telemetry(leak_source_t source, const char *source_id,
                                       bool rmleak_issued)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    char idbuf[WIRE_DEVICE_ID_BUF];
    cJSON_AddStringToObject(root, "event", "auto_close");
    cJSON_AddStringToObject(root, "source_type", leak_source_to_str(source));
    add_device_id(root, source_id, idbuf);
    cJSON_AddBoolToObject(root, "rmleak_asserted", rmleak_issued);

    // Add location if available
    if (source_id && source != LEAK_SOURCE_VALVE) {
        sensor_meta_entry_t meta;   // a copy, never a pointer into the table (L16)
        if (sensor_meta_get(source_to_sensor_type(source), source_id, &meta)) {
            cJSON *loc = cJSON_CreateObject();
            cJSON_AddStringToObject(loc, "code",
                sensor_meta_location_code_to_str(meta.location_code));
            cJSON_AddStringToObject(loc, "label", meta.label);
            cJSON_AddItemToObject(root, "location", loc);
        }
    }

    // Free any previous pending telemetry
    if (g_pending_telemetry) {
        free(g_pending_telemetry);
    }
    g_pending_telemetry = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
}

// ─── Public API ─────────────────────────────────────────────────────────────

void rules_engine_init(void)
{
    if (g_initialized) return;

    g_mutex = xSemaphoreCreateMutex();
    if (!g_mutex) {
        ESP_LOGE(RULES_TAG, "Failed to create mutex");
        return;
    }

    rules_config_t rules;
    if (provisioning_get_rules_config(&rules)) {
        ESP_LOGI(RULES_TAG, "Initialized: auto_close=%s triggers=0x%02X",
                 rules.auto_close_enabled ? "enabled" : "disabled",
                 rules.trigger_mask);
    } else {
        ESP_LOGI(RULES_TAG, "Initialized with defaults (auto_close=enabled triggers=ALL)");
    }

    // Restore override window from NVS (survives power cycle)
    override_load_from_nvs();

    /* Restore the incident latch so a hub reboot mid-incident doesn't lose
     * track of an in-flight leak. Final disambiguation happens at the next
     * valve reconnect in rules_engine_on_valve_connected(). */
    incident_load_from_nvs();

    /* Publish the restored latch to the health engine straight away, so a hub that
     * rebooted mid-incident shows the YELLOW "holding the valve closed" floor from
     * the first LED evaluation instead of waiting for the next latch transition. */
    health_set_interlock_held(g_leak_incident_active);

    g_initialized = true;
}

void rules_engine_evaluate_leak(leak_source_t source, bool leak_active, const char *source_id)
{
    if (!g_initialized) return;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(RULES_TAG, "Failed to take mutex");
        return;
    }

    // Track active leak sources for auto-clear timeout
    track_leak_source(source_id, leak_active);

    // Only act on leak-detected events for auto-close
    if (!leak_active) {
        xSemaphoreGive(g_mutex);
        return;
    }

    // Check if device is provisioned
    if (!provisioning_is_provisioned()) {
        xSemaphoreGive(g_mutex);
        return;
    }

    // Get current rules config
    rules_config_t rules;
    if (!provisioning_get_rules_config(&rules)) {
        xSemaphoreGive(g_mutex);
        return;
    }

    // Check master enable
    if (!rules.auto_close_enabled) {
        ESP_LOGD(RULES_TAG, "Auto-close disabled, ignoring leak from %s",
                 source_id ? source_id : "unknown");
        xSemaphoreGive(g_mutex);
        return;
    }

    // Check trigger mask
    uint8_t trigger_bit = source_to_trigger_bit(source);
    if (!(rules.trigger_mask & trigger_bit)) {
        ESP_LOGD(RULES_TAG, "Source %s not in trigger mask (0x%02X), ignoring",
                 leak_source_to_str(source), rules.trigger_mask);
        xSemaphoreGive(g_mutex);
        return;
    }

    // Latch the leak incident ALWAYS — even during override window.
    // This ensures the system knows about the incident when the window expires
    // and can immediately auto-close.
    if (!g_leak_incident_active) {
        ESP_LOGW(RULES_TAG, "LEAK INCIDENT latched by %s sensor %s",
                 leak_source_to_str(source), source_id ? source_id : "unknown");
        g_leak_incident_active = true;
        incident_save_to_nvs();
    }

    // 24h override window: block automatic valve closure but allow leak tracking.
    // The user physically overrode the valve and has guaranteed water access for 24h.
    // Leak events are still reported to the cloud via normal telemetry paths.
    if (g_override_state == OVERRIDE_STATE_ACTIVE) {
        TickType_t now = xTaskGetTickCount();
        if ((now - g_last_blocked_event_tick) >= pdMS_TO_TICKS(OVERRIDE_BLOCKED_COOLDOWN_MS) ||
            g_last_blocked_event_tick == 0) {
            g_last_blocked_event_tick = now;

            // Compute remaining time for telemetry
            time_t now_epoch;
            time(&now_epoch);
            int32_t remaining = (now_epoch >= EPOCH_VALID_THRESHOLD && g_override_window_expiry > now_epoch)
                ? (int32_t)(g_override_window_expiry - now_epoch) : -1;
            if (override_on_uptime_basis()) {
                remaining = override_uptime_remaining_s(now_epoch);  // stamped before clock sync
            } else if (now_epoch < EPOCH_VALID_THRESHOLD) {
                remaining = override_unsynced_epoch_remaining_s(now_epoch);  // clock lost at power-on
            }

            ESP_LOGI(RULES_TAG, "Override active — auto-close BLOCKED for %s sensor %s (remaining=%lds)",
                     leak_source_to_str(source), source_id ? source_id : "unknown", (long)remaining);

            cJSON *root = cJSON_CreateObject();
            if (root) {
                char idbuf[WIRE_DEVICE_ID_BUF];
                cJSON_AddStringToObject(root, "event", "auto_close_blocked_override");
                cJSON_AddStringToObject(root, "source_type", leak_source_to_str(source));
                add_device_id(root, source_id, idbuf);
                if (remaining >= 0) {
                    cJSON_AddNumberToObject(root, "override_remaining_s", remaining);
                }
                if (g_pending_telemetry) free(g_pending_telemetry);
                g_pending_telemetry = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
            }
        }
        xSemaphoreGive(g_mutex);
        return;
    }

    // Check if valve is already closed AND RMLEAK already asserted. Only the provisioned
    // valve counts: after a valve decommission the old link stays up until its DISCONNECT
    // (about 0.5 s, up to the 5 s supervision timeout), and neither its stale cache nor
    // that link may speak for a valve that is gone. The link is gated the same way below.
    bool has_target = ble_valve_has_target_mac();
    int valve_state = ble_valve_get_state();
    bool rmleak_already = ble_valve_get_rmleak_state();
    if (has_target && valve_state == 0 && rmleak_already) {
        ESP_LOGD(RULES_TAG, "Valve closed + RMLEAK active, no action needed");
        xSemaphoreGive(g_mutex);
        return;
    }

    // Check cooldown (only for the close+rmleak write, not for the latch)
    TickType_t now = xTaskGetTickCount();
    if (g_auto_close_triggered &&
        (now - g_last_auto_close_tick) < pdMS_TO_TICKS(AUTO_CLOSE_COOLDOWN_MS)) {
        ESP_LOGD(RULES_TAG, "Auto-close cooldown active, skipping");
        xSemaphoreGive(g_mutex);
        return;
    }

    // === AUTO-CLOSE + RMLEAK: All conditions met ===
    ESP_LOGW(RULES_TAG, "AUTO-CLOSE + RMLEAK triggered by %s sensor %s",
             leak_source_to_str(source), source_id ? source_id : "unknown");

    g_auto_close_triggered = true;
    g_last_auto_close_tick = now;
    g_rmleak_assert_tick = now;  // Grace period: don't check valve override until BLE write propagates

    // Sample the link ONCE, here, and use the same value for the telemetry and for
    // the branch below. Reading ble_valve_is_connected() twice would let the event
    // and the action disagree if the link dropped between them. It counts any GAP
    // link, including a decommissioned valve's that is still being torn down, so it
    // is gated on the target: otherwise auto_close claimed rmleak_asserted:true for a
    // valve that no longer exists (and whose commands are refused).
    bool valve_reachable = has_target && ble_valve_is_connected();

    // Build telemetry before releasing mutex
    build_auto_close_telemetry(source, source_id, valve_reachable);

    xSemaphoreGive(g_mutex);

    // With a provisioned valve the RMLEAK + close are issued whether or not it is linked.
    // Unlinked, the valve module pends both (RMLEAK first) and applies them at the next
    // setup completion, ahead of anything else. Without that, an OPEN pended while the
    // valve was away (a C2D open, or a failed write) was written at reconnect BEFORE the
    // reconciliation (on_valve_connected) closed the valve again: the pended CLOSE now
    // overwrites it. Pended commands cannot fire after the leak has cleared:
    // g_auto_close_triggered is set above, so track_leak_source() cancels them when the
    // last source dries. The reconciliation still closes on connect as before.
    //
    // Both calls are QUEUE POSTS ONLY — neither writes the cached valve state.
    // The cache is written later, on the ble_valve task, inside
    // write_rmleak_command() / write_valve_command(). RMLEAK-before-close
    // ordering therefore holds only because both land on the same FIFO
    // (ble_cmd_queue) drained by one task.
    //
    // Because we run on iothub_task and the writes land on ble_valve task, the
    // snapshot coupled to this event could otherwise flush while the cache still
    // holds the PRE-close state. ble_valve_open/close/set_rmleak arm a settle
    // barrier on enqueue; the snapshot flush block honours it via
    // ble_valve_cmd_settling() and defers until the write lands (or ~1.5 s).
    if (valve_reachable) {
        // Enqueue failures are surfaced, not swallowed: a full command queue means
        // the interlock/close never reaches the valve, and silence there looks
        // identical to success in every log and every telemetry field.
        if (!ble_valve_set_rmleak(true))
            valve_cmd_not_sent("AUTO-CLOSE: RMLEAK", "interlock not applied");
        if (!ble_valve_close())
            valve_cmd_not_sent("AUTO-CLOSE: close", "valve NOT closed");
    } else if (!ble_valve_has_target_mac()) {
        // Sensors-only hub: the incident is latched and reported, there is nothing to close.
        ESP_LOGW(RULES_TAG, "AUTO-CLOSE: no provisioned valve - nothing to close");
    } else {
        ESP_LOGW(RULES_TAG, "AUTO-CLOSE: valve not connected — scanning; "
                            "close deferred to reconnect reconciliation");
        // Pend the interlock, then the close (see above), then trigger the scan.
        if (!ble_valve_set_rmleak(true))
            valve_cmd_not_sent("AUTO-CLOSE: RMLEAK", "interlock not applied");
        if (!ble_valve_close())
            valve_cmd_not_sent("AUTO-CLOSE: close", "valve NOT closed");
        if (!ble_valve_connect())
            valve_cmd_not_sent("AUTO-CLOSE: connect", "no reconnect scan requested");
    }
}

bool rules_engine_handle_config_command(const char *json_str)
{
    if (!json_str || !g_initialized) {
        return false;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(RULES_TAG, "Failed to parse config JSON");
        return false;
    }

    // Get current config (merge semantics)
    rules_config_t rules;
    if (!provisioning_get_rules_config(&rules)) {
        // Use defaults if can't read
        rules.auto_close_enabled = true;
        rules.trigger_mask = RULES_TRIGGER_ALL;
    }

    // Merge fields
    cJSON *auto_close = cJSON_GetObjectItem(root, "auto_close_enabled");
    if (auto_close && cJSON_IsBool(auto_close)) {
        rules.auto_close_enabled = cJSON_IsTrue(auto_close);
    }

    cJSON *trigger_mask = cJSON_GetObjectItem(root, "trigger_mask");
    if (trigger_mask && cJSON_IsNumber(trigger_mask)) {
        rules.trigger_mask = (uint8_t)trigger_mask->valueint;
    }

    // Parse individual trigger enables (convenience fields)
    cJSON *ble_leak = cJSON_GetObjectItem(root, "trigger_ble_leak");
    if (ble_leak && cJSON_IsBool(ble_leak)) {
        if (cJSON_IsTrue(ble_leak)) {
            rules.trigger_mask |= RULES_TRIGGER_BLE_LEAK;
        } else {
            rules.trigger_mask &= ~RULES_TRIGGER_BLE_LEAK;
        }
    }
    cJSON *lora = cJSON_GetObjectItem(root, "trigger_lora");
    if (lora && cJSON_IsBool(lora)) {
        if (cJSON_IsTrue(lora)) {
            rules.trigger_mask |= RULES_TRIGGER_LORA;
        } else {
            rules.trigger_mask &= ~RULES_TRIGGER_LORA;
        }
    }
    cJSON *valve_flood = cJSON_GetObjectItem(root, "trigger_valve_flood");
    if (valve_flood && cJSON_IsBool(valve_flood)) {
        if (cJSON_IsTrue(valve_flood)) {
            rules.trigger_mask |= RULES_TRIGGER_VALVE_FLOOD;
        } else {
            rules.trigger_mask &= ~RULES_TRIGGER_VALVE_FLOOD;
        }
    }

    cJSON_Delete(root);

    // Persist
    bool ok = provisioning_set_rules_config(&rules);
    if (ok) {
        ESP_LOGI(RULES_TAG, "Config updated: auto_close=%s triggers=0x%02X",
                 rules.auto_close_enabled ? "enabled" : "disabled",
                 rules.trigger_mask);
    }

    return ok;
}

char *rules_engine_take_pending_telemetry(void)
{
    if (!g_initialized) return NULL;

    char *result = NULL;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        result = g_pending_telemetry;
        g_pending_telemetry = NULL;
        xSemaphoreGive(g_mutex);
    }

    return result;
}

bool rules_engine_is_leak_incident_active(void)
{
    if (!g_initialized) return false;

    bool active = false;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        active = g_leak_incident_active;
        xSemaphoreGive(g_mutex);
    }
    return active;
}

bool rules_engine_reset_leak_incident(void)
{
    if (!g_initialized) return false;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(RULES_TAG, "Failed to take mutex for LEAK_RESET");
        return false;
    }

    /* Guard: refuse to clear the interlock while any leak source is still wet.
     * Otherwise a follow-up valve_open would restore water during an active leak
     * with NO override window and no protection. During-leak water must go
     * through override_enable (the guarded 24h window). Mirrors the 30s
     * auto-clear, which likewise requires all sensors clear. */
    if (g_active_leak_count > 0) {
        ESP_LOGW(RULES_TAG, "LEAK_RESET refused — %u leak source(s) still active (use override to open during a leak)",
                 (unsigned)g_active_leak_count);
        xSemaphoreGive(g_mutex);
        return false;
    }

    // Always clear hub-side state, even if not latched (handles reboot scenario)
    bool was_active = g_leak_incident_active;
    g_leak_incident_active = false;
    incident_save_to_nvs();
    g_auto_close_triggered = false;
    g_all_clear_since = 0;
    g_active_leak_count = 0;
    g_rmleak_assert_tick = 0;
    /* Clear the reconnect-event cooldown too: an explicit reset is the "start from a
     * clean slate" point, and a stale stamp would otherwise swallow the first
     * reconnect announcement of the NEXT incident. */
    g_last_reconnect_close_tick = 0;

    // Also clear override window — LEAK_RESET is a full reset that restores
    // normal auto-close behavior immediately.
    bool had_override = (g_override_state == OVERRIDE_STATE_ACTIVE);
    if (had_override) {
        cancel_override_window();
    }

    // Check if valve still has RMLEAK asserted (survives hub reboot)
    bool valve_rmleak = ble_valve_get_rmleak_state();

    if (was_active || valve_rmleak || had_override) {
        ESP_LOGW(RULES_TAG, "LEAK_RESET: clearing incident (hub_latch=%d, valve_rmleak=%d, override=%d)",
                 was_active, valve_rmleak, had_override);

        // Build reset telemetry
        cJSON *root = cJSON_CreateObject();
        if (root) {
            cJSON_AddStringToObject(root, "event", "rmleak_cleared");
            // The RMLEAK interlock lives on the valve, so this event names the
            // valve like every other device-scoped message — under valve_id,
            // the same key the snapshot and the leak event use. Before 2.0.1 it
            // carried no identity at all and was unattributable.
            //
            // BOTH keys are omitted unless a real MAC resolves. A hub can be
            // provisioned with sensors and NO valve (see should_remain_provisioned),
            // and a sensor leak still latches an incident — so these events are
            // reachable with no valve in existence. wire_device_id() would then
            // fall back to the literal "valve", inventing a phantom device that
            // appears in no snapshot, no twin and no other event. No identity is
            // better than a wrong one.
            add_interlock_device_id(root);
            if (had_override) {
                cJSON_AddBoolToObject(root, "override_cancelled", true);
            }
            if (g_pending_telemetry) free(g_pending_telemetry);
            g_pending_telemetry = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
        }
    } else {
        ESP_LOGI(RULES_TAG, "LEAK_RESET: no active incident");
    }

    xSemaphoreGive(g_mutex);

    // Always clear RMLEAK on valve — handles case where hub rebooted but
    // valve still has RMLEAK=1 from previous session
    if (was_active || valve_rmleak) {
        if (!ble_valve_set_rmleak(false))
            valve_cmd_not_sent("LEAK_RESET: RMLEAK clear", "valve interlock left set");
    }
    return true;
}

bool rules_engine_cancel_override(void)
{
    if (!g_initialized) return false;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(RULES_TAG, "Failed to take mutex for override_cancel");
        return false;
    }

    if (g_override_state != OVERRIDE_STATE_ACTIVE) {
        ESP_LOGI(RULES_TAG, "override_cancel: no active override window");
        xSemaphoreGive(g_mutex);
        return true;  // Not an error — command succeeds, nothing to cancel
    }

    int32_t remaining = cancel_override_window();

    // Build acknowledgment telemetry
    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root, "event", "auto_close_reenabled");
        cJSON_AddNumberToObject(root, "previous_remaining_s", remaining);
        cJSON_AddStringToObject(root, "reason", "c2d_command");
        if (g_pending_telemetry) free(g_pending_telemetry);
        g_pending_telemetry = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
    }

    // If leaks are currently active, immediately trigger auto-close evaluation.
    // This ensures the valve closes as soon as the user re-enables auto-close,
    // without waiting for the next leak event or tick cycle.
    // Note: g_leak_incident_active may have been cleared during the override window
    // (e.g., by auto-clear timer), so we check g_active_leak_count directly.
    if (g_active_leak_count > 0) {
        rules_config_t rules;
        if (provisioning_get_rules_config(&rules) && rules.auto_close_enabled) {
            ESP_LOGW(RULES_TAG, "Override cancelled with %d active leak(s) — executing auto-close",
                     g_active_leak_count);

            g_leak_incident_active = true;  // Re-latch incident for auto-close path
            incident_save_to_nvs();
            g_auto_close_triggered = true;
            g_rmleak_assert_tick = xTaskGetTickCount();
            g_all_clear_since = 0;

            xSemaphoreGive(g_mutex);

            /* RMLEAK before close — see comment in rules_engine_evaluate_leak.
             * Ensures the valve_state_changed event reports rmleak=true. */
            if (ble_valve_is_connected()) {
                if (!ble_valve_set_rmleak(true))
                    valve_cmd_not_sent("OVERRIDE CANCEL: RMLEAK", "interlock not applied");
                if (!ble_valve_close())
                    valve_cmd_not_sent("OVERRIDE CANCEL: close", "valve NOT closed");
            } else {
                /* Unreachable: pend RMLEAK then CLOSE for the next link, so they overwrite
                 * an OPEN pended meanwhile (see rules_engine_evaluate_leak), then scan. */
                if (ble_valve_has_target_mac()) {
                    if (!ble_valve_set_rmleak(true))
                        valve_cmd_not_sent("OVERRIDE CANCEL: RMLEAK", "interlock not applied");
                    if (!ble_valve_close())
                        valve_cmd_not_sent("OVERRIDE CANCEL: close", "valve NOT closed");
                }
                if (!ble_valve_connect())
                    valve_cmd_not_sent("OVERRIDE CANCEL: connect", "no reconnect scan requested");
            }
            return true;
        }
    }

    /* No active leaks at cancel time. The leak episode is over — wipe residual
     * incident state so the next leak starts a fresh auto-close cycle.
     *
     * Without this, rules_engine_tick() would still see g_leak_incident_active
     * latched from a since-cleared leak during the window, plus valve RMLEAK=0
     * (left over from the user's physical override that *started* the window),
     * and misread that as a brand-new physical override on the very next leak
     * NOTIFY — silently re-arming the 24h window that the user just cancelled.
     *
     * g_rmleak_assert_tick is also reset so the next ble_valve_set_rmleak(true)
     * gets a fresh grace period instead of inheriting a stale tick from a
     * prior auto-close many seconds ago. */
    bool needs_save = g_leak_incident_active;
    g_leak_incident_active = false;
    g_auto_close_triggered = false;
    g_all_clear_since = 0;
    g_rmleak_assert_tick = 0;
    if (needs_save) {
        incident_save_to_nvs();
    }

    xSemaphoreGive(g_mutex);
    return true;
}

override_enable_result_t rules_engine_enable_override_remote(void)
{
    if (!g_initialized) return OVERRIDE_ENABLE_ERR_INTERNAL;

    // ── Precondition 1: a valve must be provisioned to override ───────────
    if (!provisioning_is_provisioned() || !ble_valve_has_target_mac()) {
        ESP_LOGW(RULES_TAG, "override_enable: no valve provisioned");
        return OVERRIDE_ENABLE_ERR_NOT_PROVISIONED;
    }

    // ── Precondition 2: valve must be reachable. Bounded reconnect so the
    // window starts at EXECUTION, never at receipt. Polls (not a busy-wait) and
    // stays within the app's 20s cmd_ack timeout. ──────────────────────────
    if (!ble_valve_is_ready()) {
        ESP_LOGI(RULES_TAG, "override_enable: valve not ready — reconnecting (<=%dms)",
                 OVERRIDE_CONNECT_TIMEOUT_MS);
        if (!ble_valve_connect())
            valve_cmd_not_sent("override_enable: connect", "no reconnect scan requested");
        TickType_t start = xTaskGetTickCount();
        while (!ble_valve_is_ready()) {
            if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(OVERRIDE_CONNECT_TIMEOUT_MS)) {
                ESP_LOGW(RULES_TAG, "override_enable: valve unreachable after reconnect window");
                return OVERRIDE_ENABLE_ERR_VALVE_DISCONNECTED;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    // ── Precondition 3: there must be something to override — an active
    // incident, an asserted valve RMLEAK, or an already-active window
    // (idempotent refresh). Reject pre-emptive use. (Query fns take the mutex,
    // so call them BEFORE acquiring it below.) ─────────────────────────────
    bool window_active = rules_engine_is_override_window_active();
    bool incident      = rules_engine_is_leak_incident_active();
    bool valve_rmleak  = ble_valve_get_rmleak_state();
    if (!window_active && !incident && !valve_rmleak) {
        ESP_LOGW(RULES_TAG, "override_enable: no active incident to override");
        return OVERRIDE_ENABLE_ERR_NO_INCIDENT;
    }

    // ── Precondition 4: the valve's OWN flood probe is an absolute floor —
    // no override (physical or remote) opens the valve while it stands in
    // water; the valve would refuse/re-close anyway. ───────────────────────
    if (ble_valve_get_leak()) {
        ESP_LOGW(RULES_TAG, "override_enable: valve flood probe wet — refusing");
        return OVERRIDE_ENABLE_ERR_VALVE_FLOOD;
    }

    // ── Execute: window → clear RMLEAK → open ─────────────────────────────
    // Window FIRST so rules_engine_evaluate_leak() blocks auto-close and
    // rules_engine_tick() Check 2 is skipped (it only fires when override is
    // INACTIVE) — otherwise the hub could misread the RMLEAK 1->0 we are about
    // to cause as a *physical* override, or race a concurrent leak into an
    // auto-close between the RMLEAK clear and the open.
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(RULES_TAG, "override_enable: mutex timeout");
        return OVERRIDE_ENABLE_ERR_INTERNAL;
    }
    g_leak_incident_active = false;
    incident_save_to_nvs();
    g_auto_close_triggered = false;
    g_all_clear_since = 0;
    g_rmleak_assert_tick = 0;
    start_override_window("c2d_command");   /* persists NVS + queues telemetry */
    xSemaphoreGive(g_mutex);

    // RMLEAK must be cleared BEFORE the open — the valve refuses an open while
    // its remote_leak_active interlock is set (mirrors the physical button,
    // which clears its local latch before driving the motor open).
    if (!ble_valve_set_rmleak(false))
        valve_cmd_not_sent("override_enable: RMLEAK clear", "valve interlock left set");
    if (!ble_valve_open())
        valve_cmd_not_sent("override_enable: open", "valve NOT opened");

    ESP_LOGW(RULES_TAG, "override_enable: 24h override started remotely — RMLEAK cleared, valve opening");
    return OVERRIDE_ENABLE_OK;
}

void rules_engine_reassert_rmleak_if_needed(void)
{
    // Kept for backward compatibility — delegates to full reconnect handler
    rules_engine_on_valve_connected();
}

void rules_engine_on_valve_connected(void)
{
    if (!g_initialized) return;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;

    // Read actual valve characteristics for reconciliation logging
    int valve_state = ble_valve_get_state();
    bool valve_rmleak = ble_valve_get_rmleak_state();
    bool valve_leak = ble_valve_get_leak();

    ESP_LOGI(RULES_TAG, "╔══ VALVE RECONNECT RECONCILIATION ══╗");
    ESP_LOGI(RULES_TAG, "║ Valve: state=%s rmleak=%s flood=%s",
             valve_state == 1 ? "OPEN" : valve_state == 0 ? "CLOSED" : "UNKNOWN",
             valve_rmleak ? "ACTIVE" : "CLEAR",
             valve_leak ? "LEAK" : "OK");
    ESP_LOGI(RULES_TAG, "║ Hub:   incident=%d leaks=%d override=%s",
             g_leak_incident_active, g_active_leak_count,
             g_override_state == OVERRIDE_STATE_ACTIVE ? "ACTIVE" : "INACTIVE");

    if (g_override_state == OVERRIDE_STATE_ACTIVE) {
        time_t now;
        time(&now);
        int32_t remaining = (now >= EPOCH_VALID_THRESHOLD && g_override_window_expiry > now)
            ? (int32_t)(g_override_window_expiry - now) : -1;
        if (override_on_uptime_basis()) {
            remaining = override_uptime_remaining_s(now);  // stamped before clock sync
        } else if (now < EPOCH_VALID_THRESHOLD) {
            remaining = override_unsynced_epoch_remaining_s(now);  // clock lost at power-on
        }
        ESP_LOGI(RULES_TAG, "║ Override window: remaining=%lds", (long)remaining);
    }
    ESP_LOGI(RULES_TAG, "╚════════════════════════════════════╝");

    // Refresh RMLEAK grace period on reconnect.  Pending commands (CLOSE + RMLEAK)
    // are being applied by the BLE valve task right now.  Without this, tick Check 2
    // could see stale RMLEAK=0 and falsely detect a physical override.
    if (g_leak_incident_active || g_auto_close_triggered) {
        g_rmleak_assert_tick = xTaskGetTickCount();
    }

    // ── Priority 0: Override window check ─────────────────────────────────
    // If the override window is active, do NOT auto-close on reconnect.
    // The user has guaranteed water access for the remaining window duration.
    if (g_override_state == OVERRIDE_STATE_ACTIVE) {
        // Still sync hub incident latch with valve state
        if (!g_leak_incident_active && valve_rmleak) {
            ESP_LOGW(RULES_TAG, "Reconnected: valve RMLEAK active + override window — re-latching incident");
            g_leak_incident_active = true;
            incident_save_to_nvs();
        }
        ESP_LOGI(RULES_TAG, "Reconnected: override window active — skipping auto-close");
        xSemaphoreGive(g_mutex);
        return;
    }

    // ── Priority 1: Active leaks present → auto-close if enabled ──────────
    // This handles the case where leaks were detected while valve was offline.
    // Single evaluation regardless of how many sensors are leaking (anti-spam).
    if (g_active_leak_count > 0) {
        rules_config_t rules;
        if (provisioning_get_rules_config(&rules) && rules.auto_close_enabled) {

            /* IDEMPOTENCE GUARD — mirrors rules_engine_evaluate_leak().
             *
             * The interlock is already in its desired end state: the valve is shut
             * and RMLEAK is confirmed asserted (the cache is fed only by notifies
             * and read-backs, never by an optimistic write, so this is the valve's
             * own word). Re-issuing the writes achieves nothing and re-publishing
             * the event is pure noise.
             *
             * This path had neither of evaluate_leak's two guards, which is what
             * turned a re-announced link into an unbounded storm. */
            if (valve_state == 0 && valve_rmleak) {
                ESP_LOGI(RULES_TAG,
                         "Reconnected with %d active leak(s) — valve already closed + RMLEAK asserted, nothing to do",
                         g_active_leak_count);
                g_leak_incident_active = true;
                incident_save_to_nvs();
                g_auto_close_triggered = true;
                g_rmleak_assert_tick = xTaskGetTickCount();
                g_all_clear_since = 0;
                xSemaphoreGive(g_mutex);
                return;
            }

            ESP_LOGW(RULES_TAG, "Valve reconnected with %d active leak(s) — executing auto-close",
                     g_active_leak_count);

            g_leak_incident_active = true;
            incident_save_to_nvs();
            g_auto_close_triggered = true;
            g_rmleak_assert_tick = xTaskGetTickCount();
            g_all_clear_since = 0;

            /* RATE LIMIT THE EVENT, NEVER THE ACTION.
             *
             * evaluate_leak() lets AUTO_CLOSE_COOLDOWN_MS skip the close as well,
             * which is safe there because that path only runs on a fresh leak
             * report. Here it would not be: evaluate_leak() stamps
             * g_last_auto_close_tick BEFORE it checks connectivity, so a leak
             * detected while the valve was offline arrives here already inside the
             * cooldown. Gating the close on that would mean a wet sensor and an
             * OPEN valve — strictly worse than the storm this is guarding against.
             *
             * So the writes below always run (both are idempotent at the valve),
             * and only the duplicate telemetry is suppressed. The stamp advances
             * only when an event is actually emitted, so the window is anchored to
             * the last announcement and expires predictably. */
            TickType_t now_tick = xTaskGetTickCount();
            bool emit_event = (g_last_reconnect_close_tick == 0) ||
                              ((now_tick - g_last_reconnect_close_tick) >= pdMS_TO_TICKS(AUTO_CLOSE_COOLDOWN_MS));

            /* Sample the link ONCE and reuse it for the event and the branch below,
             * as evaluate_leak() does — reading it twice lets the event claim the
             * writes were issued while the action sees a dropped link. */
            bool valve_reachable = ble_valve_is_connected();

            // Build telemetry
            cJSON *root = emit_event ? cJSON_CreateObject() : NULL;
            if (!emit_event) {
                ESP_LOGI(RULES_TAG,
                         "Reconnect auto-close event suppressed (cooldown) — writes still issued");
            }
            if (root) {
                cJSON_AddStringToObject(root, "event", "auto_close");
                char idbuf[WIRE_DEVICE_ID_BUF];
                // Why this auto_close fired, NOT what kind of device reported it.
                // Before 2.0.0 this was source_type:"reconnect" — a value outside
                // the device-type vocabulary, so a consumer validating source_type
                // against the enum saw a member that does not exist. The real
                // source type is not recoverable here: the active-leak table keys
                // by id only, and re-deriving a type from the id's string format
                // is the fragility this release removed everywhere else.
                //
                // The identity KEY is still correct without it — valve-vs-sensor
                // is decidable from the tracking id (== VALVE_SOURCE_ID), and
                // both sensor types share sensor_id. So this event names its
                // device under the same key as every other message even though
                // it cannot name the device's type.
                cJSON_AddStringToObject(root, "cause", "reconnect");
                if (g_active_leak_count > 0)
                    add_device_id(root, g_active_leak_ids[0], idbuf);
                // Same honesty rule as the main auto_close path: report whether the
                // writes are actually being issued, not what we intended.
                //
                // The previous comment here claimed this "fires FROM the connect
                // handler" so the link must be up. It does not — it runs on
                // iothub_task when BLE_UPD_CONNECTED is DEQUEUED, which can be
                // arbitrarily later than the connect itself (the queue is 16 deep
                // and one item is dequeued per loop iteration). The link can have
                // dropped again by the time we get here.
                //
                // NOTE ON SEMANTICS: this field means "the RMLEAK write was issued",
                // matching build_auto_close_telemetry(), which is handed the same
                // once-sampled link state. It deliberately does NOT report
                // ble_valve_get_rmleak_state(): that is the PRE-write cache at this
                // point (the write happens after the mutex is released), so it would
                // read false on exactly the pass that asserts the interlock. Whether
                // the interlock actually took is carried by valve.rmleak in the next
                // snapshot, which is fed by the read-back.
                cJSON_AddBoolToObject(root, "rmleak_asserted", valve_reachable);
                cJSON_AddNumberToObject(root, "active_leak_count", g_active_leak_count);
                if (g_pending_telemetry) free(g_pending_telemetry);
                g_pending_telemetry = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
                g_last_reconnect_close_tick = now_tick;  /* anchor the cooldown to this announcement */
            }

            xSemaphoreGive(g_mutex);

            /* RMLEAK before close — see comment in rules_engine_evaluate_leak.
             * Enqueue failures surfaced, as on the main auto-close path: a full
             * command queue here means the deferred close never happens, and
             * this is the recovery path for a leak the hub already missed once. */
            if (!ble_valve_set_rmleak(true))
                valve_cmd_not_sent("RECONNECT AUTO-CLOSE: RMLEAK", "interlock not applied");
            if (!ble_valve_close())
                valve_cmd_not_sent("RECONNECT AUTO-CLOSE: close", "valve NOT closed");
            return;
        }
    }

    // ── Priority 2: Hub/valve RMLEAK state synchronization ────────────────
    // Handles reboot scenarios where one side lost state.
    bool hub_active = g_leak_incident_active;

    if (hub_active && !valve_rmleak) {
        /* Hub had an incident, valve reports RMLEAK cleared. Two ways this
         * can arise — disambiguate via valve_state:
         *
         *  (a) valve_state == OPEN: user physically overrode while the hub
         *      was offline. With the new firmware, processLongPress atomically
         *      opens the valve AND clears RMLEAK + notifies the hub. If the
         *      hub was rebooting at the moment of NOTIFY, the override would
         *      otherwise be lost. Start the 24h grace window retroactively
         *      (from now — we don't know the exact override moment).
         *
         *  (b) valve_state == CLOSED: valve rebooted and lost RMLEAK (e.g.,
         *      Debug build, pin reset cleared BKP0R). Re-assert RMLEAK so the
         *      interlock comes back. */
        if (valve_state == 1) {
            ESP_LOGW(RULES_TAG, "Reconnected: hub incident + valve open + RMLEAK clear — inferring physical override, starting 24h window");
            g_leak_incident_active = false;
            incident_save_to_nvs();
            g_auto_close_triggered = false;
            g_all_clear_since = 0;
            start_override_window("button");    /* writes its own NVS entries */
            xSemaphoreGive(g_mutex);
        } else {
            ESP_LOGW(RULES_TAG, "Reconnected: hub incident active, valve closed + RMLEAK clear — re-asserting");
            g_rmleak_assert_tick = xTaskGetTickCount();
            xSemaphoreGive(g_mutex);
            if (!ble_valve_set_rmleak(true))
                valve_cmd_not_sent("RECONNECT: RMLEAK re-assert", "interlock not applied");
        }
    } else if (!hub_active && valve_rmleak) {
        // Valve has RMLEAK but hub lost incident (hub rebooted).
        // Conservatively re-latch the incident. If the valve has RMLEAK but hub
        // doesn't know about it, the user will need to send LEAK_RESET to clear.
        ESP_LOGW(RULES_TAG, "Reconnected: valve RMLEAK active, hub incident clear — re-latching incident");
        g_leak_incident_active = true;
        incident_save_to_nvs();
        xSemaphoreGive(g_mutex);
    } else if (hub_active && valve_rmleak) {
        ESP_LOGI(RULES_TAG, "Reconnected: hub + valve RMLEAK in sync");
        xSemaphoreGive(g_mutex);
    } else {
        ESP_LOGI(RULES_TAG, "Reconnected: no active incident, valve clear");
        xSemaphoreGive(g_mutex);
    }
}

void rules_engine_tick(void)
{
    if (!g_initialized) return;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    // ── Override window expiry check ──────────────────────────────────────
    // Runs regardless of incident state — the window can expire even if all
    // leaks have cleared (the incident may still be latched).
    if (g_override_state == OVERRIDE_STATE_ACTIVE) {
        time_t now_epoch;
        time(&now_epoch);
        bool synced = (now_epoch >= EPOCH_VALID_THRESHOLD);

        // First valid clock for a window stamped before the sync (expiry in 1970).
        if (synced && g_override_window_expiry < EPOCH_VALID_THRESHOLD) {
            if (g_override_unsynced_this_boot) {
                // Started this boot: its elapsed uptime is exact, so carry the window
                // over to the synced clock instead of ending it the moment SNTP lands.
                uint32_t elapsed = uptime_s() - g_override_start_uptime_s;
                g_override_window_expiry = now_epoch - (time_t)elapsed + OVERRIDE_WINDOW_DURATION_S;
                g_override_unsynced_this_boot = false;
                if (g_override_window_expiry > now_epoch) {
                    ESP_LOGW(RULES_TAG, "Override window re-based to the synced clock (expiry=%ld, remaining=%lds)",
                             (long)g_override_window_expiry, (long)(g_override_window_expiry - now_epoch));
                    override_save_to_nvs();
                } else {
                    // Already past: the expiry check below ends it and clears NVS.
                    ESP_LOGW(RULES_TAG, "Override window ran its full duration before the clock synced (elapsed=%lus) - expiring it now",
                             (unsigned long)elapsed);
                }
            } else {
                // Restored from an earlier boot: how long it already ran is unknown, so
                // it ends here through the normal expiry below (2.1.3's behaviour, which
                // fails toward auto-close).
                ESP_LOGW(RULES_TAG, "Override window was stamped before a clock sync in an earlier boot - elapsed time unknown, expiring it now");
            }
        }

        // Synced: the real-epoch expiry decides. Not synced: a window stamped that way is
        // timed on uptime, and a real-epoch one (restored after a power-on lost the clock)
        // from that power-on, instead of waiting for a sync that may never come.
        bool expired;
        if (synced) {
            expired = (now_epoch >= g_override_window_expiry);
        } else if (override_on_uptime_basis()) {
            expired = (override_uptime_remaining_s(now_epoch) == 0);
        } else {
            expired = (override_unsynced_epoch_remaining_s(now_epoch) == 0);
            if (expired) {
                ESP_LOGW(RULES_TAG, "Override window restored after a power-on has run its full duration with no clock sync - expiring it now");
            }
        }
        if (expired) {
            ESP_LOGW(RULES_TAG, "OVERRIDE WINDOW EXPIRED: auto-close re-enabled");
            g_override_state = OVERRIDE_STATE_INACTIVE;
            g_override_window_expiry = 0;
            g_override_unsynced_this_boot = false;
            override_clear_nvs();

            // Build expiry telemetry
            cJSON *root = cJSON_CreateObject();
            if (root) {
                cJSON_AddStringToObject(root, "event", "water_access_override_expired");
                cJSON_AddBoolToObject(root, "auto_close_resumed",
                    g_active_leak_count > 0);
                cJSON_AddNumberToObject(root, "active_leak_count", g_active_leak_count);
                if (g_pending_telemetry) free(g_pending_telemetry);
                g_pending_telemetry = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
            }

            // If leaks are still active, immediately trigger auto-close.
            // This resumes normal protection as soon as the override expires.
            if (g_active_leak_count > 0) {
                rules_config_t rules;
                if (provisioning_get_rules_config(&rules) && rules.auto_close_enabled) {
                    ESP_LOGW(RULES_TAG, "Override expired with %d active leak(s) — executing auto-close",
                             g_active_leak_count);

                    g_leak_incident_active = true;
                    incident_save_to_nvs();
                    g_auto_close_triggered = true;
                    g_rmleak_assert_tick = xTaskGetTickCount();
                    g_all_clear_since = 0;

                    xSemaphoreGive(g_mutex);

                    /* RMLEAK before close — see comment in rules_engine_evaluate_leak. */
                    if (ble_valve_is_connected()) {
                        if (!ble_valve_set_rmleak(true))
                            valve_cmd_not_sent("OVERRIDE EXPIRED: RMLEAK", "interlock not applied");
                        if (!ble_valve_close())
                            valve_cmd_not_sent("OVERRIDE EXPIRED: close", "valve NOT closed");
                    } else {
                        /* Unreachable: pend RMLEAK then CLOSE for the next link, so they
                         * overwrite an OPEN pended meanwhile (see rules_engine_evaluate_leak),
                         * then scan. */
                        if (ble_valve_has_target_mac()) {
                            if (!ble_valve_set_rmleak(true))
                                valve_cmd_not_sent("OVERRIDE EXPIRED: RMLEAK", "interlock not applied");
                            if (!ble_valve_close())
                                valve_cmd_not_sent("OVERRIDE EXPIRED: close", "valve NOT closed");
                        }
                        if (!ble_valve_connect())
                            valve_cmd_not_sent("OVERRIDE EXPIRED: connect", "no reconnect scan requested");
                    }
                    return;
                }
            }
        }
    }

    if (!g_leak_incident_active) {
        xSemaphoreGive(g_mutex);
        return;
    }

    TickType_t now = xTaskGetTickCount();

    // Check 1: Auto-clear timeout (all sensors clear for AUTO_CLEAR_TIMEOUT_MS)
    if (g_all_clear_since != 0) {
        if ((now - g_all_clear_since) >= pdMS_TO_TICKS(AUTO_CLEAR_TIMEOUT_MS)) {
            ESP_LOGW(RULES_TAG, "AUTO-CLEAR: all sensors clear for %ds — clearing RMLEAK",
                     AUTO_CLEAR_TIMEOUT_MS / 1000);
            g_leak_incident_active = false;
            incident_save_to_nvs();
            g_auto_close_triggered = false;
            g_all_clear_since = 0;

            cJSON *root = cJSON_CreateObject();
            if (root) {
                cJSON_AddStringToObject(root, "event", "rmleak_auto_cleared");
                // Same rule as rmleak_cleared above: the interlock is the valve's,
                // so name it — but only when a real MAC resolves. Added in 2.0.1;
                // before that this event reached the cloud with no source_type and
                // no identity at all, so a backend could not attribute it.
                add_interlock_device_id(root);
                cJSON_AddNumberToObject(root, "clear_after_seconds", AUTO_CLEAR_TIMEOUT_MS / 1000);
                if (g_pending_telemetry) free(g_pending_telemetry);
                g_pending_telemetry = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
            }

            xSemaphoreGive(g_mutex);
            if (!ble_valve_set_rmleak(false))  // Does NOT open valve
                valve_cmd_not_sent("AUTO-CLEAR: RMLEAK clear", "valve interlock left set");
            return;
        }
    }

    // Check 2: Valve-side physical override (RMLEAK cleared on valve while incident active)
    // SKIP when override window is already active — re-detecting would corrupt the
    // incident latch (clearing g_leak_incident_active) and prevent auto-close from
    // working when the override is later cancelled or expires.
    // ONLY check when valve is fully ready (GATT setup complete) — during disconnection
    // or GATT setup, ble_valve_get_rmleak_state() returns stale data.
    if (g_override_state == OVERRIDE_STATE_INACTIVE && ble_valve_is_ready()) {
        // Grace period: skip check if RMLEAK was just written or valve just reconnected
        // (BLE write may still be in-flight or pending commands not yet applied)
        if (g_rmleak_assert_tick != 0 &&
            (now - g_rmleak_assert_tick) < pdMS_TO_TICKS(RMLEAK_GRACE_PERIOD_MS)) {
            xSemaphoreGive(g_mutex);
            return;
        }
        // Detect transition to ready — valve just reconnected and pending RMLEAK
        // write may not have propagated yet.  Refresh grace period once.
        if (!g_valve_was_ready) {
            g_valve_was_ready = true;
            g_rmleak_assert_tick = now;
            ESP_LOGD(RULES_TAG, "Valve just became ready — refreshing RMLEAK grace period");
            xSemaphoreGive(g_mutex);
            return;
        }
        if (!ble_valve_get_rmleak_state()) {
            // Physical override detected — the user cleared RMLEAK via the valve button.
            // Start a 24h override window: automatic closures are blocked to guarantee
            // water access, but leaks continue to be reported to the cloud.
            ESP_LOGW(RULES_TAG, "RMLEAK cleared externally (valve override) — starting 24h override window");
            g_leak_incident_active = false;
            incident_save_to_nvs();
            g_auto_close_triggered = false;
            g_all_clear_since = 0;

            start_override_window("button");
        }
    }
    // Track valve ready state for transition detection
    if (!ble_valve_is_ready()) {
        g_valve_was_ready = false;
    }

    xSemaphoreGive(g_mutex);
}

// ─── Override Window Query APIs ─────────────────────────────────────────────

bool rules_engine_is_override_window_active(void)
{
    if (!g_initialized) return false;

    bool active = false;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        active = (g_override_state == OVERRIDE_STATE_ACTIVE);
        xSemaphoreGive(g_mutex);
    }
    return active;
}

int32_t rules_engine_get_override_remaining_s(void)
{
    if (!g_initialized) return -1;

    int32_t remaining = -1;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (g_override_state == OVERRIDE_STATE_ACTIVE) {
            time_t now;
            time(&now);
            if (override_on_uptime_basis()) {
                remaining = override_uptime_remaining_s(now);  // stamped before clock sync
            } else if (now >= EPOCH_VALID_THRESHOLD && g_override_window_expiry > now) {
                remaining = (int32_t)(g_override_window_expiry - now);
            } else if (now < EPOCH_VALID_THRESHOLD) {
                remaining = override_unsynced_epoch_remaining_s(now);  // clock lost at power-on
            } else {
                remaining = 0;  // Expired but not yet processed by tick
            }
        }
        xSemaphoreGive(g_mutex);
    }
    return remaining;
}

void rules_engine_get_override_status(bool *active, int32_t *remaining_s,
                                      uint32_t *expires_ts)
{
    /* Single mutex hold for all three fields — avoids the TOCTOU between the
     * separate is_active / remaining_s getters (active could read true and a
     * later expiry read 0 after a concurrent cancel/expiry on the rules task).
     * remaining_s keeps the EXACT semantics of rules_engine_get_override_remaining_s()
     * so the snapshot's override_remaining_s value is unchanged. */
    bool a = false;
    int32_t rem = -1;
    uint32_t exp = 0;

    if (g_initialized && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (g_override_state == OVERRIDE_STATE_ACTIVE) {
            a = true;
            time_t now;
            time(&now);
            if (override_on_uptime_basis()) {
                rem = override_uptime_remaining_s(now);  // stamped before clock sync — omit expires_ts
            } else if (now >= EPOCH_VALID_THRESHOLD && g_override_window_expiry > now) {
                rem = (int32_t)(g_override_window_expiry - now);
                exp = (uint32_t)g_override_window_expiry;   // absolute epoch (matches event field)
            } else if (now < EPOCH_VALID_THRESHOLD) {
                rem = override_unsynced_epoch_remaining_s(now);  // clock lost at power-on — omit expires_ts
            } else {
                rem = 0;  // expired but not yet processed by tick
            }
        }
        xSemaphoreGive(g_mutex);
    }

    if (active)      *active = a;
    if (remaining_s) *remaining_s = rem;
    if (expires_ts)  *expires_ts = exp;   // 0 => omit the snapshot field
}

bool rules_engine_reset_all(void)
{
    /* Under the mutex, unlike the NVS-only clear this replaces. That one ran unlocked on
     * the esp-mqtt task and could interleave with an incident_save_to_nvs() running under
     * the mutex on iothub_task, re-persisting the incident right after the erase (N22).
     * It also reset only the latch, so the in-RAM override window survived and the final
     * decommission snapshot could still report override_active:true (L12). */
    bool locked = g_initialized &&
                  xSemaphoreTake(g_mutex, pdMS_TO_TICKS(5000)) == pdTRUE;
    if (locked) {
        g_leak_incident_active      = false;
        g_auto_close_triggered      = false;
        g_all_clear_since           = 0;
        g_rmleak_assert_tick        = 0;
        g_last_reconnect_close_tick = 0;
        g_last_auto_close_tick      = 0;
        g_valve_was_ready           = false;
        g_active_leak_count         = 0;
        g_override_state            = OVERRIDE_STATE_INACTIVE;
        g_override_window_expiry    = 0;
        g_override_unsynced_this_boot = false;
        g_last_blocked_event_tick   = 0;
        // g_pending_telemetry is kept: an already-built event describes something that
        // really happened, and iothub_task still publishes it.
    } else {
        // A stuck mutex must not block a decommission, a boot or the empty-hub edge: the
        // NVS erase and the interlock release below still run, only the RAM state is left
        // alone, and the false return tells the caller a RAM reset is still owed.
        ESP_LOGE(RULES_TAG, "Rules reset: mutex unavailable — erasing NVS state only");
    }

    nvs_handle_t h;
    if (nvs_open_from_partition(NVS_PROV_PARTITION, NVS_OVERRIDE_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY_OVR_STATE);
        nvs_erase_key(h, NVS_KEY_OVR_EXPIRY);
        nvs_erase_key(h, NVS_KEY_INCIDENT);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(RULES_TAG, "NVS: rules-engine persistent state cleared");
    }
    /* The key is gone, so the write-skip cache must forget what it thought was stored —
     * otherwise a save of the same logical value after a wipe would be skipped and the
     * key would never come back. Written on the unlocked path too: -1 only ever forces
     * one extra write, so it cannot corrupt anything a stuck holder is doing. */
    g_incident_persisted = -1;

    /* Release the floor and cancel a pending close on BOTH paths: both are non-blocking,
     * and the floor must drop even when the latch could not be reset. Zero devices means
     * nothing to protect, yet a decommissioned hub's final snapshot used to report warning
     * with a leak-interlock reason.
     *
     * On the locked path they run BEFORE the give (both are already called under g_mutex
     * elsewhere, e.g. track_leak_source()). After it, a concurrent evaluate_leak() could
     * latch a NEW incident in the gap and then have its floor and its pending close undone
     * by this reset of the old state. */
    health_set_interlock_held(false);
    ble_valve_cancel_pending_close();

    if (locked) xSemaphoreGive(g_mutex);

    // STATE only: the rules config (auto_close_enabled / trigger_mask) is the caller's call.
    ESP_LOGW(RULES_TAG, "Rules-engine state reset (hub empty / decommission)");
    return locked;
}

bool rules_engine_forget_unprovisioned(void)
{
    // Not initialised: evaluate_leak() returns before tracking anything, so the
    // active-leak set is empty and there is nothing to forget.
    if (!g_initialized) return true;

    // Provisioning FIRST and released before the rules mutex: no nesting, and a
    // timeout here is "unknown" (retry), never "nothing is provisioned".
    prov_device_set_t set;
    if (!provisioning_get_device_set(&set)) return false;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(RULES_TAG, "Failed to take mutex (forget_unprovisioned)");
        return false;
    }

    int i = 0;
    while (i < g_active_leak_count) {
        const char *id = g_active_leak_ids[i];
        bool keep = false;

        if (strcmp(id, VALVE_SOURCE_ID) == 0) {
            keep = set.has_valve;
        } else if (id[0] == '0' && (id[1] == 'x' || id[1] == 'X')) {
            uint32_t sid = (uint32_t)strtoul(id, NULL, 16);
            for (int k = 0; k < set.lora_count; k++) {
                if (set.lora_ids[k] == sid) { keep = true; break; }
            }
        } else {
            for (int k = 0; k < set.ble_count; k++) {
                if (strcasecmp(id, set.ble_macs[k]) == 0) { keep = true; break; }
            }
        }

        if (keep) {
            i++;
            continue;
        }

        /* Copy first: track_leak_source() re-packs g_active_leak_ids in place, so the
         * entry `id` points at is overwritten by its successor. Do NOT advance i — the
         * successor now sits at this index. */
        char gone[sizeof(g_active_leak_ids[0])];
        memcpy(gone, id, sizeof(gone));
        gone[sizeof(gone) - 1] = '\0';
        track_leak_source(gone, false);
        ESP_LOGW(RULES_TAG, "Rules: forgot removed leak source %s", gone);
    }

    xSemaphoreGive(g_mutex);
    return true;
}

void rules_engine_on_valve_replaced(void)
{
    // Not initialised: nothing is tracked or latched yet.
    if (!g_initialized) return;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(RULES_TAG, "Failed to take mutex (valve replaced)");
        return;
    }

    /* VALVE_SOURCE_ID is MAC-less, so forget_unprovisioned() keeps it while any valve is
     * provisioned: the old valve's flood reading used to survive the swap and close the
     * new, dry valve on its first link. If the new valve is wet, its own link-up LEAK
     * re-adds the source and closes it through evaluate_leak(). */
    uint8_t before = g_active_leak_count;
    track_leak_source(VALVE_SOURCE_ID, false);
    bool dropped = (g_active_leak_count < before);

    /* Nothing else wet: release the latch now, not on the 30 s all-clear. The new valve
     * usually links sooner, and on_valve_connected() would read a latched incident with
     * an open valve and RMLEAK clear as a physical override (Priority 2) and block
     * auto-close for 24 h. Nothing is written to the valve: the old valve's RMLEAK left
     * with it, and the new one never carried one. With another source still wet the
     * latch and count stay, so Priority 1 closes the new valve on its first link. The
     * override window is not touched. */
    bool released = false;
    if (g_active_leak_count == 0 && g_leak_incident_active) {
        g_leak_incident_active = false;
        incident_save_to_nvs();   // also releases the health interlock floor
        g_auto_close_triggered = false;
        g_all_clear_since = 0;
        g_rmleak_assert_tick = 0;
        released = true;
    }

    ESP_LOGW(RULES_TAG, "Valve replaced: old valve leak source %s, %u source(s) still wet, incident %s",
             dropped ? "dropped" : "not tracked", (unsigned)g_active_leak_count,
             released ? "released" : (g_leak_incident_active ? "kept" : "not latched"));

    xSemaphoreGive(g_mutex);
}
