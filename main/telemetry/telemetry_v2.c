#include "telemetry_v2.h"
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "nvs.h"
#include "cJSON.h"
#include "nvs_store/nvs_store.h"

#include "app_ble_valve.h"
#include "provisioning_manager.h"
#include "sensor_meta.h"
#include "health_engine.h"
#include "rules_engine.h"
#include "offline_buffer.h"
#include "hub_identity.h"
#include "app_iothub.h"   // iothub_task_handle

#define TELEM_TAG "TELEMETRY_V2"

// Settings live in the dedicated commissioning partition, not the default "nvs".
// The physical button wipes only WiFi credentials from the default partition, and
// app_main()'s corruption recovery can erase all of it — a user who chose a 15-min
// cadence should not silently get 5-min back after re-entering their WiFi password.
// Cleared deliberately by decommission, which is the factory-reset path.
#define NVS_NS_TELEM       "telemetry"
#define NVS_KEY_SNAP_INT   "snap_int"

// ---- Module state ----------------------------------------------------------
// The client, its topic and the caches are set by iothub_task, which builds every message here
// but the esp-mqtt task's cmd_ack. Since 2.1.4 WP2c cloud_tx sends them (see "The hand-over"
// below): the client and topic are set before the client starts, so before any session it
// sends into. s_connected has its own rules (telemetry_v2_set_connected()).

static esp_mqtt_client_handle_t s_mqtt   = NULL;
static char s_device_id[64]              = {0};
static char s_gateway_id[32]             = {0};
static char s_topic[128]                 = {0};

static const telem_lora_cache_t     *s_lora_cache = NULL;
static const telem_ble_leak_cache_t *s_ble_cache  = NULL;

static bool           s_connected      = false;
static TimerHandle_t  s_snapshot_timer = NULL;
static QueueHandle_t  s_snapshot_queue = NULL;

// The MQTT session's generation (2.1.4 WP2c): how many MQTT_EVENT_CONNECTEDs have marked the
// session connected since boot. One writer, telemetry_v2_set_connected(true), which only the
// CONNECTED handler calls (the esp-mqtt task), so its plain increment is safe. Stored before
// s_connected, both with release: a task that reads s_connected true and then this reads
// that session's number or a later one. A change across a publish means that the session it
// was handed to ended and another began (send_str()'s A-1, replay_publish()).
static volatile uint32_t s_sess_gen   = 0;

// Events wait in the offline buffer although the client is connected: an event the outbox
// refused for room (send_str()), or the rest of a drain cut short. Set by whichever task
// published, cleared by the drain (cloud_tx, 2.1.4 WP2c); a lost race costs one retry's delay.
// While it is set, or anything is buffered, cloud_tx's next event drains first and never
// overtakes them (send_str()), and cloud_tx replays them every 10 s while connected.
static volatile bool  s_replay_owed    = false;

// Heartbeat interval in SECONDS. Written by the Twin desired-property handler
// (telemetry_v2_set_snapshot_interval, esp-mqtt task) as a single 32-bit store —
// naturally atomic on the 32-bit Xtensa core — and read by the iothub_task
// snapshot scheduler. The actual heartbeat cadence is driven by that scheduler's
// monotonic deadline, not by s_snapshot_timer (which stays a fixed liveness backstop).
static volatile int32_t s_snap_interval_s = SNAPSHOT_INTERVAL_MS / 1000;

// ---- The hand-over to cloud_tx (2.1.4 WP2c; see telemetry_v2.h) -----------
// Created once by telemetry_v2_init(), from static storage (cannot fail). iothub_task posts
// with a 0 timeout and try-takes s_busy; cloud_tx takes the items and holds s_busy while it
// works. s_tx_task is written once, before the first post.
static StaticQueue_t     s_txq_buf;
static uint8_t           s_txq_store[TELEM_TX_FIFO_LEN * sizeof(telem_tx_item_t)];
static QueueHandle_t     s_txq     = NULL;
static StaticQueue_t     s_sessq_buf;
static uint8_t           s_sessq_store[TELEM_TX_SESSION_LEN * sizeof(telem_tx_item_t)];
static QueueHandle_t     s_sessq   = NULL;
static StaticSemaphore_t s_busy_buf;
static SemaphoreHandle_t s_busy    = NULL;
static TaskHandle_t      s_tx_task = NULL;
// Set once, by iothub_task, at a decommission whose clear did not finish (telemetry_v2_tx_freeze()).
static volatile bool     s_tx_frozen = false;

_Static_assert(sizeof(telem_tx_item_t) == 12, "the FIFO's item is 12 B (WP2c section 2.2)");

// Health alerts go to cloud_tx while TX is idle, or while fewer than this many items wait and
// the heap has room for their builds on top of a stalled session (telemetry_v2_tx_health_admit()).
// 8 of the FIFO's 24: at least 16 slots are always left for the leak, valve and rules events
// (2.1.4 SAFE-1, the user's decision of 2026-10-02; 16 until then left as few as 8).
#define TX_HEALTH_MAX_QUEUED   8
#define TX_HEALTH_FREE_MIN     (12 * 1024)   // G3's 8 KB floor plus one event's build
#define TX_HEALTH_LARGEST_MIN  (4608)        // G3's 4.5 KB block
#define TX_HEALTH_HEAP_CAPS    (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)

// ---- Helpers --------------------------------------------------------------

// Single source of truth for the hub firmware version: the ESP-IDF application
// descriptor, populated from PROJECT_VER in the top-level CMakeLists.txt. This
// is the SAME string shown in the boot banner ("App version") and the OTA image
// header, so gateway.fw / twin fw_version can never drift from the build version.
const char *telemetry_v2_fw_version(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    return (desc && desc->version[0]) ? desc->version : "0.0.0";
}

// Minimum epoch to consider time synced (2024-01-01 00:00:00 UTC)
#define EPOCH_VALID_THRESHOLD_TELEM  1704067200

// Out of memory part-way through building a message: frees it and says so. Nothing of it
// is published or buffered.
static void drop_unbuilt(cJSON *root, const char *what)
{
    ESP_LOGE(TELEM_TAG, "Message not built (%s) - out of memory", what);
    cJSON_Delete(root);
}

static cJSON *build_envelope(const char *type)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "schema", TELEMETRY_SCHEMA);

    time_t now;
    time(&now);

    /* Before the first clock sync. A snapshot, the lifecycle or anything else that is not
     * an event is suppressed, as always: it is regenerated after connect, and the snapshot
     * scheduler relies on this NULL. An EVENT is built as normal around the unsynced ts
     * and held in the offline buffer (publish_json); the drain stamps it with the real
     * time once the clock has synced. Leak protection now runs before Wi-Fi (N1), so a
     * leak_detected / auto_close raised while the router is still down used to be lost
     * here for good - the leak delta caches had already recorded the wet state. */
    if (now < EPOCH_VALID_THRESHOLD_TELEM) {
        if (strcmp(type, "event") != 0) {
            ESP_LOGW(TELEM_TAG, "Time not synced (ts=%ld) — suppressing %s", (long)now, type);
            cJSON_Delete(root);
            return NULL;
        }
        ESP_LOGW(TELEM_TAG,
                 "Time not synced (ts=%ld) - holding %s for replay; stamped when the clock syncs",
                 (long)now, type);
    }

    // "ts" stays the SECOND key, right after "schema": the offline buffer finds it by text
    // scan (the first "ts": in the message) when it stamps a pre-sync event, and reads
    // gateway.uptime_s, below, the same way.
    cJSON_AddNumberToObject(root, "ts", (double)now);

    // Created already attached, so it is either in the tree or freed: a detached object
    // leaked its whole subtree when the attach (the key's strdup) failed. Nothing goes out
    // without its gateway - a pre-sync event without gateway.uptime_s cannot be stamped.
    cJSON *gw = cJSON_AddObjectToObject(root, "gateway");
    if (!gw) {
        drop_unbuilt(root, type);
        return NULL;
    }
    cJSON_AddStringToObject(gw, "id", s_gateway_id);
    cJSON_AddStringToObject(gw, "short_id", hub_identity_get_short_id());
    const char *hub_name = hub_identity_get_name();
    if (hub_name[0])
        cJSON_AddStringToObject(gw, "name", hub_name);
    cJSON_AddStringToObject(gw, "fw", telemetry_v2_fw_version());
    cJSON_AddNumberToObject(gw, "uptime_s",
                            (double)(esp_timer_get_time() / 1000000));

    cJSON_AddStringToObject(root, "type", type);

    return root;
}

// A message's build: prints root into *out and frees root (2.1.4 WP2c: publish_json() is
// split into this and send_str(), so that a message can be built on one task and sent on
// another). false on a NULL root or out of memory, with nothing left to free; otherwise
// out->json is the caller's to free.
static bool build_str(cJSON *root, telem_msg_t *out)
{
    if (!root) return false;

    // An event built before the first clock sync carries the unsynced time() in "ts"
    // (build_envelope). Decided from the envelope's own number, never a fresh time(): a
    // sync landing between the build and the send must not let that ts onto the wire.
    const cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "ts");
    bool presync = cJSON_IsNumber(ts) &&
                   ts->valuedouble < (double)EPOCH_VALID_THRESHOLD_TELEM;

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return false;

    out->json    = json_str;
    out->len     = strlen(json_str);
    out->presync = presync;
    return true;
}

