/****************************************************
 *  MODULE:   BLE Leak Sensor Scanner
 *  PURPOSE:  Passive BLE scanner for "eleak" leak sensors.
 *            Uses extended scanning (ble_gap_ext_disc) to receive
 *            both legacy 1M and Coded PHY advertisements, enabling
 *            support for STM32WB (legacy) and STM32WBA (long range)
 *            leak sensors simultaneously.
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
 * Common advertisement processing for leak sensors.
 * Called from both legacy (BLE_GAP_EVENT_DISC) and extended
 * (BLE_GAP_EVENT_EXT_DISC) event handlers.
 * --------------------------------------------------------- */
static void process_leak_adv(const ble_addr_t *addr, int8_t rssi,
                             const uint8_t *data, uint8_t data_len)
{
    // Parse advertisement fields
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, data, data_len) != 0) {
        return;
    }

    // Check device name matches "eleak" (case-insensitive)
    if (fields.name == NULL || fields.name_len != ELEAK_DEVICE_NAME_LEN) {
        return;
    }
    if (strncasecmp((const char *)fields.name, ELEAK_DEVICE_NAME, ELEAK_DEVICE_NAME_LEN) != 0) {
        return;
    }

    // Get advertiser MAC (NimBLE stores as addr.val[6], byte 0 = LSB)
    const uint8_t *adv_mac = addr->val;

    // Check whitelist
    int idx = whitelist_find(adv_mac);
    if (idx < 0) {
        return;  // Not a commissioned sensor
    }

    // Verify manufacturer-specific data
    if (fields.mfg_data == NULL || fields.mfg_data_len < ELEAK_MFG_DATA_LEN) {
        return;
    }

    // Verify company ID (little-endian: 0x30, 0x00 = 0x0030)
    uint16_t company_id = (uint16_t)fields.mfg_data[0] | ((uint16_t)fields.mfg_data[1] << 8);
    if (company_id != ELEAK_COMPANY_ID) {
        return;
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
        return;  // No change and heartbeat not due, skip
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
    evt.rssi = rssi;

    ESP_LOGI(BLE_LEAK_TAG, "eleak %s — leak=%d batt=%d%% rssi=%d",
             evt.sensor_mac_str, leak, battery, evt.rssi);

    xQueueSend(ble_leak_rx_queue, &evt, 0);
    s->last_event_tick = xTaskGetTickCount();

    // Health engine: sensor check-in
    health_post_ble_leak_checkin(evt.sensor_mac_str, evt.battery, evt.rssi);
}

/* ---------------------------------------------------------
 * GAP event callback for extended scanning.
 * Handles both legacy (1M PHY) and extended (Coded PHY)
 * advertising reports from a single ble_gap_ext_disc() session.
 * --------------------------------------------------------- */
static int ble_leak_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

    case BLE_GAP_EVENT_DISC:
        // Legacy advertisement received (fallback path)
        process_leak_adv(&event->disc.addr, event->disc.rssi,
                         event->disc.data, event->disc.length_data);
        break;

#if MYNEWT_VAL(BLE_EXT_ADV)
    case BLE_GAP_EVENT_EXT_DISC: {
        const struct ble_gap_ext_disc_desc *ext = &event->ext_disc;

        // Only process complete advertising data
        if (ext->data_status != BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE) {
            break;
        }

        process_leak_adv(&ext->addr, ext->rssi, ext->data, ext->length_data);
        break;
    }
#endif

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGW(BLE_LEAK_TAG, "Scan complete (reason=%d) — will restart", event->disc_complete.reason);
        s_scan_restart_needed = true;
        break;

    default:
        break;
    }

    return 0;
}

/* ---------------------------------------------------------
 * Start passive BLE scan using extended scanning API.
 * Scans on both 1M PHY (legacy WB leak sensors) and
 * Coded PHY (long-range WBA leak sensors) simultaneously.
 * --------------------------------------------------------- */
