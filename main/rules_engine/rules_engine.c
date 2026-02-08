#include "rules_engine.h"
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "provisioning_manager.h"
#include "app_ble_valve.h"
#include "sensor_meta.h"

#define RULES_TAG "RULES_ENGINE"
#define AUTO_CLOSE_COOLDOWN_MS 10000   // 10s cooldown between auto-closes

static bool g_initialized = false;
static bool g_auto_close_triggered = false;
static bool g_leak_incident_active = false;  // Latched: stays true until explicit LEAK_RESET
static TickType_t g_last_auto_close_tick = 0;
static SemaphoreHandle_t g_mutex = NULL;

// Pending auto-close telemetry (built by rules engine, consumed by IoT Hub)
static char *g_pending_telemetry = NULL;

// ─── Helpers ────────────────────────────────────────────────────────────────

static uint8_t source_to_trigger_bit(leak_source_t source)
{
    switch (source) {
        case LEAK_SOURCE_BLE:         return RULES_TRIGGER_BLE_LEAK;
        case LEAK_SOURCE_LORA:        return RULES_TRIGGER_LORA;
        case LEAK_SOURCE_VALVE_FLOOD: return RULES_TRIGGER_VALVE_FLOOD;
        default:                      return 0;
    }
}

static const char *source_to_str(leak_source_t source)
{
    switch (source) {
        case LEAK_SOURCE_BLE:         return "ble_leak";
        case LEAK_SOURCE_LORA:        return "lora";
        case LEAK_SOURCE_VALVE_FLOOD: return "valve_flood";
        default:                      return "unknown";
    }
}

static sensor_type_t source_to_sensor_type(leak_source_t source)
{
    switch (source) {
        case LEAK_SOURCE_BLE:  return SENSOR_TYPE_BLE_LEAK;
        case LEAK_SOURCE_LORA: return SENSOR_TYPE_LORA;
        default:               return SENSOR_TYPE_BLE_LEAK;  // valve flood has no metadata
    }
}

static void build_auto_close_telemetry(leak_source_t source, const char *source_id)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddStringToObject(root, "event", "auto_close");
    cJSON_AddStringToObject(root, "trigger_source", source_to_str(source));
    cJSON_AddStringToObject(root, "trigger_sensor", source_id ? source_id : "unknown");
    cJSON_AddBoolToObject(root, "rmleak_asserted", true);

    // Add location if available
    if (source_id && source != LEAK_SOURCE_VALVE_FLOOD) {
        const sensor_meta_entry_t *meta = sensor_meta_find(
            source_to_sensor_type(source), source_id);
        if (meta) {
            cJSON *loc = cJSON_CreateObject();
            cJSON_AddStringToObject(loc, "code",
                sensor_meta_location_code_to_str(meta->location_code));
            cJSON_AddStringToObject(loc, "label", meta->label);
            cJSON_AddItemToObject(root, "location", loc);
        }
    }

    // Free any previous pending telemetry
    if (g_pending_telemetry) {
        free(g_pending_telemetry);
    }
    g_pending_telemetry = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
}

// ─── Public API ─────────────────────────────────────────────────────────────

void rules_engine_init(void)
{
    if (g_initialized) return;

    g_mutex = xSemaphoreCreateMutex();
    if (!g_mutex) {
        ESP_LOGE(RULES_TAG, "Failed to create mutex");
        return;
    }

    rules_config_t rules;
    if (provisioning_get_rules_config(&rules)) {
        ESP_LOGI(RULES_TAG, "Initialized: auto_close=%s triggers=0x%02X",
                 rules.auto_close_enabled ? "enabled" : "disabled",
                 rules.trigger_mask);
    } else {
        ESP_LOGI(RULES_TAG, "Initialized with defaults (auto_close=enabled triggers=ALL)");
    }

    g_initialized = true;
}

void rules_engine_evaluate_leak(leak_source_t source, bool leak_active, const char *source_id)
{
    if (!g_initialized) return;

    // Only act on leak-detected events, not leak-cleared
    if (!leak_active) return;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(RULES_TAG, "Failed to take mutex");
        return;
    }

    // Check if device is provisioned
    if (!provisioning_is_provisioned()) {
        xSemaphoreGive(g_mutex);
        return;
    }

    // Get current rules config
    rules_config_t rules;
    if (!provisioning_get_rules_config(&rules)) {
        xSemaphoreGive(g_mutex);
        return;
    }

    // Check master enable
    if (!rules.auto_close_enabled) {
        ESP_LOGD(RULES_TAG, "Auto-close disabled, ignoring leak from %s",
                 source_id ? source_id : "unknown");
        xSemaphoreGive(g_mutex);
        return;
    }

    // Check trigger mask
    uint8_t trigger_bit = source_to_trigger_bit(source);
    if (!(rules.trigger_mask & trigger_bit)) {
        ESP_LOGD(RULES_TAG, "Source %s not in trigger mask (0x%02X), ignoring",
                 source_to_str(source), rules.trigger_mask);
        xSemaphoreGive(g_mutex);
        return;
    }

    // Latch the leak incident (stays active until explicit LEAK_RESET)
    // This must happen even if the valve is already closed — we need RMLEAK asserted
    if (!g_leak_incident_active) {
        ESP_LOGW(RULES_TAG, "LEAK INCIDENT latched by %s sensor %s",
                 source_to_str(source), source_id ? source_id : "unknown");
        g_leak_incident_active = true;
    }

    // Check if valve is already closed AND RMLEAK already asserted
    int valve_state = ble_valve_get_state();
    bool rmleak_already = ble_valve_get_rmleak_state();
    if (valve_state == 0 && rmleak_already) {
        ESP_LOGD(RULES_TAG, "Valve closed + RMLEAK active, no action needed");
        xSemaphoreGive(g_mutex);
        return;
    }

    // Check cooldown (only for the close+rmleak write, not for the latch)
    TickType_t now = xTaskGetTickCount();
    if (g_auto_close_triggered &&
        (now - g_last_auto_close_tick) < pdMS_TO_TICKS(AUTO_CLOSE_COOLDOWN_MS)) {
        ESP_LOGD(RULES_TAG, "Auto-close cooldown active, skipping");
        xSemaphoreGive(g_mutex);
        return;
    }

    // === AUTO-CLOSE + RMLEAK: All conditions met ===
    ESP_LOGW(RULES_TAG, "AUTO-CLOSE + RMLEAK triggered by %s sensor %s",
             source_to_str(source), source_id ? source_id : "unknown");

    g_auto_close_triggered = true;
    g_last_auto_close_tick = now;

    // Build telemetry before releasing mutex
    build_auto_close_telemetry(source, source_id);

    xSemaphoreGive(g_mutex);

    // Issue valve commands (these are thread-safe, send to BLE command queue)
    ble_valve_connect();
    ble_valve_close();
    ble_valve_set_rmleak(true);
}