// The "Pub" line, the publish and its "failed" line. gated: the caller holds the publish gate
// (iothub_pub_begin(), app_iothub.c), given back right after the publish, so that an MQTT stop
// waiting for it (wifi_task) waits for this one write, and none of the lines after it.
// current (cloud_tx's snapshot; NULL otherwise): asked again after the line, right before the
// write. A C2D device-set change can land while a full-hub snapshot's line prints (about 0.9 s)
// and ack first; that snapshot, built before it, is then not sent (E-10, R1-3).
static int publish_logged(const char *json_str, const char *type_hint, bool gated,
                          telem_tx_current_fn current, uint32_t tag)
{
    ESP_LOGI(TELEM_TAG, "Pub %s: %s", type_hint, json_str);
    if (current != NULL && !current(tag)) {
        if (gated)
            iothub_pub_end(NULL, 0, 0);
        ESP_LOGW(TELEM_TAG, "Pub %s not sent - the device set or session changed during its line; built again",
                 type_hint);
        return TELEM_TX_NOT_CURRENT;
    }
    int64_t t0 = esp_timer_get_time();
    int msg_id = esp_mqtt_client_publish(s_mqtt, s_topic, json_str, 0, 1, 0);
    if (gated)
        iothub_pub_end(type_hint, msg_id, t0);
    if (msg_id < 0)
        ESP_LOGW(TELEM_TAG, "Pub %s failed (msg_id=%d)", type_hint, msg_id);
    return msg_id;
}

// cloud_tx's store into the offline buffer (it may wait for the buffer's lock): none once a
// decommission's fallback has erased the buffer for the restart, even when the freeze landed
// during this event's publish (telemetry_v2_tx_freeze()).
static bool tx_store(const char *json, size_t len)
{
    return !s_tx_frozen && offline_buffer_store(json, len);
}

// A message's send: publishes a message from build_str(), or buffers or drops it. Does not
// free m->json. Runs on cloud_tx for what iothub_task built (2.1.4 WP2c), and on the esp-mqtt
// task for its own cmd_ack; never on iothub_task, which evaluates the leaks (refused, E line).
// `gen`: on cloud_tx, the session whose replay and lifecycle it has done, the only one an
// event may go into; NULL elsewhere.
// Returns true ONLY when the message actually reached esp-mqtt (online branch
// taken AND esp_mqtt_client_publish accepted it, msg_id >= 0). Returns false
// offline (buffered or dropped), or on a negative msg_id (e.g. outbox
// saturated). A pre-sync event is always buffered, never sent, so it returns false too.
// (The snapshot's result, which re-arms the heartbeat, is cloud_tx_snapshot()'s, app_iothub.c.)
static bool send_str(const telem_msg_t *m, const char *type_hint, const uint32_t *gen)
{
    const char *json_str = m->json;
    bool presync = m->presync;

    // The guard (2.1.4 WP2c): every publish of iothub_task's messages is cloud_tx's. Should
    // never print; the message is dropped rather than wait on the network there.
    if (xTaskGetCurrentTaskHandle() == iothub_task_handle) {
        ESP_LOGE(TELEM_TAG, "%s called on iothub_task - refused", __func__);
        return false;
    }

    bool sent = false;
    bool is_event  = (strcmp(type_hint, "event") == 0);
    bool on_tx     = (s_tx_task != NULL && xTaskGetCurrentTaskHandle() == s_tx_task);
    // cloud_tx's publishes take the publish gate: never beside wifi_task's MQTT stop
    // (app_iothub.c, "MQTT stop / resume"). Not the esp-mqtt task's cmd_ack: it runs on its
    // own client's task, which that stop ends.
    bool gated     = on_tx;
    bool online    = s_mqtt && s_connected && !presync;

    // A decommission's fallback has erased the offline buffer for the restart: nothing more
    // goes out or into it (telemetry_v2_tx_freeze()).
    if (on_tx && s_tx_frozen)
        return false;

    // Events wait in the offline buffer although the client is connected (refused for room,
    // or the rest of a drain cut short): an event from cloud_tx must not overtake them,
    // or a consequence reaches the cloud before its cause (F-08: leak_detected and
    // auto_close kept for the replay, then the valve's "closed" published first). They are
    // offered first; if the outbox still refuses some, this event waits behind them for the
    // replay. Not on the esp-mqtt task (a cmd_ack, an answer, not a cause): it must not
    // drain, as it holds esp-mqtt's API lock (see the -2 refusal below).
    // Whenever any are buffered (offline_buffer_pending(), 2.1.4 WP2c), not only while the
    // replay is owed: a drain cut short by its session's end leaves the flag clear, so on the
    // pass where the next CONNECTED lands after the lifecycle block, this pass's events
    // overtook the buffered ones (HANDOFF 15m residual 8); and a drain whose count read timed
    // out left them unowed (15i residual 8).
    bool behind = false;
    if (online && is_event && on_tx && (s_replay_owed || offline_buffer_pending() > 0)) {
        telemetry_v2_drain_offline();
        behind = s_replay_owed || offline_buffer_pending() > 0;
        // Read again: a replay publish whose write fails ends the session (esp-mqtt aborts and
        // dispatches DISCONNECTED on this task). The event is then buffered, not handed to a
        // client that a link loss may stop with it still in its outbox.
        online = s_connected;
        // Then it is stored as offline, under the offline line, as before 2.1.4 WP2c: what
        // is still buffered is the next connect's replay, not an outbox refusal (the store
        // is the same, behind them).
        if (!online)
            behind = false;
    }

    // Published only under the gate, with the session up (connected, its stop not asked:
    // iothub_pub_begin()) and, on cloud_tx, still the session `gen`. One that began since has
    // its replay and lifecycle to go first (cloud_tx_session_work(), app_iothub.c): this event
    // is buffered instead, and that replay sends it in its place. Otherwise as offline.
    if (online && !behind && gated) {
        online = iothub_pub_begin();
        if (online && gen && telemetry_v2_session_gen() != *gen) {
            iothub_pub_end(NULL, 0, 0);
            online = false;
        }
    }

    if (online && !behind) {
        // Online: publish directly
        uint32_t pub_gen = telemetry_v2_session_gen();   // the session it is handed to (A-1)
        int msg_id = publish_logged(json_str, type_hint, gated, NULL, 0);
        sent = (msg_id >= 0);
        // -2: refused for room (the outbox limit, app_iothub.c build_mqtt_cfg()), nothing
        // queued. An event is kept for the replay rather than lost; cloud_tx replays it
        // while connected (telemetry_v2_replay_owed()). A -1 is kept by A-1, below.
        // Off cloud_tx - the esp-mqtt task, for a cmd_ack, inside its event handler with
        // esp-mqtt's API lock held - the buffer is not waited for: cloud_tx's drain holds
        // it while it waits for that lock (offline_buffer_drain()), so a wait would stall both
        // tasks for the buffer's 1 s timeout and keep nothing.
        if (msg_id == -2 && is_event) {
            bool kept = on_tx ? tx_store(json_str, m->len)
                              : offline_buffer_try_store(json_str, m->len);
            if (kept) {
                s_replay_owed = true;
                ESP_LOGW(TELEM_TAG, "Outbox full - %s kept for replay", type_hint);
            } else {
                ESP_LOGW(TELEM_TAG, "Outbox full - %s not kept", type_hint);
            }
        } else if (is_event && on_tx &&
                   (msg_id < 0 || !s_connected || telemetry_v2_session_gen() != pub_gen)) {
            // A-1 (2.1.4 WP2c; the user's decision D3 of 2026-10-02, at-least-once): an event
            // esp-mqtt did not take (-1: a failed write, such as the LS-1 write into a dead
            // WAN that times out and ends the session, or no memory), or took into a session
            // that did not stay up, is also kept for the replay. Such a message sits at most
            // in an outbox that the stop at a link loss deletes and that expires its items
            // after 30 s; it used to be lost. The session's end is known by now: esp-mqtt
            // dispatches DISCONNECTED on the publishing task before its publish returns, and
            // on any other task before it releases the API lock this publish then took. The
            // outbox can still deliver its copy too, possibly after newer events. The copy kept
            // here is the published text, and the replay publishes it unchanged, so both copies
            // are the same bytes, ts included: the cloud drops a message only when it is
            // byte-identical to one it already has (the payload, or a hash of it; the user's
            // decision on TC-2, 2026-10-02) and keeps the first copy, so two different messages
            // are never merged. Not the esp-mqtt task's cmd_ack (it cannot wait for the buffer,
            // above).
            if (tx_store(json_str, m->len)) {
                s_replay_owed = true;
                ESP_LOGW(TELEM_TAG, "Pub %s not confirmed (msg_id=%d) - kept for replay, a duplicate is possible",
                         type_hint, msg_id);
            } else {
                ESP_LOGW(TELEM_TAG, "Pub %s not confirmed (msg_id=%d) - not kept", type_hint, msg_id);
            }
        }
    } else if (is_event) {
        if (presync) {
            // Built before the first clock sync (build_envelope() has logged it). Buffered
            // even when online - MQTT cannot normally connect before the clock syncs - so
            // the drain stamps it with the real time before it reaches the cloud.
            offline_buffer_store_presync(json_str, m->len);
            // Still in the FIFO when the clock synced, cloud_tx stores it only now: stamped in
            // NVS now, as the others were at the sync (offline_buffer_stamp_presync(); a no-op
            // while the clock is still unsynced).
            if (on_tx)
                offline_buffer_stamp_presync();
        } else if (behind) {
            // Behind the events the replay could not send yet (see above), in order.
            if (tx_store(json_str, m->len))
                ESP_LOGW(TELEM_TAG, "Outbox full - %s kept for replay, behind the buffered ones", type_hint);
            else
                ESP_LOGW(TELEM_TAG, "Outbox full - %s not kept", type_hint);
        } else {
            // Offline: buffer critical events for replay on reconnect. The esp-mqtt task does
            // not wait for the buffer (its cmd_ack, after a stop was asked: see the -2 refusal).
            ESP_LOGW(TELEM_TAG, "Offline — buffering %s event", type_hint);
            if (on_tx)
                tx_store(json_str, m->len);
            else
                offline_buffer_try_store(json_str, m->len);
        }
    } else {
        // Offline: drop lifecycle/snapshot (regenerated on reconnect)
        ESP_LOGD(TELEM_TAG, "Offline — dropping %s (regenerated)", type_hint);
    }

    return sent;
}

