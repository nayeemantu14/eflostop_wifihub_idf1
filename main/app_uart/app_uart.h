#ifndef APP_UART_H
#define APP_UART_H
#pragma once
#include <stdio.h>
#include <string.h>
#include <esp_err.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define UART_TAG "APP_UART"
#define UART_TX_PIN 37
#define UART_RX_PIN 36
#define TXBUFSIZE 1024
#define RXBUFSIZE 1024

void configureUART(void);
static void uart_event_task(void *params);

#endif