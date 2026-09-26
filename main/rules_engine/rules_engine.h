#ifndef RULES_ENGINE_H
#define RULES_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Water-detection sources. The wire spelling of each member is produced by
// leak_source_to_str() below, which health_engine.c now calls directly instead
// of keeping a parallel table — one device-type vocabulary, one definition,
// across leak events, auto_close events and health events.
typedef enum {
    LEAK_SOURCE_BLE = 0,
    LEAK_SOURCE_LORA,
    LEAK_SOURCE_VALVE       // The valve's own on-board flood probe
} leak_source_t;

/**
 * @brief Wire spelling of a water-detection source ("ble_leak_sensor" | "lora" |
 *        "valve"). The single source of truth for data.source_type on leak events
 *        (telemetry_v2.c), auto_close events (rules_engine.c) and health events
 *        (health_engine.c) — do not re-spell these literals at a call site.
 */
const char *leak_source_to_str(leak_source_t source);

/**
 * @brief Wire KEY under which a message names the device it is about.
 *
 * The single source of truth for the identity-key vocabulary, in the same spirit
 * as leak_source_to_str() is for the device-type vocabulary: two spellings, one
 * definition, every emitter delegating here rather than keeping a literal.
 *
 * The key names the device TYPE — `valve_id` for the valve, `sensor_id` for a
 * leak sensor — and always sits in the same position with the same string form,
 * so a consumer that switches on source_type reads one key either way. As of
 * 2.1.0 this holds on EVERY outbound message: snapshots, leak events,
 * valve_state_changed, auto_close, the RMLEAK interlock events and health
 * alerts. The generic `device_id` no longer appears anywhere on the D2C plane.
 *
 * Deliberately a bool, not a leak_source_t. Both sensor sources share one key,
 * so valve-vs-sensor is the whole rule — which is why the valve-reconnect
 * auto_close, the one path that genuinely cannot recover a leak_source_t, can
 * still name its identity key correctly from the tracking id alone.
 *
 * @param is_valve true for the valve, false for any leak sensor.
 */
const char *leak_identity_key(bool is_valve);

/**
 * @brief Internal tracking id for the valve as a leak SOURCE.
 *
 * The active-leak table (g_active_leak_ids) matches sources by string, so the
 * valve's key must be stable even while the BLE link is down — hence a constant
 * rather than its MAC. This value is NOT what goes on the wire: data.valve_id
 * carries the valve's real MAC so the cloud can join auto_close to the same
 * device that the leak event, the snapshot (data.valve.valve_id) and the health
 * alert name — all of them under that one key.
 */
#define VALVE_SOURCE_ID  "valve"

// Result of a remote (C2D) override_enable request. Maps 1:1 to the cmd_ack
// error.detail strings produced by the IoT Hub command dispatcher.
typedef enum {
    OVERRIDE_ENABLE_OK = 0,
    OVERRIDE_ENABLE_ERR_NO_INCIDENT,        // No active leak incident / RMLEAK to override
    OVERRIDE_ENABLE_ERR_VALVE_FLOOD,        // Valve's own flood probe is wet (absolute floor)
    OVERRIDE_ENABLE_ERR_VALVE_DISCONNECTED, // Valve unreachable after bounded reconnect
    OVERRIDE_ENABLE_ERR_NOT_PROVISIONED,    // Hub unprovisioned / no valve configured
    OVERRIDE_ENABLE_ERR_INTERNAL            // Mutex timeout / not initialized
} override_enable_result_t;

/**
 * @brief Initialize the rules engine. Call after provisioning_init().
 *        Loads override window state from NVS if previously persisted.
 */
void rules_engine_init(void);

/**
 * @brief Evaluate a leak event and auto-close the valve if rules allow.
 *        During a 24h override window, the incident is latched and leak events
 *        are reported to the cloud, but automatic valve closure is blocked.
 *
 * @param source Which sensor type triggered the leak
 * @param leak_active true = leak detected, false = leak cleared
 * @param source_id Human-readable ID (MAC string or "0xHEXID")
 */
void rules_engine_evaluate_leak(leak_source_t source, bool leak_active, const char *source_id);

/**
 * @brief Handle RULES_CONFIG: C2D JSON command.
 *        Merge semantics: only fields present in JSON are changed.
 *
 * @param json_str JSON string (null-terminated)
 * @return true if config was updated successfully
 */
