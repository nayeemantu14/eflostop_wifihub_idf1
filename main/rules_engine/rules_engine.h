#ifndef RULES_ENGINE_H
#define RULES_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LEAK_SOURCE_BLE = 0,
    LEAK_SOURCE_LORA,
    LEAK_SOURCE_VALVE_FLOOD
} leak_source_t;

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
 */
void rules_engine_reset_leak_incident(void);

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
 */
void rules_engine_tick(void);

// ─── 24h Override Window APIs ────────────────────────────────────────────────

/**
 * @brief Check if the 24h water access override window is active.
 */
bool rules_engine_is_override_window_active(void);

/**
 * @brief Get remaining seconds in the override window.
 * @return Seconds remaining (>=0), or -1 if no override window active.
 */
int32_t rules_engine_get_override_remaining_s(void);

/**
 * @brief Cancel the 24h override window via C2D command.
 *        Re-enables auto-close immediately. If leaks are active, triggers
 *        auto-close right away. Publishes "auto_close_reenabled" telemetry.
 * @return true on success (always succeeds, even if no window was active)
 */
bool rules_engine_cancel_override(void);

#ifdef __cplusplus
}
#endif

#endif // RULES_ENGINE_H
