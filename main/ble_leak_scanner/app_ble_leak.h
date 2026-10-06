#ifndef APP_BLE_LEAK_H
#define APP_BLE_LEAK_H
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// Queue for sending BLE leak sensor events to IoT Hub task
extern QueueHandle_t ble_leak_rx_queue;

// BLE leak sensor event (sent from scanner to IoT Hub)
typedef struct {
    uint8_t sensor_mac[6];     // Raw MAC bytes
    char sensor_mac_str[18];   // "XX:XX:XX:XX:XX:XX"
    uint8_t battery;           // 0-100%
    bool leak_detected;        // true if leak
    int8_t rssi;               // Advertisement RSSI
    char fw_version[12];       // "M.m.p" or "" if not available
} ble_leak_event_t;

/**
 * @brief Initialize the BLE leak scanner module.
 * Creates the queue and task (blocked until signaled).
 * Call from app_main() after app_ble_valve_init().
 */
void app_ble_leak_init(void);

/**
 * @brief Signal the leak scanner task that NimBLE is ready.
 * Call from ble_starter_task after NimBLE initialization.
 */
void app_ble_leak_signal_start(void);

/**
 * @brief Reset per-sensor tracking state so next advertisement
 * from each sensor is treated as "first seen".
 * Called once by iothub_task at startup, before BLE starts.
 */
void app_ble_leak_reset_tracking(void);

/**
 * @brief Wake the BLE scan executor (the leak scanner's task) now, rather than at its next
 * poll. The valve module calls it when what it wants from the executor changes (a hunt, a
 * claim, a link up or down), and the radio policy when a fact or a pulse request changes.
 * Any task, not an ISR. Ignored before the task exists.
 *
 * Since 2.1.4 (WP5) this task is the only code that starts or stops a BLE scan: the valve
 * hunt runs on its scans (ble_valve_hunt_wanted(), ble_valve_note_adv()), and since WP8 it
 * runs the radio policy's rows and pulses (radio_policy.h).
 */
void app_ble_leak_kick(void);

#ifdef __cplusplus
}
#endif

#endif // APP_BLE_LEAK_H
