#include "sensor_meta.h"
#include <string.h>
#include <strings.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "nvs_store/nvs_store.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define META_TAG "SENSOR_META"
#define NVS_NAMESPACE "sen_meta"
#define NVS_KEY_VERSION "meta_ver"
#define NVS_KEY_TABLE "meta_tbl"
#define CURRENT_META_VERSION 1

static sensor_meta_entry_t s_table[MAX_SENSOR_META];
static uint8_t s_count = 0;
static SemaphoreHandle_t s_mutex = NULL;
static bool s_initialized = false;

static const char *s_location_strings[] = {
    "unknown", "bathroom", "kitchen", "laundry", "garage",
    "garden", "basement", "utility", "hallway",
    "bedroom", "living_room", "attic", "outdoor"
};
_Static_assert(sizeof(s_location_strings) / sizeof(s_location_strings[0]) == LOC_COUNT,
               "Location string table must match LOC_COUNT");

// ─── NVS helpers ────────────────────────────────────────────────────────────

static bool save_table_to_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PROV_PARTITION, NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(META_TAG, "NVS open failed: %s", esp_err_to_name(err));
        return false;
    }

    bool ok = true;

    uint8_t ver = CURRENT_META_VERSION;
    err = nvs_set_u8(h, NVS_KEY_VERSION, ver);
    if (err != ESP_OK) { ok = false; }

    // Store count + entries as one blob: [count(1)] [entries(count*sizeof)]
    size_t blob_size = 1 + (size_t)s_count * sizeof(sensor_meta_entry_t);
    uint8_t *blob = malloc(blob_size);
    if (!blob) {
        nvs_close(h);
        return false;
    }
    blob[0] = s_count;
    if (s_count > 0) {
        memcpy(blob + 1, s_table, s_count * sizeof(sensor_meta_entry_t));
    }

    err = nvs_set_blob(h, NVS_KEY_TABLE, blob, blob_size);
    free(blob);
    if (err != ESP_OK) { ok = false; }

    if (ok) {
        err = nvs_commit(h);
        if (err != ESP_OK) { ok = false; }
    }

    nvs_close(h);
    return ok;
}

static bool load_table_from_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(NVS_PROV_PARTITION, NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGD(META_TAG, "NVS namespace not found (first boot?)");
        return false;
    }

    uint8_t ver = 0;
    err = nvs_get_u8(h, NVS_KEY_VERSION, &ver);
    if (err != ESP_OK || ver == 0) {
        nvs_close(h);
        return false;
    }

    // Get blob size first
    size_t blob_size = 0;
    err = nvs_get_blob(h, NVS_KEY_TABLE, NULL, &blob_size);
    if (err != ESP_OK || blob_size < 1) {
        nvs_close(h);
        return false;
    }

    uint8_t *blob = malloc(blob_size);
    if (!blob) {
        nvs_close(h);
        return false;
    }

    err = nvs_get_blob(h, NVS_KEY_TABLE, blob, &blob_size);
    nvs_close(h);

    if (err != ESP_OK) {
        free(blob);
        return false;
    }

    uint8_t count = blob[0];
    if (count > MAX_SENSOR_META) {
        count = MAX_SENSOR_META;
    }

    size_t expected = 1 + (size_t)count * sizeof(sensor_meta_entry_t);
    if (blob_size < expected) {
        ESP_LOGW(META_TAG, "Blob size mismatch: got %d, expected %d", (int)blob_size, (int)expected);
        free(blob);
        return false;
    }

    s_count = count;
    if (s_count > 0) {
        memcpy(s_table, blob + 1, s_count * sizeof(sensor_meta_entry_t));
    }

    free(blob);
    ESP_LOGI(META_TAG, "Loaded %d sensor metadata entries from NVS", s_count);
    return true;
}

// ─── Public API ─────────────────────────────────────────────────────────────

bool sensor_meta_init(void)
{
    if (s_initialized) {
        return true;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(META_TAG, "Failed to create mutex");
        return false;
    }

    memset(s_table, 0, sizeof(s_table));
    s_count = 0;

    load_table_from_nvs();  // OK if it fails (empty table)

    s_initialized = true;
    ESP_LOGI(META_TAG, "Sensor metadata initialized (%d entries)", s_count);
    return true;
}

