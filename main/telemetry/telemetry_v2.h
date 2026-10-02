#ifndef TELEMETRY_V2_H
#define TELEMETRY_V2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
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
// Since 2.1.4 WP2c they BUILD the message there (its ts, and whether it is pre-sync, fixed at
// the build) and hand it to cloud_tx, which sends it (see "The sender" below); iothub_task
// never waits on the network. Not telemetry_v2_publish_cmd_ack(): the esp-mqtt task builds and
// sends its own, inside its session.
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
 * @brief The type="lifecycle" birth message (online, reset_reason, config), built: reads the
 *        provisioning state (its mutex) and prints the message into *out (2.1.4 WP2c).
 *        false = not built (before the first clock sync, or out of memory), and *out is not
 *        set. iothub_task builds it once per CONNECTED and hands it to cloud_tx
 *        (telemetry_v2_tx_post_session()), again every 5 s while esp-mqtt has not taken it.
 */
bool telemetry_v2_build_lifecycle(telem_msg_t *out);

/**
 * @brief Build type="snapshot" with all current device + sensor state, and hand it to
 *        cloud_tx (2.1.4 WP2c: iothub_task never publishes). The result comes back
 *        asynchronously (cloud_tx_snapshot(), app_iothub.c): the heartbeat is re-armed only
 *        on a snapshot esp-mqtt took (msg_id >= 0).
 * @param trigger Trigger reason string emitted as data.reason
 *                ("heartbeat" | "event" | "commission" | "boot"); may be NULL.
 * @param tag     The item's tag (the snapshot's session, device-set sequence and ticket).
 * @param flags   TELEM_TX_FINAL for the decommission's last snapshot, else 0.
 * @return true = built and handed over; false = not built: suppressed pre-SNTP, deferred
 *         because the health table was busy (its copy timed out: nothing is published
 *         rather than empty device arrays), not built for lack of memory, or the FIFO full.
 */
bool telemetry_v2_post_snapshot(const char *trigger, uint32_t tag, uint8_t flags);

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
 *  starts the next session generation (telemetry_v2_session_gen()) and wakes cloud_tx for that
 *  session's replay and lifecycle: only the MQTT_EVENT_CONNECTED handler, on the esp-mqtt
 *  task, calls it with true. */
void telemetry_v2_set_connected(bool connected);

/**
 * @brief The MQTT session's generation (2.1.4 WP2c): how many CONNECTEDs have marked the
 *        session connected since boot. Read after telemetry_v2_is_connected() returned true,
 *        it is that session's number or a later one; a change between two reads means a
 *        session ended and another began in between. Any task; never waits.
 */
uint32_t telemetry_v2_session_gen(void);

/** Drain all NVS-buffered events via MQTT. Call on reconnect before lifecycle. Does nothing
 *  while the client is not connected (2.1.4 WP2). cloud_tx only (2.1.4 WP2c). */
void telemetry_v2_drain_offline(void);

/**
 * True while events wait in the offline buffer with the client connected: an event the
 * outbox refused for room (msg_id -2), or the rest of a drain cut short. cloud_tx then
 * calls telemetry_v2_drain_offline() again while connected (2.1.4 WP2).
 */
bool telemetry_v2_replay_owed(void);

// ---------------------------------------------------------------------------
// The sender, cloud_tx (2.1.4 WP2c, LS-1; the user's decision D1 (i) of 2026-10-02)
//
// iothub_task evaluates the leaks and commands the valve; a publish there could wait 10-20 s
// on a dead WAN (esp-mqtt's write, or its API lock behind esp-mqtt's own write), and every
// leak item waited with it. So iothub_task now only BUILDS the messages, and hands each to
// cloud_tx (app_iothub.c), which makes every esp_mqtt_client_publish() for them, the offline
// buffer's replay and every store into it. The hand-over never waits: a queue send with a 0
// timeout, a flag, or a try-take. Static storage throughout, no heap at rest.
//   - The FIFO (TELEM_TX_FIFO_LEN): events, twin reports (a device-set change's, or one the
//     esp-mqtt task asked for), the snapshot and the decommission's clear, in build order. It
//     owns each item's json; cloud_tx frees it.
//   - The session queue (TELEM_TX_SESSION_LEN): a session's lifecycle and twin, built by
//     iothub_task when it first sees the session connected, tagged with its generation
//     (telemetry_v2_session_gen()); one for a session that has ended is dropped.
//   - The busy mutex: cloud_tx holds it whenever it works, and gives it before it blocks.
//     iothub_task only try-takes it (telemetry_v2_tx_idle_take()).
// ---------------------------------------------------------------------------

#define TELEM_TX_FIFO_LEN     24
#define TELEM_TX_SESSION_LEN  2

typedef enum {
    TELEM_TX_EVENT = 0,     // FIFO: an event (leak, valve, rules, health)
    TELEM_TX_TWIN,          // FIFO (a device-set change, a report owed) or session queue
                            // (a CONNECTED)
    TELEM_TX_LIFECYCLE,     // session queue only
    TELEM_TX_SNAPSHOT,      // FIFO: built only into an idle TX, at most one in flight
    TELEM_TX_DECOM_CLEAR,   // FIFO, no json: the decommission's clear of the offline buffer
} telem_tx_kind_t;

