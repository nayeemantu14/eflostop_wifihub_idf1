#include "provisioning_manager.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define PROV_TAG "PROVISIONING"
#define NVS_NAMESPACE "provision"
#define NVS_KEY_VERSION "cfg_ver"
#define NVS_KEY_STATE "state"
#define NVS_KEY_VALVE_MAC "valve_mac"
#define NVS_KEY_LORA_COUNT "lora_cnt"
#define NVS_KEY_LORA_IDS "lora_ids"
#define NVS_KEY_LEAK_COUNT "leak_cnt"
#define NVS_KEY_LEAK_MACS "leak_macs"

#define CURRENT_CONFIG_VERSION 1

static provisioning_config_t g_config = {0};
static bool g_initialized = false;
static SemaphoreHandle_t g_prov_mutex = NULL;

// Forward declaration
static bool validate_mac_string(const char *mac_str);
static bool parse_hex_id(const char *hex_str, uint32_t *out_id);

bool provisioning_init(void)
{
    if (g_initialized) {
        ESP_LOGW(PROV_TAG, "Already initialized");
        return true;
    }

    ESP_LOGI(PROV_TAG, "Initializing provisioning manager...");

    // Create mutex for thread-safe access
    g_prov_mutex = xSemaphoreCreateMutex();
    if (g_prov_mutex == NULL) {
        ESP_LOGE(PROV_TAG, "Failed to create mutex");
        return false;
    }

    // Initialize NVS if not already done
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(PROV_TAG, "NVS partition was truncated or version changed, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
        ESP_LOGE(PROV_TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        vSemaphoreDelete(g_prov_mutex);
        g_prov_mutex = NULL;
        return false;
    }

    // Try to load existing config
    memset(&g_config, 0, sizeof(g_config));
    g_config.config_version = CURRENT_CONFIG_VERSION;
    g_config.state = PROV_STATE_UNPROVISIONED;

    if (provisioning_load_from_nvs(&g_config)) {
        ESP_LOGI(PROV_TAG, "Loaded existing config from NVS");
        ESP_LOGI(PROV_TAG, "State: %s", 
                 g_config.state == PROV_STATE_PROVISIONED ? "PROVISIONED" : "UNPROVISIONED");
        if (g_config.state == PROV_STATE_PROVISIONED) {
            ESP_LOGI(PROV_TAG, "Valve MAC: %s", g_config.valve_mac);
            ESP_LOGI(PROV_TAG, "LoRa sensors: %d", g_config.lora_sensor_count);
            ESP_LOGI(PROV_TAG, "BLE leak sensors: %d", g_config.ble_leak_sensor_count);
        }
    } else {
        ESP_LOGI(PROV_TAG, "No existing config found, starting UNPROVISIONED");
    }

    g_initialized = true;
    return true;
}

bool provisioning_is_provisioned(void)
{
    if (!g_initialized || g_prov_mutex == NULL) {
        return false;
    }
    
    bool result = false;
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        result = (g_config.state == PROV_STATE_PROVISIONED);
        xSemaphoreGive(g_prov_mutex);
    } else {
        ESP_LOGW(PROV_TAG, "Failed to take mutex in is_provisioned");
    }
    
    return result;
}

provisioning_state_t provisioning_get_state(void)
{
    if (!g_initialized || g_prov_mutex == NULL) {
        return PROV_STATE_UNPROVISIONED;
    }
    
    provisioning_state_t state = PROV_STATE_UNPROVISIONED;
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        state = g_config.state;
        xSemaphoreGive(g_prov_mutex);
    } else {
        ESP_LOGW(PROV_TAG, "Failed to take mutex in get_state");
    }
    
    return state;
}