const sensor_meta_entry_t *sensor_meta_find(sensor_type_t type, const char *sensor_id)
{
    if (!sensor_id || !s_initialized) {
        return NULL;
    }

    const sensor_meta_entry_t *result = NULL;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        for (int i = 0; i < s_count; i++) {
            if (s_table[i].sensor_type == (uint8_t)type &&
                strcasecmp(s_table[i].sensor_id, sensor_id) == 0) {
                result = &s_table[i];
                break;
            }
        }
        xSemaphoreGive(s_mutex);
    }

    return result;
}

bool sensor_meta_set(sensor_type_t type, const char *sensor_id,
                     int location_code, const char *label)
{
    if (!sensor_id || !s_initialized) {
        return false;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(META_TAG, "Failed to acquire mutex");
        return false;
    }

    // Find existing entry
    sensor_meta_entry_t *entry = NULL;
    for (int i = 0; i < s_count; i++) {
        if (s_table[i].sensor_type == (uint8_t)type &&
            strcasecmp(s_table[i].sensor_id, sensor_id) == 0) {
            entry = &s_table[i];
            break;
        }
    }

    // Create new entry if not found
    if (!entry) {
        if (s_count >= MAX_SENSOR_META) {
            ESP_LOGE(META_TAG, "Table full (%d entries)", MAX_SENSOR_META);
            xSemaphoreGive(s_mutex);
            return false;
        }
        entry = &s_table[s_count++];
        memset(entry, 0, sizeof(*entry));
        entry->sensor_type = (uint8_t)type;
        strncpy(entry->sensor_id, sensor_id, SENSOR_META_ID_MAX - 1);
        entry->sensor_id[SENSOR_META_ID_MAX - 1] = '\0';
        entry->location_code = LOC_UNKNOWN;
    }

    // Merge: -1 means keep existing, NULL means keep existing
    if (location_code >= 0 && location_code < LOC_COUNT) {
        entry->location_code = (uint8_t)location_code;
    }
    if (label) {
        strncpy(entry->label, label, SENSOR_META_LABEL_MAX - 1);
        entry->label[SENSOR_META_LABEL_MAX - 1] = '\0';
    }

    bool ok = save_table_to_nvs();

    xSemaphoreGive(s_mutex);

    ESP_LOGI(META_TAG, "Set metadata: type=%d id=%s loc=%s label=\"%s\"",
             type, sensor_id,
             sensor_meta_location_code_to_str(entry->location_code),
             entry->label);

    return ok;
}

bool sensor_meta_remove(sensor_type_t type, const char *sensor_id)
{
    if (!sensor_id || !s_initialized) {
        return false;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(META_TAG, "Failed to acquire mutex");
        return false;
    }

    bool found = false;
    for (int i = 0; i < s_count; i++) {
        if (s_table[i].sensor_type == (uint8_t)type &&
            strcasecmp(s_table[i].sensor_id, sensor_id) == 0) {
            // Shift-compact
            for (int j = i; j < s_count - 1; j++) {
                s_table[j] = s_table[j + 1];
            }
            s_count--;
            memset(&s_table[s_count], 0, sizeof(sensor_meta_entry_t));
            found = true;
            break;
        }
    }

    bool ok = true;
    if (found) {
        ok = save_table_to_nvs();
        ESP_LOGI(META_TAG, "Removed metadata for %s (type=%d), %d entries remain",
                 sensor_id, type, s_count);
    } else {
        ESP_LOGD(META_TAG, "No metadata found for %s (type=%d)", sensor_id, type);
    }

    xSemaphoreGive(s_mutex);
    return ok;
}

