#ifndef PROVISIONING_MANAGER_H
#define PROVISIONING_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_LORA_SENSORS 16
#define MAX_BLE_LEAK_SENSORS 16

typedef enum {
    PROV_STATE_UNPROVISIONED = 0,
    PROV_STATE_PROVISIONED = 1
} provisioning_state_t;

// D2D Rules Engine trigger source bitmask
#define RULES_TRIGGER_BLE_LEAK    (1 << 0)
#define RULES_TRIGGER_LORA        (1 << 1)
#define RULES_TRIGGER_VALVE_FLOOD (1 << 2)
#define RULES_TRIGGER_ALL         (RULES_TRIGGER_BLE_LEAK | RULES_TRIGGER_LORA | RULES_TRIGGER_VALVE_FLOOD)

typedef struct {
    bool auto_close_enabled;    // Master enable/disable (default: true)
    uint8_t trigger_mask;       // Bitmask of sensor types that trigger auto-close
} rules_config_t;

typedef struct {
    char valve_mac[18];                              // "XX:XX:XX:XX:XX:XX"
    uint32_t lora_sensor_ids[MAX_LORA_SENSORS];      // Array of sensor IDs
    uint8_t lora_sensor_count;                       // Number of valid sensor IDs
    char ble_leak_sensors[MAX_BLE_LEAK_SENSORS][18]; // Array of MAC addresses
    uint8_t ble_leak_sensor_count;                   // Number of valid leak sensor MACs
    uint8_t config_version;                          // Config schema version
    provisioning_state_t state;                      // Provisioned or not
    rules_config_t rules;                            // D2D rules engine config
} provisioning_config_t;

/**
 * @brief Initialize provisioning manager and load config from NVS
 * 
 * @return true if initialization successful
 */
bool provisioning_init(void);

/**
 * @brief Check if the hub is provisioned
 * 
 * @return true if provisioned, false if unprovisioned
 */
bool provisioning_is_provisioned(void);

/**
 * @brief Get current provisioning state
 * 
 * @return provisioning_state_t
 */
provisioning_state_t provisioning_get_state(void);

/**
 * @brief Load provisioning config from NVS
 * 
 * @param config Pointer to config structure to populate
 * @return true if config loaded successfully
 */
bool provisioning_load_from_nvs(provisioning_config_t *config);

/**
 * @brief Save provisioning config to NVS
 * 
 * @param config Pointer to config structure to save
 * @return true if saved successfully
 */
bool provisioning_save_to_nvs(const provisioning_config_t *config);

/**
 * @brief Handle provisioning JSON payload from Azure
 * 
 * @param json JSON string (may not be null-terminated)
 * @param len Length of JSON string
 * @return true if provisioning successful
 */
bool provisioning_handle_azure_payload_json(const char *json, size_t len);

/**
 * @brief Decommission device - erase all provisioning data and return to UNPROVISIONED state
 * 
 * This function:
 * - Clears all provisioning data from memory
 * - Erases provisioning data from NVS
 * - Returns device to UNPROVISIONED state
 * - Thread-safe (uses mutex)
 * 
 * @return true if decommissioning successful
 */
bool provisioning_decommission(void);

/**
 * @brief Remove valve from provisioning (selective decommission)
 * 
 * Removes valve MAC and updates state to UNPROVISIONED if no other devices remain
 * 
 * @return true if removal successful
 */
bool provisioning_remove_valve(void);

/**
 * @brief Remove specific LoRa sensor from provisioning (selective decommission)
 * 
 * Removes sensor from list and updates state to UNPROVISIONED if no other devices remain
 * 
 * @param sensor_id Sensor ID to remove
 * @return true if removal successful
 */
bool provisioning_remove_lora_sensor(uint32_t sensor_id);

/**
 * @brief Remove specific BLE leak sensor from provisioning (selective decommission)
 * 
 * Removes sensor from list and updates state to UNPROVISIONED if no other devices remain
 * 
 * @param mac MAC address to remove (format: "XX:XX:XX:XX:XX:XX")
 * @return true if removal successful
 */
bool provisioning_remove_ble_sensor(const char *mac);

/**
 * @brief Add a LoRa sensor to existing provisioning
 * 
 * @param sensor_id Sensor ID to add
 * @return true if addition successful
 */
bool provisioning_add_lora_sensor(uint32_t sensor_id);

/**
 * @brief Add a BLE leak sensor to existing provisioning
 * 
 * @param mac MAC address to add (format: "XX:XX:XX:XX:XX:XX")
 * @return true if addition successful
 */
bool provisioning_add_ble_sensor(const char *mac);

/**
 * @brief Get valve MAC address
 * 
 * @param mac_out Output buffer (must be at least 18 bytes)
 * @return true if valve MAC is available
 */
bool provisioning_get_valve_mac(char *mac_out);

/**
 * @brief Check if a LoRa sensor ID is provisioned
 * 
 * @param sensor_id Sensor ID to check
 * @return true if sensor is provisioned
 */
bool provisioning_is_lora_sensor_provisioned(uint32_t sensor_id);

/**
 * @brief Get list of provisioned LoRa sensor IDs
 * 
 * @param ids_out Output array (must be at least MAX_LORA_SENSORS size)
 * @param count_out Output count of sensor IDs
 * @return true if sensor list is available
 */
bool provisioning_get_lora_sensors(uint32_t *ids_out, uint8_t *count_out);

/**
 * @brief Get list of provisioned BLE leak sensor MACs
 * 
 * @param macs_out Output array (must be at least MAX_BLE_LEAK_SENSORS * 18 bytes)
 * @param count_out Output count of sensor MACs
 * @return true if sensor list is available
 */
bool provisioning_get_ble_leak_sensors(char macs_out[][18], uint8_t *count_out);

/**
 * @brief Get current rules engine configuration
 */
bool provisioning_get_rules_config(rules_config_t *rules_out);

/**
 * @brief Set rules engine configuration and persist to NVS
 */
bool provisioning_set_rules_config(const rules_config_t *rules);

#ifdef __cplusplus
}
#endif

#endif // PROVISIONING_MANAGER_H