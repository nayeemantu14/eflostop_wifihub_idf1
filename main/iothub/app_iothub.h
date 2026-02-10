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
// AZURE CONFIGURATION
// =============================================================================
#define AZURE_HUB_NAME      "wd-core-iothub-poc"
#define AZURE_DEVICE_ID     "WiFi-Hub-Enware"
#define AZURE_PRIMARY_KEY   "RLI+ccdGK4XrViw0jr0+sO3Pn9t/2jL2Rea28MwWHGE="

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
