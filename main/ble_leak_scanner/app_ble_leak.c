/****************************************************
 *  MODULE:   BLE Leak Sensor Scanner
 *  PURPOSE:  Passive BLE scanner for "eleak" leak sensors.
 *            Parses manufacturer-specific advertising data
 *            (company ID 0x0030) for leak status and battery.
 *            Commissioned sensors are whitelisted by MAC from
 *            the provisioning manager (Azure C2D).
 ****************************************************/

#include "app_ble_leak.h"
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "provisioning_manager/provisioning_manager.h"
#include "health_engine/health_engine.h"

/* ---------------------------------------------------------
 * Constants
 * --------------------------------------------------------- */
#define BLE_LEAK_TAG            "BLE_LEAK"
#define ELEAK_COMPANY_ID        0x0030      // ST Microelectronics
#define ELEAK_MFG_DATA_LEN      4           // 2B company ID + 1B leak + 1B battery
#define ELEAK_DEVICE_NAME       "eleak"
#define ELEAK_DEVICE_NAME_LEN   5
#define MAX_TRACKED_SENSORS     MAX_BLE_LEAK_SENSORS
#define SCAN_RESTART_DELAY_MS   500
#define WHITELIST_RELOAD_MS     10000       // Re-check provisioning every 10s
#define BLE_LEAK_HEARTBEAT_MS   (5 * 60 * 1000)  // 5-min heartbeat for health engine

/* ---------------------------------------------------------
 * Internal types
 * --------------------------------------------------------- */
// Per-sensor state for delta/dedup tracking
typedef struct {
    uint8_t mac[6];
    uint8_t last_battery;
    bool last_leak;
    bool seen;              // true after first advertisement received
    TickType_t last_event_tick;  // for health engine heartbeat
} sensor_state_t;

/* ---------------------------------------------------------
 * Static variables
 * --------------------------------------------------------- */
QueueHandle_t ble_leak_rx_queue = NULL;

static TaskHandle_t ble_leak_task_handle = NULL;
static volatile bool s_scan_restart_needed = false;

// Cached whitelist from provisioning manager
static uint8_t s_whitelist[MAX_TRACKED_SENSORS][6];
static uint8_t s_whitelist_count = 0;

// Per-sensor tracking for dedup
static sensor_state_t s_sensors[MAX_TRACKED_SENSORS];

/* ---------------------------------------------------------
 * Helper: parse MAC string "XX:XX:XX:XX:XX:XX" to 6-byte array
 * NimBLE stores addresses LSB-first, so we reverse the byte order.
 * "00:80:E1:27:9A:E6" → [0xE6, 0x9A, 0x27, 0xE1, 0x80, 0x00]
 * --------------------------------------------------------- */
static void mac_str_to_bytes(const char *str, uint8_t *out)
{
    unsigned int b[6];
    sscanf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
           &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
    for (int i = 0; i < 6; i++) {
        out[i] = (uint8_t)b[5 - i];
    }
}

/* ---------------------------------------------------------
 * Helper: format NimBLE 6-byte MAC (LSB-first) to string "XX:XX:XX:XX:XX:XX"
 * [0xE6, 0x9A, 0x27, 0xE1, 0x80, 0x00] → "00:80:E1:27:9A:E6"
 * --------------------------------------------------------- */
static void mac_bytes_to_str(const uint8_t *mac, char *out)
{
    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
}

/* ---------------------------------------------------------
 * Reload whitelist from provisioning manager
 * --------------------------------------------------------- */
static void reload_whitelist(void)
{
    char mac_strs[MAX_BLE_LEAK_SENSORS][18];
    uint8_t count = 0;

    if (provisioning_get_ble_leak_sensors(mac_strs, &count)) {
        s_whitelist_count = count;
        for (int i = 0; i < count; i++) {
            mac_str_to_bytes(mac_strs[i], s_whitelist[i]);
        }
        ESP_LOGI(BLE_LEAK_TAG, "Whitelist reloaded: %d sensor(s)", count);
    } else {
        s_whitelist_count = 0;
    }
}

/* ---------------------------------------------------------
 * Check if a MAC is in the whitelist
 * Returns index (0..N-1) or -1 if not found
 * --------------------------------------------------------- */
static int whitelist_find(const uint8_t *mac)
{
    for (int i = 0; i < s_whitelist_count; i++) {
        if (memcmp(s_whitelist[i], mac, 6) == 0) {
            return i;
        }
    }
    return -1;
}

/* ---------------------------------------------------------
 * GAP event callback for passive scanning
 * --------------------------------------------------------- */
