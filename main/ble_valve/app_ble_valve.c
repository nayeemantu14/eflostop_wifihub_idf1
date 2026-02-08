#include "app_ble_valve.h"
#include "ble_leak_scanner/app_ble_leak.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "host/ble_store.h"
#include "host/ble_sm.h"

#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "os/os_mbuf.h"

#define BLE_TAG "BLE_VALVE"
#define VALVE_DEVICE_NAME "eFlofStopV2"

// -----------------------------------------------------------------------------
// SECURITY CONFIGURATION
// -----------------------------------------------------------------------------
// Fixed passkey matching STM32WB valve (CFG_FIXED_PIN = 222900)
#define BLE_VALVE_FIXED_PASSKEY  222900

// Timeout for overall connection setup (discovery + pairing + reads)
#define SECURITY_TIMEOUT_MS 60000

// Discovery timeout
#define DISCOVERY_TIMEOUT_MS 30000

// Small delay after connection before starting security (allow link to stabilize)
#define POST_CONNECT_SECURITY_DELAY_MS 1000

// Retry delay if initial security attempt fails
#define SECURITY_RETRY_DELAY_MS 2000

// Maximum security initiation retries
#define MAX_SECURITY_RETRIES 3

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
// ATT ERROR CODES
// -----------------------------------------------------------------------------
#define ATT_ERR_INSUFFICIENT_AUTHEN  0x05
#define ATT_ERR_INSUFFICIENT_ENC     0x0F
#define BLE_HS_ATT_ERR(att_err)      (0x100 + (att_err))

// -----------------------------------------------------------------------------
// GLOBALS
// -----------------------------------------------------------------------------
static QueueHandle_t ble_cmd_queue = NULL;
QueueHandle_t ble_update_queue = NULL;

static TaskHandle_t ble_starter_task_handle = NULL;

// Event group for thread-safe state synchronization
static EventGroupHandle_t ble_state_event_group = NULL;

// Mutex for serializing GATT operations
static SemaphoreHandle_t gatt_mutex = NULL;

static uint16_t valve_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool is_scanning = false;
static bool g_ble_synced = false;
static bool g_connect_requested = false;

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
static TimerHandle_t discovery_timeout_timer = NULL;
static TimerHandle_t post_connect_timer = NULL;
static TimerHandle_t security_retry_timer = NULL;

static int g_security_retry_count = 0;

// Flag to suppress updates during initial setup (avoid publishing partial data)
static bool g_setup_in_progress = false;

// Provisioning support - target MAC filtering
static char g_target_valve_mac[18] = {0};
static bool g_has_target_mac = false;

// Forward declarations
static int ble_gap_event(struct ble_gap_event *event, void *arg);
static void start_scan(void);
static void start_discovery_chain(void);
static void sec_timeout_cb(TimerHandle_t xTimer);
static void discovery_timeout_cb(TimerHandle_t xTimer);
static void post_connect_timer_cb(TimerHandle_t xTimer);
static void security_retry_timer_cb(TimerHandle_t xTimer);
static void initiate_security(void);

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

static const char* state_bits_to_str(EventBits_t bits)
{
    static char buf[128];
    snprintf(buf, sizeof(buf), "CONN=%d PAIR=%d ENC=%d AUTH=%d BOND=%d DISC=%d",
             (bits & BLE_STATE_BIT_CONNECTED) ? 1 : 0,
             (bits & BLE_STATE_BIT_PAIRING) ? 1 : 0,
             (bits & BLE_STATE_BIT_ENCRYPTED) ? 1 : 0,
             (bits & BLE_STATE_BIT_AUTHENTICATED) ? 1 : 0,
             (bits & BLE_STATE_BIT_BONDED) ? 1 : 0,
             (bits & BLE_STATE_BIT_DISCOVERY_DONE) ? 1 : 0);
    return buf;
}

// -----------------------------------------------------------------------------
// EVENT GROUP HELPERS
// -----------------------------------------------------------------------------
static void set_state_bit(EventBits_t bit)
{
    if (ble_state_event_group != NULL)
    {
        xEventGroupSetBits(ble_state_event_group, bit);
        ESP_LOGI(BLE_TAG, "[STATE] Set bit 0x%02X -> %s", (unsigned)bit, state_bits_to_str(xEventGroupGetBits(ble_state_event_group)));
    }
}

static void clear_state_bit(EventBits_t bit)
{
    if (ble_state_event_group != NULL)
    {
        xEventGroupClearBits(ble_state_event_group, bit);
        ESP_LOGI(BLE_TAG, "[STATE] Clear bit 0x%02X -> %s", (unsigned)bit, state_bits_to_str(xEventGroupGetBits(ble_state_event_group)));
    }
}

static void clear_all_state_bits(void)
{
    if (ble_state_event_group != NULL)
    {
        xEventGroupClearBits(ble_state_event_group,
            BLE_STATE_BIT_CONNECTED | BLE_STATE_BIT_PAIRING | BLE_STATE_BIT_ENCRYPTED |
            BLE_STATE_BIT_AUTHENTICATED | BLE_STATE_BIT_BONDED | BLE_STATE_BIT_DISCOVERY_DONE);
        ESP_LOGI(BLE_TAG, "[STATE] All bits cleared");
    }
}

static EventBits_t get_state_bits(void)
{
    if (ble_state_event_group != NULL)
    {
        return xEventGroupGetBits(ble_state_event_group);
    }
    return 0;
}

static bool is_link_encrypted(void)
{
    return (get_state_bits() & BLE_STATE_BIT_ENCRYPTED) != 0;
}

static bool is_ready_for_gatt(void)
{
    EventBits_t bits = get_state_bits();
    return (bits & BLE_STATE_BIT_READY_FOR_GATT) == BLE_STATE_BIT_READY_FOR_GATT;
}

