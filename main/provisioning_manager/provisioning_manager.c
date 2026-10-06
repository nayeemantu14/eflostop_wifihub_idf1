#include "provisioning_manager.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "nvs_store/nvs_store.h"
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
#define NVS_KEY_RULES_EN "rules_en"
#define NVS_KEY_RULES_TRIG "rules_trig"

#define CURRENT_CONFIG_VERSION 2

static provisioning_config_t g_config = {0};
static bool g_initialized = false;
static SemaphoreHandle_t g_prov_mutex = NULL;

// Forward declaration
static bool validate_mac_string(const char *mac_str);
static bool parse_hex_id(const char *hex_str, uint32_t *out_id);
static bool should_remain_provisioned(const provisioning_config_t *config);

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

    // NVS (default + nvs_prov) is initialized in app_main via nvs_flash_init() +
    // nvs_store_init(); do not re-init or erase here. Commissioning lives in the
    // dedicated NVS_PROV_PARTITION so a WiFi reset / default-partition wipe can't touch it.

    // Try to load existing config
    memset(&g_config, 0, sizeof(g_config));
    g_config.config_version = CURRENT_CONFIG_VERSION;
    g_config.state = PROV_STATE_UNPROVISIONED;
    g_config.rules.auto_close_enabled = true;
    g_config.rules.trigger_mask = RULES_TRIGGER_ALL;

    if (provisioning_load_from_nvs(&g_config)) {
        ESP_LOGI(PROV_TAG, "Loaded existing config from NVS");
        ESP_LOGI(PROV_TAG, "State: %s", 
                 g_config.state == PROV_STATE_PROVISIONED ? "PROVISIONED" : "UNPROVISIONED");
        if (g_config.state == PROV_STATE_PROVISIONED) {
            ESP_LOGI(PROV_TAG, "Valve MAC: %s", g_config.valve_mac);
            ESP_LOGI(PROV_TAG, "LoRa sensors: %d", g_config.lora_sensor_count);
            ESP_LOGI(PROV_TAG, "BLE leak sensors: %d", g_config.ble_leak_sensor_count);
        }
        ESP_LOGI(PROV_TAG, "Rules: auto_close=%s triggers=0x%02X",
                 g_config.rules.auto_close_enabled ? "enabled" : "disabled",
                 g_config.rules.trigger_mask);
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
    esp_err_t err = nvs_open_from_partition(NVS_PROV_PARTITION, NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
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
    // A corrupt count past the array would overrun every loop over the list (load, remove,
    // rollback). Clamped, never reset: the ids that do fit still load.
    if (config->lora_sensor_count > MAX_LORA_SENSORS) {
        ESP_LOGE(PROV_TAG, "LoRa sensor count %u in NVS exceeds %d - clamped",
                 (unsigned)config->lora_sensor_count, MAX_LORA_SENSORS);
        config->lora_sensor_count = MAX_LORA_SENSORS;
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
    // Same clamp as the LoRa count above.
    if (config->ble_leak_sensor_count > MAX_BLE_LEAK_SENSORS) {
        ESP_LOGE(PROV_TAG, "BLE leak sensor count %u in NVS exceeds %d - clamped",
                 (unsigned)config->ble_leak_sensor_count, MAX_BLE_LEAK_SENSORS);
        config->ble_leak_sensor_count = MAX_BLE_LEAK_SENSORS;
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

    // Load rules config (backward compatible — defaults if missing)
    {
        uint8_t rules_en = 1;
        err = nvs_get_u8(nvs_handle, NVS_KEY_RULES_EN, &rules_en);
        config->rules.auto_close_enabled = (err == ESP_OK) ? (rules_en != 0) : true;

        uint8_t rules_trig = RULES_TRIGGER_ALL;
        err = nvs_get_u8(nvs_handle, NVS_KEY_RULES_TRIG, &rules_trig);
        config->rules.trigger_mask = (err == ESP_OK) ? rules_trig : RULES_TRIGGER_ALL;
    }

cleanup:
    // Every loaded string ends inside its buffer, on every exit path (a failed load keeps
    // what was read). A valid MAC is 17 chars, so this never shortens one.
    config->valve_mac[sizeof(config->valve_mac) - 1] = '\0';
    for (int i = 0; i < MAX_BLE_LEAK_SENSORS; i++) {
        config->ble_leak_sensors[i][sizeof(config->ble_leak_sensors[i]) - 1] = '\0';
    }
    nvs_close(nvs_handle);
    return success;
}

bool provisioning_save_to_nvs(const provisioning_config_t *config)
{
    if (!config) {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open_from_partition(NVS_PROV_PARTITION, NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
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

    // Save rules config
    err = nvs_set_u8(nvs_handle, NVS_KEY_RULES_EN, config->rules.auto_close_enabled ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(PROV_TAG, "Failed to save rules_en");
        success = false;
        goto cleanup;
    }
    err = nvs_set_u8(nvs_handle, NVS_KEY_RULES_TRIG, config->rules.trigger_mask);
    if (err != ESP_OK) {
        ESP_LOGE(PROV_TAG, "Failed to save rules_trig");
        success = false;
        goto cleanup;
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

// A failed save or erase can stop part-way, after some keys were already written or
// erased (NVS writes land at nvs_set_*, not at commit). Call with g_prov_mutex held and
// g_config back at the last good state: it is written out again so the next boot loads
// what RAM holds, not a mix of old and new keys (L13).
static void resave_after_failed_write(void)
{
    if (!provisioning_save_to_nvs(&g_config)) {
        ESP_LOGE(PROV_TAG, "Rewriting the previous config to NVS also failed - "
                           "NVS may not match RAM until the next successful save");
    }
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

bool provisioning_handle_azure_payload_json(const char *json, size_t len, bool *now_empty)
{
    if (now_empty) *now_empty = false;

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

    // Parse the valve identifier.
    //
    // Canonical key is "valve_id" — the same name the hub reports back on the
    // snapshot, the leak/valve events, the lifecycle message and twin reported,
    // so the app sends and receives one spelling.
    //
    // "valve_mac" is the pre-2.0.0 name, still accepted so an app or production
    // tool built against the old contract keeps commissioning. Remove the
    // fallback once both are updated. valve_id wins if a payload carries both.
    // Each key is validated on its own and the first USABLE one wins, rather than
    // the first one merely present. During the transition a backend may populate
    // both — sending valve_mac always and valve_id only once it knows it — and a
    // malformed valve_id must not discard a perfectly good valve_mac beside it.
    const char *valve_id_str  = NULL;
    bool        valve_key_seen = false;

    // NOTE the guard is cJSON_IsString, not mere presence. A non-string value —
    // in practice JSON null, which is what an app emits for "no valve in this
    // payload" when it serialises optional fields — must be SKIPPED, exactly as
    // it was before 2.0.0. Treating null as "a valve was offered" would fail the
    // whole provision and take the sensor arrays down with it.
    cJSON *vj = cJSON_GetObjectItem(root, "valve_id");
    if (vj && cJSON_IsString(vj)) {
        valve_key_seen = true;
        if (validate_mac_string(vj->valuestring))
            valve_id_str = vj->valuestring;
        else
            ESP_LOGW(PROV_TAG, "provision: 'valve_id' is not a valid MAC string");
    }
    if (!valve_id_str) {
        vj = cJSON_GetObjectItem(root, "valve_mac");
        if (vj && cJSON_IsString(vj)) {
            valve_key_seen = true;
            ESP_LOGW(PROV_TAG,
                     "provision: 'valve_mac' is deprecated — send 'valve_id'");
            if (validate_mac_string(vj->valuestring))
                valve_id_str = vj->valuestring;
            else
                ESP_LOGW(PROV_TAG, "provision: 'valve_mac' is not a valid MAC string");
        }
    }

    if (valve_id_str) {
        strncpy(new_config.valve_mac, valve_id_str, sizeof(new_config.valve_mac) - 1);
        new_config.valve_mac[sizeof(new_config.valve_mac) - 1] = '\0';
        ESP_LOGI(PROV_TAG, "Valve MAC: %s", new_config.valve_mac);
        has_updates = true;
    } else if (valve_key_seen) {
        // A valve key was offered AS A STRING and no spelling of it yielded a
        // usable MAC. Abort the whole provision, as before — silently keeping the
        // previously stored valve while acking "ok" would be worse than a visible
        // failure.
        ESP_LOGE(PROV_TAG, "No usable valve identifier in provision payload");
        xSemaphoreGive(g_prov_mutex);
        cJSON_Delete(root);
        return false;
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
                    // A repeated id would take two health slots, and the one that never
                    // hears a packet holds the hub RED for good (N13). Compared as parsed
                    // values, so "0x0000abcd" and "0xABCD" are the same sensor.
                    bool dup = false;
                    for (int k = 0; k < new_config.lora_sensor_count; k++) {
                        if (new_config.lora_sensor_ids[k] == sensor_id) {
                            dup = true;
                            break;
                        }
                    }
                    if (dup) {
                        ESP_LOGW(PROV_TAG, "LoRa sensor 0x%08lX duplicate in payload - ignored",
                                 (unsigned long)sensor_id);
                        continue;
                    }
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
                    // Same MAC twice, in any case mix, is one sensor (N13; see LoRa above).
                    bool dup = false;
                    for (int k = 0; k < new_config.ble_leak_sensor_count; k++) {
                        if (strcasecmp(new_config.ble_leak_sensors[k], mac_str) == 0) {
                            dup = true;
                            break;
                        }
                    }
                    if (dup) {
                        ESP_LOGW(PROV_TAG, "BLE leak sensor %s duplicate in payload - ignored",
                                 mac_str);
                        continue;
                    }
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

    // Sensor arrays that leave no device empty the hub just as the last removal does, and in
    // the same way (see mark_unprovisioned_if_empty()): decided in this mutex hold, with the
    // rules back to the defaults in this same save, so the reset is ordered before any later
    // provision or rules_config (E-05). Done BEFORE the auto-close and rules keys below, so
    // anything this payload sets for them still applies on top of the defaults. A provision
    // cannot clear the valve, so only a sensors-only hub gets here. An empty set always came
    // from an array above, so has_updates is already true. The state stays PROVISIONED.
    bool emptied = g_config.state == PROV_STATE_PROVISIONED &&
                   should_remain_provisioned(&g_config) &&
                   !should_remain_provisioned(&new_config);
    if (emptied) {
        new_config.rules.auto_close_enabled = true;
        new_config.rules.trigger_mask       = RULES_TRIGGER_ALL;
    }

    // Parse the setup-flow auto-close opt-in.
    //
    // Top-level "auto_close_enabled" is the COMMISSIONING form: the app asks the
    // user one yes/no question while adding the valve and sensors — "should a leak
    // shut the water off?" — and sends the answer in the same payload as the
    // devices it applies to.
    //
    // Opting IN also arms EVERY trigger source, not just the sensor ones. A user
    // who wants leaks to close the valve means all of them, including the valve's
    // own flood probe (bit 2) — the valve standing in water is the least ambiguous
    // leak there is. Per-source tuning stays available afterwards via rules_config.
    //
    // Opting OUT flips the master flag ONLY and leaves trigger_mask untouched.
    // rules_engine_evaluate_leak() tests auto_close_enabled BEFORE the mask, so the
    // mask is not consulted while the flag is false; clearing it would buy nothing
    // and would destroy a per-source selection the user gets back for free by
    // re-enabling.
    //
    // NOTE this deliberately differs from the same key in rules_config, which is a
    // pure master switch and never touches the mask. There the caller is editing
    // settings and says exactly what it wants; here it is answering a setup
    // question and expects the obvious whole-system behaviour.
    cJSON *auto_close_top = cJSON_GetObjectItem(root, "auto_close_enabled");
    if (auto_close_top && cJSON_IsBool(auto_close_top)) {
        new_config.rules.auto_close_enabled = cJSON_IsTrue(auto_close_top);
        if (new_config.rules.auto_close_enabled) {
            new_config.rules.trigger_mask = RULES_TRIGGER_ALL;
        }
        ESP_LOGI(PROV_TAG, "Auto-close opt-in: %s (triggers=0x%02X)",
                 new_config.rules.auto_close_enabled ? "ENABLED" : "disabled",
                 new_config.rules.trigger_mask);
        has_updates = true;
    } else if (auto_close_top) {
        // Same guard style as valve_id: a non-bool is ignored, not an error. In
        // practice this is JSON null from an app that serialises optional fields —
        // "the user was not asked", which must leave the stored setting alone
        // rather than silently disarming auto-close.
        ESP_LOGW(PROV_TAG,
                 "provision: 'auto_close_enabled' is not a JSON boolean — ignored");
    }

    // Parse optional rules config.
    //
    // Parsed AFTER the top-level flag on purpose: an explicit rules object is the
    // specific form and wins over the setup-flow shorthand, so a payload carrying
    // both {"auto_close_enabled":true} and {"rules":{"trigger_mask":3}} ends up
    // enabled with only the two sensor bits armed.
    cJSON *rules_json = cJSON_GetObjectItem(root, "rules");
    if (rules_json && cJSON_IsObject(rules_json)) {
        cJSON *auto_close = cJSON_GetObjectItem(rules_json, "auto_close_enabled");
        if (auto_close && cJSON_IsBool(auto_close)) {
            new_config.rules.auto_close_enabled = cJSON_IsTrue(auto_close);
        }
        cJSON *trigger_mask = cJSON_GetObjectItem(rules_json, "trigger_mask");
        if (trigger_mask && cJSON_IsNumber(trigger_mask)) {
            new_config.rules.trigger_mask = (uint8_t)trigger_mask->valueint;
        }
        ESP_LOGI(PROV_TAG, "Rules: auto_close=%s triggers=0x%02X",
                 new_config.rules.auto_close_enabled ? "enabled" : "disabled",
                 new_config.rules.trigger_mask);
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
        ESP_LOGE(PROV_TAG, "Failed to save provisioning data to NVS - previous config kept");
        resave_after_failed_write();   // g_config is still the previous config
        xSemaphoreGive(g_prov_mutex);
        return false;
    }

    // Update global config
    memcpy(&g_config, &new_config, sizeof(provisioning_config_t));

    xSemaphoreGive(g_prov_mutex);

    // Logged from the local copy: g_config may already be changing under another task.
    ESP_LOGI(PROV_TAG, "Provisioning completed successfully!");
    ESP_LOGI(PROV_TAG, "State: PROVISIONED");
    ESP_LOGI(PROV_TAG, "Valve MAC: %s", new_config.valve_mac);
    ESP_LOGI(PROV_TAG, "LoRa sensors: %d", new_config.lora_sensor_count);
    ESP_LOGI(PROV_TAG, "BLE leak sensors: %d", new_config.ble_leak_sensor_count);
    ESP_LOGI(PROV_TAG, "Auto-close: %s triggers=0x%02X",
             new_config.rules.auto_close_enabled ? "enabled" : "disabled",
             new_config.rules.trigger_mask);
    if (emptied) ESP_LOGI(PROV_TAG, "Hub empty: rules config reset to defaults");

    if (now_empty) *now_empty = emptied;
    return true;
}

bool provisioning_decommission(void)
{
    if (!g_initialized || g_prov_mutex == NULL) {
        ESP_LOGE(PROV_TAG, "Provisioning manager not initialized");
        return false;
    }

    ESP_LOGW(PROV_TAG, "=== DECOMMISSIONING DEVICE ===");

    // Acquire mutex for thread-safe access. Held across the NVS erase + commit, and RAM is
    // cleared only once both succeeded: clearing RAM first and then failing the erase left
    // a hub running as empty that came back provisioned on the next boot (L13).
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(PROV_TAG, "Failed to acquire mutex for decommissioning");
        return false;
    }

    // Erase from NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open_from_partition(NVS_PROV_PARTITION, NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(PROV_TAG, "Failed to open NVS for erase: %s - still provisioned",
                 esp_err_to_name(err));
        xSemaphoreGive(g_prov_mutex);
        return false;
    }

    // Erase all keys in the namespace
    err = nvs_erase_all(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(PROV_TAG, "Failed to erase NVS: %s - still provisioned", esp_err_to_name(err));
    } else {
        // Commit the erase
        err = nvs_commit(nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(PROV_TAG, "Failed to commit NVS erase: %s - still provisioned",
                     esp_err_to_name(err));
        }
    }
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        resave_after_failed_write();   // the erase may have removed some keys; RAM is intact
        xSemaphoreGive(g_prov_mutex);
        return false;
    }

    // Clear in-memory config
    memset(&g_config, 0, sizeof(provisioning_config_t));
    g_config.config_version = CURRENT_CONFIG_VERSION;
    g_config.state = PROV_STATE_UNPROVISIONED;
    g_config.rules.auto_close_enabled = true;
    g_config.rules.trigger_mask = RULES_TRIGGER_ALL;

    xSemaphoreGive(g_prov_mutex);

    ESP_LOGI(PROV_TAG, "Decommissioning successful!");
    ESP_LOGI(PROV_TAG, "Device state: UNPROVISIONED");
    ESP_LOGI(PROV_TAG, "All provisioning data erased from NVS");
    
    return true;
}

// Canonical MAC rendering on the wire is UPPERCASE colon-separated, matching what the
// radio path already produces ("%02X" in app_ble_leak.c mac_bytes_to_str and in
// app_ble_valve.c for g_valve_mac). A PROVISIONED mac arrives in whatever case the
// cloud sent it, and it reaches the wire by a different route — the health table, and
// from there the snapshot device arrays, health events, the lifecycle message and twin
// reported. Before this, a sensor commissioned in lower case appeared as
// "00:80:e1:2a:3b:00" in every snapshot and "00:80:E1:2A:3B:00" in every event: one
// physical device under two values of data.sensor_id, which is the key the cloud joins
// on. Confirmed on a real capture, 26 lower-case vs 11 upper-case occurrences.
//
// Normalising on READ rather than on store also repairs hubs already commissioned with
// a lower-case MAC, with no NVS migration. Safe: every lookup against these strings is
// strcasecmp (health_engine find_device, sensor_meta_get, the connected-MAC gate in
// app_iothub) or an sscanf "%02X" parse (the BLE whitelist), which accept either case.
// The rules engine's three case-sensitive strcmp calls compare tracking ids that never
// originate here.
static void mac_normalize_upper(char *s)
{
    if (!s) return;
    for (; *s; s++)
        *s = (char)toupper((unsigned char)*s);
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
            mac_normalize_upper(mac_out);
            result = true;
        }
        xSemaphoreGive(g_prov_mutex);
    } else {
        ESP_LOGW(PROV_TAG, "Failed to take mutex in get_valve_mac");
    }
    
    return result;
}

// The sensor lists, with g_prov_mutex held; the hub's state is the caller's to check. One
// definition each for the membership reads below and provisioning_get_rules_and_state().
static bool lora_listed_locked(uint32_t sensor_id)
{
    for (int i = 0; i < g_config.lora_sensor_count && i < MAX_LORA_SENSORS; i++) {
        if (g_config.lora_sensor_ids[i] == sensor_id) {
            return true;
        }
    }
    return false;
}

static bool ble_listed_locked(const char *mac)
{
    for (int i = 0; i < g_config.ble_leak_sensor_count && i < MAX_BLE_LEAK_SENSORS; i++) {
        if (strcasecmp(g_config.ble_leak_sensors[i], mac) == 0) {
            return true;
        }
    }
    return false;
}

// UNKNOWN is logged here, distinctly, because the caller acts on it as if provisioned: a
// packet from a sensor that is really gone would otherwise be handled with no trace of why.
prov_member_t provisioning_lora_sensor_membership(uint32_t sensor_id)
{
    if (!g_initialized || g_prov_mutex == NULL) {
        ESP_LOGW(PROV_TAG, "LoRa 0x%08lX membership UNKNOWN: provisioning not initialised",
                 (unsigned long)sensor_id);
        return PROV_MEMBER_UNKNOWN;
    }

    prov_member_t result = PROV_MEMBER_NO;
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (g_config.state == PROV_STATE_PROVISIONED && lora_listed_locked(sensor_id)) {
            result = PROV_MEMBER_YES;
        }
        xSemaphoreGive(g_prov_mutex);
    } else {
        ESP_LOGW(PROV_TAG, "LoRa 0x%08lX membership UNKNOWN: provisioning mutex timeout",
                 (unsigned long)sensor_id);
        result = PROV_MEMBER_UNKNOWN;
    }

    return result;
}

bool provisioning_is_lora_sensor_provisioned(uint32_t sensor_id)
{
    return provisioning_lora_sensor_membership(sensor_id) == PROV_MEMBER_YES;
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
                macs_out[i][17] = '\0';
                mac_normalize_upper(macs_out[i]);
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

bool provisioning_get_device_set(prov_device_set_t *out)
{
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    if (!g_initialized || g_prov_mutex == NULL) {
        return false;
    }

    // ONE hold for all three lists, so a reconcile can never see a valve from before a
    // provision next to sensor lists from after it.
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(PROV_TAG, "Failed to take mutex in get_device_set");
        return false;
    }

    if (g_config.state == PROV_STATE_PROVISIONED) {
        if (g_config.valve_mac[0] != '\0') {
            out->has_valve = true;
            memcpy(out->valve_mac, g_config.valve_mac, sizeof(out->valve_mac));
            out->valve_mac[sizeof(out->valve_mac) - 1] = '\0';
            mac_normalize_upper(out->valve_mac);
        }

        uint8_t lc = g_config.lora_sensor_count;
        if (lc > MAX_LORA_SENSORS) lc = MAX_LORA_SENSORS;
        memcpy(out->lora_ids, g_config.lora_sensor_ids, sizeof(uint32_t) * lc);
        out->lora_count = lc;

        uint8_t bc = g_config.ble_leak_sensor_count;
        if (bc > MAX_BLE_LEAK_SENSORS) bc = MAX_BLE_LEAK_SENSORS;
        for (int i = 0; i < bc; i++) {
            memcpy(out->ble_macs[i], g_config.ble_leak_sensors[i], sizeof(out->ble_macs[i]));
            out->ble_macs[i][17] = '\0';
            mac_normalize_upper(out->ble_macs[i]);
        }
        out->ble_count = bc;
    }

    xSemaphoreGive(g_prov_mutex);
    return true;
}

bool provisioning_get_summary(prov_summary_t *out)
{
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!g_initialized || g_prov_mutex == NULL) {
        return false;
    }

    // Silent on a timeout: the caller says what it does instead.
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    // Each value as its own getter reads it (provisioning_get_valve_mac(),
    // provisioning_get_lora_sensors(), provisioning_get_ble_leak_sensors(),
    // provisioning_get_rules_config()), counts clamped as provisioning_get_device_set() does.
    out->provisioned = (g_config.state == PROV_STATE_PROVISIONED);
    if (out->provisioned) {
        if (g_config.valve_mac[0] != '\0') {
            memcpy(out->valve_mac, g_config.valve_mac, sizeof(out->valve_mac));
            out->valve_mac[sizeof(out->valve_mac) - 1] = '\0';
            mac_normalize_upper(out->valve_mac);
        }
        out->lora_count = (g_config.lora_sensor_count > MAX_LORA_SENSORS)
                          ? MAX_LORA_SENSORS : g_config.lora_sensor_count;
        out->ble_count  = (g_config.ble_leak_sensor_count > MAX_BLE_LEAK_SENSORS)
                          ? MAX_BLE_LEAK_SENSORS : g_config.ble_leak_sensor_count;
    }
    out->rules = g_config.rules;
    xSemaphoreGive(g_prov_mutex);
    return true;
}

bool provisioning_with_valve_target(prov_valve_target_cb_t cb, void *ctx)
{
    if (!cb || !g_initialized || g_prov_mutex == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(PROV_TAG, "Failed to take mutex in with_valve_target");
        return false;
    }

    // Read as provisioning_get_device_set() does, trimmed to what the BLE target needs
    // (18 B on the caller's stack instead of the ~376 B set).
    char    mac[sizeof(g_config.valve_mac)] = {0};
    bool    has_valve = false;
    uint8_t bc = 0;
    if (g_config.state == PROV_STATE_PROVISIONED) {
        if (g_config.valve_mac[0] != '\0') {
            memcpy(mac, g_config.valve_mac, sizeof(mac));
            mac[sizeof(mac) - 1] = '\0';
            mac_normalize_upper(mac);
            has_valve = true;
        }
        bc = g_config.ble_leak_sensor_count;
        if (bc > MAX_BLE_LEAK_SENSORS) bc = MAX_BLE_LEAK_SENSORS;
    }

    // Still inside the hold: this is what makes the read and whatever cb applies one step.
    cb(has_valve ? mac : NULL, bc, ctx);

    xSemaphoreGive(g_prov_mutex);
    return true;
}

prov_member_t provisioning_ble_sensor_membership(const char *mac)
{
    if (!mac) {
        return PROV_MEMBER_NO;   // no address: nothing it could match
    }
    if (!g_initialized || g_prov_mutex == NULL) {
        ESP_LOGW(PROV_TAG, "BLE %s membership UNKNOWN: provisioning not initialised", mac);
        return PROV_MEMBER_UNKNOWN;
    }

    prov_member_t result = PROV_MEMBER_NO;
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (g_config.state == PROV_STATE_PROVISIONED && ble_listed_locked(mac)) {
            result = PROV_MEMBER_YES;
        }
        xSemaphoreGive(g_prov_mutex);
    } else {
        ESP_LOGW(PROV_TAG, "BLE %s membership UNKNOWN: provisioning mutex timeout", mac);
        result = PROV_MEMBER_UNKNOWN;
    }

    return result;
}

