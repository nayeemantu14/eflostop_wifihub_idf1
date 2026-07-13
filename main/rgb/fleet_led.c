/****************************************************
 *  MODULE:   Fleet / System-Status LED (GPIO 48)
 *  PURPOSE:  Overall device-health roll-up indicator, independent of the
 *            GPIO 38 network LED. Reuses the GPIO 38 ramp effect (rgb.c:47-76)
 *            applied identically to green / orange / red.
 *
 *  SOURCE OF TRUTH: health_get_system_rating() (lock-free volatile worst-of
 *  across all provisioned devices). This module only READS it — it holds no
 *  health state of its own, so it can never disagree with the health engine.
 ****************************************************/

#include "fleet_led.h"

#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "health_engine.h"   /* health_get_system_rating, health_rating_to_str, health_rating_t */

#define FLEET_TAG        "FLEET_LED"
#define FLEET_LED_GPIO   48

/* Ramp peak amplitude — matches the LUT peak of 50 in rgb.c:51. Per-colour
 * "peak" triples below scale this curve into each channel, so a pure green/red
 * ramp is bit-identical to the GPIO 38 rampRED (only the channel differs). */
#define RAMP_PEAK        50

/* Logical (R,G,B) peaks. led_strip reorders to GRB on the wire internally, so
 * these are passed R,G,B — proven by pulseGREEN(0,45,0) in rgb.c:105 under
 * LED_STRIP_COLOR_COMPONENT_FMT_GRB. Orange has no prior value in-tree; G is
 * tunable in ~20..32 for a true orange vs amber on the SK68XXMINI-HS. */
#define GREEN_R   0
#define GREEN_G   50
#define GREEN_B   0
#define ORANGE_R  50
#define ORANGE_G  30
#define ORANGE_B  0
#define RED_R     50
#define RED_G     0
#define RED_B     0

typedef enum { FLEET_GREEN = 0, FLEET_ORANGE, FLEET_RED } fleet_color_t;

static const char *fleet_color_name(fleet_color_t c)
{
    switch (c) {
    case FLEET_GREEN:  return "GREEN";
    case FLEET_ORANGE: return "ORANGE";
    case FLEET_RED:    return "RED";
    default:           return "?";
    }
}

/* Collapse the 4-level device-health rating to the 3 LED colours (LOCK-3):
 * EXCELLENT|GOOD -> GREEN, WARNING -> ORANGE, CRITICAL -> RED. */
static fleet_color_t rating_to_color(health_rating_t r)
{
    switch (r) {
    case HEALTH_CRITICAL: return FLEET_RED;
    case HEALTH_WARNING:  return FLEET_ORANGE;
    default:              return FLEET_GREEN;   /* HEALTH_EXCELLENT, HEALTH_GOOD */
    }
}

static fleet_color_t current_target_color(void)
{
    return rating_to_color(health_get_system_rating());
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

/* ------------------------------------------------------------------ */
/* Ramp brightness LUT — MUST match rgb.c:49-52 (rampColor). Duplicated here on
 * purpose: sharing it would require editing rgb.c, which drives the GPIO 38 LED
 * (LOCK-1: that code stays untouched). Keep the two in sync if ever changed. */
static const uint8_t R[] = {0, 0, 1, 1, 1, 2, 2, 3, 4, 5, 5, 8, 9, 10, 11, 13, 15, 15, 17, 19,
                            25, 25, 27, 29, 31, 33, 35, 37, 39, 41, 43, 43, 45, 47, 47, 49,
                            49, 50, 50, 50};

/* Stateless analogue of rgb.c's led_cmd_pending(): returns true when the
 * collapsed target colour has moved away from the one currently animating, so a
 * state change lands within ~one 50 ms step instead of after the ~4.5 s cycle. */
static bool color_changed(fleet_color_t active)
{
    return current_target_color() != active;
}

/* One full ramp cycle (up / 500 ms hold / down) for the given colour, mirroring
 * rampColor() (rgb.c:47-76) exactly in shape + timing. Returns early if the
 * target colour changes mid-cycle. */
static void rampColorRGB(led_strip_handle_t strip, fleet_color_t color)
{
    const size_t R_LEN = sizeof(R) / sizeof(R[0]);

    uint8_t pr, pg, pb;
    switch (color) {
    case FLEET_RED:    pr = RED_R;    pg = RED_G;    pb = RED_B;    break;
    case FLEET_ORANGE: pr = ORANGE_R; pg = ORANGE_G; pb = ORANGE_B; break;
    case FLEET_GREEN:
    default:           pr = GREEN_R;  pg = GREEN_G;  pb = GREEN_B;  break;
    }

    /* ramp up */
    for (size_t i = 0; i < R_LEN; i++) {
        ESP_ERROR_CHECK(led_strip_set_pixel(strip, 0,
                        (uint32_t)R[i] * pr / RAMP_PEAK,
                        (uint32_t)R[i] * pg / RAMP_PEAK,
                        (uint32_t)R[i] * pb / RAMP_PEAK));
        ESP_ERROR_CHECK(led_strip_refresh(strip));
        vTaskDelay(pdMS_TO_TICKS(50));
        if (color_changed(color)) return;
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    if (color_changed(color)) return;

    /* ramp down */
    for (size_t i = R_LEN; i-- > 0;) {
        ESP_ERROR_CHECK(led_strip_set_pixel(strip, 0,
                        (uint32_t)R[i] * pr / RAMP_PEAK,
                        (uint32_t)R[i] * pg / RAMP_PEAK,
                        (uint32_t)R[i] * pb / RAMP_PEAK));
        ESP_ERROR_CHECK(led_strip_refresh(strip));
        vTaskDelay(pdMS_TO_TICKS(50));
        if (color_changed(color)) return;
    }

    ESP_ERROR_CHECK(led_strip_clear(strip));
}

/* ------------------------------------------------------------------ */
static void fleet_led_task(void *param)
{
    led_strip_handle_t strip = (led_strip_handle_t)param;
    int last_logged = -1;   /* force an initial log line */

    while (1) {
        health_rating_t rating = health_get_system_rating();
        fleet_color_t   color  = rating_to_color(rating);

        if ((int)color != last_logged) {
            /* one line per transition; the production tool asserts this exact form */
            ESP_LOGI(FLEET_TAG, "rating=%s color=%s effect=RAMP",
                     health_rating_to_str(rating), fleet_color_name(color));
            last_logged = (int)color;
        }

        /* Runs one ramp cycle, returning early if the target colour changes. */
        rampColorRGB(strip, color);
    }

    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
void setupFleetLEDTask(void)
{
    led_strip_handle_t strip = configFleetLED();
    /* priority 1 (lowest, same as led_task) — cannot preempt BLE/IoT; reads only
     * the lock-free rating so it never blocks on a mutex. Safe to start before
     * health_engine_init(): the rating defaults to EXCELLENT (green). */
    xTaskCreate(fleet_led_task, "fleet_led_task", 2560, (void *)strip, 1, NULL);
}
