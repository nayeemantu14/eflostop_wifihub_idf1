#ifndef RESET_BUTTON_H
#define RESET_BUTTON_H

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_RESET_BUTTON_GPIO  40

/**
 * @brief Initialize and start the WiFi reset button.
 *        Configures GPIO 40 with pull-up + any-edge ISR,
 *        creates event queue, 5-second one-shot timer, and task.
 *        Call once from app_main() after NVS and WiFi manager are running.
 */
void reset_button_init(void);

#ifdef __cplusplus
}
#endif

#endif // RESET_BUTTON_H
