#include "rules_engine.h"
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include "esp_log.h"
#include "cJSON.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "provisioning_manager.h"
#include "app_ble_valve.h"
#include "sensor_meta.h"

#define RULES_TAG "RULES_ENGINE"
#define AUTO_CLOSE_COOLDOWN_MS 10000   // 10s cooldown between auto-closes
#define AUTO_CLEAR_TIMEOUT_MS  (30 * 1000)  // 30s all-clear before RMLEAK auto-reset
#define RMLEAK_GRACE_PERIOD_MS 5000    // 5s grace after RMLEAK write before checking valve state

// ─── 24-Hour Water Access Override Window ────────────────────────────────────
// When the user physically overrides the valve (clears RMLEAK via button press),
// all automatic valve closures are blocked for 24 hours. This gives the user
// guaranteed water access while still reporting leaks to the cloud. The window
// can be cancelled early by a C2D "override_cancel" command or LEAK_RESET.
#define OVERRIDE_WINDOW_DURATION_S   (24 * 60 * 60)  // 24 hours
#define OVERRIDE_BLOCKED_COOLDOWN_MS 60000            // Rate-limit "blocked" telemetry to 1/min
#define NVS_OVERRIDE_NAMESPACE       "rules_eng"
#define NVS_KEY_OVR_STATE            "ovr_state"
#define NVS_KEY_OVR_EXPIRY           "ovr_expiry"

// Minimum epoch value to consider time "synced" (2024-01-01 00:00:00 UTC)
#define EPOCH_VALID_THRESHOLD        1704067200

// ─── Override State Machine ──────────────────────────────────────────────────
// OVERRIDE_STATE_INACTIVE:  Normal auto-close behavior. All rules apply.
// OVERRIDE_STATE_ACTIVE:    24h override window active. Auto-close blocked.
//                           Leaks still tracked + reported to cloud.
typedef enum {
    OVERRIDE_STATE_INACTIVE = 0,
    OVERRIDE_STATE_ACTIVE   = 1,
} override_state_t;

static bool g_initialized = false;
static bool g_auto_close_triggered = false;
static bool g_leak_incident_active = false;  // Latched: stays true until explicit LEAK_RESET
static TickType_t g_last_auto_close_tick = 0;
static TickType_t g_rmleak_assert_tick = 0;  // When RMLEAK was last written — grace period for override check
static SemaphoreHandle_t g_mutex = NULL;

// 24h override window state
static override_state_t g_override_state = OVERRIDE_STATE_INACTIVE;
static time_t g_override_window_expiry = 0;  // Unix epoch when window expires (0 = inactive)
static TickType_t g_last_blocked_event_tick = 0;

// Tracks valve ready-state transitions to detect reconnect in tick()
static bool g_valve_was_ready = false;

// Pending auto-close telemetry (built by rules engine, consumed by IoT Hub)
static char *g_pending_telemetry = NULL;

// Active leak source tracking for auto-clear timeout
#define MAX_ACTIVE_LEAK_SOURCES 16
static char g_active_leak_ids[MAX_ACTIVE_LEAK_SOURCES][18];
static uint8_t g_active_leak_count = 0;
static TickType_t g_all_clear_since = 0;  // 0 = not yet all clear

// ─── NVS Persistence for Override Window ─────────────────────────────────────

static void override_clear_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_OVERRIDE_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return;
    nvs_erase_key(h, NVS_KEY_OVR_STATE);
    nvs_erase_key(h, NVS_KEY_OVR_EXPIRY);
    nvs_commit(h);
    nvs_close(h);
}

static void override_save_to_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_OVERRIDE_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(RULES_TAG, "NVS open failed for override save: %s", esp_err_to_name(err));
        return;
    }
    nvs_set_u8(h, NVS_KEY_OVR_STATE, (uint8_t)g_override_state);
    // time_t is 32-bit on ESP32 — store as u32 (valid until 2038)
    nvs_set_u32(h, NVS_KEY_OVR_EXPIRY, (uint32_t)g_override_window_expiry);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(RULES_TAG, "NVS: override state=%d expiry=%ld saved",
             g_override_state, (long)g_override_window_expiry);
}

