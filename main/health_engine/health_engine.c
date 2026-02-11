#include "health_engine.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "provisioning_manager.h"

#define HEALTH_TAG "HEALTH_ENGINE"

// ---------------------------------------------------------------------------
// Internal per-device state
// ---------------------------------------------------------------------------
typedef struct {
    bool              in_use;
    health_dev_type_t dev_type;
    char              dev_id[18];       // MAC string or "0xHEXID"
    health_rating_t   rating;
    health_rating_t   prev_rating;
    int64_t           last_seen_ms;     // Monotonic: esp_timer_get_time()/1000
    uint8_t           last_battery;     // 0xFF = unknown
    int8_t            last_rssi;        // 0 = unknown
    int64_t           last_alert_ms;    // Last alert timestamp (debounce)
    bool              ever_seen;        // false until first check-in this uptime
    int64_t           disconnect_ms;    // Valve only: disconnect timestamp, 0 = connected
} health_device_t;

// ---------------------------------------------------------------------------
// Static state
// ---------------------------------------------------------------------------
static QueueHandle_t  s_health_queue  = NULL;   // Input: health events
static QueueHandle_t  s_alert_queue   = NULL;   // Output: alerts for IoT Hub
static TimerHandle_t  s_tick_timer    = NULL;
static health_device_t s_devices[HEALTH_MAX_DEVICES];
static volatile health_rating_t s_system_rating = HEALTH_EXCELLENT;
static bool s_initialized = false;
static SemaphoreHandle_t s_mutex = NULL;
static bool    s_boot_sync_done = false;
static int64_t s_boot_start_ms  = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int64_t now_ms(void)
{
    return (int64_t)(esp_timer_get_time() / 1000);
}

const char *health_rating_to_str(health_rating_t rating)
{
    switch (rating) {
        case HEALTH_EXCELLENT: return "excellent";
        case HEALTH_GOOD:      return "good";
        case HEALTH_WARNING:   return "warning";
        case HEALTH_CRITICAL:  return "critical";
        default:               return "unknown";
    }
}

static const char *dev_type_to_str(health_dev_type_t dt)
{
    switch (dt) {
        case HEALTH_DEV_VALVE:    return "valve";
        case HEALTH_DEV_LORA:     return "lora";
        case HEALTH_DEV_BLE_LEAK: return "ble_leak";
        default:                  return "unknown";
    }
}

// ---------------------------------------------------------------------------
// Device lookup
// ---------------------------------------------------------------------------

static health_device_t *find_device(health_dev_type_t dt, const char *dev_id)
{
    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (s_devices[i].in_use &&
            s_devices[i].dev_type == dt &&
            strcasecmp(s_devices[i].dev_id, dev_id) == 0) {
            return &s_devices[i];
        }
    }
    return NULL;
}

static health_device_t *find_valve(void)
{
    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (s_devices[i].in_use && s_devices[i].dev_type == HEALTH_DEV_VALVE) {
            return &s_devices[i];
        }
    }
    return NULL;
}

// Forward declaration (defined after evaluate_timeouts)
static void check_boot_sync_locked(void);

// ---------------------------------------------------------------------------
// Rating calculation
// ---------------------------------------------------------------------------

static health_rating_t compute_sensor_rating(const health_device_t *dev, int64_t now)
{
    // Check connectivity timeout
    uint32_t timeout_ms = (dev->dev_type == HEALTH_DEV_LORA)
                          ? HEALTH_LORA_TIMEOUT_MS
                          : HEALTH_BLE_LEAK_TIMEOUT_MS;

    if (dev->last_seen_ms == 0 || (now - dev->last_seen_ms) > timeout_ms) {
        return HEALTH_CRITICAL;
    }

    // Online — evaluate battery and signal
    if (dev->last_battery != 0xFF && dev->last_battery <= HEALTH_BATTERY_WARN_PCT) {
        return HEALTH_WARNING;
    }
    if (dev->last_rssi != 0 && dev->last_rssi <= HEALTH_RSSI_WARN_DBM) {
        return HEALTH_WARNING;
    }
    if (dev->last_battery != 0xFF && dev->last_battery <= HEALTH_BATTERY_GOOD_PCT) {
        return HEALTH_GOOD;
    }
    if (dev->last_rssi != 0 &&
        dev->last_rssi > HEALTH_RSSI_WARN_DBM &&
        dev->last_rssi <= HEALTH_RSSI_GOOD_DBM) {
        return HEALTH_GOOD;
    }

    return HEALTH_EXCELLENT;
}

