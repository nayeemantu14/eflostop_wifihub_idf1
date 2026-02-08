#ifndef SENSOR_META_H
#define SENSOR_META_H

#include <stdbool.h>
#include <stdint.h>

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
 * @brief Handle SENSOR_META: C2D JSON command.
 */
bool sensor_meta_handle_command(const char *json_str);

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