#define TELEM_TX_PRESYNC  0x01u   // an event built before the first clock sync (telem_msg_t)
#define TELEM_TX_FINAL    0x02u   // the decommission's snapshot: no result, never stale

/** One message handed to cloud_tx (12 B). */
typedef struct {
    char    *json;    // malloc'd, NUL-terminated (NULL for TELEM_TX_DECOM_CLEAR); whoever
                      // holds the item frees it
    uint32_t tag;     // the session generation it was built for (twin, lifecycle); for a
                      // snapshot its session, device-set sequence and ticket (app_iothub.c)
    uint8_t  kind;    // telem_tx_kind_t
    uint8_t  flags;   // TELEM_TX_PRESYNC, TELEM_TX_FINAL
    uint16_t seq;     // TELEM_TX_TWIN: the report's build number, in build order (app_iothub.c,
                      // 2.1.4 TW-1); 0 for the other kinds
} telem_tx_item_t;

/** iothub_task, once, right after it created cloud_tx: the task the hand-over wakes. */
void telemetry_v2_tx_set_consumer(TaskHandle_t task);

/** Any task: wakes cloud_tx (a task notification). Never waits. */
void telemetry_v2_tx_kick(void);

/**
 * @brief iothub_task: hands one item to cloud_tx through the FIFO, never waiting, and wakes it.
 *        false = the FIFO is full: the item's json is freed, and when `what` is set the
 *        E line "TX queue full (24) - <what> not sent" says what was lost.
 */
bool telemetry_v2_tx_post(telem_tx_item_t *it, const char *what);

/**
 * @brief iothub_task: hands a session's lifecycle or twin to cloud_tx through the session
 *        queue, never waiting. false = the queue is full: the json is freed, and the caller
 *        owes the message again.
 */
bool telemetry_v2_tx_post_session(telem_tx_item_t *it);

/** cloud_tx: the next item of the FIFO, or of the session queue, without waiting. */
bool telemetry_v2_tx_take(telem_tx_item_t *out);
bool telemetry_v2_tx_take_session(telem_tx_item_t *out);

/** Any task: how many items wait in the FIFO. */
unsigned telemetry_v2_tx_queued(void);

/** cloud_tx: the busy mutex, around each round of its work (it waits only for an iothub_task
 *  snapshot build, milliseconds). */
void telemetry_v2_tx_busy_take(void);
void telemetry_v2_tx_busy_give(void);

/**
 * @brief iothub_task: "TX idle" - both queues empty and the busy mutex free - and if so, the
 *        mutex is taken, so cloud_tx starts no work until telemetry_v2_tx_idle_give(). Never
 *        waits (a try-take).
 */
bool telemetry_v2_tx_idle_take(void);
void telemetry_v2_tx_idle_give(void);

/**
 * @brief iothub_task, before it takes a health alert (device_offline / device_recovered):
 *        whether one may go to cloud_tx now. Yes while fewer than 16 items wait in the FIFO,
 *        and either TX is idle or internal DMA-capable heap has 12 KB free with a 4.5 KB
 *        block. Otherwise the alerts wait, losslessly, for TX to go idle: a stall must not
 *        pile health events on top of a stalled session's heap (WP2c section 2.3). Sets the
 *        heap figures it read (0 when it did not need them).
 */
bool telemetry_v2_tx_health_admit(size_t *free_b, size_t *largest);

/**
 * @brief cloud_tx: sends one event from the FIFO, or stores it in the offline buffer, with
 *        the same lines and rules as before (replay first, behind the buffered ones, offline,
 *        pre-sync, the outbox full, kept when unconfirmed). Published only into session
 *        `gen`, the one whose replay and lifecycle cloud_tx has done. Does not free it->json.
 */
void telemetry_v2_tx_send_event(const telem_tx_item_t *it, uint32_t gen);

/** cloud_tx: whether a message is still the one to send (a snapshot: its session and its
 *  device set, app_iothub.c), given the item's tag. */
typedef bool (*telem_tx_current_fn)(uint32_t tag);

/** telemetry_v2_tx_publish()'s answer when `current` said no after the "Pub" line: nothing
 *  was published. Never an esp-mqtt msg_id (those are >= -2). */
#define TELEM_TX_NOT_CURRENT  (-100)

/**
 * @brief cloud_tx: publishes json (a snapshot, the lifecycle) with the "Pub" line and its
 *        "failed" line. The caller holds the publish gate (iothub_pub_begin()); this gives it
 *        back right after the publish. The msg_id: >= 0 = esp-mqtt took it.
 *        `current` (NULL: none) is asked again after the "Pub" line, right before the write: a
 *        full-hub snapshot's line holds the console for about 0.9 s. If it says no, nothing is
 *        published, the gate is given back, a W line says so, and TELEM_TX_NOT_CURRENT is
 *        returned.
 */
int telemetry_v2_tx_publish(const char *json, const char *type_hint,
                            telem_tx_current_fn current, uint32_t tag);

/**
 * @brief iothub_task, at a decommission whose clear cloud_tx did not finish in time: from now
 *        on cloud_tx publishes, stores, drains and stamps nothing (the items it still takes
 *        are dropped), so the offline buffer erased right after stays empty until the
 *        restart (WP2c section 2.8, R1-7).
 */
void telemetry_v2_tx_freeze(void);
bool telemetry_v2_tx_frozen(void);

#ifdef __cplusplus
}
#endif

#endif // TELEMETRY_V2_H