static void override_load_from_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_OVERRIDE_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        // No stored data — start fresh
        g_override_state = OVERRIDE_STATE_INACTIVE;
        g_override_window_expiry = 0;
        return;
    }

    uint8_t state = 0;
    uint32_t expiry = 0;
    err = nvs_get_u8(h, NVS_KEY_OVR_STATE, &state);
    if (err != ESP_OK) state = 0;
    err = nvs_get_u32(h, NVS_KEY_OVR_EXPIRY, &expiry);
    if (err != ESP_OK) expiry = 0;
    nvs_close(h);

    if (state == OVERRIDE_STATE_ACTIVE && expiry > 0) {
        time_t now;
        time(&now);

        if (now >= EPOCH_VALID_THRESHOLD && (time_t)expiry <= now) {
            // Window already expired — clear it
            ESP_LOGW(RULES_TAG, "NVS: override window expired during power-off (expiry=%lu, now=%ld)",
                     (unsigned long)expiry, (long)now);
            g_override_state = OVERRIDE_STATE_INACTIVE;
            g_override_window_expiry = 0;
            override_clear_nvs();
        } else {
            // Window still active (or time not synced yet — defer check to tick)
            g_override_state = OVERRIDE_STATE_ACTIVE;
            g_override_window_expiry = (time_t)expiry;
            int32_t remaining = (now >= EPOCH_VALID_THRESHOLD)
                ? (int32_t)(g_override_window_expiry - now) : -1;
            ESP_LOGW(RULES_TAG, "NVS: restored override window (expiry=%lu, remaining=%lds)",
                     (unsigned long)expiry, (long)remaining);
        }
    } else {
        g_override_state = OVERRIDE_STATE_INACTIVE;
        g_override_window_expiry = 0;
    }
}

// Start or refresh the 24h override window.  Must be called with g_mutex held.
static void start_override_window(void)
{
    time_t now;
    time(&now);

    override_state_t prev_state = g_override_state;
    g_override_state = OVERRIDE_STATE_ACTIVE;
    g_override_window_expiry = now + OVERRIDE_WINDOW_DURATION_S;

    override_save_to_nvs();

    if (prev_state == OVERRIDE_STATE_INACTIVE) {
        ESP_LOGW(RULES_TAG, "OVERRIDE WINDOW STARTED: auto-close blocked for 24h (expiry=%ld)",
                 (long)g_override_window_expiry);
    } else {
        ESP_LOGW(RULES_TAG, "OVERRIDE WINDOW REFRESHED: timer reset to 24h (expiry=%ld)",
                 (long)g_override_window_expiry);
    }

    // Build telemetry event
    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root, "event", "water_access_override_enabled");
        cJSON_AddNumberToObject(root, "expiry_epoch", (double)g_override_window_expiry);
        cJSON_AddNumberToObject(root, "duration_h", 24);
        if (g_pending_telemetry) free(g_pending_telemetry);
        g_pending_telemetry = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
    }
}

// Cancel the override window and return to normal auto-close. Must be called with g_mutex held.
// Returns the remaining seconds at time of cancellation (for telemetry).
static int32_t cancel_override_window(void)
{
    int32_t remaining = 0;
    if (g_override_state == OVERRIDE_STATE_ACTIVE) {
        time_t now;
        time(&now);
        if (now >= EPOCH_VALID_THRESHOLD && g_override_window_expiry > now) {
            remaining = (int32_t)(g_override_window_expiry - now);
        }
    }

    g_override_state = OVERRIDE_STATE_INACTIVE;
    g_override_window_expiry = 0;
    override_clear_nvs();

    ESP_LOGW(RULES_TAG, "OVERRIDE WINDOW CANCELLED (remaining_s=%ld)",
             (long)remaining);
    return remaining;
}

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
        case LEAK_SOURCE_BLE:         return "ble_leak_sensor";
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

