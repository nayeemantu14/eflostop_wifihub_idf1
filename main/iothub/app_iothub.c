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
#include "sensor_meta/sensor_meta.h"

// External Queue from LoRa app
extern QueueHandle_t lora_rx_queue;

TaskHandle_t iothub_task_handle = NULL;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool g_iot_hub_connected = false;
static char g_gateway_id[32] = {0};

// Last sent values for duplicate detection
typedef struct {
    uint8_t battery;
    bool leak_state;
    int valve_state;
    bool valid;
} valve_cache_t;

static valve_cache_t g_last_valve = {0};

// LoRa sensor cache (simple last-sent tracking)
#define MAX_LORA_CACHE 16
typedef struct {
    uint32_t sensor_id;
    uint8_t battery;
    uint8_t leak_status;
    int8_t rssi;
    bool valid;
} lora_cache_entry_t;

static lora_cache_entry_t g_lora_cache[MAX_LORA_CACHE] = {0};

// BLE leak sensor cache (for duplicate detection)
#define MAX_BLE_LEAK_CACHE 16
typedef struct {
    char mac_str[18];
    uint8_t battery;
    bool leak_state;
    int8_t rssi;
    bool valid;
} ble_leak_cache_entry_t;

static ble_leak_cache_entry_t g_ble_leak_cache[MAX_BLE_LEAK_CACHE] = {0};

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

// Check if LoRa packet is a duplicate
static bool is_lora_duplicate(const lora_packet_t *pkt)
{
    for (int i = 0; i < MAX_LORA_CACHE; i++) {
        if (g_lora_cache[i].valid && g_lora_cache[i].sensor_id == pkt->sensorId) {
            // Found cached entry - check if values changed
            if (g_lora_cache[i].battery == pkt->batteryPercentage &&
                g_lora_cache[i].leak_status == pkt->leakStatus &&
                g_lora_cache[i].rssi == pkt->rssi) {
                return true; // Duplicate
            }
            // Values changed, update cache
            g_lora_cache[i].battery = pkt->batteryPercentage;
            g_lora_cache[i].leak_status = pkt->leakStatus;
            g_lora_cache[i].rssi = pkt->rssi;
            return false;
        }
    }
    
    // Not in cache, add it
    for (int i = 0; i < MAX_LORA_CACHE; i++) {
        if (!g_lora_cache[i].valid) {
            g_lora_cache[i].sensor_id = pkt->sensorId;
            g_lora_cache[i].battery = pkt->batteryPercentage;
            g_lora_cache[i].leak_status = pkt->leakStatus;
            g_lora_cache[i].rssi = pkt->rssi;
            g_lora_cache[i].valid = true;
            return false;
        }
    }
    
    // Cache full, overwrite oldest (index 0)
    g_lora_cache[0].sensor_id = pkt->sensorId;
    g_lora_cache[0].battery = pkt->batteryPercentage;
    g_lora_cache[0].leak_status = pkt->leakStatus;
    g_lora_cache[0].rssi = pkt->rssi;
    g_lora_cache[0].valid = true;
    return false;
}

// Check if valve data changed
static bool valve_data_changed(void)
{
    uint8_t batt = ble_valve_get_battery();
    bool leak = ble_valve_get_leak();
    int state = ble_valve_get_state();
    
    if (!g_last_valve.valid) {
        // First time
        g_last_valve.battery = batt;
        g_last_valve.leak_state = leak;
        g_last_valve.valve_state = state;
        g_last_valve.valid = true;
        return true;
    }
    
    if (g_last_valve.battery != batt || 
        g_last_valve.leak_state != leak || 
        g_last_valve.valve_state != state) {
        // Changed
        g_last_valve.battery = batt;
        g_last_valve.leak_state = leak;
        g_last_valve.valve_state = state;
        return true;
    }
    
    return false; // No change
}

// Build valve delta JSON
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

