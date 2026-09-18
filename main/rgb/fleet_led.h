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
 * Evaluated in this precedence order:
 *
 *   no devices provisioned            -> WHITE   (hub not commissioned / idle)
 *   CRITICAL                          -> RED     (leak anywhere, or device offline)
 *   WARNING                           -> YELLOW  (low battery / weak signal, or the
 *                                                 hub still holding the valve shut)
 *   provisioned but not yet heard from -> WHITE   (syncing — boot window still open)
 *   EXCELLENT | GOOD                  -> GREEN   (all good)
 *
 * So a leak incident reads RED while wet -> YELLOW while dry but still interlocked
 * -> GREEN once the interlock releases.
 *
 * Colour is health_get_system_rating() collapsed, with NO special cases: leak is an
 * input to that rating for every device type (valve probe, BLE and LoRa sensors), and
 * a latched interlock raises a WARNING floor on it. The two WHITE states come from
 * health_get_sync_counts() — total == 0 for unprovisioned, seen < total (with the
 * boot window still open) for syncing. This module never touches the GPIO 38 strip,
 * its task, its queue, or the net_status coordinator.
 */
void setupFleetLEDTask(void);

#ifdef __cplusplus
}
#endif

#endif /* FLEET_LED_H */
