#include "app_iothub.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "esp_mac.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"

#include "app_lora/app_lora.h"
#include "ble_valve/app_ble_valve.h"
#include "ble_leak_scanner/app_ble_leak.h"
#include "provisioning_manager/provisioning_manager.h"
#include "rules_engine/rules_engine.h"
#include "health_engine/health_engine.h"
#include "sensor_meta/sensor_meta.h"
#include "telemetry/telemetry_v2.h"
#include "commands/c2d_commands.h"

// External Queue from LoRa app
extern QueueHandle_t lora_rx_queue;

TaskHandle_t iothub_task_handle = NULL;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool g_iot_hub_connected = false;
static char g_gateway_id[32] = {0};

// Lifecycle flag: set in MQTT_EVENT_CONNECTED, consumed in event loop
static bool g_needs_lifecycle = false;

// Boot snapshot: sent once after health engine boot sync completes
static bool g_boot_snapshot_sent = false;

// ---------------------------------------------------------------------------
// Telemetry v2 caches (shared with telemetry module for snapshot reads)
// ---------------------------------------------------------------------------

static telem_lora_cache_t     g_telem_lora_cache[TELEM_MAX_LORA_CACHE] = {0};
static telem_ble_leak_cache_t g_telem_ble_cache[TELEM_MAX_BLE_LEAK_CACHE] = {0};

// ---------------------------------------------------------------------------
// Legacy cache types (deprecated — kept for build_*_delta_json() below)
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t battery;
    bool leak_state;
    int valve_state;
    bool rmleak;
    bool valid;
} valve_cache_t;

static valve_cache_t g_last_valve = {0};

// ---------------------------------------------------------------------------
// SAS token helpers (unchanged)
// ---------------------------------------------------------------------------

void url_encode(const char *src, char *dst, size_t dst_len)
{
    char *end = dst + dst_len - 1;
    while (*src && dst < end)
    {
        if ((*src >= 'A' && *src <= 'Z') || (*src >= 'a' && *src <= 'z') ||
            (*src >= '0' && *src <= '9') || *src == '-' || *src == '.' || *src == '_' || *src == '~')
        {
            *dst++ = *src;
        }
        else
        {
            if (dst + 3 > end)
                break;
            dst += sprintf(dst, "%%%02X", (unsigned char)*src);
        }
        src++;
    }
    *dst = '\0';
}

char *generate_sas_token(const char *resource_uri, const char *key, long expiry_seconds)
{
    char expiry_str[20];
    time_t now;
    time(&now);
    long expiry = now + expiry_seconds;
    snprintf(expiry_str, sizeof(expiry_str), "%ld", expiry);

    char encoded_uri[128];
    url_encode(resource_uri, encoded_uri, sizeof(encoded_uri));
    char string_to_sign[256];
    snprintf(string_to_sign, sizeof(string_to_sign), "%s\n%s", encoded_uri, expiry_str);
    unsigned char decoded_key[64];
    size_t decoded_key_len = 0;
    mbedtls_base64_decode(decoded_key, sizeof(decoded_key), &decoded_key_len, (const unsigned char *)key, strlen(key));
    unsigned char hmac[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, decoded_key, decoded_key_len);
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)string_to_sign, strlen(string_to_sign));
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);
    unsigned char signature_b64[64];
    size_t signature_b64_len = 0;
    mbedtls_base64_encode(signature_b64, sizeof(signature_b64), &signature_b64_len, hmac, 32);
    signature_b64[signature_b64_len] = '\0';
    char encoded_signature[128];
    url_encode((char *)signature_b64, encoded_signature, sizeof(encoded_signature));
    char *sas_token = (char *)malloc(512);
    snprintf(sas_token, 512, "SharedAccessSignature sr=%s&sig=%s&se=%s", encoded_uri, encoded_signature, expiry_str);
    return sas_token;
}

// ---------------------------------------------------------------------------
// v2 change detection helpers
// ---------------------------------------------------------------------------

/**
 * Update the LoRa telem cache and return true if leak_status changed.
 * Always updates all cached fields (battery, rssi, snr) for snapshot use.
 */