bool provisioning_is_ble_sensor_provisioned(const char *mac)
{
    return provisioning_ble_sensor_membership(mac) == PROV_MEMBER_YES;
}

// ============================================================================
// Selective Decommissioning Functions
// ============================================================================

/**
 * @brief Helper function to check if device should remain provisioned
 * 
 * Device stays provisioned if it has at least one device:
 * - Valve MAC is set, OR
 * - At least one LoRa sensor, OR
 * - At least one BLE leak sensor
 */
static bool should_remain_provisioned(const provisioning_config_t *config)
{
    return (config->valve_mac[0] != '\0' ||
            config->lora_sensor_count > 0 ||
            config->ble_leak_sensor_count > 0);
}

// A removal just changed g_config (g_prov_mutex held, not yet saved). When it left no device,
// the hub goes UNPROVISIONED and its rules go back to the provisioning defaults in that SAME
// save. Deciding "empty" and resetting the rules in the removal's own mutex hold orders the
// reset before any later provision or rules_config (esp-mqtt handles C2D one at a time); the
// reset used to run later on iothub_task from its own read, and could overwrite a just-acked
// opt-out or miss the edge entirely (E-05). No new keys: the save writes rules_en/rules_trig.
// A rules_config set on an already empty hub is not touched here. Returns true = emptied.
static bool mark_unprovisioned_if_empty(void)
{
    if (should_remain_provisioned(&g_config)) return false;
    g_config.state                    = PROV_STATE_UNPROVISIONED;
    g_config.rules.auto_close_enabled = true;
    g_config.rules.trigger_mask       = RULES_TRIGGER_ALL;
    ESP_LOGI(PROV_TAG, "No devices remain - state changed to UNPROVISIONED");
    return true;
}

