#ifndef APP_IOTHUB_H
#define APP_IOTHUB_H
#pragma once

#include "app_lora.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define IOTHUB_TAG "IOTHUB"

// =============================================================================
// AZURE CONFIGURATION
// =============================================================================
#define AZURE_HUB_NAME      "wd-core-iothub-poc"
#define AZURE_DEVICE_ID     "esp32"
#define AZURE_PRIMARY_KEY   "fnCtWP8Spn/4OYyIf00lItjAhT0q2IqqR4IY63HnqZY="

// Global Handle (Exposed so Wi-Fi can notify it)
extern TaskHandle_t iothub_task_handle;

// Just starts the task (which will immediately block)
void initialize_iothub(void);

#endif