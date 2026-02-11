#ifndef HEALTH_ENGINE_H
#define HEALTH_ENGINE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

#define HEALTH_LORA_TIMEOUT_MS       (10 * 60 * 1000)   // 10 min
#define HEALTH_BLE_LEAK_TIMEOUT_MS   (10 * 60 * 1000)   // 10 min
#define HEALTH_TICK_INTERVAL_MS      (30 * 1000)         // 30s evaluation cycle
#define HEALTH_ALERT_DEBOUNCE_MS     (60 * 1000)         // 60s min between alerts per device
#define HEALTH_BATTERY_WARN_PCT      20
#define HEALTH_BATTERY_GOOD_PCT      35
#define HEALTH_RSSI_WARN_DBM         (-90)
#define HEALTH_RSSI_GOOD_DBM         (-80)
#define HEALTH_VALVE_DISC_TIMEOUT_MS (3 * 60 * 1000)      // 3-min grace before CRITICAL
#define HEALTH_BOOT_SYNC_TIMEOUT_MS  (2 * 60 * 1000)     // 2-min boot window for sensor check-ins
#define HEALTH_MAX_DEVICES           33                   // 1 valve + 16 LoRa + 16 BLE leak

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

typedef enum {
    HEALTH_EXCELLENT = 0,
    HEALTH_GOOD,
    HEALTH_WARNING,
    HEALTH_CRITICAL
} health_rating_t;

typedef enum {
    HEALTH_DEV_VALVE = 0,
    HEALTH_DEV_LORA,
    HEALTH_DEV_BLE_LEAK
} health_dev_type_t;

typedef enum {
    HEALTH_EVT_LORA_CHECKIN = 0,
    HEALTH_EVT_BLE_LEAK_CHECKIN,
    HEALTH_EVT_VALVE_CONNECTED,
    HEALTH_EVT_VALVE_DISCONNECTED,
    HEALTH_EVT_TICK
} health_event_type_t;

// Input event: posted by modules, consumed by health engine task
typedef struct {
    health_event_type_t type;
    union {
        struct {
            uint32_t sensor_id;
            uint8_t  battery;
            int8_t   rssi;
            float    snr;
        } lora;
        struct {
            char    mac_str[18];
            uint8_t battery;
            int8_t  rssi;
        } ble_leak;
    };
} health_event_t;

// Output alert: produced by health engine task, consumed by IoT Hub task
typedef struct {
    health_dev_type_t dev_type;
    char              dev_id[18];
    health_rating_t   new_rating;
    health_rating_t   old_rating;
    uint8_t           battery;
    int8_t            rssi;
    uint32_t          offline_duration_s;
} health_alert_t;

// Read-only snapshot of one device's health state (for cross-task queries)
typedef struct {
    bool              in_use;
    health_dev_type_t dev_type;
    char              dev_id[18];
    health_rating_t   rating;
    bool              connected;        // ever_seen AND not timed out / BLE up
    bool              ever_seen;        // true after first check-in this uptime
    uint8_t           last_battery;     // 0xFF = unknown
    int8_t            last_rssi;        // 0 = unknown
    uint32_t          last_seen_age_s;  // UINT32_MAX = never seen
} health_device_status_t;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * @brief Initialize health engine: create task, queue, timer.
 *        Loads provisioned device list. Call after provisioning_init().
 */
void health_engine_init(void);

/**
 * @brief Reload device list from provisioning manager.
 *        Call when provisioning changes (add/remove devices).
 *        Resets all health states.
 */
void health_engine_reload_devices(void);

/**
 * @brief Post a health event (thread-safe, non-blocking).
 * @return true if event was enqueued, false if queue full.
 */
bool health_post_event(const health_event_t *evt);

/**
 * @brief Get worst health rating across all provisioned devices.
 *        Lock-free (volatile read), safe to call from any task.
 */
health_rating_t health_get_system_rating(void);

/**
 * @brief Dequeue one pending alert (non-blocking).
 * @param out  Pointer to alert struct to populate.
 * @return true if an alert was dequeued, false if queue empty.
 */
bool health_pop_alert(health_alert_t *out);

/**
 * @brief Convert a health_alert_t to a cJSON-formatted string.
 *        Caller must free() the returned string.
 * @return JSON string or NULL on failure.
 */
char *health_alert_to_json(const health_alert_t *alert);

/**
 * @brief Convert health_rating_t to string.
 */
const char *health_rating_to_str(health_rating_t rating);

/**
 * @brief Copy status of ALL provisioned devices into caller-supplied array.
 *        Thread-safe (acquires internal mutex). Call from iothub_task for snapshot.
 * @param out       Array of HEALTH_MAX_DEVICES entries.
 * @param count_out Number of valid (in_use) entries written.
 * @return true on success, false if mutex timeout or not initialized.
 */
bool health_get_device_status_all(health_device_status_t out[HEALTH_MAX_DEVICES],
                                  uint8_t *count_out);

/**
 * @brief Check whether boot sync is complete.
 *        Complete when all provisioned devices have checked in once,
 *        or the 2-minute boot window has elapsed.
 */
bool health_is_boot_sync_complete(void);

// ---------------------------------------------------------------------------
// Convenience inline helpers (for hook sites)
// ---------------------------------------------------------------------------

static inline void health_post_lora_checkin(uint32_t sensor_id, uint8_t battery,
                                             int8_t rssi, float snr)
{
    health_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = HEALTH_EVT_LORA_CHECKIN;
    evt.lora.sensor_id = sensor_id;
    evt.lora.battery   = battery;
    evt.lora.rssi      = rssi;
    evt.lora.snr       = snr;
    health_post_event(&evt);
}

static inline void health_post_ble_leak_checkin(const char *mac_str, uint8_t battery,
                                                 int8_t rssi)
{
    health_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = HEALTH_EVT_BLE_LEAK_CHECKIN;
    strncpy(evt.ble_leak.mac_str, mac_str, sizeof(evt.ble_leak.mac_str) - 1);
    evt.ble_leak.battery = battery;
    evt.ble_leak.rssi    = rssi;
    health_post_event(&evt);
}

static inline void health_post_valve_event(bool connected)
{
    health_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = connected ? HEALTH_EVT_VALVE_CONNECTED : HEALTH_EVT_VALVE_DISCONNECTED;
    health_post_event(&evt);
}

#ifdef __cplusplus
}
#endif

#endif // HEALTH_ENGINE_H
