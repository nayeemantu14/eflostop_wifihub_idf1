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
 * Call after draining ble_leak_rx_queue (e.g. at IoT Hub startup).
 */
void app_ble_leak_reset_tracking(void);

/**
 * @brief Process a BLE advertisement for possible leak sensor data.
 * Can be called from any scan callback (e.g. the valve module's GAP
 * handler) so leak sensors are detected even while the valve module
 * owns the active scan.
 */
void app_ble_leak_process_adv(const void *addr, int8_t rssi,
                              const uint8_t *data, uint8_t data_len);

#ifdef __cplusplus
}
#endif

#endif // APP_BLE_LEAK_H