// publish_json() on iothub_task: builds the event and hands it to cloud_tx. The event's name
// (data.event) is read before the build frees root, so that the line saying it was lost, should
// the FIFO be full, names it. Apart, and not inlined, so that its buffer is not on the esp-mqtt
// task's stack, whose cmd_ack goes through publish_json() too.
static __attribute__((noinline)) void post_event(cJSON *root, const char *type_hint)
{
    char what[48];
    const char *ev = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(root, "data"), "event"));
    if (ev != NULL)
        snprintf(what, sizeof(what), "%s %s", ev, type_hint);
    else
        snprintf(what, sizeof(what), "%s", type_hint);

    telem_msg_t m;
    if (!build_str(root, &m)) return;
    telem_tx_item_t it = {
        .json  = m.json,
        .tag   = 0,
        .kind  = TELEM_TX_EVENT,
        .flags = m.presync ? TELEM_TX_PRESYNC : 0,
        .seq   = 0,
    };
    telemetry_v2_tx_post(&it, what);   // frees it if the FIFO is full
}

// Builds one event and sends it (build_str(), then send_str()), or, on iothub_task, hands it
// to cloud_tx (2.1.4 WP2c, LS-1): iothub_task evaluates the leaks and commands the valve, and
// never waits on the network. The event's ts, and whether it is pre-sync, are fixed here, at
// the build. send_str()'s result; false on a NULL root, a message not built, or one handed on.
static bool publish_json(cJSON *root, const char *type_hint)
{
    if (xTaskGetCurrentTaskHandle() == iothub_task_handle) {
        post_event(root, type_hint);
        return false;
    }
    telem_msg_t m;
    if (!build_str(root, &m)) return false;
    bool sent = send_str(&m, type_hint, NULL);
    free(m.json);
    return sent;
}

static const char *reset_reason_str(void)
{
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "power_on";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:       return "watchdog";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_DEEPSLEEP: return "deep_sleep";
        default:                return "unknown";
    }
}

// false = the location object, or its code or label, could not be allocated. The snapshot
// fails its whole build on that (L14); the event path ignores it and ships whatever was
// added, as before.
static bool add_location_obj(cJSON *parent, sensor_type_t type,
                             const char *sensor_id)
{
    sensor_meta_entry_t meta;   // a copy, never a pointer into the table (L16)
    bool have_meta = sensor_meta_get(type, sensor_id, &meta);
    cJSON *loc = cJSON_AddObjectToObject(parent, "location");   // created + attached, or NULL
    if (!loc) return false;
    if (!cJSON_AddStringToObject(loc, "code",
            sensor_meta_location_code_to_str(
                have_meta ? meta.location_code : LOC_UNKNOWN)))
        return false;
    return cJSON_AddStringToObject(loc, "label", have_meta ? meta.label : "") != NULL;
}

// location for a water-detection source. `location` is part of the required core
// of a leak event, so it is emitted for EVERY source — including the valve, which
// has no sensor_meta entry (sensor_type_t has no valve member and the C2D
// sensor_meta command cannot address one). The valve therefore always reports
// {"code":"unknown","label":""} rather than being looked up in the wrong table.
static void add_location_for_source(cJSON *parent, leak_source_t source,
                                    const char *device_id)
{
    switch (source) {
        case LEAK_SOURCE_BLE:
            add_location_obj(parent, SENSOR_TYPE_BLE_LEAK, device_id);
            break;
        case LEAK_SOURCE_LORA:
            add_location_obj(parent, SENSOR_TYPE_LORA, device_id);
            break;
        default: {   // LEAK_SOURCE_VALVE — no metadata exists for the valve
            cJSON *loc = cJSON_AddObjectToObject(parent, "location");   // attached, or NULL
            if (loc) {
                cJSON_AddStringToObject(loc, "code",
                    sensor_meta_location_code_to_str(LOC_UNKNOWN));
                cJSON_AddStringToObject(loc, "label", "");
            }
            break;
        }
    }
}

// source_type spelling comes from leak_source_to_str() in rules_engine.c — one
// definition shared with the auto_close and health events, so they cannot drift.

// Identity key for an event that DESCRIBES a device: the key names the device
// type, so the valve reports valve_id and a leak sensor reports sensor_id. Both
// sit in the same position (3rd key, right after source_type) and carry the same
// string form, so a consumer that switches on source_type reads one key either
// way. Derived from the enum, never from a call-site literal.
//
// Spelling comes from leak_identity_key() in rules_engine.c — the same single
// definition the hub-generated events (auto_close, RMLEAK interlock, health
// alerts) use, so the two families cannot drift apart.
static const char *identity_key_for_source(leak_source_t source)
{
    return leak_identity_key(source == LEAK_SOURCE_VALVE);
}

// ---- System health reason builder ----------------------------------------

#define HEALTH_REASON_MAX_PARTS 8
#define HEALTH_REASON_PART_LEN  64

// Best-effort human label for a leaking device, for the reason string. Falls back
// to the raw id when the device has no commissioned metadata.
//
// Copies into the caller's buffer rather than returning the table pointer, and strips
// the one sequence that matters: `reason` is a COMMA-JOINED list of causes, and the
// label is user/cloud-supplied via provisioning. A label of "kitchen, Valve offline"
// would otherwise forge an extra cause that a consumer splitting on ", " reads as a
// real one. Commas become semicolons; other characters are left alone (cJSON handles
// JSON escaping). The metadata itself arrives as a copy (sensor_meta_get), so `src`
// never points into the sensor_meta table after its lock is released (L16).
static void leak_label_for(const health_device_status_t *d, char *out, size_t out_len)
{
    sensor_meta_entry_t meta;   // function scope: `src` may point at meta.label below
    const char *src;
    if (d->dev_type == HEALTH_DEV_VALVE) {
        src = "valve";
    } else {
        sensor_type_t type = (d->dev_type == HEALTH_DEV_LORA) ? SENSOR_TYPE_LORA
                                                              : SENSOR_TYPE_BLE_LEAK;
        bool have_meta = sensor_meta_get(type, d->dev_id, &meta);
        src = (have_meta && meta.label[0]) ? meta.label : d->dev_id;
    }

    size_t i = 0;
    for (; src[i] != '\0' && i + 1 < out_len; i++) {
        out[i] = (src[i] == ',') ? ';' : src[i];
    }
    out[i] = '\0';
}

