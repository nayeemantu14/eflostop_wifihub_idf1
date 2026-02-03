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

// Note: We don't need to include app_iothub.h here, only in the .c file
#define WIFI_TAG "APP_WIFI"

void app_wifi_start();

// These don't strictly need to be public, but it helps if you need to debug them
void cb_connection_ok(void *pvParameter);
void cb_connection_lost(void *pvParameter);

#endif