static health_rating_t compute_valve_rating(const health_device_t *dev, int64_t now)
{
    // Disconnected: check grace period
    if (dev->disconnect_ms > 0) {
        if ((now - dev->disconnect_ms) >= HEALTH_VALVE_DISC_TIMEOUT_MS)
            return HEALTH_CRITICAL;
        return HEALTH_WARNING;  // Grace period — not yet CRITICAL
    }

    // Never connected this uptime
    if (dev->last_seen_ms == 0) {
        return HEALTH_CRITICAL;
    }

    // Connected — evaluate battery
    if (dev->last_battery != 0xFF && dev->last_battery <= HEALTH_BATTERY_WARN_PCT) {
        return HEALTH_WARNING;
    }
    if (dev->last_battery != 0xFF && dev->last_battery <= HEALTH_BATTERY_GOOD_PCT) {
        return HEALTH_GOOD;
    }

    return HEALTH_EXCELLENT;
}

static void recalc_system_rating(void)
{
    health_rating_t worst = HEALTH_EXCELLENT;

    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (!s_devices[i].in_use) continue;
        if (s_devices[i].rating > worst) {
            worst = s_devices[i].rating;
        }
    }

    s_system_rating = worst;
}

// ---------------------------------------------------------------------------
// Alert generation
// ---------------------------------------------------------------------------

