#ifndef FLEET_LED_H
#define FLEET_LED_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GPIO 48 "system status" roll-up LED.
 *
 * An INDEPENDENT, additional indicator to the GPIO 38 network LED (rgb.c). It
 * shows the hub's overall device-health as a single at-a-glance colour, driven
 * by the existing lock-free source of truth health_get_system_rating():
 *
 *   EXCELLENT | GOOD -> GREEN   (all good)
 *   WARNING          -> ORANGE  (warning)
 *   CRITICAL         -> RED      (critical)
 *
 * All three colours use the SAME ramp (fade up / hold / down) effect defined
 * for the GPIO 38 LED in rgb.c:47-76 (faithfully reused here, generalised to an
 * arbitrary RGB target). This module never touches the GPIO 38 strip, its task,
 * its queue, or the net_status coordinator.
 */
void setupFleetLEDTask(void);

#ifdef __cplusplus
}
#endif

#endif /* FLEET_LED_H */
