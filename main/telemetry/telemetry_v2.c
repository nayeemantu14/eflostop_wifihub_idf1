#include "telemetry_v2.h"
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
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

#define TELEM_TAG "TELEMETRY_V2"

// Settings live in the dedicated commissioning partition, not the default "nvs".
// The physical button wipes only WiFi credentials from the default partition, and
// app_main()'s corruption recovery can erase all of it — a user who chose a 15-min
// cadence should not silently get 5-min back after re-entering their WiFi password.
// Cleared deliberately by decommission, which is the factory-reset path.
#define NVS_NS_TELEM       "telemetry"
#define NVS_KEY_SNAP_INT   "snap_int"

// ---- Module state (all accessed from iothub_task only) --------------------

static esp_mqtt_client_handle_t s_mqtt   = NULL;
static char s_device_id[64]              = {0};
static char s_gateway_id[32]             = {0};
static char s_topic[128]                 = {0};

static const telem_lora_cache_t     *s_lora_cache = NULL;
static const telem_ble_leak_cache_t *s_ble_cache  = NULL;

static bool           s_connected      = false;
static TimerHandle_t  s_snapshot_timer = NULL;
static QueueHandle_t  s_snapshot_queue = NULL;

// Heartbeat interval in SECONDS. Written by the Twin desired-property handler
// (telemetry_v2_set_snapshot_interval, esp-mqtt task) as a single 32-bit store —
// naturally atomic on the 32-bit Xtensa core — and read by the iothub_task
// snapshot scheduler. The actual heartbeat cadence is driven by that scheduler's
// monotonic deadline, not by s_snapshot_timer (which stays a fixed liveness backstop).
static volatile int32_t s_snap_interval_s = SNAPSHOT_INTERVAL_MS / 1000;

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

static cJSON *build_envelope(const char *type)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "schema", TELEMETRY_SCHEMA);

    time_t now;
    time(&now);

    /* Suppress telemetry if SNTP has not synced yet */
    if (now < EPOCH_VALID_THRESHOLD_TELEM) {
        ESP_LOGW(TELEM_TAG, "Time not synced (ts=%ld) — suppressing %s", (long)now, type);
        cJSON_Delete(root);
        return NULL;
    }

    cJSON_AddNumberToObject(root, "ts", (double)now);

    cJSON *gw = cJSON_CreateObject();
    cJSON_AddStringToObject(gw, "id", s_gateway_id);
    cJSON_AddStringToObject(gw, "short_id", hub_identity_get_short_id());
    const char *hub_name = hub_identity_get_name();
    if (hub_name[0])
        cJSON_AddStringToObject(gw, "name", hub_name);
    cJSON_AddStringToObject(gw, "fw", telemetry_v2_fw_version());
    cJSON_AddNumberToObject(gw, "uptime_s",
                            (double)(esp_timer_get_time() / 1000000));
    cJSON_AddItemToObject(root, "gateway", gw);

    cJSON_AddStringToObject(root, "type", type);

    return root;
}

