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
#include "nvs_flash.h"
#include "nvs_store/nvs_store.h"
#include "wifi_manager.h"
#include "rgb.h"
#include "rgb/fleet_led.h"
#include "net_status/net_status.h"
#include "app_wifi/app_wifi.h"
#include "app_uart/app_uart.h"
#include "app_lora/app_lora.h"
#include "iothub/app_iothub.h"
#include "ble_valve/app_ble_valve.h"
#include "ble_leak_scanner/app_ble_leak.h"
#include "systemservices/monitoring.h"
#include "wifi_reset/reset_button.h"
#include "hub_identity/hub_identity.h"

/* ---------------------------------------------------------
 * Tags
 * --------------------------------------------------------- */
/* @brief tag used for ESP serial console messages */
__attribute__((unused)) static const char TAG[] = "main";

/* ---------------------------------------------------------
 * Main Application
 * --------------------------------------------------------- */
void app_main(void)
{
	/* initialize NVS — required by Wi-Fi, BLE, and other subsystems */
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	/* dedicated NVS partition for commissioning/identity — survives a WiFi
	 * reset and any default-partition erase. Must init before hub_identity /
	 * provisioning / dps / sensor_meta / rules_engine touch their namespaces. */
	nvs_store_init();

	/* derive Gateway ID + Short ID from MAC, load hub name from NVS */
	hub_identity_init();

	/* start subsystems */
    setupLEDTask();
    net_status_init();   /* network status LED coordinator (after ledQueue exists) */
    setupFleetLEDTask(); /* GPIO 48 overall device-health roll-up LED (independent of GPIO 38 network LED) */
	configureUART();
	/* the wifi_manager / http_server tags of the setup portal's component (components/
	 * wifi_portal) are capped at WARN before Wi-Fi starts; their warnings and errors still
	 * show, and app_wifi logs the connect/IP lines itself. The cap was added because the
	 * registry copy logged the site Wi-Fi password at INFO; since 2.1.4 (C1) the component
	 * logs the SSID and the password's length only, and the cap stays. */
	esp_log_level_set("wifi_manager", ESP_LOG_WARN);
	esp_log_level_set("http_server", ESP_LOG_WARN);
    app_wifi_start();
	configurelora();
	/* LoRa and both BLE modules create their event queues BEFORE iothub_task starts: it
	 * no longer waits for Wi-Fi (leak protection runs from boot) and builds its event
	 * QueueSet immediately, so every member queue must already exist. The BLE radio still
	 * starts later, when iothub_task applies the provisioned device set. */
	app_ble_valve_init();
	app_ble_leak_init();
	initialize_iothub();

	/* WiFi reset button (GPIO 40, hold 10s to clear WiFi credentials + reboot into AP
	 * captive portal; commissioning is preserved in nvs_prov, decommission is app-only) */
	reset_button_init();

	/* start system monitoring (heap, uptime, diagnostics) */
	monitoring_init();
}