static bool update_lora_cache_check_leak(const lora_packet_t *pkt)
{
    // Search for existing entry
    for (int i = 0; i < TELEM_MAX_LORA_CACHE; i++) {
        if (g_telem_lora_cache[i].valid &&
            g_telem_lora_cache[i].sensor_id == pkt->sensorId) {
            bool leak_changed =
                (g_telem_lora_cache[i].leak_status != pkt->leakStatus);
            g_telem_lora_cache[i].battery     = pkt->batteryPercentage;
            g_telem_lora_cache[i].leak_status = pkt->leakStatus;
            g_telem_lora_cache[i].rssi        = pkt->rssi;
            g_telem_lora_cache[i].snr         = pkt->snr;
            return leak_changed;
        }
    }

    // Not in cache — find empty slot
    for (int i = 0; i < TELEM_MAX_LORA_CACHE; i++) {
        if (!g_telem_lora_cache[i].valid) {
            g_telem_lora_cache[i].sensor_id   = pkt->sensorId;
            g_telem_lora_cache[i].battery     = pkt->batteryPercentage;
            g_telem_lora_cache[i].leak_status = pkt->leakStatus;
            g_telem_lora_cache[i].rssi        = pkt->rssi;
            g_telem_lora_cache[i].snr         = pkt->snr;
            g_telem_lora_cache[i].valid       = true;
            return (pkt->leakStatus != 0);  // First time: only emit if actively leaking
        }
    }

    // Cache full — overwrite slot 0
    g_telem_lora_cache[0].sensor_id   = pkt->sensorId;
    g_telem_lora_cache[0].battery     = pkt->batteryPercentage;
    g_telem_lora_cache[0].leak_status = pkt->leakStatus;
    g_telem_lora_cache[0].rssi        = pkt->rssi;
    g_telem_lora_cache[0].snr         = pkt->snr;
    g_telem_lora_cache[0].valid       = true;
    return (pkt->leakStatus != 0);
}

/**
 * Update the BLE leak telem cache and return true if leak_state changed.
 * Always updates all cached fields for snapshot use.
 */
static bool update_ble_leak_cache_check_leak(const ble_leak_event_t *evt)
{
    for (int i = 0; i < TELEM_MAX_BLE_LEAK_CACHE; i++) {
        if (g_telem_ble_cache[i].valid &&
            strcasecmp(g_telem_ble_cache[i].mac_str, evt->sensor_mac_str) == 0) {
            bool leak_changed =
                (g_telem_ble_cache[i].leak_state != evt->leak_detected);
            g_telem_ble_cache[i].battery    = evt->battery;
            g_telem_ble_cache[i].leak_state = evt->leak_detected;
            g_telem_ble_cache[i].rssi       = evt->rssi;
            return leak_changed;
        }
    }

    // Not in cache — find empty slot
    for (int i = 0; i < TELEM_MAX_BLE_LEAK_CACHE; i++) {
        if (!g_telem_ble_cache[i].valid) {
            strncpy(g_telem_ble_cache[i].mac_str, evt->sensor_mac_str, 17);
            g_telem_ble_cache[i].mac_str[17] = '\0';
            g_telem_ble_cache[i].battery    = evt->battery;
            g_telem_ble_cache[i].leak_state = evt->leak_detected;
            g_telem_ble_cache[i].rssi       = evt->rssi;
            g_telem_ble_cache[i].valid      = true;
            return evt->leak_detected;  // First time: only emit if actively leaking
        }
    }

    // Cache full — overwrite slot 0
    strncpy(g_telem_ble_cache[0].mac_str, evt->sensor_mac_str, 17);
    g_telem_ble_cache[0].mac_str[17] = '\0';
    g_telem_ble_cache[0].battery    = evt->battery;
    g_telem_ble_cache[0].leak_state = evt->leak_detected;
    g_telem_ble_cache[0].rssi       = evt->rssi;
    g_telem_ble_cache[0].valid      = true;
    return evt->leak_detected;
}

// ---------------------------------------------------------------------------
// Legacy helpers (deprecated — kept until v1 telemetry fully removed)
// ---------------------------------------------------------------------------

// (DEPRECATED) Check if LoRa packet is a duplicate
__attribute__((unused))
static bool is_lora_duplicate(const lora_packet_t *pkt)
{
    (void)pkt;
    return true; // Always suppress — v2 uses update_lora_cache_check_leak()
}

// (DEPRECATED) Check if valve data changed
__attribute__((unused))
static bool valve_data_changed(void)
{
    uint8_t batt = ble_valve_get_battery();
    bool leak = ble_valve_get_leak();
    int state = ble_valve_get_state();
    bool rmleak = ble_valve_get_rmleak_state();

    if (!g_last_valve.valid) {
        g_last_valve.battery = batt;
        g_last_valve.leak_state = leak;
        g_last_valve.valve_state = state;
        g_last_valve.rmleak = rmleak;
        g_last_valve.valid = true;
        return true;
    }

    if (g_last_valve.battery != batt ||
        g_last_valve.leak_state != leak ||
        g_last_valve.valve_state != state ||
        g_last_valve.rmleak != rmleak) {
        g_last_valve.battery = batt;
        g_last_valve.leak_state = leak;
        g_last_valve.valve_state = state;
        g_last_valve.rmleak = rmleak;
        return true;
    }

    return false;
}

