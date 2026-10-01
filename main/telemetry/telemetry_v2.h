#ifndef TELEMETRY_V2_H
#define TELEMETRY_V2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "mqtt_client.h"
#include "rules_engine.h"   // leak_source_t — the water-detection source vocabulary

#ifdef __cplusplus
extern "C" {
#endif

#define TELEMETRY_SCHEMA        "eflostop.v2"
#define SNAPSHOT_INTERVAL_MS    (5 * 60 * 1000)   // 5 minutes (default)

// Accepted range for the Twin-tunable heartbeat interval. Declared here rather
// than at the Twin call site so ONE rule governs every path into the setting —
// the Twin handler, the NVS restore and any future C2D command. A value outside
// this range can therefore never reach the scheduler or the flash.
#define SNAPSHOT_INTERVAL_MIN_S 60
#define SNAPSHOT_INTERVAL_MAX_S 3600

// Hub firmware version (gateway.fw / twin fw_version). Single source of truth =
// PROJECT_VER in the top-level CMakeLists.txt, read at runtime from the ESP-IDF
// app descriptor. To bump the hub version, edit PROJECT_VER only.
const char *telemetry_v2_fw_version(void);

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
    char    fw_version[12];    // "M.m.p" or "" if not available
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
 * @brief Attach (or replace) the MQTT client and correct the publish topic once
 *        DPS has assigned a device id.
 *
 * telemetry_v2_init() may be called with a NULL client so the snapshot queue and
 * timer exist — and so leak events are buffered rather than dropped — before DPS
 * registration has succeeded. Call this when the cloud connection comes up.
 */
void telemetry_v2_attach_client(esp_mqtt_client_handle_t client,
                                const char *device_id);

/**
 * @brief Get the snapshot trigger queue handle.
 *        Add this to the QueueSet so the event loop wakes when due.
 */
QueueHandle_t telemetry_v2_get_snapshot_queue(void);

/**
 * @brief Non-blocking wake of the iothub_task event loop via the snapshot queue.
 *        Safe to call from any task (e.g. esp-mqtt event task on reconnect):
 *        it only enqueues a trigger, never publishes.
 */
void telemetry_v2_wake_snapshot(void);

/** @brief Current MQTT connectivity state (true once MQTT_EVENT_CONNECTED). */
bool telemetry_v2_is_connected(void);

/** @brief Current heartbeat interval in seconds (Twin-tunable; default 300). */
int32_t telemetry_v2_get_snapshot_interval_s(void);

/**
 * @brief Start (or restart) the periodic snapshot timer.
 *        Call after QueueSet is set up and on MQTT reconnect.
 */
void telemetry_v2_start_snapshot_timer(void);

// ---------------------------------------------------------------------------
// Publishers — all run in iothub_task context, non-blocking
//
// Before the first clock sync the lifecycle and the snapshot are suppressed, but an
// event (type="event") is built around the unsynced ts and held in the offline buffer,
// never published directly. It gets its real time when the clock syncs
// (offline_buffer_stamp_presync()), or at the latest from telemetry_v2_drain_offline().
// ---------------------------------------------------------------------------

/**
 * A message built and printed, not yet sent (2.1.4 WP2c: a message is built apart from its
 * send, so that the two can run on different tasks). `json` is malloc'd and the caller frees
 * it; `len` is strlen(json). `presync` is true for an event built before the first clock
 * sync (its envelope "ts" is the unsynced time()): it is buffered for the replay, never sent.
 */
typedef struct {
    char   *json;
    size_t  len;
    bool    presync;
} telem_msg_t;

/**
 * Publish type="lifecycle" birth message (online, reset_reason, config).
 * @return true ONLY if it reached esp-mqtt (online, msg_id >= 0); false if it was
 *         refused (the outbox full: -2), dropped offline, or not built. iothub_task
 *         publishes it again while connected until true (2.1.4 WP2).
 *         The same as telemetry_v2_build_lifecycle(), then telemetry_v2_send_lifecycle().
 */
bool telemetry_v2_publish_lifecycle(void);

/**
 * @brief The lifecycle's build: reads the provisioning state (its mutex) and prints the
 *        message into *out (2.1.4 WP2c). false = not built (before the first clock sync, or
 *        out of memory), and *out is not set.
 */
bool telemetry_v2_build_lifecycle(telem_msg_t *out);

/**
 * @brief The lifecycle's send: publishes a message from telemetry_v2_build_lifecycle(), with
 *        the same lines and the same result as telemetry_v2_publish_lifecycle(). Does not
 *        free m->json.
 */
bool telemetry_v2_send_lifecycle(const telem_msg_t *m);

/**
 * @brief Publish type="snapshot" with all current device + sensor state.
 * @param trigger Trigger reason string emitted as data.reason
 *                ("heartbeat" | "event" | "commission" | "boot"); may be NULL.
 * @return true ONLY if the snapshot actually reached esp-mqtt (online, msg_id>=0);
 *         false if dropped offline, suppressed pre-SNTP, deferred because the health
 *         table was busy (its copy timed out: nothing is published rather than empty
 *         device arrays), or not built for lack of memory. The caller re-arms the
 *         heartbeat only on true.
 */
bool telemetry_v2_publish_snapshot(const char *trigger);

/**
 * @brief Publish type="event" for valve position transitions (valve_state_changed).
 * @param valve_id  The valve MAC, already validated by the caller. Required —
 *                  the event is dropped if NULL. Passed in rather than re-read
 *                  here so a GAP disconnect racing the publish cannot strip the
 *                  identity key off an otherwise complete event. Emitted under
 *                  the wire key `valve_id`.
 */
void telemetry_v2_publish_valve_event(const char *event_name, const char *valve_id);

/**
 * @brief One water-detection event, from any source.
 *
 * Since 1.9.0 the valve's own flood probe reports through this same family
 * instead of the separate valve_flood_detected / valve_flood_cleared events, so
 * the cloud has ONE handler for "water was detected somewhere", discriminated by
 * source_type.
 *
 * Required core, emitted for every source:
 *      event, source_type, <identity>, leak_state, battery, location
 * where <identity> is `valve_id` when source_type is "valve" and `sensor_id`
 * otherwise — same position, same string form, key named for the device type.
 * Source-specific extras, emitted only where they apply:
 *      sensors: rssi        valve: valve_state, rmleak, fw_version
 */
typedef struct {
    const char   *event;         // "leak_detected" | "leak_cleared"
    leak_source_t source;        // selects the source_type spelling, the identity
                                 // KEY NAME, and the sensor_meta table — never
                                 // re-derived from a string, so a call-site typo
                                 // cannot silently look up the wrong table.
    const char   *device_id;     // the identity VALUE: valve MAC | sensor MAC |
                                 // "0xNNNNNNNN". A struct field name, not a wire
                                 // key — the key it lands under is valve_id or
                                 // sensor_id, chosen from `source`. No outbound
                                 // message has carried a literal device_id since
                                 // 2.1.0.
    bool          leak_state;    // the value that selected `event`, not a re-read
    uint8_t       battery;       // percent 0-100, 0xFF = unknown (published as null)

    bool          has_rssi;      // sensors only — the valve link has no cached RSSI
    int8_t        rssi;          // dBm, as heard at the hub

    bool          has_valve_ext; // valve only
    const char   *valve_state;   // "open" | "closed" | "unknown"
    bool          rmleak;
    const char   *fw_version;    // NULL -> key omitted (DIS read failed)
} telem_leak_event_t;

/** Publish type="event" for a water-detection transition from any source. */
void telemetry_v2_publish_leak_event(const telem_leak_event_t *ev);

/** Publish type="event" wrapping existing rules-engine JSON in v2 envelope. */
void telemetry_v2_publish_rules_event(const char *rules_json);

/** Publish type="event" command acknowledgment. */
void telemetry_v2_publish_cmd_ack(const char *correlation_id,
                                  const char *cmd_name,
                                  bool success,
                                  const char *error_msg);

/** Publish type="event" wrapping health engine JSON in v2 envelope. */
void telemetry_v2_publish_health_event(const char *health_json);

// ---------------------------------------------------------------------------
// Device Twin integration
// ---------------------------------------------------------------------------

/**
 * @brief Change the heartbeat interval at runtime (from Device Twin desired).
 *
 * Validates against [SNAPSHOT_INTERVAL_MIN_S, SNAPSHOT_INTERVAL_MAX_S] and
 * persists to NVS so the setting survives a reboot. Writes flash only when the
 * value actually changes, so a Twin that re-delivers the same desired document
 * on every reconnect costs no erase cycles.
 *
 * @return true if accepted and applied; false if out of range (the previous
 *         value is left untouched, in RAM and in flash).
 */
bool telemetry_v2_set_snapshot_interval(int seconds);

/**
 * @brief Restore persisted telemetry settings from NVS.
 *
 * Call once at boot, after nvs_store_init() and before telemetry_v2_init().
 * Absent or out-of-range stored values fall back to the compile-time default,
 * so a corrupt entry degrades to 300 s rather than to something unusable.
 */
void telemetry_v2_load_settings(void);

/**
 * @brief Erase persisted telemetry settings (factory reset).
 *
 * Called from the decommission path alongside the other namespace wipes. Also
 * resets the in-RAM value, so the hub does not keep running on a cadence the
 * flash no longer records.
 */
void telemetry_v2_clear_settings(void);

// ---------------------------------------------------------------------------
// Offline buffer integration
// ---------------------------------------------------------------------------

/** Set MQTT connectivity state. When false, event telemetry is buffered to NVS. true also
 *  starts the next session generation (telemetry_v2_session_gen()): only the
 *  MQTT_EVENT_CONNECTED handler, on the esp-mqtt task, calls it with true. */
void telemetry_v2_set_connected(bool connected);

/**
 * @brief The MQTT session's generation (2.1.4 WP2c): how many CONNECTEDs have marked the
 *        session connected since boot. Read after telemetry_v2_is_connected() returned true,
 *        it is that session's number or a later one; a change between two reads means a
 *        session ended and another began in between. Any task; never waits.
 */
uint32_t telemetry_v2_session_gen(void);

/** Drain all NVS-buffered events via MQTT. Call on reconnect before lifecycle. Does nothing
 *  while the client is not connected (2.1.4 WP2). */
void telemetry_v2_drain_offline(void);

/**
 * True while events wait in the offline buffer with the client connected: an event the
 * outbox refused for room (msg_id -2), or the rest of a drain cut short. iothub_task then
 * calls telemetry_v2_drain_offline() again while connected (2.1.4 WP2).
 */
bool telemetry_v2_replay_owed(void);

#ifdef __cplusplus
}
#endif

#endif // TELEMETRY_V2_H