// Returns true ONLY when the message actually reached esp-mqtt (online branch
// taken AND esp_mqtt_client_publish accepted it, msg_id >= 0). Returns false on
// a NULL root, offline (buffered or dropped), or a negative msg_id (e.g. outbox
// saturated). The snapshot scheduler re-arms the heartbeat only on a true return,
// so an offline/pre-SNTP/outbox-full drop never counts as "sent".
static bool publish_json(cJSON *root, const char *type_hint)
{
    if (!root) return false;

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return false;

    bool sent = false;
    if (s_mqtt && s_connected) {
        // Online: publish directly
        ESP_LOGI(TELEM_TAG, "Pub %s: %s", type_hint, json_str);
        int msg_id = esp_mqtt_client_publish(s_mqtt, s_topic, json_str, 0, 1, 0);
        sent = (msg_id >= 0);
        if (!sent)
            ESP_LOGW(TELEM_TAG, "Pub %s failed (msg_id=%d)", type_hint, msg_id);
    } else if (strcmp(type_hint, "event") == 0) {
        // Offline: buffer critical events for replay on reconnect
        ESP_LOGW(TELEM_TAG, "Offline — buffering %s event", type_hint);
        offline_buffer_store(json_str, strlen(json_str));
    } else {
        // Offline: drop lifecycle/snapshot (regenerated on reconnect)
        ESP_LOGD(TELEM_TAG, "Offline — dropping %s (regenerated)", type_hint);
    }

    free(json_str);
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

static void add_location_obj(cJSON *parent, sensor_type_t type,
                             const char *sensor_id)
{
    const sensor_meta_entry_t *meta = sensor_meta_find(type, sensor_id);
    cJSON *loc = cJSON_CreateObject();
    cJSON_AddStringToObject(loc, "code",
        sensor_meta_location_code_to_str(
            meta ? meta->location_code : LOC_UNKNOWN));
    cJSON_AddStringToObject(loc, "label", meta ? meta->label : "");
    cJSON_AddItemToObject(parent, "location", loc);
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
            cJSON *loc = cJSON_CreateObject();
            cJSON_AddStringToObject(loc, "code",
                sensor_meta_location_code_to_str(LOC_UNKNOWN));
            cJSON_AddStringToObject(loc, "label", "");
            cJSON_AddItemToObject(parent, "location", loc);
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
// JSON escaping). Copying also removes the lifetime question of dereferencing a
// sensor_meta table pointer after the lookup returned.
static void leak_label_for(const health_device_status_t *d, char *out, size_t out_len)
{
    const char *src;
    if (d->dev_type == HEALTH_DEV_VALVE) {
        src = "valve";
    } else {
        sensor_type_t type = (d->dev_type == HEALTH_DEV_LORA) ? SENSOR_TYPE_LORA
                                                              : SENSOR_TYPE_BLE_LEAK;
        const sensor_meta_entry_t *meta = sensor_meta_find(type, d->dev_id);
        src = (meta && meta->label[0]) ? meta->label : d->dev_id;
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
    // rollup_syncing is passed in rather than queried here so it is sampled in a defined
    // order relative to the rating — see the call site. It is the health engine's OWN
    // exclusion predicate (health_is_rollup_syncing), not the shorter snapshot gate, so
    // this string can never claim a device is still syncing while the rating has already
    // started counting it — or vice versa, which is what produced a published
    // "1 sensor offline" for a healthy sensor that beaconed at 265.6 s.
    // A leaking device is never counted as "unheard": we plainly have heard from it, and
    // recalc_system_rating() exempts it from the roll-up exclusion for that reason. Without
    // the same exemption here the device would be tallied as awaiting contact AND bucketed
    // as a cause below — described twice, contradictorily.
    int unheard = 0;
    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (health[i].in_use && !health[i].ever_seen && !health[i].leaking) unheard++;
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
    bool valve_offline = false;
    bool valve_grace   = false;
    bool valve_batt    = false;
    const health_device_status_t *sole_leaker = NULL;

    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (!health[i].in_use || health[i].rating != sys_rating)
            continue;

        const health_device_status_t *d = &health[i];

        // While syncing, a device we have never heard from is already accounted for
        // by the trailing "syncing N devices" part. Bucketing it as "offline" too
        // would have the same device described twice, in contradictory terms.
        //
        // `!d->leaking` mirrors recalc_system_rating() EXACTLY, and must. That function
        // exempts leaking devices from the roll-up exclusion, so a wet device can set the
        // rating to critical while ever_seen is still false — the valve especially, since
        // handle_valve_leak() deliberately does not touch ever_seen. Without the same
        // exemption the cause would be skipped here and the snapshot would publish
        // rating:"critical" alongside reason:"syncing N devices", naming no leak at all.
        if (syncing && !d->ever_seen && !d->leaking) continue;

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
            // it is wet and would contradict itself; and for a CONNECTED valve the
            // branch below would classify it as "Valve battery low", which is simply
            // the wrong cause.
            if (!(d->dev_type == HEALTH_DEV_VALVE && !d->connected)) continue;
        }

        if (d->dev_type == HEALTH_DEV_VALVE) {
            if (!d->connected) {
                if (sys_rating == HEALTH_CRITICAL) valve_offline = true;
                else                               valve_grace   = true;
            } else {
                // Connected valve at degraded rating → battery issue
                valve_batt = true;
            }
        } else {
            // Sensor: determine root cause of this rating
            if (!d->connected) {
                offline_count++;
            } else if (d->last_battery != 0xFF &&
                       d->last_battery <= HEALTH_BATTERY_GOOD_PCT) {
                batt_count++;
            } else {
                signal_count++;
            }
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
    return s_connected;
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

void telemetry_v2_publish_lifecycle(void)
{
    cJSON *root = build_envelope("lifecycle");
    if (!root) return;

    cJSON *data = cJSON_CreateObject();
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
        cJSON *r = cJSON_CreateObject();
        cJSON_AddBoolToObject(r, "auto_close_enabled", rules.auto_close_enabled);
        cJSON_AddNumberToObject(r, "trigger_mask", rules.trigger_mask);
        cJSON_AddItemToObject(data, "rules", r);
    }

    cJSON_AddItemToObject(root, "data", data);
    publish_json(root, "lifecycle");
}

// ---- Snapshot -------------------------------------------------------------

bool telemetry_v2_publish_snapshot(const char *trigger)
{
    cJSON *root = build_envelope("snapshot");
    if (!root) return false;   // pre-SNTP / alloc fail — treated as "not published"

    cJSON *data = cJSON_CreateObject();

    // Trigger reason (heartbeat | event | commission | boot) — lets the app
    // attribute each snapshot in its event-log-vs-UI-refresh model. (Named
    // `trigger` to avoid colliding with the system_health `char reason[128]` below.)
    if (trigger && trigger[0])
        cJSON_AddStringToObject(data, "reason", trigger);

    // ---- Fetch health device status for all provisioned devices ----
    health_device_status_t health[HEALTH_MAX_DEVICES];
    uint8_t health_count = 0;
    bool have_health = health_get_device_status_all(health, &health_count);

    // ---- system_health (worst rating + human-readable reason) ----
    // Order matters: sample the sync state BEFORE the rating. health_is_rollup_syncing()
    // evaluates the window deadlines on read, and the grace expiring re-rolls the roll-up
    // (devices never heard from stop being excluded). Reading the rating first would let it
    // close in between and publish rating:excellent next to reason:"All devices healthy"
    // for a hub that had, microseconds earlier, become critical over a missing sensor.
    // Sampling this first means the rating is always the fresher of the two.
    bool rollup_syncing = health_is_rollup_syncing();
    health_rating_t sys_rating = health_get_system_rating();
    cJSON *sys_health = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_health, "rating",
        health_rating_to_str(sys_rating));
    // 192, not 128: the builder can now emit up to 8 comma-joined parts (leak, the
    // interlock, three valve causes, three sensor causes, syncing), and a leak part
    // carries a user-supplied label. At 128 the last cause truncated mid-word once six
    // parts were present. strncat is bounded either way, so this only buys fidelity.
    char reason[192];
    if (have_health) {
        build_system_health_reason(health, sys_rating, rollup_syncing, reason, sizeof(reason));
    } else {
        snprintf(reason, sizeof(reason), "Health data unavailable");
    }
    cJSON_AddStringToObject(sys_health, "reason", reason);
    cJSON_AddItemToObject(data, "system_health", sys_health);

    // ---- valve ----
    cJSON *valve = cJSON_CreateObject();
    char vmac[18];
    bool vconn = ble_valve_get_mac(vmac);

    // Find valve health entry for MAC / rating / last_seen
    const health_device_status_t *valve_hs = NULL;
    if (have_health) {
        for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
            if (health[i].in_use && health[i].dev_type == HEALTH_DEV_VALVE) {
                valve_hs = &health[i];
                break;
            }
        }
    }

    // Identity: prefer live BLE, fall back to health (provisioned) entry.
    // Key is valve_id — the identity key names the device type (valve_id /
    // sensor_id) on every outbound message, whether the device is reporting
    // itself or the hub is reporting about it. Literal rather than
    // leak_identity_key() only because the enclosing object is statically the
    // valve; the dynamic sites all delegate.
    if (vconn) {
        cJSON_AddStringToObject(valve, "valve_id", vmac);
    } else if (valve_hs) {
        cJSON_AddStringToObject(valve, "valve_id", valve_hs->dev_id);
    }

    if (vconn) {
        int st = ble_valve_get_state();
        cJSON_AddStringToObject(valve, "state",
            st == 1 ? "open" : st == 0 ? "closed" : "unknown");
        cJSON_AddNumberToObject(valve, "battery", ble_valve_get_battery());
        cJSON_AddBoolToObject(valve, "leak_state", ble_valve_get_leak());
        cJSON_AddBoolToObject(valve, "rmleak", ble_valve_get_rmleak_state());
        cJSON_AddBoolToObject(valve, "connected", true);

        char valve_fw[32];
        if (ble_valve_get_firmware_rev(valve_fw, sizeof(valve_fw)))
            cJSON_AddStringToObject(valve, "fw_version", valve_fw);
        else
            cJSON_AddNullToObject(valve, "fw_version");
    } else {
        cJSON_AddStringToObject(valve, "state", "disconnected");
        cJSON_AddBoolToObject(valve, "connected", false);
    }

    // Health metadata
    if (valve_hs) {
        cJSON_AddStringToObject(valve, "rating",
            health_rating_to_str(valve_hs->rating));
        if (valve_hs->last_seen_age_s != UINT32_MAX) {
            cJSON_AddNumberToObject(valve, "last_seen_age_s",
                valve_hs->last_seen_age_s);
        } else {
            cJSON_AddNullToObject(valve, "last_seen_age_s");
        }
    }

    cJSON_AddItemToObject(data, "valve", valve);

    // ---- LoRa sensors (iterate health entries, merge cache data) ----
    cJSON *lora_arr = cJSON_CreateArray();
    if (have_health) {
        for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
            if (!health[i].in_use || health[i].dev_type != HEALTH_DEV_LORA)
                continue;

            cJSON *s = cJSON_CreateObject();
            cJSON_AddStringToObject(s, "sensor_id", health[i].dev_id);
            cJSON_AddBoolToObject(s, "connected", health[i].connected);
            cJSON_AddStringToObject(s, "rating",
                health_rating_to_str(health[i].rating));

            if (health[i].last_seen_age_s != UINT32_MAX) {
                cJSON_AddNumberToObject(s, "last_seen_age_s",
                    health[i].last_seen_age_s);
            } else {
                cJSON_AddNullToObject(s, "last_seen_age_s");
            }

            // Merge telemetry data from cache — only when the device is currently
            // connected. A reload (provision/decommission) wipes health seen-state
            // but not this cache, so without the connected gate a just-reloaded
            // sensor would emit connected:false yet carry stale battery/rssi/fw.
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
                cJSON_AddNumberToObject(s, "battery", health[i].last_battery);
            else
                cJSON_AddNullToObject(s, "battery");

            if (health[i].last_rssi != 0)
                cJSON_AddNumberToObject(s, "rssi", health[i].last_rssi);
            else
                cJSON_AddNullToObject(s, "rssi");

            /* health[i].leaking, never a literal false and never the cache's copy. An
             * offline-but-WET sensor used to publish leak_state:false in the same snapshot
             * whose system_health.reason said "Leak detected: <its label>" — a document
             * that contradicted itself, with the safer of the two values being the one a
             * consumer reading the array would take. Field stays boolean; schema unchanged. */
            cJSON_AddBoolToObject(s, "leak_state", health[i].leaking);

            if (cached)
                cJSON_AddNumberToObject(s, "snr", cached->snr);
            else
                cJSON_AddNullToObject(s, "snr");

            add_location_obj(s, SENSOR_TYPE_LORA, health[i].dev_id);
            cJSON_AddItemToArray(lora_arr, s);
        }
    }
    cJSON_AddItemToObject(data, "lora_sensors", lora_arr);

    // ---- BLE leak sensors (iterate health entries, merge cache data) ----
    cJSON *ble_arr = cJSON_CreateArray();
    if (have_health) {
        for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
            if (!health[i].in_use || health[i].dev_type != HEALTH_DEV_BLE_LEAK)
                continue;

            cJSON *s = cJSON_CreateObject();
            cJSON_AddStringToObject(s, "sensor_id", health[i].dev_id);
            cJSON_AddBoolToObject(s, "connected", health[i].connected);
            cJSON_AddStringToObject(s, "rating",
                health_rating_to_str(health[i].rating));

            if (health[i].last_seen_age_s != UINT32_MAX) {
                cJSON_AddNumberToObject(s, "last_seen_age_s",
                    health[i].last_seen_age_s);
            } else {
                cJSON_AddNullToObject(s, "last_seen_age_s");
            }

            // Merge telemetry data from cache — only when the device is currently
            // connected (see LoRa note): prevents emitting connected:false with
            // stale battery/rssi/fw after a reload wipes health seen-state.
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
             * The reload-staleness concern the `connected` gate was protecting against is
             * handled for free here: health_engine_reload_devices() resets last_battery to
             * 0xFF and last_rssi to 0, so a just-reloaded device emits null rather than
             * stale values — and no cache fallback can resurrect them.
             *
             * fw_version still comes from the cache: the health table does not carry it and
             * it cannot change at runtime, so staleness is not a concern for that one. */
            if (health[i].last_battery != 0xFF)
                cJSON_AddNumberToObject(s, "battery", health[i].last_battery);
            else
                cJSON_AddNullToObject(s, "battery");

            if (health[i].last_rssi != 0)
                cJSON_AddNumberToObject(s, "rssi", health[i].last_rssi);
            else
                cJSON_AddNullToObject(s, "rssi");

            /* Always the health engine's wet/dry, connected or not — it is both the
             * freshest source and the one that cannot contradict system_health.reason for
             * an offline-but-wet sensor. */
            cJSON_AddBoolToObject(s, "leak_state", health[i].leaking);

            if (cached && cached->fw_version[0])
                cJSON_AddStringToObject(s, "fw_version", cached->fw_version);
            else
                cJSON_AddNullToObject(s, "fw_version");

            add_location_obj(s, SENSOR_TYPE_BLE_LEAK, health[i].dev_id);
            cJSON_AddItemToArray(ble_arr, s);
        }
    }
    cJSON_AddItemToObject(data, "ble_leak_sensors", ble_arr);

    // ---- Rules config (master auto-close enable + trigger mask) ----
    // Mirrors the lifecycle "rules" object EXACTLY (same shape + guard) so the
    // app reuses one parser. Kept as a GLOBAL object, disjoint from any future
    // per-sensor auto_close flag, so per-sensor shutoff stays purely additive.
    rules_config_t rules;
    if (provisioning_get_rules_config(&rules)) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddBoolToObject(r, "auto_close_enabled", rules.auto_close_enabled);
        cJSON_AddNumberToObject(r, "trigger_mask", rules.trigger_mask);
        cJSON_AddItemToObject(data, "rules", r);
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
    cJSON_AddBoolToObject(data, "override_active", ovr_active);
    if (ovr_active) {
        if (ovr_remaining >= 0)
            cJSON_AddNumberToObject(data, "override_remaining_s", ovr_remaining);
        if (ovr_expires > 0)
            cJSON_AddNumberToObject(data, "expires_ts", (double)ovr_expires);
    }

    cJSON_AddItemToObject(root, "data", data);
    return publish_json(root, "snapshot");
}