// (DEPRECATED) Build valve delta JSON
__attribute__((unused))
static char *build_valve_delta_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "gatewayID", g_gateway_id);

    cJSON *devicesArr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "devices", devicesArr);

    cJSON *deviceObj = cJSON_CreateObject();
    cJSON_AddItemToArray(devicesArr, deviceObj);

    cJSON *valveObj = cJSON_CreateObject();
    cJSON_AddItemToObject(deviceObj, "valve", valveObj);

    char valve_mac[18];
    if (ble_valve_get_mac(valve_mac))
    {
        cJSON_AddStringToObject(valveObj, "valve_mac", valve_mac);
        cJSON_AddNumberToObject(valveObj, "battery", ble_valve_get_battery());
        cJSON_AddBoolToObject(valveObj, "leak_state", ble_valve_get_leak());
        cJSON_AddBoolToObject(valveObj, "rmleak", ble_valve_get_rmleak_state());

        int state = ble_valve_get_state();
        if (state == 1)
            cJSON_AddStringToObject(valveObj, "valve_state", "open");
        else if (state == 0)
            cJSON_AddStringToObject(valveObj, "valve_state", "closed");
        else
            cJSON_AddStringToObject(valveObj, "valve_state", "unknown");
    }
    else
    {
        cJSON_AddNullToObject(valveObj, "valve_mac");
        cJSON_AddStringToObject(valveObj, "valve_state", "disconnected");
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// (DEPRECATED) Build LoRa delta JSON
__attribute__((unused))
static char *build_lora_delta_json(const lora_packet_t *pkt)
{
    if (!pkt) return NULL;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "gatewayID", g_gateway_id);

    cJSON *devicesArr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "devices", devicesArr);

    cJSON *deviceObj = cJSON_CreateObject();
    cJSON_AddItemToArray(devicesArr, deviceObj);

    cJSON *sensorsObj = cJSON_CreateObject();
    cJSON_AddItemToObject(deviceObj, "leak_sensors", sensorsObj);

    char sensorKey[32];
    snprintf(sensorKey, sizeof(sensorKey), "sensor_0x%08lX", pkt->sensorId);

    cJSON *thisSensor = cJSON_CreateObject();
    cJSON_AddItemToObject(sensorsObj, sensorKey, thisSensor);

    cJSON_AddNumberToObject(thisSensor, "battery", pkt->batteryPercentage);
    cJSON_AddBoolToObject(thisSensor, "leak_state", (pkt->leakStatus != 0));
    cJSON_AddNumberToObject(thisSensor, "rssi", pkt->rssi);

    char lora_id[16];
    snprintf(lora_id, sizeof(lora_id), "0x%08lX", pkt->sensorId);
    const sensor_meta_entry_t *meta = sensor_meta_find(SENSOR_TYPE_LORA, lora_id);
    cJSON *locObj = cJSON_CreateObject();
    cJSON_AddStringToObject(locObj, "code",
        sensor_meta_location_code_to_str(meta ? meta->location_code : LOC_UNKNOWN));
    cJSON_AddStringToObject(locObj, "label", meta ? meta->label : "");
    cJSON_AddItemToObject(thisSensor, "location", locObj);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// (DEPRECATED) Build BLE leak sensor delta JSON
__attribute__((unused))
static char *build_ble_leak_delta_json(const ble_leak_event_t *evt)
{
    if (!evt) return NULL;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "gatewayID", g_gateway_id);

    cJSON *devicesArr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "devices", devicesArr);

    cJSON *deviceObj = cJSON_CreateObject();
    cJSON_AddItemToArray(devicesArr, deviceObj);

    cJSON *sensorsObj = cJSON_CreateObject();
    cJSON_AddItemToObject(deviceObj, "ble_leak_sensors", sensorsObj);

    char sensorKey[32];
    snprintf(sensorKey, sizeof(sensorKey), "BLE_%s", evt->sensor_mac_str);

    cJSON *thisSensor = cJSON_CreateObject();
    cJSON_AddItemToObject(sensorsObj, sensorKey, thisSensor);

    cJSON_AddNumberToObject(thisSensor, "battery", evt->battery);
    cJSON_AddBoolToObject(thisSensor, "leak_state", evt->leak_detected);
    cJSON_AddNumberToObject(thisSensor, "rssi", evt->rssi);

    const sensor_meta_entry_t *meta = sensor_meta_find(SENSOR_TYPE_BLE_LEAK, evt->sensor_mac_str);
    cJSON *locObj = cJSON_CreateObject();
    cJSON_AddStringToObject(locObj, "code",
        sensor_meta_location_code_to_str(meta ? meta->location_code : LOC_UNKNOWN));
    cJSON_AddStringToObject(locObj, "label", meta ? meta->label : "");
    cJSON_AddItemToObject(thisSensor, "location", locObj);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// ---------------------------------------------------------------------------
