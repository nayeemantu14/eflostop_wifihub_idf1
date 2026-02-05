#include "app_ble_valve.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "host/ble_store.h"

#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "os/os_mbuf.h"

#define BLE_TAG "BLE_VALVE"
#define VALVE_DEVICE_NAME "eFlofStopV2"

// Wait for connection parameter update before starting discovery
// No pairing required - just need L2CAP negotiation
#define SECURITY_INITIATE_DELAY_MS 1000  // 1 second for L2CAP params

// -----------------------------------------------------------------------------
// UUIDS (128-bit Explicit)
// -----------------------------------------------------------------------------
static const ble_uuid128_t UUID_SVC_VALVE =
    BLE_UUID128_INIT(0x8f, 0xe5, 0xb3, 0xd5, 0x2e, 0x7f, 0x4a, 0x98, 0x2a, 0x48, 0x7a, 0xcc, 0x02, 0x00, 0x00, 0x00);
static const ble_uuid128_t UUID_CHR_VALVE =
    BLE_UUID128_INIT(0x19, 0xed, 0x82, 0xae, 0xed, 0x21, 0x4c, 0x9d, 0x41, 0x45, 0x22, 0x8e, 0x02, 0x00, 0x00, 0x00);

static const ble_uuid128_t UUID_SVC_FLOOD =
    BLE_UUID128_INIT(0x8f, 0xe5, 0xb3, 0xd5, 0x2e, 0x7f, 0x4a, 0x98, 0x2a, 0x48, 0x7a, 0xcc, 0x01, 0x00, 0x00, 0x00);
static const ble_uuid128_t UUID_CHR_FLOOD =
    BLE_UUID128_INIT(0x19, 0xed, 0x82, 0xae, 0xed, 0x21, 0x4c, 0x9d, 0x41, 0x45, 0x22, 0x8e, 0x01, 0x00, 0x00, 0x00);

static const ble_uuid128_t UUID_SVC_BATT =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x0f, 0x18, 0x00, 0x00);

static const ble_uuid128_t UUID_CHR_BATT =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x19, 0x2a, 0x00, 0x00);

// -----------------------------------------------------------------------------
// GLOBALS
// -----------------------------------------------------------------------------
static QueueHandle_t ble_cmd_queue = NULL;
QueueHandle_t ble_update_queue = NULL;

static TaskHandle_t ble_starter_task_handle = NULL;

static uint16_t valve_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool is_scanning = false;
static bool g_ble_synced = false;
static bool g_connect_requested = false;

static bool g_secured = false;
static bool g_ready = false;

static uint8_t g_own_addr_type = BLE_OWN_ADDR_PUBLIC;

static char g_valve_mac[18] = {0};

static ble_addr_t g_peer_addr;
static bool g_peer_addr_valid = false;

static uint16_t h_valve_char = 0, h_flood_char = 0, h_batt_char = 0;
static uint16_t h_valve_svc_end = 0, h_flood_svc_end = 0, h_batt_svc_end = 0;

static uint8_t g_val_battery = 0;
static bool g_val_leak = false;
static int g_val_state = -1;

static int g_pending_valve_cmd = -1;

static TimerHandle_t sec_timeout_timer = NULL;
static TimerHandle_t sec_delay_timer = NULL;

static int g_security_retry_count = 0;
#define MAX_SECURITY_RETRIES 3

// Track if we've seen connection parameter update (L2CAP negotiation complete)
static bool g_conn_params_updated = false;

// Provisioning support - target MAC filtering
static char g_target_valve_mac[18] = {0};
static bool g_has_target_mac = false;

static int ble_gap_event(struct ble_gap_event *event, void *arg);
static void start_scan(void);
static void start_discovery_chain(void);
static void sec_timeout_cb(TimerHandle_t xTimer);
static void sec_delay_cb(TimerHandle_t xTimer);

// -----------------------------------------------------------------------------
// DEBUG HELPER
// -----------------------------------------------------------------------------
static void print_hex_dump(const char *prefix, const uint8_t *data, uint16_t len)
{
    char buf[64];
    int offset = 0;
    for (uint16_t i = 0; i < len && offset < 60; i++)
    {
        offset += snprintf(buf + offset, sizeof(buf) - offset, "%02X ", data[i]);
    }
    ESP_LOGI(BLE_TAG, "%s [%u bytes]: %s", prefix, len, buf);
}

// -----------------------------------------------------------------------------
// UPDATE NOTIFY
// -----------------------------------------------------------------------------
static void notify_hub_update(ble_update_type_t update_type)
{
    if (ble_update_queue != NULL)
    {
        (void)xQueueSend(ble_update_queue, &update_type, 0);
    }
}

