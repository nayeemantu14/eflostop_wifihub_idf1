#ifndef APP_IOTHUB_H
#define APP_IOTHUB_H
#pragma once

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" // Required for Mutex
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app_lora.h"


#define IOTHUB_TAG "IOTHUB"

void initialize_iothub(void);

void send_packet_to_iothub(const uint8_t* data, size_t length);

void iothub_task(void *param);



#endif