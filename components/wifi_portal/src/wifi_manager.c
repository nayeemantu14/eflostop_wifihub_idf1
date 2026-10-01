/*
Copyright (c) 2017-2020 Tony Pottier

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

@file wifi_manager.c
@author Tony Pottier
@brief Defines all functions necessary for esp32 to connect to a wifi/scan wifis

Contains the freeRTOS task and all necessary support

@see https://idyl.io
@see https://github.com/tonyp7/esp32-wifi-manager
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_system.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/timers.h>
#include <http_app.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi_types.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/ip4_addr.h"


#include "json.h"
#include "dns_server.h"
#include "nvs_sync.h"
#include "wifi_manager.h"



/* objects used to manipulate the main queue of events */
QueueHandle_t wifi_manager_queue;

/* @brief software timer to wait between each connection retry.
 * There is no point hogging a hardware timer for a functionality like this which only needs to be 'accurate enough' */
TimerHandle_t wifi_manager_retry_timer = NULL;

/* @brief software timer that will trigger shutdown of the AP after a succesful STA connection
 * There is no point hogging a hardware timer for a functionality like this which only needs to be 'accurate enough' */
TimerHandle_t wifi_manager_shutdown_ap_timer = NULL;

SemaphoreHandle_t wifi_manager_json_mutex = NULL;
SemaphoreHandle_t wifi_manager_sta_ip_mutex = NULL;
char *wifi_manager_sta_ip = NULL;
/* LOCAL PATCH (2.1.4 C2b): the network list's JSON exists only while the AP is up (the page is its
 * only reader): allocated at START_AP, freed at STOP_AP, NULL otherwise. The scan's records are read
 * one at a time onto the stack (wifi_manager_read_ap_records()): the MAX_AP_NUM wifi_ap_record_t
 * array the list was built from (1,380 B of heap, for good) is gone. */
char *accessp_json = NULL;
/* the size of accessp_json (LOCAL PATCH 2.1.4 C2e: one constant for the allocation and the bounds) */
#define ACCESSP_JSON_SIZE	(MAX_AP_NUM * JSON_ONE_APP_SIZE + 4) /* 4 bytes for json encapsulation of "[\n" and "]\0" */
/* START_AP sets it, STOP_AP clears it: the list should exist (a failed allocation is retried at the
 * next SCAN_DONE). wifi_manager task only. */
static bool ap_list_wanted = false;
/* LOCAL PATCH (2.1.4 WP1): the list's "no memory" line is printed for this AP start (START_AP
 * clears it): once per AP session, not at every SCAN_DONE that finds no room. wifi_manager task
 * only. */
static bool ap_list_logged = false;
/* LOCAL PATCH (2.1.4 WP1): an allocation that can wait (the network list) is tried only while the
 * largest free block exceeds it by this much. One that fails is counted as a failed allocation
 * (MONITOR's allocfail, the figure the memory gates pass on) and replaces the record of the last
 * one, which should name the allocation that could not wait. */
#define WIFI_MANAGER_HEAP_MARGIN	4096
/* LOCAL PATCH (2.1.4 WP1): the AP is up with its HTTP or DNS server not running: START_AP could
 * not start it (httpd_start() or the DNS task's creation failed, for lack of memory), and nothing
 * else would before the next START_AP or STOP_AP, which the setup portal may never see (plan I11:
 * both up from START_AP to STOP_AP). The task's loop then starts them again every
 * WIFI_MANAGER_AP_SERVERS_RETRY_MS, counted from ap_servers_tick (the last try), each try once the
 * largest free block has room for a server task's stack (WIFI_MANAGER_AP_SERVER_STACK, httpd's)
 * and WIFI_MANAGER_HEAP_MARGIN. STOP_AP clears it. wifi_manager task only. */
static bool ap_servers_down = false;
static TickType_t ap_servers_tick = 0;
#define WIFI_MANAGER_AP_SERVERS_RETRY_MS	5000
#define WIFI_MANAGER_AP_SERVER_STACK		4096
/* LOCAL PATCH (2.1.4 WP1, a bench diagnostic): a scan this task started is in flight, from the
 * esp_wifi_scan_start() that succeeded to this task's WM_EVENT_SCAN_DONE (done, failed or
 * stopped); the radio then visits every channel (wifi_manager_scan_in_flight()). Not cleared at a
 * STA disconnect, where the event handler clears WIFI_MANAGER_SCAN_BIT in case no SCAN_DONE
 * follows: were a SCAN_DONE ever lost, this would stay set until the next scan's, so it errs
 * towards "in flight", never away from it. wifi_manager task only. */
static bool scan_in_flight = false;
char *ip_info_json = NULL;
wifi_config_t* wifi_manager_config_sta = NULL;

/* @brief Array of callback function pointers */
void (**cb_ptr_arr)(void*) = NULL;

/* @brief tag used for ESP serial console messages */
static const char TAG[] = "wifi_manager";

/* @brief task handle for the main wifi_manager task */
static TaskHandle_t task_wifi_manager = NULL;

/* @brief netif object for the STATION */
static esp_netif_t* esp_netif_sta = NULL;

/* @brief netif object for the ACCESS POINT */
static esp_netif_t* esp_netif_ap = NULL;

/**
 * The actual WiFi settings in use
 */
struct wifi_settings_t wifi_settings = {
	.ap_ssid = DEFAULT_AP_SSID,
	.ap_pwd = DEFAULT_AP_PASSWORD,
	.ap_channel = DEFAULT_AP_CHANNEL,
	.ap_ssid_hidden = DEFAULT_AP_SSID_HIDDEN,
	.ap_bandwidth = DEFAULT_AP_BANDWIDTH,
	.sta_only = DEFAULT_STA_ONLY,
	.sta_power_save = DEFAULT_STA_POWER_SAVE,
	.sta_static_ip = 0,
};

const char wifi_manager_nvs_namespace[] = "espwifimgr";

static EventGroupHandle_t wifi_manager_event_group;

/* @brief indicate that the ESP32 is currently connected. */
const int WIFI_MANAGER_WIFI_CONNECTED_BIT = BIT0;

const int WIFI_MANAGER_AP_STA_CONNECTED_BIT = BIT1;

/* @brief Set automatically once the SoftAP is started */
const int WIFI_MANAGER_AP_STARTED_BIT = BIT2;

/* @brief When set, means a client requested to connect to an access point.*/
const int WIFI_MANAGER_REQUEST_STA_CONNECT_BIT = BIT3;

/* @brief This bit is set automatically as soon as a connection was lost */
const int WIFI_MANAGER_STA_DISCONNECT_BIT = BIT4;

/* @brief When set, means the wifi manager attempts to restore a previously saved connection at startup. */
const int WIFI_MANAGER_REQUEST_RESTORE_STA_BIT = BIT5;

/* @brief When set, means a client requested to disconnect from currently connected AP. */
const int WIFI_MANAGER_REQUEST_WIFI_DISCONNECT_BIT = BIT6;

/* @brief When set, means a scan is in progress */
const int WIFI_MANAGER_SCAN_BIT = BIT7;

/* @brief When set, means user requested for a disconnect */
const int WIFI_MANAGER_REQUEST_DISCONNECT_BIT = BIT8;



void wifi_manager_timer_retry_cb( TimerHandle_t xTimer ){

	/* LOCAL PATCH (2.1.4 C5): no retry of the component's own once the AP is up, which it can be by
	 * the time this fires (see wifi_manager_start_retry_timer()) */
	if(xEventGroupGetBits(wifi_manager_event_group) & WIFI_MANAGER_AP_STARTED_BIT){
		xTimerStop( xTimer, (TickType_t) 0 );
		return;
	}

	ESP_LOGI(TAG, "Retry Timer Tick! Sending ORDER_CONNECT_STA with reason CONNECTION_REQUEST_AUTO_RECONNECT");

	/* stop the timer */
	xTimerStop( xTimer, (TickType_t) 0 );

	/* Attempt to reconnect */
	wifi_manager_send_message(WM_ORDER_CONNECT_STA, (void*)CONNECTION_REQUEST_AUTO_RECONNECT);

}

/**
 * @brief LOCAL PATCH (2.1.4 C5): starts the retry timer, only while the AP is down
 * (WIFI_MANAGER_AP_STARTED_BIT clear). While the AP is up the app's router retry is the only
 * retry owner (plan I9): with two, each could send a connect into the other's attempt. The timer
 * callback checks the bit again, for an AP that comes up while the timer runs.
 */
static void wifi_manager_start_retry_timer(){
	if(! (xEventGroupGetBits(wifi_manager_event_group) & WIFI_MANAGER_AP_STARTED_BIT) ){
		xTimerStart( wifi_manager_retry_timer, (TickType_t)0 );
	}
}

/**
 * @brief LOCAL PATCH (2.1.4 C2c): what follows a failed attempt that was not a user's: a lost link,
 * or an automatic retry or the restore at boot that failed or did not start. retries is the task's
 * count of them. Moved here unchanged from STA_DISCONNECTED's lost-connection branch.
 */
static void wifi_manager_retry_or_start_ap(EventBits_t uxBits, uint8_t *retries){

	/* Start the timer that will try to restore the saved config
	 * LOCAL PATCH (2.1.4 C5): only while the AP is down. With the AP up (the router-fallback
	 * portal, or the setup AP's tail after an IP, where it stays up once the STA is lost)
	 * the app's router retry owns the retries; here they went on every few seconds, and
	 * a portal Submit or the app's retry could land in one of their attempts. */
	wifi_manager_start_retry_timer();

	/* if the AP is not started, we check if we have reached the threshold of failed attempt to start it */
	if(! (uxBits & WIFI_MANAGER_AP_STARTED_BIT) ){

		/* if the nunber of retries is below the threshold to start the AP, a reconnection attempt is made
		 * This way we avoid restarting the AP directly in case the connection is mementarily lost */
		if(*retries < WIFI_MANAGER_MAX_RETRY_START_AP){
			(*retries)++;
		}
		else{
			/* In this scenario the connection was lost beyond repair: kick start the AP! */
			*retries = 0;

			/* start SoftAP */
			wifi_manager_send_message(WM_ORDER_START_AP, NULL);
		}
	}
}