static int on_notify(uint16_t conn_handle, uint16_t attr_handle, struct os_mbuf *om, void *arg)
{
    (void)conn_handle;
    (void)arg;

    uint8_t data[16] = {0};
    uint16_t len = OS_MBUF_PKTLEN(om);
    if (len > sizeof(data))
        len = sizeof(data);

    os_mbuf_copydata(om, 0, len, data);

    ESP_LOGI(BLE_TAG, "on_notify: attr_handle=%u, len=%u", attr_handle, len);
    print_hex_dump("Notify data", data, len);

    if (attr_handle == h_valve_char)
    {
        int old_state = g_val_state;
        g_val_state = data[0];
        ESP_LOGI(BLE_TAG, "UPDATE: Valve State=%d (%s)", g_val_state, g_val_state ? "OPEN" : "CLOSED");
        if (old_state != g_val_state) {
            notify_hub_update(BLE_UPD_STATE);
        }
    }
    else if (attr_handle == h_flood_char)
    {
        bool old_leak = g_val_leak;
        g_val_leak = (data[0] != 0);
        ESP_LOGI(BLE_TAG, "UPDATE: Leak=%d (%s)", g_val_leak, g_val_leak ? "LEAK" : "OK");
        if (old_leak != g_val_leak) {
            notify_hub_update(BLE_UPD_LEAK);
        }
    }
    else if (attr_handle == h_batt_char)
    {
        uint8_t old_batt = g_val_battery;
        g_val_battery = data[0];
        ESP_LOGI(BLE_TAG, "UPDATE: Battery=%u%% (raw=0x%02X)", g_val_battery, g_val_battery);
        if (old_batt != g_val_battery) {
            notify_hub_update(BLE_UPD_BATTERY);
        }
    }
    else
    {
        ESP_LOGW(BLE_TAG, "Unknown attr_handle=%u", attr_handle);
    }

    return 0;
}

// -----------------------------------------------------------------------------
// SEQUENTIAL SETUP
// -----------------------------------------------------------------------------
static int setup_step = 0;
static void setup_next_step(void);

static int on_cccd_write_cb(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr,
                            void *arg)
{
    (void)conn_handle;
    (void)attr;
    uint16_t chr_val_handle = (uint16_t)(uintptr_t)arg;

    if (error->status == 0)
    {
        ESP_LOGI(BLE_TAG, "CCCD enabled for chr val handle=%u", chr_val_handle);
    }
    else
    {
        ESP_LOGW(BLE_TAG, "CCCD enable failed for chr=%u status=%d", chr_val_handle, error->status);
    }

    setup_next_step();
    return 0;
}

static int on_dsc_disc_cb(uint16_t conn_handle,
                          const struct ble_gatt_error *error,
                          uint16_t chr_val_handle,
                          const struct ble_gatt_dsc *dsc,
                          void *arg)
{
    (void)arg;

    if (error->status == 0)
    {
        uint16_t uuid16 = ble_uuid_u16(&dsc->uuid.u);
        ESP_LOGI(BLE_TAG, "  Descriptor: handle=%u, uuid16=0x%04X", dsc->handle, uuid16);

        if (uuid16 == BLE_GATT_DSC_CLT_CFG_UUID16)
        {
            ESP_LOGI(BLE_TAG, "  -> CCCD at handle=%u, enabling notifications", dsc->handle);
            uint8_t cccd[2] = {0x01, 0x00};
            int rc = ble_gattc_write_flat(conn_handle, dsc->handle, cccd, sizeof(cccd),
                                          on_cccd_write_cb, (void *)(uintptr_t)chr_val_handle);
            if (rc != 0)
            {
                ESP_LOGE(BLE_TAG, "CCCD write start failed rc=%d", rc);
                setup_next_step();
            }
            return BLE_HS_EDONE;
        }
        return 0;
    }

    if (error->status == BLE_HS_EDONE)
    {
        ESP_LOGW(BLE_TAG, "  No CCCD found for chr=%u", chr_val_handle);
        setup_next_step();
    }
    else
    {
        ESP_LOGE(BLE_TAG, "Descriptor discovery error status=%d", error->status);
        setup_next_step();
    }

    return 0;
}