static void build_system_health_reason(const health_device_status_t *health,
                                       health_rating_t sys_rating, bool rollup_syncing,
                                       char *buf, size_t buf_len)
{
    // Devices provisioned but never yet heard from this sync cycle. While the sync
    // window is open these are excluded from the roll-up (recalc_system_rating in
    // health_engine.c), so the rating can legitimately read EXCELLENT with devices
    // still unaccounted for — and "All devices healthy" would then be a claim we
    // have not earned.
    //
    // "Unheard" is the health engine's OWN per-device exclusion predicate (`excused`),
    // sampled under the same lock as the rating (see the call site), not the shorter
    // snapshot gate — so this string can never claim a device is still syncing while the
    // rating has already started counting it, or vice versa, which is what produced a
    // published "1 sensor offline" for a healthy sensor that beaconed at 265.6 s.
    // `excused` already exempts a leaking device (we plainly have heard from it), so it
    // can never be tallied as awaiting contact AND bucketed as a cause below.
    // rollup_syncing is the same predicate reduced to one flag; the two agree by
    // construction.
    int unheard = 0;
    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (health[i].in_use && health[i].excused) unheard++;
    }
    bool syncing = (unheard > 0) && rollup_syncing;

    if (sys_rating == HEALTH_EXCELLENT) {
        if (syncing) {
            snprintf(buf, buf_len, "Syncing - waiting for %d device%s",
                     unheard, unheard > 1 ? "s" : "");
        } else {
            snprintf(buf, buf_len, "All devices healthy");
        }
        return;
    }

    // Count devices at system rating level, categorized by root cause
    int leak_count    = 0;
    int offline_count = 0;
    int batt_count    = 0;
    int signal_count  = 0;
    bool valve_offline   = false;
    bool valve_grace     = false;
    bool valve_batt_crit = false;
    bool valve_batt      = false;
    const health_device_status_t *sole_leaker = NULL;

    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (!health[i].in_use || health[i].rating != sys_rating)
            continue;

        const health_device_status_t *d = &health[i];

        // An excused device (never heard, still inside its own excuse window) is already
        // accounted for by the trailing "syncing N devices" part. Bucketing it as
        // "offline" too would have the same device described twice, in contradictory
        // terms.
        //
        // `excused` is the exact predicate recalc_system_rating() uses, and must be. That
        // function exempts leaking devices from the roll-up exclusion, so a wet device can
        // set the rating to critical while ever_seen is still false — the valve
        // especially, since handle_valve_leak() deliberately does not touch ever_seen.
        // `excused` is false for it, so its cause is named here rather than the snapshot
        // publishing rating:"critical" alongside reason:"syncing N devices".
        if (d->excused) continue;

        // LEAK FIRST — it outranks every other cause and must not be bucketed as
        // one. A wet device is rated CRITICAL by compute_*_rating() regardless of
        // its connectivity or battery, so without this branch a leaking sensor fell
        // through to "offline" or "signal weak": the reason string would send
        // someone hunting a dead battery while water was running.
        if (d->leaking) {
            sole_leaker = (leak_count == 0) ? d : NULL;
            leak_count++;
            // Fall through ONLY for a valve that is ALSO unreachable. That is the worst
            // state the system has — the hub cannot close the thing that stops the
            // water — and reporting just "Leak detected: valve" would hide precisely
            // the fact that makes it urgent.
            //
            // Everything else skips: for a sensor, "offline" adds nothing once we know
            // it is wet and would contradict itself; and a CONNECTED valve has no lost
            // link to report (its battery, if also low, is outranked by the leak).
            if (!(d->dev_type == HEALTH_DEV_VALVE && !d->connected)) continue;
        }

        // The cause is the health engine's own (health_device_status_t.cause), set with the
        // rating. It used to be inferred here — "connected valve at a degraded rating means
        // battery low" — which could not name a critical battery at all (BUG-1).
        if (d->dev_type == HEALTH_DEV_VALVE) {
            // A leaking valve reaches this point only when it is ALSO disconnected (see
            // above). Its cause is LEAK, because the leak outranks the link in
            // compute_valve_rating(), so it is bucketed by the link it has lost.
            if (d->cause == HEALTH_CAUSE_LINK || d->leaking) {
                if (sys_rating == HEALTH_CRITICAL) valve_offline = true;
                else                               valve_grace   = true;
            } else if (d->cause == HEALTH_CAUSE_BATTERY) {
                if (d->rating == HEALTH_CRITICAL) valve_batt_crit = true;
                else                              valve_batt      = true;
            }
        } else {
            // Sensor: root cause of this rating
            if (d->cause == HEALTH_CAUSE_LINK)         offline_count++;
            else if (d->cause == HEALTH_CAUSE_BATTERY) batt_count++;
            else if (d->cause == HEALTH_CAUSE_SIGNAL)  signal_count++;
        }
    }

    // Build comma-separated reason string (most critical issues first)
    char parts[HEALTH_REASON_MAX_PARTS][HEALTH_REASON_PART_LEN];
    int n = 0;

    if (leak_count == 1 && sole_leaker) {
        char label[40];
        leak_label_for(sole_leaker, label, sizeof(label));
        snprintf(parts[n++], HEALTH_REASON_PART_LEN, "Leak detected: %s", label);
    }
    else if (leak_count > 1)
        snprintf(parts[n++], HEALTH_REASON_PART_LEN, "%d leaks detected", leak_count);

    // The interlock raises a WARNING floor with no device sitting at that rating to
    // explain it, so it has to name itself. Queried rather than inferred from "no other
    // cause matched": at boot the syncing part below would otherwise claim the rating,
    // reporting "syncing 4 devices" for a hub whose real state is a latched incident.
    //
    // Wording: "latched", NOT "valve closed". The floor tracks the hub's incident latch,
    // which is also set when an active water-access override BLOCKED the close — so the
    // valve may well be open. Asserting it was closed would be a claim the flag does not
    // support; valve.state in the same snapshot is the authority on position.
    if (health_is_interlock_held() && n < HEALTH_REASON_MAX_PARTS)
        snprintf(parts[n++], HEALTH_REASON_PART_LEN, "Leak interlock latched");

    if (valve_offline && n < HEALTH_REASON_MAX_PARTS)
        snprintf(parts[n++], HEALTH_REASON_PART_LEN, "Valve offline");
    if (valve_grace && n < HEALTH_REASON_MAX_PARTS)
        snprintf(parts[n++], HEALTH_REASON_PART_LEN, "Valve disconnected");
    if (valve_batt_crit && n < HEALTH_REASON_MAX_PARTS)
        snprintf(parts[n++], HEALTH_REASON_PART_LEN, "Valve battery critical");
    if (valve_batt && n < HEALTH_REASON_MAX_PARTS)
        snprintf(parts[n++], HEALTH_REASON_PART_LEN, "Valve battery low");
    if (offline_count > 0 && n < HEALTH_REASON_MAX_PARTS)
        snprintf(parts[n++], HEALTH_REASON_PART_LEN, "%d sensor%s offline",
                 offline_count, offline_count > 1 ? "s" : "");
    if (batt_count > 0 && n < HEALTH_REASON_MAX_PARTS)
        snprintf(parts[n++], HEALTH_REASON_PART_LEN, "%d sensor%s battery low",
                 batt_count, batt_count > 1 ? "s" : "");
    if (signal_count > 0 && n < HEALTH_REASON_MAX_PARTS)
        snprintf(parts[n++], HEALTH_REASON_PART_LEN, "%d sensor%s signal weak",
                 signal_count, signal_count > 1 ? "s" : "");
    // Last: informational, so the actionable causes read first.
    if (syncing && n < HEALTH_REASON_MAX_PARTS)
        snprintf(parts[n++], HEALTH_REASON_PART_LEN, "syncing %d device%s",
                 unheard, unheard > 1 ? "s" : "");

    if (n == 0) {
        snprintf(buf, buf_len, "Degraded");
        return;
    }

    buf[0] = '\0';
    for (int i = 0; i < n; i++) {
        if (i > 0) strncat(buf, ", ", buf_len - strlen(buf) - 1);
        strncat(buf, parts[i], buf_len - strlen(buf) - 1);
    }
}

// ---- Snapshot timer callback (runs in timer-daemon context) ---------------

static void snapshot_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    uint8_t trigger = 1;
    // Non-blocking send; if queue is full a snapshot is already pending.
    xQueueSend(s_snapshot_queue, &trigger, 0);
}

// ---- Public API -----------------------------------------------------------

void telemetry_v2_init(esp_mqtt_client_handle_t client,
                       const char *device_id,
                       const char *gateway_id,
                       const telem_lora_cache_t *lora_cache,
                       const telem_ble_leak_cache_t *ble_cache)
{
    s_mqtt = client;
    strncpy(s_device_id, device_id, sizeof(s_device_id) - 1);
    strncpy(s_gateway_id, gateway_id, sizeof(s_gateway_id) - 1);
    snprintf(s_topic, sizeof(s_topic),
             "devices/%s/messages/events/", s_device_id);

    s_lora_cache = lora_cache;
    s_ble_cache  = ble_cache;

    // 1-item queue: timer callback writes here, event loop reads via QueueSet
    s_snapshot_queue = xQueueCreate(1, sizeof(uint8_t));

    // The hand-over to cloud_tx (2.1.4 WP2c): static storage, so none of these can fail.
    s_txq   = xQueueCreateStatic(TELEM_TX_FIFO_LEN, sizeof(telem_tx_item_t),
                                 s_txq_store, &s_txq_buf);
    s_sessq = xQueueCreateStatic(TELEM_TX_SESSION_LEN, sizeof(telem_tx_item_t),
                                 s_sessq_store, &s_sessq_buf);
    s_busy  = xSemaphoreCreateMutexStatic(&s_busy_buf);

    s_snapshot_timer = xTimerCreate("snap_tmr",
                                   pdMS_TO_TICKS(SNAPSHOT_INTERVAL_MS),
                                   pdTRUE,   // auto-reload
                                   NULL,
                                   snapshot_timer_cb);

    // Report the EFFECTIVE interval, which telemetry_v2_load_settings() may
    // already have restored from NVS — not the compile-time default, which was
    // what this line printed before and made a restored value look unapplied.
    ESP_LOGI(TELEM_TAG, "Init: schema=%s interval=%ds",
             TELEMETRY_SCHEMA, (int)s_snap_interval_s);
}

void telemetry_v2_attach_client(esp_mqtt_client_handle_t client,
                                const char *device_id)
{
    s_mqtt = client;
    if (device_id && device_id[0]) {
        strncpy(s_device_id, device_id, sizeof(s_device_id) - 1);
        s_device_id[sizeof(s_device_id) - 1] = '\0';
        snprintf(s_topic, sizeof(s_topic),
                 "devices/%s/messages/events/", s_device_id);
    }
    ESP_LOGI(TELEM_TAG, "Client attached: device=%s", s_device_id);
}

QueueHandle_t telemetry_v2_get_snapshot_queue(void)
{
    return s_snapshot_queue;
}

void telemetry_v2_wake_snapshot(void)
{
    // Non-blocking wake of the iothub_task event loop (e.g. from the esp-mqtt
    // event task on reconnect). Safe from any task: only enqueues, never publishes.
    if (s_snapshot_queue) {
        uint8_t trigger = 1;
        xQueueSend(s_snapshot_queue, &trigger, 0);
    }
}

bool telemetry_v2_is_connected(void)
{
    // Acquire, paired with telemetry_v2_set_connected()'s release: read before
    // telemetry_v2_session_gen(), "connected" comes with that session's generation or a later
    // one (2.1.4 WP2c). The answer itself is unchanged.
    return __atomic_load_n(&s_connected, __ATOMIC_ACQUIRE);
}

int32_t telemetry_v2_get_snapshot_interval_s(void)
{
    return s_snap_interval_s;
}

void telemetry_v2_start_snapshot_timer(void)
{
    if (s_snapshot_timer) {
        xTimerReset(s_snapshot_timer, 0);   // (re)start from now
        ESP_LOGI(TELEM_TAG, "Snapshot timer started (%ds)",
                 SNAPSHOT_INTERVAL_MS / 1000);
    }
}