// Must be called with g_mutex held
static void track_leak_source(const char *source_id, bool leak_active)
{
    if (!source_id) return;

    if (leak_active) {
        // Add to tracking table if not already present
        for (int i = 0; i < g_active_leak_count; i++) {
            if (strcmp(g_active_leak_ids[i], source_id) == 0) return;
        }
        if (g_active_leak_count < MAX_ACTIVE_LEAK_SOURCES) {
            strncpy(g_active_leak_ids[g_active_leak_count], source_id, 17);
            g_active_leak_ids[g_active_leak_count][17] = '\0';
            g_active_leak_count++;
        }
        g_all_clear_since = 0;  // Reset timer whenever any source reports leak
    } else {
        // Remove from tracking table
        for (int i = 0; i < g_active_leak_count; i++) {
            if (strcmp(g_active_leak_ids[i], source_id) == 0) {
                for (int j = i; j < g_active_leak_count - 1; j++) {
                    strcpy(g_active_leak_ids[j], g_active_leak_ids[j + 1]);
                }
                g_active_leak_count--;
                break;
            }
        }
        if (g_active_leak_count == 0) {
            // Cancel any pending auto-close commands that haven't been applied yet.
            // This prevents stale CLOSE/RMLEAK from firing on reconnect after the
            // leak has already resolved. Only cancels if auto-close set them.
            if (g_auto_close_triggered) {
                ble_valve_cancel_pending_close();
                ESP_LOGI(RULES_TAG, "All leaks resolved — pending auto-close cancelled");
                g_auto_close_triggered = false;
            }
            // Note: override window is NOT lifted when sensors clear — it is purely
            // time-based (24h) and only cleared by expiry, C2D override_cancel, or LEAK_RESET.

            // If incident is active, start auto-clear timer
            if (g_leak_incident_active && g_all_clear_since == 0) {
                g_all_clear_since = xTaskGetTickCount();
                if (g_all_clear_since == 0) g_all_clear_since = 1;  // Avoid sentinel confusion
                ESP_LOGI(RULES_TAG, "All sensors clear — auto-clear timer started (%ds)",
                         AUTO_CLEAR_TIMEOUT_MS / 1000);
            }
        }
    }
}

static void build_auto_close_telemetry(leak_source_t source, const char *source_id)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddStringToObject(root, "event", "auto_close");
    cJSON_AddStringToObject(root, "source_type", source_to_str(source));
    cJSON_AddStringToObject(root, "sensor_id", source_id ? source_id : "unknown");
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

    // Restore override window from NVS (survives power cycle)
    override_load_from_nvs();

    g_initialized = true;
}