// C2D command dispatch (uses c2d_commands parser)
// ---------------------------------------------------------------------------

static void handle_c2d_command(const char *data, size_t data_len)
{
    c2d_command_t cmd;
    if (!c2d_command_parse(data, data_len, &cmd)) {
        ESP_LOGW(IOTHUB_TAG, "Unrecognized C2D payload");
        return;
    }

    ESP_LOGI(IOTHUB_TAG, "C2D cmd='%s' ver=%d id='%s'", cmd.cmd, cmd.ver, cmd.id);

    bool success = true;
    const char *error_msg = NULL;

    // ---- Valve control ----
    if (strcmp(cmd.cmd, C2D_CMD_VALVE_OPEN) == 0) {
        ESP_LOGI(IOTHUB_TAG, "Command: VALVE_OPEN");
        ble_valve_connect();
        ble_valve_open();
    }
    else if (strcmp(cmd.cmd, C2D_CMD_VALVE_CLOSE) == 0) {
        ESP_LOGI(IOTHUB_TAG, "Command: VALVE_CLOSE");
        ble_valve_connect();
        ble_valve_close();
    }
    // ---- Valve set state (unified open/close) ----
    else if (strcmp(cmd.cmd, C2D_CMD_VALVE_SET_STATE) == 0) {
        cJSON *pl = cmd.payload_json ? cJSON_Parse(cmd.payload_json) : NULL;
        const char *desired = pl ? cJSON_GetStringValue(cJSON_GetObjectItem(pl, "state")) : NULL;
        if (!desired) {
            success = false;
            error_msg = "missing 'state' field (expected \"open\" or \"closed\")";
        } else if (strcmp(desired, "open") == 0) {
            ESP_LOGI(IOTHUB_TAG, "Command: VALVE_SET_STATE -> open");
            ble_valve_connect();
            ble_valve_open();
        } else if (strcmp(desired, "closed") == 0) {
            ESP_LOGI(IOTHUB_TAG, "Command: VALVE_SET_STATE -> closed");
            ble_valve_connect();
            ble_valve_close();
        } else {
            success = false;
            error_msg = "invalid state value (expected \"open\" or \"closed\")";
        }
        if (pl) cJSON_Delete(pl);
    }
    // ---- Leak reset ----
    else if (strcmp(cmd.cmd, C2D_CMD_LEAK_RESET) == 0) {
        ESP_LOGI(IOTHUB_TAG, "Command: LEAK_RESET");
        rules_engine_reset_leak_incident();
        ESP_LOGI(IOTHUB_TAG, "Leak incident cleared, RMLEAK reset");
    }
    // ---- Decommission ----
    else if (strcmp(cmd.cmd, C2D_CMD_DECOMMISSION) == 0) {
        cJSON *pl = cmd.payload_json ? cJSON_Parse(cmd.payload_json) : NULL;
        const char *target = pl ? cJSON_GetStringValue(cJSON_GetObjectItem(pl, "target")) : NULL;

        if (!target) {
            success = false;
            error_msg = "missing decommission target";
        }
        else if (strcmp(target, "valve") == 0) {
            ESP_LOGW(IOTHUB_TAG, "!!! DECOMMISSION_VALVE !!!");
            if (provisioning_remove_valve()) {
                health_engine_reload_devices();
                ble_valve_set_target_mac(NULL);
                ble_valve_disconnect();
                if (!provisioning_is_provisioned())
                    ESP_LOGI(IOTHUB_TAG, "Device is now UNPROVISIONED");
            } else {
                success = false;
                error_msg = "valve decommission failed";
            }
        }
        else if (strcmp(target, "lora") == 0) {
            const char *sid_str = cJSON_GetStringValue(
                cJSON_GetObjectItem(pl, "sensor_id"));
            uint32_t sid = sid_str ? (uint32_t)strtoul(sid_str, NULL, 16) : 0;
            ESP_LOGW(IOTHUB_TAG, "!!! DECOMMISSION_LORA: 0x%08lX !!!", (unsigned long)sid);
            if (provisioning_remove_lora_sensor(sid)) {
                health_engine_reload_devices();
                char lora_id_str[16];
                snprintf(lora_id_str, sizeof(lora_id_str), "0x%08lX",
                         (unsigned long)sid);
                sensor_meta_remove(SENSOR_TYPE_LORA, lora_id_str);
                if (!provisioning_is_provisioned())
                    ESP_LOGI(IOTHUB_TAG, "Device is now UNPROVISIONED");
            } else {
                success = false;
                error_msg = "lora sensor decommission failed";
            }
        }
        else if (strcmp(target, "ble") == 0) {
            const char *mac = cJSON_GetStringValue(
                cJSON_GetObjectItem(pl, "sensor_id"));
            ESP_LOGW(IOTHUB_TAG, "!!! DECOMMISSION_BLE: %s !!!", mac ? mac : "?");
            if (mac && provisioning_remove_ble_sensor(mac)) {
                health_engine_reload_devices();
                sensor_meta_remove(SENSOR_TYPE_BLE_LEAK, mac);
                if (!provisioning_is_provisioned())
                    ESP_LOGI(IOTHUB_TAG, "Device is now UNPROVISIONED");
            } else {
                success = false;
                error_msg = "ble sensor decommission failed";
            }
        }
        else if (strcmp(target, "all") == 0) {
            ESP_LOGW(IOTHUB_TAG, "!!! DECOMMISSION_ALL !!!");
            if (provisioning_decommission()) {
                sensor_meta_clear_all();
                ble_valve_set_target_mac(NULL);
                ble_valve_disconnect();

                // Send ack before restart
                if (cmd.is_envelope || cmd.id[0]) {
                    telemetry_v2_publish_cmd_ack(cmd.id, cmd.cmd, true, NULL);
                }
                c2d_command_free(&cmd);
                if (pl) cJSON_Delete(pl);

                ESP_LOGI(IOTHUB_TAG, "Restarting in 3s...");
                vTaskDelay(pdMS_TO_TICKS(3000));
                esp_restart();
                return;  // never reached
            } else {
                success = false;
                error_msg = "full decommission failed";
            }
        }
        else {
            success = false;
            error_msg = "unknown decommission target";
        }

        if (pl) cJSON_Delete(pl);
    }
    // ---- Rules config ----
    else if (strcmp(cmd.cmd, C2D_CMD_RULES_CONFIG) == 0) {
        ESP_LOGI(IOTHUB_TAG, "Command: RULES_CONFIG");
        if (!cmd.payload_json ||
            !rules_engine_handle_config_command(cmd.payload_json)) {
            success = false;
            error_msg = "rules config update failed";
        }
    }
    // ---- Sensor metadata ----
    else if (strcmp(cmd.cmd, C2D_CMD_SENSOR_META) == 0) {
        ESP_LOGI(IOTHUB_TAG, "Command: SENSOR_META");
        if (!cmd.payload_json ||
            !sensor_meta_handle_command(cmd.payload_json)) {
            success = false;
            error_msg = "sensor metadata update failed";
        }
    }
    // ---- Provisioning ----
    else if (strcmp(cmd.cmd, C2D_CMD_PROVISION) == 0) {
        ESP_LOGI(IOTHUB_TAG, "Provisioning JSON detected");
        if (cmd.payload_json &&
            provisioning_handle_azure_payload_json(
                cmd.payload_json, strlen(cmd.payload_json))) {
            health_engine_reload_devices();
            iothub_apply_provisioned_mac();
        } else {
            success = false;
            error_msg = "provisioning failed";
        }
    }
    else {
        ESP_LOGW(IOTHUB_TAG, "Unknown command: %s", cmd.cmd);
        success = false;
        error_msg = "unknown command";
    }

    // Send ack for v1 commands or when correlation ID is present
    if (cmd.is_envelope || cmd.id[0]) {
        telemetry_v2_publish_cmd_ack(cmd.id, cmd.cmd, success, error_msg);
    }

    c2d_command_free(&cmd);
}

