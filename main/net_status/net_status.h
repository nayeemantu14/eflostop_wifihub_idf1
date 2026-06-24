#ifndef NET_STATUS_H
#define NET_STATUS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Connection-status coordinator for the GPIO38 status LED.
 *
 * Owns the two facts that decide the network LED state — WiFi (STA has an IP)
 * and Azure IoT Hub (MQTT) connectivity — and derives the single LED command
 * from the *full* truth under a mutex. The WiFi and MQTT events fire on
 * different FreeRTOS tasks in an unspecified order; deriving the colour from
 * both flags (rather than each event blindly sending a colour) makes that
 * ordering irrelevant and prevents a stale LED state. The codes map to the
 * led_task animations via ledQueue (see rgb.h):
 *
 *   !wifi                -> LED_CMD_NO_INTERNET (ramp red)
 *   wifi && !mqtt        -> LED_CMD_CONNECTING  (beat blue @ 500 ms)
 *   wifi &&  mqtt        -> LED_CMD_CONNECTED   (ramp blue)
 *
 * Invariant: mqtt-up is only meaningful while wifi is up, so
 * net_status_set_wifi(false) also clears the mqtt flag (the TLS/MQTT session
 * cannot survive WiFi loss, even before esp-mqtt reports MQTT_EVENT_DISCONNECTED).
 *
 * The LoRa-activity green pulse is an independent one-shot overlay and is not
 * managed here.
 */

/* Create the mutex and latch the boot state (no internet -> ramp red).
 * Call once, after setupLEDTask() (so ledQueue exists) and before the WiFi /
 * MQTT tasks that drive the setters are created. */
void net_status_init(void);

/* WiFi STA got/lost an IP. */
void net_status_set_wifi(bool up);

/* Azure IoT Hub MQTT connected/disconnected. */
void net_status_set_mqtt(bool up);

#ifdef __cplusplus
}
#endif

#endif /* NET_STATUS_H */
