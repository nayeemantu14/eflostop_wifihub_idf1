#ifndef FLEET_LED_H
#define FLEET_LED_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GPIO 48 "system status" roll-up LED.
 *
 * An INDEPENDENT, additional indicator to the GPIO 38 network LED (rgb.c). It
 * shows the hub's overall device-health as a single at-a-glance SOLID colour,
 * driven by the existing health source of truth:
 *
 *   no devices provisioned  -> WHITE   (hub not commissioned / idle)
 *   active valve leak        -> RED      (critical — overrides health rating)
 *   EXCELLENT | GOOD         -> GREEN   (all good)
 *   WARNING                  -> ORANGE  (warning)
 *   CRITICAL                 -> RED      (critical)
 *
 * Colour = health_get_system_rating() collapsed, EXCEPT an active valve leak
 * (ble_valve_get_leak / RMLEAK) forces RED since the health rating does not
 * track leak state. The WHITE state = provisioned device count
 * (health_get_sync_counts) is zero. This module never touches the GPIO 38
 * strip, its task, its queue, or the net_status coordinator.
 */
void setupFleetLEDTask(void);

#ifdef __cplusplus
}
#endif

#endif /* FLEET_LED_H */