void wifi_manager_timer_shutdown_ap_cb( TimerHandle_t xTimer){

	/* stop the timer */
	xTimerStop( xTimer, (TickType_t) 0 );

	/* Attempt to shutdown AP */
	wifi_manager_send_message(WM_ORDER_STOP_AP, NULL);
}

void wifi_manager_scan_async(){
	wifi_manager_send_message(WM_ORDER_START_WIFI_SCAN, NULL);
}

void wifi_manager_disconnect_async(){
	wifi_manager_send_message(WM_ORDER_DISCONNECT_STA, NULL);
}


void wifi_manager_start(){

	/* disable the default wifi logging */
	esp_log_level_set("wifi", ESP_LOG_NONE);

	/* initialize flash memory (idempotent if already done in app_main) */
	esp_err_t nvs_ret = nvs_flash_init();
	if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		nvs_flash_erase();
		nvs_ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(nvs_ret);
	ESP_ERROR_CHECK(nvs_sync_create()); /* semaphore for thread synchronization on NVS memory */

	/* memory allocation */
	/* LOCAL PATCH (2.1.4 C2h): 8 messages, not 3. The event handler and the task itself post with
	 * portMAX_DELAY, so a full queue blocked the default event loop, or the task on its own queue.
	 * 5 more 8-byte slots: +40 B of heap. */
	wifi_manager_queue = xQueueCreate( 8, sizeof( queue_message) );
	wifi_manager_json_mutex = xSemaphoreCreateMutex();
	ip_info_json = (char*)malloc(sizeof(char) * JSON_IP_INFO_SIZE);
	wifi_manager_config_sta = (wifi_config_t*)malloc(sizeof(wifi_config_t));
	cb_ptr_arr = malloc(sizeof(void (*)(void*)) * WM_MESSAGE_CODE_COUNT);
	wifi_manager_sta_ip_mutex = xSemaphoreCreateMutex();
	wifi_manager_sta_ip = (char*)malloc(sizeof(char) * IP4ADDR_STRLEN_MAX);
	wifi_manager_event_group = xEventGroupCreate();

	/* create timer for to keep track of retries */
	wifi_manager_retry_timer = xTimerCreate( NULL, pdMS_TO_TICKS(WIFI_MANAGER_RETRY_TIMER), pdFALSE, ( void * ) 0, wifi_manager_timer_retry_cb);

	/* create timer for to keep track of AP shutdown */
	wifi_manager_shutdown_ap_timer = xTimerCreate( NULL, pdMS_TO_TICKS(WIFI_MANAGER_SHUTDOWN_AP_TIMER), pdFALSE, ( void * ) 0, wifi_manager_timer_shutdown_ap_cb);

	/* LOCAL PATCH (2.1.4 C2d): every allocation above is checked before its first use. The task, its
	 * event handler, the HTTP handlers and the app's calls use them unchecked from here on, so a
	 * failure stops the boot, as the ESP_ERROR_CHECKs above do (at boot, with the heap whole). */
	bool allocated = wifi_manager_queue && wifi_manager_json_mutex && ip_info_json && wifi_manager_config_sta &&
			cb_ptr_arr && wifi_manager_sta_ip_mutex && wifi_manager_sta_ip && wifi_manager_event_group &&
			wifi_manager_retry_timer && wifi_manager_shutdown_ap_timer;
	ESP_ERROR_CHECK(allocated ? ESP_OK : ESP_ERR_NO_MEM);

	wifi_manager_clear_ip_info_json();
	memset(wifi_manager_config_sta, 0x00, sizeof(wifi_config_t));
	memset(&wifi_settings.sta_static_ip_config, 0x00, sizeof(esp_netif_ip_info_t));
	for(int i=0; i<WM_MESSAGE_CODE_COUNT; i++){
		cb_ptr_arr[i] = NULL;
	}
	wifi_manager_safe_update_sta_ip_string((uint32_t)0);

	/* start wifi manager task */
	BaseType_t task_created = xTaskCreate(&wifi_manager, "wifi_manager", 4096, NULL, WIFI_MANAGER_TASK_PRIORITY, &task_wifi_manager);
	ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

esp_err_t wifi_manager_save_sta_config(){

	nvs_handle handle;
	esp_err_t esp_err;
	size_t sz;

	/* variables used to check if write is really needed */
	wifi_config_t tmp_conf;
	struct wifi_settings_t tmp_settings;
	memset(&tmp_conf, 0x00, sizeof(tmp_conf));
	memset(&tmp_settings, 0x00, sizeof(tmp_settings));
	bool change = false;
	bool write_failed = false;

	ESP_LOGI(TAG, "About to save config to flash!!");

	if(wifi_manager_config_sta && nvs_sync_lock( portMAX_DELAY )){

		esp_err = nvs_open(wifi_manager_nvs_namespace, NVS_READWRITE, &handle);
		if (esp_err != ESP_OK){
			nvs_sync_unlock();
			return esp_err;
		}

		/* LOCAL PATCH (2.1.4 C2f): every path from here ends at "done", which closes the handle and
		 * gives the lock back. A failed write returned with the handle open (leaked), and a failed
		 * commit, or a key that could not be read with nothing to write, with the lock held too:
		 * every later save, and the 10 s reset's erase for 3 s, then waited on it. The fields
		 * are compared within their sizes: a 32-byte SSID or a 64-byte password has no terminator,
		 * and strcmp() ran on into the next field. The keys, sizes and meaning are unchanged (the
		 * 10 s reset's erase_wifi_credentials() relies on them): "ssid" 32 B, "password" 64 B,
		 * "settings", namespace "espwifimgr", an empty SSID = nothing saved. */

		sz = sizeof(tmp_conf.sta.ssid);
		esp_err = nvs_get_blob(handle, "ssid", tmp_conf.sta.ssid, &sz);
		if( (esp_err == ESP_OK  || esp_err == ESP_ERR_NVS_NOT_FOUND) &&
				strncmp( (char*)tmp_conf.sta.ssid, (char*)wifi_manager_config_sta->sta.ssid, sizeof(tmp_conf.sta.ssid)) != 0){
			/* different ssid or ssid does not exist in flash: save new ssid */
			esp_err = nvs_set_blob(handle, "ssid", wifi_manager_config_sta->sta.ssid, 32);
			if (esp_err != ESP_OK){
				write_failed = true;
				goto done;
			}
			change = true;
			/* LOCAL PATCH (2.1.4 C1): no credential in the log. The SSID is bounded: a 32-byte one has
			 * no terminator, and %s would run on into the password stored right after it. */
			ESP_LOGI(TAG, "wifi_manager_wrote wifi_sta_config: ssid:%.*s",
					(int)strnlen((char*)wifi_manager_config_sta->sta.ssid, sizeof(wifi_manager_config_sta->sta.ssid)),
					(char*)wifi_manager_config_sta->sta.ssid);

		}

		sz = sizeof(tmp_conf.sta.password);
		esp_err = nvs_get_blob(handle, "password", tmp_conf.sta.password, &sz);
		if( (esp_err == ESP_OK  || esp_err == ESP_ERR_NVS_NOT_FOUND) &&
				strncmp( (char*)tmp_conf.sta.password, (char*)wifi_manager_config_sta->sta.password, sizeof(tmp_conf.sta.password)) != 0){
			/* different password or password does not exist in flash: save new password */
			esp_err = nvs_set_blob(handle, "password", wifi_manager_config_sta->sta.password, 64);
			if (esp_err != ESP_OK){
				write_failed = true;
				goto done;
			}
			change = true;
			ESP_LOGI(TAG, "wifi_manager_wrote wifi_sta_config: pwd_len:%u",
					(unsigned)strnlen((char*)wifi_manager_config_sta->sta.password, sizeof(wifi_manager_config_sta->sta.password)));
		}

		sz = sizeof(tmp_settings);
		esp_err = nvs_get_blob(handle, "settings", &tmp_settings, &sz);
		if( (esp_err == ESP_OK  || esp_err == ESP_ERR_NVS_NOT_FOUND) &&
				(
				strncmp( (char*)tmp_settings.ap_ssid, (char*)wifi_settings.ap_ssid, sizeof(tmp_settings.ap_ssid)) != 0 ||
				strncmp( (char*)tmp_settings.ap_pwd, (char*)wifi_settings.ap_pwd, sizeof(tmp_settings.ap_pwd)) != 0 ||
				tmp_settings.ap_ssid_hidden != wifi_settings.ap_ssid_hidden ||
				tmp_settings.ap_bandwidth != wifi_settings.ap_bandwidth ||
				tmp_settings.sta_only != wifi_settings.sta_only ||
				tmp_settings.sta_power_save != wifi_settings.sta_power_save ||
				tmp_settings.ap_channel != wifi_settings.ap_channel
				)
		){
			esp_err = nvs_set_blob(handle, "settings", &wifi_settings, sizeof(wifi_settings));
			if (esp_err != ESP_OK){
				write_failed = true;
				goto done;
			}
			change = true;

			/* LOCAL PATCH (2.1.4 C1): as above */
			ESP_LOGD(TAG, "wifi_manager_wrote wifi_settings: SoftAP_ssid: %.*s",
					(int)strnlen((char*)wifi_settings.ap_ssid, sizeof(wifi_settings.ap_ssid)), (char*)wifi_settings.ap_ssid);
			ESP_LOGD(TAG, "wifi_manager_wrote wifi_settings: SoftAP_pwd_len: %u",
					(unsigned)strnlen((char*)wifi_settings.ap_pwd, sizeof(wifi_settings.ap_pwd)));
			ESP_LOGD(TAG, "wifi_manager_wrote wifi_settings: SoftAP_channel: %i",wifi_settings.ap_channel);
			ESP_LOGD(TAG, "wifi_manager_wrote wifi_settings: SoftAP_hidden (1 = yes): %i",wifi_settings.ap_ssid_hidden);
			ESP_LOGD(TAG, "wifi_manager_wrote wifi_settings: SoftAP_bandwidth (1 = 20MHz, 2 = 40MHz): %i",wifi_settings.ap_bandwidth);
			ESP_LOGD(TAG, "wifi_manager_wrote wifi_settings: sta_only (0 = APSTA, 1 = STA when connected): %i",wifi_settings.sta_only);
			ESP_LOGD(TAG, "wifi_manager_wrote wifi_settings: sta_power_save (1 = yes): %i",wifi_settings.sta_power_save);
		}

		if(change){
			esp_err = nvs_commit(handle);
			write_failed = (esp_err != ESP_OK);
		}
		else{
			ESP_LOGI(TAG, "Wifi config was not saved to flash because no change has been detected.");
		}

done:
		nvs_close(handle);
		nvs_sync_unlock();

		if(write_failed){
			ESP_LOGW(TAG, "Wi-Fi config not saved to flash (%s)", esp_err_to_name(esp_err));
		}
		return esp_err;

	}
	else{
		ESP_LOGE(TAG, "wifi_manager_save_sta_config failed to acquire nvs_sync mutex");
	}

	return ESP_OK;
}

bool wifi_manager_fetch_wifi_sta_config(){

	nvs_handle handle;
	esp_err_t esp_err;
	if(nvs_sync_lock( portMAX_DELAY )){

		esp_err = nvs_open(wifi_manager_nvs_namespace, NVS_READONLY, &handle);

		if(esp_err != ESP_OK){
			nvs_sync_unlock();
			return false;
		}

		/* LOCAL PATCH (2.1.4 C2f): every path closes the handle and gives the lock back: a missing
		 * key returned with the handle open, about 50 B of heap leaked at each boot with nothing
		 * saved (reset_button.c). The blobs are read through a buffer on the stack, not a malloc()
		 * used unchecked (of which only 4 bytes were cleared). The reads, their order and what a
		 * missing key leaves in the RAM copy are as before. */
		bool found = false;
		if(wifi_manager_config_sta == NULL){
			wifi_manager_config_sta = (wifi_config_t*)malloc(sizeof(wifi_config_t));
		}
		if(wifi_manager_config_sta != NULL){

			memset(wifi_manager_config_sta, 0x00, sizeof(wifi_config_t));

			/* buffer */
			uint8_t buff[sizeof(wifi_settings)];
			memset(buff, 0x00, sizeof(buff));
			size_t sz;

			/* ssid */
			sz = sizeof(wifi_manager_config_sta->sta.ssid);
			esp_err = nvs_get_blob(handle, "ssid", buff, &sz);
			if(esp_err == ESP_OK){
				memcpy(wifi_manager_config_sta->sta.ssid, buff, sz);

				/* password */
				sz = sizeof(wifi_manager_config_sta->sta.password);
				esp_err = nvs_get_blob(handle, "password", buff, &sz);
			}
			if(esp_err == ESP_OK){
				memcpy(wifi_manager_config_sta->sta.password, buff, sz);

				/* settings */
				sz = sizeof(wifi_settings);
				esp_err = nvs_get_blob(handle, "settings", buff, &sz);
			}
			if(esp_err == ESP_OK){
				memcpy(&wifi_settings, buff, sz);
				found = true;
			}
		}

		nvs_close(handle);
		nvs_sync_unlock();

		if(!found){
			return false;
		}


		/* LOCAL PATCH (2.1.4 C1): no credential in the log, and the SSIDs bounded (see the save above) */
		ESP_LOGI(TAG, "wifi_manager_fetch_wifi_sta_config: ssid:%.*s pwd_len:%u",
				(int)strnlen((char*)wifi_manager_config_sta->sta.ssid, sizeof(wifi_manager_config_sta->sta.ssid)),
				(char*)wifi_manager_config_sta->sta.ssid,
				(unsigned)strnlen((char*)wifi_manager_config_sta->sta.password, sizeof(wifi_manager_config_sta->sta.password)));
		ESP_LOGD(TAG, "wifi_manager_fetch_wifi_settings: SoftAP_ssid:%.*s",
				(int)strnlen((char*)wifi_settings.ap_ssid, sizeof(wifi_settings.ap_ssid)), (char*)wifi_settings.ap_ssid);
		ESP_LOGD(TAG, "wifi_manager_fetch_wifi_settings: SoftAP_pwd_len:%u",
				(unsigned)strnlen((char*)wifi_settings.ap_pwd, sizeof(wifi_settings.ap_pwd)));
		ESP_LOGD(TAG, "wifi_manager_fetch_wifi_settings: SoftAP_channel:%i",wifi_settings.ap_channel);
		ESP_LOGD(TAG, "wifi_manager_fetch_wifi_settings: SoftAP_hidden (1 = yes):%i",wifi_settings.ap_ssid_hidden);
		ESP_LOGD(TAG, "wifi_manager_fetch_wifi_settings: SoftAP_bandwidth (1 = 20MHz, 2 = 40MHz)%i",wifi_settings.ap_bandwidth);
		ESP_LOGD(TAG, "wifi_manager_fetch_wifi_settings: sta_only (0 = APSTA, 1 = STA when connected):%i",wifi_settings.sta_only);
		ESP_LOGD(TAG, "wifi_manager_fetch_wifi_settings: sta_power_save (1 = yes):%i",wifi_settings.sta_power_save);
		ESP_LOGD(TAG, "wifi_manager_fetch_wifi_settings: sta_static_ip (0 = dhcp client, 1 = static ip):%i",wifi_settings.sta_static_ip);

		return wifi_manager_config_sta->sta.ssid[0] != '\0';


	}
	else{
		return false;
	}

}


void wifi_manager_clear_ip_info_json(){
	strcpy(ip_info_json, "{}\n");
}


void wifi_manager_generate_ip_info_json(update_reason_code_t update_reason_code){

	wifi_config_t *config = wifi_manager_get_wifi_sta_config();
	if(config){

		/* LOCAL PATCH (2.1.4 C2e): built with bounds. json_print_ssid() writes the SSID bounded by
		 * its 32-byte field, which has no terminator when the SSID is 32 bytes long (the password
		 * stored after it ran into this JSON), and a raw SSID (json.h) gets "raw":1 after it. */
		const char *ip_info_json_format = "%s,\"ip\":\"%s\",\"netmask\":\"%s\",\"gw\":\"%s\",\"urc\":%d}\n";

		/* the reason code tells why this was updated without a connection: "0" for each address then */
		char ip[IP4ADDR_STRLEN_MAX] = "0"; /* note: IP4ADDR_STRLEN_MAX is defined in lwip */
		char gw[IP4ADDR_STRLEN_MAX] = "0";
		char netmask[IP4ADDR_STRLEN_MAX] = "0";
		if(update_reason_code == UPDATE_CONNECTION_OK){
			esp_netif_ip_info_t ip_info;
			/* LOCAL PATCH (2.1.4 C2d): logged, not ESP_ERROR_CHECK: the addresses stay "0" then */
			esp_err_t err = esp_netif_get_ip_info(esp_netif_sta, &ip_info);
			if(err == ESP_OK){
				esp_ip4addr_ntoa(&ip_info.ip, ip, IP4ADDR_STRLEN_MAX);
				esp_ip4addr_ntoa(&ip_info.gw, gw, IP4ADDR_STRLEN_MAX);
				esp_ip4addr_ntoa(&ip_info.netmask, netmask, IP4ADDR_STRLEN_MAX);
			}
			else{
				ESP_LOGW(TAG, "esp_netif_get_ip_info failed (%s) - status without addresses", esp_err_to_name(err));
			}
		}

		/* to avoid declaring a new buffer we copy the data directly into the buffer at its correct address */
		static const char ssid_key[] = "{\"ssid\":";
		size_t len = sizeof(ssid_key) - 1;
		memcpy(ip_info_json, ssid_key, len);
		bool raw = false;
		size_t ssid_len = json_print_ssid(config->sta.ssid, sizeof(config->sta.ssid), ip_info_json + len, JSON_IP_INFO_SIZE - len, &raw);
		if(ssid_len == 0){
			/* cannot happen: JSON_IP_INFO_SIZE takes the longest SSID */
			wifi_manager_clear_ip_info_json();
			return;
		}
		len += ssid_len;

		/* rest of the information is copied after the ssid */
		snprintf( (ip_info_json + len), JSON_IP_INFO_SIZE - len, ip_info_json_format,
				raw ? ",\"raw\":1" : "",
				ip,
				netmask,
				gw,
				(int)update_reason_code);
	}
	else{
		wifi_manager_clear_ip_info_json();
	}


}


void wifi_manager_clear_access_points_json(){
	/* LOCAL PATCH (2.1.4 C2b): no list while the AP is down */
	if(accessp_json){
		strcpy(accessp_json, "[]\n");
	}
}

/**
 * LOCAL PATCH (2.1.4 C2b): one network of the list, as the page needs it: 35 bytes on the stack,
 * where the wifi_ap_record_t the list was built from took 92 bytes of heap.
 */
typedef struct {
	uint8_t ssid[MAX_SSID_SIZE];	/* zero-padded; no terminator when 32 bytes long */
	uint8_t chan;
	int8_t rssi;
	uint8_t auth;					/* wifi_auth_mode_t */
} wifi_manager_ap_t;

/**
 * @brief LOCAL PATCH (2.1.4 C2b): the most records one scan's list is read for; the driver frees
 * the rest (esp_wifi_clear_ap_list()). The driver keeps them strongest first.
 */
#define WIFI_MANAGER_SCAN_RECORDS_MAX		64

/**
 * LOCAL PATCH (2.1.4 C2e): appends one access point to the list, which is len bytes long, never
 * past limit (the length the list may reach before its closing "]\n"): {"ssid":...,"chan":N,
 * "rssi":N,"auth":N}, with ,"raw":1 before the brace for a raw SSID (json.h), after ",\n" if an
 * entry precedes it. Returns false, *len unchanged, if the entry does not fit.
 */
static bool wifi_manager_ap_json_entry(size_t *len, size_t limit, const uint8_t *ssid, int chan, int rssi, int auth){

	size_t o = *len;
	bool raw = false;

	int n = snprintf(accessp_json + o, limit + 1 - o, "%s{\"ssid\":", (o > 1) ? ",\n" : "");
	if(n < 0 || (size_t)n > limit - o){
		return false;
	}
	o += (size_t)n;

	/* ssid needs to be json escaped. To save on heap memory it's directly printed at the correct address */
	size_t ssid_len = json_print_ssid(ssid, MAX_SSID_SIZE, accessp_json + o, limit + 1 - o, &raw);
	if(ssid_len == 0){
		return false;
	}
	o += ssid_len;

	/* print the rest of the json for this access point: no more string to escape */
	n = snprintf(accessp_json + o, limit + 1 - o, ",\"chan\":%d,\"rssi\":%d,\"auth\":%d%s}",
			chan, rssi, auth, raw ? ",\"raw\":1" : "");
	if(n < 0 || (size_t)n > limit - o){
		return false;
	}

	*len = o + (size_t)n;
	return true;
}

/**
 * @brief Generates the list of access points from count compact entries. Returns how many did not
 * fit. Called under the json lock, with accessp_json allocated.
 */
static unsigned wifi_manager_generate_acess_points_json(const wifi_manager_ap_t *aps, uint16_t count){

	/* LOCAL PATCH (2.1.4 C2e): built with bounds (an SSID of control characters overran the buffer,
	 * N4). An entry that does not fit is left out, and the list stays valid JSON: "[" and the
	 * entries joined by ",\n", then "]\n", as before; "[]\n" for no entry (it was "[", invalid). */
	const size_t limit = ACCESSP_JSON_SIZE - 3;   /* room for the closing "]\n" and the terminator */
	size_t len = 0;
	unsigned left_out = 0;

	accessp_json[len++] = '[';
	for(int i=0; i<count;i++){

		const wifi_manager_ap_t *ap = &aps[i];

		if(!wifi_manager_ap_json_entry(&len, limit, ap->ssid, ap->chan, ap->rssi, ap->auth)){
			left_out++;
		}
	}
	accessp_json[len++] = ']';
	accessp_json[len++] = '\n';
	accessp_json[len] = '\0';

	return left_out;
}

/**
 * @brief LOCAL PATCH (2.1.4 C2b, C2 (b)): reads the last scan's records and rebuilds the network
 * list. Called on a successful SCAN_DONE while the list exists.
 *
 * The records are read from the driver one at a time (esp_wifi_scan_get_ap_record()) into a
 * compact array on this task's stack, keeping the MAX_AP_NUM strongest named networks: one entry
 * per SSID and auth mode, at its strongest access point's RSSI and channel (a hidden network, with
 * no SSID, is left out as before), strongest first. esp_wifi_clear_ap_list() then frees what the
 * driver still holds. A read that fails (it never aborts, where esp_wifi_scan_get_ap_records()
 * under ESP_ERROR_CHECK rebooted the hub) keeps the list as it was. Only the rebuild takes the
 * json lock. In a frame of its own, about 0.7 KB, given back before the SCAN_DONE callback runs.
 */
static __attribute__((noinline)) void wifi_manager_read_ap_records(){

	wifi_manager_ap_t aps[MAX_AP_NUM];
	wifi_ap_record_t rec;
	uint16_t count = 0;
	esp_err_t err = ESP_OK;

	for(int i=0; i<WIFI_MANAGER_SCAN_RECORDS_MAX; i++){

		/* ESP_FAIL: no record left */
		err = esp_wifi_scan_get_ap_record(&rec);
		if(err != ESP_OK){
			break;
		}

		uint8_t ssid[MAX_SSID_SIZE] = { 0 };
		size_t ssid_len = strnlen((const char*)rec.ssid, MAX_SSID_SIZE);
		if(ssid_len == 0){
			continue;
		}
		memcpy(ssid, rec.ssid, ssid_len);

		/* the same SSID and auth mode: one network, at its strongest access point */
		int k;
		for(k=0; k<count; k++){
			if(aps[k].auth == (uint8_t)rec.authmode && memcmp(aps[k].ssid, ssid, MAX_SSID_SIZE) == 0){
				break;
			}
		}
		if(k < count){
			if(rec.rssi > aps[k].rssi){
				aps[k].rssi = rec.rssi;
				aps[k].chan = rec.primary;
			}
			continue;
		}

		/* a new network: in a free entry, else in place of the weakest if it is stronger */
		if(count < MAX_AP_NUM){
			k = count++;
		}
		else{
			k = 0;
			for(int j=1; j<count; j++){
				if(aps[j].rssi < aps[k].rssi){
					k = j;
				}
			}
			if(rec.rssi <= aps[k].rssi){
				continue;
			}
		}
		memcpy(aps[k].ssid, ssid, MAX_SSID_SIZE);
		aps[k].chan = rec.primary;
		aps[k].rssi = rec.rssi;
		aps[k].auth = (uint8_t)rec.authmode;
	}

	/* frees the records not read, or all of them after an error */
	esp_wifi_clear_ap_list();

	if(err != ESP_OK && err != ESP_FAIL){
		ESP_LOGW(TAG, "esp_wifi_scan_get_ap_record failed (%s) - network list kept", esp_err_to_name(err));
		return;
	}

	/* strongest first */
	for(int i=1; i<count; i++){
		wifi_manager_ap_t ap = aps[i];
		int j = i;
		while(j > 0 && aps[j-1].rssi < ap.rssi){
			aps[j] = aps[j-1];
			j--;
		}
		aps[j] = ap;
	}

	/* make sure the http server isn't trying to access the list while it gets refreshed */
	if(wifi_manager_lock_json_buffer( pdMS_TO_TICKS(1000) )){
		unsigned left_out = wifi_manager_generate_acess_points_json(aps, count);
		wifi_manager_unlock_json_buffer();
		if(left_out){
			ESP_LOGW(TAG, "network list: %u access points left out (list buffer full)", left_out);
		}
	}
	else{
		ESP_LOGE(TAG, "could not get access to json mutex in wifi_scan");
	}
}

/**
 * @brief LOCAL PATCH (2.1.4 WP1): the largest free block with the caps has room for size bytes
 * and WIFI_MANAGER_HEAP_MARGIN more (see there).
 */
static bool wifi_manager_heap_has(uint32_t caps, size_t size){
	return heap_caps_get_largest_free_block(caps) >= size + WIFI_MANAGER_HEAP_MARGIN;
}

/**
 * @brief LOCAL PATCH (2.1.4 C2b): allocates the network list (START_AP, or a SCAN_DONE after a
 * failed allocation while the AP is up). wifi_manager task only.
 * LOCAL PATCH (2.1.4 WP1): only while the heap has room to spare (wifi_manager_heap_has()), so
 * a heap that stays low (the AP-start dip, a laptop flood) is not met with a failed malloc() at
 * every scan the page orders, and its "no memory" line is printed once per AP start.
 */
static void wifi_manager_alloc_ap_list(){

	if(accessp_json != NULL){
		return;
	}
	if(wifi_manager_heap_has(MALLOC_CAP_DEFAULT, ACCESSP_JSON_SIZE)){
		if(!wifi_manager_lock_json_buffer( portMAX_DELAY )){
			return;
		}
		accessp_json = (char*)malloc(ACCESSP_JSON_SIZE);
		wifi_manager_clear_access_points_json();
		wifi_manager_unlock_json_buffer();
	}
	if(accessp_json == NULL && !ap_list_logged){
		ap_list_logged = true;
		ESP_LOGW(TAG, "network list: no memory for its %u B - the page lists no network yet", (unsigned)ACCESSP_JSON_SIZE);
	}
}

/**
 * @brief LOCAL PATCH (2.1.4 WP1): starts the AP's HTTP and DNS servers: at START_AP, and again
 * (retry) from the task's loop while one of them is not running (see ap_servers_down). Each start
 * does nothing while its server runs. A retry waits for room in the heap (a start that fails is a
 * failed allocation, MONITOR's allocfail), and prints nothing while it waits. Every try that
 * leaves a server down prints one W line; the try that brings both back prints one too.
 * wifi_manager task only.
 */
static void wifi_manager_start_ap_servers(bool retry){

	ap_servers_tick = xTaskGetTickCount();
	if(retry && !wifi_manager_heap_has(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, WIFI_MANAGER_AP_SERVER_STACK)){
		return;
	}

	bool http_up = http_app_start(true);
	bool dns_up = dns_server_start();
	bool was_down = ap_servers_down;
	ap_servers_down = !(http_up && dns_up);

	if(ap_servers_down){
		ESP_LOGW(TAG, "AP up without its %s - tried again every %d s",
				http_up ? "DNS server" : (dns_up ? "HTTP server" : "HTTP and DNS servers"),
				WIFI_MANAGER_AP_SERVERS_RETRY_MS / 1000);
	}
	else if(was_down){
		ESP_LOGW(TAG, "AP servers running again (HTTP and DNS)");
	}
}



bool wifi_manager_lock_sta_ip_string(TickType_t xTicksToWait){
	if(wifi_manager_sta_ip_mutex){
		if( xSemaphoreTake( wifi_manager_sta_ip_mutex, xTicksToWait ) == pdTRUE ) {
			return true;
		}
		else{
			return false;
		}
	}
	else{
		return false;
	}

}
void wifi_manager_unlock_sta_ip_string(){
	xSemaphoreGive( wifi_manager_sta_ip_mutex );
}

void wifi_manager_safe_update_sta_ip_string(uint32_t ip){

	if(wifi_manager_lock_sta_ip_string(portMAX_DELAY)){

		esp_ip4_addr_t ip4;
		ip4.addr = ip;

		char str_ip[IP4ADDR_STRLEN_MAX];
		esp_ip4addr_ntoa(&ip4, str_ip, IP4ADDR_STRLEN_MAX);

		strcpy(wifi_manager_sta_ip, str_ip);

		ESP_LOGI(TAG, "Set STA IP String to: %s", wifi_manager_sta_ip);

		wifi_manager_unlock_sta_ip_string();
	}
}

char* wifi_manager_get_sta_ip_string(){
	return wifi_manager_sta_ip;
}


bool wifi_manager_lock_json_buffer(TickType_t xTicksToWait){
	if(wifi_manager_json_mutex){
		if( xSemaphoreTake( wifi_manager_json_mutex, xTicksToWait ) == pdTRUE ) {
			return true;
		}
		else{
			return false;
		}
	}
	else{
		return false;
	}

}
void wifi_manager_unlock_json_buffer(){
	xSemaphoreGive( wifi_manager_json_mutex );
}

char* wifi_manager_get_ap_list_json(){
	return accessp_json;
}

bool wifi_manager_scan_in_flight(){
	return scan_in_flight;
}


/**
 * @brief Standard wifi event handler
 */
static void wifi_manager_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){


	if (event_base == WIFI_EVENT){

		switch(event_id){

		/* The Wi-Fi driver will never generate this event, which, as a result, can be ignored by the application event
		 * callback. This event may be removed in future releases. */
		case WIFI_EVENT_WIFI_READY:
			ESP_LOGI(TAG, "WIFI_EVENT_WIFI_READY");
			break;

		/* The scan-done event is triggered by esp_wifi_scan_start() and will arise in the following scenarios:
			  The scan is completed, e.g., the target AP is found successfully, or all channels have been scanned.
			  The scan is stopped by esp_wifi_scan_stop().
			  The esp_wifi_scan_start() is called before the scan is completed. A new scan will override the current
				 scan and a scan-done event will be generated.
			The scan-done event will not arise in the following scenarios:
			  It is a blocked scan.
			  The scan is caused by esp_wifi_connect().
			Upon receiving this event, the event task does nothing. The application event callback needs to call
			esp_wifi_scan_get_ap_num() and esp_wifi_scan_get_ap_records() to fetch the scanned AP list and trigger
			the Wi-Fi driver to free the internal memory which is allocated during the scan (do not forget to do this)!
		 */
		case WIFI_EVENT_SCAN_DONE:
			ESP_LOGD(TAG, "WIFI_EVENT_SCAN_DONE");
	    	xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_SCAN_BIT);
			/* LOCAL PATCH (2.1.4 C2a): the scan's status is the message's parameter itself, so this
			 * handler allocates nothing (an allocation that failed here crashed the hub) */
			uint32_t scan_status = event_data ? ((wifi_event_sta_scan_done_t*)event_data)->status : 1;
	    	wifi_manager_send_message(WM_EVENT_SCAN_DONE, (void*)(uintptr_t)scan_status);
			break;

		/* If esp_wifi_start() returns ESP_OK and the current Wi-Fi mode is Station or AP+Station, then this event will
		 * arise. Upon receiving this event, the event task will initialize the LwIP network interface (netif).
		 * Generally, the application event callback needs to call esp_wifi_connect() to connect to the configured AP. */
		case WIFI_EVENT_STA_START:
			ESP_LOGI(TAG, "");
			break;

		/* If esp_wifi_stop() returns ESP_OK and the current Wi-Fi mode is Station or AP+Station, then this event will arise.
		 * Upon receiving this event, the event task will release the station’s IP address, stop the DHCP client, remove
		 * TCP/UDP-related connections and clear the LwIP station netif, etc. The application event callback generally does
		 * not need to do anything. */
		case WIFI_EVENT_STA_STOP:
			ESP_LOGI(TAG, "WIFI_EVENT_STA_STOP");
			break;

		/* If esp_wifi_connect() returns ESP_OK and the station successfully connects to the target AP, the connection event
		 * will arise. Upon receiving this event, the event task starts the DHCP client and begins the DHCP process of getting
		 * the IP address. Then, the Wi-Fi driver is ready for sending and receiving data. This moment is good for beginning
		 * the application work, provided that the application does not depend on LwIP, namely the IP address. However, if
		 * the application is LwIP-based, then you need to wait until the got ip event comes in. */
		case WIFI_EVENT_STA_CONNECTED:
			ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
			break;

		/* This event can be generated in the following scenarios:
		 *
		 *     When esp_wifi_disconnect(), or esp_wifi_stop(), or esp_wifi_deinit(), or esp_wifi_restart() is called and
		 *     the station is already connected to the AP.
		 *
		 *     When esp_wifi_connect() is called, but the Wi-Fi driver fails to set up a connection with the AP due to certain
		 *     reasons, e.g. the scan fails to find the target AP, authentication times out, etc. If there are more than one AP
		 *     with the same SSID, the disconnected event is raised after the station fails to connect all of the found APs.
		 *
		 *     When the Wi-Fi connection is disrupted because of specific reasons, e.g., the station continuously loses N beacons,
		 *     the AP kicks off the station, the AP’s authentication mode is changed, etc.
		 *
		 * Upon receiving this event, the default behavior of the event task is: - Shuts down the station’s LwIP netif.
		 * - Notifies the LwIP task to clear the UDP/TCP connections which cause the wrong status to all sockets. For socket-based
		 * applications, the application callback can choose to close all sockets and re-create them, if necessary, upon receiving
		 * this event.
		 *
		 * The most common event handle code for this event in application is to call esp_wifi_connect() to reconnect the Wi-Fi.
		 * However, if the event is raised because esp_wifi_disconnect() is called, the application should not call esp_wifi_connect()
		 * to reconnect. It’s application’s responsibility to distinguish whether the event is caused by esp_wifi_disconnect() or
		 * other reasons. Sometimes a better reconnect strategy is required, refer to <Wi-Fi Reconnect> and
		 * <Scan When Wi-Fi Is Connecting>.
		 *
		 * Another thing deserves our attention is that the default behavior of LwIP is to abort all TCP socket connections on
		 * receiving the disconnect. Most of time it is not a problem. However, for some special application, this may not be
		 * what they want, consider following scenarios:
		 *
		 *    The application creates a TCP connection to maintain the application-level keep-alive data that is sent out
		 *    every 60 seconds.
		 *
		 *    Due to certain reasons, the Wi-Fi connection is cut off, and the <WIFI_EVENT_STA_DISCONNECTED> is raised.
		 *    According to the current implementation, all TCP connections will be removed and the keep-alive socket will be
		 *    in a wrong status. However, since the application designer believes that the network layer should NOT care about
		 *    this error at the Wi-Fi layer, the application does not close the socket.
		 *
		 *    Five seconds later, the Wi-Fi connection is restored because esp_wifi_connect() is called in the application
		 *    event callback function. Moreover, the station connects to the same AP and gets the same IPV4 address as before.
		 *
		 *    Sixty seconds later, when the application sends out data with the keep-alive socket, the socket returns an error
		 *    and the application closes the socket and re-creates it when necessary.
		 *
		 * In above scenario, ideally, the application sockets and the network layer should not be affected, since the Wi-Fi
		 * connection only fails temporarily and recovers very quickly. The application can enable “Keep TCP connections when
		 * IP changed” via LwIP menuconfig.*/
		case WIFI_EVENT_STA_DISCONNECTED:
			ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");

			/* LOCAL PATCH (2.1.4 C2a): the reason is the message's parameter itself (see the scan above) */
			uint8_t disconnect_reason = event_data ? ((wifi_event_sta_disconnected_t*)event_data)->reason : 0;

			/* if a DISCONNECT message is posted while a scan is in progress this scan will NEVER end, causing scan to never work again. For this reason SCAN_BIT is cleared too */
			xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_WIFI_CONNECTED_BIT | WIFI_MANAGER_SCAN_BIT);

			/* post disconnect event with reason code */
			wifi_manager_send_message(WM_EVENT_STA_DISCONNECTED, (void*)(uintptr_t)disconnect_reason );
			break;

		/* This event arises when the AP to which the station is connected changes its authentication mode, e.g., from no auth
		 * to WPA. Upon receiving this event, the event task will do nothing. Generally, the application event callback does
		 * not need to handle this either. */
		case WIFI_EVENT_STA_AUTHMODE_CHANGE:
			ESP_LOGI(TAG, "WIFI_EVENT_STA_AUTHMODE_CHANGE");
			break;

		case WIFI_EVENT_AP_START:
			ESP_LOGI(TAG, "WIFI_EVENT_AP_START");
			xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_AP_STARTED_BIT);
			break;

		case WIFI_EVENT_AP_STOP:
			ESP_LOGI(TAG, "WIFI_EVENT_AP_STOP");
			xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_AP_STARTED_BIT);
			break;

		/* Every time a station is connected to ESP32 AP, the <WIFI_EVENT_AP_STACONNECTED> will arise. Upon receiving this
		 * event, the event task will do nothing, and the application callback can also ignore it. However, you may want
		 * to do something, for example, to get the info of the connected STA, etc. */
		case WIFI_EVENT_AP_STACONNECTED:
			ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED");
			break;

		/* This event can happen in the following scenarios:
		 *   The application calls esp_wifi_disconnect(), or esp_wifi_deauth_sta(), to manually disconnect the station.
		 *   The Wi-Fi driver kicks off the station, e.g. because the AP has not received any packets in the past five minutes, etc.
		 *   The station kicks off the AP.
		 * When this event happens, the event task will do nothing, but the application event callback needs to do
		 * something, e.g., close the socket which is related to this station, etc. */
		case WIFI_EVENT_AP_STADISCONNECTED:
			ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED");
			break;

		/* This event is disabled by default. The application can enable it via API esp_wifi_set_event_mask().
		 * When this event is enabled, it will be raised each time the AP receives a probe request. */
		case WIFI_EVENT_AP_PROBEREQRECVED:
			ESP_LOGI(TAG, "WIFI_EVENT_AP_PROBEREQRECVED");
			break;

		} /* end switch */
	}
	else if(event_base == IP_EVENT){

		switch(event_id){

		/* This event arises when the DHCP client successfully gets the IPV4 address from the DHCP server,
		 * or when the IPV4 address is changed. The event means that everything is ready and the application can begin
		 * its tasks (e.g., creating sockets).
		 * The IPV4 may be changed because of the following reasons:
		 *    The DHCP client fails to renew/rebind the IPV4 address, and the station’s IPV4 is reset to 0.
		 *    The DHCP client rebinds to a different address.
		 *    The static-configured IPV4 address is changed.
		 * Whether the IPV4 address is changed or NOT is indicated by field ip_change of ip_event_got_ip_t.
		 * The socket is based on the IPV4 address, which means that, if the IPV4 changes, all sockets relating to this
		 * IPV4 will become abnormal. Upon receiving this event, the application needs to close all sockets and recreate
		 * the application when the IPV4 changes to a valid one. */
		case IP_EVENT_STA_GOT_IP:
			ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");
	        xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_WIFI_CONNECTED_BIT);
			/* LOCAL PATCH (2.1.4 C2a): the IPv4 address is the message's parameter itself (see the scan above) */
			uint32_t got_ip = event_data ? ((ip_event_got_ip_t*)event_data)->ip_info.ip.addr : 0;
	        wifi_manager_send_message(WM_EVENT_STA_GOT_IP, (void*)(uintptr_t)got_ip );
			break;

		/* This event arises when the IPV6 SLAAC support auto-configures an address for the ESP32, or when this address changes.
		 * The event means that everything is ready and the application can begin its tasks (e.g., creating sockets). */
		case IP_EVENT_GOT_IP6:
			ESP_LOGI(TAG, "IP_EVENT_GOT_IP6");
			break;

		/* This event arises when the IPV4 address become invalid.
		 * IP_STA_LOST_IP doesn’t arise immediately after the WiFi disconnects, instead it starts an IPV4 address lost timer,
		 * if the IPV4 address is got before ip lost timer expires, IP_EVENT_STA_LOST_IP doesn’t happen. Otherwise, the event
		 * arises when IPV4 address lost timer expires.
		 * Generally the application don’t need to care about this event, it is just a debug event to let the application
		 * know that the IPV4 address is lost. */
		case IP_EVENT_STA_LOST_IP:
			ESP_LOGI(TAG, "IP_EVENT_STA_LOST_IP");
			break;

		}
	}

}


