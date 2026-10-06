/****************************************************
 *  MODULE:   Radio lab (2.1.4 WP7: the G1 lab ladder)
 *  PURPOSE:  The lab image's run-time settings for the radio
 *            policy's open choices, their console keys, their
 *            log lines and their NVS copy. Compiled only with
 *            Kconfig APP_RADIO_LAB (default n): see radio_lab.h.
 ****************************************************/

#include "radio_lab.h"

#if CONFIG_APP_RADIO_LAB

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "radio_policy.h"
#include "ble_leak_scanner/app_ble_leak.h"

/* A lab image is a bench image: it must not build once WP10 turns the bench diagnostics off for a
 * release, so an APP_RADIO_LAB left on in a release sdkconfig stops that build. */
#if !CONFIG_APP_BENCH_DIAG
#error "APP_RADIO_LAB is a bench image: it needs APP_BENCH_DIAG (main/Kconfig.projbuild) - never a release"
#endif

#define LAB_TAG         "RADIO_LAB"
#define LAB_NVS_NS      "rp_lab"     // the default NVS partition (a 10 s reset erases only Wi-Fi's keys)
#define LAB_NVS_KEY     "cfg"
#define LAB_CFG_VER     1

/* The settings, as NVS keeps them. A blob of another size or version reads as the defaults. */
typedef struct __attribute__((packed)) {
    uint8_t ver;        // LAB_CFG_VER
    uint8_t rung;       // RP_RUNG_*
    uint8_t w06;        // 1: AP_IDLE's Wi-Fi slot 0.6 s
    uint8_t join;       // 1: the join assist on
    uint8_t k1;         // 1: contingency K1 on
} lab_cfg_t;

/* The settings now: one writer (radio_lab_init() at boot, then lora_task's keys), read by any
 * task one byte at a time. The defaults are the production values. */
static volatile uint8_t s_rung = RP_SERVE_RUNG;
static volatile bool s_w06 = false;
static volatile bool s_join = true;
static volatile bool s_k1 = false;

uint8_t radio_lab_rung(void)
{
    return s_rung;
}

bool radio_lab_apidle_w06(void)
{
    return s_w06;
}

bool radio_lab_join_on(void)
{
    return s_join;
}

bool radio_lab_k1_on(void)
{
    return s_k1;
}

const char *radio_lab_rung_name(uint8_t rung)
{
    switch (rung) {
    case RP_RUNG_APIDLE:
        return "AP_IDLE density";
    case RP_RUNG_SERVE_A_THIN:
        return "SERVE-A-thin";
    case RP_RUNG_SERVE_A:
        return "SERVE-A";
    case RP_RUNG_SERVE_C:
        return "SERVE-C";
    case RP_RUNG_SERVE_B:
        return "SERVE-B";
    default:
        return "?";
    }
}

const char *radio_lab_rung_text(uint8_t rung)
{
    switch (rung) {
    case RP_RUNG_APIDLE:
        return "no SERVE geometry: AP_IDLE's rows and discovery cadence";
    case RP_RUNG_SERVE_A_THIN:
        return "Coded 0.6 s / Wi-Fi 0.6 s for 10 s after a page, user request or 302, else as AP_IDLE";
    case RP_RUNG_SERVE_A:
        return "Coded 0.6 s / Wi-Fi 0.6 s, discovery every 3 periods";
    case RP_RUNG_SERVE_C:
        return "Coded 1.0 s / Wi-Fi 1.0 s, discovery every 3 periods (lab only: breaks I8 and the period rule)";
    case RP_RUNG_SERVE_B:
        return "Coded 0.6 s / Wi-Fi 1.2 s, discovery every 3 periods";
    default:
        return "?";
    }
}

void radio_lab_log(const char *why, int k1_asked)
{
    char k1n[24] = "";
    if (k1_asked >= 0) {
        snprintf(k1n, sizeof(k1n), " (%d asked)", k1_asked);
    }
    ESP_LOGI(LAB_TAG, "[LAB] %s - SERVE rung %s; AP_IDLE Coded 0.6 s / Wi-Fi %s s; join assist %s; K1 %s%s",
             why, radio_lab_rung_name(s_rung), s_w06 ? "0.6" : "0.3", s_join ? "on" : "off",
             s_k1 ? "on" : "off", k1n);
}