// ---- Events ---------------------------------------------------------------

// Valve POSITION transitions only (valve_state_changed). Water at the valve's own
// flood probe is NOT reported here — it goes through the unified leak event below.
void telemetry_v2_publish_valve_event(const char *event_name, const char *valve_id)
{
    if (!valve_id) return;

    cJSON *root = build_envelope("event");
    if (!root) return;

    cJSON *data = cJSON_CreateObject();
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
    cJSON_AddNumberToObject(data, "battery", ble_valve_get_battery());
    cJSON_AddBoolToObject(data, "leak_state", ble_valve_get_leak());
    cJSON_AddBoolToObject(data, "rmleak", ble_valve_get_rmleak_state());

    char valve_fw[32];
    if (ble_valve_get_firmware_rev(valve_fw, sizeof(valve_fw)))
        cJSON_AddStringToObject(data, "fw_version", valve_fw);

    cJSON_AddItemToObject(root, "data", data);
    publish_json(root, "event");
}

void telemetry_v2_publish_leak_event(const telem_leak_event_t *ev)
{
    if (!ev || !ev->event || !ev->device_id) return;

    cJSON *root = build_envelope("event");
    if (!root) return;

    cJSON *data = cJSON_CreateObject();

    // ---- required core: same keys, order and types for every source; the 3rd
    // key is the identity, named for the device type (valve_id | sensor_id) ----
    cJSON_AddStringToObject(data, "event", ev->event);
    cJSON_AddStringToObject(data, "source_type", leak_source_to_str(ev->source));
    cJSON_AddStringToObject(data, identity_key_for_source(ev->source), ev->device_id);
    cJSON_AddBoolToObject(data, "leak_state", ev->leak_state);
    cJSON_AddNumberToObject(data, "battery", ev->battery);
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

    cJSON_AddItemToObject(root, "data", data);
    publish_json(root, "event");
}