wifi_config_t* wifi_manager_get_wifi_sta_config(){
	return wifi_manager_config_sta;
}


void wifi_manager_connect_async(){
	/* in order to avoid a false positive on the front end app we need to quickly flush the ip json
	 * There'se a risk the front end sees an IP or a password error when in fact
	 * it's a remnant from a previous connection
	 */
	if(wifi_manager_lock_json_buffer( portMAX_DELAY )){
		wifi_manager_clear_ip_info_json();
		wifi_manager_unlock_json_buffer();
	}
	wifi_manager_send_message(WM_ORDER_CONNECT_STA, (void*)CONNECTION_REQUEST_USER);
}


char* wifi_manager_get_ip_info_json(){
	return ip_info_json;
}


void wifi_manager_destroy(){

	vTaskDelete(task_wifi_manager);
	task_wifi_manager = NULL;

	/* heap buffers */
	free(accessp_json);
	accessp_json = NULL;
	free(ip_info_json);
	ip_info_json = NULL;
	free(wifi_manager_sta_ip);
	wifi_manager_sta_ip = NULL;
	if(wifi_manager_config_sta){
		free(wifi_manager_config_sta);
		wifi_manager_config_sta = NULL;
	}

	/* RTOS objects */
	vSemaphoreDelete(wifi_manager_json_mutex);
	wifi_manager_json_mutex = NULL;
	vSemaphoreDelete(wifi_manager_sta_ip_mutex);
	wifi_manager_sta_ip_mutex = NULL;
	vEventGroupDelete(wifi_manager_event_group);
	wifi_manager_event_group = NULL;
	vQueueDelete(wifi_manager_queue);
	wifi_manager_queue = NULL;


}