bool provisioning_load_from_nvs(provisioning_config_t *config)
{
    if (!config) {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGD(PROV_TAG, "NVS namespace not found (first boot?)");
        return false;
    }

    bool success = true;

    // Load version
    uint8_t version = 0;
    err = nvs_get_u8(nvs_handle, NVS_KEY_VERSION, &version);
    if (err != ESP_OK || version == 0) {
        ESP_LOGW(PROV_TAG, "Config version not found or invalid");
        success = false;
        goto cleanup;
    }
    config->config_version = version;

    // Load state
    uint8_t state = 0;
    err = nvs_get_u8(nvs_handle, NVS_KEY_STATE, &state);
    if (err != ESP_OK) {
        ESP_LOGW(PROV_TAG, "State not found");
        success = false;
        goto cleanup;
    }
    config->state = (provisioning_state_t)state;

    if (config->state != PROV_STATE_PROVISIONED) {
        // Not provisioned, no need to load other fields
        goto cleanup;
    }

    // Load valve MAC
    size_t required_size = sizeof(config->valve_mac);
    err = nvs_get_str(nvs_handle, NVS_KEY_VALVE_MAC, config->valve_mac, &required_size);
    if (err != ESP_OK) {
        ESP_LOGW(PROV_TAG, "Valve MAC not found");
        success = false;
        goto cleanup;
    }

    // Load LoRa sensor count
    err = nvs_get_u8(nvs_handle, NVS_KEY_LORA_COUNT, &config->lora_sensor_count);
    if (err != ESP_OK) {
        config->lora_sensor_count = 0;
    }

    // Load LoRa sensor IDs
    if (config->lora_sensor_count > 0) {
        required_size = sizeof(config->lora_sensor_ids);
        err = nvs_get_blob(nvs_handle, NVS_KEY_LORA_IDS, config->lora_sensor_ids, &required_size);
        if (err != ESP_OK) {
            ESP_LOGW(PROV_TAG, "Failed to load LoRa sensor IDs");
            config->lora_sensor_count = 0;
        }
    }

    // Load BLE leak sensor count
    err = nvs_get_u8(nvs_handle, NVS_KEY_LEAK_COUNT, &config->ble_leak_sensor_count);
    if (err != ESP_OK) {
        config->ble_leak_sensor_count = 0;
    }

    // Load BLE leak sensor MACs
    if (config->ble_leak_sensor_count > 0) {
        required_size = sizeof(config->ble_leak_sensors);
        err = nvs_get_blob(nvs_handle, NVS_KEY_LEAK_MACS, config->ble_leak_sensors, &required_size);
        if (err != ESP_OK) {
            ESP_LOGW(PROV_TAG, "Failed to load BLE leak sensor MACs");
            config->ble_leak_sensor_count = 0;
        }
    }

cleanup:
    nvs_close(nvs_handle);
    return success;
}

bool provisioning_save_to_nvs(const provisioning_config_t *config)
{
    if (!config) {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(PROV_TAG, "Failed to open NVS for write: %s", esp_err_to_name(err));
        return false;
    }

    bool success = true;

    // Save version
    err = nvs_set_u8(nvs_handle, NVS_KEY_VERSION, config->config_version);
    if (err != ESP_OK) {
        ESP_LOGE(PROV_TAG, "Failed to save version");
        success = false;
        goto cleanup;
    }

    // Save state
    err = nvs_set_u8(nvs_handle, NVS_KEY_STATE, (uint8_t)config->state);
    if (err != ESP_OK) {
        ESP_LOGE(PROV_TAG, "Failed to save state");
        success = false;
        goto cleanup;
    }

    // Save valve MAC
    err = nvs_set_str(nvs_handle, NVS_KEY_VALVE_MAC, config->valve_mac);
    if (err != ESP_OK) {
        ESP_LOGE(PROV_TAG, "Failed to save valve MAC");
        success = false;
        goto cleanup;
    }

    // Save LoRa sensor count
    err = nvs_set_u8(nvs_handle, NVS_KEY_LORA_COUNT, config->lora_sensor_count);
    if (err != ESP_OK) {
        ESP_LOGE(PROV_TAG, "Failed to save LoRa count");
        success = false;
        goto cleanup;
    }

    // Save LoRa sensor IDs
    if (config->lora_sensor_count > 0) {
        err = nvs_set_blob(nvs_handle, NVS_KEY_LORA_IDS, 
                          config->lora_sensor_ids, 
                          sizeof(uint32_t) * config->lora_sensor_count);
        if (err != ESP_OK) {
            ESP_LOGE(PROV_TAG, "Failed to save LoRa IDs");
            success = false;
            goto cleanup;
        }
    }

    // Save BLE leak sensor count
    err = nvs_set_u8(nvs_handle, NVS_KEY_LEAK_COUNT, config->ble_leak_sensor_count);
    if (err != ESP_OK) {
        ESP_LOGE(PROV_TAG, "Failed to save leak count");
        success = false;
        goto cleanup;
    }

    // Save BLE leak sensor MACs
    if (config->ble_leak_sensor_count > 0) {
        err = nvs_set_blob(nvs_handle, NVS_KEY_LEAK_MACS, 
                          config->ble_leak_sensors, 
                          18 * config->ble_leak_sensor_count);
        if (err != ESP_OK) {
            ESP_LOGE(PROV_TAG, "Failed to save leak MACs");
            success = false;
            goto cleanup;
        }
    }

    // Commit
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(PROV_TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        success = false;
    } else {
        ESP_LOGI(PROV_TAG, "Config saved to NVS successfully");
    }

cleanup:
    nvs_close(nvs_handle);
    return success;
}