// -----------------------------------------------------------------------------
// SECURITY INITIATION
// Called after connection to start the pairing/encryption process
// -----------------------------------------------------------------------------
static void initiate_security(void)
{
    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(BLE_TAG, "[SECURITY] Cannot initiate - no connection");
        return;
    }

    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(valve_conn_handle, &desc);
    if (rc != 0)
    {
        ESP_LOGE(BLE_TAG, "[SECURITY] Cannot find connection: rc=%d", rc);
        return;
    }

    ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(BLE_TAG, "║            SECURITY INITIATION (attempt %d/%d)                ║",
             g_security_retry_count + 1, MAX_SECURITY_RETRIES);
    ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");

    ESP_LOGI(BLE_TAG, "[SECURITY] Connection handle: %d", valve_conn_handle);
    ESP_LOGI(BLE_TAG, "[SECURITY] Role: %s", (desc.role == BLE_GAP_ROLE_MASTER) ? "MASTER/CENTRAL" : "SLAVE/PERIPHERAL");
    ESP_LOGI(BLE_TAG, "[SECURITY] Peer addr: %02X:%02X:%02X:%02X:%02X:%02X",
             desc.peer_id_addr.val[5], desc.peer_id_addr.val[4], desc.peer_id_addr.val[3],
             desc.peer_id_addr.val[2], desc.peer_id_addr.val[1], desc.peer_id_addr.val[0]);
    ESP_LOGI(BLE_TAG, "[SECURITY] Current state: encrypted=%d, authenticated=%d, bonded=%d, key_size=%d",
             desc.sec_state.encrypted, desc.sec_state.authenticated,
             desc.sec_state.bonded, desc.sec_state.key_size);

    // If already encrypted (reconnection with bonded device), proceed to discovery
    if (desc.sec_state.encrypted)
    {
        ESP_LOGI(BLE_TAG, "[SECURITY] Already encrypted (bonded reconnection)");

        set_state_bit(BLE_STATE_BIT_ENCRYPTED);

        if (desc.sec_state.authenticated)
            set_state_bit(BLE_STATE_BIT_AUTHENTICATED);
        if (desc.sec_state.bonded)
            set_state_bit(BLE_STATE_BIT_BONDED);

        if (sec_timeout_timer)
            xTimerStop(sec_timeout_timer, 0);

        ESP_LOGI(BLE_TAG, "[SECURITY] Proceeding to service discovery...");
        start_discovery_chain();
        return;
    }

    // Not encrypted - need to initiate pairing
    ESP_LOGI(BLE_TAG, "[SECURITY] Link not encrypted. Initiating pairing...");
    set_state_bit(BLE_STATE_BIT_PAIRING);

    if (sec_timeout_timer)
        xTimerReset(sec_timeout_timer, 0);

    ESP_LOGI(BLE_TAG, "[SECURITY] SM Config: io_cap=%d, bonding=%d, mitm=%d, sc=%d",
             ble_hs_cfg.sm_io_cap, ble_hs_cfg.sm_bonding,
             ble_hs_cfg.sm_mitm, ble_hs_cfg.sm_sc);

    ESP_LOGI(BLE_TAG, "[SECURITY] Calling ble_gap_security_initiate(handle=%d)...", valve_conn_handle);
    rc = ble_gap_security_initiate(valve_conn_handle);

    if (rc == 0)
    {
        ESP_LOGI(BLE_TAG, "[SECURITY] ble_gap_security_initiate() SUCCESS - pairing started");
        return;
    }

    const char *err_str = "UNKNOWN";
    switch (rc) {
        case BLE_HS_EAGAIN: err_str = "BLE_HS_EAGAIN (busy)"; break;
        case BLE_HS_EALREADY: err_str = "BLE_HS_EALREADY (in progress)"; break;
        case BLE_HS_ENOTCONN: err_str = "BLE_HS_ENOTCONN (not connected)"; break;
        case BLE_HS_ENOTSUP: err_str = "BLE_HS_ENOTSUP (not supported)"; break;
    }
    ESP_LOGE(BLE_TAG, "[SECURITY] ble_gap_security_initiate() FAILED: rc=%d (%s)", rc, err_str);

    // Try ble_gap_pair_initiate as fallback
    ESP_LOGI(BLE_TAG, "[SECURITY] Trying ble_gap_pair_initiate() as fallback...");
    rc = ble_gap_pair_initiate(valve_conn_handle);

    if (rc == 0)
    {
        ESP_LOGI(BLE_TAG, "[SECURITY] ble_gap_pair_initiate() SUCCESS - pairing started");
        return;
    }

    ESP_LOGE(BLE_TAG, "[SECURITY] ble_gap_pair_initiate() FAILED: rc=%d", rc);
    clear_state_bit(BLE_STATE_BIT_PAIRING);

    // Schedule retry if not exceeded max
    g_security_retry_count++;
    if (g_security_retry_count < MAX_SECURITY_RETRIES && security_retry_timer != NULL)
    {
        ESP_LOGW(BLE_TAG, "[SECURITY] Scheduling retry %d/%d in %d ms...",
                 g_security_retry_count, MAX_SECURITY_RETRIES, SECURITY_RETRY_DELAY_MS);
        xTimerStart(security_retry_timer, 0);
    }
    else
    {
        ESP_LOGE(BLE_TAG, "[SECURITY] Max retries exceeded. Starting discovery anyway...");
        start_discovery_chain();
    }
}

// -----------------------------------------------------------------------------
// TIMER CALLBACKS
// -----------------------------------------------------------------------------
static void security_retry_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;

    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(BLE_TAG, "[SECURITY_RETRY] Connection lost");
        return;
    }

    if (is_link_encrypted())
    {
        ESP_LOGI(BLE_TAG, "[SECURITY_RETRY] Already encrypted, skipping retry");
        return;
    }

    ESP_LOGI(BLE_TAG, "[SECURITY_RETRY] Retrying security initiation...");
    initiate_security();
}

