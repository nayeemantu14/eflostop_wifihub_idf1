/****************************************************
 *  MODULE:   Main File
 *  PURPOSE:  Contains the main application code
 ****************************************************/

/* ---------------------------------------------------------
 * Includes
 * --------------------------------------------------------- */
#include <stdio.h>
#include <string.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "wifi_manager.h"
#include "rgb.h"
#include "app_wifi.h"
#include "app_uart.h"
#include "app_lora.h"
#include "iothub/app_iothub.h"
#include "ble_valve/app_ble_valve.h"

/* ---------------------------------------------------------
 * Tags
 * --------------------------------------------------------- */
/* @brief tag used for ESP serial console messages */
static const char TAG[] = "main";

/* ---------------------------------------------------------
 * Globals
 * --------------------------------------------------------- */

/* ---------------------------------------------------------
 * Function Prototypes
 * --------------------------------------------------------- */

static void monitoring_task(void *pvParameter);

/* ---------------------------------------------------------
 * Main Application
 * --------------------------------------------------------- */
void app_main(void)
{	
	/* start the wifi manager */
    setupLEDTask();
	configureUART();
    app_wifi_start();
	configurelora();
	initialize_iothub();
	app_ble_valve_init();
#if CONFIG_SOC_CPU_CORES_NUM > 1
	/* create a task on core 1 that monitors free heap memory */
	xTaskCreate(&monitoring_task, "monitoring_task", 4096, NULL, 1, NULL);
#endif
}

/* ---------------------------------------------------------
 * Callback Functions
 * --------------------------------------------------------- */


/* ---------------------------------------------------------
 * RTOS Tasks
 * --------------------------------------------------------- */
/**
 * @brief RTOS task that periodically prints the heap memory available.
 * @note Debug information only; avoid in production.
 */
static void monitoring_task(void *pvParameter)
{
	(void)pvParameter;

	for (;;)
	{
		ESP_LOGI(TAG, "free heap: %lu", (unsigned long)esp_get_free_heap_size());
		vTaskDelay(pdMS_TO_TICKS(10000));
	}
}
