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
#include "sdkconfig.h"

/* ---------------------------------------------------------
 * Load-bearing sdkconfig values (2.1.4 plan section 4.9, D12; invariant I14)
 *
 * sdkconfig is untracked, and kconfgen loads an existing sdkconfig over sdkconfig.defaults,
 * so a stale local sdkconfig would build without the lines this release depends on. Each
 * check below stops that build instead. To pass one, put the value of sdkconfig.defaults
 * into sdkconfig (idf.py menuconfig, or delete the symbol's line from sdkconfig and run
 * idf.py reconfigure); change a value here only together with its line there.
 * --------------------------------------------------------- */
#if !defined(CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN) || CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN != 2048
#error "I14: CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN must be 2048 (asymmetric lengths on): one TLS write takes at most a 2,389 B block"
#endif
#if defined(CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE)
#error "I14: CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE must be off: the server certificate is freed after the handshake (about 4 KB)"
#endif
#if !defined(CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM) || CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM != 16
#error "I14: CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM must be 16: the bound on what a frame flood can hold (plan section 8)"
#endif
#if !defined(CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER) || CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM != 16
#error "I14: dynamic Wi-Fi TX buffers with CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM 16 (plan section 8)"
#endif
#if defined(CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT)
#error "I5: NimBLE connect re-attempt must be off (CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT): the valve module owns every connect"
#endif
#if CONFIG_DEFAULT_AP_MAX_CONNECTIONS != 4
#error "I14: CONFIG_DEFAULT_AP_MAX_CONNECTIONS must be 4: app_wifi.c's portal client table and the SoftAP's station cap (plan section 8)"
#endif
#if CONFIG_DEFAULT_AP_CHANNEL != 11
#error "I14: CONFIG_DEFAULT_AP_CHANNEL must be 11 (D7): the setup SoftAP clear of BLE advertising channels 37 and 38"
#endif

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
	/* count failed allocations from here on (monitoring_init() reports them) */
	monitoring_alloc_fail_hook_init();

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