static bool validate_mac_string(const char *mac_str)
{
    if (!mac_str) return false;
    
    // Expected format: "XX:XX:XX:XX:XX:XX" (17 chars)
    if (strlen(mac_str) != 17) return false;

    for (int i = 0; i < 17; i++) {
        if (i % 3 == 2) {
            if (mac_str[i] != ':') return false;
        } else {
            char c = mac_str[i];
            if (!((c >= '0' && c <= '9') || 
                  (c >= 'A' && c <= 'F') || 
                  (c >= 'a' && c <= 'f'))) {
                return false;
            }
        }
    }
    return true;
}

static bool parse_hex_id(const char *hex_str, uint32_t *out_id)
{
    if (!hex_str || !out_id) return false;

    // Expected format: "0xXXXXXXXX" or "0XXXXXXXXX"
    if (strlen(hex_str) < 3) return false;
    
    if (hex_str[0] == '0' && (hex_str[1] == 'x' || hex_str[1] == 'X')) {
        // Parse hex string
        unsigned long val = strtoul(hex_str + 2, NULL, 16);
        *out_id = (uint32_t)val;
        return true;
    }
    
    return false;
}

bool provisioning_handle_azure_payload_json(const char *json, size_t len)
{
    if (!json || len == 0) {
        ESP_LOGE(PROV_TAG, "Invalid JSON input");
        return false;
    }

    if (!g_initialized || g_prov_mutex == NULL) {
        ESP_LOGE(PROV_TAG, "Provisioning manager not initialized");
        return false;
    }

    ESP_LOGI(PROV_TAG, "Handling provisioning JSON (%d bytes)", len);

    // Create null-terminated copy for cJSON
    char *json_copy = (char *)malloc(len + 1);
    if (!json_copy) {
        ESP_LOGE(PROV_TAG, "Failed to allocate memory for JSON");
        return false;
    }
    memcpy(json_copy, json, len);
    json_copy[len] = '\0';

    ESP_LOGI(PROV_TAG, "Provisioning JSON: %s", json_copy);

    cJSON *root = cJSON_Parse(json_copy);
    free(json_copy);

    if (!root) {
        ESP_LOGE(PROV_TAG, "Failed to parse JSON");
        return false;
    }

    // Acquire mutex for thread-safe config update
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(PROV_TAG, "Failed to acquire mutex for provisioning update");
        cJSON_Delete(root);
        return false;
    }

    provisioning_config_t new_config = g_config; // Start with current config
    bool has_updates = false;

    // Parse valve_mac
    cJSON *valve_mac_json = cJSON_GetObjectItem(root, "valve_mac");
    if (valve_mac_json && cJSON_IsString(valve_mac_json)) {
        const char *mac_str = valve_mac_json->valuestring;
        if (validate_mac_string(mac_str)) {
            strncpy(new_config.valve_mac, mac_str, sizeof(new_config.valve_mac) - 1);
            new_config.valve_mac[sizeof(new_config.valve_mac) - 1] = '\0';
            ESP_LOGI(PROV_TAG, "Valve MAC: %s", new_config.valve_mac);
            has_updates = true;
        } else {
            ESP_LOGE(PROV_TAG, "Invalid valve MAC format: %s", mac_str);
            xSemaphoreGive(g_prov_mutex);
            cJSON_Delete(root);
            return false;
        }
    }

    // Parse lora_sensors
    cJSON *lora_sensors_json = cJSON_GetObjectItem(root, "lora_sensors");
    if (lora_sensors_json && cJSON_IsArray(lora_sensors_json)) {
        int array_size = cJSON_GetArraySize(lora_sensors_json);
        if (array_size > MAX_LORA_SENSORS) {
            ESP_LOGW(PROV_TAG, "Too many LoRa sensors (%d), limiting to %d", 
                     array_size, MAX_LORA_SENSORS);
            array_size = MAX_LORA_SENSORS;
        }

        new_config.lora_sensor_count = 0;
        for (int i = 0; i < array_size; i++) {
            cJSON *sensor = cJSON_GetArrayItem(lora_sensors_json, i);
            if (cJSON_IsString(sensor)) {
                uint32_t sensor_id;
                if (parse_hex_id(sensor->valuestring, &sensor_id)) {
                    new_config.lora_sensor_ids[new_config.lora_sensor_count++] = sensor_id;
                    ESP_LOGI(PROV_TAG, "LoRa Sensor[%d]: 0x%08lX", 
                             new_config.lora_sensor_count - 1, sensor_id);
                } else {
                    ESP_LOGW(PROV_TAG, "Invalid LoRa sensor ID format: %s", 
                             sensor->valuestring);
                }
            }
        }
        has_updates = true;
    }

    // Parse ble_leak_sensors
    cJSON *ble_leak_json = cJSON_GetObjectItem(root, "ble_leak_sensors");
    if (ble_leak_json && cJSON_IsArray(ble_leak_json)) {
        int array_size = cJSON_GetArraySize(ble_leak_json);
        if (array_size > MAX_BLE_LEAK_SENSORS) {
            ESP_LOGW(PROV_TAG, "Too many BLE leak sensors (%d), limiting to %d", 
                     array_size, MAX_BLE_LEAK_SENSORS);
            array_size = MAX_BLE_LEAK_SENSORS;
        }

        new_config.ble_leak_sensor_count = 0;
        for (int i = 0; i < array_size; i++) {
            cJSON *sensor = cJSON_GetArrayItem(ble_leak_json, i);
            if (cJSON_IsString(sensor)) {
                const char *mac_str = sensor->valuestring;
                if (validate_mac_string(mac_str)) {
                    strncpy(new_config.ble_leak_sensors[new_config.ble_leak_sensor_count], 
                           mac_str, 18);
                    new_config.ble_leak_sensors[new_config.ble_leak_sensor_count][17] = '\0';
                    ESP_LOGI(PROV_TAG, "BLE Leak Sensor[%d]: %s", 
                             new_config.ble_leak_sensor_count, 
                             new_config.ble_leak_sensors[new_config.ble_leak_sensor_count]);
                    new_config.ble_leak_sensor_count++;
                } else {
                    ESP_LOGW(PROV_TAG, "Invalid BLE leak sensor MAC format: %s", mac_str);
                }
            }
        }
        has_updates = true;
    }

    cJSON_Delete(root);

    if (!has_updates) {
        ESP_LOGW(PROV_TAG, "No valid provisioning data in JSON");
        xSemaphoreGive(g_prov_mutex);
        return false;
    }

    // Mark as provisioned
    new_config.state = PROV_STATE_PROVISIONED;
    new_config.config_version = CURRENT_CONFIG_VERSION;

    // Save to NVS (NVS operations are already thread-safe)
    if (!provisioning_save_to_nvs(&new_config)) {
        ESP_LOGE(PROV_TAG, "Failed to save provisioning data to NVS");
        xSemaphoreGive(g_prov_mutex);
        return false;
    }

    // Update global config
    memcpy(&g_config, &new_config, sizeof(provisioning_config_t));

    xSemaphoreGive(g_prov_mutex);

    ESP_LOGI(PROV_TAG, "Provisioning completed successfully!");
    ESP_LOGI(PROV_TAG, "State: PROVISIONED");
    ESP_LOGI(PROV_TAG, "Valve MAC: %s", g_config.valve_mac);
    ESP_LOGI(PROV_TAG, "LoRa sensors: %d", g_config.lora_sensor_count);
    ESP_LOGI(PROV_TAG, "BLE leak sensors: %d", g_config.ble_leak_sensor_count);

    return true;
}