BaseType_t wifi_manager_send_message_to_front(message_code_t code, void *param){
	queue_message msg;
	msg.code = code;
	msg.param = param;
	return xQueueSendToFront( wifi_manager_queue, &msg, portMAX_DELAY);
}

BaseType_t wifi_manager_send_message(message_code_t code, void *param){
	queue_message msg;
	msg.code = code;
	msg.param = param;
	return xQueueSend( wifi_manager_queue, &msg, portMAX_DELAY);
}


void wifi_manager_set_callback(message_code_t message_code, void (*func_ptr)(void*) ){

	if(cb_ptr_arr && message_code < WM_MESSAGE_CODE_COUNT){
		cb_ptr_arr[message_code] = func_ptr;
	}
}

esp_netif_t* wifi_manager_get_esp_netif_ap(){
	return esp_netif_ap;
}

esp_netif_t* wifi_manager_get_esp_netif_sta(){
	return esp_netif_sta;
}

void wifi_manager( void * pvParameters ){


	queue_message msg;
	BaseType_t xStatus;
	EventBits_t uxBits;
	uint8_t	retries = 0;


	/* initialize the tcp stack */
	ESP_ERROR_CHECK(esp_netif_init());

	/* event loop for the wifi driver */
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	esp_netif_sta = esp_netif_create_default_wifi_sta();
	esp_netif_ap = esp_netif_create_default_wifi_ap();


	/* default wifi config */
	wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

	/* event handler for the connection */
    esp_event_handler_instance_t instance_wifi_event;
    esp_event_handler_instance_t instance_ip_event;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_manager_event_handler, NULL,&instance_wifi_event));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_manager_event_handler, NULL,&instance_ip_event));


	/* SoftAP - Wifi Access Point configuration setup */
	wifi_config_t ap_config = {
		.ap = {
			.ssid_len = 0,
			.channel = wifi_settings.ap_channel,
			.ssid_hidden = wifi_settings.ap_ssid_hidden,
			.max_connection = DEFAULT_AP_MAX_CONNECTIONS,
			.beacon_interval = DEFAULT_AP_BEACON_INTERVAL,
		},
	};
	memcpy(ap_config.ap.ssid, wifi_settings.ap_ssid , sizeof(wifi_settings.ap_ssid));

	/* if the password lenght is under 8 char which is the minium for WPA2, the access point starts as open */
	/* LOCAL PATCH (2.1.4 C2f): bounded, the field comes from NVS and a 64-byte password has no terminator */
	if(strnlen( (char*)wifi_settings.ap_pwd, sizeof(wifi_settings.ap_pwd)) < WPA2_MINIMUM_PASSWORD_LENGTH){
		ap_config.ap.authmode = WIFI_AUTH_OPEN;
		memset( ap_config.ap.password, 0x00, sizeof(ap_config.ap.password) );
	}
	else{
		ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
		memcpy(ap_config.ap.password, wifi_settings.ap_pwd, sizeof(wifi_settings.ap_pwd));
	}
	

	/* DHCP AP configuration */
	esp_netif_dhcps_stop(esp_netif_ap); /* DHCP client/server must be stopped before setting new IP information. */
	esp_netif_ip_info_t ap_ip_info;
	memset(&ap_ip_info, 0x00, sizeof(ap_ip_info));
	inet_pton(AF_INET, DEFAULT_AP_IP, &ap_ip_info.ip);
	inet_pton(AF_INET, DEFAULT_AP_GATEWAY, &ap_ip_info.gw);
	inet_pton(AF_INET, DEFAULT_AP_NETMASK, &ap_ip_info.netmask);
	ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif_ap, &ap_ip_info));
	ESP_ERROR_CHECK(esp_netif_dhcps_start(esp_netif_ap));

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config));
	ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, wifi_settings.ap_bandwidth));
	ESP_ERROR_CHECK(esp_wifi_set_ps(wifi_settings.sta_power_save));


	/* by default the mode is STA because wifi_manager will not start the access point unless it has to! */
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_start());

	/* start http server */
	http_app_start(false);

	/* wifi scanner config */
	wifi_scan_config_t scan_config = {
		.ssid = 0,
		.bssid = 0,
		.channel = 0,
		.show_hidden = true
	};

	/* enqueue first event: load previous config */
	wifi_manager_send_message(WM_ORDER_LOAD_AND_RESTORE_STA, NULL);


	/* main processing loop */
	for(;;){
		/* LOCAL PATCH (2.1.4 WP1): with the AP up and one of its servers down, they are started
		 * again every WIFI_MANAGER_AP_SERVERS_RETRY_MS between messages, and the wait for the next
		 * message ends at the next try. Otherwise the task waits for a message as before. */
		TickType_t wait = portMAX_DELAY;
		if(ap_servers_down){
			TickType_t since = xTaskGetTickCount() - ap_servers_tick;
			if(since >= pdMS_TO_TICKS(WIFI_MANAGER_AP_SERVERS_RETRY_MS)){
				wifi_manager_start_ap_servers(true);
				since = 0;
			}
			if(ap_servers_down){
				wait = pdMS_TO_TICKS(WIFI_MANAGER_AP_SERVERS_RETRY_MS) - since;
			}
		}

		xStatus = xQueueReceive( wifi_manager_queue, &msg, wait );

		if( xStatus == pdPASS ){
			switch(msg.code){

			case WM_EVENT_SCAN_DONE:{
				/* LOCAL PATCH (2.1.4 C2a): the parameter is the scan's status (0 = success), not a pointer */
				uint32_t scan_status = (uint32_t)(uintptr_t)msg.param;
				/* LOCAL PATCH (2.1.4 WP1): the scan is over, done, failed or stopped */
				scan_in_flight = false;
				/* LOCAL PATCH (2.1.4 C2b, C2 (b)): a list that failed to allocate at START_AP is tried
				 * again while the AP is up */
				if(ap_list_wanted){
					wifi_manager_alloc_ap_list();
				}
				/* only check for AP if the scan is succesful, and while there is a list to build */
				if(scan_status == 0 && accessp_json != NULL){
					wifi_manager_read_ap_records();
				}
				else{
					/* a failed scan, or no list: free whatever the driver keeps of it */
					esp_wifi_clear_ap_list();
				}

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])( msg.param );
				}
				break;

			case WM_ORDER_START_WIFI_SCAN:
				ESP_LOGD(TAG, "MESSAGE: ORDER_START_WIFI_SCAN");

				/* if a scan is already in progress this message is simply ignored thanks to the WIFI_MANAGER_SCAN_BIT uxBit */
				uxBits = xEventGroupGetBits(wifi_manager_event_group);
				if(! (uxBits & WIFI_MANAGER_SCAN_BIT) ){
					xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_SCAN_BIT);
					/* LOCAL PATCH: scan can fail transiently with ESP_ERR_WIFI_STATE when
					 * a connect/disconnect is in flight (captive-portal race). Do NOT abort —
					 * clear the bit so a later scan request can retry. */
					esp_err_t scan_err = esp_wifi_scan_start(&scan_config, false);
					if(scan_err != ESP_OK){
						ESP_LOGW(TAG, "esp_wifi_scan_start failed (%s) — skipping scan", esp_err_to_name(scan_err));
						xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_SCAN_BIT);
					}
					else{
						scan_in_flight = true;	/* LOCAL PATCH (2.1.4 WP1) */
					}
				}

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case WM_ORDER_LOAD_AND_RESTORE_STA:
				ESP_LOGI(TAG, "MESSAGE: ORDER_LOAD_AND_RESTORE_STA");
				if(wifi_manager_fetch_wifi_sta_config()){
					ESP_LOGI(TAG, "Saved wifi found on startup. Will attempt to connect.");
					wifi_manager_send_message(WM_ORDER_CONNECT_STA, (void*)CONNECTION_REQUEST_RESTORE_CONNECTION);
				}
				else{
					/* no wifi saved: start soft AP! This is what should happen during a first run */
					ESP_LOGI(TAG, "No saved wifi found on startup. Starting access point.");
					wifi_manager_send_message(WM_ORDER_START_AP, NULL);
				}

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case WM_ORDER_CONNECT_STA:{
				ESP_LOGI(TAG, "MESSAGE: ORDER_CONNECT_STA");

				/* LOCAL PATCH (2.1.4 C2c): esp_wifi_set_config() and esp_wifi_connect() were each under
				 * ESP_ERROR_CHECK, and both fail at runtime: ESP_ERR_WIFI_STATE when an attempt is still
				 * connecting (a portal Submit, the app's router retry and the retry timer each send their
				 * own) or a scan cannot stop in time, ESP_ERR_WIFI_SSID for an empty SSID (the retry timer
				 * after a forget), others at low heap. A connect that does not start is now a failed
				 * attempt of its kind (below), and nothing reboots. */
				connection_request_made_by_code_t request = (connection_request_made_by_code_t)(uintptr_t)msg.param;
				esp_err_t connect_err = ESP_OK;
				bool config_failed = false;

				uxBits = xEventGroupGetBits(wifi_manager_event_group);
				if( ! (uxBits & WIFI_MANAGER_WIFI_CONNECTED_BIT) ){
					/* update config to latest and attempt connection */
					connect_err = esp_wifi_set_config(ESP_IF_WIFI_STA, wifi_manager_get_wifi_sta_config());
					config_failed = (connect_err != ESP_OK);

					if(!config_failed){
						/* if there is a wifi scan in progress abort it first
						   Calling esp_wifi_scan_stop will trigger a SCAN_DONE event which will reset this bit */
						if(uxBits & WIFI_MANAGER_SCAN_BIT){
							esp_wifi_scan_stop();
						}
						connect_err = esp_wifi_connect();
					}

					if(connect_err == ESP_OK){
						/* very important: precise that this connection attempt is specifically requested.
						 * Param in that case is a boolean indicating if the request was made automatically
						 * by the wifi_manager.
						 * LOCAL PATCH (2.1.4 C2c): set once the attempt has started, never for one that did
						 * not, nor with the STA connected (no attempt starts then): a bit left set was read
						 * by a later, unrelated disconnect as this request's failure, with no retry and no
						 * AP after it.
						 * */
						if(request == CONNECTION_REQUEST_USER) {
							xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_STA_CONNECT_BIT);
						}
						else if(request == CONNECTION_REQUEST_RESTORE_CONNECTION) {
							xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_RESTORE_STA_BIT);
						}
					}
					else{
						ESP_LOGW(TAG, "ORDER_CONNECT_STA: %s failed (%s) - attempt not started",
								config_failed ? "esp_wifi_set_config" : "esp_wifi_connect", esp_err_to_name(connect_err));

						if(request == CONNECTION_REQUEST_USER){
							/* a user's request (the portal page's Connect, or the app's router retry): its
							 * status reads failed, for the SSID it asked for, as for an attempt that fails */
							if(wifi_manager_lock_json_buffer( portMAX_DELAY )){
								wifi_manager_generate_ip_info_json( UPDATE_FAILED_ATTEMPT );
								wifi_manager_unlock_json_buffer();
							}
							/* what the page wrote into the RAM copy is dropped if the driver refused it:
							 * the copy goes back to the network the driver has. An attempt still
							 * connecting to that one then saves and reports that one at its IP, not what
							 * was typed, and the app's router retry tries it. */
							if(config_failed && wifi_manager_config_sta){
								esp_wifi_get_config(ESP_IF_WIFI_STA, wifi_manager_config_sta);
							}
						}
						else{
							/* an automatic retry or the restore at boot: as a lost connection, its status
							 * and the next retry or the AP (C5: no retry timer with the AP up) */
							if(wifi_manager_lock_json_buffer( portMAX_DELAY )){
								wifi_manager_generate_ip_info_json( UPDATE_LOST_CONNECTION );
								wifi_manager_unlock_json_buffer();
							}
							wifi_manager_retry_or_start_ap(uxBits, &retries);
						}
					}
				}

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				/* LOCAL PATCH (2.1.4 C2c): an attempt that did not start ends here for the app too, which
				 * counts one from the callback above: its STA_DISCONNECTED callback, with the reason
				 * WIFI_REASON_CONNECTION_FAIL, as after an attempt that failed. Not with the STA
				 * connected: nothing started or ended then. */
				if(connect_err != ESP_OK && cb_ptr_arr[WM_EVENT_STA_DISCONNECTED]){
					(*cb_ptr_arr[WM_EVENT_STA_DISCONNECTED])( (void*)(uintptr_t)WIFI_REASON_CONNECTION_FAIL );
				}

				}
				break;

			case WM_EVENT_STA_DISCONNECTED:
				/* LOCAL PATCH (2.1.4 C2a): the parameter is the disconnect reason, not a pointer */
				;uint8_t disconnect_reason = (uint8_t)(uintptr_t)msg.param;
				ESP_LOGI(TAG, "MESSAGE: EVENT_STA_DISCONNECTED with Reason code: %d", disconnect_reason);

				/* this even can be posted in numerous different conditions
				 *
				 * 1. SSID password is wrong
				 * 2. Manual disconnection ordered
				 * 3. Connection lost
				 *
				 * Having clear understand as to WHY the event was posted is key to having an efficient wifi manager
				 *
				 * With wifi_manager, we determine:
				 *  If WIFI_MANAGER_REQUEST_STA_CONNECT_BIT is set, We consider it's a client that requested the connection.
				 *    When SYSTEM_EVENT_STA_DISCONNECTED is posted, it's probably a password/something went wrong with the handshake.
				 *
				 *  If WIFI_MANAGER_REQUEST_STA_CONNECT_BIT is set, it's a disconnection that was ASKED by the client (clicking disconnect in the app)
				 *    When SYSTEM_EVENT_STA_DISCONNECTED is posted, saved wifi is erased from the NVS memory.
				 *
				 *  If WIFI_MANAGER_REQUEST_STA_CONNECT_BIT and WIFI_MANAGER_REQUEST_STA_CONNECT_BIT are NOT set, it's a lost connection
				 *
				 *  In this version of the software, reason codes are not used. They are indicated here for potential future usage.
				 *
				 *  REASON CODE:
				 *  1		UNSPECIFIED
				 *  2		AUTH_EXPIRE					auth no longer valid, this smells like someone changed a password on the AP
				 *  3		AUTH_LEAVE
				 *  4		ASSOC_EXPIRE
				 *  5		ASSOC_TOOMANY				too many devices already connected to the AP => AP fails to respond
				 *  6		NOT_AUTHED
				 *  7		NOT_ASSOCED
				 *  8		ASSOC_LEAVE					tested as manual disconnect by user OR in the wireless MAC blacklist
				 *  9		ASSOC_NOT_AUTHED
				 *  10		DISASSOC_PWRCAP_BAD
				 *  11		DISASSOC_SUPCHAN_BAD
				 *	12		<n/a>
				 *  13		IE_INVALID
				 *  14		MIC_FAILURE
				 *  15		4WAY_HANDSHAKE_TIMEOUT		wrong password! This was personnaly tested on my home wifi with a wrong password.
				 *  16		GROUP_KEY_UPDATE_TIMEOUT
				 *  17		IE_IN_4WAY_DIFFERS
				 *  18		GROUP_CIPHER_INVALID
				 *  19		PAIRWISE_CIPHER_INVALID
				 *  20		AKMP_INVALID
				 *  21		UNSUPP_RSN_IE_VERSION
				 *  22		INVALID_RSN_IE_CAP
				 *  23		802_1X_AUTH_FAILED			wrong password?
				 *  24		CIPHER_SUITE_REJECTED
				 *  200		BEACON_TIMEOUT
				 *  201		NO_AP_FOUND
				 *  202		AUTH_FAIL
				 *  203		ASSOC_FAIL
				 *  204		HANDSHAKE_TIMEOUT
				 *
				 * */

				/* reset saved sta IP */
				wifi_manager_safe_update_sta_ip_string((uint32_t)0);

				/* if there was a timer on to stop the AP, well now it's time to cancel that since connection was lost! */
				if(xTimerIsTimerActive(wifi_manager_shutdown_ap_timer) == pdTRUE ){
					xTimerStop( wifi_manager_shutdown_ap_timer, (TickType_t)0 );
				}

				uxBits = xEventGroupGetBits(wifi_manager_event_group);
				if( uxBits & WIFI_MANAGER_REQUEST_STA_CONNECT_BIT ){
					/* there are no retries when it's a user requested connection by design. This avoids a user hanging too much
					 * in case they typed a wrong password for instance. Here we simply clear the request bit and move on */
					xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_STA_CONNECT_BIT);

					if(wifi_manager_lock_json_buffer( portMAX_DELAY )){
						wifi_manager_generate_ip_info_json( UPDATE_FAILED_ATTEMPT );
						wifi_manager_unlock_json_buffer();
					}

				}
				else if (uxBits & WIFI_MANAGER_REQUEST_DISCONNECT_BIT){
					/* user manually requested a disconnect so the lost connection is a normal event. Clear the flag and restart the AP */
					xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_DISCONNECT_BIT);

					/* erase configuration */
					if(wifi_manager_config_sta){
						memset(wifi_manager_config_sta, 0x00, sizeof(wifi_config_t));
					}

					/* regenerate json status */
					if(wifi_manager_lock_json_buffer( portMAX_DELAY )){
						wifi_manager_generate_ip_info_json( UPDATE_USER_DISCONNECT );
						wifi_manager_unlock_json_buffer();
					}

					/* save NVS memory */
					wifi_manager_save_sta_config();

					/* start SoftAP */
					wifi_manager_send_message(WM_ORDER_START_AP, NULL);
				}
				else{
					/* lost connection ? */
					if(wifi_manager_lock_json_buffer( portMAX_DELAY )){
						wifi_manager_generate_ip_info_json( UPDATE_LOST_CONNECTION );
						wifi_manager_unlock_json_buffer();
					}

					/* if it was a restore attempt connection, we clear the bit */
					xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_RESTORE_STA_BIT);

					/* the retry timer and the count towards the AP (LOCAL PATCH 2.1.4 C2c: shared with a
					 * connect that does not start) */
					wifi_manager_retry_or_start_ap(uxBits, &retries);
				}

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])( msg.param );

				break;

			case WM_ORDER_START_AP:
				ESP_LOGI(TAG, "MESSAGE: ORDER_START_AP");

				/* LOCAL PATCH: stop the auto-reconnect retry timer while the captive
				 * portal is up. Otherwise it keeps firing ORDER_CONNECT_STA and the
				 * resulting esp_wifi_connect() races with captive-portal scan requests,
				 * producing ESP_ERR_WIFI_STATE in WM_ORDER_START_WIFI_SCAN. The timer
				 * is naturally re-armed by WM_EVENT_STA_DISCONNECTED if a later STA
				 * attempt fails. (2.1.4 C5: only once the AP is down again; while it is
				 * up, the app's router retry is the only retry.) */
				if(xTimerIsTimerActive(wifi_manager_retry_timer) == pdTRUE){
					xTimerStop(wifi_manager_retry_timer, (TickType_t)0);
				}

				/* LOCAL PATCH (2.1.4 C2d): logged, not ESP_ERROR_CHECK (a mode switch can fail for heap).
				 * With no AP nothing else of the portal starts and the callback is not called. The retry
				 * timer stopped above is armed instead: its attempt, failed or not started, counts towards
				 * WIFI_MANAGER_MAX_RETRY_START_AP, which brings the hub back here. */
				esp_err_t ap_err = esp_wifi_set_mode(WIFI_MODE_APSTA);
				if(ap_err != ESP_OK){
					ESP_LOGE(TAG, "ORDER_START_AP: esp_wifi_set_mode failed (%s) - no AP, tried again through the retry timer", esp_err_to_name(ap_err));
					wifi_manager_start_retry_timer();
					break;
				}

				/* restart HTTP daemon */
				http_app_stop();

				/* start HTTP, and DNS
				 * LOCAL PATCH (2.1.4 C4): DNS: nothing to do while it runs (START_AP with the AP up).
				 * It now runs until STOP_AP: no longer stopped at GOT_IP.
				 * LOCAL PATCH (2.1.4 WP1): a server that does not start is started again while the
				 * AP is up (wifi_manager_start_ap_servers()) */
				wifi_manager_start_ap_servers(false);

				/* LOCAL PATCH (2.1.4 C2b): the network list, for as long as the AP is up.
				 * LOCAL PATCH (2.1.4 WP1): after the servers, which captive detection needs (plan
				 * I11): at the AP-start heap dip they have the memory first, and a list that does
				 * not fit then is allocated at a later SCAN_DONE */
				ap_list_wanted = true;
				ap_list_logged = false;
				wifi_manager_alloc_ap_list();

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case WM_ORDER_STOP_AP:
				ESP_LOGI(TAG, "MESSAGE: ORDER_STOP_AP");


				uxBits = xEventGroupGetBits(wifi_manager_event_group);

				/* before stopping the AP, we check that we are still connected. There's a chance that once the timer
				 * kicks in, for whatever reason the esp32 is already disconnected.
				 */
				if(uxBits & WIFI_MANAGER_WIFI_CONNECTED_BIT){

					/* set to STA only */
					esp_wifi_set_mode(WIFI_MODE_STA);

					/* stop DNS
					 * LOCAL PATCH (2.1.4 C4): waits up to 1 s for its task to close its socket */
					dns_server_stop();
					ap_servers_down = false;	/* LOCAL PATCH (2.1.4 WP1): no retry with the AP down */

					/* restart HTTP daemon */
					http_app_stop();
					http_app_start(false);

					/* LOCAL PATCH (2.1.4 C2b): the network list goes with the AP (+1,489 B of heap) */
					ap_list_wanted = false;
					if(wifi_manager_lock_json_buffer( portMAX_DELAY )){
						free(accessp_json);
						accessp_json = NULL;
						wifi_manager_unlock_json_buffer();
					}

					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);
				}

				break;

			case WM_EVENT_STA_GOT_IP:
				ESP_LOGI(TAG, "WM_EVENT_STA_GOT_IP");
				/* LOCAL PATCH (2.1.4 C2a): the parameter is the IPv4 address (network byte order), not a pointer */
				uint32_t got_ip = (uint32_t)(uintptr_t)msg.param;
				uxBits = xEventGroupGetBits(wifi_manager_event_group);

				/* reset connection requests bits -- doesn't matter if it was set or not */
				xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_STA_CONNECT_BIT);

				/* save IP as a string for the HTTP server host */
				wifi_manager_safe_update_sta_ip_string(got_ip);

				/* save wifi config in NVS if it wasn't a restored of a connection */
				if(uxBits & WIFI_MANAGER_REQUEST_RESTORE_STA_BIT){
					xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_RESTORE_STA_BIT);
				}
				else{
					wifi_manager_save_sta_config();
				}

				/* reset number of retries */
				retries = 0;

				/* refresh JSON with the new IP */
				if(wifi_manager_lock_json_buffer( portMAX_DELAY )){
					/* generate the connection info with success */
					wifi_manager_generate_ip_info_json( UPDATE_CONNECTION_OK );
					wifi_manager_unlock_json_buffer();
				}
				else{
					/* LOCAL PATCH (2.1.4 C2d): logged, not abort() (an unbounded wait: only a missing mutex fails it) */
					ESP_LOGE(TAG, "could not get access to json mutex in WM_EVENT_STA_GOT_IP");
				}

				/* LOCAL PATCH (2.1.4 C4): the DNS hijack is no longer brought down here. It stays up with
				 * the AP until STOP_AP, so a phone that joins or re-joins the AP in its tail (the setup
				 * AP's last 60 s, or the AP left up after the STA lost the link again) is still sent to
				 * the portal (plan I11) */

				/* start the timer that will eventually shutdown the access point
				 * We check first that it's actually running because in case of a boot and restore connection
				 * the AP is not even started to begin with.
				 */
				if(uxBits & WIFI_MANAGER_AP_STARTED_BIT){
					TickType_t t = pdMS_TO_TICKS( WIFI_MANAGER_SHUTDOWN_AP_TIMER );

					/* if for whatever reason user configured the shutdown timer to be less than 1 tick, the AP is stopped straight away */
					if(t > 0){
						xTimerStart( wifi_manager_shutdown_ap_timer, (TickType_t)0 );
					}
					else{
						wifi_manager_send_message(WM_ORDER_STOP_AP, (void*)NULL);
					}

				}

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])( msg.param );

				break;

			case WM_ORDER_DISCONNECT_STA:
				ESP_LOGI(TAG, "MESSAGE: ORDER_DISCONNECT_STA");

				/* precise this is coming from a user request */
				xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_DISCONNECT_BIT);

				/* order wifi discconect */
				/* LOCAL PATCH (2.1.4 C2d): logged, not ESP_ERROR_CHECK. The request bit stays set, as it
				 * did: the next disconnect (the app's own forget event for an idle STA) still erases. */
				esp_err_t disconnect_err = esp_wifi_disconnect();
				if(disconnect_err != ESP_OK){
					ESP_LOGW(TAG, "ORDER_DISCONNECT_STA: esp_wifi_disconnect failed (%s)", esp_err_to_name(disconnect_err));
				}

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			default:
				break;

			} /* end of switch/case */
		} /* end of if status=pdPASS */
	} /* end of for loop */

	vTaskDelete( NULL );

}