// Build LoRa delta JSON
static char *build_lora_delta_json(const lora_packet_t *pkt)
{
    if (!pkt) return NULL;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "gatewayID", g_gateway_id);

    cJSON *devicesArr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "devices", devicesArr);

    cJSON *deviceObj = cJSON_CreateObject();
    cJSON_AddItemToArray(devicesArr, deviceObj);

    // NOTE: leak_sensors is now a sibling to valve, not nested under it
    cJSON *sensorsObj = cJSON_CreateObject();
    cJSON_AddItemToObject(deviceObj, "leak_sensors", sensorsObj);

    char sensorKey[32];
    snprintf(sensorKey, sizeof(sensorKey), "sensor_0x%08lX", pkt->sensorId);

    cJSON *thisSensor = cJSON_CreateObject();
    cJSON_AddItemToObject(sensorsObj, sensorKey, thisSensor);

    cJSON_AddNumberToObject(thisSensor, "battery", pkt->batteryPercentage);
    cJSON_AddBoolToObject(thisSensor, "leak_state", (pkt->leakStatus == 1));
    cJSON_AddNumberToObject(thisSensor, "rssi", pkt->rssi);

    // Enrich with location metadata
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

// Build BLE leak sensor delta JSON
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

    // Enrich with location metadata
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

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(IOTHUB_TAG, "Connected to Azure IoT Hub!");
        g_iot_hub_connected = true;
        char sub_topic[128];
        snprintf(sub_topic, sizeof(sub_topic), "devices/%s/messages/devicebound/#", AZURE_DEVICE_ID);
        esp_mqtt_client_subscribe(mqtt_client, sub_topic, 1);
        break;
        
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(IOTHUB_TAG, "Disconnected.");
        g_iot_hub_connected = false;
        break;
        
    case MQTT_EVENT_DATA:
    {
        ESP_LOGI(IOTHUB_TAG, "Received C2D Message!");
        
        // CRITICAL: event->data is NOT null-terminated
        // Create a safe null-terminated copy
        char *data_copy = NULL;
        if (event->data_len > 0) {
            data_copy = (char *)malloc(event->data_len + 1);
            if (data_copy) {
                memcpy(data_copy, event->data, event->data_len);
                data_copy[event->data_len] = '\0';
                
                ESP_LOGI(IOTHUB_TAG, "Payload: %s", data_copy);
                
                // Trim leading whitespace for command detection
                char *trimmed = data_copy;
                while (*trimmed == ' ' || *trimmed == '\t' || *trimmed == '\n' || *trimmed == '\r') {
                    trimmed++;
                }
                
                // ============================================================
                // DECOMMISSION COMMANDS
                // ============================================================
                
                // Check for DECOMMISSION_VALVE command
                if (strstr(trimmed, "DECOMMISSION_VALVE"))
                {
                    ESP_LOGW(IOTHUB_TAG, "!!! DECOMMISSION_VALVE COMMAND RECEIVED !!!");
                    
                    if (provisioning_remove_valve()) {
                        ESP_LOGI(IOTHUB_TAG, "Valve decommissioned successfully");
                        
                        // Clear BLE target MAC
                        ble_valve_set_target_mac(NULL);
                        
                        // Disconnect active BLE connection to old valve
                        ESP_LOGI(IOTHUB_TAG, "Disconnecting BLE connection...");
                        ble_valve_disconnect();
                        
                        if (!provisioning_is_provisioned()) {
                            ESP_LOGI(IOTHUB_TAG, "Device is now UNPROVISIONED - BLE stopped");
                        }
                    } else {
                        ESP_LOGE(IOTHUB_TAG, "Valve decommissioning failed!");
                    }
                }
                // Check for DECOMMISSION_LORA:0xXXXXXXXX command
                else if (strstr(trimmed, "DECOMMISSION_LORA:"))
                {
                    char *id_str = strstr(trimmed, "DECOMMISSION_LORA:") + strlen("DECOMMISSION_LORA:");
                    uint32_t sensor_id = 0;
                    
                    // Parse hex ID (supports 0x prefix or raw hex)
                    if (strncmp(id_str, "0x", 2) == 0 || strncmp(id_str, "0X", 2) == 0) {
                        sensor_id = (uint32_t)strtoul(id_str, NULL, 16);
                    } else {
                        sensor_id = (uint32_t)strtoul(id_str, NULL, 16);
                    }
                    
                    ESP_LOGW(IOTHUB_TAG, "!!! DECOMMISSION_LORA COMMAND: 0x%08lX !!!", sensor_id);
                    
                    if (provisioning_remove_lora_sensor(sensor_id)) {
                        ESP_LOGI(IOTHUB_TAG, "LoRa sensor 0x%08lX decommissioned successfully", sensor_id);

                        // Remove associated metadata
                        char lora_id_str[16];
                        snprintf(lora_id_str, sizeof(lora_id_str), "0x%08lX", sensor_id);
                        sensor_meta_remove(SENSOR_TYPE_LORA, lora_id_str);

                        if (!provisioning_is_provisioned()) {
                            ESP_LOGI(IOTHUB_TAG, "Device is now UNPROVISIONED");
                        }
                    } else {
                        ESP_LOGE(IOTHUB_TAG, "LoRa sensor decommissioning failed!");
                    }
                }
                // Check for DECOMMISSION_LORA without colon (user error - show help)
                else if (strstr(trimmed, "DECOMMISSION_LORA"))
                {
                    ESP_LOGE(IOTHUB_TAG, "Invalid command format!");
                    ESP_LOGE(IOTHUB_TAG, "Usage: DECOMMISSION_LORA:0xSENSORID");
                    ESP_LOGE(IOTHUB_TAG, "Example: DECOMMISSION_LORA:0x754A6237");
                }
                // Check for DECOMMISSION_BLE:MAC command
                else if (strstr(trimmed, "DECOMMISSION_BLE:"))
                {
                    char *mac_str = strstr(trimmed, "DECOMMISSION_BLE:") + strlen("DECOMMISSION_BLE:");
                    
                    // Trim any trailing whitespace from MAC
                    char mac_clean[18] = {0};
                    int mac_len = 0;
                    while (*mac_str && mac_len < 17 && 
                           (*mac_str != ' ' && *mac_str != '\t' && *mac_str != '\n' && *mac_str != '\r')) {
                        mac_clean[mac_len++] = *mac_str++;
                    }
                    mac_clean[mac_len] = '\0';
                    
                    ESP_LOGW(IOTHUB_TAG, "!!! DECOMMISSION_BLE COMMAND: %s !!!", mac_clean);
                    
                    if (provisioning_remove_ble_sensor(mac_clean)) {
                        ESP_LOGI(IOTHUB_TAG, "BLE sensor %s decommissioned successfully", mac_clean);

                        // Remove associated metadata
                        sensor_meta_remove(SENSOR_TYPE_BLE_LEAK, mac_clean);

                        if (!provisioning_is_provisioned()) {
                            ESP_LOGI(IOTHUB_TAG, "Device is now UNPROVISIONED");
                        }
                    } else {
                        ESP_LOGE(IOTHUB_TAG, "BLE sensor decommissioning failed!");
                    }
                }
                // Check for DECOMMISSION_BLE without colon (user error - show help)
                else if (strstr(trimmed, "DECOMMISSION_BLE"))
                {
                    ESP_LOGE(IOTHUB_TAG, "Invalid command format!");
                    ESP_LOGE(IOTHUB_TAG, "Usage: DECOMMISSION_BLE:MAC_ADDRESS");
                    ESP_LOGE(IOTHUB_TAG, "Example: DECOMMISSION_BLE:AA:BB:CC:DD:EE:FF");
                }
                // Check for DECOMMISSION_ALL or plain DECOMMISSION command (full factory reset)
                // Must check AFTER all specific DECOMMISSION_* commands
                else if (strstr(trimmed, "DECOMMISSION_ALL") || 
                         (strcmp(trimmed, "DECOMMISSION") == 0))
                {
                    ESP_LOGW(IOTHUB_TAG, "!!! DECOMMISSION_ALL COMMAND RECEIVED !!!");
                    
                    // Call decommission function
                    if (provisioning_decommission()) {
                        // Clear all sensor metadata
                        sensor_meta_clear_all();

                        ESP_LOGI(IOTHUB_TAG, "Decommissioning successful - restarting device in 3 seconds...");
                        
                        // Clear BLE target MAC
                        ble_valve_set_target_mac(NULL);
                        
                        // Disconnect BLE connection (will be terminated by restart anyway, but explicit is better)
                        ble_valve_disconnect();
                        
                        // Give time for MQTT ack and logs to flush
                        vTaskDelay(pdMS_TO_TICKS(3000));
                        
                        // Restart the device to clean state
                        esp_restart();
                    } else {
                        ESP_LOGE(IOTHUB_TAG, "Decommissioning failed!");
                    }
                }
                // ============================================================
                // VALVE CONTROL COMMANDS
                // ============================================================
                // Check for valve commands
                else if (strstr(trimmed, "VALVE_OPEN"))
                {
                    ESP_LOGI(IOTHUB_TAG, "Command: VALVE_OPEN");
                    ble_valve_connect();
                    ble_valve_open();
                }
                else if (strstr(trimmed, "VALVE_CLOSE"))
                {
                    ESP_LOGI(IOTHUB_TAG, "Command: VALVE_CLOSE");
                    ble_valve_connect();
                    ble_valve_close();
                }
                // ============================================================
                // CONFIGURATION COMMANDS
                // ============================================================
                else if (strstr(trimmed, "RULES_CONFIG:"))
                {
                    char *json_str = strstr(trimmed, "RULES_CONFIG:") + strlen("RULES_CONFIG:");
                    ESP_LOGI(IOTHUB_TAG, "Command: RULES_CONFIG");
                    if (rules_engine_handle_config_command(json_str)) {
                        ESP_LOGI(IOTHUB_TAG, "Rules config updated successfully");
                    } else {
                        ESP_LOGE(IOTHUB_TAG, "Rules config update failed");
                    }
                }
                else if (strstr(trimmed, "SENSOR_META:"))
                {
                    char *json_str = strstr(trimmed, "SENSOR_META:") + strlen("SENSOR_META:");
                    ESP_LOGI(IOTHUB_TAG, "Command: SENSOR_META");
                    if (sensor_meta_handle_command(json_str)) {
                        ESP_LOGI(IOTHUB_TAG, "Sensor metadata updated successfully");

                        // Publish ack telemetry
                        cJSON *ack_root = cJSON_Parse(json_str);
                        if (ack_root) {
                            cJSON_AddStringToObject(ack_root, "gatewayID", g_gateway_id);
                            cJSON_AddStringToObject(ack_root, "event", "sensor_meta_updated");
                            char *ack_json = cJSON_PrintUnformatted(ack_root);
                            cJSON_Delete(ack_root);
                            if (ack_json) {
                                char ack_topic[128];
                                snprintf(ack_topic, sizeof(ack_topic),
                                         "devices/%s/messages/events/", AZURE_DEVICE_ID);
                                esp_mqtt_client_publish(mqtt_client, ack_topic, ack_json, 0, 1, 0);
                                free(ack_json);
                            }
                        }
                    } else {
                        ESP_LOGE(IOTHUB_TAG, "Sensor metadata update failed");
                    }
                }
                // ============================================================
                // LEAK INCIDENT RESET
                // ============================================================
                else if (strstr(trimmed, "LEAK_RESET"))
                {
                    ESP_LOGI(IOTHUB_TAG, "Command: LEAK_RESET");
                    rules_engine_reset_leak_incident();
                    ESP_LOGI(IOTHUB_TAG, "Leak incident cleared, RMLEAK reset");
                }
                // Check if it's a JSON provisioning payload
                else if (trimmed[0] == '{')
                {
                    ESP_LOGI(IOTHUB_TAG, "Provisioning JSON detected");
                    if (provisioning_handle_azure_payload_json(trimmed, strlen(trimmed))) {
                        ESP_LOGI(IOTHUB_TAG, "Provisioning successful!");
                        
                        // Apply provisioned valve MAC to BLE
                        iothub_apply_provisioned_mac();
                    } else {
                        ESP_LOGW(IOTHUB_TAG, "Provisioning failed or incomplete");
                    }
                }
                
                free(data_copy);
            }
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
                // Trigger reconnection by sending disconnect then connect
                // The BLE module will use the new target MAC on next connect
                ble_valve_connect();
            }
        } else {
            // Not connected, trigger connection
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

    // Check provisioning state
    if (provisioning_is_provisioned()) {
        ESP_LOGI(IOTHUB_TAG, "Hub is PROVISIONED");
        // Apply provisioned MAC to BLE and start BLE
        char valve_mac[18];
        if (provisioning_get_valve_mac(valve_mac)) {
            ESP_LOGI(IOTHUB_TAG, "Provisioned valve MAC: %s", valve_mac);
            ble_valve_set_target_mac(valve_mac);
            
            // Start BLE with provisioned MAC
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

    // Drain queues
    lora_packet_t dummy_pkt;
    ble_update_type_t dummy_upd;
    ble_leak_event_t dummy_leak;
    while (xQueueReceive(lora_rx_queue, &dummy_pkt, 0) == pdTRUE)
        ;
    while (xQueueReceive(ble_update_queue, &dummy_upd, 0) == pdTRUE)
        ;
    while (ble_leak_rx_queue && xQueueReceive(ble_leak_rx_queue, &dummy_leak, 0) == pdTRUE)
        ;
    // Reset BLE leak sensor tracking so next advertisement triggers a fresh event
    // (the drain above discards early events before MQTT is connected)
    app_ble_leak_reset_tracking();

    QueueSetHandle_t evt_queue_set = xQueueCreateSet(25);
    xQueueAddToSet(lora_rx_queue, evt_queue_set);
    xQueueAddToSet(ble_update_queue, evt_queue_set);
    if (ble_leak_rx_queue) {
        xQueueAddToSet(ble_leak_rx_queue, evt_queue_set);
    }

    lora_packet_t pkt;
    ble_update_type_t ble_upd_type;
    ble_leak_event_t ble_leak_evt;
    QueueSetMemberHandle_t active_queue;
    char topic[128];
    snprintf(topic, sizeof(topic), "devices/%s/messages/events/", AZURE_DEVICE_ID);

    ESP_LOGI(IOTHUB_TAG, "QueueSet Initialized. Event loop starting...");

    while (1)
    {
        active_queue = xQueueSelectFromSet(evt_queue_set, portMAX_DELAY);

        // =================================================================
        // Phase 1: RECEIVE (always — regardless of connection state)
        // =================================================================
        bool has_lora = false, has_valve = false, has_ble_leak = false;

        if (active_queue == lora_rx_queue) {
            has_lora = xQueueReceive(lora_rx_queue, &pkt, 0);
        } else if (active_queue == ble_update_queue) {
            has_valve = xQueueReceive(ble_update_queue, &ble_upd_type, 0);
        } else if (active_queue == ble_leak_rx_queue) {
            has_ble_leak = xQueueReceive(ble_leak_rx_queue, &ble_leak_evt, 0);
        }

        // =================================================================
        // Phase 2: RULES (always — works offline, no MQTT needed)
        // =================================================================
        if (has_lora) {
            char lora_id_str[16];
            snprintf(lora_id_str, sizeof(lora_id_str), "0x%08lX", pkt.sensorId);
            rules_engine_evaluate_leak(LEAK_SOURCE_LORA, (pkt.leakStatus == 1), lora_id_str);
        }
        if (has_ble_leak) {
            rules_engine_evaluate_leak(LEAK_SOURCE_BLE, ble_leak_evt.leak_detected,
                                       ble_leak_evt.sensor_mac_str);
        }
        if (has_valve && ble_upd_type == BLE_UPD_LEAK && ble_valve_get_leak()) {
            rules_engine_evaluate_leak(LEAK_SOURCE_VALVE_FLOOD, true, "valve");
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

        // Publish rules engine telemetry if pending (auto-close, rmleak events)
        if (auto_close_json) {
            cJSON *ac_obj = cJSON_Parse(auto_close_json);
            free(auto_close_json);
            if (ac_obj) {
                cJSON_AddStringToObject(ac_obj, "gatewayID", g_gateway_id);
                char *ac_full = cJSON_PrintUnformatted(ac_obj);
                cJSON_Delete(ac_obj);
                if (ac_full) {
                    ESP_LOGI(IOTHUB_TAG, "Publishing rules event: %s", ac_full);
                    esp_mqtt_client_publish(mqtt_client, topic, ac_full, 0, 1, 0);
                    free(ac_full);
                }
            }
        }

        // Build and publish sensor telemetry
        char *json_payload = NULL;

        if (has_lora)
        {
            ESP_LOGI(IOTHUB_TAG, "Event: LoRa Packet from 0x%08lX", pkt.sensorId);

            // Check if this specific sensor is provisioned
            if (!provisioning_is_lora_sensor_provisioned(pkt.sensorId)) {
                ESP_LOGW(IOTHUB_TAG, "Sensor 0x%08lX not provisioned, skipping", pkt.sensorId);
            }
            // Check for duplicates
            else if (!is_lora_duplicate(&pkt)) {
                json_payload = build_lora_delta_json(&pkt);
            } else {
                ESP_LOGD(IOTHUB_TAG, "LoRa data unchanged, skipping publish");
            }
        }
        else if (has_valve)
        {
            ESP_LOGI(IOTHUB_TAG, "Event: BLE Update type=%d", ble_upd_type);

            // DEFENSE-IN-DEPTH: Verify connected valve MAC matches provisioned MAC
            char connected_mac[18];
            char provisioned_mac[18];
            bool mac_ok = false;
            if (ble_valve_get_mac(connected_mac) &&
                provisioning_get_valve_mac(provisioned_mac)) {
                if (strcasecmp(connected_mac, provisioned_mac) == 0) {
                    mac_ok = true;
                } else {
                    ESP_LOGW(IOTHUB_TAG, "Connected valve MAC %s doesn't match provisioned MAC %s, skipping",
                             connected_mac, provisioned_mac);
                }
            } else {
                ESP_LOGD(IOTHUB_TAG, "BLE not connected or no provisioned MAC, skipping");
            }

            if (mac_ok) {
                // Only publish if it's a meaningful update and data changed
                if (ble_upd_type == BLE_UPD_BATTERY ||
                    ble_upd_type == BLE_UPD_LEAK ||
                    ble_upd_type == BLE_UPD_STATE ||
                    ble_upd_type == BLE_UPD_CONNECTED)
                {
                    if (valve_data_changed()) {
                        json_payload = build_valve_delta_json();
                    } else {
                        ESP_LOGD(IOTHUB_TAG, "Valve data unchanged, skipping publish");
                    }
                }
            }
        }
        else if (has_ble_leak)
        {
            ESP_LOGI(IOTHUB_TAG, "Event: BLE Leak Sensor %s leak=%d batt=%d",
                     ble_leak_evt.sensor_mac_str, ble_leak_evt.leak_detected, ble_leak_evt.battery);
            json_payload = build_ble_leak_delta_json(&ble_leak_evt);
        }

        if (json_payload)
        {
            ESP_LOGI(IOTHUB_TAG, "Publishing: %s", json_payload);
            esp_mqtt_client_publish(mqtt_client, topic, json_payload, 0, 1, 0);
            free(json_payload);
        }
    }
}

void initialize_iothub(void)
{
    xTaskCreate(iothub_task, "iothub_task", 8192, NULL, 5, &iothub_task_handle);
}