bool provisioning_get_valve_mac(char *mac_out)
{
    if (!mac_out || !g_initialized || g_prov_mutex == NULL) {
        return false;
    }
    
    bool result = false;
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (g_config.state == PROV_STATE_PROVISIONED && g_config.valve_mac[0] != '\0') {
            strcpy(mac_out, g_config.valve_mac);
            result = true;
        }
        xSemaphoreGive(g_prov_mutex);
    } else {
        ESP_LOGW(PROV_TAG, "Failed to take mutex in get_valve_mac");
    }
    
    return result;
}

bool provisioning_is_lora_sensor_provisioned(uint32_t sensor_id)
{
    if (!g_initialized || g_prov_mutex == NULL) {
        return false;
    }
    
    bool result = false;
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (g_config.state == PROV_STATE_PROVISIONED) {
            for (int i = 0; i < g_config.lora_sensor_count; i++) {
                if (g_config.lora_sensor_ids[i] == sensor_id) {
                    result = true;
                    break;
                }
            }
        }
        xSemaphoreGive(g_prov_mutex);
    } else {
        ESP_LOGW(PROV_TAG, "Failed to take mutex in is_lora_sensor_provisioned");
    }
    
    return result;
}

bool provisioning_get_lora_sensors(uint32_t *ids_out, uint8_t *count_out)
{
    if (!ids_out || !count_out || !g_initialized || g_prov_mutex == NULL) {
        return false;
    }
    
    bool result = false;
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (g_config.state == PROV_STATE_PROVISIONED && g_config.lora_sensor_count > 0) {
            memcpy(ids_out, g_config.lora_sensor_ids, 
                   sizeof(uint32_t) * g_config.lora_sensor_count);
            *count_out = g_config.lora_sensor_count;
            result = true;
        } else {
            *count_out = 0;
        }
        xSemaphoreGive(g_prov_mutex);
    } else {
        ESP_LOGW(PROV_TAG, "Failed to take mutex in get_lora_sensors");
        *count_out = 0;
    }
    
    return result;
}

bool provisioning_get_ble_leak_sensors(char macs_out[][18], uint8_t *count_out)
{
    if (!macs_out || !count_out || !g_initialized || g_prov_mutex == NULL) {
        return false;
    }
    
    bool result = false;
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (g_config.state == PROV_STATE_PROVISIONED && g_config.ble_leak_sensor_count > 0) {
            for (int i = 0; i < g_config.ble_leak_sensor_count; i++) {
                strncpy(macs_out[i], g_config.ble_leak_sensors[i], 18);
            }
            *count_out = g_config.ble_leak_sensor_count;
            result = true;
        } else {
            *count_out = 0;
        }
        xSemaphoreGive(g_prov_mutex);
    } else {
        ESP_LOGW(PROV_TAG, "Failed to take mutex in get_ble_leak_sensors");
        *count_out = 0;
    }
    
    return result;
}