bool rules_engine_handle_config_command(const char *json_str)
{
    if (!json_str || !g_initialized) {
        return false;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(RULES_TAG, "Failed to parse config JSON");
        return false;
    }

    // Get current config (merge semantics)
    rules_config_t rules;
    if (!provisioning_get_rules_config(&rules)) {
        // Use defaults if can't read
        rules.auto_close_enabled = true;
        rules.trigger_mask = RULES_TRIGGER_ALL;
    }

    // Merge fields
    cJSON *auto_close = cJSON_GetObjectItem(root, "auto_close_enabled");
    if (auto_close && cJSON_IsBool(auto_close)) {
        rules.auto_close_enabled = cJSON_IsTrue(auto_close);
    }

    cJSON *trigger_mask = cJSON_GetObjectItem(root, "trigger_mask");
    if (trigger_mask && cJSON_IsNumber(trigger_mask)) {
        rules.trigger_mask = (uint8_t)trigger_mask->valueint;
    }

    // Parse individual trigger enables (convenience fields)
    cJSON *ble_leak = cJSON_GetObjectItem(root, "trigger_ble_leak");
    if (ble_leak && cJSON_IsBool(ble_leak)) {
        if (cJSON_IsTrue(ble_leak)) {
            rules.trigger_mask |= RULES_TRIGGER_BLE_LEAK;
        } else {
            rules.trigger_mask &= ~RULES_TRIGGER_BLE_LEAK;
        }
    }
    cJSON *lora = cJSON_GetObjectItem(root, "trigger_lora");
    if (lora && cJSON_IsBool(lora)) {
        if (cJSON_IsTrue(lora)) {
            rules.trigger_mask |= RULES_TRIGGER_LORA;
        } else {
            rules.trigger_mask &= ~RULES_TRIGGER_LORA;
        }
    }
    cJSON *valve_flood = cJSON_GetObjectItem(root, "trigger_valve_flood");
    if (valve_flood && cJSON_IsBool(valve_flood)) {
        if (cJSON_IsTrue(valve_flood)) {
            rules.trigger_mask |= RULES_TRIGGER_VALVE_FLOOD;
        } else {
            rules.trigger_mask &= ~RULES_TRIGGER_VALVE_FLOOD;
        }
    }

    cJSON_Delete(root);

    // Persist
    bool ok = provisioning_set_rules_config(&rules);
    if (ok) {
        ESP_LOGI(RULES_TAG, "Config updated: auto_close=%s triggers=0x%02X",
                 rules.auto_close_enabled ? "enabled" : "disabled",
                 rules.trigger_mask);
    }

    return ok;
}

char *rules_engine_take_pending_telemetry(void)
{
    if (!g_initialized) return NULL;

    char *result = NULL;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        result = g_pending_telemetry;
        g_pending_telemetry = NULL;
        xSemaphoreGive(g_mutex);
    }

    return result;
}

bool rules_engine_is_leak_incident_active(void)
{
    if (!g_initialized) return false;

    bool active = false;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        active = g_leak_incident_active;
        xSemaphoreGive(g_mutex);
    }
    return active;
}

void rules_engine_reset_leak_incident(void)
{
    if (!g_initialized) return;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(RULES_TAG, "Failed to take mutex for LEAK_RESET");
        return;
    }

    if (!g_leak_incident_active) {
        ESP_LOGI(RULES_TAG, "LEAK_RESET: no active incident");
        xSemaphoreGive(g_mutex);
        return;
    }

    ESP_LOGW(RULES_TAG, "LEAK_RESET: clearing incident latch + RMLEAK");
    g_leak_incident_active = false;
    g_auto_close_triggered = false;

    // Build reset telemetry
    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root, "event", "rmleak_cleared");
        if (g_pending_telemetry) free(g_pending_telemetry);
        g_pending_telemetry = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
    }

    xSemaphoreGive(g_mutex);

    // Clear RMLEAK on valve (non-blocking BLE write)
    ble_valve_set_rmleak(false);
}

void rules_engine_reassert_rmleak_if_needed(void)
{
    if (!g_initialized) return;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        bool active = g_leak_incident_active;
        xSemaphoreGive(g_mutex);

        if (active) {
            ESP_LOGW(RULES_TAG, "Reconnected with active leak incident — re-asserting RMLEAK");
            ble_valve_set_rmleak(true);
        }
    }
}