static void post_connect_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;

    ESP_LOGI(BLE_TAG, "[TIMER] Post-connect delay complete.");

    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(BLE_TAG, "[TIMER] Connection lost");
        return;
    }

    g_security_retry_count = 0;
    ESP_LOGI(BLE_TAG, "[TIMER] Initiating security...");
    initiate_security();
}

static void sec_timeout_cb(TimerHandle_t xTimer)
{
    (void)xTimer;

    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
        return;

    if (!is_ready_for_gatt())
    {
        ESP_LOGE(BLE_TAG, "[TIMEOUT] Setup incomplete. State: %s", state_bits_to_str(get_state_bits()));
        ESP_LOGE(BLE_TAG, "[TIMEOUT] Disconnecting to retry...");
        ble_gap_terminate(valve_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static void discovery_timeout_cb(TimerHandle_t xTimer)
{
    (void)xTimer;

    if (valve_conn_handle != BLE_HS_CONN_HANDLE_NONE && !is_ready_for_gatt())
    {
        ESP_LOGE(BLE_TAG, "[TIMEOUT] Discovery timeout. Disconnecting...");
        ble_gap_terminate(valve_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
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

    ESP_LOGI(BLE_TAG, "[NOTIFY] attr_handle=%u, len=%u", attr_handle, len);
    print_hex_dump("Notify data", data, len);

    if (attr_handle == h_valve_char)
    {
        int old_state = g_val_state;
        g_val_state = data[0];
        ESP_LOGI(BLE_TAG, "[DATA] Valve State=%d (%s)", g_val_state, g_val_state ? "OPEN" : "CLOSED");
        if (old_state != g_val_state && !g_setup_in_progress)
            notify_hub_update(BLE_UPD_STATE);
    }
    else if (attr_handle == h_flood_char)
    {
        bool old_leak = g_val_leak;
        g_val_leak = (data[0] != 0);
        ESP_LOGI(BLE_TAG, "[DATA] Leak=%d (%s)", g_val_leak, g_val_leak ? "LEAK" : "OK");
        if (old_leak != g_val_leak && !g_setup_in_progress)
            notify_hub_update(BLE_UPD_LEAK);
    }
    else if (attr_handle == h_batt_char)
    {
        uint8_t old_batt = g_val_battery;
        g_val_battery = data[0];
        ESP_LOGI(BLE_TAG, "[DATA] Battery=%u%%", g_val_battery);
        if (old_batt != g_val_battery && !g_setup_in_progress)
            notify_hub_update(BLE_UPD_BATTERY);
    }
    else
    {
        ESP_LOGW(BLE_TAG, "[NOTIFY] Unknown attr_handle=%u", attr_handle);
    }

    return 0;
}

// -----------------------------------------------------------------------------
// SEQUENTIAL SETUP (subscribing to notifications and reading initial values)
// -----------------------------------------------------------------------------
static int setup_step = 0;
static void setup_next_step(void);

static int on_cccd_write_cb(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr,
                            void *arg)
{
    (void)attr;
    uint16_t chr_val_handle = (uint16_t)(uintptr_t)arg;

    if (error->status == 0)
    {
        ESP_LOGI(BLE_TAG, "[SETUP] CCCD enabled for chr val handle=%u", chr_val_handle);
    }
    else
    {
        ESP_LOGW(BLE_TAG, "[SETUP] CCCD enable failed for chr=%u status=0x%04X", chr_val_handle, error->status);

        if (error->status == BLE_HS_ATT_ERR(ATT_ERR_INSUFFICIENT_AUTHEN) ||
            error->status == BLE_HS_ATT_ERR(ATT_ERR_INSUFFICIENT_ENC))
        {
            if (!is_link_encrypted() && conn_handle != BLE_HS_CONN_HANDLE_NONE)
            {
                ESP_LOGI(BLE_TAG, "[SETUP] Auth error - triggering security...");
                g_security_retry_count = 0;
                initiate_security();
                return 0;
            }
        }
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
        ESP_LOGI(BLE_TAG, "[SETUP] Descriptor: handle=%u, uuid16=0x%04X", dsc->handle, uuid16);

        if (uuid16 == BLE_GATT_DSC_CLT_CFG_UUID16)
        {
            ESP_LOGI(BLE_TAG, "[SETUP] CCCD found at handle=%u, enabling notifications", dsc->handle);
            uint8_t cccd[2] = {0x01, 0x00};
            int rc = ble_gattc_write_flat(conn_handle, dsc->handle, cccd, sizeof(cccd),
                                          on_cccd_write_cb, (void *)(uintptr_t)chr_val_handle);
            if (rc != 0)
            {
                ESP_LOGE(BLE_TAG, "[SETUP] CCCD write start failed rc=%d", rc);
                setup_next_step();
            }
            return BLE_HS_EDONE;
        }
        return 0;
    }

    if (error->status == BLE_HS_EDONE)
    {
        ESP_LOGW(BLE_TAG, "[SETUP] No CCCD found for chr=%u", chr_val_handle);
    }
    else
    {
        ESP_LOGE(BLE_TAG, "[SETUP] Descriptor discovery error status=%d", error->status);
    }

    setup_next_step();
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
        ESP_LOGI(BLE_TAG, "[SETUP] Read success: handle=%u", attr->handle);
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
        ESP_LOGW(BLE_TAG, "[SETUP] Read failed status=0x%04X", error->status);

        if (error->status == BLE_HS_ATT_ERR(ATT_ERR_INSUFFICIENT_AUTHEN) ||
            error->status == BLE_HS_ATT_ERR(ATT_ERR_INSUFFICIENT_ENC))
        {
            if (!is_link_encrypted() && conn_handle != BLE_HS_CONN_HANDLE_NONE)
            {
                ESP_LOGI(BLE_TAG, "[SETUP] Auth error on read - triggering security...");
                g_security_retry_count = 0;
                initiate_security();
                return 0;
            }
        }
    }

    setup_next_step();
    return 0;
}

static void apply_pending_valve_cmd_if_any(void)
{
    if (!is_ready_for_gatt() || valve_conn_handle == BLE_HS_CONN_HANDLE_NONE || h_valve_char == 0)
        return;

    if (g_pending_valve_cmd == 0 || g_pending_valve_cmd == 1)
    {
        uint8_t v = (uint8_t)g_pending_valve_cmd;

        ESP_LOGI(BLE_TAG, "[CMD] Applying pending valve command=%d", g_pending_valve_cmd);

        if (gatt_mutex != NULL && xSemaphoreTake(gatt_mutex, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            int rc = ble_gattc_write_flat(valve_conn_handle, h_valve_char, &v, 1, NULL, NULL);
            ESP_LOGI(BLE_TAG, "[CMD] Valve write rc=%d", rc);
            if (rc == 0)
            {
                g_val_state = v;
                notify_hub_update(BLE_UPD_STATE);
            }
            xSemaphoreGive(gatt_mutex);
        }
        g_pending_valve_cmd = -1;
    }
}

static void setup_next_step(void)
{
    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(BLE_TAG, "[SETUP] Connection lost during setup");
        return;
    }

    setup_step++;
    ESP_LOGI(BLE_TAG, "[SETUP] Step %d", setup_step);

    switch (setup_step)
    {
    case 1:
        if (h_valve_char && h_valve_svc_end)
        {
            ESP_LOGI(BLE_TAG, "[SETUP] Subscribe VALVE (chr=%u, end=%u)", h_valve_char, h_valve_svc_end);
            int rc = ble_gattc_disc_all_dscs(valve_conn_handle, h_valve_char, h_valve_svc_end, on_dsc_disc_cb, NULL);
            if (rc == 0) return;
            ESP_LOGE(BLE_TAG, "[SETUP] disc dsc valve rc=%d", rc);
        }
        setup_next_step();
        break;

    case 2:
        if (h_flood_char && h_flood_svc_end)
        {
            ESP_LOGI(BLE_TAG, "[SETUP] Subscribe FLOOD (chr=%u, end=%u)", h_flood_char, h_flood_svc_end);
            int rc = ble_gattc_disc_all_dscs(valve_conn_handle, h_flood_char, h_flood_svc_end, on_dsc_disc_cb, NULL);
            if (rc == 0) return;
            ESP_LOGE(BLE_TAG, "[SETUP] disc dsc flood rc=%d", rc);
        }
        setup_next_step();
        break;

    case 3:
        if (h_batt_char && h_batt_svc_end)
        {
            ESP_LOGI(BLE_TAG, "[SETUP] Subscribe BATT (chr=%u, end=%u)", h_batt_char, h_batt_svc_end);
            int rc = ble_gattc_disc_all_dscs(valve_conn_handle, h_batt_char, h_batt_svc_end, on_dsc_disc_cb, NULL);
            if (rc == 0) return;
            ESP_LOGE(BLE_TAG, "[SETUP] disc dsc batt rc=%d", rc);
        }
        setup_next_step();
        break;

    case 4:
        if (h_valve_char)
        {
            ESP_LOGI(BLE_TAG, "[SETUP] Read VALVE");
            int rc = ble_gattc_read(valve_conn_handle, h_valve_char, on_read_cb, NULL);
            if (rc == 0) return;
            ESP_LOGE(BLE_TAG, "[SETUP] read valve rc=%d", rc);
        }
        setup_next_step();
        break;

    case 5:
        if (h_flood_char)
        {
            ESP_LOGI(BLE_TAG, "[SETUP] Read FLOOD");
            int rc = ble_gattc_read(valve_conn_handle, h_flood_char, on_read_cb, NULL);
            if (rc == 0) return;
            ESP_LOGE(BLE_TAG, "[SETUP] read flood rc=%d", rc);
        }
        setup_next_step();
        break;

    case 6:
        if (h_batt_char)
        {
            ESP_LOGI(BLE_TAG, "[SETUP] Read BATT");
            int rc = ble_gattc_read(valve_conn_handle, h_batt_char, on_read_cb, NULL);
            if (rc == 0) return;
            ESP_LOGE(BLE_TAG, "[SETUP] read batt rc=%d", rc);
        }
        setup_next_step();
        break;

    default:
        // Done with setup
        g_security_retry_count = 0;

        if (discovery_timeout_timer)
            xTimerStop(discovery_timeout_timer, 0);
        if (sec_timeout_timer)
            xTimerStop(sec_timeout_timer, 0);

        set_state_bit(BLE_STATE_BIT_DISCOVERY_DONE);

        ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
        ESP_LOGI(BLE_TAG, "║            SETUP COMPLETE - READY FOR GATT                   ║");
        ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");
        ESP_LOGI(BLE_TAG, "[READY] Valve=%u, Flood=%u, Batt=%u", h_valve_char, h_flood_char, h_batt_char);
        ESP_LOGI(BLE_TAG, "[READY] Battery=%u%%, Leak=%s, Valve=%s",
                 g_val_battery,
                 g_val_leak ? "LEAK" : "OK",
                 g_val_state == 1 ? "OPEN" : (g_val_state == 0 ? "CLOSED" : "UNKNOWN"));
        ESP_LOGI(BLE_TAG, "[READY] State: %s", state_bits_to_str(get_state_bits()));

        g_setup_in_progress = false;
        notify_hub_update(BLE_UPD_CONNECTED);
        apply_pending_valve_cmd_if_any();
        break;
    }
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
        ESP_LOGI(BLE_TAG, "[DISC] Found BATT char: val_handle=%u, props=0x%02X", chr->val_handle, chr->properties);
        setup_step = 0;
        setup_next_step();
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGW(BLE_TAG, "[DISC] Battery char not found");
        h_batt_char = 0;
        setup_step = 0;
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
        ESP_LOGI(BLE_TAG, "[DISC] Found BATT svc: start=%u, end=%u", svc->start_handle, svc->end_handle);
        ble_gattc_disc_chrs_by_uuid(conn, svc->start_handle, svc->end_handle, &UUID_CHR_BATT.u, on_disc_batt_chr, NULL);
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGW(BLE_TAG, "[DISC] Battery svc not found");
        h_batt_char = 0;
        h_batt_svc_end = 0;
        setup_step = 0;
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
        ESP_LOGI(BLE_TAG, "[DISC] Found FLOOD char: val_handle=%u, props=0x%02X", chr->val_handle, chr->properties);
        ble_gattc_disc_svc_by_uuid(conn, &UUID_SVC_BATT.u, on_disc_batt_svc, NULL);
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGE(BLE_TAG, "[DISC] FLOOD char not found. Disconnecting.");
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
        ESP_LOGI(BLE_TAG, "[DISC] Found FLOOD svc: start=%u, end=%u", svc->start_handle, svc->end_handle);
        ble_gattc_disc_chrs_by_uuid(conn, svc->start_handle, svc->end_handle, &UUID_CHR_FLOOD.u, on_disc_flood_chr, NULL);
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGE(BLE_TAG, "[DISC] FLOOD svc not found. Disconnecting.");
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
        ESP_LOGI(BLE_TAG, "[DISC] Found VALVE char: val_handle=%u, props=0x%02X", chr->val_handle, chr->properties);
        ble_gattc_disc_svc_by_uuid(conn, &UUID_SVC_FLOOD.u, on_disc_flood_svc, NULL);
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGE(BLE_TAG, "[DISC] VALVE char not found. Disconnecting.");
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
        ESP_LOGI(BLE_TAG, "[DISC] Found VALVE svc: start=%u, end=%u", svc->start_handle, svc->end_handle);
        ble_gattc_disc_chrs_by_uuid(conn, svc->start_handle, svc->end_handle, &UUID_CHR_VALVE.u, on_disc_valve_chr, NULL);
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGE(BLE_TAG, "[DISC] VALVE svc not found. Disconnecting.");
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }

    return 0;
}

static void start_discovery_chain(void)
{
    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(BLE_TAG, "[DISC] Cannot start discovery - no connection");
        return;
    }

    ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(BLE_TAG, "║            STARTING SERVICE DISCOVERY                        ║");
    ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");

    g_setup_in_progress = true;
    setup_step = 0;

    h_valve_char = 0;
    h_flood_char = 0;
    h_batt_char = 0;
    h_valve_svc_end = 0;
    h_flood_svc_end = 0;
    h_batt_svc_end = 0;

    if (discovery_timeout_timer)
        xTimerReset(discovery_timeout_timer, 0);

    ble_gattc_disc_svc_by_uuid(valve_conn_handle, &UUID_SVC_VALVE.u, on_disc_valve_svc, NULL);
}

// -----------------------------------------------------------------------------
// GAP EVENTS
// -----------------------------------------------------------------------------
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
    {
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0)
            return 0;

        char discovered_mac[18];
        snprintf(discovered_mac, sizeof(discovered_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 event->disc.addr.val[5], event->disc.addr.val[4], event->disc.addr.val[3],
                 event->disc.addr.val[2], event->disc.addr.val[1], event->disc.addr.val[0]);

        bool mac_match = false;
        if (g_has_target_mac && strcasecmp(discovered_mac, g_target_valve_mac) == 0)
        {
            mac_match = true;
            ESP_LOGI(BLE_TAG, "[SCAN] Target MAC matched: %s", discovered_mac);
        }

        bool name_match = false;
        if (fields.name &&
            fields.name_len == strlen(VALVE_DEVICE_NAME) &&
            strncmp((const char *)fields.name, VALVE_DEVICE_NAME, fields.name_len) == 0)
        {
            name_match = true;
        }

        if ((g_has_target_mac && mac_match) || (!g_has_target_mac && name_match))
        {
            if (g_has_target_mac)
                ESP_LOGI(BLE_TAG, "[SCAN] Connecting to provisioned valve: %s", discovered_mac);
            else
                ESP_LOGI(BLE_TAG, "[SCAN] Connecting to valve by name: %s", VALVE_DEVICE_NAME);

            memcpy(&g_peer_addr, &event->disc.addr, sizeof(ble_addr_t));
            g_peer_addr_valid = true;

            ble_gap_disc_cancel();
            is_scanning = false;

            rc = ble_gap_connect(g_own_addr_type, &event->disc.addr, 30000, NULL, ble_gap_event, NULL);
            if (rc != 0)
            {
                ESP_LOGE(BLE_TAG, "[SCAN] ble_gap_connect rc=%d", rc);
                start_scan();
            }
        }
        return 0;
    }

    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
        ESP_LOGI(BLE_TAG, "║            GAP CONNECT EVENT                                 ║");
        ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");
        ESP_LOGI(BLE_TAG, "[CONNECT] status=%d", event->connect.status);

        if (event->connect.status == 0)
        {
            valve_conn_handle = event->connect.conn_handle;
            is_scanning = false;

            clear_all_state_bits();

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
                ESP_LOGI(BLE_TAG, "[CONNECT] MAC=%s, handle=%u", g_valve_mac, valve_conn_handle);

                memcpy(&g_peer_addr, &desc.peer_id_addr, sizeof(ble_addr_t));
                g_peer_addr_valid = true;
            }

            set_state_bit(BLE_STATE_BIT_CONNECTED);

            if (post_connect_timer)
            {
                ESP_LOGI(BLE_TAG, "[CONNECT] Starting %dms delay before security...", POST_CONNECT_SECURITY_DELAY_MS);
                xTimerStart(post_connect_timer, 0);
            }
            else
            {
                start_discovery_chain();
            }
        }
        else
        {
            ESP_LOGW(BLE_TAG, "[CONNECT] Failed status=%d", event->connect.status);
            valve_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            clear_all_state_bits();
            start_scan();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
        ESP_LOGI(BLE_TAG, "║            GAP DISCONNECT EVENT                              ║");
        ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");
        ESP_LOGW(BLE_TAG, "[DISCONNECT] reason=0x%02x", event->disconnect.reason);

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

        clear_all_state_bits();
        memset(g_valve_mac, 0, sizeof(g_valve_mac));
        notify_hub_update(BLE_UPD_DISCONNECTED);

        if (sec_timeout_timer) xTimerStop(sec_timeout_timer, 0);
        if (post_connect_timer) xTimerStop(post_connect_timer, 0);
        if (discovery_timeout_timer) xTimerStop(discovery_timeout_timer, 0);
        if (security_retry_timer) xTimerStop(security_retry_timer, 0);

        if (g_connect_requested)
            start_scan();
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        return on_notify(event->notify_rx.conn_handle, event->notify_rx.attr_handle, event->notify_rx.om, NULL);

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(BLE_TAG, "[GAP] MTU updated: %u", event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(BLE_TAG, "[GAP] Connection params updated: status=%d", event->conn_update.status);
        return 0;

    case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
        ESP_LOGI(BLE_TAG, "[GAP] L2CAP update request");
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(BLE_TAG, "[GAP] Scan complete: reason=%d", event->disc_complete.reason);
        is_scanning = false;
        return 0;

    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        ESP_LOGI(BLE_TAG, "[GAP] PHY update complete");
        return 0;

#ifdef BLE_GAP_EVENT_DATA_LEN_CHG
    case BLE_GAP_EVENT_DATA_LEN_CHG:
        ESP_LOGI(BLE_TAG, "[GAP] Data length changed: tx=%d rx=%d",
                 event->data_len_chg.max_tx_octets, event->data_len_chg.max_rx_octets);
        return 0;
#endif

    // -------------------------------------------------------------------------
    // SECURITY EVENTS
    // -------------------------------------------------------------------------
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
        ESP_LOGI(BLE_TAG, "║            ENCRYPTION CHANGE EVENT                           ║");
        ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");
        ESP_LOGI(BLE_TAG, "[ENC_CHANGE] status=%d", event->enc_change.status);

        if (event->enc_change.status == 0)
        {
            if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0)
            {
                ESP_LOGI(BLE_TAG, "[ENC_CHANGE] encrypted=%d, authenticated=%d, bonded=%d, key_size=%d",
                         desc.sec_state.encrypted, desc.sec_state.authenticated,
                         desc.sec_state.bonded, desc.sec_state.key_size);

                if (desc.sec_state.encrypted)
                {
                    ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
                    ESP_LOGI(BLE_TAG, "║            LINK ENCRYPTED SUCCESSFULLY                       ║");
                    ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");

                    clear_state_bit(BLE_STATE_BIT_PAIRING);
                    set_state_bit(BLE_STATE_BIT_ENCRYPTED);

                    if (desc.sec_state.authenticated)
                    {
                        ESP_LOGI(BLE_TAG, "[ENC_CHANGE] MITM authentication achieved");
                        set_state_bit(BLE_STATE_BIT_AUTHENTICATED);
                    }

                    if (desc.sec_state.bonded)
                    {
                        ESP_LOGI(BLE_TAG, "[ENC_CHANGE] Device is bonded (keys stored)");
                        set_state_bit(BLE_STATE_BIT_BONDED);
                    }

                    if (sec_timeout_timer)
                        xTimerStop(sec_timeout_timer, 0);

                    EventBits_t bits = get_state_bits();
                    if (!(bits & BLE_STATE_BIT_DISCOVERY_DONE))
                    {
                        ESP_LOGI(BLE_TAG, "[ENC_CHANGE] Link secured. Starting discovery...");
                        start_discovery_chain();
                    }
                }
            }
        }
        else
        {
            ESP_LOGE(BLE_TAG, "[ENC_CHANGE] Encryption failed: status=%d", event->enc_change.status);
            clear_state_bit(BLE_STATE_BIT_PAIRING);

            if (valve_conn_handle != BLE_HS_CONN_HANDLE_NONE)
                ble_gap_terminate(valve_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
        ESP_LOGI(BLE_TAG, "║            PASSKEY ACTION EVENT                              ║");
        ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");
        ESP_LOGI(BLE_TAG, "[PASSKEY] action=%d", event->passkey.params.action);

        if (event->passkey.params.action == BLE_SM_IOACT_INPUT)
        {
            ESP_LOGI(BLE_TAG, "[PASSKEY] INPUT required. Responding with fixed passkey: %lu",
                     (unsigned long)BLE_VALVE_FIXED_PASSKEY);

            struct ble_sm_io pk;
            pk.action = BLE_SM_IOACT_INPUT;
            pk.passkey = BLE_VALVE_FIXED_PASSKEY;

            rc = ble_sm_inject_io(event->passkey.conn_handle, &pk);
            if (rc == 0)
                ESP_LOGI(BLE_TAG, "[PASSKEY] Passkey injected successfully");
            else
                ESP_LOGE(BLE_TAG, "[PASSKEY] ble_sm_inject_io failed: rc=%d", rc);
        }
        else if (event->passkey.params.action == BLE_SM_IOACT_DISP)
        {
            ESP_LOGI(BLE_TAG, "[PASSKEY] DISPLAY action. Our passkey: %lu",
                     (unsigned long)BLE_VALVE_FIXED_PASSKEY);

            struct ble_sm_io pk;
            pk.action = BLE_SM_IOACT_DISP;
            pk.passkey = BLE_VALVE_FIXED_PASSKEY;

            ble_sm_inject_io(event->passkey.conn_handle, &pk);
        }
        else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP)
        {
            ESP_LOGI(BLE_TAG, "[PASSKEY] Numeric comparison: %lu", (unsigned long)event->passkey.params.numcmp);

            struct ble_sm_io pk;
            pk.action = BLE_SM_IOACT_NUMCMP;
            pk.numcmp_accept = 1;

            rc = ble_sm_inject_io(event->passkey.conn_handle, &pk);
            if (rc == 0)
                ESP_LOGI(BLE_TAG, "[PASSKEY] Numeric comparison accepted");
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
        ESP_LOGI(BLE_TAG, "║            REPEAT PAIRING EVENT                              ║");
        ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");

        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc == 0)
        {
            ESP_LOGI(BLE_TAG, "[REPEAT_PAIR] Deleting old bond for peer...");
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    default:
        ESP_LOGW(BLE_TAG, "[GAP] Unhandled event: %d (0x%02X)", event->type, event->type);
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
        ESP_LOGW(BLE_TAG, "[SCAN] Not synced");
        return;
    }

    if (valve_conn_handle != BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(BLE_TAG, "[SCAN] Already connected");
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

    // Cancel any active scan (e.g. BLE leak scanner) before starting valve scan
    ble_gap_disc_cancel();

    ESP_LOGI(BLE_TAG, "[SCAN] Starting scan for '%s'...", VALVE_DEVICE_NAME);
    int rc = ble_gap_disc(g_own_addr_type, BLE_HS_FOREVER, &disc_params, ble_gap_event, NULL);
    if (rc == 0)
        is_scanning = true;
    else
        ESP_LOGE(BLE_TAG, "[SCAN] ble_gap_disc rc=%d", rc);
}

// -----------------------------------------------------------------------------
// WRITE COMMAND
// -----------------------------------------------------------------------------
static void write_valve_command(uint8_t val)
{
    if (!is_ready_for_gatt() || valve_conn_handle == BLE_HS_CONN_HANDLE_NONE || h_valve_char == 0)
    {
        ESP_LOGW(BLE_TAG, "[CMD] Valve write not ready. Queuing val=%u", val);
        g_pending_valve_cmd = (int)val;
        g_connect_requested = true;
        start_scan();
        return;
    }

    if (gatt_mutex != NULL && xSemaphoreTake(gatt_mutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        ESP_LOGI(BLE_TAG, "[CMD] Writing valve command=%u", val);
        int rc = ble_gattc_write_flat(valve_conn_handle, h_valve_char, &val, 1, NULL, NULL);
        ESP_LOGI(BLE_TAG, "[CMD] Valve write rc=%d", rc);
        if (rc == 0)
        {
            g_val_state = val;
            notify_hub_update(BLE_UPD_STATE);
        }
        xSemaphoreGive(gatt_mutex);
    }
    else
    {
        ESP_LOGW(BLE_TAG, "[CMD] Failed to acquire mutex. Queuing val=%u", val);
        g_pending_valve_cmd = (int)val;
    }
}

// -----------------------------------------------------------------------------
// NIMBLE HOST TASK
// -----------------------------------------------------------------------------
static void nimble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(BLE_TAG, "[HOST] NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void on_stack_reset(int reason)
{
    ESP_LOGE(BLE_TAG, "[HOST] NimBLE stack reset: reason=%d", reason);
    g_ble_synced = false;
    clear_all_state_bits();
}

static void on_stack_sync(void)
{
    ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(BLE_TAG, "║            NIMBLE STACK SYNCED                               ║");
    ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");

    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        ESP_LOGE(BLE_TAG, "[HOST] ble_hs_util_ensure_addr rc=%d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(BLE_TAG, "[HOST] ble_hs_id_infer_auto rc=%d", rc);
        g_own_addr_type = BLE_OWN_ADDR_PUBLIC;
    }

    uint8_t addr[6];
    ble_hs_id_copy_addr(g_own_addr_type, addr, NULL);
    ESP_LOGI(BLE_TAG, "[HOST] Own address: %02X:%02X:%02X:%02X:%02X:%02X (type=%u)",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], g_own_addr_type);

    ESP_LOGI(BLE_TAG, "[HOST] Security Manager ready");
    ESP_LOGI(BLE_TAG, "[HOST] SM Config: io_cap=%d, bonding=%d, mitm=%d, sc=%d",
             ble_hs_cfg.sm_io_cap, ble_hs_cfg.sm_bonding,
             ble_hs_cfg.sm_mitm, ble_hs_cfg.sm_sc);

    g_ble_synced = true;

    if (g_connect_requested)
        start_scan();
}

// -----------------------------------------------------------------------------
// BLE COMMAND TASK
// -----------------------------------------------------------------------------
static void ble_valve_task(void *pvParameters)
{
    (void)pvParameters;
    ble_valve_msg_t msg;

    ESP_LOGI(BLE_TAG, "[TASK] BLE command task started");

    while (1)
    {
        if (xQueueReceive(ble_cmd_queue, &msg, portMAX_DELAY) != pdTRUE)
            continue;

        switch (msg.command)
        {
        case BLE_CMD_CONNECT:
            ESP_LOGI(BLE_TAG, "[TASK] CMD: CONNECT");
            g_connect_requested = true;
            start_scan();
            break;

        case BLE_CMD_OPEN_VALVE:
            ESP_LOGI(BLE_TAG, "[TASK] CMD: OPEN_VALVE");
            write_valve_command(1);
            break;

        case BLE_CMD_CLOSE_VALVE:
            ESP_LOGI(BLE_TAG, "[TASK] CMD: CLOSE_VALVE");
            write_valve_command(0);
            break;

        case BLE_CMD_DISCONNECT:
            ESP_LOGI(BLE_TAG, "[TASK] CMD: DISCONNECT");
            g_connect_requested = false;
            if (valve_conn_handle != BLE_HS_CONN_HANDLE_NONE)
                ble_gap_terminate(valve_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            break;

        case BLE_CMD_SECURE:
            ESP_LOGI(BLE_TAG, "[TASK] CMD: SECURE");
            if (valve_conn_handle != BLE_HS_CONN_HANDLE_NONE && !is_link_encrypted())
                initiate_security();
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

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ESP_LOGI(BLE_TAG, "[INIT] Signal received. Starting BLE stack...");

    int rc = nimble_port_init();
    if (rc != ESP_OK)
    {
        ESP_LOGE(BLE_TAG, "[INIT] nimble_port_init failed: %d", rc);
        vTaskDelete(NULL);
        return;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("eFloStopHub");

    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.reset_cb = on_stack_reset;

    // -------------------------------------------------------------------------
    // SECURITY MANAGER CONFIGURATION
    // Must match STM32WB valve settings:
    //   - MITM required (passkey entry)
    //   - Bonding enabled
    //   - Secure Connections mandatory
    //   - Fixed passkey: 222900
    // -------------------------------------------------------------------------
    ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(BLE_TAG, "║            SECURITY MANAGER CONFIGURATION                    ║");
    ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_KEYBOARD_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    extern void ble_store_config_init(void);
    ble_store_config_init();
    ESP_LOGI(BLE_TAG, "[SM] Store config initialized");

    ESP_LOGI(BLE_TAG, "[SM] IO Capability: KEYBOARD_ONLY");
    ESP_LOGI(BLE_TAG, "[SM] Bonding: ENABLED");
    ESP_LOGI(BLE_TAG, "[SM] MITM: REQUIRED");
    ESP_LOGI(BLE_TAG, "[SM] Secure Connections: ENABLED");
    ESP_LOGI(BLE_TAG, "[SM] Fixed Passkey: %lu", (unsigned long)BLE_VALVE_FIXED_PASSKEY);

    // Create timers
    sec_timeout_timer = xTimerCreate("ble_sec_to", pdMS_TO_TICKS(SECURITY_TIMEOUT_MS),
                                     pdFALSE, NULL, sec_timeout_cb);
    post_connect_timer = xTimerCreate("ble_post_conn", pdMS_TO_TICKS(POST_CONNECT_SECURITY_DELAY_MS),
                                      pdFALSE, NULL, post_connect_timer_cb);
    discovery_timeout_timer = xTimerCreate("ble_disc_to", pdMS_TO_TICKS(DISCOVERY_TIMEOUT_MS),
                                           pdFALSE, NULL, discovery_timeout_cb);
    security_retry_timer = xTimerCreate("ble_sec_retry", pdMS_TO_TICKS(SECURITY_RETRY_DELAY_MS),
                                        pdFALSE, NULL, security_retry_timer_cb);

    nimble_port_freertos_init(nimble_host_task);
    xTaskCreate(ble_valve_task, "ble_valve", 4096, NULL, 5, NULL);
    ble_valve_connect();

    // Signal BLE leak scanner that NimBLE stack is ready
    app_ble_leak_signal_start();

    vTaskDelete(NULL);
}

// -----------------------------------------------------------------------------
// PUBLIC API
// -----------------------------------------------------------------------------
void app_ble_valve_init(void)
{
    ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(BLE_TAG, "║            BLE VALVE MODULE INIT                             ║");
    ESP_LOGI(BLE_TAG, "║            Event-Driven Security Model                       ║");
    ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");

    ble_state_event_group = xEventGroupCreate();
    if (ble_state_event_group == NULL)
    {
        ESP_LOGE(BLE_TAG, "[INIT] Failed to create state event group");
        return;
    }

    gatt_mutex = xSemaphoreCreateMutex();
    if (gatt_mutex == NULL)
    {
        ESP_LOGE(BLE_TAG, "[INIT] Failed to create GATT mutex");
        return;
    }

    ble_cmd_queue = xQueueCreate(10, sizeof(ble_valve_msg_t));
    if (ble_cmd_queue == NULL)
    {
        ESP_LOGE(BLE_TAG, "[INIT] Failed to create command queue");
        return;
    }

    ble_update_queue = xQueueCreate(5, sizeof(ble_update_type_t));
    if (ble_update_queue == NULL)
    {
        ESP_LOGE(BLE_TAG, "[INIT] Failed to create update queue");
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
    return is_ready_for_gatt();
}

bool ble_valve_is_secured(void)
{
    return is_link_encrypted();
}

bool ble_valve_is_authenticated(void)
{
    return (get_state_bits() & BLE_STATE_BIT_AUTHENTICATED) != 0;
}

void ble_valve_set_target_mac(const char *mac_str)
{
    if (!mac_str)
    {
        g_has_target_mac = false;
        g_target_valve_mac[0] = '\0';
        g_connect_requested = false;
        ESP_LOGI(BLE_TAG, "[API] Target MAC cleared");
        return;
    }

    strncpy(g_target_valve_mac, mac_str, sizeof(g_target_valve_mac) - 1);
    g_target_valve_mac[sizeof(g_target_valve_mac) - 1] = '\0';
    g_has_target_mac = true;

    ESP_LOGI(BLE_TAG, "[API] Target MAC set to: %s", g_target_valve_mac);
}

bool ble_valve_has_target_mac(void)
{
    return g_has_target_mac;
}

EventGroupHandle_t ble_valve_get_state_event_group(void)
{
    return ble_state_event_group;
}

void ble_valve_clear_bonds(void)
{
    ESP_LOGI(BLE_TAG, "[API] Clearing all BLE bonds...");
    int rc = ble_store_clear();
    ESP_LOGI(BLE_TAG, "[API] ble_store_clear() rc=%d", rc);
}