void rules_engine_evaluate_leak(leak_source_t source, bool leak_active, const char *source_id)
{
    if (!g_initialized) return;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(RULES_TAG, "Failed to take mutex");
        return;
    }

    // Track active leak sources for auto-clear timeout
    track_leak_source(source_id, leak_active);

    // Only act on leak-detected events for auto-close
    if (!leak_active) {
        xSemaphoreGive(g_mutex);
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

    // Latch the leak incident ALWAYS — even during override window.
    // This ensures the system knows about the incident when the window expires
    // and can immediately auto-close.
    if (!g_leak_incident_active) {
        ESP_LOGW(RULES_TAG, "LEAK INCIDENT latched by %s sensor %s",
                 source_to_str(source), source_id ? source_id : "unknown");
        g_leak_incident_active = true;
    }

    // 24h override window: block automatic valve closure but allow leak tracking.
    // The user physically overrode the valve and has guaranteed water access for 24h.
    // Leak events are still reported to the cloud via normal telemetry paths.
    if (g_override_state == OVERRIDE_STATE_ACTIVE) {
        TickType_t now = xTaskGetTickCount();
        if ((now - g_last_blocked_event_tick) >= pdMS_TO_TICKS(OVERRIDE_BLOCKED_COOLDOWN_MS) ||
            g_last_blocked_event_tick == 0) {
            g_last_blocked_event_tick = now;

            // Compute remaining time for telemetry
            time_t now_epoch;
            time(&now_epoch);
            int32_t remaining = (now_epoch >= EPOCH_VALID_THRESHOLD && g_override_window_expiry > now_epoch)
                ? (int32_t)(g_override_window_expiry - now_epoch) : -1;

            ESP_LOGI(RULES_TAG, "Override active — auto-close BLOCKED for %s sensor %s (remaining=%lds)",
                     source_to_str(source), source_id ? source_id : "unknown", (long)remaining);

            cJSON *root = cJSON_CreateObject();
            if (root) {
                cJSON_AddStringToObject(root, "event", "auto_close_blocked_override");
                cJSON_AddStringToObject(root, "source_type", source_to_str(source));
                cJSON_AddStringToObject(root, "sensor_id", source_id ? source_id : "unknown");
                if (remaining >= 0) {
                    cJSON_AddNumberToObject(root, "override_remaining_s", remaining);
                }
                if (g_pending_telemetry) free(g_pending_telemetry);
                g_pending_telemetry = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
            }
        }
        xSemaphoreGive(g_mutex);
        return;
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
    g_rmleak_assert_tick = now;  // Grace period: don't check valve override until BLE write propagates

    // Build telemetry before releasing mutex
    build_auto_close_telemetry(source, source_id);

    xSemaphoreGive(g_mutex);

    // Issue valve commands only if connected — avoids stale pending commands
    // that would fire on reconnect even if the leak has since cleared.
    // If disconnected, the reconciliation (on_valve_connected) handles it.
    if (ble_valve_is_connected()) {
        ble_valve_close();
        ble_valve_set_rmleak(true);
    } else {
        ble_valve_connect();  // Trigger scan; reconciliation closes on connect
    }
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

    // Always clear hub-side state, even if not latched (handles reboot scenario)
    bool was_active = g_leak_incident_active;
    g_leak_incident_active = false;
    g_auto_close_triggered = false;
    g_all_clear_since = 0;
    g_active_leak_count = 0;
    g_rmleak_assert_tick = 0;

    // Also clear override window — LEAK_RESET is a full reset that restores
    // normal auto-close behavior immediately.
    bool had_override = (g_override_state == OVERRIDE_STATE_ACTIVE);
    if (had_override) {
        cancel_override_window();
    }

    // Check if valve still has RMLEAK asserted (survives hub reboot)
    bool valve_rmleak = ble_valve_get_rmleak_state();

    if (was_active || valve_rmleak || had_override) {
        ESP_LOGW(RULES_TAG, "LEAK_RESET: clearing incident (hub_latch=%d, valve_rmleak=%d, override=%d)",
                 was_active, valve_rmleak, had_override);

        // Build reset telemetry
        cJSON *root = cJSON_CreateObject();
        if (root) {
            cJSON_AddStringToObject(root, "event", "rmleak_cleared");
            if (had_override) {
                cJSON_AddBoolToObject(root, "override_cancelled", true);
            }
            if (g_pending_telemetry) free(g_pending_telemetry);
            g_pending_telemetry = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
        }
    } else {
        ESP_LOGI(RULES_TAG, "LEAK_RESET: no active incident");
    }

    xSemaphoreGive(g_mutex);

    // Always clear RMLEAK on valve — handles case where hub rebooted but
    // valve still has RMLEAK=1 from previous session
    if (was_active || valve_rmleak) {
        ble_valve_set_rmleak(false);
    }
}

bool rules_engine_cancel_override(void)
{
    if (!g_initialized) return false;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(RULES_TAG, "Failed to take mutex for override_cancel");
        return false;
    }

    if (g_override_state != OVERRIDE_STATE_ACTIVE) {
        ESP_LOGI(RULES_TAG, "override_cancel: no active override window");
        xSemaphoreGive(g_mutex);
        return true;  // Not an error — command succeeds, nothing to cancel
    }

    int32_t remaining = cancel_override_window();

    // Build acknowledgment telemetry
    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root, "event", "auto_close_reenabled");
        cJSON_AddNumberToObject(root, "previous_remaining_s", remaining);
        cJSON_AddStringToObject(root, "reason", "c2d_command");
        if (g_pending_telemetry) free(g_pending_telemetry);
        g_pending_telemetry = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
    }

    // If leaks are currently active, immediately trigger auto-close evaluation.
    // This ensures the valve closes as soon as the user re-enables auto-close,
    // without waiting for the next leak event or tick cycle.
    // Note: g_leak_incident_active may have been cleared during the override window
    // (e.g., by auto-clear timer), so we check g_active_leak_count directly.
    if (g_active_leak_count > 0) {
        rules_config_t rules;
        if (provisioning_get_rules_config(&rules) && rules.auto_close_enabled) {
            ESP_LOGW(RULES_TAG, "Override cancelled with %d active leak(s) — executing auto-close",
                     g_active_leak_count);

            g_leak_incident_active = true;  // Re-latch incident for auto-close path
            g_auto_close_triggered = true;
            g_rmleak_assert_tick = xTaskGetTickCount();
            g_all_clear_since = 0;

            xSemaphoreGive(g_mutex);

            if (ble_valve_is_connected()) {
                ble_valve_close();
                ble_valve_set_rmleak(true);
            } else {
                ble_valve_connect();
            }
            return true;
        }
    }

    xSemaphoreGive(g_mutex);
    return true;
}