static int on_read_cb(uint16_t conn_handle,
                      const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr,
                      void *arg)
{
    (void)arg;

    if (error->status == 0 && attr != NULL)
    {
        ESP_LOGI(BLE_TAG, "Read success: handle=%u", attr->handle);
        if (attr->om != NULL)
        {
            uint8_t data[16] = {0};
            uint16_t len = OS_MBUF_PKTLEN(attr->om);
            if (len > sizeof(data))
                len = sizeof(data);
            os_mbuf_copydata(attr->om, 0, len, data);
            print_hex_dump("Read data", data, len);
        }
        on_notify(conn_handle, attr->handle, attr->om, NULL);
    }
    else
    {
        ESP_LOGW(BLE_TAG, "Read failed status=%d", error->status);
    }

    setup_next_step();
    return 0;
}

static void apply_pending_valve_cmd_if_any(void)
{
    if (!g_ready || valve_conn_handle == BLE_HS_CONN_HANDLE_NONE || h_valve_char == 0)
        return;

    if (g_pending_valve_cmd == 0 || g_pending_valve_cmd == 1)
    {
        uint8_t v = (uint8_t)g_pending_valve_cmd;
        int rc = ble_gattc_write_flat(valve_conn_handle, h_valve_char, &v, 1, NULL, NULL);
        ESP_LOGI(BLE_TAG, "Applying pending valve cmd=%d rc=%d", g_pending_valve_cmd, rc);
        if (rc == 0)
        {
            g_val_state = v;
            notify_hub_update(BLE_UPD_STATE);
        }
        g_pending_valve_cmd = -1;
    }
}

static void setup_next_step(void)
{
    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(BLE_TAG, "setup_next_step: connection lost");
        return;
    }

    setup_step++;
    ESP_LOGI(BLE_TAG, "Setup step %d", setup_step);

    if (setup_step == 1)
    {
        if (h_valve_char && h_valve_svc_end)
        {
            ESP_LOGI(BLE_TAG, "Setup: subscribe VALVE (chr=%u, end=%u)", h_valve_char, h_valve_svc_end);
            int rc = ble_gattc_disc_all_dscs(valve_conn_handle, h_valve_char, h_valve_svc_end, on_dsc_disc_cb, NULL);
            if (rc != 0)
            {
                ESP_LOGE(BLE_TAG, "disc dsc valve rc=%d", rc);
                setup_next_step();
            }
            return;
        }
        setup_next_step();
        return;
    }

    if (setup_step == 2)
    {
        if (h_flood_char && h_flood_svc_end)
        {
            ESP_LOGI(BLE_TAG, "Setup: subscribe FLOOD (chr=%u, end=%u)", h_flood_char, h_flood_svc_end);
            int rc = ble_gattc_disc_all_dscs(valve_conn_handle, h_flood_char, h_flood_svc_end, on_dsc_disc_cb, NULL);
            if (rc != 0)
            {
                ESP_LOGE(BLE_TAG, "disc dsc flood rc=%d", rc);
                setup_next_step();
            }
            return;
        }
        setup_next_step();
        return;
    }

    if (setup_step == 3)
    {
        if (h_batt_char && h_batt_svc_end)
        {
            ESP_LOGI(BLE_TAG, "Setup: subscribe BATT (chr=%u, end=%u)", h_batt_char, h_batt_svc_end);
            int rc = ble_gattc_disc_all_dscs(valve_conn_handle, h_batt_char, h_batt_svc_end, on_dsc_disc_cb, NULL);
            if (rc != 0)
            {
                ESP_LOGE(BLE_TAG, "disc dsc batt rc=%d", rc);
                setup_next_step();
            }
            return;
        }
        setup_next_step();
        return;
    }

    if (setup_step == 4)
    {
        if (h_valve_char)
        {
            ESP_LOGI(BLE_TAG, "Setup: read VALVE");
            int rc = ble_gattc_read(valve_conn_handle, h_valve_char, on_read_cb, NULL);
            if (rc != 0)
            {
                ESP_LOGE(BLE_TAG, "read valve rc=%d", rc);
                setup_next_step();
            }
            return;
        }
        setup_next_step();
        return;
    }

    if (setup_step == 5)
    {
        if (h_flood_char)
        {
            ESP_LOGI(BLE_TAG, "Setup: read FLOOD");
            int rc = ble_gattc_read(valve_conn_handle, h_flood_char, on_read_cb, NULL);
            if (rc != 0)
            {
                ESP_LOGE(BLE_TAG, "read flood rc=%d", rc);
                setup_next_step();
            }
            return;
        }
        setup_next_step();
        return;
    }

    if (setup_step == 6)
    {
        if (h_batt_char)
        {
            ESP_LOGI(BLE_TAG, "Setup: read BATT");
            int rc = ble_gattc_read(valve_conn_handle, h_batt_char, on_read_cb, NULL);
            if (rc != 0)
            {
                ESP_LOGE(BLE_TAG, "read batt rc=%d", rc);
                setup_next_step();
            }
            return;
        }
        setup_next_step();
        return;
    }

    // Done
    g_ready = true;
    g_security_retry_count = 0;
    ESP_LOGI(BLE_TAG, "==== SETUP COMPLETE ====");
    ESP_LOGI(BLE_TAG, "  Valve=%u, Flood=%u, Batt=%u", h_valve_char, h_flood_char, h_batt_char);
    ESP_LOGI(BLE_TAG, "  Battery=%u%%, Leak=%s, Valve=%s",
             g_val_battery,
             g_val_leak ? "LEAK" : "OK",
             g_val_state == 1 ? "OPEN" : (g_val_state == 0 ? "CLOSED" : "UNKNOWN"));
    notify_hub_update(BLE_UPD_CONNECTED);
    apply_pending_valve_cmd_if_any();
}

