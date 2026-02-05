#ifndef APP_BLE_VALVE_H
#define APP_BLE_VALVE_H
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Queue for sending updates TO the IoT Hub Task
    extern QueueHandle_t ble_update_queue;

    // BLE update event types for delta forwarding
    typedef enum
    {
        BLE_UPD_NONE = 0,
        BLE_UPD_BATTERY,
        BLE_UPD_LEAK,
        BLE_UPD_STATE,
        BLE_UPD_CONNECTED,
        BLE_UPD_DISCONNECTED
    } ble_update_type_t;

    typedef enum
    {
        BLE_CMD_CONNECT = 0,
        BLE_CMD_DISCONNECT,
        BLE_CMD_OPEN_VALVE,
        BLE_CMD_CLOSE_VALVE,
        BLE_CMD_SECURE
    } ble_valve_cmd_t;

    typedef struct
    {
        ble_valve_cmd_t command;
    } ble_valve_msg_t;

    /**
     * @brief Prepares queues and the 'starter' task but DOES NOT turn on the radio.
     * Call this in app_main().
     */
    void app_ble_valve_init(void);

    /**
     * @brief Signals the BLE starter task to wake up and initialize the stack.
     * Call this from the Wi-Fi Connected callback.
     * It is safe to call multiple times (subsequent calls are ignored).
     */
    void app_ble_valve_signal_start(void);

    // API
    bool ble_valve_open(void);
    bool ble_valve_close(void);
    bool ble_valve_connect(void);
    bool ble_valve_disconnect(void);

    // Provisioning support
    void ble_valve_set_target_mac(const char *mac_str);
    bool ble_valve_has_target_mac(void);

    // Getters
    bool ble_valve_get_mac(char *mac_buffer);
    uint8_t ble_valve_get_battery(void);
    bool ble_valve_get_leak(void);
    int ble_valve_get_state(void);

    bool ble_valve_is_ready(void);
    bool ble_valve_is_secured(void);

#ifdef __cplusplus
}
#endif

#endif // APP_BLE_VALVE_H