bool telemetry_v2_set_snapshot_interval(int seconds)
{
    // ONE range rule for every caller (see telemetry_v2.h). Rejecting here rather
    // than at the Twin call site is what guarantees the stored value is always
    // usable: nothing out of range can reach RAM, so nothing out of range can
    // reach flash either.
    if (seconds < SNAPSHOT_INTERVAL_MIN_S || seconds > SNAPSHOT_INTERVAL_MAX_S) {
        ESP_LOGW(TELEM_TAG, "Snapshot interval %ds rejected — outside [%d..%d], keeping %ds",
                 seconds, SNAPSHOT_INTERVAL_MIN_S, SNAPSHOT_INTERVAL_MAX_S,
                 (int)s_snap_interval_s);
        return false;
    }

    if (seconds == s_snap_interval_s) {
        // Azure re-delivers the whole desired document on every twin GET, so this
        // is the common case on each reconnect. Returning early keeps it off the
        // flash — an unconditional write here would burn an erase cycle per
        // reconnect for a value that never changed.
        ESP_LOGD(TELEM_TAG, "Snapshot interval already %ds — no write", seconds);
        return true;
    }

    // Heartbeat cadence is driven by the iothub_task deadline scheduler, which
    // latches this value each iteration. Store it as a single 32-bit (atomic on
    // the Xtensa core) write from this esp-mqtt-task context — do NOT reprogram
    // s_snapshot_timer (it stays a fixed liveness backstop), which would create a
    // dual-cadence drift and a cross-task int64 race on the deadline.
    s_snap_interval_s = seconds;

    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PROV_PARTITION, NVS_NS_TELEM,
                                            NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_i32(h, NVS_KEY_SNAP_INT, (int32_t)seconds);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        // The cadence still changed — the scheduler already latched it. Say so
        // plainly rather than implying the setting is durable when it is not.
        ESP_LOGE(TELEM_TAG,
                 "Snapshot interval %ds applied but NOT persisted (%s) — reverts on reboot",
                 seconds, esp_err_to_name(err));
    } else {
        ESP_LOGI(TELEM_TAG, "Snapshot interval set to %ds (persisted)", seconds);
    }
    return true;
}

void telemetry_v2_load_settings(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PROV_PARTITION, NVS_NS_TELEM,
                                            NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGI(TELEM_TAG, "No stored telemetry settings — snapshot interval %ds (default)",
                 (int)s_snap_interval_s);
        return;
    }

    int32_t stored = 0;
    err = nvs_get_i32(h, NVS_KEY_SNAP_INT, &stored);
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGI(TELEM_TAG, "Snapshot interval %ds (default, none stored)",
                 (int)s_snap_interval_s);
        return;
    }

    // Re-validate on the way out of flash. The range could tighten in a future
    // build, and a value written by an older one must not survive that change.
    if (stored < SNAPSHOT_INTERVAL_MIN_S || stored > SNAPSHOT_INTERVAL_MAX_S) {
        ESP_LOGW(TELEM_TAG,
                 "Stored snapshot interval %ds is outside [%d..%d] — using default %ds",
                 (int)stored, SNAPSHOT_INTERVAL_MIN_S, SNAPSHOT_INTERVAL_MAX_S,
                 SNAPSHOT_INTERVAL_MS / 1000);
        return;
    }

    s_snap_interval_s = stored;
    ESP_LOGI(TELEM_TAG, "Snapshot interval %ds restored from NVS", (int)stored);
}

void telemetry_v2_clear_settings(void)
{
    s_snap_interval_s = SNAPSHOT_INTERVAL_MS / 1000;

    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PROV_PARTITION, NVS_NS_TELEM,
                                            NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TELEM_TAG, "Telemetry settings erase skipped: %s", esp_err_to_name(err));
        return;
    }
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TELEM_TAG, "Telemetry settings cleared — snapshot interval back to %ds",
             (int)s_snap_interval_s);
}

// ---- Lifecycle ------------------------------------------------------------

bool telemetry_v2_build_lifecycle(telem_msg_t *out)
{
    cJSON *root = build_envelope("lifecycle");
    if (!root) return false;

    // Every object here is created already attached ("data" is the last root key anyway),
    // so a failed attach cannot leave a detached subtree to leak. Key order is unchanged.
    cJSON *data = cJSON_AddObjectToObject(root, "data");
    if (!data) {
        drop_unbuilt(root, "lifecycle");
        return false;
    }
    cJSON_AddStringToObject(data, "event", "online");
    cJSON_AddStringToObject(data, "reset_reason", reset_reason_str());
    cJSON_AddBoolToObject(data, "provisioned", provisioning_is_provisioned());

    char valve_mac[18];
    if (provisioning_get_valve_mac(valve_mac))
        cJSON_AddStringToObject(data, "valve_id", valve_mac);

    uint32_t ids[MAX_LORA_SENSORS];
    uint8_t cnt = 0;
    provisioning_get_lora_sensors(ids, &cnt);
    cJSON_AddNumberToObject(data, "lora_sensor_count", cnt);

    char macs[MAX_BLE_LEAK_SENSORS][18];
    uint8_t bcnt = 0;
    provisioning_get_ble_leak_sensors(macs, &bcnt);
    cJSON_AddNumberToObject(data, "ble_leak_sensor_count", bcnt);

    rules_config_t rules;
    if (provisioning_get_rules_config(&rules)) {
        cJSON *r = cJSON_AddObjectToObject(data, "rules");
        if (r) {
            cJSON_AddBoolToObject(r, "auto_close_enabled", rules.auto_close_enabled);
            cJSON_AddNumberToObject(r, "trigger_mask", rules.trigger_mask);
        }
    }

    return build_str(root, out);
}

// ---- Snapshot -------------------------------------------------------------

// build_envelope() does not fail a message over a scalar key it could not add, so that a
// leak event still ships. A snapshot must not ship without one, so it checks its envelope
// here. gateway.name is optional on the wire (sent only when set) and is not checked.
static bool snapshot_envelope_complete(const cJSON *root)
{
    const cJSON *gw = cJSON_GetObjectItemCaseSensitive(root, "gateway");
    return cJSON_GetObjectItemCaseSensitive(root, "schema") &&
           cJSON_GetObjectItemCaseSensitive(root, "ts") &&
           cJSON_GetObjectItemCaseSensitive(root, "type") &&
           cJSON_GetObjectItemCaseSensitive(gw, "id") &&
           cJSON_GetObjectItemCaseSensitive(gw, "short_id") &&
           cJSON_GetObjectItemCaseSensitive(gw, "fw") &&
           cJSON_GetObjectItemCaseSensitive(gw, "uptime_s") &&
           (hub_identity_get_name()[0] == '\0' ||           // name is optional on the wire
            cJSON_GetObjectItemCaseSensitive(gw, "name"));
}

// Every key the snapshot adds goes through this: one that could not be allocated fails
// the whole build. Events do not use it - an event ships with whatever it has.
#define SNAP_ADD(x) do { if (!(x)) goto fail; } while (0)