bool provisioning_remove_valve(bool *now_empty)
{
    if (now_empty) *now_empty = false;

    if (!g_initialized || g_prov_mutex == NULL) {
        ESP_LOGE(PROV_TAG, "Provisioning manager not initialized");
        return false;
    }

    ESP_LOGW(PROV_TAG, "=== REMOVING VALVE ===");

    // Acquire mutex for thread-safe access
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(PROV_TAG, "Failed to acquire mutex");
        return false;
    }

    // Same test as provisioning_get_valve_mac(). Removing a valve that is not there used
    // to rewrite NVS and ack "ok" (F23).
    if (g_config.state != PROV_STATE_PROVISIONED || g_config.valve_mac[0] == '\0') {
        xSemaphoreGive(g_prov_mutex);
        ESP_LOGW(PROV_TAG, "No valve provisioned - nothing to remove");
        return false;
    }

    // Only these fields change. Kept (not the whole ~384 B config) so a failed save
    // can put RAM back (L13).
    char old_valve_mac[sizeof(g_config.valve_mac)];
    memcpy(old_valve_mac, g_config.valve_mac, sizeof(old_valve_mac));
    provisioning_state_t old_state = g_config.state;
    rules_config_t old_rules = g_config.rules;

    // Clear valve MAC
    memset(g_config.valve_mac, 0, sizeof(g_config.valve_mac));
    
    // Check if device should stay provisioned
    bool emptied = mark_unprovisioned_if_empty();

    // Save updated config to NVS
    bool save_result = provisioning_save_to_nvs(&g_config);
    if (!save_result) {
        memcpy(g_config.valve_mac, old_valve_mac, sizeof(g_config.valve_mac));
        g_config.state = old_state;
        g_config.rules = old_rules;
        ESP_LOGE(PROV_TAG, "Failed to save updated config to NVS - valve removal rolled back");
        resave_after_failed_write();
    }
    provisioning_state_t state_now = g_config.state;

    xSemaphoreGive(g_prov_mutex);

    if (save_result) {
        ESP_LOGI(PROV_TAG, "Valve removed successfully");
        ESP_LOGI(PROV_TAG, "State: %s", 
                 state_now == PROV_STATE_PROVISIONED ? "PROVISIONED" : "UNPROVISIONED");
        if (emptied) ESP_LOGI(PROV_TAG, "Hub empty: rules config reset to defaults");
    }

    if (now_empty) *now_empty = save_result && emptied;
    return save_result;
}