static void start_passive_scan(void)
{
#if MYNEWT_VAL(BLE_EXT_ADV)
    // 1M PHY params — catches legacy WB leak sensors
    struct ble_gap_ext_disc_params uncoded_params = {0};
    uncoded_params.itvl = 160;      // 100ms interval
    uncoded_params.window = 80;     // 50ms window (50% duty)
    uncoded_params.passive = 1;

    // Coded PHY params — catches long-range WBA leak sensors
    struct ble_gap_ext_disc_params coded_params = {0};
    coded_params.itvl = 160;        // 100ms interval
    coded_params.window = 80;       // 50ms window (50% duty)
    coded_params.passive = 1;

    int rc = ble_gap_ext_disc(
        BLE_OWN_ADDR_PUBLIC,
        0,                          // duration: 0 = continuous
        0,                          // period: 0 = no periodic restart
        0,                          // filter_duplicates: disabled for fast change detection
        0,                          // filter_policy: accept all
        0,                          // limited: disabled
        &uncoded_params,            // 1M PHY scan params
        &coded_params,              // Coded PHY scan params
        ble_leak_gap_event,
        NULL
    );

    if (rc == 0) {
        ESP_LOGI(BLE_LEAK_TAG, "Extended passive scan started (1M + Coded PHY)");
    } else if (rc == BLE_HS_EALREADY) {
        ESP_LOGD(BLE_LEAK_TAG, "Scan already active (valve scanning?), will retry");
        s_scan_restart_needed = true;
    } else {
        ESP_LOGW(BLE_LEAK_TAG, "Failed to start ext scan: %d, will retry", rc);
        s_scan_restart_needed = true;
    }
#else
    // Fallback: legacy scanning (1M PHY only)
    struct ble_gap_disc_params disc_params = {0};
    disc_params.passive = 1;
    disc_params.filter_duplicates = 0;
    disc_params.itvl = 160;
    disc_params.window = 80;

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER,
                          &disc_params, ble_leak_gap_event, NULL);
    if (rc == 0) {
        ESP_LOGI(BLE_LEAK_TAG, "Passive scan started (1M only, ext_adv disabled)");
    } else if (rc == BLE_HS_EALREADY) {
        ESP_LOGD(BLE_LEAK_TAG, "Scan already active (valve scanning?), will retry");
        s_scan_restart_needed = true;
    } else {
        ESP_LOGW(BLE_LEAK_TAG, "Failed to start scan: %d, will retry", rc);
        s_scan_restart_needed = true;
    }
#endif
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
    TickType_t last_heartbeat_log = xTaskGetTickCount();

    for (;;) {
        // Handle scan restart if needed
        if (s_scan_restart_needed) {
            s_scan_restart_needed = false;
            vTaskDelay(pdMS_TO_TICKS(SCAN_RESTART_DELAY_MS));
            start_passive_scan();
        }
        // Self-healing: detect when our scan was cancelled externally
        // (e.g., valve module's scan/connect sequence) without a
        // BLE_GAP_EVENT_DISC_COMPLETE reaching our callback.
        else if (!ble_gap_disc_active()) {
            ESP_LOGW(BLE_LEAK_TAG, "Scan not active (external cancel?), restarting");
            start_passive_scan();
        }

        // Periodically reload whitelist (handles runtime commissioning)
        if ((xTaskGetTickCount() - last_whitelist_reload) >= pdMS_TO_TICKS(WHITELIST_RELOAD_MS)) {
            reload_whitelist();
            last_whitelist_reload = xTaskGetTickCount();
        }

        // Periodic scan-alive heartbeat (every 60s)
        if ((xTaskGetTickCount() - last_heartbeat_log) >= pdMS_TO_TICKS(60000)) {
            ESP_LOGI(BLE_LEAK_TAG, "[HEARTBEAT] Scanner alive, whitelist=%d sensors", s_whitelist_count);
            last_heartbeat_log = xTaskGetTickCount();
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

void app_ble_leak_process_adv(const void *addr, int8_t rssi,
                              const uint8_t *data, uint8_t data_len)
{
    if (ble_leak_rx_queue == NULL || s_whitelist_count == 0) {
        return;
    }
    process_leak_adv((const ble_addr_t *)addr, rssi, data, data_len);
}
