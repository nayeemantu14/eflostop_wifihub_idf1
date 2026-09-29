#include "reset_button.h"
#include <string.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include "esp_system.h"
#include "nvs.h"
#include "wifi_manager.h"
#include "nvs_sync.h"   // wifi_manager's NVS mutex (in the component's src/, like wifi_manager.h)

#define TAG "RESET_BTN"

// wifi_manager's NVS namespace ("espwifimgr"). wifi_manager.c defines it with external linkage
// but wifi_manager.h does not declare it, so it is declared here rather than the string being
// repeated: the managed component stays its only definition.
extern const char wifi_manager_nvs_namespace[];

// ---------------------------------------------------------------------------
// Event types posted to the shared queue (by ISR and timer callback)
// ---------------------------------------------------------------------------
typedef enum {
    BTN_EVT_EDGE,           // GPIO interrupt fired (press or release)
    BTN_EVT_TIMER_EXPIRED   // 10-second one-shot timer fired
} btn_event_t;

// ---------------------------------------------------------------------------
// Task state machine
// ---------------------------------------------------------------------------
typedef enum {
    STATE_IDLE,
    STATE_PRESSED_PENDING,       // Button down, waiting for 10 s timer
    STATE_TRIGGERED_WAIT_RELEASE // Reset fired, waiting for release
} btn_state_t;

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
static QueueHandle_t  s_evt_queue    = NULL;
static TimerHandle_t  s_hold_timer   = NULL;
static TaskHandle_t   s_task_handle  = NULL;

#define HOLD_TIME_MS     10000   // 10 s hold to avoid accidental activation
#define DEBOUNCE_MS      50
#define EVT_QUEUE_LEN    8
#define TASK_STACK_SIZE  3072
#define TASK_PRIORITY    5

// Button is active-low (pulled high, pressed = 0)
static inline bool button_is_pressed(void)
{
    return gpio_get_level(WIFI_RESET_BUTTON_GPIO) == 0;
}

// ---------------------------------------------------------------------------
// ISR — thin: just post an edge event, nothing else
// ---------------------------------------------------------------------------
static void IRAM_ATTR button_isr_handler(void *arg)
{
    btn_event_t evt = BTN_EVT_EDGE;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_evt_queue, &evt, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// ---------------------------------------------------------------------------
// Timer callback — posts TIMER_EXPIRED to the same queue
// ---------------------------------------------------------------------------
static void hold_timer_cb(TimerHandle_t xTimer)
{
    btn_event_t evt = BTN_EVT_TIMER_EXPIRED;
    xQueueSend(s_evt_queue, &evt, 0);
}

// ---------------------------------------------------------------------------
// Erase wifi_manager's saved STA credentials straight from NVS (see execute_wifi_reset()).
// Returns with the component's NVS mutex still HELD if it was obtained: the caller reboots.
// ---------------------------------------------------------------------------
static void erase_wifi_credentials(void)
{
    /* The mutex serialises wifi_manager's own read-compare-write of the config. It is taken
     * and never given back: held through esp_restart(), it keeps any later wifi_manager save
     * (a GOT_IP, if the router comes back just then) from writing back the credentials still
     * in its RAM copy. NVS itself is thread-safe, so if the mutex is not free within 3 s (a
     * wifi_manager save whose commit failed returns without giving it) the erase goes ahead
     * without it. */
    if (!nvs_sync_lock(pdMS_TO_TICKS(3000))) {
        ESP_LOGW(TAG, "Wi-Fi NVS lock busy for 3 s - erasing without it");
    }

    /* A saved "ssid" and "password" are overwritten with zero blobs of their own sizes, the
     * state wifi_manager's own erase leaves and 2.1.3 already reads as "nothing saved":
     * wifi_manager_fetch_wifi_sta_config() reads all three keys, closes its handle and returns
     * false for the empty SSID, and LOAD_AND_RESTORE opens the portal about 0.7 s after the
     * reboot. The keys are NOT erased: "settings" (the SoftAP's own) is kept, so the namespace
     * stays, and the fetch returns at a missing key without closing its NVS handle, leaking
     * ~50 B of heap on every boot until Wi-Fi is set up. Only a key that exists is written (no
     * key is created), and zeros over the zeros wifi_manager already saved on a connected STA
     * cost no flash write: NVS skips an unchanged value. A key that cannot be written (NVS
     * full) is erased instead, which also reads as "nothing saved".
     * "ssid" first, and the password only once the SSID is cleared: a saved SSID whose password
     * is gone reads as "nothing saved" (no reconnect) yet leaves the SSID in wifi_manager's RAM
     * copy, which keeps the portal window shut (a fallback AP that keeps BLE scanning). An
     * empty SSID beside a password left behind (a failure, or power lost between the two)
     * reads as the factory state. These keys are the only copy: wifi_manager runs the driver
     * with WIFI_STORAGE_RAM.
     * Probed read-only first: NVS_READWRITE creates the namespace on a hub that never saved
     * one, so ESP_ERR_NVS_NOT_FOUND there means nothing was ever saved. */
    static const uint8_t zeros[64] = { 0 };   // .rodata (flash): the larger blob's size
    static const struct { const char *key; size_t len; } creds[] = {
        { "ssid", 32 }, { "password", 64 },   // the blob sizes wifi_manager saves and reads
    };
    nvs_handle_t h;
    int erased = 0;
    esp_err_t err = nvs_open(wifi_manager_nvs_namespace, NVS_READONLY, &h);
    if (err == ESP_OK) {
        nvs_close(h);
        err = nvs_open(wifi_manager_nvs_namespace, NVS_READWRITE, &h);
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    } else if (err == ESP_OK) {
        for (int i = 0; i < 2 && err == ESP_OK; i++) {   // stops at an SSID not cleared
            size_t len = 0;
            esp_err_t rc = nvs_get_blob(h, creds[i].key, NULL, &len);   // size only: saved?
            if (rc == ESP_ERR_NVS_NOT_FOUND) {
                continue;
            }
            rc = nvs_set_blob(h, creds[i].key, zeros, creds[i].len);
            if (rc != ESP_OK) {
                rc = nvs_erase_key(h, creds[i].key);
            }
            if (rc == ESP_OK) {
                erased++;
            } else {
                err = rc;
            }
        }
        if (erased > 0) {
            esp_err_t rc = nvs_commit(h);
            if (err == ESP_OK) {
                err = rc;
            }
        }
        nvs_close(h);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi credential erase failed (%s) - rebooting anyway",
                 esp_err_to_name(err));
    } else if (erased > 0) {
        ESP_LOGI(TAG, "Wi-Fi credentials erased from NVS");
    } else {
        ESP_LOGI(TAG, "No Wi-Fi credentials saved - nothing to erase");
    }
}