static void maybe_enqueue_alert(health_device_t *dev, health_rating_t new_rating, int64_t now)
{
    health_rating_t old_rating = dev->rating;

    // Only alert on Critical transitions (into or out of)
    bool into_critical  = (new_rating == HEALTH_CRITICAL && old_rating != HEALTH_CRITICAL);
    bool out_of_critical = (new_rating != HEALTH_CRITICAL && old_rating == HEALTH_CRITICAL);

    if (!into_critical && !out_of_critical) {
        return;
    }

    // Suppress boot-time "recovered" alerts — first check-in is not a real recovery
    if (out_of_critical && !dev->ever_seen) {
        return;
    }

    // Debounce check
    if (dev->last_alert_ms != 0 &&
        (now - dev->last_alert_ms) < HEALTH_ALERT_DEBOUNCE_MS) {
        return;
    }

    // Build alert
    health_alert_t alert;
    memset(&alert, 0, sizeof(alert));
    alert.dev_type    = dev->dev_type;
    strncpy(alert.dev_id, dev->dev_id, sizeof(alert.dev_id) - 1);
    alert.new_rating  = new_rating;
    alert.old_rating  = old_rating;
    alert.battery     = dev->last_battery;
    alert.rssi        = dev->last_rssi;

    if (into_critical && dev->last_seen_ms > 0) {
        alert.offline_duration_s = (uint32_t)((now - dev->last_seen_ms) / 1000);
    }

    if (xQueueSend(s_alert_queue, &alert, 0) == pdTRUE) {
        dev->last_alert_ms = now;
        ESP_LOGW(HEALTH_TAG, "ALERT: %s %s %s -> %s",
                 dev_type_to_str(dev->dev_type), dev->dev_id,
                 health_rating_to_str(old_rating),
                 health_rating_to_str(new_rating));
    }
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

static void handle_lora_checkin(const health_event_t *evt)
{
    char id_str[16];
    snprintf(id_str, sizeof(id_str), "0x%08lX", (unsigned long)evt->lora.sensor_id);

    health_device_t *dev = find_device(HEALTH_DEV_LORA, id_str);
    if (!dev) return;  // Not provisioned

    int64_t now = now_ms();
    dev->last_seen_ms  = now;
    dev->last_battery  = evt->lora.battery;
    dev->last_rssi     = evt->lora.rssi;

    health_rating_t new_rating = compute_sensor_rating(dev, now);
    maybe_enqueue_alert(dev, new_rating, now);
    dev->prev_rating = dev->rating;
    dev->rating = new_rating;
    dev->ever_seen = true;
    check_boot_sync_locked();
}

static void handle_ble_leak_checkin(const health_event_t *evt)
{
    health_device_t *dev = find_device(HEALTH_DEV_BLE_LEAK, evt->ble_leak.mac_str);
    if (!dev) return;

    int64_t now = now_ms();
    dev->last_seen_ms  = now;
    dev->last_battery  = evt->ble_leak.battery;
    dev->last_rssi     = evt->ble_leak.rssi;

    health_rating_t new_rating = compute_sensor_rating(dev, now);
    maybe_enqueue_alert(dev, new_rating, now);
    dev->prev_rating = dev->rating;
    dev->rating = new_rating;
    dev->ever_seen = true;
    check_boot_sync_locked();
}

static void handle_valve_event(bool connected)
{
    health_device_t *dev = find_valve();
    if (!dev) return;

    int64_t now = now_ms();

    if (connected) {
        dev->last_seen_ms = now;
        dev->disconnect_ms = 0;   // Clear grace period
    } else {
        dev->disconnect_ms = now;  // Start grace period (keep last_seen_ms)
    }

    health_rating_t new_rating = compute_valve_rating(dev, now);
    maybe_enqueue_alert(dev, new_rating, now);
    dev->prev_rating = dev->rating;
    dev->rating = new_rating;
    if (connected) {
        dev->ever_seen = true;
        check_boot_sync_locked();
    }
}

static void evaluate_timeouts(void)
{
    int64_t now = now_ms();

    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        health_device_t *dev = &s_devices[i];
        if (!dev->in_use) continue;

        // Valve: check disconnect grace period expiry (WARNING → CRITICAL)
        if (dev->dev_type == HEALTH_DEV_VALVE) {
            if (dev->disconnect_ms > 0) {
                health_rating_t new_rating = compute_valve_rating(dev, now);
                if (new_rating != dev->rating) {
                    maybe_enqueue_alert(dev, new_rating, now);
                    dev->prev_rating = dev->rating;
                    dev->rating = new_rating;
                }
            }
            continue;
        }

        // Sensors: skip devices never seen (already CRITICAL from init)
        if (dev->last_seen_ms == 0) continue;

        health_rating_t new_rating = compute_sensor_rating(dev, now);

        if (new_rating != dev->rating) {
            maybe_enqueue_alert(dev, new_rating, now);
            dev->prev_rating = dev->rating;
            dev->rating = new_rating;
        }
    }

    check_boot_sync_locked();
}

// ---------------------------------------------------------------------------
// Boot sync check (call with s_mutex held)
// ---------------------------------------------------------------------------
static void check_boot_sync_locked(void)
{
    if (s_boot_sync_done) return;

    bool all_seen = true;
    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        if (s_devices[i].in_use && !s_devices[i].ever_seen) {
            all_seen = false;
            break;
        }
    }

    if (all_seen) {
        s_boot_sync_done = true;
        ESP_LOGI(HEALTH_TAG, "Boot sync: all devices seen");
    } else if ((now_ms() - s_boot_start_ms) >= HEALTH_BOOT_SYNC_TIMEOUT_MS) {
        s_boot_sync_done = true;
        ESP_LOGW(HEALTH_TAG, "Boot sync: timeout (2 min)");
    }
}

// ---------------------------------------------------------------------------
// Tick timer callback (runs in timer daemon context)
// ---------------------------------------------------------------------------
static void tick_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    health_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = HEALTH_EVT_TICK;
    xQueueSend(s_health_queue, &evt, 0);
}