static void lab_defaults(void)
{
    s_rung = RP_SERVE_RUNG;
    s_w06 = false;
    s_join = true;
    s_k1 = false;
}

static bool lab_valid(const lab_cfg_t *c)
{
    return c->ver == LAB_CFG_VER && c->rung < RP_RUNG_COUNT && c->w06 <= 1 && c->join <= 1 && c->k1 <= 1;
}

static void lab_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(LAB_NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return;   // never saved: the defaults
    }
    if (err != ESP_OK) {
        ESP_LOGW(LAB_TAG, "[LAB] settings not read (%s) - the production values", esp_err_to_name(err));
        return;
    }
    lab_cfg_t c;
    size_t len = sizeof(c);
    err = nvs_get_blob(h, LAB_NVS_KEY, &c, &len);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return;
    }
    if (err != ESP_OK || len != sizeof(c) || !lab_valid(&c)) {
        ESP_LOGW(LAB_TAG, "[LAB] saved settings not usable (%s, %u B) - the production values",
                 esp_err_to_name(err), (unsigned)len);
        return;
    }
    s_rung = c.rung;
    s_w06 = c.w06;
    s_join = c.join;
    s_k1 = c.k1;
}

static void lab_save(void)
{
    lab_cfg_t c = {
        .ver = LAB_CFG_VER, .rung = s_rung, .w06 = s_w06, .join = s_join, .k1 = s_k1,
    };
    nvs_handle_t h;
    esp_err_t err = nvs_open(LAB_NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, LAB_NVS_KEY, &c, sizeof(c));
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGW(LAB_TAG, "[LAB] settings not saved (%s) - they hold until the next boot", esp_err_to_name(err));
    }
}

static void lab_help(void)
{
    ESP_LOGI(LAB_TAG, "[LAB] keys: 1 AP_IDLE density, 2 SERVE-A-thin, 3 SERVE-A, 4 SERVE-C, 5 SERVE-B; "
             "i AP_IDLE Wi-Fi 0.3/0.6 s; j join assist; k K1; l settings; x production values; h keys");
}

void radio_lab_init(void)
{
    lab_load();
    ESP_LOGW(LAB_TAG, "radio lab image (APP_RADIO_LAB, the G1 ladder) - not for release");
    radio_lab_log("settings at boot", -1);
    lab_help();
}

bool radio_lab_key(char c)
{
    char why[48];
    switch (c) {
    case '1':
    case '2':
    case '3':
    case '4':
    case '5': {
        // In the order of plan 4.3's table, the most Coded first.
        static const uint8_t k_rungs[5] = {
            RP_RUNG_APIDLE, RP_RUNG_SERVE_A_THIN, RP_RUNG_SERVE_A, RP_RUNG_SERVE_C, RP_RUNG_SERVE_B,
        };
        s_rung = k_rungs[c - '1'];
        snprintf(why, sizeof(why), "key %c: SERVE rung", c);
        ESP_LOGI(LAB_TAG, "[LAB] SERVE rung %s: %s", radio_lab_rung_name(s_rung), radio_lab_rung_text(s_rung));
        break;
    }
    case 'i':
        s_w06 = !s_w06;
        snprintf(why, sizeof(why), "key i: AP_IDLE Wi-Fi slot");
        break;
    case 'j':
        s_join = !s_join;
        snprintf(why, sizeof(why), "key j: join assist");
        break;
    case 'k':
        s_k1 = !s_k1;
        snprintf(why, sizeof(why), "key k: K1");
        break;
    case 'x':
        lab_defaults();
        snprintf(why, sizeof(why), "key x: production values");
        break;
    case 'l':
        radio_lab_log("on request", -1);
        return true;
    case 'h':
    case '?':
        lab_help();
        return true;
    default:
        return false;
    }
    lab_save();
    radio_lab_log(why, -1);
    // A rung or a slot takes effect at the executor's next slot boundary (I1's row switch).
    app_ble_leak_kick();
    return true;
}

#endif // CONFIG_APP_RADIO_LAB
