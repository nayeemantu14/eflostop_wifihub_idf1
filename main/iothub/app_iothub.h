#ifndef APP_IOTHUB_H
#define APP_IOTHUB_H
#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define IOTHUB_TAG "IOTHUB"

// =============================================================================
// AZURE DPS CONFIGURATION
//
// AZURE_DPS_GROUP_KEY is the *enrollment group* symmetric key. Each device's own
// key is HMAC-SHA256(group_key, "GW-<mac>") — see dps_client.c derive_device_key().
// It is NOT the DPS service SAS policy key; that one must never be in firmware.
//
// AZURE_DPS_PROV_EPOCH invalidates the NVS-cached DPS assignment (hub hostname +
// derived device key) across an infrastructure change. dps_register() returns the
// cached assignment without contacting DPS, so moving to a new DPS instance would
// otherwise leave every already-registered hub talking to the OLD IoT Hub forever.
// BUMP THIS whenever the ID scope, group key, or target DPS instance changes.
//   epoch 1 (implicit, pre-1.8.0): original DPS instance, ID scope 0ne01136E89
//   epoch 2 (1.8.0): resi-apex-iot-dps-dev, ID scope 0ne0124BF06
// =============================================================================
#define AZURE_DPS_ID_SCOPE   "0ne0124BF06"
#define AZURE_DPS_GROUP_KEY  "/nevC+JnwgNEm6uLRFeglK6HmiL/gBUr1M3hCEha+wletdx8cL5bKR0w1pnSytoFFnEJ+y3R5PMiAIoTZDacKw=="
#define AZURE_DPS_PROV_EPOCH 2

// Minimum epoch to consider the wall clock synced (2024-01-01 00:00:00 UTC).
// Every SAS token — the IoT Hub one and the DPS registration one — stamps its
// expiry from time(NULL), so minting before SNTP lands produces a credential that
// expired in 1971 and is rejected with a 401 that no amount of retrying fixes.
#define SNTP_EPOCH_VALID  1704067200

// SAS token helpers (used by dps_client)
void url_encode(const char *src, char *dst, size_t dst_len);
char *generate_sas_token(const char *resource_uri, const char *key, long expiry_seconds);

// Handle of iothub_task (set by initialize_iothub())
extern TaskHandle_t iothub_task_handle;

// Task entry point
void iothub_task(void *param);

// Initialize and start the IoT Hub task. Every event queue it selects on (LoRa, valve,
// BLE leak) must already exist: the task builds its QueueSet as soon as it starts.
void initialize_iothub(void);

// Wi-Fi STA got an IP (the Wi-Fi manager's callback; called on every (re)connect, safe at
// any time, even before initialize_iothub()). iothub_task no longer waits for Wi-Fi - leak
// protection and BLE run from boot - so this only marks the network usable and wakes the
// loop, which then starts SNTP and, once its cloud admission lets it in (the SoftAP down,
// internal heap to spare), the cloud bring-up (DPS, MQTT), without blocking.
void iothub_on_wifi_connected(void);

// Wi-Fi STA lost its link, or a connect attempt ended without one (the Wi-Fi manager's
// callback; safe at any time). A flag, a count and a wake only: iothub_task withdraws the
// admission on its next pass, asks wifi_task to stop MQTT (iothub_mqtt_stop_service()) and
// holds the cloud off until the next admission.
void iothub_on_wifi_lost(void);

// wifi_task only (app_wifi.c), on every pass and when woken: runs the MQTT client stop that
// iothub_task asked for, if any, then wakes iothub_task. It can take as long as the stop:
// about 1 s in a session, up to 5 s between esp-mqtt's reconnects, 10-30 s with a connect in
// flight. Holds no lock of the app's meanwhile. Safe at any time, even before
// initialize_iothub(); its first call records the caller as the task iothub_task wakes.
void iothub_mqtt_stop_service(void);

// The publish gate (2.1.4 WP2c, R0-1): every publish of cloud_tx's runs under it, and so does
// wifi_task's MQTT stop, so the two never run at once. esp-mqtt's task frees the outbox when
// its stop ends without its API lock, and a publish beside that would corrupt it. Neither side
// ever waits for the gate: a try-take only.
// iothub_pub_begin(): true = the gate is taken AND the session is up (connected, the client
// built, no stop asked); then call iothub_pub_end() right after the one publish. false = do
// not publish (treat it as offline), nothing to give back.
bool iothub_pub_begin(void);
// Gives the gate back, and wakes wifi_task if its stop is waiting for it. what / msg_id / t0_us
// describe the publish just made (esp_timer_get_time() before it); what = NULL: none was made.
void iothub_pub_end(const char *what, int msg_id, int64_t t0_us);

// Apply the provisioned device set to BLE: the valve target becomes the provisioned valve
// (or none), and BLE starts when there is a valve or a BLE leak sensor to serve. The target
// is read and set in one provisioning mutex hold, so it is never a valve that a concurrent
// provisioning change has already removed or replaced.
// Returns false when provisioning could not be read (busy): NOTHING was applied, and the
// caller must hand the retry to iothub_task (it retries every pass until one succeeds).
bool iothub_apply_provisioned_mac(void);

#ifdef __cplusplus
}
#endif

#endif // APP_IOTHUB_H
