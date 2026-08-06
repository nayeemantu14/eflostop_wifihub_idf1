#ifndef APP_IOTHUB_H
#define APP_IOTHUB_H
#pragma once

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

// Global Handle (Exposed so Wi-Fi can notify it)
extern TaskHandle_t iothub_task_handle;

// Task entry point
void iothub_task(void *param);

// Initialize and start the IoT Hub task
void initialize_iothub(void);

// Trigger provisioning MAC application to BLE
void iothub_apply_provisioned_mac(void);

// Suspend/resume the MQTT client on WiFi loss/restore. Stopping the client while
// STA is down frees the large TLS buffers so the SoftAP captive portal stays
// responsive after a button WiFi reset (no MQTT TLS-reconnect heap thrash).
void iothub_suspend_mqtt(void);
void iothub_resume_mqtt(void);

#ifdef __cplusplus
}
#endif

#endif // APP_IOTHUB_H