void telemetry_v2_publish_rules_event(const char *rules_json)
{
    if (!rules_json) return;
    cJSON *root = build_envelope("event");
    if (!root) return;

    cJSON *parsed = cJSON_Parse(rules_json);
    if (parsed) {
        // Rules engine JSON becomes the "data" payload directly
        cJSON_AddItemToObject(root, "data", parsed);
    } else {
        cJSON *data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "event", "rules_engine");
        cJSON_AddStringToObject(data, "raw", rules_json);
        cJSON_AddItemToObject(root, "data", data);
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
        cJSON_AddItemToObject(root, "data", parsed);
    } else {
        cJSON *data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "event", "health_engine");
        cJSON_AddStringToObject(data, "raw", health_json);
        cJSON_AddItemToObject(root, "data", data);
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

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "event", "cmd_ack");
    if (correlation_id && correlation_id[0])
        cJSON_AddStringToObject(data, "id", correlation_id);
    cJSON_AddStringToObject(data, "cmd", cmd_name);
    cJSON_AddStringToObject(data, "status", success ? "ok" : "error");
    if (!success && error_msg) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "code", cmd_name);
        cJSON_AddStringToObject(err, "detail", error_msg);
        cJSON_AddItemToObject(data, "error", err);
    }

    cJSON_AddItemToObject(root, "data", data);
    publish_json(root, "event");
}

// ---- Offline buffer integration -------------------------------------------

void telemetry_v2_set_connected(bool connected)
{
    s_connected = connected;
    ESP_LOGI(TELEM_TAG, "MQTT connected = %s", connected ? "true" : "false");
}

void telemetry_v2_drain_offline(void)
{
    int pending = offline_buffer_count();
    if (pending == 0) return;

    ESP_LOGI(TELEM_TAG, "Draining %d offline event(s) before lifecycle...", pending);
    int published = offline_buffer_drain(s_mqtt, s_topic);
    ESP_LOGI(TELEM_TAG, "Offline drain complete: %d event(s) replayed", published);
}