// ---------------------------------------------------------------------------
// WiFi reset action
// ---------------------------------------------------------------------------
static void execute_wifi_reset(void)
{
    ESP_LOGW(TAG, "=== LONG PRESS CONFIRMED — CLEARING WIFI CREDENTIALS ===");

    /*
     * Clear ONLY the WiFi credentials, then reboot so the hub comes up fresh in
     * AP mode for reconfiguration. Whatever the STA is doing: connected, idle on a
     * router-outage fallback portal, or already in the setup portal.
     *
     * wifi_manager_disconnect_async() comes first: with the STA connected it leaves
     * the router cleanly ("WiFi Disconnected. Reason: 8"), and wifi_manager's
     * STA_DISCONNECTED handler zeroes its RAM copy and saves that. That handler is
     * wifi_manager's only erase, though, and an idle STA never reaches it: no
     * disconnect event comes. On the router-outage fallback portal the STA is idle
     * (START_AP stops the retry timer), so a reset there used to keep the credentials,
     * and with them a fallback AP that keeps BLE scanning: a customer whose router
     * password changed could not reconfigure the hub (2026-09-29 bench capture). So
     * after the 2 s wait erase_wifi_credentials() erases them straight from NVS, in
     * every state, and keeps wifi_manager from saving them again before the reboot.
     *
     * We then reboot. A fresh boot gives the SoftAP captive portal a less fragmented
     * heap than the running one, with no MQTT/TLS session loaded (without Wi-Fi
     * there is no cloud bring-up), so it stays responsive under a phone's DNS/HTTP
     * probe storm. It is NOT a BLE-free heap any more: since 2.1.4 iothub_task
     * starts NimBLE at boot, with no Wi-Fi gate, on any hub with a valve or a BLE
     * sensor (leak protection must not wait for Wi-Fi), and NimBLE stays initialised
     * beside the portal. The old ~130 KB figure predates that; the portal's heap next
     * to BLE is recorded on the bench (S21: free, min_ever, largest block while a
     * phone drives the portal).
     *
     * BLE does not SCAN beside this portal, though. With no credentials saved, the
     * portal priority window (app_wifi.c) pauses the leak scanner and the valve hunt
     * while the setup portal is up: continuous scanning left the SoftAP so little radio
     * time that no phone could join. A valve already linked stays linked. The window
     * opens only if the credentials really are gone after the reboot, which the erase
     * above now makes sure of.
     *
     * The reboot does NOT forget provisioned devices: commissioning (valve / LoRa
     * / BLE-leak sensors), hub identity, and DPS cache live in the dedicated
     * NVS_PROV_PARTITION, which survives reboots and any default-partition erase —
     * only the WiFi credentials are cleared. Full decommission stays app-only
     * (C2D "decommission").
     */
    ESP_LOGI(TAG, "Erasing WiFi credentials, then rebooting into AP "
                  "(commissioning preserved in nvs_prov)...");
    wifi_manager_disconnect_async();

    // Let wifi_manager finish the disconnect, and its own save, before the erase.
    vTaskDelay(pdMS_TO_TICKS(2000));

    erase_wifi_credentials();   // holds wifi_manager's NVS mutex through the restart

    ESP_LOGW(TAG, "Rebooting into AP mode...");
    esp_restart();
}