bool rules_engine_handle_config_command(const char *json_str);

/**
 * @brief Get the last auto-close telemetry JSON (if any).
 *        Caller must free() the returned string.
 *
 * @return JSON string or NULL if no pending telemetry
 */
char *rules_engine_take_pending_telemetry(void);

/**
 * @brief Check if a leak incident is currently active (latched).
 */
bool rules_engine_is_leak_incident_active(void);

/**
 * @brief Reset the leak incident latch and clear RMLEAK on the valve.
 *        Also cancels any active 24h override window.
 *        Does NOT open the valve — opening requires a separate command.
 *
 * GUARDED: refuses (returns false, clears nothing) while any leak source is
 * still actively wet. Clearing the interlock then would let a follow-up
 * valve_open restore water during a live leak with NO override window and no
 * protection. The sanctioned during-leak water path is
 * rules_engine_enable_override_remote() (which starts the guarded 24h window).
 * Mirrors the 30s auto-clear, which likewise requires all sensors clear.
 *
 * @return true if the incident was cleared (or there was nothing to clear);
 *         false if refused because a leak is still active (or on error).
 */
bool rules_engine_reset_leak_incident(void);

/**
 * @brief Re-assert RMLEAK on valve if a leak incident is active.
 *        Call this after BLE reconnection to ensure valve interlock is restored.
 *        (Legacy wrapper — delegates to rules_engine_on_valve_connected.)
 */
void rules_engine_reassert_rmleak_if_needed(void);

/**
 * @brief Full valve reconnect reconciliation. Call once after valve GATT setup completes.
 *
 * Priority 0: If 24h override window is active, skip auto-close but sync incident latch.
 * Priority 1: If any sensors are actively reporting leaks AND auto_close is enabled,
 *             close valve + assert RMLEAK. Single command regardless of sensor count.
 * Priority 2: Synchronize hub/valve RMLEAK state (handles reboot scenarios).
 */
void rules_engine_on_valve_connected(void);

/**
 * @brief Periodic tick — call from event loop (every ~30s).
 *        Checks override window expiry, auto-clear timeout, and valve-side override.
 *        A window stamped before the clock synced is timed on uptime until the first
 *        valid clock, then re-based to it (started this boot) or expired (restored
 *        from an earlier boot, whose elapsed time is unknown). A real-epoch window
 *        restored after a power-on lost the clock expires, while the clock is still
 *        unsynced, once the full duration has passed since that power-on.
 */
void rules_engine_tick(void);

// ─── 24h Override Window APIs ────────────────────────────────────────────────

/**
 * @brief Check if the 24h water access override window is active.
 */
bool rules_engine_is_override_window_active(void);

/**
 * @brief Get remaining seconds in the override window.
 *        Before the clock syncs, a window stamped from the unsynced clock is measured
 *        on uptime; a window with a real-epoch expiry (restored after a power-on) counts
 *        down from that power-on.
 * @return Seconds remaining (>=0), or -1 if no override window active.
 */
int32_t rules_engine_get_override_remaining_s(void);

/**
 * @brief Read override window status atomically under a single mutex hold.
 *        Avoids the TOCTOU of calling is_active + remaining_s separately.
 * @param active      [out] true if the 24h override window is active (may be NULL)
 * @param remaining_s [out] seconds remaining (>=0) or -1 if inactive; identical
 *                    semantics to rules_engine_get_override_remaining_s (may be NULL)
 * @param expires_ts  [out] absolute Unix epoch expiry, or 0 when inactive / time
 *                    not yet synced / window still timed on uptime (caller should
 *                    omit the field when 0) (may be NULL)
 */
void rules_engine_get_override_status(bool *active, int32_t *remaining_s,
                                      uint32_t *expires_ts);

/**
 * @brief Cancel the 24h override window via C2D command.
 *        Re-enables auto-close immediately. If leaks are active, triggers
 *        auto-close right away. Publishes "auto_close_reenabled" telemetry.
 * @return true on success (always succeeds, even if no window was active)
 */
bool rules_engine_cancel_override(void);

