#ifndef C2D_COMMANDS_H
#define C2D_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Current canonical schema identifier (no version in string)
#define C2D_CMD_SCHEMA          "eflostop.cmd"
#define C2D_CMD_SCHEMA_VER      1

// Legacy schema string — accepted for backward compatibility
#define C2D_CMD_SCHEMA_LEGACY   "eflostop.cmd.v1"

// ---------------------------------------------------------------------------
// Parsed command structure
// ---------------------------------------------------------------------------

typedef struct {
    char    id[64];          // Correlation ID (empty string if not provided)
    char    cmd[32];         // Normalized command name
    char   *payload_json;    // Heap-allocated payload JSON string (may be NULL)
    bool    is_envelope;     // true = parsed from JSON envelope; false = legacy text
    int     ver;             // Envelope version (1 = current; 0 = legacy/unknown)
} c2d_command_t;

// ---------------------------------------------------------------------------
// Normalized command name constants
// ---------------------------------------------------------------------------

#define C2D_CMD_VALVE_OPEN          "valve_open"
#define C2D_CMD_VALVE_CLOSE         "valve_close"
#define C2D_CMD_VALVE_SET_STATE     "valve_set_state"
#define C2D_CMD_LEAK_RESET          "leak_reset"
#define C2D_CMD_DECOMMISSION        "decommission"
#define C2D_CMD_RULES_CONFIG        "rules_config"
#define C2D_CMD_SENSOR_META         "sensor_meta"
#define C2D_CMD_PROVISION           "provision"

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/**
 * @brief Parse a C2D message into a normalized command.
 *
 * Supports three formats (tried in order):
 *  1) Canonical envelope: {"schema":"eflostop.cmd","ver":1,"cmd":"...","payload":{...}}
 *  2) Legacy envelope:    {"schema":"eflostop.cmd.v1","cmd":"...","payload":{...}}
 *  3) Legacy text:        VALVE_OPEN, DECOMMISSION_LORA:0xID, RULES_CONFIG:{json}, etc.
 *
 * @param data       Raw message data (need not be null-terminated)
 * @param data_len   Length of data
 * @param cmd_out    Output: populated on success
 * @return true if a valid command was parsed
 */
bool c2d_command_parse(const char *data, size_t data_len, c2d_command_t *cmd_out);

/**
 * @brief Free heap-allocated fields inside a c2d_command_t.
 *        Safe to call even if payload_json is NULL.
 */
void c2d_command_free(c2d_command_t *cmd);

#ifdef __cplusplus
}
#endif

#endif // C2D_COMMANDS_H