// ---------------------------------------------------------------------------
// Button task — all logic runs here
// ---------------------------------------------------------------------------
static void reset_button_task(void *pvParameters)
{
    btn_state_t state = STATE_IDLE;
    TickType_t  last_edge_tick = 0;

    ESP_LOGI(TAG, "Task started, monitoring GPIO %d", WIFI_RESET_BUTTON_GPIO);

    btn_event_t evt;
    for (;;) {
        if (xQueueReceive(s_evt_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        TickType_t now = xTaskGetTickCount();

        switch (evt) {

        case BTN_EVT_EDGE: {
            // Debounce: ignore edges within DEBOUNCE_MS of the last one
            if ((now - last_edge_tick) < pdMS_TO_TICKS(DEBOUNCE_MS)) {
                break;
            }
            last_edge_tick = now;

            bool pressed = button_is_pressed();

            switch (state) {
            case STATE_IDLE:
                if (pressed) {
                    ESP_LOGI(TAG, "Button pressed — starting %d ms hold timer",
                             HOLD_TIME_MS);
                    xTimerStart(s_hold_timer, 0);
                    state = STATE_PRESSED_PENDING;
                }
                break;

            case STATE_PRESSED_PENDING:
                if (!pressed) {
                    // Released before 10 seconds — cancel
                    ESP_LOGI(TAG, "Button released early — timer cancelled");
                    xTimerStop(s_hold_timer, 0);
                    state = STATE_IDLE;
                }
                break;

            case STATE_TRIGGERED_WAIT_RELEASE:
                if (!pressed) {
                    ESP_LOGI(TAG, "Button released after reset trigger");
                    state = STATE_IDLE;
                }
                break;
            }
            break;
        }

        case BTN_EVT_TIMER_EXPIRED: {
            if (state != STATE_PRESSED_PENDING) {
                // Stale timer event (button was released before it fired)
                break;
            }

            // Verify button is still physically held down
            if (button_is_pressed()) {
                ESP_LOGW(TAG, "%d-second hold confirmed — executing WiFi reset", HOLD_TIME_MS / 1000);
                execute_wifi_reset();
                state = STATE_TRIGGERED_WAIT_RELEASE;
            } else {
                // Button was released but edge was missed/debounced
                ESP_LOGI(TAG, "Timer expired but button not pressed — ignoring");
                state = STATE_IDLE;
            }
            break;
        }

        } // switch(evt)
    } // for(;;)
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void reset_button_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi reset button on GPIO %d", WIFI_RESET_BUTTON_GPIO);

    // Create event queue (before ISR can fire)
    s_evt_queue = xQueueCreate(EVT_QUEUE_LEN, sizeof(btn_event_t));
    if (!s_evt_queue) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return;
    }

    // Create one-shot 10-second hold timer
    s_hold_timer = xTimerCreate("btn_hold",
                                pdMS_TO_TICKS(HOLD_TIME_MS),
                                pdFALSE,    // one-shot
                                NULL,
                                hold_timer_cb);
    if (!s_hold_timer) {
        ESP_LOGE(TAG, "Failed to create hold timer");
        return;
    }

    // Configure GPIO: input, pull-up, interrupt on any edge
    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_ANYEDGE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << WIFI_RESET_BUTTON_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_ENABLE
    };
    gpio_config(&io_conf);

    // Install ISR service (safe to call multiple times — returns ESP_ERR_INVALID_STATE
    // if already installed, which it normally is: lora.cpp installs it first).
    //
    // Expect one benign line on every boot:
    //   E (nnn) gpio: gpio_install_isr_service(526): GPIO isr service already installed
    // That E level is emitted by the IDF driver itself before it returns the error —
    // it is not this module's log and cannot be suppressed from here without a
    // private "is it installed" query the driver does not expose. Handled below.
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
        return;
    }

    gpio_isr_handler_add(WIFI_RESET_BUTTON_GPIO, button_isr_handler, NULL);

    // Create task
    xTaskCreate(reset_button_task, "reset_btn", TASK_STACK_SIZE,
                NULL, TASK_PRIORITY, &s_task_handle);

    ESP_LOGI(TAG, "WiFi reset button ready (hold %d s to reset)", HOLD_TIME_MS / 1000);
}
