#include "app_wifi.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "rgb.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "app_iothub.h"
#include "app_ble_valve.h"

// =============================================================================
// PRIVATE GLOBALS
// =============================================================================

TaskHandle_t wifiTaskHandle = NULL;
static bool has_notified_azure = false;
static uint32_t disconnect_count = 0;
static TickType_t last_disconnect_time = 0;
static bool wifi_connected = false;
static bool watchdog_active = true;
static bool ap_mode_active = false;  // Track if AP mode is running

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

static void wifi_task(void *pvParameter);
static void wifi_watchdog_task(void *pvParameter);
static uint32_t get_watchdog_boot_count(void);
static void increment_watchdog_boot_count(void);
static void reset_watchdog_boot_count(void);
static void clear_wifi_credentials(void);  // Made accessible for AP callback

// =============================================================================
// PUBLIC FUNCTIONS
// =============================================================================

void app_wifi_start(void)
{
    ESP_LOGI(WIFI_TAG, "Starting Wi-Fi with watchdog protection");
    
    // Pre-configure Wi-Fi to safe state to avoid crashes
    esp_wifi_set_ps(WIFI_PS_NONE);  // Disable power save to reduce state conflicts
    ESP_LOGI(WIFI_TAG, "Wi-Fi power save disabled");
    
    // Start Wi-Fi manager
    wifi_manager_start();
    wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_connection_ok);
    wifi_manager_set_callback(WM_EVENT_STA_DISCONNECTED, &cb_connection_lost);
    // Note: WM_EVENT_AP_STA_CONNECTED doesn't exist in this Wi-Fi manager library
    // We handle AP mode detection in the watchdog instead
    
    // Start Wi-Fi task
    xTaskCreate(&wifi_task, "wifi_task", 4096, NULL, 5, &wifiTaskHandle);
    
    // Start watchdog to monitor Wi-Fi manager health
    xTaskCreate(&wifi_watchdog_task, "wifi_watchdog", 3072, NULL, 4, NULL);
    
    ESP_LOGI(WIFI_TAG, "Wi-Fi manager started with watchdog (timeout: %d sec, max triggers: %d)", 
             MAX_DISCONNECT_TIME_SEC, MAX_WATCHDOG_TRIGGERS);
}

void cb_connection_ok(void *pvParameter)
{
    ip_event_got_ip_t *param = (ip_event_got_ip_t *)pvParameter;
    char str_ip[16];
    esp_ip4addr_ntoa(&param->ip_info.ip, str_ip, IP4ADDR_STRLEN_MAX);

    ESP_LOGI(WIFI_TAG, "Connected! IP: %s", str_ip);

    // Reset disconnect tracking
    wifi_connected = true;
    disconnect_count = 0;
    
    // Reset watchdog boot counter on successful connection
    reset_watchdog_boot_count();

    // 1. Wake up Azure IoT Task
    if (!has_notified_azure && iothub_task_handle != NULL)
    {
        ESP_LOGI(WIFI_TAG, "Waking up Azure IoT Task...");
        xTaskNotifyGive(iothub_task_handle);
        has_notified_azure = true;
    }

    // 2. Trigger BLE Start
    ESP_LOGI(WIFI_TAG, "Triggering BLE Start...");
    app_ble_valve_signal_start();

    // 3. LED Blue (connected)
    if (ledQueue != NULL)
    {
        uint8_t msg = 'B';
        xQueueSend(ledQueue, &msg, 0);
    }
}

void cb_connection_lost(void *pvParameter)
{
    if (pvParameter != NULL)
    {
        wifi_event_sta_disconnected_t *wifi_event = (wifi_event_sta_disconnected_t *)pvParameter;
        ESP_LOGW(WIFI_TAG, "WiFi Disconnected. Reason: %d", wifi_event->reason);
        
        // Track disconnect events
        wifi_connected = false;
        disconnect_count++;
        last_disconnect_time = xTaskGetTickCount();
        
        // If reason is "no AP found", we might be in wrong location
        if (wifi_event->reason == WIFI_REASON_NO_AP_FOUND) {
            ESP_LOGW(WIFI_TAG, "Network not found (reason 201). Disconnect count: %lu", disconnect_count);
            ESP_LOGW(WIFI_TAG, "Hint: Saved network might not be available at current location");
        }
    }

    // LED Red (disconnected)
    if (ledQueue != NULL)
    {
        uint8_t msg = 'R';
        xQueueSend(ledQueue, &msg, 0);
    }
}

