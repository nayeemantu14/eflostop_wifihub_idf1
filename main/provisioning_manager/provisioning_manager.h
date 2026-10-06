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

// Answer to "is this sensor provisioned?". UNKNOWN (manager not initialised, or the mutex
// timed out) is NOT "no": a caller deciding whether to act on a leak must treat it as
// provisioned, so a busy provisioning mutex can never drop a real leak.
typedef enum {
    PROV_MEMBER_NO = 0,
    PROV_MEMBER_YES,
    PROV_MEMBER_UNKNOWN
} prov_member_t;

// One consistent copy of every provisioned device (see provisioning_get_device_set()).
typedef struct {
    bool     has_valve;
    char     valve_mac[18];
    uint8_t  lora_count;
    uint32_t lora_ids[MAX_LORA_SENSORS];
    uint8_t  ble_count;
    char     ble_macs[MAX_BLE_LEAK_SENSORS][18];
} prov_device_set_t;

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
 * A sensor count past its array capacity (corrupt NVS) is clamped and logged, and every
 * loaded MAC string is NUL-terminated within its buffer, on every exit path.
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
 * A LoRa id or BLE MAC (case-insensitive) listed twice in the payload is kept once;
 * the repeat is logged and ignored.
 * 
 * Sensor arrays that leave a hub with devices holding none empty it, as the last removal
 * does: the rules config goes back to the defaults (auto-close on, RULES_TRIGGER_ALL) in
 * the same mutex hold and the same save, before this payload's own auto_close_enabled /
 * rules are applied on top. The state stays PROVISIONED.
 * 
 * @param json JSON string (may not be null-terminated)
 * @param len Length of JSON string
 * @param now_empty Optional (NULL allowed): set true only when this provision succeeded
 *                  and emptied a hub that had devices; false on every other return
 * @return true if provisioning successful
 */
bool provisioning_handle_azure_payload_json(const char *json, size_t len, bool *now_empty);

/**
 * @brief Decommission device - erase all provisioning data and return to UNPROVISIONED state
 * 
 * This function:
 * - Erases provisioning data from NVS
 * - Then clears all provisioning data from memory and returns to UNPROVISIONED
 * - Thread-safe (the mutex is held across the erase and the RAM clear)
 * 
 * On failure RAM is left untouched (still provisioned), and if the erase got as far as
 * removing keys the config is written back to NVS (best effort, logged if that fails).
 * 
 * @return true if decommissioning successful
 */
bool provisioning_decommission(void);

/**
 * @brief Remove valve from provisioning (selective decommission)
 * 
 * Removes valve MAC and updates state to UNPROVISIONED if no other devices remain.
 * The remove/add/set functions below change RAM only when the NVS save succeeds.
 * 
 * A removal that leaves NO device also puts the rules config back to the defaults
 * (auto-close on, RULES_TRIGGER_ALL) in the same mutex hold and the same save, so the
 * empty-hub reset is ordered before any later provision or rules_config. The same holds
 * for the two sensor removals below.
 * 
 * @param now_empty Optional (NULL allowed): set true only when this removal succeeded
 *                  and left no device provisioned; false on every other return
 * @return true if removal successful; false if no valve is provisioned, the mutex
 *         timed out, or the NVS save failed (RAM then unchanged)
 */
bool provisioning_remove_valve(bool *now_empty);

/**
 * @brief Remove specific LoRa sensor from provisioning (selective decommission)
 * 
 * Removes sensor from list and updates state to UNPROVISIONED if no other devices remain
 * 
 * @param sensor_id Sensor ID to remove
 * @param now_empty Optional (NULL allowed): see provisioning_remove_valve()
 * @return true if removal successful
 */
bool provisioning_remove_lora_sensor(uint32_t sensor_id, bool *now_empty);

/**
 * @brief Remove specific BLE leak sensor from provisioning (selective decommission)
 * 
 * Removes sensor from list and updates state to UNPROVISIONED if no other devices remain
 * 
 * @param mac MAC address to remove (format: "XX:XX:XX:XX:XX:XX")
 * @param now_empty Optional (NULL allowed): see provisioning_remove_valve()
 * @return true if removal successful
 */
bool provisioning_remove_ble_sensor(const char *mac, bool *now_empty);

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
 * @return true if sensor is provisioned (false for "no" AND for "unknown")
 */
bool provisioning_is_lora_sensor_provisioned(uint32_t sensor_id);

/**
 * @brief Tri-state LoRa membership: YES, NO, or UNKNOWN (not initialised / mutex timeout,
 *        logged). See prov_member_t for how UNKNOWN must be handled.
 */
prov_member_t provisioning_lora_sensor_membership(uint32_t sensor_id);

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
 * @brief Get ONE atomic copy of every provisioned device (valve, LoRa ids, BLE MACs),
 *        taken under a single mutex hold. MACs are upper-cased, as in the per-list
 *        getters.
 *
 * Returns false ONLY when the manager is not initialised or the mutex (1000 ms) timed
 * out. An unprovisioned hub returns TRUE with an empty set (has_valve=false, counts 0).
 *
 * Callers that reconcile against this set (health table, scanner whitelist, telemetry
 * caches, rules engine) MUST treat false as "unknown", never as "no devices". The
 * per-list getters above return false for BOTH "none provisioned" and "mutex timeout",
 * which is exactly the trap this function exists to avoid: a reconcile that read a
 * timeout as an empty list would forget every device.
 *
 * @param out Output set (~375 B; zeroed first on every call)
 * @return true if *out is a valid snapshot of the provisioned set
 */
bool provisioning_get_device_set(prov_device_set_t *out);