/**
 * @brief Remotely start the 24h water-access override — the app-initiated
 *        equivalent of a physical valve-button override (SRS §4.4.3).
 *
 * Identical end-state to the button: clears the leak incident latch, starts the
 * 24h window (auto-close blocked, leaks still reported), clears the valve RMLEAK
 * interlock and opens the valve. Order is window → clear RMLEAK → open so the
 * hub does not race-close or misread the resulting RMLEAK 1->0 as a physical
 * override. Idempotent: refreshes an already-active window and re-emits the
 * "water_access_override_enabled" event (trigger="c2d_command").
 *
 * Preconditions are checked before any state changes; the window starts only on
 * successful execution, never on mere receipt. Blocks up to ~10s attempting a
 * BLE reconnect if the valve is not ready.
 *
 * @return OVERRIDE_ENABLE_OK on success, or an error code the dispatcher maps to
 *         a frozen cmd_ack error.detail string.
 */
override_enable_result_t rules_engine_enable_override_remote(void);

/**
 * @brief Reset the rules engine to its factory state, in RAM AND in NVS: incident
 *        latch, auto-close latch, active-leak set, all-clear / RMLEAK / cooldown
 *        timers, the 24h override window, and the NVS keys that persist them. Also
 *        releases the health WARNING floor and cancels a pending auto-close.
 *
 * Call when no device remains (the hub was emptied by removals, or boots empty) and
 * from decommission_all, so a re-provisioned hub doesn't inherit a stale incident or
 * override from the previous deployment. Replaces the NVS-only
 * rules_engine_clear_persistent_state(), which left the in-RAM override running (so
 * the final decommission snapshot could still claim override_active) and erased
 * the keys without the mutex, racing a concurrent incident save (L12/N22).
 *
 * Takes the rules mutex (5 s). If it cannot, the NVS keys are still erased and the
 * interlock floor still released, but RAM state is left alone: a stuck mutex must
 * never block a decommission. A pending auto_close/rules event already built is kept
 * — it describes something that really happened. Resets STATE only; the rules config
 * (auto_close_enabled / trigger_mask) is provisioning's and is left untouched.
 *
 * @return true if the RAM state was reset under the mutex; false if the mutex was
 *         unavailable or the engine is not initialised (NVS keys still erased, RAM
 *         state left as it was — the caller may retry).
 */
bool rules_engine_reset_all(void);

/**
 * @brief Drop active-leak sources that are no longer provisioned (a device
 *        decommissioned while wet).
 *
 * Without this a removed wet sensor stayed in the active-leak set forever: leak_reset
 * was refused as "a leak is still active", and override cancel / override expiry
 * re-closed the valve for a device that no longer exists. Each dropped source goes
 * through the normal "leak cleared" path, so when the count reaches 0 a pending
 * auto-close is cancelled and, if an incident is latched, the all-clear timer starts.
 *
 * Call from iothub_task after a device-set change. Takes the provisioning mutex BEFORE
 * the rules mutex, never nested.
 *
 * @return false if it could not run (provisioning or rules mutex unavailable) —
 *         the caller retries; true otherwise.
 */
bool rules_engine_forget_unprovisioned(void);

/**
 * @brief The provisioned valve was replaced by a DIFFERENT valve: drop the old valve's
 *        rules state that would otherwise act on the new one.
 *
 * The valve's leak source (VALVE_SOURCE_ID) is MAC-less, so a flood reading from the old
 * valve survived the swap and auto-closed the new, dry valve on its first link. This
 * drops it through the normal "leak cleared" path. If no other source is then wet, a
 * latched incident is released at once (NVS and the health floor with it): left to the
 * 30 s all-clear, the new valve's first link (open, RMLEAK clear) would read as a
 * physical override and start a 24 h window. Nothing is written to either valve. If
 * another source is still wet, the latch and count stay, so the new valve is closed on
 * its first link. The override window is not touched. A wet new valve re-adds the source
 * with its own link-up leak report.
 *
 * Call from iothub_task when the provisioned valve MAC changes from one valve to another
 * (not on a first provision or a removal: forget_unprovisioned() covers a removal). Takes
 * the rules mutex (1 s); on a timeout it logs and changes nothing.
 */
void rules_engine_on_valve_replaced(void);

#ifdef __cplusplus
}
#endif

#endif // RULES_ENGINE_H
