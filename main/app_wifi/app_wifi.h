#ifndef APP_WIFI_H
#define APP_WIFI_H
#pragma once

#include <stdio.h>
#include <string.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "wifi_manager.h"
#include "rgb.h"

#define WIFI_TAG "APP_WIFI"

// =============================================================================
// WATCHDOG CONFIGURATION
// =============================================================================

// Maximum disconnection time before forcing AP mode (in seconds)
// Adjust this based on your network environment:
// - 30 seconds: Aggressive (quick failover to AP mode)
// - 60 seconds: Balanced (default)
// - 120 seconds: Conservative (gives more time for flaky networks)
#define MAX_DISCONNECT_TIME_SEC 60

// Maximum number of watchdog-triggered reboots before halting
// This prevents infinite reboot loops if there's a deeper issue
// Set to 0 to disable this safety check (not recommended)
#define MAX_WATCHDOG_TRIGGERS 3

// NVS namespace for watchdog boot counter
#define WIFI_WATCHDOG_NVS_NAMESPACE "app_wifi"
#define WIFI_WATCHDOG_BOOT_COUNT_KEY "boot_count"

// =============================================================================
// PUBLIC API
// =============================================================================

/**
 * Initialize and start Wi-Fi manager with watchdog protection
 * Call this once during app initialization
 */
void app_wifi_start(void);

/**
 * Callback when Wi-Fi connection is established
 * Triggers Azure IoT Hub and BLE initialization
 */
void cb_connection_ok(void *pvParameter);

/**
 * Callback when Wi-Fi connection is lost
 * Tracks disconnect events for watchdog
 */
void cb_connection_lost(void *pvParameter);

#endif // APP_WIFI_H