bool provisioning_remove_lora_sensor(uint32_t sensor_id, bool *now_empty)
{
    if (now_empty) *now_empty = false;

    if (!g_initialized || g_prov_mutex == NULL) {
        ESP_LOGE(PROV_TAG, "Provisioning manager not initialized");
        return false;
    }

    ESP_LOGW(PROV_TAG, "=== REMOVING LORA SENSOR 0x%08lX ===", sensor_id);

    // Acquire mutex for thread-safe access
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(PROV_TAG, "Failed to acquire mutex");
        return false;
    }

    // Find and remove the sensor
    int idx = -1;
    for (int i = 0; i < g_config.lora_sensor_count; i++) {
        if (g_config.lora_sensor_ids[i] == sensor_id) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        ESP_LOGW(PROV_TAG, "Sensor 0x%08lX not found in provisioned list", sensor_id);
        xSemaphoreGive(g_prov_mutex);
        return false;
    }

    // Undo record for a failed save (L13): the slot index, the state and the rules. The id
    // itself is sensor_id, and the shift below keeps every other entry.
    provisioning_state_t old_state = g_config.state;
    rules_config_t old_rules = g_config.rules;

    // Shift remaining sensors down
    for (int j = idx; j < g_config.lora_sensor_count - 1; j++) {
        g_config.lora_sensor_ids[j] = g_config.lora_sensor_ids[j + 1];
    }
    g_config.lora_sensor_count--;

    // Check if device should stay provisioned
    bool emptied = mark_unprovisioned_if_empty();

    // Save updated config to NVS
    bool save_result = provisioning_save_to_nvs(&g_config);
    if (!save_result) {
        // Shift back up and put the id back in its old slot.
        for (int j = g_config.lora_sensor_count; j > idx; j--) {
            g_config.lora_sensor_ids[j] = g_config.lora_sensor_ids[j - 1];
        }
        g_config.lora_sensor_ids[idx] = sensor_id;
        g_config.lora_sensor_count++;
        g_config.state = old_state;
        g_config.rules = old_rules;
        ESP_LOGE(PROV_TAG, "Failed to save updated config to NVS - LoRa removal rolled back");
        resave_after_failed_write();
    }
    int remaining = g_config.lora_sensor_count;
    provisioning_state_t state_now = g_config.state;

    xSemaphoreGive(g_prov_mutex);

    if (save_result) {
        ESP_LOGI(PROV_TAG, "LoRa sensor 0x%08lX removed successfully", sensor_id);
        ESP_LOGI(PROV_TAG, "Remaining LoRa sensors: %d", remaining);
        ESP_LOGI(PROV_TAG, "State: %s", 
                 state_now == PROV_STATE_PROVISIONED ? "PROVISIONED" : "UNPROVISIONED");
        if (emptied) ESP_LOGI(PROV_TAG, "Hub empty: rules config reset to defaults");
    }

    if (now_empty) *now_empty = save_result && emptied;
    return save_result;
}