void rules_engine_reassert_rmleak_if_needed(void)
{
    // Kept for backward compatibility — delegates to full reconnect handler
    rules_engine_on_valve_connected();
}

void rules_engine_on_valve_connected(void)
{
    if (!g_initialized) return;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;

    // Read actual valve characteristics for reconciliation logging
    int valve_state = ble_valve_get_state();
    bool valve_rmleak = ble_valve_get_rmleak_state();
    bool valve_leak = ble_valve_get_leak();

    ESP_LOGI(RULES_TAG, "╔══ VALVE RECONNECT RECONCILIATION ══╗");
    ESP_LOGI(RULES_TAG, "║ Valve: state=%s rmleak=%s flood=%s",
             valve_state == 1 ? "OPEN" : valve_state == 0 ? "CLOSED" : "UNKNOWN",
             valve_rmleak ? "ACTIVE" : "CLEAR",
             valve_leak ? "LEAK" : "OK");
    ESP_LOGI(RULES_TAG, "║ Hub:   incident=%d leaks=%d override=%s",
             g_leak_incident_active, g_active_leak_count,
             g_override_state == OVERRIDE_STATE_ACTIVE ? "ACTIVE" : "INACTIVE");

    if (g_override_state == OVERRIDE_STATE_ACTIVE) {
        time_t now;
        time(&now);
        int32_t remaining = (now >= EPOCH_VALID_THRESHOLD && g_override_window_expiry > now)
            ? (int32_t)(g_override_window_expiry - now) : -1;
        ESP_LOGI(RULES_TAG, "║ Override window: remaining=%lds", (long)remaining);
    }
    ESP_LOGI(RULES_TAG, "╚════════════════════════════════════╝");

    // Refresh RMLEAK grace period on reconnect.  Pending commands (CLOSE + RMLEAK)
    // are being applied by the BLE valve task right now.  Without this, tick Check 2
    // could see stale RMLEAK=0 and falsely detect a physical override.
    if (g_leak_incident_active || g_auto_close_triggered) {
        g_rmleak_assert_tick = xTaskGetTickCount();
    }

    // ── Priority 0: Override window check ─────────────────────────────────
    // If the override window is active, do NOT auto-close on reconnect.
    // The user has guaranteed water access for the remaining window duration.
    if (g_override_state == OVERRIDE_STATE_ACTIVE) {
        // Still sync hub incident latch with valve state
        if (!g_leak_incident_active && valve_rmleak) {
            ESP_LOGW(RULES_TAG, "Reconnected: valve RMLEAK active + override window — re-latching incident");
            g_leak_incident_active = true;
        }
        ESP_LOGI(RULES_TAG, "Reconnected: override window active — skipping auto-close");
        xSemaphoreGive(g_mutex);
        return;
    }

    // ── Priority 1: Active leaks present → auto-close if enabled ──────────
    // This handles the case where leaks were detected while valve was offline.
    // Single evaluation regardless of how many sensors are leaking (anti-spam).
    if (g_active_leak_count > 0) {
        rules_config_t rules;
        if (provisioning_get_rules_config(&rules) && rules.auto_close_enabled) {
            ESP_LOGW(RULES_TAG, "Valve reconnected with %d active leak(s) — executing auto-close",
                     g_active_leak_count);

            g_leak_incident_active = true;
            g_auto_close_triggered = true;
            g_rmleak_assert_tick = xTaskGetTickCount();
            g_all_clear_since = 0;

            // Build telemetry
            cJSON *root = cJSON_CreateObject();
            if (root) {
                cJSON_AddStringToObject(root, "event", "auto_close");
                cJSON_AddStringToObject(root, "source_type", "reconnect");
                cJSON_AddStringToObject(root, "sensor_id",
                    g_active_leak_count > 0 ? g_active_leak_ids[0] : "unknown");
                cJSON_AddBoolToObject(root, "rmleak_asserted", true);
                cJSON_AddNumberToObject(root, "active_leak_count", g_active_leak_count);
                if (g_pending_telemetry) free(g_pending_telemetry);
                g_pending_telemetry = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
            }

            xSemaphoreGive(g_mutex);

            ble_valve_close();
            ble_valve_set_rmleak(true);
            return;
        }
    }

    // ── Priority 2: Hub/valve RMLEAK state synchronization ────────────────
    // Handles reboot scenarios where one side lost state.
    bool hub_active = g_leak_incident_active;

    if (hub_active && !valve_rmleak) {
        // Hub has incident but valve lost RMLEAK (valve rebooted) — re-assert
        ESP_LOGW(RULES_TAG, "Reconnected: hub incident active, valve RMLEAK clear — re-asserting");
        g_rmleak_assert_tick = xTaskGetTickCount();
        xSemaphoreGive(g_mutex);
        ble_valve_set_rmleak(true);
    } else if (!hub_active && valve_rmleak) {
        // Valve has RMLEAK but hub lost incident (hub rebooted).
        // Conservatively re-latch the incident. If the valve has RMLEAK but hub
        // doesn't know about it, the user will need to send LEAK_RESET to clear.
        ESP_LOGW(RULES_TAG, "Reconnected: valve RMLEAK active, hub incident clear — re-latching incident");
        g_leak_incident_active = true;
        xSemaphoreGive(g_mutex);
    } else if (hub_active && valve_rmleak) {
        ESP_LOGI(RULES_TAG, "Reconnected: hub + valve RMLEAK in sync");
        xSemaphoreGive(g_mutex);
    } else {
        ESP_LOGI(RULES_TAG, "Reconnected: no active incident, valve clear");
        xSemaphoreGive(g_mutex);
    }
}

