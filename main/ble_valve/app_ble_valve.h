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
     * Called by iothub_apply_provisioned_mac() when a valve or a BLE sensor is provisioned:
     * at boot, once iothub_task has built its event QueueSet, and on every `provision`.
     * It is safe to call multiple times, from any task (only the first call signals; later
     * calls are ignored).
     */
    void app_ble_valve_signal_start(void);

    // API
    //
    // The module only keeps a link to, commands, or reports the PROVISIONED valve (the target
    // MAC below); any other peer is disconnected at connect. open / close / connect (and
    // ble_valve_set_rmleak) return false - logged, nothing queued, settle barrier not
    // armed - when no valve is provisioned, and also when the command queue is full.
    // A queued command is pended while the provisioned valve is not linked and ready, and
    // replayed on its link only. ble_valve_disconnect() is never gated: it also cancels a
    // connect in flight and a valve scan.
    bool ble_valve_open(void);
    bool ble_valve_close(void);
    bool ble_valve_connect(void);
    bool ble_valve_disconnect(void);

    // Provisioning support
    /**
     * @brief Point the module at the provisioned valve (NULL = no valve).
     * A CHANGE (or removal) flushes every queued and pending valve command, and a link
     * still up to the previous valve is disconnected. NULL also stops reconnecting;
     * callers removing a valve still send ble_valve_disconnect() for its link.
     */
    void ble_valve_set_target_mac(const char *mac_str);
    bool ble_valve_has_target_mac(void);

    // Getters
    /**
     * @brief MAC ("XX:XX:XX:XX:XX:XX") of the peer currently linked, if any.
     * @param mac_buffer At least 18 bytes.
     * Not necessarily the provisioned valve: a link being torn down after a target change
     * still answers. Compare it with the provisioned MAC, or use ble_valve_is_ready().
     */
    bool ble_valve_get_mac(char *mac_buffer);
    /**
     * @brief Battery percent from the current link's last read/notify.
     * @return 0-100, or 0xFF when unknown (no link, characteristic missing, read
     *         failed, setup not done). 0 is a REAL 0 %: publish 0xFF as null.
     */
    uint8_t ble_valve_get_battery(void);
    bool ble_valve_get_leak(void);
    int ble_valve_get_state(void);

    /**
     * @brief GATT-ready (connected, encrypted, discovered) AND linked to the provisioned valve.
     */
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
     * @return false when no valve is provisioned or the command queue is full (nothing queued).
     */
    bool ble_valve_set_rmleak(bool enabled);

    /**
     * @brief Get the last-known RMLEAK value read/notified from the valve.
     */
    bool ble_valve_get_rmleak_state(void);

    /**
     * @brief Check if the valve BLE connection is established.
     * Returns true when a GAP connection exists (conn_handle != NONE).
     * Does NOT guarantee GATT is ready — use ble_valve_is_ready() for that.
     */
    bool ble_valve_is_connected(void);

    /**
     * @brief True while a hub-issued valve command has been queued but its effect
     *        on the cached valve state has not yet landed.
     *
     * ble_valve_open/close/set_rmleak only enqueue; the cache is written later on
     * the ble_valve task. Publishing a snapshot in that window reports the
     * PRE-transition valve state, which the UI then renders. The snapshot flush
     * block defers while this is true. Self-clearing after VALVE_CMD_SETTLE_MS so
     * a command that never reaches GATT (link down, mutex timeout) makes the
     * snapshot late, never blocked.
     */
    bool ble_valve_cmd_settling(void);

    /**
     * @brief Cancel any pending auto-close commands (valve CLOSE + RMLEAK SET).
     * Called by the rules engine when all leak sources clear before the valve
     * reconnects, so stale close commands are not applied on reconnect.
     */
    void ble_valve_cancel_pending_close(void);

    /**
     * @brief Get the valve's firmware revision string read from DIS (0x180A).
     * @param buffer Output buffer for the firmware revision string.
     * @param len    Size of the output buffer.
     * @return true if firmware revision is available, false otherwise.
     */
    bool ble_valve_get_firmware_rev(char *buffer, size_t len);

#ifdef __cplusplus
}
#endif

#endif // APP_BLE_VALVE_H