bool provisioning_remove_ble_sensor(const char *mac, bool *now_empty)
{
    if (now_empty) *now_empty = false;

    if (!mac || !g_initialized || g_prov_mutex == NULL) {
        ESP_LOGE(PROV_TAG, "Invalid parameters");
        return false;
    }

    if (!validate_mac_string(mac)) {
        ESP_LOGE(PROV_TAG, "Invalid MAC format: %s", mac);
        return false;
    }

    ESP_LOGW(PROV_TAG, "=== REMOVING BLE LEAK SENSOR %s ===", mac);

    // Acquire mutex for thread-safe access
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(PROV_TAG, "Failed to acquire mutex");
        return false;
    }

    // Find and remove the sensor
    int idx = -1;
    for (int i = 0; i < g_config.ble_leak_sensor_count; i++) {
        if (strcasecmp(g_config.ble_leak_sensors[i], mac) == 0) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        ESP_LOGW(PROV_TAG, "BLE sensor %s not found in provisioned list", mac);
        xSemaphoreGive(g_prov_mutex);
        return false;
    }

    // Undo record for a failed save (L13): the slot, the stored string (its case may
    // differ from `mac`), the state and the rules - ~24 B, not a copy of the 288 B MAC table.
    char removed_mac[sizeof(g_config.ble_leak_sensors[0])];
    memcpy(removed_mac, g_config.ble_leak_sensors[idx], sizeof(removed_mac));
    provisioning_state_t old_state = g_config.state;
    rules_config_t old_rules = g_config.rules;

    // Shift remaining sensors down
    for (int j = idx; j < g_config.ble_leak_sensor_count - 1; j++) {
        strncpy(g_config.ble_leak_sensors[j], g_config.ble_leak_sensors[j + 1], 18);
    }
    g_config.ble_leak_sensor_count--;

    // Check if device should stay provisioned
    bool emptied = mark_unprovisioned_if_empty();

    // Save updated config to NVS
    bool save_result = provisioning_save_to_nvs(&g_config);
    if (!save_result) {
        // Shift back up and put the MAC back in its old slot.
        for (int j = g_config.ble_leak_sensor_count; j > idx; j--) {
            memcpy(g_config.ble_leak_sensors[j], g_config.ble_leak_sensors[j - 1],
                   sizeof(g_config.ble_leak_sensors[j]));
        }
        memcpy(g_config.ble_leak_sensors[idx], removed_mac, sizeof(removed_mac));
        g_config.ble_leak_sensor_count++;
        g_config.state = old_state;
        g_config.rules = old_rules;
        ESP_LOGE(PROV_TAG, "Failed to save updated config to NVS - BLE removal rolled back");
        resave_after_failed_write();
    }
    int remaining = g_config.ble_leak_sensor_count;
    provisioning_state_t state_now = g_config.state;

    xSemaphoreGive(g_prov_mutex);

    if (save_result) {
        ESP_LOGI(PROV_TAG, "BLE leak sensor %s removed successfully", mac);
        ESP_LOGI(PROV_TAG, "Remaining BLE sensors: %d", remaining);
        ESP_LOGI(PROV_TAG, "State: %s", 
                 state_now == PROV_STATE_PROVISIONED ? "PROVISIONED" : "UNPROVISIONED");
        if (emptied) ESP_LOGI(PROV_TAG, "Hub empty: rules config reset to defaults");
    }

    if (now_empty) *now_empty = save_result && emptied;
    return save_result;
}

