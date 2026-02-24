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
// =============================================================================
#define AZURE_DPS_ID_SCOPE   "0ne01136E89"
#define AZURE_DPS_GROUP_KEY  "wYbUyG99DN+s8L/JvrGjpaij+9eELo1cS6YDE91aWtILn0KUX4FT8+VhyczKy1FfxLfvbVH9+DAoAIoTCeakRQ=="

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

#ifdef __cplusplus
}
#endif

#endif // APP_IOTHUB_H