// ---------------------------------------------------------------------------
// Main task
// ---------------------------------------------------------------------------
static void health_engine_task(void *param)
{
    (void)param;
    health_event_t evt;

    ESP_LOGI(HEALTH_TAG, "Task started");

    while (1) {
        if (xQueueReceive(s_health_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        xSemaphoreTake(s_mutex, portMAX_DELAY);

        switch (evt.type) {
            case HEALTH_EVT_LORA_CHECKIN:
                handle_lora_checkin(&evt);
                break;
            case HEALTH_EVT_BLE_LEAK_CHECKIN:
                handle_ble_leak_checkin(&evt);
                break;
            case HEALTH_EVT_VALVE_CONNECTED:
                handle_valve_event(true);
                break;
            case HEALTH_EVT_VALVE_DISCONNECTED:
                handle_valve_event(false);
                break;
            case HEALTH_EVT_TICK:
                evaluate_timeouts();
                break;
        }

        recalc_system_rating();

        xSemaphoreGive(s_mutex);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void health_engine_reload_devices(void)
{
    bool have_mutex = (s_mutex != NULL);
    if (have_mutex) xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000));

    // Clear all entries
    memset(s_devices, 0, sizeof(s_devices));
    int idx = 0;

    // Valve
    char valve_mac[18];
    if (provisioning_get_valve_mac(valve_mac)) {
        s_devices[idx].in_use   = true;
        s_devices[idx].dev_type = HEALTH_DEV_VALVE;
        strncpy(s_devices[idx].dev_id, valve_mac, sizeof(s_devices[idx].dev_id) - 1);
        s_devices[idx].rating      = HEALTH_CRITICAL;  // Until connected
        s_devices[idx].prev_rating = HEALTH_CRITICAL;
        s_devices[idx].last_battery = 0xFF;
        idx++;
    }

    // LoRa sensors
    uint32_t lora_ids[MAX_LORA_SENSORS];
    uint8_t lora_count = 0;
    if (provisioning_get_lora_sensors(lora_ids, &lora_count)) {
        for (int i = 0; i < lora_count && idx < HEALTH_MAX_DEVICES; i++) {
            s_devices[idx].in_use   = true;
            s_devices[idx].dev_type = HEALTH_DEV_LORA;
            snprintf(s_devices[idx].dev_id, sizeof(s_devices[idx].dev_id),
                     "0x%08lX", (unsigned long)lora_ids[i]);
            s_devices[idx].rating      = HEALTH_CRITICAL;  // Until first packet
            s_devices[idx].prev_rating = HEALTH_CRITICAL;
            s_devices[idx].last_battery = 0xFF;
            idx++;
        }
    }

    // BLE leak sensors
    char ble_macs[MAX_BLE_LEAK_SENSORS][18];
    uint8_t ble_count = 0;
    if (provisioning_get_ble_leak_sensors(ble_macs, &ble_count)) {
        for (int i = 0; i < ble_count && idx < HEALTH_MAX_DEVICES; i++) {
            s_devices[idx].in_use   = true;
            s_devices[idx].dev_type = HEALTH_DEV_BLE_LEAK;
            strncpy(s_devices[idx].dev_id, ble_macs[i], sizeof(s_devices[idx].dev_id) - 1);
            s_devices[idx].rating      = HEALTH_CRITICAL;  // Until first advertisement
            s_devices[idx].prev_rating = HEALTH_CRITICAL;
            s_devices[idx].last_battery = 0xFF;
            idx++;
        }
    }

    s_boot_sync_done = false;  // Reset boot sync on reload

    ESP_LOGI(HEALTH_TAG, "Device table loaded: %d device(s)", idx);

    if (have_mutex) xSemaphoreGive(s_mutex);
}

void health_engine_init(void)
{
    if (s_initialized) return;

    s_health_queue = xQueueCreate(16, sizeof(health_event_t));
    if (!s_health_queue) {
        ESP_LOGE(HEALTH_TAG, "Failed to create health event queue");
        return;
    }

    s_alert_queue = xQueueCreate(4, sizeof(health_alert_t));
    if (!s_alert_queue) {
        ESP_LOGE(HEALTH_TAG, "Failed to create alert queue");
        return;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(HEALTH_TAG, "Failed to create mutex");
        return;
    }

    s_tick_timer = xTimerCreate("health_tick",
                                pdMS_TO_TICKS(HEALTH_TICK_INTERVAL_MS),
                                pdTRUE,   // auto-reload
                                NULL,
                                tick_timer_cb);
    if (!s_tick_timer) {
        ESP_LOGE(HEALTH_TAG, "Failed to create tick timer");
        return;
    }

    health_engine_reload_devices();
    s_boot_start_ms = now_ms();

    xTaskCreate(health_engine_task, "health_engine", 3072, NULL, 2, NULL);
    xTimerStart(s_tick_timer, 0);

    s_initialized = true;
    ESP_LOGI(HEALTH_TAG, "Initialized (tick=%ds, sensor_timeout=%ds)",
             HEALTH_TICK_INTERVAL_MS / 1000,
             HEALTH_LORA_TIMEOUT_MS / 1000);
}

bool health_post_event(const health_event_t *evt)
{
    if (!s_health_queue || !evt) return false;
    return xQueueSend(s_health_queue, evt, 0) == pdTRUE;
}

health_rating_t health_get_system_rating(void)
{
    return s_system_rating;
}

bool health_pop_alert(health_alert_t *out)
{
    if (!s_alert_queue || !out) return false;
    return xQueueReceive(s_alert_queue, out, 0) == pdTRUE;
}

char *health_alert_to_json(const health_alert_t *alert)
{
    if (!alert) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "category", "health");

    bool is_offline = (alert->new_rating == HEALTH_CRITICAL);
    cJSON_AddStringToObject(root, "event",
                            is_offline ? "device_offline" : "device_recovered");

    cJSON_AddStringToObject(root, "dev_type", dev_type_to_str(alert->dev_type));
    cJSON_AddStringToObject(root, "sensor_id", alert->dev_id);
    cJSON_AddStringToObject(root, "rating", health_rating_to_str(alert->new_rating));
    cJSON_AddStringToObject(root, "prev_rating", health_rating_to_str(alert->old_rating));

    if (alert->battery != 0xFF) {
        cJSON_AddNumberToObject(root, "battery", alert->battery);
    }
    if (alert->rssi != 0) {
        cJSON_AddNumberToObject(root, "rssi", alert->rssi);
    }
    if (alert->offline_duration_s > 0) {
        cJSON_AddNumberToObject(root, "offline_duration_s", alert->offline_duration_s);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

bool health_get_device_status_all(health_device_status_t out[HEALTH_MAX_DEVICES],
                                  uint8_t *count_out)
{
    if (!out || !count_out || !s_mutex) return false;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }

    int64_t now = now_ms();
    uint8_t count = 0;

    for (int i = 0; i < HEALTH_MAX_DEVICES; i++) {
        health_device_status_t *dst = &out[i];
        const health_device_t  *src = &s_devices[i];

        dst->in_use = src->in_use;
        if (!src->in_use) continue;

        dst->dev_type     = src->dev_type;
        memcpy(dst->dev_id, src->dev_id, sizeof(dst->dev_id));
        dst->rating       = src->rating;
        dst->ever_seen    = src->ever_seen;
        dst->last_battery = src->last_battery;
        dst->last_rssi    = src->last_rssi;

        // Compute connected status
        if (src->dev_type == HEALTH_DEV_VALVE) {
            dst->connected = src->ever_seen && (src->disconnect_ms == 0);
        } else {
            // Sensor: connected if ever_seen and within timeout
            if (!src->ever_seen || src->last_seen_ms == 0) {
                dst->connected = false;
            } else {
                uint32_t timeout = (src->dev_type == HEALTH_DEV_LORA)
                                    ? HEALTH_LORA_TIMEOUT_MS
                                    : HEALTH_BLE_LEAK_TIMEOUT_MS;
                dst->connected = ((now - src->last_seen_ms) <= timeout);
            }
        }

        // Compute last_seen_age_s
        if (!src->ever_seen || src->last_seen_ms == 0) {
            dst->last_seen_age_s = UINT32_MAX;
        } else {
            dst->last_seen_age_s = (uint32_t)((now - src->last_seen_ms) / 1000);
        }

        count++;
    }

    *count_out = count;
    xSemaphoreGive(s_mutex);
    return true;
}

bool health_is_boot_sync_complete(void)
{
    if (!s_mutex) return true;  // Fail-open if not initialized

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return true;  // Fail-open to avoid stalling iothub
    }

    bool done = s_boot_sync_done;
    xSemaphoreGive(s_mutex);
    return done;
}
