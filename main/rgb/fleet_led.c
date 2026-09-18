/****************************************************
 *  MODULE:   Fleet / System-Status LED (GPIO 48)
 *  PURPOSE:  Overall device-health roll-up indicator, independent of the
 *            GPIO 38 network LED. Shows a single SOLID colour, in this
 *            precedence order:
 *              no devices provisioned  -> WHITE  (hub not commissioned / idle)
 *              CRITICAL                -> RED    (leak anywhere, or device offline)
 *              WARNING                 -> YELLOW (low battery / weak signal, or the
 *                                                 hub still holding the valve shut)
 *              provisioned but not yet heard from -> WHITE (syncing, boot window)
 *              EXCELLENT | GOOD        -> GREEN  (all good)
 *
 *  A leak incident therefore reads RED while wet -> YELLOW while dry but still
 *  interlocked -> GREEN once the interlock releases.
 *
 *  SOURCE OF TRUTH: health_get_system_rating() (lock-free volatile worst-of
 *  across all provisioned devices), health_is_rollup_syncing() for "is that
 *  rating complete yet", and health_get_sync_counts() for the provisioned count.
 *  This module only READS them — it holds no health state of its own, so it can
 *  never disagree with the health engine. Leak is an input to the rating itself,
 *  so there is no leak special case here.
 ****************************************************/

#include "fleet_led.h"

#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "health_engine.h"   /* health_get_system_rating, health_get_sync_counts,
                              * health_is_rollup_syncing, health_rating_to_str */

#define FLEET_TAG        "FLEET_LED"
#define FLEET_LED_GPIO   48

/* Poll cadence — how often the roll-up state is re-evaluated. Solid colours
 * latch on the WS2812 until the next change, so the LED is only rewritten on a
 * state transition; this just bounds how quickly a change is reflected. */
#define FLEET_POLL_MS    250

/* Logical (R,G,B) values. led_strip reorders to GRB on the wire internally, so
 * these are passed R,G,B — proven by pulseGREEN(0,45,0) in rgb.c:105 under
 * LED_STRIP_COLOR_COMPONENT_FMT_GRB. Kept dim to match the existing indicators
 * (rgb.c blips/ramps peak at 45–50).
 *
 * YELLOW (warning): the WS2812 green die is perceptually much brighter than
 * red, so an equal-ratio yellow (50,50,0) reads as lime/green. Green is held
 * below red (G/R ~= 0.7) for a true yellow that stays clearly distinct from the
 * RED critical state through the diffusing white housing. Nudge YELLOW_G at
 * bring-up: ~20 = amber, ~28 = warm yellow, ~35 = yellow, ~42 = leans lime.
 * White level is likewise tunable on the SK68XXMINI-HS. */
#define GREEN_R    0
#define GREEN_G   50
#define GREEN_B    0
#define YELLOW_R  50
#define YELLOW_G  35
#define YELLOW_B   0
#define RED_R     50
#define RED_G      0
#define RED_B      0
#define WHITE_R   25
#define WHITE_G   25
#define WHITE_B   25

typedef enum { FLEET_OFF = 0, FLEET_WHITE, FLEET_GREEN, FLEET_YELLOW, FLEET_RED } fleet_state_t;

static const char *state_name(fleet_state_t s)
{
    switch (s) {
    case FLEET_OFF:    return "OFF";
    case FLEET_WHITE:  return "WHITE";
    case FLEET_GREEN:  return "GREEN";
    case FLEET_YELLOW: return "YELLOW";
    case FLEET_RED:    return "RED";
    default:           return "?";
    }
}

/* Collapse the 4-level device-health rating to the 3 colour states (LOCK-3):
 * EXCELLENT|GOOD -> GREEN, WARNING -> YELLOW, CRITICAL -> RED. */