void rules_engine_tick(void)
{
    if (!g_initialized) return;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    // ── Override window expiry check ──────────────────────────────────────
    // Runs regardless of incident state — the window can expire even if all
    // leaks have cleared (the incident may still be latched).
    if (g_override_state == OVERRIDE_STATE_ACTIVE) {
        time_t now_epoch;
        time(&now_epoch);
        // Only check expiry when time is synced (valid epoch)
        if (now_epoch >= EPOCH_VALID_THRESHOLD && now_epoch >= g_override_window_expiry) {
            ESP_LOGW(RULES_TAG, "OVERRIDE WINDOW EXPIRED: auto-close re-enabled");
            g_override_state = OVERRIDE_STATE_INACTIVE;
            g_override_window_expiry = 0;
            override_clear_nvs();

            // Build expiry telemetry
            cJSON *root = cJSON_CreateObject();
            if (root) {
                cJSON_AddStringToObject(root, "event", "water_access_override_expired");
                cJSON_AddBoolToObject(root, "auto_close_resumed",
                    g_active_leak_count > 0);
                cJSON_AddNumberToObject(root, "active_leak_count", g_active_leak_count);
                if (g_pending_telemetry) free(g_pending_telemetry);
                g_pending_telemetry = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
            }

            // If leaks are still active, immediately trigger auto-close.
            // This resumes normal protection as soon as the override expires.
            if (g_active_leak_count > 0) {
                rules_config_t rules;
                if (provisioning_get_rules_config(&rules) && rules.auto_close_enabled) {
                    ESP_LOGW(RULES_TAG, "Override expired with %d active leak(s) — executing auto-close",
                             g_active_leak_count);

                    g_leak_incident_active = true;
                    g_auto_close_triggered = true;
                    g_rmleak_assert_tick = xTaskGetTickCount();
                    g_all_clear_since = 0;

                    xSemaphoreGive(g_mutex);

                    if (ble_valve_is_connected()) {
                        ble_valve_close();
                        ble_valve_set_rmleak(true);
                    } else {
                        ble_valve_connect();
                    }
                    return;
                }
            }
        }
    }

    if (!g_leak_incident_active) {
        xSemaphoreGive(g_mutex);
        return;
    }

    TickType_t now = xTaskGetTickCount();

    // Check 1: Auto-clear timeout (all sensors clear for AUTO_CLEAR_TIMEOUT_MS)
    if (g_all_clear_since != 0) {
        if ((now - g_all_clear_since) >= pdMS_TO_TICKS(AUTO_CLEAR_TIMEOUT_MS)) {
            ESP_LOGW(RULES_TAG, "AUTO-CLEAR: all sensors clear for %ds — clearing RMLEAK",
                     AUTO_CLEAR_TIMEOUT_MS / 1000);
            g_leak_incident_active = false;
            g_auto_close_triggered = false;
            g_all_clear_since = 0;

            cJSON *root = cJSON_CreateObject();
            if (root) {
                cJSON_AddStringToObject(root, "event", "rmleak_auto_cleared");
                cJSON_AddNumberToObject(root, "clear_after_seconds", AUTO_CLEAR_TIMEOUT_MS / 1000);
                if (g_pending_telemetry) free(g_pending_telemetry);
                g_pending_telemetry = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
            }

            xSemaphoreGive(g_mutex);
            ble_valve_set_rmleak(false);  // Does NOT open valve
            return;
        }
    }

    // Check 2: Valve-side physical override (RMLEAK cleared on valve while incident active)
    // SKIP when override window is already active — re-detecting would corrupt the
    // incident latch (clearing g_leak_incident_active) and prevent auto-close from
    // working when the override is later cancelled or expires.
    // ONLY check when valve is fully ready (GATT setup complete) — during disconnection
    // or GATT setup, ble_valve_get_rmleak_state() returns stale data.
    if (g_override_state == OVERRIDE_STATE_INACTIVE && ble_valve_is_ready()) {
        // Grace period: skip check if RMLEAK was just written or valve just reconnected
        // (BLE write may still be in-flight or pending commands not yet applied)
        if (g_rmleak_assert_tick != 0 &&
            (now - g_rmleak_assert_tick) < pdMS_TO_TICKS(RMLEAK_GRACE_PERIOD_MS)) {
            xSemaphoreGive(g_mutex);
            return;
        }
        // Detect transition to ready — valve just reconnected and pending RMLEAK
        // write may not have propagated yet.  Refresh grace period once.
        if (!g_valve_was_ready) {
            g_valve_was_ready = true;
            g_rmleak_assert_tick = now;
            ESP_LOGD(RULES_TAG, "Valve just became ready — refreshing RMLEAK grace period");
            xSemaphoreGive(g_mutex);
            return;
        }
        if (!ble_valve_get_rmleak_state()) {
            // Physical override detected — the user cleared RMLEAK via the valve button.
            // Start a 24h override window: automatic closures are blocked to guarantee
            // water access, but leaks continue to be reported to the cloud.
            ESP_LOGW(RULES_TAG, "RMLEAK cleared externally (valve override) — starting 24h override window");
            g_leak_incident_active = false;
            g_auto_close_triggered = false;
            g_all_clear_since = 0;

            start_override_window();
        }
    }
    // Track valve ready state for transition detection
    if (!ble_valve_is_ready()) {
        g_valve_was_ready = false;
    }

    xSemaphoreGive(g_mutex);
}

// ─── Override Window Query APIs ─────────────────────────────────────────────

bool rules_engine_is_override_window_active(void)
{
    if (!g_initialized) return false;

    bool active = false;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        active = (g_override_state == OVERRIDE_STATE_ACTIVE);
        xSemaphoreGive(g_mutex);
    }
    return active;
}

int32_t rules_engine_get_override_remaining_s(void)
{
    if (!g_initialized) return -1;

    int32_t remaining = -1;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (g_override_state == OVERRIDE_STATE_ACTIVE) {
            time_t now;
            time(&now);
            if (now >= EPOCH_VALID_THRESHOLD && g_override_window_expiry > now) {
                remaining = (int32_t)(g_override_window_expiry - now);
            } else if (now < EPOCH_VALID_THRESHOLD) {
                remaining = OVERRIDE_WINDOW_DURATION_S;  // Time not synced, report full duration
            } else {
                remaining = 0;  // Expired but not yet processed by tick
            }
        }
        xSemaphoreGive(g_mutex);
    }
    return remaining;
}
