#ifndef RGB_H
#define RGB_H

#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define BLINKLED 38

// --- LED COMMAND CODES ---
// Single-byte codes pushed onto ledQueue with xQueueSend(ledQueue, &code, 0).
// led_task maps each to an animation. The three network-state codes are driven
// by the net_status coordinator (derived from WiFi + MQTT truth); LORA_PULSE is
// an independent one-shot overlay sent by the LoRa RX path.
#define LED_CMD_NO_INTERNET 'R'   // ramp red   — WiFi down / no IP
#define LED_CMD_CONNECTING  'B'   // beat blue  — got IP, IoT Hub not yet connected
#define LED_CMD_CONNECTED   'F'   // ramp blue  — IoT Hub (MQTT) connected
#define LED_CMD_LORA_PULSE  'G'   // green blip — LoRa packet RX (overlay)
#define LED_CMD_CLEAR       'C'   // clear

#ifdef __cplusplus
extern "C" {
#endif

// --- DECLARATION (Tells linker this exists somewhere) ---
extern QueueHandle_t ledQueue;

led_strip_handle_t configLED(void);
void rampRED(led_strip_handle_t strip);
void rampBLUE(led_strip_handle_t strip);
void beatBLUE(led_strip_handle_t strip);
void pulseGREEN(led_strip_handle_t strip);
void clearLED(led_strip_handle_t strip);
void setupLEDTask(void);
void led_task(void *param);

#ifdef __cplusplus
}
#endif

#endif