bool provisioning_add_lora_sensor(uint32_t sensor_id)
{
    if (!g_initialized || g_prov_mutex == NULL) {
        ESP_LOGE(PROV_TAG, "Provisioning manager not initialized");
        return false;
    }

    ESP_LOGI(PROV_TAG, "=== ADDING LORA SENSOR 0x%08lX ===", sensor_id);

    // Acquire mutex for thread-safe access
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(PROV_TAG, "Failed to acquire mutex");
        return false;
    }

    // Check if already exists
    for (int i = 0; i < g_config.lora_sensor_count; i++) {
        if (g_config.lora_sensor_ids[i] == sensor_id) {
            ESP_LOGW(PROV_TAG, "Sensor 0x%08lX already provisioned", sensor_id);
            xSemaphoreGive(g_prov_mutex);
            return true; // Not an error - already exists
        }
    }

    // Check capacity
    if (g_config.lora_sensor_count >= MAX_LORA_SENSORS) {
        ESP_LOGE(PROV_TAG, "Maximum LoRa sensors (%d) reached", MAX_LORA_SENSORS);
        xSemaphoreGive(g_prov_mutex);
        return false;
    }

    // Undo record for a failed save (L13)
    provisioning_state_t old_state = g_config.state;
    uint8_t old_version = g_config.config_version;

    // Add sensor
    g_config.lora_sensor_ids[g_config.lora_sensor_count++] = sensor_id;
    
    // Ensure device is provisioned
    if (g_config.state != PROV_STATE_PROVISIONED) {
        g_config.state = PROV_STATE_PROVISIONED;
        g_config.config_version = CURRENT_CONFIG_VERSION;
        ESP_LOGI(PROV_TAG, "State changed to PROVISIONED");
    }

    // Save updated config to NVS
    bool save_result = provisioning_save_to_nvs(&g_config);
    if (!save_result) {
        g_config.lora_sensor_count--;
        g_config.state = old_state;
        g_config.config_version = old_version;
        ESP_LOGE(PROV_TAG, "Failed to save updated config to NVS - LoRa add rolled back");
        resave_after_failed_write();
    }
    int total = g_config.lora_sensor_count;

    xSemaphoreGive(g_prov_mutex);

    if (save_result) {
        ESP_LOGI(PROV_TAG, "LoRa sensor 0x%08lX added successfully", sensor_id);
        ESP_LOGI(PROV_TAG, "Total LoRa sensors: %d", total);
    }

    return save_result;
}