// Parse + validate + apply ONE metadata object:
//   { "sensor_type":"ble"|"lora", "sensor_id":"...",
//     "location_code":"<name>" (opt), "label":"<text>" (opt) }
// SINGLE shared apply primitive used by BOTH the standalone `sensor_meta` command
// (rename) and the inline `sensor_meta[]` array carried in a `provision` command.
// Operates on a cJSON object (not a string) so there is exactly one
// parse/validate/apply path — no duplicated parsing between the two entry points.
static bool apply_one_meta_obj(const cJSON *obj)
{
    if (!cJSON_IsObject(obj)) {
        ESP_LOGE(META_TAG, "meta entry is not an object");
        return false;
    }

    // sensor_type: "ble" or "lora" (required)
    cJSON *type_json = cJSON_GetObjectItem(obj, "sensor_type");
    if (!cJSON_IsString(type_json)) {
        ESP_LOGE(META_TAG, "Missing sensor_type");
        return false;
    }
    sensor_type_t type;
    if (strcasecmp(type_json->valuestring, "ble") == 0) {
        type = SENSOR_TYPE_BLE_LEAK;
    } else if (strcasecmp(type_json->valuestring, "lora") == 0) {
        type = SENSOR_TYPE_LORA;
    } else {
        ESP_LOGE(META_TAG, "Unknown sensor_type: %s", type_json->valuestring);
        return false;
    }

    // sensor_id (required, non-empty). Matched case-insensitively downstream
    // (sensor_meta_set uses strcasecmp), so mixed-case cloud MACs still resolve.
    cJSON *id_json = cJSON_GetObjectItem(obj, "sensor_id");
    if (!cJSON_IsString(id_json) || id_json->valuestring[0] == '\0') {
        ESP_LOGE(META_TAG, "Missing sensor_id");
        return false;
    }

    // location_code (optional string). Distinguish three cases so a typo can't
    // silently wipe a stored location: absent -> keep existing (-1); a recognized
    // name (incl. an explicit "unknown") -> apply it; an UNRECOGNIZED string ->
    // keep existing + warn. Without this, location_code_from_str returns
    // LOC_UNKNOWN(0) for any unknown string and sensor_meta_set treats 0 as a
    // valid set, clobbering e.g. {loc=KITCHEN} on a label-only rename with a typo.
    int loc_code = -1;  // -1 = keep existing
    cJSON *loc_json = cJSON_GetObjectItem(obj, "location_code");
    if (cJSON_IsString(loc_json)) {
        location_code_t lc = sensor_meta_location_code_from_str(loc_json->valuestring);
        if (lc != LOC_UNKNOWN || strcasecmp(loc_json->valuestring, "unknown") == 0) {
            loc_code = (int)lc;   // recognized location (or explicit "unknown")
        } else {
            ESP_LOGW(META_TAG, "unrecognized location_code '%s' — keeping existing",
                     loc_json->valuestring);
        }
    }

    // label (optional string; NULL = keep existing). sensor_meta_set truncates
    // to SENSOR_META_LABEL_MAX, so an oversized label is safely clamped.
    const char *label = NULL;
    cJSON *label_json = cJSON_GetObjectItem(obj, "label");
    if (cJSON_IsString(label_json)) {
        label = label_json->valuestring;
    }

    return sensor_meta_set(type, id_json->valuestring, loc_code, label);
}

bool sensor_meta_handle_command(const char *json_str)
{
    if (!json_str || !s_initialized) {
        return false;
    }
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(META_TAG, "Failed to parse JSON");
        return false;
    }
    bool ok = apply_one_meta_obj(root);
    cJSON_Delete(root);
    return ok;
}

int sensor_meta_apply_array_from_payload(const char *payload_json)
{
    if (!payload_json || !s_initialized) {
        return 0;
    }
    cJSON *root = cJSON_Parse(payload_json);
    if (!root) {
        // The provision handler already validated this payload; if it somehow
        // doesn't parse here, there's simply no inline metadata to apply.
        return 0;
    }

    int applied = 0;
    cJSON *arr = cJSON_GetObjectItem(root, "sensor_meta");
    if (cJSON_IsArray(arr)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, arr) {
            if (apply_one_meta_obj(item)) {
                applied++;
            } else {
                ESP_LOGW(META_TAG, "inline sensor_meta entry rejected — skipped");
            }
        }
    }

    cJSON_Delete(root);
    return applied;
}

void sensor_meta_clear_all(void)
{
    if (!s_initialized) {
        return;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        memset(s_table, 0, sizeof(s_table));
        s_count = 0;

        // Erase NVS
        nvs_handle_t h;
        esp_err_t err = nvs_open_from_partition(NVS_PROV_PARTITION, NVS_NAMESPACE, NVS_READWRITE, &h);
        if (err == ESP_OK) {
            nvs_erase_all(h);
            nvs_commit(h);
            nvs_close(h);
        }

        xSemaphoreGive(s_mutex);
        ESP_LOGI(META_TAG, "All sensor metadata cleared");
    }
}

const char *sensor_meta_location_code_to_str(location_code_t code)
{
    if (code >= LOC_COUNT) {
        return "unknown";
    }
    return s_location_strings[code];
}

location_code_t sensor_meta_location_code_from_str(const char *str)
{
    if (!str) {
        return LOC_UNKNOWN;
    }
    for (int i = 0; i < LOC_COUNT; i++) {
        if (strcasecmp(str, s_location_strings[i]) == 0) {
            return (location_code_t)i;
        }
    }
    return LOC_UNKNOWN;
}