static int ble_leak_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

    case BLE_GAP_EVENT_DISC: {
        // Parse advertisement fields
        struct ble_hs_adv_fields fields;
        int rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                          event->disc.length_data);
        if (rc != 0) {
            break;
        }

        // Check device name matches "eleak" (case-insensitive)
        if (fields.name == NULL || fields.name_len != ELEAK_DEVICE_NAME_LEN) {
            break;
        }
        if (strncasecmp((const char *)fields.name, ELEAK_DEVICE_NAME, ELEAK_DEVICE_NAME_LEN) != 0) {
            break;
        }

        // Get advertiser MAC (NimBLE stores as addr.val[6], byte 0 = LSB)
        const uint8_t *adv_mac = event->disc.addr.val;

        // Check whitelist
        int idx = whitelist_find(adv_mac);
        if (idx < 0) {
            break;  // Not a commissioned sensor
        }

        // Verify manufacturer-specific data
        if (fields.mfg_data == NULL || fields.mfg_data_len < ELEAK_MFG_DATA_LEN) {
            break;
        }

        // Verify company ID (little-endian: 0x30, 0x00 = 0x0030)
        uint16_t company_id = (uint16_t)fields.mfg_data[0] | ((uint16_t)fields.mfg_data[1] << 8);
        if (company_id != ELEAK_COMPANY_ID) {
            break;
        }

        // Extract payload
        uint8_t leak_status = fields.mfg_data[2];
        uint8_t battery     = fields.mfg_data[3];
        bool leak = (leak_status != 0);

        // Delta check: skip if unchanged from last report (unless heartbeat due)
        sensor_state_t *s = &s_sensors[idx];
        bool data_changed = !s->seen || s->last_leak != leak || s->last_battery != battery;
        bool heartbeat_due = s->seen &&
            ((xTaskGetTickCount() - s->last_event_tick) >= pdMS_TO_TICKS(BLE_LEAK_HEARTBEAT_MS));
        if (!data_changed && !heartbeat_due) {
            break;  // No change and heartbeat not due, skip
        }

        // Update tracked state
        memcpy(s->mac, adv_mac, 6);
        s->last_leak = leak;
        s->last_battery = battery;
        s->seen = true;

        // Build event and enqueue
        ble_leak_event_t evt;
        memcpy(evt.sensor_mac, adv_mac, 6);
        mac_bytes_to_str(adv_mac, evt.sensor_mac_str);
        evt.battery = battery;
        evt.leak_detected = leak;
        evt.rssi = event->disc.rssi;

        ESP_LOGI(BLE_LEAK_TAG, "eleak %s — leak=%d batt=%d%% rssi=%d",
                 evt.sensor_mac_str, leak, battery, evt.rssi);

        xQueueSend(ble_leak_rx_queue, &evt, 0);
        s->last_event_tick = xTaskGetTickCount();

        // Health engine: sensor check-in
        health_post_ble_leak_checkin(evt.sensor_mac_str, evt.battery, evt.rssi);
        break;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGD(BLE_LEAK_TAG, "Scan complete (reason=%d)", event->disc_complete.reason);
        s_scan_restart_needed = true;
        break;

    default:
        break;
    }

    return 0;
}

/* ---------------------------------------------------------
 * Start passive BLE scan
 * --------------------------------------------------------- */
static void start_passive_scan(void)
{
    struct ble_gap_disc_params disc_params = {0};
    disc_params.passive = 1;
    disc_params.filter_duplicates = 0;  // Catch every adv for fast change detection
    disc_params.itvl = 160;             // 100ms interval
    disc_params.window = 80;            // 50ms window (50% duty)

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER,
                          &disc_params, ble_leak_gap_event, NULL);
    if (rc == 0) {
        ESP_LOGI(BLE_LEAK_TAG, "Passive scan started");
    } else if (rc == BLE_HS_EALREADY) {
        ESP_LOGD(BLE_LEAK_TAG, "Scan already active (valve scanning?), will retry");
        s_scan_restart_needed = true;
    } else {
        ESP_LOGW(BLE_LEAK_TAG, "Failed to start scan: %d, will retry", rc);
        s_scan_restart_needed = true;
    }
}

/* ---------------------------------------------------------
 * Main scanner task
 * --------------------------------------------------------- */
static void ble_leak_scan_task(void *param)
{
    (void)param;
    ESP_LOGI(BLE_LEAK_TAG, "Task started, waiting for NimBLE...");

    // Block until NimBLE is initialized
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ESP_LOGI(BLE_LEAK_TAG, "NimBLE ready, initializing scanner");

    // Load whitelist
    reload_whitelist();
    memset(s_sensors, 0, sizeof(s_sensors));

    // Initial scan start (with small delay to let valve module connect first)
    vTaskDelay(pdMS_TO_TICKS(2000));
    start_passive_scan();

    TickType_t last_whitelist_reload = xTaskGetTickCount();

    for (;;) {
        // Handle scan restart if needed
        if (s_scan_restart_needed) {
            s_scan_restart_needed = false;
            vTaskDelay(pdMS_TO_TICKS(SCAN_RESTART_DELAY_MS));
            start_passive_scan();
        }

        // Periodically reload whitelist (handles runtime commissioning)
        if ((xTaskGetTickCount() - last_whitelist_reload) >= pdMS_TO_TICKS(WHITELIST_RELOAD_MS)) {
            reload_whitelist();
            last_whitelist_reload = xTaskGetTickCount();
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ---------------------------------------------------------
 * Public API
 * --------------------------------------------------------- */
void app_ble_leak_init(void)
{
    ESP_LOGI(BLE_LEAK_TAG, "Initializing BLE leak scanner module");

    ble_leak_rx_queue = xQueueCreate(10, sizeof(ble_leak_event_t));
    if (ble_leak_rx_queue == NULL) {
        ESP_LOGE(BLE_LEAK_TAG, "Failed to create event queue");
        return;
    }

    xTaskCreate(ble_leak_scan_task, "ble_leak_scan", 3072, NULL, 4, &ble_leak_task_handle);
}

void app_ble_leak_signal_start(void)
{
    if (ble_leak_task_handle != NULL) {
        xTaskNotifyGive(ble_leak_task_handle);
    }
}

void app_ble_leak_reset_tracking(void)
{
    memset(s_sensors, 0, sizeof(s_sensors));
    ESP_LOGI(BLE_LEAK_TAG, "Sensor tracking reset");
}
