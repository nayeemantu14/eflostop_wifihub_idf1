#ifndef RGB_H
#define RGB_H

#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define BLINKLED 38

#ifdef __cplusplus
extern "C" {
#endif

// --- DECLARATION (Tells linker this exists somewhere) ---
extern QueueHandle_t ledQueue; 

led_strip_handle_t configLED(void);
void rampRED(led_strip_handle_t strip);
void beatBLUE(led_strip_handle_t strip);
void pulseGREEN(led_strip_handle_t strip);
void clearLED(led_strip_handle_t strip);
void setupLEDTask(void);
void led_task(void *param);

#ifdef __cplusplus
}
#endif

#endif