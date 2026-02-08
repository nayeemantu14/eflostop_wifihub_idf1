#ifndef APP_BLE_VALVE_H
#define APP_BLE_VALVE_H
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

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
        BLE_UPD_RMLEAK,
        BLE_UPD_CONNECTED,
        BLE_UPD_DISCONNECTED
    } ble_update_type_t;

    typedef enum
    {
        BLE_CMD_CONNECT = 0,
        BLE_CMD_DISCONNECT,
        BLE_CMD_OPEN_VALVE,
        BLE_CMD_CLOSE_VALVE,
        BLE_CMD_SECURE,
        BLE_CMD_SET_RMLEAK,
        BLE_CMD_CLEAR_RMLEAK
    } ble_valve_cmd_t;

    typedef struct
    {
        ble_valve_cmd_t command;
    } ble_valve_msg_t;

    // -----------------------------------------------------------------------------
    // BLE State Event Bits (for event group synchronization)
    // These bits track the security and connection state machine
    // -----------------------------------------------------------------------------
    #define BLE_STATE_BIT_CONNECTED       (1 << 0)  // GAP connection established
    #define BLE_STATE_BIT_PAIRING         (1 << 1)  // Pairing/bonding in progress
    #define BLE_STATE_BIT_ENCRYPTED       (1 << 2)  // Link encryption enabled
    #define BLE_STATE_BIT_AUTHENTICATED   (1 << 3)  // MITM authentication achieved
    #define BLE_STATE_BIT_BONDED          (1 << 4)  // Device is bonded (keys stored)
    #define BLE_STATE_BIT_DISCOVERY_DONE  (1 << 5)  // Service/char discovery complete

    // Composite bit: link is secure and ready for protected GATT operations
    #define BLE_STATE_BIT_SECURE_READY    (BLE_STATE_BIT_CONNECTED | BLE_STATE_BIT_ENCRYPTED)

    // Composite bit: fully ready for all GATT operations
    #define BLE_STATE_BIT_READY_FOR_GATT  (BLE_STATE_BIT_CONNECTED | BLE_STATE_BIT_ENCRYPTED | BLE_STATE_BIT_DISCOVERY_DONE)

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
    bool ble_valve_is_authenticated(void);

    /**
     * @brief Get the BLE state event group handle for external synchronization.
     * @return EventGroupHandle_t or NULL if not initialized.
     */
    EventGroupHandle_t ble_valve_get_state_event_group(void);

    /**
     * @brief Clear stored bonds for the valve device.
     * Use this for decommissioning or troubleshooting pairing issues.
     */
    void ble_valve_clear_bonds(void);

    /**
     * @brief Write RMLEAK characteristic on the valve (1=assert interlock, 0=clear).
     * Non-blocking: queues a BLE command. If disconnected, queues pending and triggers reconnect.
     */
    bool ble_valve_set_rmleak(bool enabled);

    /**
     * @brief Get the last-known RMLEAK value read/notified from the valve.
     */
    bool ble_valve_get_rmleak_state(void);

#ifdef __cplusplus
}
#endif

#endif // APP_BLE_VALVE_H