// -----------------------------------------------------------------------------
// DISCOVERY CHAIN
// -----------------------------------------------------------------------------
static int on_disc_batt_chr(uint16_t conn, const struct ble_gatt_error *err,
                            const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;

    if (err->status == 0)
    {
        h_batt_char = chr->val_handle;
        ESP_LOGI(BLE_TAG, "Found BATT char: val_handle=%u, props=0x%02X", chr->val_handle, chr->properties);
        setup_step = 0;
        g_ready = false;
        setup_next_step();
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGW(BLE_TAG, "Battery char not found");
        h_batt_char = 0;
        setup_step = 0;
        g_ready = false;
        setup_next_step();
    }

    return 0;
}

static int on_disc_batt_svc(uint16_t conn, const struct ble_gatt_error *err,
                            const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;

    if (err->status == 0)
    {
        h_batt_svc_end = svc->end_handle;
        ESP_LOGI(BLE_TAG, "Found BATT svc: start=%u, end=%u", svc->start_handle, svc->end_handle);
        ble_gattc_disc_chrs_by_uuid(conn, svc->start_handle, svc->end_handle, &UUID_CHR_BATT.u, on_disc_batt_chr, NULL);
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGW(BLE_TAG, "Battery svc not found");
        h_batt_char = 0;
        h_batt_svc_end = 0;
        setup_step = 0;
        g_ready = false;
        setup_next_step();
    }

    return 0;
}

static int on_disc_flood_chr(uint16_t conn, const struct ble_gatt_error *err,
                             const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;

    if (err->status == 0)
    {
        h_flood_char = chr->val_handle;
        ESP_LOGI(BLE_TAG, "Found FLOOD char: val_handle=%u, props=0x%02X", chr->val_handle, chr->properties);
        ble_gattc_disc_svc_by_uuid(conn, &UUID_SVC_BATT.u, on_disc_batt_svc, NULL);
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGE(BLE_TAG, "FLOOD char not found. Disconnecting.");
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }

    return 0;
}

static int on_disc_flood_svc(uint16_t conn, const struct ble_gatt_error *err,
                             const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;

    if (err->status == 0)
    {
        h_flood_svc_end = svc->end_handle;
        ESP_LOGI(BLE_TAG, "Found FLOOD svc: start=%u, end=%u", svc->start_handle, svc->end_handle);
        ble_gattc_disc_chrs_by_uuid(conn, svc->start_handle, svc->end_handle, &UUID_CHR_FLOOD.u, on_disc_flood_chr, NULL);
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGE(BLE_TAG, "FLOOD svc not found. Disconnecting.");
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }

    return 0;
}

static int on_disc_valve_chr(uint16_t conn, const struct ble_gatt_error *err,
                             const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;

    if (err->status == 0)
    {
        h_valve_char = chr->val_handle;
        ESP_LOGI(BLE_TAG, "Found VALVE char: val_handle=%u, props=0x%02X", chr->val_handle, chr->properties);
        ble_gattc_disc_svc_by_uuid(conn, &UUID_SVC_FLOOD.u, on_disc_flood_svc, NULL);
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGE(BLE_TAG, "VALVE char not found. Disconnecting.");
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }

    return 0;
}

static int on_disc_valve_svc(uint16_t conn, const struct ble_gatt_error *err,
                             const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;

    if (err->status == 0)
    {
        h_valve_svc_end = svc->end_handle;
        ESP_LOGI(BLE_TAG, "Found VALVE svc: start=%u, end=%u", svc->start_handle, svc->end_handle);
        ble_gattc_disc_chrs_by_uuid(conn, svc->start_handle, svc->end_handle, &UUID_CHR_VALVE.u, on_disc_valve_chr, NULL);
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGE(BLE_TAG, "VALVE svc not found. Disconnecting.");
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }

    return 0;
}