// ---------------------------------------------------------------------------
// MQTT event handler
// ---------------------------------------------------------------------------

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(IOTHUB_TAG, "Connected to Azure IoT Hub!");
        g_iot_hub_connected = true;
        g_needs_lifecycle = true;  // Event loop will publish lifecycle + snapshot
        {
            char sub_topic[128];
            snprintf(sub_topic, sizeof(sub_topic),
                     "devices/%s/messages/devicebound/#", AZURE_DEVICE_ID);
            esp_mqtt_client_subscribe(mqtt_client, sub_topic, 1);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(IOTHUB_TAG, "Disconnected.");
        g_iot_hub_connected = false;
        break;

    case MQTT_EVENT_DATA:
    {
        ESP_LOGI(IOTHUB_TAG, "Received C2D Message!");
        if (event->data_len > 0) {
            // Log raw payload for debugging
            char *dbg = (char *)malloc(event->data_len + 1);
            if (dbg) {
                memcpy(dbg, event->data, event->data_len);
                dbg[event->data_len] = '\0';
                ESP_LOGI(IOTHUB_TAG, "Payload: %s", dbg);
                free(dbg);
            }
            handle_c2d_command(event->data, event->data_len);
        }
        break;
    }

    default:
        break;
    }
}

void iothub_apply_provisioned_mac(void)
{
    char valve_mac[18];
    if (provisioning_get_valve_mac(valve_mac)) {
        ESP_LOGI(IOTHUB_TAG, "Applying provisioned valve MAC: %s", valve_mac);
        ble_valve_set_target_mac(valve_mac);

        // Start BLE now that we're provisioned
        ESP_LOGI(IOTHUB_TAG, "Starting BLE with provisioned MAC...");
        app_ble_valve_signal_start();

        // If we're already connected to wrong device, disconnect
        char current_mac[18];
        if (ble_valve_get_mac(current_mac)) {
            if (strcasecmp(current_mac, valve_mac) != 0) {
                ESP_LOGW(IOTHUB_TAG, "Connected to wrong MAC, will reconnect to: %s", valve_mac);
                ble_valve_connect();
            }
        } else {
            ESP_LOGI(IOTHUB_TAG, "Not connected, triggering connection to: %s", valve_mac);
            ble_valve_connect();
        }
    }
}

