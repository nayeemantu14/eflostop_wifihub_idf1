#include "reset_button.h"
#include <string.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include "esp_system.h"
#include "wifi_manager.h"

#define TAG "RESET_BTN"

// ---------------------------------------------------------------------------
// Event types posted to the shared queue (by ISR and timer callback)
// ---------------------------------------------------------------------------
typedef enum {
    BTN_EVT_EDGE,           // GPIO interrupt fired (press or release)
    BTN_EVT_TIMER_EXPIRED   // 5-second one-shot timer fired
} btn_event_t;

// ---------------------------------------------------------------------------
// Task state machine
// ---------------------------------------------------------------------------
typedef enum {
    STATE_IDLE,
    STATE_PRESSED_PENDING,       // Button down, waiting for 5 s timer
    STATE_TRIGGERED_WAIT_RELEASE // Reset fired, waiting for release
} btn_state_t;

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
static QueueHandle_t  s_evt_queue    = NULL;
static TimerHandle_t  s_hold_timer   = NULL;
static TaskHandle_t   s_task_handle  = NULL;

#define HOLD_TIME_MS     5000
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
// WiFi reset action
// ---------------------------------------------------------------------------
static void execute_wifi_reset(void)
{
    ESP_LOGW(TAG, "=== LONG PRESS CONFIRMED — RESETTING WIFI ===");

    /*
     * wifi_manager_disconnect_async() sends WM_ORDER_DISCONNECT_STA.
     * The wifi_manager handles the full sequence:
     *   1. esp_wifi_disconnect()           — clean disconnect
     *   2. memset config to 0 + save NVS   — erase stored credentials
     *   3. WM_ORDER_START_AP               — start captive portal
     *
     * See wifi_manager.c WIFI_EVENT_STA_DISCONNECTED handler with
     * WIFI_MANAGER_REQUEST_DISCONNECT_BIT set.
     */
    ESP_LOGI(TAG, "Disconnecting WiFi + erasing credentials...");
    wifi_manager_disconnect_async();

    // Give wifi_manager time to erase NVS credentials before reboot
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGW(TAG, "Restarting ESP...");
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
                    // Released before 5 seconds — cancel
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
                ESP_LOGW(TAG, "5-second hold confirmed — executing WiFi reset");
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

    // Create one-shot 5-second hold timer
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

    // Install ISR service (safe to call multiple times — returns ESP_ERR_INVALID_STATE if already installed)
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
