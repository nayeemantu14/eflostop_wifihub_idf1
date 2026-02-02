#include "rgb.h"

// --- 1. DEFINE VARIABLES ---
// This allocates memory for the queue and task handle
QueueHandle_t ledQueue = NULL;
TaskHandle_t ledTaskHandle = NULL;

// --- 2. DEFINE HELPER FUNCTIONS ---

led_strip_handle_t configLED(void)
{
    led_strip_handle_t strip = NULL;

    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINKLED,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {.invert_out = false},
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = {.with_dma = false},
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip));
    ESP_ERROR_CHECK(led_strip_clear(strip));
    return strip;
}

void rampRED(led_strip_handle_t strip)
{
    const uint8_t R[] = {0, 0, 1, 1, 1, 2, 2, 3, 4, 5, 5, 8, 9, 10, 11, 13, 15, 15, 17, 19,
                         25, 25, 27, 29, 31, 33, 35, 37, 39, 41, 43, 43, 45, 47, 47, 49,
                         49, 50, 50, 50};
    const size_t R_LEN = sizeof(R) / sizeof(R[0]);

    // ramp up
    for (size_t i = 0; i < R_LEN; i++)
    {
        ESP_ERROR_CHECK(led_strip_set_pixel(strip, 0, R[i], 0, 0));
        ESP_ERROR_CHECK(led_strip_refresh(strip));
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    // ramp down
    for (size_t i = R_LEN; i-- > 0;)
    {
        ESP_ERROR_CHECK(led_strip_set_pixel(strip, 0, R[i], 0, 0));
        ESP_ERROR_CHECK(led_strip_refresh(strip));
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_ERROR_CHECK(led_strip_clear(strip));
}

void beatBLUE(led_strip_handle_t strip)
{
    for (int j = 0; j < 3; j++)
    {
        ESP_ERROR_CHECK(led_strip_set_pixel(strip, 0, 0, 0, 45));
        ESP_ERROR_CHECK(led_strip_refresh(strip));
        vTaskDelay(pdMS_TO_TICKS(100));

        ESP_ERROR_CHECK(led_strip_clear(strip));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void pulseGREEN(led_strip_handle_t strip)
{
    ESP_ERROR_CHECK(led_strip_set_pixel(strip, 0, 0, 45, 0));
    ESP_ERROR_CHECK(led_strip_refresh(strip));
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_ERROR_CHECK(led_strip_clear(strip));
    vTaskDelay(pdMS_TO_TICKS(100));
}

void clearLED(led_strip_handle_t strip)
{
    ESP_ERROR_CHECK(led_strip_clear(strip));
    vTaskDelay(pdMS_TO_TICKS(100));
}

// --- 3. DEFINE TASKS ---

void setupLEDTask(void)
{
    // Only create if it doesn't exist
    if (ledQueue == NULL)
    {
        ledQueue = xQueueCreate(10, sizeof(uint8_t));
        if (ledQueue == NULL)
        {
            printf("Failed to create LED queue\n");
            return;
        }
    }

    led_strip_handle_t strip = configLED();
    xTaskCreate(led_task, "led_task", 2048, (void *)strip, 1, &ledTaskHandle);
}

void led_task(void *param)
{
    led_strip_handle_t strip = (led_strip_handle_t)param;
    uint8_t color;

    // Default state
    uint8_t stateColor = 'R'; 

    while (1)
    {
        // 1. Check for new commands WITHOUT blocking (wait time = 0)
        if (xQueueReceive(ledQueue, &color, 0) == pdTRUE)
        {
            switch (color)
            {
            case 'R':
                stateColor = 'R'; // Update state to Red
                break;
            case 'B':
                stateColor = 'B'; // Update state to Blue
                break;
            case 'G':
                clearLED(strip);
                vTaskDelay(pdMS_TO_TICKS(100));
                pulseGREEN(strip);
                // Note: We do NOT update stateColor, so it returns 
                // to Red or Blue automatically after this.
                break;
            case 'C':
                clearLED(strip);
                break;
            default:
                break;
            }
        }

        // 2. Play the Continuous Animation based on current state
        switch (stateColor)
        {
        case 'R':
            rampRED(strip);
            break;
        case 'B':
            beatBLUE(strip);
            break;
        default:
            vTaskDelay(pdMS_TO_TICKS(100)); // Safety delay
            break;
        }

        // Small delay between loops
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}