// The snapshot's build: prints it into *out (2.1.4 WP2c: built apart from its send, like
// every message). false = not built (see telemetry_v2_post_snapshot() in the header), and
// *out is not set.
static bool build_snapshot(const char *trigger, telem_msg_t *out)
{
    cJSON *root = build_envelope("snapshot");
    if (!root) return false;   // pre-SNTP / alloc fail — treated as "not published"
    if (!snapshot_envelope_complete(root)) goto fail;

    // Every container below is attached to its parent the moment it is created (data is
    // the last root key anyway), and every add is checked (SNAP_ADD, add_location_obj). So
    // an allocation failure anywhere in the build frees everything with one
    // cJSON_Delete(root) and returns false - nothing leaks, and a snapshot missing a key is
    // never published or counted as sent; the 5 s retry floor rebuilds it whole (L14).
    // The wire key order is unchanged — keys are still added in the same sequence.
    cJSON *data = cJSON_AddObjectToObject(root, "data");   // created + attached, or NULL
    if (!data) goto fail;

    // Trigger reason (heartbeat | event | commission | boot) — lets the app
    // attribute each snapshot in its event-log-vs-UI-refresh model. (Named
    // `trigger` to avoid colliding with the system_health `char reason[192]` below.)
    if (trigger && trigger[0])
        SNAP_ADD(cJSON_AddStringToObject(data, "reason", trigger));

    // ---- Fetch health device status for all provisioned devices ----
    health_device_status_t health[HEALTH_MAX_DEVICES];
    uint8_t health_count = 0;

    // ---- system_health (worst rating + human-readable reason) ----
    // The table, the syncing flag and the rating come from ONE call, sampled under one
    // health lock after the window deadlines are evaluated. They used to be three reads,
    // and an excuse expiring between them re-rolled the rating under the reason string —
    // e.g. rating:excellent next to "All devices healthy" for a hub that had, microseconds
    // earlier, become critical over a missing sensor. Now they cannot disagree (L15).
    health_rating_t sys_rating = HEALTH_EXCELLENT;
    bool rollup_syncing = false;
    if (!health_get_device_status_all(health, &health_count, &sys_rating, &rollup_syncing)) {
        // Copy failed (mutex timeout). Do NOT publish: the fallback used to ship empty
        // device arrays for a provisioned hub and report success, so the heartbeat was
        // re-armed on a snapshot that lied (N17). false = not published; the flush
        // block's retry floor re-attempts in 5 s.
        ESP_LOGW(TELEM_TAG, "Snapshot deferred - health table busy");
        cJSON_Delete(root);
        return false;
    }

    cJSON *sys_health = cJSON_AddObjectToObject(data, "system_health");
    if (!sys_health) goto fail;
    SNAP_ADD(cJSON_AddStringToObject(sys_health, "rating",
        health_rating_to_str(sys_rating)));
    // 192, not 128: the builder can now emit up to 7 comma-joined parts (leak, the
    // interlock, one valve cause, three sensor causes, syncing), and a leak part carries
    // a user-supplied label. The valve causes (offline / disconnected / battery critical /
    // battery low) are mutually exclusive for the one valve, so at most one valve part is
    // emitted and 192 still holds the longest combination. At 128 the last cause truncated
    // mid-word once six parts were present. strncat is bounded either way, so this only
    // buys fidelity.
    char reason[192];
    if (health_count == 0) {
        // Empty hub (nothing provisioned, or a rules-only provision). "All devices
        // healthy" would claim devices that do not exist.
        snprintf(reason, sizeof(reason), "No devices provisioned");
    } else {
        build_system_health_reason(health, sys_rating, rollup_syncing, reason, sizeof(reason));
    }
    SNAP_ADD(cJSON_AddStringToObject(sys_health, "reason", reason));

    // ---- valve ----
    cJSON *valve = cJSON_AddObjectToObject(data, "valve");
    if (!valve) goto fail;

    // The PROVISIONED valve is the health table's valve entry: iothub_task reconciles the
    // table against provisioning before any flush, on this same task. The block used to be
    // keyed on the LIVE GAP link instead, so a valve still linked while its removal was
    // being torn down was published — as open, battery 65, connected — on a hub with no
    // valve at all (BUG-3), and a hub with no valve got {"state":"disconnected"} (BUG-5).
    const health_device_status_t *valve_hs = NULL;
    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (health[i].in_use && health[i].dev_type == HEALTH_DEV_VALVE) {
            valve_hs = &health[i];
            break;
        }
    }

    // No provisioned valve: the object stays EMPTY ("valve":{}).
    if (valve_hs) {
        // Key is valve_id — the identity key names the device type (valve_id /
        // sensor_id) on every outbound message, whether the device is reporting
        // itself or the hub is reporting about it. Literal rather than
        // leak_identity_key() only because the enclosing object is statically the
        // valve; the dynamic sites all delegate. Always the provisioned MAC.
        SNAP_ADD(cJSON_AddStringToObject(valve, "valve_id", valve_hs->dev_id));

        // Live data only from a link to THIS valve. A live MAC that differs from the
        // provisioned one is never trusted: it would publish another valve's state
        // under this valve's identity.
        char vmac[18];
        bool live = ble_valve_get_mac(vmac) && strcasecmp(vmac, valve_hs->dev_id) == 0;

        if (live) {
            // Sampled once: GAP-up is not GATT-ready, and until the valve's
            // characteristics have been read its state and battery are defaults, not
            // readings. Report them as unknown/null rather than as "closed"/0 %.
            bool ready = ble_valve_is_ready();
            int st = ble_valve_get_state();
            SNAP_ADD(cJSON_AddStringToObject(valve, "state",
                !ready ? "unknown" : st == 1 ? "open" : st == 0 ? "closed" : "unknown"));
            uint8_t batt = ble_valve_get_battery();
            if (ready && batt != 0xFF)
                SNAP_ADD(cJSON_AddNumberToObject(valve, "battery", batt));
            else
                SNAP_ADD(cJSON_AddNullToObject(valve, "battery"));
            SNAP_ADD(cJSON_AddBoolToObject(valve, "leak_state", ble_valve_get_leak()));
            SNAP_ADD(cJSON_AddBoolToObject(valve, "rmleak", ble_valve_get_rmleak_state()));
            SNAP_ADD(cJSON_AddBoolToObject(valve, "connected", true));

            char valve_fw[32];
            if (ble_valve_get_firmware_rev(valve_fw, sizeof(valve_fw)))
                SNAP_ADD(cJSON_AddStringToObject(valve, "fw_version", valve_fw));
            else
                SNAP_ADD(cJSON_AddNullToObject(valve, "fw_version"));
        } else {
            SNAP_ADD(cJSON_AddStringToObject(valve, "state", "disconnected"));
            SNAP_ADD(cJSON_AddBoolToObject(valve, "connected", false));
        }

        // Health metadata
        SNAP_ADD(cJSON_AddStringToObject(valve, "rating",
            health_rating_to_str(valve_hs->rating)));
        if (valve_hs->last_seen_age_s != UINT32_MAX) {
            SNAP_ADD(cJSON_AddNumberToObject(valve, "last_seen_age_s",
                valve_hs->last_seen_age_s));
        } else {
            SNAP_ADD(cJSON_AddNullToObject(valve, "last_seen_age_s"));
        }
    }

    // ---- LoRa sensors (iterate health entries, merge cache data) ----
    cJSON *lora_arr = cJSON_AddArrayToObject(data, "lora_sensors");
    if (!lora_arr) goto fail;
    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (!health[i].in_use || health[i].dev_type != HEALTH_DEV_LORA)
            continue;

        cJSON *s = cJSON_CreateObject();
        if (!s) goto fail;
        cJSON_AddItemToArray(lora_arr, s);   // attached first: freed with root on failure
        SNAP_ADD(cJSON_AddStringToObject(s, "sensor_id", health[i].dev_id));
        SNAP_ADD(cJSON_AddBoolToObject(s, "connected", health[i].connected));
        SNAP_ADD(cJSON_AddStringToObject(s, "rating",
            health_rating_to_str(health[i].rating)));

        if (health[i].last_seen_age_s != UINT32_MAX) {
            SNAP_ADD(cJSON_AddNumberToObject(s, "last_seen_age_s",
                health[i].last_seen_age_s));
        } else {
            SNAP_ADD(cJSON_AddNullToObject(s, "last_seen_age_s"));
        }

        // Merge telemetry data from cache — only when the device is currently
        // connected. A provision/decommission no longer wipes health seen-state: only a
        // newly ADDED device is seeded unheard, survivors keep theirs. The cache follows
        // neither (it keeps a device's last values across a disconnect), so the connected
        // gate is still what stops it resurrecting pre-disconnect data (here snr) beside
        // connected:false.
        const telem_lora_cache_t *cached = NULL;
        if (health[i].connected && s_lora_cache) {
            for (int j = 0; j < TELEM_MAX_LORA_CACHE; j++) {
                if (!s_lora_cache[j].valid) continue;
                char cid[16];
                snprintf(cid, sizeof(cid), "0x%08lX",
                         (unsigned long)s_lora_cache[j].sensor_id);
                if (strcasecmp(cid, health[i].dev_id) == 0) {
                    cached = &s_lora_cache[j];
                    break;
                }
            }
        }

        /* battery / rssi / leak_state from the HEALTH table, for the same reason as the
         * BLE branch below: the cache is delta-gated per packet, the health engine is
         * fed on every packet. Kept symmetrical deliberately — a LoRa install must get
         * the same live placement figures during the post-provision pulse that a BLE
         * one does, and the pulse's packet arm already covers both sources.
         *
         * snr is the one exception: the health engine does not carry it, so it stays
         * cache-sourced (and therefore as stale as the last D2C event). Flagged rather
         * than plumbed — adding snr to the health event is a wider change than this
         * fix warrants, and rssi is the figure used for placement. */
        if (health[i].last_battery != 0xFF)
            SNAP_ADD(cJSON_AddNumberToObject(s, "battery", health[i].last_battery));
        else
            SNAP_ADD(cJSON_AddNullToObject(s, "battery"));

        if (health[i].last_rssi != 0)
            SNAP_ADD(cJSON_AddNumberToObject(s, "rssi", health[i].last_rssi));
        else
            SNAP_ADD(cJSON_AddNullToObject(s, "rssi"));

        /* health[i].leaking, never a literal false and never the cache's copy. An
         * offline-but-WET sensor used to publish leak_state:false in the same snapshot
         * whose system_health.reason said "Leak detected: <its label>" — a document
         * that contradicted itself, with the safer of the two values being the one a
         * consumer reading the array would take. Field stays boolean; schema unchanged. */
        SNAP_ADD(cJSON_AddBoolToObject(s, "leak_state", health[i].leaking));

        if (cached)
            SNAP_ADD(cJSON_AddNumberToObject(s, "snr", cached->snr));
        else
            SNAP_ADD(cJSON_AddNullToObject(s, "snr"));

        if (!add_location_obj(s, SENSOR_TYPE_LORA, health[i].dev_id)) goto fail;
    }

    // ---- BLE leak sensors (iterate health entries, merge cache data) ----
    cJSON *ble_arr = cJSON_AddArrayToObject(data, "ble_leak_sensors");
    if (!ble_arr) goto fail;
    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (!health[i].in_use || health[i].dev_type != HEALTH_DEV_BLE_LEAK)
            continue;

        cJSON *s = cJSON_CreateObject();
        if (!s) goto fail;
        cJSON_AddItemToArray(ble_arr, s);    // attached first: freed with root on failure
        SNAP_ADD(cJSON_AddStringToObject(s, "sensor_id", health[i].dev_id));
        SNAP_ADD(cJSON_AddBoolToObject(s, "connected", health[i].connected));
        SNAP_ADD(cJSON_AddStringToObject(s, "rating",
            health_rating_to_str(health[i].rating)));

        if (health[i].last_seen_age_s != UINT32_MAX) {
            SNAP_ADD(cJSON_AddNumberToObject(s, "last_seen_age_s",
                health[i].last_seen_age_s));
        } else {
            SNAP_ADD(cJSON_AddNullToObject(s, "last_seen_age_s"));
        }

        // Merge telemetry data from cache — only when the device is currently
        // connected (see LoRa note): the cache keeps a device's last values across a
        // disconnect, so without the gate connected:false would ship with its
        // pre-disconnect fw_version.
        const telem_ble_leak_cache_t *cached = NULL;
        if (health[i].connected && s_ble_cache) {
            for (int j = 0; j < TELEM_MAX_BLE_LEAK_CACHE; j++) {
                if (!s_ble_cache[j].valid) continue;
                if (strcasecmp(s_ble_cache[j].mac_str,
                               health[i].dev_id) == 0) {
                    cached = &s_ble_cache[j];
                    break;
                }
            }
        }

        /* battery / rssi / leak_state come from the HEALTH table, NOT the cache.
         *
         * The cache is fed only through ble_leak_rx_queue, which the scanner's
         * telemetry delta gate throttles — and that gate does not even test rssi
         * (app_ble_leak.c tests leak | battery | fw_version only), so an RSSI-only
         * change reaches the cache ONLY when the 5-minute telemetry heartbeat forces
         * an event through. The health engine is fed on EVERY advertisement burst
         * (~100 s), because its check-in sits above that gate.
         *
         * Sourcing the live numbers from the cache defeated the entire point of the
         * post-provision pulse: publishing a snapshot per packet is worthless if the
         * placement figures inside it are up to five minutes old. Observed on the
         * Run A bench capture — a sensor moved mid-window reported rssi:-53 across
         * five consecutive pulse snapshots and only revealed its true -70 at the
         * following heartbeat, 300 s later.
         *
         * The staleness concern the `connected` gate was protecting against is handled
         * for free here: health_engine_reconcile_devices() seeds a newly added device
         * with last_battery 0xFF and last_rssi 0, so it emits null rather than stale
         * values — and no cache fallback can resurrect them.
         *
         * fw_version still comes from the cache: the health table does not carry it and
         * it cannot change at runtime, so staleness is not a concern for that one. */
        if (health[i].last_battery != 0xFF)
            SNAP_ADD(cJSON_AddNumberToObject(s, "battery", health[i].last_battery));
        else
            SNAP_ADD(cJSON_AddNullToObject(s, "battery"));

        if (health[i].last_rssi != 0)
            SNAP_ADD(cJSON_AddNumberToObject(s, "rssi", health[i].last_rssi));
        else
            SNAP_ADD(cJSON_AddNullToObject(s, "rssi"));

        /* Always the health engine's wet/dry, connected or not — it is both the
         * freshest source and the one that cannot contradict system_health.reason for
         * an offline-but-wet sensor. */
        SNAP_ADD(cJSON_AddBoolToObject(s, "leak_state", health[i].leaking));

        if (cached && cached->fw_version[0])
            SNAP_ADD(cJSON_AddStringToObject(s, "fw_version", cached->fw_version));
        else
            SNAP_ADD(cJSON_AddNullToObject(s, "fw_version"));

        if (!add_location_obj(s, SENSOR_TYPE_BLE_LEAK, health[i].dev_id)) goto fail;
    }

    // ---- Rules config (master auto-close enable + trigger mask) ----
    // Mirrors the lifecycle "rules" object EXACTLY (same shape + guard) so the
    // app reuses one parser. Kept as a GLOBAL object, disjoint from any future
    // per-sensor auto_close flag, so per-sensor shutoff stays purely additive.
    rules_config_t rules;
    if (provisioning_get_rules_config(&rules)) {
        cJSON *r = cJSON_AddObjectToObject(data, "rules");
        if (!r) goto fail;
        SNAP_ADD(cJSON_AddBoolToObject(r, "auto_close_enabled", rules.auto_close_enabled));
        SNAP_ADD(cJSON_AddNumberToObject(r, "trigger_mask", rules.trigger_mask));
    }

    // ---- Override window status ----
    // Single atomic read of all three fields (no TOCTOU). override_active and
    // override_remaining_s keep their exact prior semantics; expires_ts is an
    // additive ABSOLUTE epoch (same field name as the override event) emitted only
    // when active and the expiry is known/in-future.
    bool     ovr_active    = false;
    int32_t  ovr_remaining = -1;
    uint32_t ovr_expires   = 0;
    rules_engine_get_override_status(&ovr_active, &ovr_remaining, &ovr_expires);
    SNAP_ADD(cJSON_AddBoolToObject(data, "override_active", ovr_active));
    if (ovr_active) {
        if (ovr_remaining >= 0)
            SNAP_ADD(cJSON_AddNumberToObject(data, "override_remaining_s", ovr_remaining));
        if (ovr_expires > 0)
            SNAP_ADD(cJSON_AddNumberToObject(data, "expires_ts", (double)ovr_expires));
    }

    return build_str(root, out);