// =============================================================================
// PRIVATE FUNCTIONS - TASKS
// =============================================================================

static void wifi_task(void *pvParameter)
{
    (void)pvParameter;
    
    // Initial LED state (red - not connected yet)
    if (ledQueue != NULL)
    {
        uint8_t msg = 'R';
        xQueueSend(ledQueue, &msg, 0);
    }
    
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    
    vTaskDelete(NULL);
}

/**
 * Watchdog task that monitors Wi-Fi manager health
 * 
 * Behavior:
 * - Monitors Wi-Fi connection state
 * - If disconnected for MAX_DISCONNECT_TIME_SEC, triggers recovery
 * - Recovery: Clear saved credentials → Reboot → AP mode (captive portal)
 * - Prevents infinite reboot loops with boot counter
 * 
 * This solves the Wi-Fi manager crash bug without modifying library code
 */
static void wifi_watchdog_task(void *pvParameter)
{
    (void)pvParameter;
    
    ESP_LOGI(WIFI_TAG, "Watchdog: Started");
    
    // Check boot counter immediately
    uint32_t boot_count = get_watchdog_boot_count();
    
    if (boot_count > 0) {
        ESP_LOGW(WIFI_TAG, "Watchdog: Boot count is %lu (max: %d)", boot_count, MAX_WATCHDOG_TRIGGERS);
    }
    
    // Check if we've exceeded max triggers
    if (boot_count >= MAX_WATCHDOG_TRIGGERS) {
        ESP_LOGE(WIFI_TAG, "========================================");
        ESP_LOGE(WIFI_TAG, "WATCHDOG: TOO MANY TRIGGERED REBOOTS!");
        ESP_LOGE(WIFI_TAG, "Boot count: %lu (max allowed: %d)", boot_count, MAX_WATCHDOG_TRIGGERS);
        ESP_LOGE(WIFI_TAG, "========================================");
        ESP_LOGE(WIFI_TAG, "Possible causes:");
        ESP_LOGE(WIFI_TAG, "  1. Hardware issue (Wi-Fi antenna problem)");
        ESP_LOGE(WIFI_TAG, "  2. Corrupt NVS partition");
        ESP_LOGE(WIFI_TAG, "  3. Wi-Fi manager library bug");
        ESP_LOGE(WIFI_TAG, "========================================");
        ESP_LOGE(WIFI_TAG, "HALTING to prevent infinite reboot loop");
        ESP_LOGE(WIFI_TAG, "Manual intervention required:");
        ESP_LOGE(WIFI_TAG, "  1. Run: idf.py erase-flash");
        ESP_LOGE(WIFI_TAG, "  2. Then: idf.py flash monitor");
        ESP_LOGE(WIFI_TAG, "========================================");
        
        // Disable watchdog to prevent further reboots
        watchdog_active = false;
        
        // LED Purple (error state)
        if (ledQueue != NULL) {
            while (1) {
                uint8_t msg = 'M'; // Magenta/Purple
                xQueueSend(ledQueue, &msg, 0);
                vTaskDelay(pdMS_TO_TICKS(500));
                msg = 'K'; // Off
                xQueueSend(ledQueue, &msg, 0);
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
        
        // Halt here - do not delete task or reboot
        while (1) {
            vTaskDelay(portMAX_DELAY);
        }
    }
    
    // Increment boot counter (will be reset on successful connection)
    increment_watchdog_boot_count();
    
    // Give initial connection attempt some time before monitoring
    ESP_LOGI(WIFI_TAG, "Watchdog: Grace period (30 seconds) before monitoring starts");
    vTaskDelay(pdMS_TO_TICKS(30000)); // 30 seconds initial grace period
    
    ESP_LOGI(WIFI_TAG, "Watchdog: Monitoring active (checking every 10 seconds)");
    
    bool credentials_cleared_for_ap = false;  // Track if we've cleared credentials for AP mode
    
    while (1)
    {
        // Check every 10 seconds
        vTaskDelay(pdMS_TO_TICKS(10000));
        
        if (!watchdog_active) {
            continue; // Watchdog disabled due to max triggers
        }
        
        // CRITICAL: Check if we're in AP mode and clear credentials once
        // This prevents the crash when user connects to captive portal
        if (!wifi_connected && !credentials_cleared_for_ap) {
            // Check if AP mode might be active by looking at disconnect count
            // AP mode typically starts after several failed connection attempts
            if (disconnect_count >= 4) {
                ESP_LOGW(WIFI_TAG, "Watchdog: Likely in AP mode after %lu disconnects", disconnect_count);
                ESP_LOGW(WIFI_TAG, "Watchdog: Clearing credentials to prevent AP+STA crash");
                clear_wifi_credentials();
                credentials_cleared_for_ap = true;
                ESP_LOGI(WIFI_TAG, "Watchdog: Safe to use captive portal now");
            }
        }
        
        if (!wifi_connected && disconnect_count > 0)
        {
            TickType_t now = xTaskGetTickCount();
            uint32_t disconnect_duration_sec = (now - last_disconnect_time) / configTICK_RATE_HZ;
            
            ESP_LOGW(WIFI_TAG, "Watchdog: Disconnected for %lu seconds (count: %lu, threshold: %d sec)", 
                     disconnect_duration_sec, disconnect_count, MAX_DISCONNECT_TIME_SEC);
            
            // If disconnected for more than MAX_DISCONNECT_TIME_SEC
            if (disconnect_duration_sec >= MAX_DISCONNECT_TIME_SEC)
            {
                ESP_LOGE(WIFI_TAG, "========================================");
                ESP_LOGE(WIFI_TAG, "WATCHDOG TRIGGERED!");
                ESP_LOGE(WIFI_TAG, "Disconnected for %lu seconds (threshold: %d)", 
                         disconnect_duration_sec, MAX_DISCONNECT_TIME_SEC);
                ESP_LOGE(WIFI_TAG, "Disconnect events: %lu", disconnect_count);
                ESP_LOGE(WIFI_TAG, "========================================");
                ESP_LOGE(WIFI_TAG, "Recovery action:");
                ESP_LOGE(WIFI_TAG, "  1. Clearing saved Wi-Fi credentials (if not done)");
                ESP_LOGE(WIFI_TAG, "  2. Rebooting device");
                ESP_LOGE(WIFI_TAG, "  3. Will start in AP mode (captive portal)");
                ESP_LOGE(WIFI_TAG, "  4. Connect to ESP32's AP to configure Wi-Fi");
                ESP_LOGE(WIFI_TAG, "========================================");
                
                // Clear saved Wi-Fi credentials if not already done
                if (!credentials_cleared_for_ap) {
                    clear_wifi_credentials();
                }
                
                // Wait a bit for logs to flush
                vTaskDelay(pdMS_TO_TICKS(2000));
                
                // Reboot - will start in AP mode since no credentials
                ESP_LOGI(WIFI_TAG, "Watchdog: Rebooting now...");
                esp_restart();
            }
        }
        else if (wifi_connected)
        {
            // Reset tracking when connected
            if (disconnect_count > 0) {
                ESP_LOGI(WIFI_TAG, "Watchdog: Connection restored, resetting counters");
            }
            disconnect_count = 0;
            credentials_cleared_for_ap = false;  // Reset flag on successful connection
        }
    }
    
    vTaskDelete(NULL);
}

// =============================================================================
// PRIVATE FUNCTIONS - NVS BOOT COUNTER
// =============================================================================

/**
 * Get the watchdog boot counter from NVS
 * Returns 0 if not found or on error
 */
static uint32_t get_watchdog_boot_count(void)
{
    nvs_handle_t nvs_handle;
    uint32_t boot_count = 0;
    
    esp_err_t err = nvs_open(WIFI_WATCHDOG_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_get_u32(nvs_handle, WIFI_WATCHDOG_BOOT_COUNT_KEY, &boot_count);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(WIFI_TAG, "Watchdog: Failed to read boot count: %s", esp_err_to_name(err));
        }
        nvs_close(nvs_handle);
    }
    
    return boot_count;
}

/**
 * Increment the watchdog boot counter in NVS
 */
static void increment_watchdog_boot_count(void)
{
    nvs_handle_t nvs_handle;
    uint32_t boot_count = 0;
    
    esp_err_t err = nvs_open(WIFI_WATCHDOG_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        // Read current value (ignore error if not found)
        nvs_get_u32(nvs_handle, WIFI_WATCHDOG_BOOT_COUNT_KEY, &boot_count);
        
        // Increment
        boot_count++;
        
        // Write back
        err = nvs_set_u32(nvs_handle, WIFI_WATCHDOG_BOOT_COUNT_KEY, boot_count);
        if (err == ESP_OK) {
            nvs_commit(nvs_handle);
            ESP_LOGI(WIFI_TAG, "Watchdog: Boot count incremented to %lu", boot_count);
        } else {
            ESP_LOGW(WIFI_TAG, "Watchdog: Failed to write boot count: %s", esp_err_to_name(err));
        }
        
        nvs_close(nvs_handle);
    } else {
        ESP_LOGW(WIFI_TAG, "Watchdog: Failed to open NVS for boot count: %s", esp_err_to_name(err));
    }
}

/**
 * Reset the watchdog boot counter to 0
 * Called on successful Wi-Fi connection
 */
static void reset_watchdog_boot_count(void)
{
    nvs_handle_t nvs_handle;
    
    esp_err_t err = nvs_open(WIFI_WATCHDOG_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        // Read current value to see if we need to reset
        uint32_t boot_count = 0;
        err = nvs_get_u32(nvs_handle, WIFI_WATCHDOG_BOOT_COUNT_KEY, &boot_count);
        
        if (boot_count > 0) {
            // Reset to 0
            err = nvs_set_u32(nvs_handle, WIFI_WATCHDOG_BOOT_COUNT_KEY, 0);
            if (err == ESP_OK) {
                nvs_commit(nvs_handle);
                ESP_LOGI(WIFI_TAG, "Watchdog: Boot count reset (was %lu)", boot_count);
            }
        }
        
        nvs_close(nvs_handle);
    }
}

// =============================================================================
// PRIVATE FUNCTIONS - Wi-Fi CREDENTIALS MANAGEMENT
// =============================================================================

/**
 * Clear saved Wi-Fi credentials from NVS
 * This forces the device to start in AP mode on next boot
 */
static void clear_wifi_credentials(void)
{
    nvs_handle_t nvs_handle;
    
    // The Wi-Fi manager stores credentials in "espwifimgr" namespace
    // Try to open and erase it
    esp_err_t err = nvs_open("espwifimgr", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_erase_all(nvs_handle);
        if (err == ESP_OK) {
            nvs_commit(nvs_handle);
            ESP_LOGI(WIFI_TAG, "Watchdog: Wi-Fi credentials cleared successfully");
        } else {
            ESP_LOGW(WIFI_TAG, "Watchdog: Failed to erase Wi-Fi credentials: %s", 
                     esp_err_to_name(err));
        }
        nvs_close(nvs_handle);
    } else {
        ESP_LOGW(WIFI_TAG, "Watchdog: Failed to open Wi-Fi NVS namespace: %s", 
                 esp_err_to_name(err));
        ESP_LOGW(WIFI_TAG, "Watchdog: Credentials may not exist or already cleared");
    }
    
    // Also try alternative common namespaces used by different Wi-Fi managers
    const char *alt_namespaces[] = {"wifi", "wifi_config", "wifi_manager"};
    for (int i = 0; i < 3; i++) {
        err = nvs_open(alt_namespaces[i], NVS_READWRITE, &nvs_handle);
        if (err == ESP_OK) {
            nvs_erase_all(nvs_handle);
            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);
            ESP_LOGI(WIFI_TAG, "Watchdog: Also cleared namespace '%s'", alt_namespaces[i]);
        }
    }
}