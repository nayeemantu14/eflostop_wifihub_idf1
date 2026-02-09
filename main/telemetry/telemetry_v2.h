#ifndef TELEMETRY_V2_H
#define TELEMETRY_V2_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TELEMETRY_SCHEMA        "eflostop.v2"
#define TELEMETRY_FW_VERSION    "1.3.0"
#define SNAPSHOT_INTERVAL_MS    (5 * 60 * 1000)   // 5 minutes

// ---------------------------------------------------------------------------
// Cache types — shared between telemetry module and app_iothub for state
// ---------------------------------------------------------------------------

#define TELEM_MAX_LORA_CACHE      16
#define TELEM_MAX_BLE_LEAK_CACHE  16

typedef struct {
    uint32_t sensor_id;
    uint8_t  battery;
    uint8_t  leak_status;
    int8_t   rssi;
    float    snr;
    bool     valid;
} telem_lora_cache_t;

typedef struct {
    char    mac_str[18];
    uint8_t battery;
    bool    leak_state;
    int8_t  rssi;
    bool    valid;
} telem_ble_leak_cache_t;

// ---------------------------------------------------------------------------
// Init / lifecycle
// ---------------------------------------------------------------------------

/**
 * @brief Initialize telemetry v2 module.
 *        Creates snapshot FreeRTOS timer and 1-item trigger queue.
 *        Call after gateway ID init and MQTT client creation.
 *
 * @param client      MQTT client handle (for publishing)
 * @param device_id   Azure device ID (for MQTT topic)
 * @param gateway_id  Gateway ID string ("GW-XXXXXXXXXXXX")
 * @param lora_cache  Pointer to LoRa cache array in iothub_task scope
 * @param ble_cache   Pointer to BLE leak cache array in iothub_task scope
 */
void telemetry_v2_init(esp_mqtt_client_handle_t client,
                       const char *device_id,
                       const char *gateway_id,
                       const telem_lora_cache_t *lora_cache,
                       const telem_ble_leak_cache_t *ble_cache);

/**
 * @brief Get the snapshot trigger queue handle.
 *        Add this to the QueueSet so the event loop wakes when due.
 */
QueueHandle_t telemetry_v2_get_snapshot_queue(void);

/**
 * @brief Start (or restart) the periodic snapshot timer.
 *        Call after QueueSet is set up and on MQTT reconnect.
 */
void telemetry_v2_start_snapshot_timer(void);

// ---------------------------------------------------------------------------
// Publishers — all run in iothub_task context, non-blocking
// ---------------------------------------------------------------------------

/** Publish type="lifecycle" birth message (online, reset_reason, config). */
void telemetry_v2_publish_lifecycle(void);

/** Publish type="snapshot" with all current device + sensor state. */
void telemetry_v2_publish_snapshot(void);

/** Publish type="event" for valve transitions (state, flood). */
void telemetry_v2_publish_valve_event(const char *event_name);

/** Publish type="event" for leak sensor transitions. */
void telemetry_v2_publish_leak_event(const char *event_name,
                                     const char *source_type,
                                     const char *sensor_id,
                                     bool leak_state,
                                     uint8_t battery, int8_t rssi);

/** Publish type="event" wrapping existing rules-engine JSON in v2 envelope. */
void telemetry_v2_publish_rules_event(const char *rules_json);

/** Publish type="event" command acknowledgment. */
void telemetry_v2_publish_cmd_ack(const char *correlation_id,
                                  const char *cmd_name,
                                  bool success,
                                  const char *error_msg);

#ifdef __cplusplus
}
#endif

#endif // TELEMETRY_V2_H