fail:
    ESP_LOGE(TELEM_TAG, "Snapshot not built - out of memory");
    cJSON_Delete(root);
    return false;
}

#undef SNAP_ADD

bool telemetry_v2_post_snapshot(const char *trigger, uint32_t tag, uint8_t flags)
{
    telem_msg_t m;
    if (!build_snapshot(trigger, &m)) return false;
    telem_tx_item_t it = { .json = m.json, .tag = tag, .kind = TELEM_TX_SNAPSHOT, .flags = flags,
                           .seq = 0 };
    return telemetry_v2_tx_post(&it, "snapshot");
}

// ---- Events ---------------------------------------------------------------

// Valve POSITION transitions only (valve_state_changed). Water at the valve's own
// flood probe is NOT reported here — it goes through the unified leak event below.
void telemetry_v2_publish_valve_event(const char *event_name, const char *valve_id)
{
    if (!valve_id) return;

    cJSON *root = build_envelope("event");
    if (!root) return;

    // Created already attached, as in every event below ("data" is the last root key), so
    // a failed attach cannot leave a detached subtree to leak. Key order is unchanged.
    cJSON *data = cJSON_AddObjectToObject(root, "data");
    if (!data) {
        drop_unbuilt(root, "event");
        return;
    }
    cJSON_AddStringToObject(data, "event", event_name);

    // Same identity pair every device-reported event carries, so the cloud can
    // resolve "which device" without special-casing the valve.
    //
    // valve_id is passed in, NOT re-read from ble_valve_get_mac() here: the
    // caller has already validated the MAC against the provisioned one, and a
    // GAP disconnect on the NimBLE host task (which preempts iothub_task) would
    // otherwise make the getter fail between that check and this line, shipping
    // the event with the identity key silently absent.
    cJSON_AddStringToObject(data, "source_type", leak_source_to_str(LEAK_SOURCE_VALVE));
    cJSON_AddStringToObject(data, identity_key_for_source(LEAK_SOURCE_VALVE), valve_id);

    int st = ble_valve_get_state();
    cJSON_AddStringToObject(data, "valve_state",
        st == 1 ? "open" : st == 0 ? "closed" : "unknown");
    // 0xFF = no real reading on this link (no characteristic, failed read, setup not
    // done): null, never 0 — a 0 here read as an empty battery (BUG-1).
    uint8_t batt = ble_valve_get_battery();
    if (batt != 0xFF)
        cJSON_AddNumberToObject(data, "battery", batt);
    else
        cJSON_AddNullToObject(data, "battery");
    cJSON_AddBoolToObject(data, "leak_state", ble_valve_get_leak());
    cJSON_AddBoolToObject(data, "rmleak", ble_valve_get_rmleak_state());

    char valve_fw[32];
    if (ble_valve_get_firmware_rev(valve_fw, sizeof(valve_fw)))
        cJSON_AddStringToObject(data, "fw_version", valve_fw);

    publish_json(root, "event");
}

void telemetry_v2_publish_leak_event(const telem_leak_event_t *ev)
{
    if (!ev || !ev->event || !ev->device_id) return;

    cJSON *root = build_envelope("event");
    if (!root) return;

    cJSON *data = cJSON_AddObjectToObject(root, "data");   // created + attached, or NULL
    if (!data) {
        drop_unbuilt(root, "event");
        return;
    }

    // ---- required core: same keys, order and types for every source; the 3rd
    // key is the identity, named for the device type (valve_id | sensor_id) ----
    cJSON_AddStringToObject(data, "event", ev->event);
    cJSON_AddStringToObject(data, "source_type", leak_source_to_str(ev->source));
    cJSON_AddStringToObject(data, identity_key_for_source(ev->source), ev->device_id);
    cJSON_AddBoolToObject(data, "leak_state", ev->leak_state);
    if (ev->battery != 0xFF)                       // 0xFF = unknown, any source: null, not 0
        cJSON_AddNumberToObject(data, "battery", ev->battery);
    else
        cJSON_AddNullToObject(data, "battery");
    add_location_for_source(data, ev->source, ev->device_id);

    // ---- source-specific extras ----
    if (ev->has_rssi)
        cJSON_AddNumberToObject(data, "rssi", ev->rssi);

    if (ev->has_valve_ext) {
        cJSON_AddStringToObject(data, "valve_state",
            ev->valve_state ? ev->valve_state : "unknown");
        cJSON_AddBoolToObject(data, "rmleak", ev->rmleak);
        if (ev->fw_version && ev->fw_version[0])
            cJSON_AddStringToObject(data, "fw_version", ev->fw_version);
    }

    publish_json(root, "event");
}

void telemetry_v2_publish_rules_event(const char *rules_json)
{
    if (!rules_json) return;
    cJSON *root = build_envelope("event");
    if (!root) return;

    cJSON *parsed = cJSON_Parse(rules_json);
    if (parsed) {
        // Rules engine JSON becomes the "data" payload directly. Not attached = still ours.
        if (!cJSON_AddItemToObject(root, "data", parsed)) {
            cJSON_Delete(parsed);
            drop_unbuilt(root, "event");
            return;
        }
    } else {
        cJSON *data = cJSON_AddObjectToObject(root, "data");
        if (!data) {
            drop_unbuilt(root, "event");
            return;
        }
        cJSON_AddStringToObject(data, "event", "rules_engine");
        cJSON_AddStringToObject(data, "raw", rules_json);
    }

    publish_json(root, "event");
}