bool provisioning_add_ble_sensor(const char *mac)
{
    if (!mac || !g_initialized || g_prov_mutex == NULL) {
        ESP_LOGE(PROV_TAG, "Invalid parameters");
        return false;
    }

    if (!validate_mac_string(mac)) {
        ESP_LOGE(PROV_TAG, "Invalid MAC format: %s", mac);
        return false;
    }

    ESP_LOGI(PROV_TAG, "=== ADDING BLE LEAK SENSOR %s ===", mac);

    // Acquire mutex for thread-safe access
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(PROV_TAG, "Failed to acquire mutex");
        return false;
    }

    // Check if already exists
    for (int i = 0; i < g_config.ble_leak_sensor_count; i++) {
        if (strcasecmp(g_config.ble_leak_sensors[i], mac) == 0) {
            ESP_LOGW(PROV_TAG, "BLE sensor %s already provisioned", mac);
            xSemaphoreGive(g_prov_mutex);
            return true; // Not an error - already exists
        }
    }

    // Check capacity
    if (g_config.ble_leak_sensor_count >= MAX_BLE_LEAK_SENSORS) {
        ESP_LOGE(PROV_TAG, "Maximum BLE sensors (%d) reached", MAX_BLE_LEAK_SENSORS);
        xSemaphoreGive(g_prov_mutex);
        return false;
    }

    // Undo record for a failed save (L13)
    provisioning_state_t old_state = g_config.state;
    uint8_t old_version = g_config.config_version;

    // Add sensor
    strncpy(g_config.ble_leak_sensors[g_config.ble_leak_sensor_count], mac, 18);
    g_config.ble_leak_sensors[g_config.ble_leak_sensor_count][17] = '\0';
    g_config.ble_leak_sensor_count++;
    
    // Ensure device is provisioned
    if (g_config.state != PROV_STATE_PROVISIONED) {
        g_config.state = PROV_STATE_PROVISIONED;
        g_config.config_version = CURRENT_CONFIG_VERSION;
        ESP_LOGI(PROV_TAG, "State changed to PROVISIONED");
    }

    // Save updated config to NVS
    bool save_result = provisioning_save_to_nvs(&g_config);
    if (!save_result) {
        g_config.ble_leak_sensor_count--;
        g_config.state = old_state;
        g_config.config_version = old_version;
        ESP_LOGE(PROV_TAG, "Failed to save updated config to NVS - BLE add rolled back");
        resave_after_failed_write();
    }
    int total = g_config.ble_leak_sensor_count;

    xSemaphoreGive(g_prov_mutex);

    if (save_result) {
        ESP_LOGI(PROV_TAG, "BLE leak sensor %s added successfully", mac);
        ESP_LOGI(PROV_TAG, "Total BLE sensors: %d", total);
    }

    return save_result;
}

