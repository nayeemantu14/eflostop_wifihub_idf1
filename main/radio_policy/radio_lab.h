#ifndef RADIO_LAB_H
#define RADIO_LAB_H
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =================================================================================================
 * The radio lab image (2.1.4 WP7; plan 4.3, 4.9 C11, 11, 12 G1): Kconfig APP_RADIO_LAB, default n.
 * Without it this header declares nothing and radio_lab.c compiles to an empty object: a production
 * image carries none of the lab.
 *
 * G1 benches the radio policy's open choices on this image. They are switched at run time with the
 * console keys that lora_task polls (radio_lab_key()), every change is logged, and they are kept in
 * NVS, so they survive a reboot and the 10 s reset (which erases only the Wi-Fi credentials):
 *   - the SERVE rung (plan 4.3): AP_IDLE density, SERVE-A-thin, SERVE-A, SERVE-C (lab only: it
 *     breaks I8 and the period rule), SERVE-B;
 *   - AP_IDLE's Wi-Fi slot: 0.3 s (the plan's) or 0.6 s;
 *   - the join assist on or off; contingency K1 (plan 4.4) on or off;
 *   - C11: the SoftAP without 11b rates (from the next boot); coex_background_scan on the hub's
 *     network-list scan.
 * The defaults are the production values: with no key pressed the lab image runs as production.
 * ================================================================================================= */
#if CONFIG_APP_RADIO_LAB

/** Loads the settings (default NVS partition, namespace "rp_lab"), hands C11's to the Wi-Fi
 *  component and prints them. Once, in app_wifi_start(), before wifi_manager_start(). */
void radio_lab_init(void);

/** One console key (lora_task, its bench keys). true: a lab key (handled and logged). */
bool radio_lab_key(char c);

/** The settings now. Any task: one byte each, written by lora_task only. */
uint8_t radio_lab_rung(void);          // RP_RUNG_* (radio_policy.h)
bool radio_lab_apidle_w06(void);       // AP_IDLE's Wi-Fi slot is 0.6 s, not 0.3 s
bool radio_lab_join_on(void);          // the join assist runs (production: always)
bool radio_lab_k1_on(void);            // contingency K1 runs (production: never)

/** A rung's name ("SERVE-A") and its geometry as a mode line prints it. */
const char *radio_lab_rung_name(uint8_t rung);
const char *radio_lab_rung_text(uint8_t rung);

/** The settings in one line, after `why` (the boot, a key, the 60 s summary). k1_asked >= 0 adds
 *  the K1 assists asked since the last summary. */
void radio_lab_log(const char *why, int k1_asked);

#endif // CONFIG_APP_RADIO_LAB

#ifdef __cplusplus
}
#endif

#endif // RADIO_LAB_H
