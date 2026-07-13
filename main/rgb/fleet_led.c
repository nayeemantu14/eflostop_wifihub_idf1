/****************************************************
 *  MODULE:   Fleet / System-Status LED (GPIO 48)
 *  PURPOSE:  Overall device-health roll-up indicator, independent of the
 *            GPIO 38 network LED. Shows a single SOLID colour:
 *              no devices provisioned -> WHITE  (hub not commissioned / idle)
 *              EXCELLENT | GOOD        -> GREEN  (all good)
 *              WARNING                 -> ORANGE (warning)
 *              CRITICAL                -> RED     (critical)
 *
 *  SOURCE OF TRUTH: health_get_system_rating() (lock-free volatile worst-of
 *  across all provisioned devices) + health_get_sync_counts() for the
 *  provisioned-device count. This module only READS them — it holds no health
 *  state of its own, so it can never disagree with the health engine.
 ****************************************************/

#include "fleet_led.h"

#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "health_engine.h"   /* health_get_system_rating, health_get_sync_counts, health_rating_to_str */

#define FLEET_TAG        "FLEET_LED"
#define FLEET_LED_GPIO   48

/* Poll cadence — how often the roll-up state is re-evaluated. Solid colours
 * latch on the WS2812 until the next change, so the LED is only rewritten on a
 * state transition; this just bounds how quickly a change is reflected. */
#define FLEET_POLL_MS    250

/* Logical (R,G,B) values. led_strip reorders to GRB on the wire internally, so
 * these are passed R,G,B — proven by pulseGREEN(0,45,0) in rgb.c:105 under
 * LED_STRIP_COLOR_COMPONENT_FMT_GRB. Kept dim to match the existing indicators
 * (rgb.c blips/ramps peak at 45–50). Orange G and the white level are tunable
 * on the SK68XXMINI-HS at bring-up. */
#define GREEN_R    0
#define GREEN_G   50
#define GREEN_B    0
#define ORANGE_R  50
#define ORANGE_G  30
#define ORANGE_B   0
#define RED_R     50
#define RED_G      0
#define RED_B      0
#define WHITE_R   25
#define WHITE_G   25
#define WHITE_B   25

typedef enum { FLEET_WHITE = 0, FLEET_GREEN, FLEET_ORANGE, FLEET_RED } fleet_state_t;

static const char *state_name(fleet_state_t s)
{
    switch (s) {
    case FLEET_WHITE:  return "WHITE";
    case FLEET_GREEN:  return "GREEN";
    case FLEET_ORANGE: return "ORANGE";
    case FLEET_RED:    return "RED";
    default:           return "?";
    }
}

/* Collapse the 4-level device-health rating to the 3 colour states (LOCK-3):
 * EXCELLENT|GOOD -> GREEN, WARNING -> ORANGE, CRITICAL -> RED. */
static fleet_state_t rating_to_color(health_rating_t r)
{
    switch (r) {
    case HEALTH_CRITICAL: return FLEET_RED;
    case HEALTH_WARNING:  return FLEET_ORANGE;
    default:              return FLEET_GREEN;   /* HEALTH_EXCELLENT, HEALTH_GOOD */
    }
}

/* ------------------------------------------------------------------ */
/* LED strip — own handle + own (auto-allocated) RMT channel; the GPIO 38
 * strip in rgb.c is untouched. Mirrors configLED() (rgb.c:14-29). */
static led_strip_handle_t configFleetLED(void)
{
    led_strip_handle_t strip = NULL;

    led_strip_config_t strip_config = {
        .strip_gpio_num = FLEET_LED_GPIO,
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

/* Drive the LED to a solid colour and hold it (WS2812 latches until next set). */
static void set_solid(led_strip_handle_t strip, fleet_state_t s)
{
    uint8_t r, g, b;
    switch (s) {
    case FLEET_RED:    r = RED_R;    g = RED_G;    b = RED_B;    break;
    case FLEET_ORANGE: r = ORANGE_R; g = ORANGE_G; b = ORANGE_B; break;
    case FLEET_GREEN:  r = GREEN_R;  g = GREEN_G;  b = GREEN_B;  break;
    case FLEET_WHITE:
    default:           r = WHITE_R;  g = WHITE_G;  b = WHITE_B;  break;
    }
    ESP_ERROR_CHECK(led_strip_set_pixel(strip, 0, r, g, b));
    ESP_ERROR_CHECK(led_strip_refresh(strip));
}

/* ------------------------------------------------------------------ */
static void fleet_led_task(void *param)
{
    led_strip_handle_t strip = (led_strip_handle_t)param;
    int           shown = -1;            /* last state pushed to the LED (-1 = none yet) */
    fleet_state_t held  = FLEET_WHITE;   /* last state from a successful health query */

    while (1) {
        uint8_t total = 0;
        fleet_state_t target;

        if (health_get_sync_counts(NULL, &total)) {
            /* total == 0 -> nothing provisioned (fresh/idle hub) -> white */
            target = (total == 0) ? FLEET_WHITE
                                  : rating_to_color(health_get_system_rating());
            held = target;
        } else {
            /* Pre-init (health engine not up yet) or a rare mutex-busy read:
             * hold the last known state so a transient failure never flashes
             * white over a real red/orange. Defaults to white at boot. */
            target = held;
        }

        if ((int)target != shown) {
            set_solid(strip, target);
            /* one line per transition; the production tool asserts this exact form */
            const char *rating_str = (target == FLEET_WHITE)
                                     ? "unprovisioned"
                                     : health_rating_to_str(health_get_system_rating());
            ESP_LOGI(FLEET_TAG, "rating=%s color=%s effect=SOLID",
                     rating_str, state_name(target));
            shown = (int)target;
        }

        vTaskDelay(pdMS_TO_TICKS(FLEET_POLL_MS));
    }

    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
void setupFleetLEDTask(void)
{
    led_strip_handle_t strip = configFleetLED();
    /* priority 1 (lowest, same as led_task) — cannot preempt BLE/IoT; reads only
     * lock-free / bounded-timeout health accessors so it never stalls a hot path.
     * Safe to start before health_engine_init(): the counts read returns false
     * and the LED holds white until commissioned devices load. */
    xTaskCreate(fleet_led_task, "fleet_led_task", 2560, (void *)strip, 1, NULL);
}