bool provisioning_get_rules_config(rules_config_t *rules_out)
{
    if (!rules_out || !g_initialized || g_prov_mutex == NULL) {
        return false;
    }

    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        *rules_out = g_config.rules;
        xSemaphoreGive(g_prov_mutex);
        return true;
    }

    ESP_LOGW(PROV_TAG, "Failed to take mutex in get_rules_config");
    return false;
}

bool provisioning_get_rules_and_state(prov_dev_kind_t kind, const char *id,
                                      bool *provisioned, bool *member,
                                      rules_config_t *rules_out)
{
    if (!provisioned || !rules_out || !g_initialized || g_prov_mutex == NULL) {
        return false;
    }
    uint32_t lora_id = 0;
    bool id_ok = (kind != PROV_DEV_LORA) || parse_hex_id(id, &lora_id);   // before the hold

    // Silent on a timeout: the caller says what it does instead (once per busy episode).
    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    bool prov = (g_config.state == PROV_STATE_PROVISIONED);
    bool listed;
    switch (kind) {
        case PROV_DEV_VALVE: listed = (g_config.valve_mac[0] != '\0');           break;
        case PROV_DEV_LORA:  listed = id_ok && lora_listed_locked(lora_id);      break;
        case PROV_DEV_BLE:   listed = (id != NULL) && ble_listed_locked(id);     break;
        default:             listed = true;                                      break;
    }
    *provisioned = prov;
    if (member) {
        *member = prov && listed;
    }
    *rules_out = g_config.rules;
    xSemaphoreGive(g_prov_mutex);
    return true;
}

// Persist just the rules keys to NVS. A false can leave the first key already written.
static bool write_rules_keys(const rules_config_t *rules)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open_from_partition(NVS_PROV_PARTITION, NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(PROV_TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }

    bool ok = true;
    err = nvs_set_u8(nvs_handle, NVS_KEY_RULES_EN, rules->auto_close_enabled ? 1 : 0);
    if (err != ESP_OK) { ok = false; }
    err = nvs_set_u8(nvs_handle, NVS_KEY_RULES_TRIG, rules->trigger_mask);
    if (err != ESP_OK) { ok = false; }

    if (ok) {
        err = nvs_commit(nvs_handle);
        if (err != ESP_OK) { ok = false; }
    }

    nvs_close(nvs_handle);
    return ok;
}

bool provisioning_set_rules_config(const rules_config_t *rules)
{
    if (!rules || !g_initialized || g_prov_mutex == NULL) {
        return false;
    }

    ESP_LOGI(PROV_TAG, "Setting rules config: auto_close=%s triggers=0x%02X",
             rules->auto_close_enabled ? "enabled" : "disabled",
             rules->trigger_mask);

    if (xSemaphoreTake(g_prov_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(PROV_TAG, "Failed to acquire mutex for set_rules_config");
        return false;
    }

    // RAM takes the new rules only once NVS holds them, so a failed write can never leave
    // the engine running rules the next boot will not load (L13). On a failure the old
    // keys are written back, since the first of the two may already have landed.
    bool ok = write_rules_keys(rules);
    if (ok) {
        g_config.rules = *rules;
    } else {
        ESP_LOGE(PROV_TAG, "Failed to save rules config to NVS - previous rules kept");
        if (!write_rules_keys(&g_config.rules)) {
            ESP_LOGE(PROV_TAG, "Rewriting the previous rules to NVS also failed - "
                               "NVS may not match RAM until the next successful save");
        }
    }

    xSemaphoreGive(g_prov_mutex);

    if (ok) {
        ESP_LOGI(PROV_TAG, "Rules config saved to NVS");
    }

    return ok;
}