#ifndef SENSOR_META_H
#define SENSOR_META_H

#include <stdbool.h>
#include <stdint.h>
#include <strings.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SENSOR_META_LABEL_MAX  32
#define SENSOR_META_ID_MAX     18   // "XX:XX:XX:XX:XX:XX\0" or "0x754A6237\0"
#define MAX_SENSOR_META        32   // 16 BLE + 16 LoRa

typedef enum {
    SENSOR_TYPE_BLE_LEAK = 0,
    SENSOR_TYPE_LORA = 1
} sensor_type_t;

typedef enum {
    LOC_UNKNOWN = 0,
    LOC_BATHROOM,
    LOC_KITCHEN,
    LOC_LAUNDRY,
    LOC_GARAGE,
    LOC_GARDEN,
    LOC_BASEMENT,
    LOC_UTILITY,
    LOC_HALLWAY,
    LOC_BEDROOM,
    LOC_LIVING_ROOM,
    LOC_ATTIC,
    LOC_OUTDOOR,
    LOC_COUNT
} location_code_t;

/**
 * @brief Does this inbound type string name the BLE leak sensor?
 *
 * The canonical wire spelling is "ble_leak_sensor" — it matches `source_type` on
 * every outbound message (leak, auto_close and health events all use that one key
 * as of 2.0.0), so the backend needs only one lookup table. "ble" and "ble_leak"
 * are PERMANENT legacy aliases: "ble" is what every already-deployed app/backend
 * sends in `sensor_meta.sensor_type` and in the `decommission` target, and
 * "ble_leak" is what the hub itself emitted for a BLE sensor's device type before
 * 2.0.0 (under the old key name `dev_type`), so a backend may echo it back.
 * Never remove either alias.
 *
 * Single source of truth for the aliases — used by sensor_meta and by the C2D
 * decommission handler.
 */
static inline bool sensor_type_is_ble_leak(const char *s)
{
    return s && (strcasecmp(s, "ble_leak_sensor") == 0 ||
                 strcasecmp(s, "ble_leak") == 0 ||
                 strcasecmp(s, "ble") == 0);
}

typedef struct {
    uint8_t sensor_type;                    // sensor_type_t
    char sensor_id[SENSOR_META_ID_MAX];     // MAC or "0xHEXID"
    uint8_t location_code;                  // location_code_t
    char label[SENSOR_META_LABEL_MAX];      // user-friendly label
} sensor_meta_entry_t;

/**
 * @brief Initialize sensor metadata module. Loads table from NVS.
 * @return true on success
 */
bool sensor_meta_init(void);

/**
 * @brief Find metadata for a sensor (RAM-only, hot-path safe).
 * @return pointer to entry or NULL if not found
 */
const sensor_meta_entry_t *sensor_meta_find(sensor_type_t type, const char *sensor_id);

/**
 * @brief Set metadata for a sensor (find-or-create). Persists to NVS.
 * @param location_code -1 to keep existing value
 * @param label NULL to keep existing value
 */
bool sensor_meta_set(sensor_type_t type, const char *sensor_id,
                     int location_code, const char *label);

/**
 * @brief Remove metadata for a sensor. Persists to NVS.
 */
bool sensor_meta_remove(sensor_type_t type, const char *sensor_id);

/**
 * @brief Handle a standalone SENSOR_META C2D command (single metadata object).
 */
bool sensor_meta_handle_command(const char *json_str);

/**
 * @brief Apply an optional inline "sensor_meta":[...] array carried in a
 *        `provision` command payload. Shares the exact parse/validate/apply path
 *        with sensor_meta_handle_command. A payload without the array is a no-op;
 *        malformed individual entries are skipped (logged), not fatal.
 * @return number of metadata entries successfully applied (0 if none/absent).
 */
int sensor_meta_apply_array_from_payload(const char *payload_json);

/**
 * @brief Clear all metadata (for full decommission). Erases NVS blob.
 */
void sensor_meta_clear_all(void);

/**
 * @brief Convert location code enum to string.
 */
const char *sensor_meta_location_code_to_str(location_code_t code);

/**
 * @brief Convert location string to code enum.
 */
location_code_t sensor_meta_location_code_from_str(const char *str);

#ifdef __cplusplus
}
#endif

#endif // SENSOR_META_H