static void start_discovery_chain(void)
{
    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
        return;

    ESP_LOGI(BLE_TAG, "==== STARTING DISCOVERY CHAIN ====");
    g_ready = false;
    setup_step = 0;

    h_valve_char = 0;
    h_flood_char = 0;
    h_batt_char = 0;
    h_valve_svc_end = 0;
    h_flood_svc_end = 0;
    h_batt_svc_end = 0;

    ble_gattc_disc_svc_by_uuid(valve_conn_handle, &UUID_SVC_VALVE.u, on_disc_valve_svc, NULL);
}

// -----------------------------------------------------------------------------
// DISCOVERY TIMEOUT
// -----------------------------------------------------------------------------
static void sec_timeout_cb(TimerHandle_t xTimer)
{
    (void)xTimer;

    if (valve_conn_handle != BLE_HS_CONN_HANDLE_NONE && !g_secured)
    {
        g_security_retry_count++;
        ESP_LOGW(BLE_TAG, "Discovery timeout (retry %d/%d)", g_security_retry_count, MAX_SECURITY_RETRIES);

        if (g_security_retry_count >= MAX_SECURITY_RETRIES)
        {
            ESP_LOGE(BLE_TAG, "Max retries reached. Will retry from scratch...");
            g_security_retry_count = 0;
        }

        ble_gap_terminate(valve_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

// -----------------------------------------------------------------------------
// CONNECTION HANDLING - NO SECURITY REQUIRED
// STM32WB firmware updated to remove pairing requirement
// -----------------------------------------------------------------------------
static void sec_delay_cb(TimerHandle_t xTimer)
{
    (void)xTimer;

    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(BLE_TAG, "sec_delay_cb: no connection");
        return;
    }

    // STM32WB no longer requires pairing - proceed directly to discovery
    ESP_LOGI(BLE_TAG, "No pairing required. Starting service discovery...");
    g_secured = true;
    
    if (sec_timeout_timer)
        xTimerStop(sec_timeout_timer, 0);
    
    start_discovery_chain();
}

// -----------------------------------------------------------------------------
// GAP EVENTS
// -----------------------------------------------------------------------------
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    struct ble_gap_conn_desc desc;

    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
    {
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0)
            return 0;

        // Build MAC string from discovered device
        char discovered_mac[18];
        snprintf(discovered_mac, sizeof(discovered_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 event->disc.addr.val[5], event->disc.addr.val[4], event->disc.addr.val[3],
                 event->disc.addr.val[2], event->disc.addr.val[1], event->disc.addr.val[0]);

        // If we have a target MAC, match by MAC (authoritative)
        bool mac_match = false;
        if (g_has_target_mac) {
            if (strcasecmp(discovered_mac, g_target_valve_mac) == 0) {
                mac_match = true;
                ESP_LOGI(BLE_TAG, "Target MAC matched: %s", discovered_mac);
            }
        }
        
        // Also check name as secondary filter (optional)
        bool name_match = false;
        if (fields.name &&
            fields.name_len == strlen(VALVE_DEVICE_NAME) &&
            strncmp((const char *)fields.name, VALVE_DEVICE_NAME, fields.name_len) == 0)
        {
            name_match = true;
        }

        // Connect if:
        // - We have target MAC and it matches, OR
        // - We don't have target MAC but name matches (backward compatibility)
        if ((g_has_target_mac && mac_match) || (!g_has_target_mac && name_match))
        {
            if (g_has_target_mac) {
                ESP_LOGI(BLE_TAG, "Connecting to provisioned valve: %s", discovered_mac);
            } else {
                ESP_LOGI(BLE_TAG, "Connecting to valve by name: %s (MAC: %s)", 
                         VALVE_DEVICE_NAME, discovered_mac);
            }

            memcpy(&g_peer_addr, &event->disc.addr, sizeof(ble_addr_t));
            g_peer_addr_valid = true;

            ble_gap_disc_cancel();
            is_scanning = false;

            int rc = ble_gap_connect(g_own_addr_type, &event->disc.addr, 30000, NULL, ble_gap_event, NULL);
            if (rc != 0)
            {
                ESP_LOGE(BLE_TAG, "ble_gap_connect rc=%d", rc);
                start_scan();
            }
        }
        return 0;
    }

    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(BLE_TAG, "BLE_GAP_EVENT_CONNECT: status=%d", event->connect.status);
        if (event->connect.status == 0)
        {
            valve_conn_handle = event->connect.conn_handle;
            is_scanning = false;

            g_secured = false;
            g_ready = false;
            g_conn_params_updated = false;  // Reset flag

            h_valve_char = 0;
            h_flood_char = 0;
            h_batt_char = 0;
            h_valve_svc_end = 0;
            h_flood_svc_end = 0;
            h_batt_svc_end = 0;
            g_val_battery = 0;
            g_val_leak = false;
            g_val_state = -1;

            if (ble_gap_conn_find(valve_conn_handle, &desc) == 0)
            {
                snprintf(g_valve_mac, sizeof(g_valve_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                         desc.peer_id_addr.val[5], desc.peer_id_addr.val[4], desc.peer_id_addr.val[3],
                         desc.peer_id_addr.val[2], desc.peer_id_addr.val[1], desc.peer_id_addr.val[0]);
                ESP_LOGI(BLE_TAG, "Connected. MAC=%s, handle=%u", g_valve_mac, valve_conn_handle);

                memcpy(&g_peer_addr, &desc.peer_id_addr, sizeof(ble_addr_t));
                g_peer_addr_valid = true;
                notify_hub_update(BLE_UPD_CONNECTED);
            }

            // Start timeout for discovery (no pairing needed)
            if (sec_timeout_timer)
            {
                xTimerReset(sec_timeout_timer, 0);
            }

            // Start delay timer for L2CAP negotiation
            if (sec_delay_timer)
            {
                ESP_LOGI(BLE_TAG, "Scheduling discovery in %d ms...", SECURITY_INITIATE_DELAY_MS);
                xTimerChangePeriod(sec_delay_timer, pdMS_TO_TICKS(SECURITY_INITIATE_DELAY_MS), 0);
                xTimerStart(sec_delay_timer, 0);
            }
        }
        else
        {
            ESP_LOGW(BLE_TAG, "Connect failed status=%d", event->connect.status);
            valve_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            start_scan();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(BLE_TAG, "BLE_GAP_EVENT_DISCONNECT: reason=0x%02x", event->disconnect.reason);
        valve_conn_handle = BLE_HS_CONN_HANDLE_NONE;

        h_valve_char = 0;
        h_flood_char = 0;
        h_batt_char = 0;
        h_valve_svc_end = 0;
        h_flood_svc_end = 0;
        h_batt_svc_end = 0;

        g_val_battery = 0;
        g_val_leak = false;
        g_val_state = -1;

        g_secured = false;
        g_ready = false;
        g_conn_params_updated = false;

        memset(g_valve_mac, 0, sizeof(g_valve_mac));
        notify_hub_update(BLE_UPD_DISCONNECTED);

        if (sec_timeout_timer)
            xTimerStop(sec_timeout_timer, 0);
        if (sec_delay_timer)
            xTimerStop(sec_delay_timer, 0);

        if (g_connect_requested)
            start_scan();
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        return on_notify(event->notify_rx.conn_handle, event->notify_rx.attr_handle, event->notify_rx.om, NULL);

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(BLE_TAG, "MTU updated: %u", event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(BLE_TAG, "Connection params updated: status=%d", event->conn_update.status);
        
        // Mark that connection parameters have been negotiated
        if (event->conn_update.status == 0)
        {
            g_conn_params_updated = true;
            ESP_LOGI(BLE_TAG, "L2CAP connection parameters negotiated");
        }
        return 0;

    case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
        ESP_LOGI(BLE_TAG, "L2CAP update request");
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(BLE_TAG, "Discovery complete: reason=%d", event->disc_complete.reason);
        is_scanning = false;
        return 0;

    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        ESP_LOGI(BLE_TAG, "PHY update complete");
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
        ESP_LOGI(BLE_TAG, "Connection update request");
        return 0;

    default:
        ESP_LOGI(BLE_TAG, "GAP event: %d", event->type);
        return 0;
    }
}

// -----------------------------------------------------------------------------
// SCANNING
// -----------------------------------------------------------------------------
static void start_scan(void)
{
    if (!g_ble_synced)
    {
        ESP_LOGW(BLE_TAG, "start_scan: not synced");
        return;
    }

    if (valve_conn_handle != BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(BLE_TAG, "start_scan: already connected");
        return;
    }

    if (is_scanning)
        return;

    struct ble_gap_disc_params disc_params = {
        .filter_duplicates = 1,
        .passive = 0,
        .itvl = 160,
        .window = 80,
    };

    ESP_LOGI(BLE_TAG, "Starting scan for '%s'...", VALVE_DEVICE_NAME);
    int rc = ble_gap_disc(g_own_addr_type, BLE_HS_FOREVER, &disc_params, ble_gap_event, NULL);
    if (rc == 0)
    {
        is_scanning = true;
    }
    else
    {
        ESP_LOGE(BLE_TAG, "ble_gap_disc rc=%d", rc);
    }
}

// -----------------------------------------------------------------------------
// WRITE COMMAND
// -----------------------------------------------------------------------------
static void write_valve_command(uint8_t val)
{
    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE || !g_ready || h_valve_char == 0)
    {
        ESP_LOGW(BLE_TAG, "Valve write not ready. Queuing val=%u", val);
        g_pending_valve_cmd = (int)val;
        g_connect_requested = true;
        start_scan();
        return;
    }

    int rc = ble_gattc_write_flat(valve_conn_handle, h_valve_char, &val, 1, NULL, NULL);
    ESP_LOGI(BLE_TAG, "Valve write val=%u rc=%d", val, rc);
    if (rc == 0)
    {
        g_val_state = val;
        notify_hub_update(BLE_UPD_STATE);
    }
}

// -----------------------------------------------------------------------------
// NIMBLE HOST TASK
// -----------------------------------------------------------------------------
static void nimble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(BLE_TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void on_stack_reset(int reason)
{
    ESP_LOGE(BLE_TAG, "NimBLE stack reset: reason=%d", reason);
    g_ble_synced = false;
}

static void on_stack_sync(void)
{
    ESP_LOGI(BLE_TAG, "NimBLE stack synced");

    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        ESP_LOGE(BLE_TAG, "ble_hs_util_ensure_addr rc=%d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(BLE_TAG, "ble_hs_id_infer_auto rc=%d", rc);
        g_own_addr_type = BLE_OWN_ADDR_PUBLIC;
    }

    uint8_t addr[6];
    ble_hs_id_copy_addr(g_own_addr_type, addr, NULL);
    ESP_LOGI(BLE_TAG, "Own address: %02X:%02X:%02X:%02X:%02X:%02X (type=%u)",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], g_own_addr_type);

    // Configure Security Manager (not used but required by stack)
    // STM32WB firmware no longer requires pairing
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;

    ble_hs_cfg.sm_our_key_dist = 0;
    ble_hs_cfg.sm_their_key_dist = 0;

    ESP_LOGI(BLE_TAG, "SM config: NO PAIRING REQUIRED");
    ESP_LOGI(BLE_TAG, "STM32WB firmware updated - open connection mode");

    g_ble_synced = true;

    if (g_connect_requested)
        start_scan();
}

// -----------------------------------------------------------------------------
// BLE COMMAND TASK (REMOVED BLE_CMD_SECURE handling)
// -----------------------------------------------------------------------------
static void ble_valve_task(void *pvParameters)
{
    (void)pvParameters;
    ble_valve_msg_t msg;

    ESP_LOGI(BLE_TAG, "BLE command task started");

    while (1)
    {
        if (xQueueReceive(ble_cmd_queue, &msg, portMAX_DELAY) != pdTRUE)
            continue;

        switch (msg.command)
        {
        case BLE_CMD_CONNECT:
            ESP_LOGI(BLE_TAG, "CMD: CONNECT");
            g_connect_requested = true;
            start_scan();
            break;

        case BLE_CMD_OPEN_VALVE:
            ESP_LOGI(BLE_TAG, "CMD: OPEN_VALVE");
            write_valve_command(1);
            break;

        case BLE_CMD_CLOSE_VALVE:
            ESP_LOGI(BLE_TAG, "CMD: CLOSE_VALVE");
            write_valve_command(0);
            break;

        case BLE_CMD_DISCONNECT:
            ESP_LOGI(BLE_TAG, "CMD: DISCONNECT");
            g_connect_requested = false;  // Stop auto-reconnection
            if (valve_conn_handle != BLE_HS_CONN_HANDLE_NONE)
                ble_gap_terminate(valve_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            break;

        default:
            break;
        }
    }
}

// -----------------------------------------------------------------------------
// STARTER TASK
// -----------------------------------------------------------------------------
static void ble_starter_task(void *param)
{
    (void)param;

    // Wait for signal from Wi-Fi task
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ESP_LOGI(BLE_TAG, "Signal received. Starting BLE stack...");

    // Initialize NimBLE
    int rc = nimble_port_init();
    if (rc != ESP_OK)
    {
        ESP_LOGE(BLE_TAG, "nimble_port_init failed: %d", rc);
        vTaskDelete(NULL);
        return;
    }

    // Initialize GAP and GATT services
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("eFloStopHub");

    // Set callbacks
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.reset_cb = on_stack_reset;

    // CRITICAL FIX: Create BOTH timers
    sec_timeout_timer = xTimerCreate("ble_sec_to", pdMS_TO_TICKS(15000), pdFALSE, NULL, sec_timeout_cb);
    if (sec_timeout_timer == NULL)
    {
        ESP_LOGE(BLE_TAG, "Failed to create security timeout timer");
    }

    sec_delay_timer = xTimerCreate("ble_sec_delay", pdMS_TO_TICKS(SECURITY_INITIATE_DELAY_MS), pdFALSE, NULL, sec_delay_cb);
    if (sec_delay_timer == NULL)
    {
        ESP_LOGE(BLE_TAG, "Failed to create security delay timer");
    }

    // Start NimBLE host task
    nimble_port_freertos_init(nimble_host_task);

    // Create command processing task
    xTaskCreate(ble_valve_task, "ble_valve", 4096, NULL, 5, NULL);

    // Trigger initial connection
    ble_valve_connect();

    // This task is done
    vTaskDelete(NULL);
}

// -----------------------------------------------------------------------------
// PUBLIC API
// -----------------------------------------------------------------------------
void app_ble_valve_init(void)
{
    ESP_LOGI(BLE_TAG, "Initializing BLE Valve module");

    ble_cmd_queue = xQueueCreate(10, sizeof(ble_valve_msg_t));
    if (ble_cmd_queue == NULL)
    {
        ESP_LOGE(BLE_TAG, "Failed to create command queue");
        return;
    }

    ble_update_queue = xQueueCreate(5, sizeof(ble_update_type_t));
    if (ble_update_queue == NULL)
    {
        ESP_LOGE(BLE_TAG, "Failed to create update queue");
        return;
    }

    xTaskCreate(ble_starter_task, "ble_starter", 3072, NULL, 5, &ble_starter_task_handle);
}

void app_ble_valve_signal_start(void)
{
    static bool is_started = false;
    if (is_started)
        return;

    if (ble_starter_task_handle != NULL)
    {
        xTaskNotifyGive(ble_starter_task_handle);
        is_started = true;
    }
}

bool ble_valve_open(void)
{
    ble_valve_msg_t m = {.command = BLE_CMD_OPEN_VALVE};
    return xQueueSend(ble_cmd_queue, &m, pdMS_TO_TICKS(10)) == pdTRUE;
}

bool ble_valve_close(void)
{
    ble_valve_msg_t m = {.command = BLE_CMD_CLOSE_VALVE};
    return xQueueSend(ble_cmd_queue, &m, pdMS_TO_TICKS(10)) == pdTRUE;
}

bool ble_valve_connect(void)
{
    ble_valve_msg_t m = {.command = BLE_CMD_CONNECT};
    return xQueueSend(ble_cmd_queue, &m, pdMS_TO_TICKS(10)) == pdTRUE;
}

bool ble_valve_disconnect(void)
{
    ble_valve_msg_t m = {.command = BLE_CMD_DISCONNECT};
    return xQueueSend(ble_cmd_queue, &m, pdMS_TO_TICKS(10)) == pdTRUE;
}

bool ble_valve_get_mac(char *b)
{
    if (b == NULL)
        return false;

    if (valve_conn_handle != BLE_HS_CONN_HANDLE_NONE && g_valve_mac[0] != 0)
    {
        strcpy(b, g_valve_mac);
        return true;
    }
    return false;
}

uint8_t ble_valve_get_battery(void)
{
    return g_val_battery;
}

bool ble_valve_get_leak(void)
{
    return g_val_leak;
}

int ble_valve_get_state(void)
{
    return g_val_state;
}

bool ble_valve_is_ready(void)
{
    return g_ready;
}

bool ble_valve_is_secured(void)
{
    return g_secured;
}

void ble_valve_set_target_mac(const char *mac_str)
{
    if (!mac_str) {
        g_has_target_mac = false;
        g_target_valve_mac[0] = '\0';
        g_connect_requested = false;  // Stop auto-reconnection
        ESP_LOGI(BLE_TAG, "Target MAC cleared");
        return;
    }
    
    strncpy(g_target_valve_mac, mac_str, sizeof(g_target_valve_mac) - 1);
    g_target_valve_mac[sizeof(g_target_valve_mac) - 1] = '\0';
    g_has_target_mac = true;
    
    ESP_LOGI(BLE_TAG, "Target MAC set to: %s", g_target_valve_mac);
}

bool ble_valve_has_target_mac(void)
{
    return g_has_target_mac;
}