// What the twin report and the lifecycle message state about provisioning (provisioning_get_summary()).
typedef struct {
    bool           provisioned;     // the hub's state is PROVISIONED
    char           valve_mac[18];   // upper case; "" when no valve is provisioned
    uint8_t        lora_count;      // 0 when not provisioned
    uint8_t        ble_count;       // 0 when not provisioned
    rules_config_t rules;
} prov_summary_t;

/**
 * @brief The hub's provisioning summary in ONE mutex hold: the same values as
 *        provisioning_is_provisioned(), provisioning_get_valve_mac(),
 *        provisioning_get_lora_sensors()' and provisioning_get_ble_leak_sensors()' counts
 *        and provisioning_get_rules_config() give when the mutex is free.
 *
 * 2.1.4 WP3 (HANDOFF 15p W2): a twin report made those five reads, each with its own 1 s
 * timeout, so a busy mutex could hold iothub_task about 5 s in one build, and the report
 * then went out with defaults (provisioned false, valve_id null, counts 0). One read waits
 * 1 s at most, and a caller can leave the report owed instead.
 *
 * @return false (out zeroed) when out is NULL, the manager is not initialised or the
 *         mutex (1000 ms) timed out: "unknown", never "unprovisioned". For 2 s after a
 *         timeout it answers false at once, without waiting. Logs nothing. iothub_task only.
 */
bool provisioning_get_summary(prov_summary_t *out);

/**
 * @brief Callback for provisioning_with_valve_target(). It runs with the provisioning
 *        mutex HELD: keep it short and non-blocking, never call back into this module
 *        (the mutex is not recursive), and never take a lock ordered before it (the
 *        rules engine mutex).
 *
 * @param valve_mac Provisioned valve MAC, upper-cased; NULL when no valve is provisioned
 * @param ble_count Number of provisioned BLE leak sensors
 * @param ctx       The caller's context pointer, passed through
 */
typedef void (*prov_valve_target_cb_t)(const char *valve_mac, uint8_t ble_count, void *ctx);

/**
 * @brief Read the provisioned valve MAC and BLE leak sensor count and hand them to `cb`
 *        within ONE mutex hold. Every provisioning change takes the same mutex, so what
 *        `cb` applies (the BLE valve target) is never a value a change has already
 *        replaced: the change lands wholly before the read, or only after `cb` returned.
 *
 * Returns false WITHOUT calling `cb` only when `cb` is NULL, the manager is not
 * initialised or the mutex (1000 ms) timed out. Treat that as "unknown" and retry, never
 * as "no valve". An unprovisioned hub calls cb(NULL, 0, ctx) and returns true.
 *
 * @return true if `cb` was called
 */
bool provisioning_with_valve_target(prov_valve_target_cb_t cb, void *ctx);

/**
 * @brief Check if a BLE leak sensor MAC is provisioned (case-insensitive)
 *
 * @param mac MAC address "XX:XX:XX:XX:XX:XX"; NULL returns false
 * @return true if the sensor is provisioned (false for "no" AND for "unknown")
 */
bool provisioning_is_ble_sensor_provisioned(const char *mac);

/**
 * @brief Tri-state BLE leak sensor membership (case-insensitive): YES, NO (also for a NULL
 *        mac), or UNKNOWN (not initialised / mutex timeout, logged).
 */
prov_member_t provisioning_ble_sensor_membership(const char *mac);

/**
 * @brief Get current rules engine configuration
 */
bool provisioning_get_rules_config(rules_config_t *rules_out);

// The device a leak report names, for provisioning_get_rules_and_state(): the list its
// membership is read from, in the same mutex hold as the rules.
typedef enum {
    PROV_DEV_NONE = 0,   // no device: *member is the hub's state alone
    PROV_DEV_VALVE,      // the provisioned valve (id unused)
    PROV_DEV_LORA,       // a LoRa sensor; id "0x%08lX", as the rules engine tracks it
    PROV_DEV_BLE,        // a BLE leak sensor; id its MAC, case-insensitive
} prov_dev_kind_t;

/**
 * @brief Read the provisioned state, one device's membership and the rules config within
 *        ONE mutex hold.
 *
 * 2.1.4 WP3 (WP2D-C4, the user's decision of 2026-10-02): a leak is decided on the
 * membership read with its rules, not on the hub's state alone. A device removed after
 * the event loop's membership gate let its report through, or a report the gate passed
 * as UNKNOWN (provisioning busy), is then judged by the set as it is at the decision.
 *
 * *provisioned: the hub is PROVISIONED. *member (may be NULL): it is, and `kind` / `id`
 * name a device in its set; for PROV_DEV_NONE the hub's state; false for a sensor kind
 * with a NULL or unparsable id. The same answer as provisioning_*_membership() gives
 * YES to, and provisioning_get_device_set() lists.
 *
 * Returns false ONLY when provisioned or rules_out is NULL, the manager is not initialised
 * or the mutex (1000 ms) timed out: that is "unknown", never "unprovisioned".
 * provisioning_is_provisioned() answers false for both, which a leak decision must tell
 * apart (the rules engine then decides on its last copy). Logs nothing on a timeout.
 */
bool provisioning_get_rules_and_state(prov_dev_kind_t kind, const char *id,
                                      bool *provisioned, bool *member,
                                      rules_config_t *rules_out);

/**
 * @brief Set rules engine configuration and persist to NVS. RAM takes the new rules
 *        only if the NVS write succeeded; false leaves the previous rules in force.
 */
bool provisioning_set_rules_config(const rules_config_t *rules);

#ifdef __cplusplus
}
#endif

#endif // PROVISIONING_MANAGER_H