#include "telemetry_v2.h"
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "cJSON.h"

#include "app_ble_valve.h"
#include "provisioning_manager.h"
#include "sensor_meta.h"

#define TELEM_TAG "TELEMETRY_V2"

// ---- Module state (all accessed from iothub_task only) --------------------

static esp_mqtt_client_handle_t s_mqtt   = NULL;
static char s_device_id[64]              = {0};
static char s_gateway_id[32]             = {0};
static char s_topic[128]                 = {0};

static const telem_lora_cache_t     *s_lora_cache = NULL;
static const telem_ble_leak_cache_t *s_ble_cache  = NULL;

static TimerHandle_t  s_snapshot_timer = NULL;
static QueueHandle_t  s_snapshot_queue = NULL;

// ---- Helpers --------------------------------------------------------------

static cJSON *build_envelope(const char *type)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "schema", TELEMETRY_SCHEMA);

    time_t now;
    time(&now);
    cJSON_AddNumberToObject(root, "ts", (double)now);

    cJSON *gw = cJSON_CreateObject();
    cJSON_AddStringToObject(gw, "id", s_gateway_id);
    cJSON_AddStringToObject(gw, "fw", TELEMETRY_FW_VERSION);
    cJSON_AddNumberToObject(gw, "uptime_s",
                            (double)(esp_timer_get_time() / 1000000));
    cJSON_AddItemToObject(root, "gateway", gw);

    cJSON_AddStringToObject(root, "type", type);

    return root;
}

static void publish_json(cJSON *root, const char *type_hint)
{
    if (!root || !s_mqtt) {
        if (root) cJSON_Delete(root);
        return;
    }
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json_str) {
        ESP_LOGI(TELEM_TAG, "Pub %s: %s", type_hint, json_str);
        esp_mqtt_client_publish(s_mqtt, s_topic, json_str, 0, 1, 0);
        free(json_str);
    }
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

    ESP_LOGI(TELEM_TAG, "Init: schema=%s interval=%ds",
             TELEMETRY_SCHEMA, SNAPSHOT_INTERVAL_MS / 1000);
}

QueueHandle_t telemetry_v2_get_snapshot_queue(void)
{
    return s_snapshot_queue;
}

void telemetry_v2_start_snapshot_timer(void)
{
    if (s_snapshot_timer) {
        xTimerReset(s_snapshot_timer, 0);   // (re)start from now
        ESP_LOGI(TELEM_TAG, "Snapshot timer started (%ds)",
                 SNAPSHOT_INTERVAL_MS / 1000);
    }
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
        cJSON_AddStringToObject(data, "valve_mac", valve_mac);

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

void telemetry_v2_publish_snapshot(void)
{
    cJSON *root = build_envelope("snapshot");
    if (!root) return;

    cJSON *data = cJSON_CreateObject();

    // ---- valve ----
    cJSON *valve = cJSON_CreateObject();
    char vmac[18];
    bool vconn = ble_valve_get_mac(vmac);
    if (vconn) {
        cJSON_AddStringToObject(valve, "mac", vmac);
        int st = ble_valve_get_state();
        cJSON_AddStringToObject(valve, "state",
            st == 1 ? "open" : st == 0 ? "closed" : "unknown");
        cJSON_AddNumberToObject(valve, "battery", ble_valve_get_battery());
        cJSON_AddBoolToObject(valve, "leak_state", ble_valve_get_leak());
        cJSON_AddBoolToObject(valve, "rmleak", ble_valve_get_rmleak_state());
        cJSON_AddBoolToObject(valve, "connected", true);
    } else {
        cJSON_AddStringToObject(valve, "state", "disconnected");
        cJSON_AddBoolToObject(valve, "connected", false);
    }
    cJSON_AddItemToObject(data, "valve", valve);

    // ---- LoRa sensors ----
    cJSON *lora_arr = cJSON_CreateArray();
    if (s_lora_cache) {
        for (int i = 0; i < TELEM_MAX_LORA_CACHE; i++) {
            if (!s_lora_cache[i].valid) continue;
            cJSON *s = cJSON_CreateObject();
            char id[16];
            snprintf(id, sizeof(id), "0x%08lX",
                     (unsigned long)s_lora_cache[i].sensor_id);
            cJSON_AddStringToObject(s, "sensor_id", id);
            cJSON_AddNumberToObject(s, "battery", s_lora_cache[i].battery);
            cJSON_AddBoolToObject(s, "leak_state",
                                  s_lora_cache[i].leak_status == 1);
            cJSON_AddNumberToObject(s, "rssi", s_lora_cache[i].rssi);
            cJSON_AddNumberToObject(s, "snr",  s_lora_cache[i].snr);
            add_location_obj(s, SENSOR_TYPE_LORA, id);
            cJSON_AddItemToArray(lora_arr, s);
        }
    }
    cJSON_AddItemToObject(data, "lora_sensors", lora_arr);

    // ---- BLE leak sensors ----
    cJSON *ble_arr = cJSON_CreateArray();
    if (s_ble_cache) {
        for (int i = 0; i < TELEM_MAX_BLE_LEAK_CACHE; i++) {
            if (!s_ble_cache[i].valid) continue;
            cJSON *s = cJSON_CreateObject();
            cJSON_AddStringToObject(s, "sensor_id", s_ble_cache[i].mac_str);
            cJSON_AddNumberToObject(s, "battery", s_ble_cache[i].battery);
            cJSON_AddBoolToObject(s, "leak_state", s_ble_cache[i].leak_state);
            cJSON_AddNumberToObject(s, "rssi", s_ble_cache[i].rssi);
            add_location_obj(s, SENSOR_TYPE_BLE_LEAK, s_ble_cache[i].mac_str);
            cJSON_AddItemToArray(ble_arr, s);
        }
    }
    cJSON_AddItemToObject(data, "ble_leak_sensors", ble_arr);

    cJSON_AddItemToObject(root, "data", data);
    publish_json(root, "snapshot");
}

// ---- Events ---------------------------------------------------------------

void telemetry_v2_publish_valve_event(const char *event_name)
{
    cJSON *root = build_envelope("event");
    if (!root) return;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "event", event_name);

    int st = ble_valve_get_state();
    cJSON_AddStringToObject(data, "valve_state",
        st == 1 ? "open" : st == 0 ? "closed" : "unknown");
    cJSON_AddNumberToObject(data, "battery", ble_valve_get_battery());
    cJSON_AddBoolToObject(data, "leak_state", ble_valve_get_leak());
    cJSON_AddBoolToObject(data, "rmleak", ble_valve_get_rmleak_state());

    cJSON_AddItemToObject(root, "data", data);
    publish_json(root, "event");
}

void telemetry_v2_publish_leak_event(const char *event_name,
                                     const char *source_type,
                                     const char *sensor_id,
                                     bool leak_state,
                                     uint8_t battery, int8_t rssi)
{
    cJSON *root = build_envelope("event");
    if (!root) return;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "event", event_name);
    cJSON_AddStringToObject(data, "source_type", source_type);
    cJSON_AddStringToObject(data, "sensor_id", sensor_id);
    cJSON_AddBoolToObject(data, "leak_state", leak_state);
    cJSON_AddNumberToObject(data, "battery", battery);
    cJSON_AddNumberToObject(data, "rssi", rssi);

    sensor_type_t mt = (strcmp(source_type, "lora") == 0)
                        ? SENSOR_TYPE_LORA : SENSOR_TYPE_BLE_LEAK;
    add_location_obj(data, mt, sensor_id);

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