static void initialize_sntp(void)
{
    ESP_LOGI(IOTHUB_TAG, "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    setenv("TZ", "AEST-10", 1); tzset();
    esp_sntp_init();

    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;

    while (timeinfo.tm_year < (2020 - 1900))
    {
        ESP_LOGI(IOTHUB_TAG, "Waiting for time... (%d)", ++retry);
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now);
        localtime_r(&now, &timeinfo);
        if (retry > 60)
            break;
    }
    ESP_LOGI(IOTHUB_TAG, "Time synced: %s", asctime(&timeinfo));
}

static void init_gateway_id(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(g_gateway_id, sizeof(g_gateway_id), "GW-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(IOTHUB_TAG, "Gateway ID: %s", g_gateway_id);
}

// ---------------------------------------------------------------------------
// Main IoT Hub task
// ---------------------------------------------------------------------------

void iothub_task(void *param)
{
    ESP_LOGI(IOTHUB_TAG, "Waiting for Wi-Fi...");
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ESP_LOGI(IOTHUB_TAG, "Starting IOT Hub Task...");

    // Initialize provisioning manager
    if (!provisioning_init()) {
        ESP_LOGE(IOTHUB_TAG, "Failed to initialize provisioning manager");
    }

    // Initialize sensor metadata and rules engine
    sensor_meta_init();
    rules_engine_init();
    health_engine_init();

    // Check provisioning state
    if (provisioning_is_provisioned()) {
        ESP_LOGI(IOTHUB_TAG, "Hub is PROVISIONED");
        char valve_mac[18];
        if (provisioning_get_valve_mac(valve_mac)) {
            ESP_LOGI(IOTHUB_TAG, "Provisioned valve MAC: %s", valve_mac);
            ble_valve_set_target_mac(valve_mac);
            ESP_LOGI(IOTHUB_TAG, "Starting BLE with provisioned MAC...");
            app_ble_valve_signal_start();
        }
    } else {
        ESP_LOGI(IOTHUB_TAG, "Hub is UNPROVISIONED - waiting for provisioning JSON from Azure");
    }

    init_gateway_id();
    initialize_sntp();

    char resource_uri[128];
    snprintf(resource_uri, sizeof(resource_uri), "%s.azure-devices.net/devices/%s", AZURE_HUB_NAME, AZURE_DEVICE_ID);
    char *sas_token = generate_sas_token(resource_uri, AZURE_PRIMARY_KEY, 31536000);

    char uri[128], username[256];
    snprintf(uri, sizeof(uri), "mqtts://%s.azure-devices.net", AZURE_HUB_NAME);
    snprintf(username, sizeof(username), "%s.azure-devices.net/%s/?api-version=2021-04-12", AZURE_HUB_NAME, AZURE_DEVICE_ID);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials = {
            .username = username,
            .client_id = AZURE_DEVICE_ID,
            .authentication = {.password = sas_token}},
        .session.keepalive = 60,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    // Initialize telemetry v2 (creates snapshot timer + queue)
    telemetry_v2_init(mqtt_client, AZURE_DEVICE_ID, g_gateway_id,
                      g_telem_lora_cache, g_telem_ble_cache);

    // Drain queues before adding to QueueSet
    lora_packet_t dummy_pkt;
    ble_update_type_t dummy_upd;
    ble_leak_event_t dummy_leak;
    uint8_t dummy_snap;
    while (xQueueReceive(lora_rx_queue, &dummy_pkt, 0) == pdTRUE)
        ;
    while (xQueueReceive(ble_update_queue, &dummy_upd, 0) == pdTRUE)
        ;
    while (ble_leak_rx_queue && xQueueReceive(ble_leak_rx_queue, &dummy_leak, 0) == pdTRUE)
        ;
    // Drain snapshot queue (just created, should be empty — defensive)
    QueueHandle_t snap_q = telemetry_v2_get_snapshot_queue();
    while (snap_q && xQueueReceive(snap_q, &dummy_snap, 0) == pdTRUE)
        ;
    // Reset BLE leak sensor tracking so next advertisement triggers a fresh event
    app_ble_leak_reset_tracking();

    // QueueSet: 3 existing queues + 1 snapshot trigger queue
    QueueSetHandle_t evt_queue_set = xQueueCreateSet(26);
    xQueueAddToSet(lora_rx_queue, evt_queue_set);
    xQueueAddToSet(ble_update_queue, evt_queue_set);
    if (ble_leak_rx_queue) {
        xQueueAddToSet(ble_leak_rx_queue, evt_queue_set);
    }
    if (snap_q) {
        xQueueAddToSet(snap_q, evt_queue_set);
    }

    // Start the periodic snapshot timer (fires every SNAPSHOT_INTERVAL_MS)
    telemetry_v2_start_snapshot_timer();

    lora_packet_t pkt;
    ble_update_type_t ble_upd_type;
    ble_leak_event_t ble_leak_evt;
    QueueSetMemberHandle_t active_queue;

    ESP_LOGI(IOTHUB_TAG, "QueueSet Initialized. Event loop starting...");

    while (1)
    {
        active_queue = xQueueSelectFromSet(evt_queue_set, pdMS_TO_TICKS(30000));

        // Periodic rules engine tick (auto-clear timeout, valve override detection)
        rules_engine_tick();

        // =================================================================
        // Phase 1: RECEIVE (always -- regardless of connection state)
        // =================================================================
        bool has_lora = false, has_valve = false, has_ble_leak = false;
        bool has_snapshot = false;

        if (active_queue == lora_rx_queue) {
            has_lora = xQueueReceive(lora_rx_queue, &pkt, 0);
        } else if (active_queue == ble_update_queue) {
            has_valve = xQueueReceive(ble_update_queue, &ble_upd_type, 0);
        } else if (active_queue == ble_leak_rx_queue) {
            has_ble_leak = xQueueReceive(ble_leak_rx_queue, &ble_leak_evt, 0);
        } else if (snap_q && active_queue == snap_q) {
            uint8_t trig;
            xQueueReceive(snap_q, &trig, 0);
            has_snapshot = true;
        }

        // =================================================================
        // Phase 2: RULES (always -- works offline, no MQTT needed)
        // =================================================================
        if (has_lora) {
            char lora_id_str[16];
            snprintf(lora_id_str, sizeof(lora_id_str), "0x%08lX",
                     (unsigned long)pkt.sensorId);
            rules_engine_evaluate_leak(LEAK_SOURCE_LORA,
                                       (pkt.leakStatus != 0), lora_id_str);
        }
        if (has_ble_leak) {
            rules_engine_evaluate_leak(LEAK_SOURCE_BLE,
                                       ble_leak_evt.leak_detected,
                                       ble_leak_evt.sensor_mac_str);
        }
        if (has_valve && ble_upd_type == BLE_UPD_LEAK) {
            rules_engine_evaluate_leak(LEAK_SOURCE_VALVE_FLOOD,
                                       ble_valve_get_leak(), "valve");
        }

        // Re-assert RMLEAK on valve reconnection if leak incident is active
        if (has_valve && ble_upd_type == BLE_UPD_CONNECTED) {
            rules_engine_reassert_rmleak_if_needed();
        }

        // Check for pending rules engine telemetry (auto-close, rmleak events)
        char *auto_close_json = rules_engine_take_pending_telemetry();

        // =================================================================
        // Phase 3: PUBLISH (only when connected + provisioned)
        // =================================================================
        if (!g_iot_hub_connected || !provisioning_is_provisioned()) {
            if (auto_close_json) free(auto_close_json);
            continue;
        }

        // ---- Lifecycle on first connect / reconnect ----
        if (g_needs_lifecycle) {
            g_needs_lifecycle = false;
            telemetry_v2_publish_lifecycle();
            g_boot_snapshot_sent = false;  // Wait for boot sync before first snapshot
        }

        // ---- Boot snapshot: fires once after all sensors checked in (or 2-min timeout) ----
        if (!g_boot_snapshot_sent && health_is_boot_sync_complete()) {
            g_boot_snapshot_sent = true;
            telemetry_v2_publish_snapshot();
        }

        // ---- Rules engine events (auto-close, rmleak changes) ----
        if (auto_close_json) {
            telemetry_v2_publish_rules_event(auto_close_json);
            free(auto_close_json);
        }

        // ---- Health alerts (Critical transitions) ----
        {
            health_alert_t alert;
            while (health_pop_alert(&alert)) {
                char *json = health_alert_to_json(&alert);
                if (json) {
                    telemetry_v2_publish_health_event(json);
                    free(json);
                }
            }
        }

        // ---- Periodic snapshot ----
        if (has_snapshot) {
            telemetry_v2_publish_snapshot();
        }

        // ---- LoRa sensor events ----
        if (has_lora) {
            ESP_LOGI(IOTHUB_TAG, "Event: LoRa Packet from 0x%08lX",
                     (unsigned long)pkt.sensorId);

            if (!provisioning_is_lora_sensor_provisioned(pkt.sensorId)) {
                ESP_LOGW(IOTHUB_TAG, "Sensor 0x%08lX not provisioned, skipping",
                         (unsigned long)pkt.sensorId);
            } else {
                bool leak_changed = update_lora_cache_check_leak(&pkt);
                if (leak_changed) {
                    char lora_id[16];
                    snprintf(lora_id, sizeof(lora_id), "0x%08lX",
                             (unsigned long)pkt.sensorId);
                    telemetry_v2_publish_leak_event(
                        pkt.leakStatus ? "leak_detected" : "leak_cleared",
                        "lora", lora_id,
                        (pkt.leakStatus != 0), pkt.batteryPercentage, pkt.rssi);
                }
            }
        }

        // ---- Valve events ----
        if (has_valve) {
            ESP_LOGI(IOTHUB_TAG, "Event: BLE Update type=%d", ble_upd_type);

            // Verify connected valve MAC matches provisioned MAC
            char connected_mac[18];
            char provisioned_mac[18];
            bool mac_ok = false;
            if (ble_valve_get_mac(connected_mac) &&
                provisioning_get_valve_mac(provisioned_mac)) {
                if (strcasecmp(connected_mac, provisioned_mac) == 0) {
                    mac_ok = true;
                } else {
                    ESP_LOGW(IOTHUB_TAG,
                        "Connected valve MAC %s != provisioned %s, skipping",
                        connected_mac, provisioned_mac);
                }
            }

            if (mac_ok) {
                if (ble_upd_type == BLE_UPD_LEAK) {
                    telemetry_v2_publish_valve_event(
                        ble_valve_get_leak() ? "valve_flood_detected"
                                             : "valve_flood_cleared");
                } else if (ble_upd_type == BLE_UPD_STATE) {
                    telemetry_v2_publish_valve_event("valve_state_changed");
                }
                // BLE_UPD_BATTERY: no event — included in snapshot
                // BLE_UPD_RMLEAK: handled by rules engine events
                // BLE_UPD_CONNECTED: lifecycle/snapshot handles this
            }
        }

        // ---- BLE leak sensor events ----
        if (has_ble_leak) {
            ESP_LOGI(IOTHUB_TAG, "Event: BLE Leak %s leak=%d batt=%d",
                     ble_leak_evt.sensor_mac_str,
                     ble_leak_evt.leak_detected, ble_leak_evt.battery);

            bool leak_changed = update_ble_leak_cache_check_leak(&ble_leak_evt);
            if (leak_changed) {
                telemetry_v2_publish_leak_event(
                    ble_leak_evt.leak_detected ? "leak_detected" : "leak_cleared",
                    "ble_leak_sensor", ble_leak_evt.sensor_mac_str,
                    ble_leak_evt.leak_detected,
                    ble_leak_evt.battery, ble_leak_evt.rssi);
            }
        }
    }
}

void initialize_iothub(void)
{
    xTaskCreate(iothub_task, "iothub_task", 8192, NULL, 5, &iothub_task_handle);
}
