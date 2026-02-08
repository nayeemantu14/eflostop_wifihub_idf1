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
 */
void rules_engine_init(void);

/**
 * @brief Evaluate a leak event and auto-close the valve if rules allow.
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
 *        Does NOT open the valve — opening requires a separate command.
 */
void rules_engine_reset_leak_incident(void);

/**
 * @brief Re-assert RMLEAK on valve if a leak incident is active.
 *        Call this after BLE reconnection to ensure valve interlock is restored.
 */
void rules_engine_reassert_rmleak_if_needed(void);

#ifdef __cplusplus
}
#endif

#endif // RULES_ENGINE_H