void telemetry_v2_publish_health_event(const char *health_json)
{
    if (!health_json) return;
    cJSON *root = build_envelope("event");
    if (!root) return;

    cJSON *parsed = cJSON_Parse(health_json);
    if (parsed) {
        if (!cJSON_AddItemToObject(root, "data", parsed)) {   // not attached = still ours
            cJSON_Delete(parsed);
            drop_unbuilt(root, "event");
            return;
        }
    } else {
        cJSON *data = cJSON_AddObjectToObject(root, "data");
        if (!data) {
            drop_unbuilt(root, "event");
            return;
        }
        cJSON_AddStringToObject(data, "event", "health_engine");
        cJSON_AddStringToObject(data, "raw", health_json);
    }

    publish_json(root, "event");
}

void telemetry_v2_publish_cmd_ack(const char *correlation_id,
                                  const char *cmd_name,
                                  bool success,
                                  const char *error_msg)
{
    cJSON *root = build_envelope("event");
    if (!root) return;

    cJSON *data = cJSON_AddObjectToObject(root, "data");   // created + attached, or NULL
    if (!data) {
        drop_unbuilt(root, "event");
        return;
    }
    cJSON_AddStringToObject(data, "event", "cmd_ack");
    if (correlation_id && correlation_id[0])
        cJSON_AddStringToObject(data, "id", correlation_id);
    cJSON_AddStringToObject(data, "cmd", cmd_name);
    cJSON_AddStringToObject(data, "status", success ? "ok" : "error");
    if (!success && error_msg) {
        cJSON *err = cJSON_AddObjectToObject(data, "error");
        if (err) {
            cJSON_AddStringToObject(err, "code", cmd_name);
            cJSON_AddStringToObject(err, "detail", error_msg);
        }
    }

    publish_json(root, "event");
}

// ---- Offline buffer integration -------------------------------------------

void telemetry_v2_set_connected(bool connected)
{
    // A new session's generation first, then its mark (see s_sess_gen).
    if (connected)
        __atomic_store_n(&s_sess_gen, s_sess_gen + 1, __ATOMIC_RELEASE);
    __atomic_store_n(&s_connected, connected, __ATOMIC_RELEASE);
    ESP_LOGI(TELEM_TAG, "MQTT connected = %s", connected ? "true" : "false");
    // cloud_tx replays the buffer, then sends the lifecycle iothub_task builds for this session,
    // before any live message (cloud_tx_session_work(), app_iothub.c).
    if (connected)
        telemetry_v2_tx_kick();
}

uint32_t telemetry_v2_session_gen(void)
{
    return __atomic_load_n(&s_sess_gen, __ATOMIC_ACQUIRE);
}

// The drain's publish (offline_buffer_drain()): one replayed entry, only into a session that
// is up, and taken only if that same session is still up after the publish: not if it ended,
// nor if it ended and another began meanwhile (its generation, as for A-1 in send_str()). A
// publish whose own write failed has ended the session too (esp-mqtt aborts and dispatches
// DISCONNECTED on this task), and is reported as failed, its own line. The client exists
// whenever the session is up: s_connected is set only by its CONNECTED.
// cloud_tx: under the publish gate, like its every publish (2.1.4 WP2c; send_str()). A gate
// taken, or a stop asked, reads as the session's end: kept for the next connect.
static offline_buffer_pub_t replay_publish(const char *json, size_t len)
{
    if (s_tx_frozen || !s_mqtt || !iothub_pub_begin()) return OFFLINE_BUF_PUB_DOWN;
    uint32_t gen = telemetry_v2_session_gen();
    int64_t t0 = esp_timer_get_time();
    int msg_id = esp_mqtt_client_publish(s_mqtt, s_topic, json, (int)len, 1, 0);
    iothub_pub_end("replay", msg_id, t0);
    if (msg_id < 0) return OFFLINE_BUF_PUB_FAILED;
    return (s_connected && telemetry_v2_session_gen() == gen) ? OFFLINE_BUF_PUB_TAKEN
                                                              : OFFLINE_BUF_PUB_DOWN;
}

void telemetry_v2_drain_offline(void)
{
    // The guard (2.1.4 WP2c): the drain publishes, and holds the buffer's lock while it does.
    if (xTaskGetCurrentTaskHandle() == iothub_task_handle) {
        ESP_LOGE(TELEM_TAG, "%s called on iothub_task - refused", __func__);
        return;
    }
    // Not into a client that is not connected: a stopped one still takes a QoS 1 publish into
    // its outbox (a msg_id, so the slot is erased), and expires it there after 30 s. Nor once
    // a decommission's fallback has erased the buffer (telemetry_v2_tx_freeze()).
    if (!s_connected || s_tx_frozen) return;
    s_replay_owed = false;
    int pending = offline_buffer_count();
    if (pending == 0) return;

    ESP_LOGI(TELEM_TAG, "Draining %d offline event(s) before lifecycle...", pending);
    int published = offline_buffer_drain(replay_publish);
    ESP_LOGI(TELEM_TAG, "Offline drain complete: %d event(s) replayed", published);
    // Cut short with the client still connected (a publish refused: the outbox full): the
    // rest is owed now, not at the next connect. Cut short by the session's end (the drain
    // reads the connection at each entry): the next CONNECTED drains it.
    if (s_connected && offline_buffer_count() > 0)
        s_replay_owed = true;
}

bool telemetry_v2_replay_owed(void)
{
    return s_replay_owed;
}

// ---- The hand-over to cloud_tx (2.1.4 WP2c; see telemetry_v2.h) -----------

void telemetry_v2_tx_set_consumer(TaskHandle_t task)
{
    s_tx_task = task;
}

void telemetry_v2_tx_kick(void)
{
    TaskHandle_t t = s_tx_task;
    if (t != NULL)
        xTaskNotifyGive(t);
}

bool telemetry_v2_tx_post(telem_tx_item_t *it, const char *what)
{
    if (s_txq != NULL && xQueueSend(s_txq, it, 0) == pdTRUE) {
        telemetry_v2_tx_kick();
        return true;
    }
    // Lost: 24 items queued inside one stall of cloud_tx's (WP2c section 2.3). Up to
    // TX_HEALTH_MAX_QUEUED (8) of them can be health alerts, so it takes at least 16 leak, valve
    // or rules events to fill it (SAFE-1). An event's state still reaches the cloud in the next
    // snapshot.
    if (what)
        ESP_LOGE(TELEM_TAG, "TX queue full (%d) - %s not sent", TELEM_TX_FIFO_LEN, what);
    free(it->json);
    it->json = NULL;
    return false;
}

bool telemetry_v2_tx_post_session(telem_tx_item_t *it)
{
    if (s_sessq != NULL && xQueueSend(s_sessq, it, 0) == pdTRUE) {
        telemetry_v2_tx_kick();
        return true;
    }
    free(it->json);
    it->json = NULL;
    return false;
}

bool telemetry_v2_tx_take(telem_tx_item_t *out)
{
    return s_txq != NULL && xQueueReceive(s_txq, out, 0) == pdTRUE;
}

bool telemetry_v2_tx_take_session(telem_tx_item_t *out)
{
    return s_sessq != NULL && xQueueReceive(s_sessq, out, 0) == pdTRUE;
}

unsigned telemetry_v2_tx_queued(void)
{
    return s_txq != NULL ? (unsigned)uxQueueMessagesWaiting(s_txq) : 0;
}

void telemetry_v2_tx_busy_take(void)
{
    xSemaphoreTake(s_busy, portMAX_DELAY);
}

void telemetry_v2_tx_busy_give(void)
{
    xSemaphoreGive(s_busy);
}

bool telemetry_v2_tx_idle_take(void)
{
    if (s_busy == NULL || uxQueueMessagesWaiting(s_txq) != 0 || uxQueueMessagesWaiting(s_sessq) != 0)
        return false;
    return xSemaphoreTake(s_busy, 0) == pdTRUE;
}

void telemetry_v2_tx_idle_give(void)
{
    xSemaphoreGive(s_busy);
}

bool telemetry_v2_tx_health_admit(size_t *free_b, size_t *largest)
{
    *free_b  = 0;
    *largest = 0;
    bool room = telemetry_v2_tx_queued() < TX_HEALTH_MAX_QUEUED;
    if (room && telemetry_v2_tx_idle_take()) {
        telemetry_v2_tx_idle_give();
        return true;
    }
    *free_b  = heap_caps_get_free_size(TX_HEALTH_HEAP_CAPS);
    *largest = heap_caps_get_largest_free_block(TX_HEALTH_HEAP_CAPS);
    return room && *free_b >= TX_HEALTH_FREE_MIN && *largest >= TX_HEALTH_LARGEST_MIN;
}

void telemetry_v2_tx_send_event(const telem_tx_item_t *it, uint32_t gen)
{
    telem_msg_t m = {
        .json    = it->json,
        .len     = strlen(it->json),
        .presync = (it->flags & TELEM_TX_PRESYNC) != 0,
    };
    (void)send_str(&m, "event", &gen);
}

int telemetry_v2_tx_publish(const char *json, const char *type_hint,
                            telem_tx_current_fn current, uint32_t tag)
{
    return publish_logged(json, type_hint, true, current, tag);
}

void telemetry_v2_tx_freeze(void)
{
    s_tx_frozen = true;
}

bool telemetry_v2_tx_frozen(void)
{
    return s_tx_frozen;
}