static fleet_state_t rating_to_color(health_rating_t r)
{
    switch (r) {
    case HEALTH_CRITICAL: return FLEET_RED;
    case HEALTH_WARNING:  return FLEET_YELLOW;
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
    if (s == FLEET_OFF) {
        ESP_ERROR_CHECK(led_strip_clear(strip));   /* LED dark */
        return;
    }
    uint8_t r, g, b;
    switch (s) {
    case FLEET_RED:    r = RED_R;    g = RED_G;    b = RED_B;    break;
    case FLEET_YELLOW: r = YELLOW_R; g = YELLOW_G; b = YELLOW_B; break;
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
    int           shown       = -1;          /* last state pushed to the LED (-1 = none yet) */
    bool          initialized = false;       /* set after the first successful health read */
    fleet_state_t held        = FLEET_OFF;   /* last state from a successful query */
    const char   *held_reason = "startup";   /* matching reason string for the log */

    while (1) {
        uint8_t total = 0;
        fleet_state_t target;
        const char *reason;

        /* seen-count no longer needed: health_is_rollup_syncing() answers the question
         * this module used to derive from seen<total. Still called for `total` (the
         * unprovisioned test) and as the health-engine-is-up probe. */
        if (health_get_sync_counts(NULL, &total)) {
            initialized = true;
            /* Sample the sync flag BEFORE the rating, matching the same ordering in
             * telemetry_v2_publish_snapshot(). health_is_rollup_syncing() evaluates the
             * window deadlines on read, and the grace expiring re-rolls the roll-up
             * (devices never heard from stop being excluded). Reading the rating first
             * would use the pre-expiry value while branching on the post-expiry flag —
             * one GREEN frame, and a spurious GREEN transition logged, for a hub that had
             * just become CRITICAL over a missing device.
             *
             * NOT health_is_boot_sync_complete(): that is the SNAPSHOT gate, on the short
             * 180 s clock. Asking it here is what produced the 2026-09-17 capture's 81 s
             * of RED on a healthy hub — the gate opened at 180 s while the roll-up still
             * (correctly) had nothing to say about a sensor that beaconed at 265.6 s. This
             * predicate tracks the roll-up's ACTUAL exclusion set, so the LED and the
             * rating can no longer disagree. */
            bool syncing = health_is_rollup_syncing();
            health_rating_t r = health_get_system_rating();

            if (total == 0) {
                /* nothing provisioned (fresh/idle hub) -> white */
                target = FLEET_WHITE;
                reason = "unprovisioned";
            } else if (r >= HEALTH_WARNING) {
                /* Anything the roll-up considers actionable wins outright — RED for
                 * CRITICAL (a leak anywhere, or a device genuinely offline), YELLOW
                 * for WARNING (low battery / weak signal, or the hub still holding
                 * the valve closed after a leak).
                 *
                 * Checked BEFORE the syncing branch on purpose: a device that has
                 * been heard and is wet must not be masked by other devices not
                 * having reported in yet.
                 *
                 * The valve-specific leak override that used to live here is gone.
                 * Leak is now an input to health_get_system_rating() for every device
                 * type, so this module is back to being a pure reader of the roll-up
                 * and cannot disagree with what the snapshot reports. */
                target = rating_to_color(r);
                reason = health_rating_to_str(r);
            } else if (syncing) {
                /* Provisioned devices we simply have not heard from yet. Not a fault:
                 * the sensors are event-driven and burst every ~100 s, so at boot
                 * there is a legitimate window where the honest answer is "don't
                 * know". White says that; red would be a lie that had installers
                 * chasing healthy hardware. Once the grace expires, a device still
                 * unheard counts in the roll-up and this goes red via the branch
                 * above.
                 *
                 * The old `seen < total` half of this test is gone as redundant, not
                 * dropped: health_is_rollup_syncing() is true only when some in-use
                 * device has !ever_seen, which is exactly seen < total. One predicate
                 * instead of two that could drift apart. */
                target = FLEET_WHITE;
                reason = "syncing";
            } else {
                target = rating_to_color(r);     /* EXCELLENT | GOOD -> GREEN */
                reason = health_rating_to_str(r);
            }
            held = target;
            held_reason = reason;
        } else if (initialized) {
            /* Rare mutex-busy read after init: hold the last known state so a
             * transient failure never flashes over a real red/orange. */
            target = held;
            reason = held_reason;
        } else {
            /* Health engine not up yet (first ~3 s of boot — it inits from the
             * iothub task): keep the LED dark until the real system state is
             * known, rather than showing a misleading white on a provisioned hub. */
            target = FLEET_OFF;
            reason = "startup";
        }

        if ((int)target != shown) {
            set_solid(strip, target);
            /* one line per transition; the production tool asserts this exact form */
            ESP_LOGI(FLEET_TAG, "rating=%s color=%s effect=SOLID",
                     reason, state_name(target));
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
