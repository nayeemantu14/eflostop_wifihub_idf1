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
/* LOCAL PATCH (2.1.4 C10b): the network list is a cache. GET /ap.json only reads it (it ordered an
 * all-channel scan at every 3.8 s poll); a scan is ordered by the page's load when the list is
 * empty or older than WIFI_MANAGER_LIST_STALE_MS, and by the page's Rescan (POST /scan.json), at
 * least WIFI_MANAGER_SCAN_GAP_MS apart (wifi_manager_scan_request()). In WP8 both become the radio
 * policy's LIST pulse requests.
 * ap_list_tick: the list's last rebuild from a scan, 0 = none since it was allocated (wifi_manager
 * task writes it, the httpd task reads it); scan_order_tick: the last scan ordered for the page,
 * 0 = none (the httpd task sets it; the wifi_manager task moves it back when that scan did not
 * start or ended failed, wifi_manager_scan_failed(), so the next order may come
 * WIFI_MANAGER_SCAN_RETRY_MS later: a Rescan is not refused for 20 s after a scan that never ran,
 * and a failure that repeats, a connect attempt in flight, does not order a scan at every poll;
 * a scan that succeeded with no list allocated, low heap, keeps the 20 s gap). STOP_AP clears
 * both once the HTTP server is stopped: the next AP session's page is a new
 * one, and a list that cannot be allocated at its START_AP must not read as fresh, or the page's
 * load would order no scan, and no SCAN_DONE would retry the allocation. A stale read of either
 * costs one scan too many, or one refused. */
static volatile TickType_t ap_list_tick = 0;
static volatile TickType_t scan_order_tick = 0;
#define WIFI_MANAGER_SCAN_GAP_MS		20000
#define WIFI_MANAGER_SCAN_RETRY_MS		10000
#define WIFI_MANAGER_LIST_LOCK_MS		5000	/* a scan's rebuild waits this long for the list's lock */
#define WIFI_MANAGER_LIST_STALE_MS		60000
/* LOCAL PATCH (2.1.4 WP1): an allocation that can wait (the network list) is tried only while the
 * largest free block exceeds it by this much. One that fails is counted as a failed allocation
 * (MONITOR's allocfail, the figure the memory gates pass on) and replaces the record of the last
 * one, which should name the allocation that could not wait. */
#define WIFI_MANAGER_HEAP_MARGIN	4096
/* LOCAL PATCH (2.1.4 WP1): the AP is up with its HTTP or DNS server not running: START_AP could
 * not start it (httpd_start() or the DNS task's creation failed, for lack of memory), and nothing
 * else would before the next START_AP, which the setup portal may never see (plan I11:
 * both up from START_AP to STOP_AP). The task's loop then starts them again every
 * WIFI_MANAGER_AP_SERVERS_RETRY_MS, counted from ap_servers_tick (the last try), each try once the
 * largest free block has room for a server task's stack (WIFI_MANAGER_AP_SERVER_STACK, httpd's)
 * and WIFI_MANAGER_HEAP_MARGIN. STOP_AP clears it. wifi_manager task only. */
static bool ap_servers_down = false;
static TickType_t ap_servers_tick = 0;
#define WIFI_MANAGER_AP_SERVERS_RETRY_MS	5000
#define WIFI_MANAGER_AP_SERVER_STACK		4096
/* LOCAL PATCH (2.1.4 WP2): a STOP_AP whose switch to STA mode failed is tried again this much
 * later, through the AP-shutdown timer (see WM_ORDER_STOP_AP) */
#define WIFI_MANAGER_STOP_AP_RETRY_MS		5000
/* LOCAL PATCH (2.1.4 WP2): the AP's stop, for wifi_manager_ap_stop_done(). ap_stop_busy is set
 * just before STOP_AP's switch to STA mode and cleared once its DNS task, HTTP server and network
 * list are gone (at once if the switch fails: the AP stays up); ap_stop_done_tick is when the last
 * one finished (forced non-zero), 0 = none yet, written before ap_stop_busy is cleared.
 * wifi_manager task only (read from any task) */
static volatile bool ap_stop_busy = false;
static volatile TickType_t ap_stop_done_tick = 0;
/* LOCAL PATCH (2.1.4 WP1, a bench diagnostic): a scan this task started is in flight, from the
 * esp_wifi_scan_start() that succeeded to this task's WM_EVENT_SCAN_DONE (done, failed or
 * stopped); the radio then visits every channel (wifi_manager_scan_in_flight()). Not cleared at a
 * STA disconnect, where the event handler clears WIFI_MANAGER_SCAN_BIT in case no SCAN_DONE
 * follows: were a SCAN_DONE ever lost, this would stay set until the next scan's, so it errs
 * towards "in flight", never away from it. wifi_manager task only. */
static bool scan_in_flight = false;
/* LOCAL PATCH (2.1.4 C6): a START_AP the task owes itself. It posted START_AP to its own queue
 * with portMAX_DELAY (after its retries, after a forget), and with the queue full it waited on
 * itself for good. Set instead, and run at the top of the loop, right after the message that set
 * it and its callback (the order the post gave). wifi_manager task only. */
static bool start_ap_due = false;
/* The network in use (the RAM copy of the saved one, or all zero: nothing saved). LOCAL PATCH (2.1.4
 * C8): written by the wifi_manager task only, at the boot's load, at a candidate's IP and at a
 * forget; an HTTP handler no longer writes it (it wrote what was typed into it, before any attempt). */
wifi_config_t* wifi_manager_config_sta = NULL;

/* LOCAL PATCH (2.1.4 C8): connect ownership and transactional credentials (plan 6.2 C8, I13).
 *
 * The wifi_manager task owns every connect attempt, one at a time, of four kinds: USER (the setup
 * page's Connect, with its candidate), APP_RETRY (the app's router retry), AUTO (this component's
 * retry timer) and RESTORE (the boot's). attempt_kind is the attempt in flight, from its
 * esp_wifi_connect() to its STA_DISCONNECTED or GOT_IP.
 *
 * A Connect stores a candidate (SSID, password, channel hint) in wm_shared and queues a USER
 * order. The network in use (wifi_manager_config_sta) and NVS change only when the candidate gets
 * an IP: the GOT_IP that finds the driver's config different from the network in use commits it
 * and saves it (wifi_manager_commit_driver_config()). A candidate that fails is reported and
 * dropped, and the network in use is the one the next automatic attempt tries (the router retry
 * or the retry timer): a mistyped password no longer replaces a working network.
 *
 * A USER order never meets a running attempt: with an attempt in flight it waits for it, at most
 * WIFI_MANAGER_USER_WAIT_MS, then ends it (esp_wifi_disconnect()); with the STA connected (the
 * SoftAP's tail, or a router-fallback hub that rejoined) it leaves that network for the candidate,
 * if the SoftAP is up to report it, and does nothing if the candidate is the network in use. An
 * automatic order gives way to a user's candidate and to an attempt in flight, and starts nothing
 * then. A user's attempt with no IP after WIFI_MANAGER_USER_ATTEMPT_MS is ended (an open network
 * with no DHCP, say): it fails with WIFI_MANAGER_REASON_NO_IP and the network in use stays. The
 * latest Connect wins: a newer one replaces a candidate that has not started.
 *
 * Every esp_wifi_disconnect() of ours (ending an attempt, leaving the network for a candidate, a
 * forget) waits for its STA_DISCONNECTED (abort_tick); none in WIFI_MANAGER_ABORT_WAIT_MS ends the
 * attempt as if it had come, so nothing waits for good. Should that attempt get an IP after all,
 * the IP is not committed and the link is left at once (late_ip_leave): a forget has erased its
 * network since, or the page has reported its candidate failed. Only a user's candidate is ever
 * committed (WM_CAND_ACTIVE at its IP); an automatic attempt's IP never changes the network in use.
 *
 * WM_ORDER_CONNECT_STA's callback says what each order did (wifi_manager.h): the kind of an
 * attempt that started, or the kind with WIFI_MANAGER_CONNECT_NOT_STARTED. A USER order that waits
 * is reported when its attempt starts (or does not). No synthetic STA_DISCONNECTED follows an
 * order that did not start any more (2.1.4 C2c's), except the one of an abort that got no event. */
typedef struct {
	/* status.json, as the wifi_manager task last set it; wifi_manager_status_json() formats it */
	uint8_t ssid[MAX_SSID_SIZE];		/* its network: the one in use, or the candidate that failed */
	uint32_t ip;						/* the STA's addresses (network byte order) for urc 0 */
	uint32_t netmask;
	uint32_t gw;
	uint8_t urc;						/* update_reason_code_t, or WIFI_MANAGER_URC_NONE */
	uint8_t reason;						/* that candidate's failure (wifi_err_reason_t, WIFI_MANAGER_REASON_NO_IP), 0 = none */
	/* the user's candidate */
	uint8_t cand_state;					/* WM_CAND_* */
	uint8_t cand_chan;					/* the page's channel hint, 0 = none */
	uint8_t cand_ssid[MAX_SSID_SIZE];	/* zero-padded; no terminator when 32 bytes long */
	uint8_t cand_pwd[MAX_PASSWORD_SIZE];
} wifi_manager_shared_t;

#define WIFI_MANAGER_URC_NONE		0xFF	/* no status yet: status.json is "{}" */

#define WM_CAND_NONE				0		/* no candidate */
#define WM_CAND_POSTED				1		/* stored by an HTTP handler; its USER order is queued */
#define WM_CAND_WAITING				2		/* taken by its order: waits for an attempt or a link to end */
#define WM_CAND_ACTIVE				3		/* its attempt runs: the driver has it, the network in use is unchanged */

/* LOCAL PATCH (2.1.4 C8, C6): status.json and the candidate, allocated at boot in place of the
 * 295 B status JSON, under wm_lock, a spinlock (C6's status lock: a short copy in, or out to the
 * reader's stack, never held while formatting or sending). The HTTP handlers store a candidate and
 * read the status; the wifi_manager task does the rest. */
static wifi_manager_shared_t *wm_shared = NULL;
static portMUX_TYPE wm_lock = portMUX_INITIALIZER_UNLOCKED;

/* LOCAL PATCH (2.1.4 C8): wifi_manager task only */
static uint8_t attempt_kind = CONNECTION_REQUEST_NONE;	/* the attempt in flight */
static TickType_t attempt_tick = 0;			/* its start */
static TickType_t user_wait_tick = 0;		/* when a WM_CAND_WAITING candidate began to wait */
static TickType_t abort_tick = 0;			/* an esp_wifi_disconnect() of ours awaits its event, 0 = none */
static uint8_t abort_reason = 0;			/* the reason that end reports for a user's attempt, 0 = the driver's */
static bool user_due = false;				/* a waiting candidate may go on, after the message's callback */

/* LOCAL PATCH (2.1.4 C13): Wi-Fi parameters the component owns (plan 4.5, 6.2 C13).
 * - The SoftAP announces a channel switch (CSA) WIFI_MANAGER_AP_CSA_COUNT beacons ahead, where it
 *   left csa_count 0 (B6): when the STA joins a router on another channel, the SoftAP follows it,
 *   and a phone on it follows the announcement instead of dropping. PROVISIONAL: G0 reads the
 *   driver's "csa_count" line and decides 3 or 5 (plan 2.4).
 * - DTIM period 1 (it was left 0), set explicitly.
 * - Station scans dwell WIFI_MANAGER_SCAN_ACTIVE_MAX_MS at most on each channel (the driver's
 *   default is 120 ms) and go back to the home channel for WIFI_MANAGER_SCAN_HOME_DWELL_MS between
 *   channels (its default is 30 ms), so the SoftAP and its phone keep air time during a scan; set
 *   once after esp_wifi_start() (the driver takes them only with the station started). They are
 *   the driver's defaults for every station scan, the scan of a connect attempt included (plan
 *   4.5 sets them once); G7 (list completeness, rejoin) checks that 60 ms still finds a weak
 *   router.
 * - At each IP the router's channel goes into the network in use's config (RAM only, never saved:
 *   NVS keeps the SSID and password blobs only), so a later attempt scans it first. */
#define WIFI_MANAGER_AP_CSA_COUNT			3
#define WIFI_MANAGER_AP_DTIM_PERIOD			1
#define WIFI_MANAGER_SCAN_ACTIVE_MAX_MS		60
#define WIFI_MANAGER_SCAN_HOME_DWELL_MS		100

#define WIFI_MANAGER_USER_WAIT_MS		8000	/* a Connect waits this long for a running attempt (plan C8) */
#define WIFI_MANAGER_USER_ATTEMPT_MS	25000	/* a user's attempt with no IP this long is ended (the page waits 30 s) */
#define WIFI_MANAGER_ABORT_WAIT_MS		3000	/* our esp_wifi_disconnect()'s STA_DISCONNECTED, awaited this long */
#define WIFI_MANAGER_STALE_LEAVE_MS		5000	/* after one never came: a late one is ignored this long */
static TickType_t stale_leave_until = 0;	/* wifi_manager task only; 0 = none */
static bool save_owed = false;				/* a committed network's NVS save failed; wifi_manager task only */
static bool on_uncommitted = false;			/* connected to a network not committed (a replaced candidate's,
											 * or one a forget is under way for); wifi_manager task only */
static bool late_ip_leave = false;			/* an attempt counted as ended with no event (abort_expired()):
											 * its IP, should one come, is left; cleared when an attempt
											 * starts or a STA_DISCONNECTED ends it; wifi_manager task only */
static bool leaving_link = false;			/* our disconnect of such a link is under way: its end changes
											 * no status (the result decided before it stands); cleared at
											 * that end, or its expiry; wifi_manager task only */
_Static_assert(WIFI_MANAGER_STATUS_JSON_SIZE >= JSON_IP_INFO_SIZE + 21 + JSON_SSID_STR_MAX,
		"status.json: the status of before, \",\"reason\":255,\"pend\":\" and the candidate's SSID");

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

/* BIT3 (a client requested to connect) and BIT5 (the boot's restore): LOCAL PATCH (2.1.4 C8),
 * no longer used: attempt_kind says which attempt runs. */

/* @brief This bit is set automatically as soon as a connection was lost */
const int WIFI_MANAGER_STA_DISCONNECT_BIT = BIT4;


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

	/* Attempt to reconnect
	 * LOCAL PATCH (2.1.4 C6): never waits (it waited with no bound, holding up the timer task and
	 * every other timer): with the queue full the timer is started again, and tries next time */
	if(wifi_manager_send_message_wait(WM_ORDER_CONNECT_STA, (void*)CONNECTION_REQUEST_AUTO_RECONNECT, (TickType_t)0) != pdPASS){
		ESP_LOGW(TAG, "retry: queue full - tried again in %d ms", WIFI_MANAGER_RETRY_TIMER);
		xTimerStart( xTimer, (TickType_t)0 );
	}

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

			/* start SoftAP (LOCAL PATCH 2.1.4 C6: owed, not posted to this task's own queue) */
			start_ap_due = true;
		}
	}
}

void wifi_manager_timer_shutdown_ap_cb( TimerHandle_t xTimer){

	/* stop the timer */
	xTimerStop( xTimer, (TickType_t) 0 );

	/* Attempt to shutdown AP
	 * LOCAL PATCH (2.1.4 C6): never waits, as the retry timer above: with the queue full the stop
	 * is tried again WIFI_MANAGER_STOP_AP_RETRY_MS later (a lost link stops the timer, as ever) */
	if(wifi_manager_send_message_wait(WM_ORDER_STOP_AP, NULL, (TickType_t)0) != pdPASS){
		ESP_LOGW(TAG, "AP stop: queue full - tried again in %d ms", WIFI_MANAGER_STOP_AP_RETRY_MS);
		xTimerChangePeriod( xTimer, pdMS_TO_TICKS(WIFI_MANAGER_STOP_AP_RETRY_MS), (TickType_t)0 );
	}
}

bool wifi_manager_ap_stop_in(uint32_t ms){

	/* LOCAL PATCH (2.1.4 C12): see wifi_manager.h. Only with the STA connected, as STOP_AP itself */
	if(wifi_manager_event_group == NULL || wifi_manager_shutdown_ap_timer == NULL ||
			!(xEventGroupGetBits(wifi_manager_event_group) & WIFI_MANAGER_WIFI_CONNECTED_BIT)){
		return false;
	}

	/* rounded up to a tick, and at least one: a timer's period cannot be 0 */
	TickType_t t = (TickType_t)(ms / portTICK_PERIOD_MS + ((ms % portTICK_PERIOD_MS) ? 1 : 0));
	if(t == 0){
		t = 1;
	}

	/* never waits: with the timer task's queue full the call fails, and the AP keeps its stop */
	if(xTimerChangePeriod(wifi_manager_shutdown_ap_timer, t, (TickType_t)0) != pdPASS){
		ESP_LOGW(TAG, "AP stop in %lu ms not set (timer queue full)", (unsigned long)ms);
		return false;
	}

	/* the link lost since the check above: its STA_DISCONNECTED stops the timer, and that stop may
	 * have reached the timer task before this re-arm did. Stopped again, after the re-arm (the
	 * timer task takes its commands in the order they were sent), so the AP stays up as the lost
	 * link leaves it */
	if(!(xEventGroupGetBits(wifi_manager_event_group) & WIFI_MANAGER_WIFI_CONNECTED_BIT)){
		xTimerStop(wifi_manager_shutdown_ap_timer, (TickType_t)0);
		return false;
	}
	return true;
}

/* LOCAL PATCH (2.1.4 C6): the requests of other tasks wait WIFI_MANAGER_POST_WAIT_MS at most */
bool wifi_manager_scan_async(){
	return wifi_manager_send_message_wait(WM_ORDER_START_WIFI_SCAN, NULL, pdMS_TO_TICKS(WIFI_MANAGER_POST_WAIT_MS)) == pdPASS;
}

int wifi_manager_scan_request(bool rescan, uint32_t *wait_ms){

	/* LOCAL PATCH (2.1.4 C10b): see wifi_manager.h. httpd task only (scan_order_tick) */
	TickType_t now = xTaskGetTickCount();
	if(wait_ms){
		*wait_ms = 0;
	}
	TickType_t built = ap_list_tick;
	if(!rescan && built != 0 && now - built < pdMS_TO_TICKS(WIFI_MANAGER_LIST_STALE_MS)){
		return 0;
	}
	if(scan_order_tick != 0 && now - scan_order_tick < pdMS_TO_TICKS(WIFI_MANAGER_SCAN_GAP_MS)){
		if(wait_ms){
			*wait_ms = (uint32_t)(pdMS_TO_TICKS(WIFI_MANAGER_SCAN_GAP_MS) - (now - scan_order_tick)) * portTICK_PERIOD_MS;
		}
		return 0;
	}
	/* stamped before the post: the wifi_manager task moves it back if the scan does not start
	 * (wifi_manager_scan_failed()), and may do so before this task would run again. A post that
	 * did not fit is a failed order too: the next may come WIFI_MANAGER_SCAN_RETRY_MS on, so the
	 * page's polls, while the queue stays full, do not each wait WIFI_MANAGER_POST_WAIT_MS on it */
	scan_order_tick = (now != 0) ? now : 1;
	if(!wifi_manager_scan_async()){
		TickType_t t = xTaskGetTickCount() - pdMS_TO_TICKS(WIFI_MANAGER_SCAN_GAP_MS - WIFI_MANAGER_SCAN_RETRY_MS);
		scan_order_tick = (t != 0) ? t : 1;
		return -1;
	}
	return 1;
}

bool wifi_manager_disconnect_async(){
	return wifi_manager_send_message_wait(WM_ORDER_DISCONNECT_STA, NULL, pdMS_TO_TICKS(WIFI_MANAGER_POST_WAIT_MS)) == pdPASS;
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
	wm_shared = (wifi_manager_shared_t*)calloc(1, sizeof(wifi_manager_shared_t));	/* LOCAL PATCH (2.1.4 C8) */
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
	bool allocated = wifi_manager_queue && wifi_manager_json_mutex && wm_shared && wifi_manager_config_sta &&
			cb_ptr_arr && wifi_manager_sta_ip_mutex && wifi_manager_sta_ip && wifi_manager_event_group &&
			wifi_manager_retry_timer && wifi_manager_shutdown_ap_timer;
	ESP_ERROR_CHECK(allocated ? ESP_OK : ESP_ERR_NO_MEM);

	wm_shared->urc = WIFI_MANAGER_URC_NONE;
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


/**
 * @brief LOCAL PATCH (2.1.4 C8): fills n bytes with zeros with stores the compiler keeps (a memset()
 * of a buffer about to go out of scope may be removed): the copies of a password.
 */
static void wifi_manager_wipe(void *p, size_t n){
	volatile uint8_t *v = (volatile uint8_t*)p;
	while(n--){
		*v++ = 0;
	}
}

/* LOCAL PATCH (2.1.4 C8): under wm_lock: the candidate is gone, its bytes zeroed */
static void wifi_manager_cand_clear_locked(){
	wm_shared->cand_state = WM_CAND_NONE;
	wm_shared->cand_chan = 0;
	memset(wm_shared->cand_ssid, 0x00, sizeof(wm_shared->cand_ssid));
	wifi_manager_wipe(wm_shared->cand_pwd, sizeof(wm_shared->cand_pwd));
}

/* LOCAL PATCH (2.1.4 C8): the candidate's state (one byte, read under the lock all the same) */
static uint8_t wifi_manager_cand_state(){
	taskENTER_CRITICAL(&wm_lock);
	uint8_t state = wm_shared->cand_state;
	taskEXIT_CRITICAL(&wm_lock);
	return state;
}

/**
 * @brief LOCAL PATCH (2.1.4 C8): status.json now reads urc for the network in use, with its
 * addresses for UPDATE_CONNECTION_OK ("0" otherwise). A candidate in end_state ends with it (the
 * one whose attempt got this IP: WM_CAND_ACTIVE), in the same lock: status.json never shows it
 * pending after the result. WM_CAND_NONE: no candidate ends. wifi_manager task only.
 */
static void wifi_manager_status_set(update_reason_code_t urc, uint8_t end_state){

	esp_netif_ip_info_t ip_info;
	memset(&ip_info, 0x00, sizeof(ip_info));
	if(urc == UPDATE_CONNECTION_OK){
		/* LOCAL PATCH (2.1.4 C2d): logged, not ESP_ERROR_CHECK: the addresses stay "0" then */
		esp_err_t err = esp_netif_get_ip_info(esp_netif_sta, &ip_info);
		if(err != ESP_OK){
			ESP_LOGW(TAG, "esp_netif_get_ip_info failed (%s) - status without addresses", esp_err_to_name(err));
			memset(&ip_info, 0x00, sizeof(ip_info));
		}
	}

	taskENTER_CRITICAL(&wm_lock);
	memcpy(wm_shared->ssid, wifi_manager_config_sta->sta.ssid, MAX_SSID_SIZE);
	wm_shared->ip = ip_info.ip.addr;
	wm_shared->netmask = ip_info.netmask.addr;
	wm_shared->gw = ip_info.gw.addr;
	wm_shared->urc = (uint8_t)urc;
	wm_shared->reason = 0;
	if(end_state != WM_CAND_NONE && wm_shared->cand_state == end_state){
		wifi_manager_cand_clear_locked();
	}
	taskEXIT_CRITICAL(&wm_lock);
}

/**
 * @brief LOCAL PATCH (2.1.4 C8): the candidate, if in state, failed: status.json reads
 * UPDATE_FAILED_ATTEMPT for its SSID with reason (0: none to give), and it is dropped. A newer
 * candidate (another state) is left alone. Returns whether one failed. wifi_manager task only.
 */
static bool wifi_manager_cand_fail(uint8_t state, uint8_t reason){

	bool failed = false;
	taskENTER_CRITICAL(&wm_lock);
	if(wm_shared->cand_state == state){
		memcpy(wm_shared->ssid, wm_shared->cand_ssid, MAX_SSID_SIZE);
		wm_shared->ip = 0;
		wm_shared->netmask = 0;
		wm_shared->gw = 0;
		wm_shared->urc = (uint8_t)UPDATE_FAILED_ATTEMPT;
		wm_shared->reason = reason;
		wifi_manager_cand_clear_locked();
		failed = true;
	}
	taskEXIT_CRITICAL(&wm_lock);
	return failed;
}

size_t wifi_manager_status_json(char *out, size_t size){

	/* LOCAL PATCH (2.1.4 C8, C6): see wifi_manager.h. The lock is held for the copy only */
	struct {
		uint8_t ssid[MAX_SSID_SIZE];
		uint8_t cand_ssid[MAX_SSID_SIZE];
		uint32_t ip, netmask, gw;
		uint8_t urc, reason, cand_state;
	} s;

	if(out == NULL || size < WIFI_MANAGER_STATUS_JSON_SIZE || wm_shared == NULL){
		if(out != NULL && size > 0){
			out[0] = '\0';
		}
		return 0;
	}

	taskENTER_CRITICAL(&wm_lock);
	memcpy(s.ssid, wm_shared->ssid, MAX_SSID_SIZE);
	memcpy(s.cand_ssid, wm_shared->cand_ssid, MAX_SSID_SIZE);
	s.ip = wm_shared->ip;
	s.netmask = wm_shared->netmask;
	s.gw = wm_shared->gw;
	s.urc = wm_shared->urc;
	s.reason = wm_shared->reason;
	s.cand_state = wm_shared->cand_state;
	taskEXIT_CRITICAL(&wm_lock);

	if(s.urc == WIFI_MANAGER_URC_NONE && s.cand_state == WM_CAND_NONE){
		strcpy(out, "{}\n");
		return 3;
	}

	/* the reason code tells why this was updated without a connection: "0" for each address then */
	char ip[IP4ADDR_STRLEN_MAX] = "0"; /* note: IP4ADDR_STRLEN_MAX is defined in lwip */
	char gw[IP4ADDR_STRLEN_MAX] = "0";
	char netmask[IP4ADDR_STRLEN_MAX] = "0";
	if(s.urc == UPDATE_CONNECTION_OK){
		esp_ip4_addr_t a;
		a.addr = s.ip;
		esp_ip4addr_ntoa(&a, ip, IP4ADDR_STRLEN_MAX);
		a.addr = s.gw;
		esp_ip4addr_ntoa(&a, gw, IP4ADDR_STRLEN_MAX);
		a.addr = s.netmask;
		esp_ip4addr_ntoa(&a, netmask, IP4ADDR_STRLEN_MAX);
	}

	/* LOCAL PATCH (2.1.4 C2e): built with bounds; json_print_ssid() reads at most the 32-byte
	 * field, and a raw SSID (json.h) gets "raw":1 after it */
	static const char ssid_key[] = "{\"ssid\":";
	size_t len = sizeof(ssid_key) - 1;
	memcpy(out, ssid_key, len);
	bool raw = false;
	size_t n = json_print_ssid(s.ssid, MAX_SSID_SIZE, out + len, size - len, &raw);
	len += n;
	int k = snprintf(out + len, size - len, "%s,\"ip\":\"%s\",\"netmask\":\"%s\",\"gw\":\"%s\",\"urc\":%d,\"reason\":%u,\"pend\":",
			raw ? ",\"raw\":1" : "", ip, netmask, gw,
			(s.urc == WIFI_MANAGER_URC_NONE) ? -1 : (int)s.urc, (unsigned)s.reason);
	if(n == 0 || k < 0 || (size_t)k >= size - len){
		strcpy(out, "{}\n");	/* cannot happen: WIFI_MANAGER_STATUS_JSON_SIZE takes the longest */
		return 3;
	}
	len += (size_t)k;
	/* "pend": the SSID of a candidate posted, waiting or connecting, "" otherwise */
	n = json_print_ssid(s.cand_state != WM_CAND_NONE ? s.cand_ssid : NULL, MAX_SSID_SIZE, out + len, size - len, NULL);
	if(n == 0 || len + n + 3 > size){
		strcpy(out, "{}\n");	/* cannot happen, as above */
		return 3;
	}
	len += n;
	memcpy(out + len, "}\n", 3);
	return len + 2;
}

bool wifi_manager_connect_user_async(const uint8_t *ssid, size_t ssid_len, const uint8_t *password, size_t password_len, uint8_t channel){

	/* LOCAL PATCH (2.1.4 C8): see wifi_manager.h */
	if(wm_shared == NULL || ssid == NULL || ssid_len == 0 || ssid_len > MAX_SSID_SIZE ||
			password_len > MAX_PASSWORD_SIZE || (password_len != 0 && password == NULL)){
		return false;
	}

	taskENTER_CRITICAL(&wm_lock);
	wifi_manager_cand_clear_locked();	/* the latest Connect wins over one not started yet */
	memcpy(wm_shared->cand_ssid, ssid, ssid_len);
	if(password_len){
		memcpy(wm_shared->cand_pwd, password, password_len);
	}
	wm_shared->cand_chan = channel;
	wm_shared->cand_state = WM_CAND_POSTED;
	taskEXIT_CRITICAL(&wm_lock);

	if(wifi_manager_send_message_wait(WM_ORDER_CONNECT_STA, (void*)CONNECTION_REQUEST_USER, pdMS_TO_TICKS(WIFI_MANAGER_POST_WAIT_MS)) == pdPASS){
		return true;
	}

	/* not queued: taken back, unless an earlier USER order took it meanwhile (it runs then) */
	taskENTER_CRITICAL(&wm_lock);
	if(wm_shared->cand_state == WM_CAND_POSTED){
		wifi_manager_cand_clear_locked();
	}
	taskEXIT_CRITICAL(&wm_lock);
	return false;
}

bool wifi_manager_retry_async(){
	/* LOCAL PATCH (2.1.4 C8): the app's router retry, an APP_RETRY order */
	return wifi_manager_send_message_wait(WM_ORDER_CONNECT_STA, (void*)CONNECTION_REQUEST_APP_RETRY, pdMS_TO_TICKS(WIFI_MANAGER_POST_WAIT_MS)) == pdPASS;
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

	/* make sure the http server isn't trying to access the list while it gets refreshed
	 * LOCAL PATCH (2.1.4 C6): for up to WIFI_MANAGER_LIST_LOCK_MS, past the HTTP server's 4 s send
	 * timeout: at low heap GET /ap.json sends the list under this lock, and a send stalled for
	 * want of memory held it past the 1 s waited before, which dropped this scan's records */
	if(wifi_manager_lock_json_buffer( pdMS_TO_TICKS(WIFI_MANAGER_LIST_LOCK_MS) )){
		unsigned left_out = wifi_manager_generate_acess_points_json(aps, count);
		/* LOCAL PATCH (2.1.4 C10b): the list's age, for the page load's scan */
		TickType_t now = xTaskGetTickCount();
		ap_list_tick = (now != 0) ? now : 1;
		wifi_manager_unlock_json_buffer();
		if(left_out){
			ESP_LOGW(TAG, "network list: %u access points left out (list buffer full)", left_out);
		}
	}
	else{
		ESP_LOGE(TAG, "could not get access to json mutex in wifi_scan");
	}
}

bool wifi_manager_heap_has(uint32_t caps, size_t size){
	/* LOCAL PATCH (2.1.4 WP1): see wifi_manager.h */
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
		ap_list_tick = 0;	/* LOCAL PATCH (2.1.4 C10b): a new list is empty */
		wifi_manager_unlock_json_buffer();
	}
	if(accessp_json == NULL && !ap_list_logged){
		ap_list_logged = true;
		ESP_LOGW(TAG, "network list: no memory for its %u B - the page lists no network yet", (unsigned)ACCESSP_JSON_SIZE);
	}
}

/**
 * @brief WM_ORDER_START_AP: the SoftAP, its servers, its network list, and the callback.
 * LOCAL PATCH (2.1.4 C6): a function, for the message and for a START_AP the task owes itself
 * (start_ap_due). wifi_manager task only. Defined after the server start below.
 */
static void wifi_manager_order_start_ap();

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

static void wifi_manager_order_start_ap(){

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
		return;
	}

	/* start HTTP, and DNS
	 * LOCAL PATCH (2.1.4 C3): HTTP is no longer stopped first. It ran from boot with the
	 * STA's settings and was restarted here with the AP's; it now runs only while the
	 * AP is up, so it runs here only at a START_AP with the AP already up (the portal's
	 * forget), and keeps running, with its sessions, as the DNS does.
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
	if(cb_ptr_arr[WM_ORDER_START_AP]) (*cb_ptr_arr[WM_ORDER_START_AP])(NULL);
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

bool wifi_manager_ap_list_built(){
	/* LOCAL PATCH (2.1.4 C10b): see wifi_manager.h. An unlocked read: a stale one costs one order
	 * too many, or one a poll late */
	return ap_list_tick != 0;
}

/**
 * @brief LOCAL PATCH (2.1.4 C10b): a scan ordered for the page did not start or ended failed: the
 * next order may come WIFI_MANAGER_SCAN_RETRY_MS from now (see scan_order_tick). wifi_manager
 * task only.
 */
static void wifi_manager_scan_failed(){
	if(scan_order_tick != 0){
		TickType_t t = xTaskGetTickCount() - pdMS_TO_TICKS(WIFI_MANAGER_SCAN_GAP_MS - WIFI_MANAGER_SCAN_RETRY_MS);
		scan_order_tick = (t != 0) ? t : 1;
	}
}

bool wifi_manager_ap_stop_done(uint32_t *ms_since){

	/* LOCAL PATCH (2.1.4 WP2): see wifi_manager.h. The flag first: the tick is written before
	 * the flag is cleared */
	if(ap_stop_busy){
		return false;
	}
	if(ms_since){
		TickType_t done = ap_stop_done_tick;
		TickType_t ticks = xTaskGetTickCount() - done;
		*ms_since = (done == 0 || ticks > UINT32_MAX / portTICK_PERIOD_MS) ? UINT32_MAX :
				(uint32_t)ticks * portTICK_PERIOD_MS;
	}
	return true;
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


/* LOCAL PATCH (2.1.4 C8): wifi_manager_connect_async() and wifi_manager_get_ip_info_json() are
 * gone: wifi_manager_connect_user_async() (a candidate), wifi_manager_retry_async() and
 * wifi_manager_status_json() replace them. */


void wifi_manager_destroy(){

	vTaskDelete(task_wifi_manager);
	task_wifi_manager = NULL;

	/* heap buffers */
	free(accessp_json);
	accessp_json = NULL;
	free(wm_shared);
	wm_shared = NULL;
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

BaseType_t wifi_manager_send_message_wait(message_code_t code, void *param, TickType_t wait){
	queue_message msg;
	msg.code = code;
	msg.param = param;
	return xQueueSend( wifi_manager_queue, &msg, wait);
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

/* ---- LOCAL PATCH (2.1.4 C8): connect ownership (see wm_shared at the top) ---- */

/* WM_ORDER_CONNECT_STA's callback: the order's kind, with WIFI_MANAGER_CONNECT_NOT_STARTED when it
 * started no attempt (wifi_manager.h) */
static void wifi_manager_connect_cb(uint32_t info){
	if(cb_ptr_arr[WM_ORDER_CONNECT_STA]) (*cb_ptr_arr[WM_ORDER_CONNECT_STA])( (void*)(uintptr_t)info );
}

/* an esp_wifi_disconnect() of ours succeeded: its STA_DISCONNECTED is awaited (abort_tick), and that
 * end reports reason for a user's attempt (0: the driver's) */
static void wifi_manager_abort_mark(uint8_t reason){
	TickType_t now = xTaskGetTickCount();
	abort_tick = (now != 0) ? now : 1;
	abort_reason = reason;
}

/* the candidate is what the STA is connected with: the driver's config (not the network in use's,
 * which differs on a network not committed), the same SSID and password. In a frame of its own
 * (the config, about 0.15 KB, wiped) */
static __attribute__((noinline)) bool wifi_manager_cand_is_live(){
	wifi_config_t drv;
	memset(&drv, 0x00, sizeof(drv));
	if(esp_wifi_get_config(WIFI_IF_STA, &drv) != ESP_OK){
		return false;
	}
	taskENTER_CRITICAL(&wm_lock);
	bool same = memcmp(wm_shared->cand_ssid, drv.sta.ssid, MAX_SSID_SIZE) == 0 &&
			memcmp(wm_shared->cand_pwd, drv.sta.password, MAX_PASSWORD_SIZE) == 0;
	taskEXIT_CRITICAL(&wm_lock);
	wifi_manager_wipe(drv.sta.password, sizeof(drv.sta.password));
	return same;
}

/* status.json's network while on a network not committed: the driver's SSID (the network in use's
 * is another). In a frame of its own, as above */
static __attribute__((noinline)) void wifi_manager_status_driver_ssid(){
	wifi_config_t drv;
	memset(&drv, 0x00, sizeof(drv));
	if(esp_wifi_get_config(WIFI_IF_STA, &drv) == ESP_OK){
		taskENTER_CRITICAL(&wm_lock);
		memcpy(wm_shared->ssid, drv.sta.ssid, MAX_SSID_SIZE);
		taskEXIT_CRITICAL(&wm_lock);
	}
	wifi_manager_wipe(drv.sta.password, sizeof(drv.sta.password));
}

/**
 * @brief starts an attempt of kind: a USER one with the waiting candidate (it becomes
 * WM_CAND_ACTIVE), any other with the network in use. One that does not start is a failed
 * attempt of its kind: a user's is reported and dropped, an automatic one is a lost connection
 * (its status, and the retry timer or the AP). The callback follows either way. In a frame of
 * its own: the candidate's config (about 0.15 KB) is on it, and wiped.
 */
static __attribute__((noinline)) void wifi_manager_start_attempt(connection_request_made_by_code_t kind, uint8_t *retries){

	wifi_config_t config;
	const wifi_config_t *use = wifi_manager_config_sta;
	memset(&config, 0x00, sizeof(config));

	if(kind == CONNECTION_REQUEST_USER){
		bool taken = false;
		taskENTER_CRITICAL(&wm_lock);
		if(wm_shared->cand_state == WM_CAND_WAITING){
			memcpy(config.sta.ssid, wm_shared->cand_ssid, MAX_SSID_SIZE);
			memcpy(config.sta.password, wm_shared->cand_pwd, MAX_PASSWORD_SIZE);
			config.sta.channel = wm_shared->cand_chan;
			wm_shared->cand_state = WM_CAND_ACTIVE;
			taken = true;
		}
		taskEXIT_CRITICAL(&wm_lock);
		if(!taken){
			wifi_manager_connect_cb((uint32_t)kind | WIFI_MANAGER_CONNECT_NOT_STARTED);
			return;
		}
		use = &config;
	}

	/* LOCAL PATCH (2.1.4 C2c): esp_wifi_set_config() and esp_wifi_connect() were each under
	 * ESP_ERROR_CHECK, and both fail at runtime (ESP_ERR_WIFI_STATE when a scan cannot stop in
	 * time, ESP_ERR_WIFI_SSID for an empty SSID, others at low heap): logged, never a reboot */
	EventBits_t uxBits = xEventGroupGetBits(wifi_manager_event_group);
	esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, (wifi_config_t*)use);
	bool config_failed = (err != ESP_OK);
	if(!config_failed){
		/* if there is a wifi scan in progress abort it first
		   Calling esp_wifi_scan_stop will trigger a SCAN_DONE event which will reset this bit */
		if(uxBits & WIFI_MANAGER_SCAN_BIT){
			esp_wifi_scan_stop();
		}
		err = esp_wifi_connect();
	}
	wifi_manager_wipe(config.sta.password, sizeof(config.sta.password));

	if(err == ESP_OK){
		TickType_t now = xTaskGetTickCount();
		attempt_kind = (uint8_t)kind;
		attempt_tick = (now != 0) ? now : 1;
		on_uncommitted = false;		/* no link left: whatever it was */
		late_ip_leave = false;		/* the IP to come is this attempt's */
		wifi_manager_connect_cb((uint32_t)kind);
		return;
	}

	ESP_LOGW(TAG, "ORDER_CONNECT_STA: %s failed (%s) - attempt not started",
			config_failed ? "esp_wifi_set_config" : "esp_wifi_connect", esp_err_to_name(err));
	if(kind == CONNECTION_REQUEST_USER){
		/* reported and dropped; the network in use is as it was (the driver gets it again at the
		 * next automatic attempt) */
		wifi_manager_cand_fail(WM_CAND_ACTIVE, 0);
	}
	else{
		wifi_manager_status_set(UPDATE_LOST_CONNECTION, WM_CAND_NONE);
		wifi_manager_retry_or_start_ap(uxBits, retries);
	}
	wifi_manager_connect_cb((uint32_t)kind | WIFI_MANAGER_CONNECT_NOT_STARTED);
}

/**
 * @brief a waiting candidate goes on, if nothing it waits for remains: with the STA idle its
 * attempt starts; with the STA connected (and the SoftAP up to report it) the network in use is
 * left for it, and its attempt starts at that STA_DISCONNECTED; the network in use already: done.
 * An attempt in flight, or our disconnect's event, it waits for (the loop ends an attempt after
 * WIFI_MANAGER_USER_WAIT_MS).
 */
static void wifi_manager_user_next(uint8_t *retries){

	if(wifi_manager_cand_state() != WM_CAND_WAITING || abort_tick != 0 || attempt_kind != CONNECTION_REQUEST_NONE){
		return;
	}

	EventBits_t uxBits = xEventGroupGetBits(wifi_manager_event_group);
	if(uxBits & WIFI_MANAGER_WIFI_CONNECTED_BIT){
		/* "the network in use already" only when the STA is on it: not on a replaced candidate's
		 * network, which the newer candidate always leaves */
		/* LOCAL PATCH (2.1.4 C8): this line, the leave's below and the commit's
		 * (wifi_manager_commit_driver_config()) are W: main.c caps this tag at WARN, and the bench
		 * reads a Connect's outcome from them. One each per Connect at most, never periodic */
		if(!on_uncommitted && wifi_manager_cand_is_live()){
			ESP_LOGW(TAG, "user connect: the network in use already");
			wifi_manager_status_set(UPDATE_CONNECTION_OK, WM_CAND_WAITING);
			wifi_manager_connect_cb((uint32_t)CONNECTION_REQUEST_USER | WIFI_MANAGER_CONNECT_NOT_STARTED);
			return;
		}
		if(!(uxBits & WIFI_MANAGER_AP_STARTED_BIT)){
			/* no page can see the result: the network in use is not left for it */
			ESP_LOGW(TAG, "user connect dropped: the SoftAP is down and the STA connected");
			wifi_manager_cand_fail(WM_CAND_WAITING, 0);
			wifi_manager_connect_cb((uint32_t)CONNECTION_REQUEST_USER | WIFI_MANAGER_CONNECT_NOT_STARTED);
			return;
		}
		esp_err_t err = esp_wifi_disconnect();
		if(err != ESP_OK){
			ESP_LOGW(TAG, "user connect: esp_wifi_disconnect failed (%s) - the network in use stays", esp_err_to_name(err));
			wifi_manager_cand_fail(WM_CAND_WAITING, 0);
			wifi_manager_connect_cb((uint32_t)CONNECTION_REQUEST_USER | WIFI_MANAGER_CONNECT_NOT_STARTED);
			return;
		}
		ESP_LOGW(TAG, "user connect: leaving the network in use for the candidate");
		wifi_manager_abort_mark(0);
		return;
	}

	wifi_manager_start_attempt(CONNECTION_REQUEST_USER, retries);
}

/**
 * @brief WM_ORDER_CONNECT_STA. A USER order takes its candidate (posted: waiting) and goes on as
 * far as it can (wifi_manager_user_next()); one that finds none (a newer Connect's order took it,
 * or a forget dropped it) starts nothing. Any other kind starts an attempt with the network in use,
 * unless the STA is connected, an attempt or our disconnect is under way, or a user's candidate is
 * posted or waiting: it gives way then and starts nothing.
 */
static void wifi_manager_order_connect(connection_request_made_by_code_t kind, uint8_t *retries){

	ESP_LOGI(TAG, "MESSAGE: ORDER_CONNECT_STA (kind %d)", (int)kind);

	if(kind == CONNECTION_REQUEST_USER){
		bool taken = false;
		taskENTER_CRITICAL(&wm_lock);
		if(wm_shared->cand_state == WM_CAND_POSTED){
			wm_shared->cand_state = WM_CAND_WAITING;
			taken = true;
		}
		taskEXIT_CRITICAL(&wm_lock);
		if(!taken){
			wifi_manager_connect_cb((uint32_t)kind | WIFI_MANAGER_CONNECT_NOT_STARTED);
			return;
		}
		TickType_t now = xTaskGetTickCount();
		user_wait_tick = (now != 0) ? now : 1;
		wifi_manager_user_next(retries);
		return;
	}

	EventBits_t uxBits = xEventGroupGetBits(wifi_manager_event_group);
	uint8_t state = wifi_manager_cand_state();
	if((uxBits & WIFI_MANAGER_WIFI_CONNECTED_BIT) || attempt_kind != CONNECTION_REQUEST_NONE || abort_tick != 0 ||
			state == WM_CAND_POSTED || state == WM_CAND_WAITING){
		wifi_manager_connect_cb((uint32_t)kind | WIFI_MANAGER_CONNECT_NOT_STARTED);
		return;
	}
	wifi_manager_start_attempt(kind, retries);
}

/**
 * @brief a forget whose save failed: the saved SSID's key, then the password's, are erased.
 * nvs_erase_key() needs no free space, where a write does (NVS writes a new entry before it drops
 * the old one), and a missing "ssid" reads as "nothing saved" (wifi_manager_fetch_wifi_sta_config();
 * the 10 s reset's erase_wifi_credentials() falls back the same way). The SSID decides: the
 * password's erase is best effort. "settings" is kept.
 */
static esp_err_t wifi_manager_erase_saved_network(){

	nvs_handle handle;
	if(!nvs_sync_lock( portMAX_DELAY )){
		return ESP_ERR_TIMEOUT;
	}
	esp_err_t err = nvs_open(wifi_manager_nvs_namespace, NVS_READWRITE, &handle);
	if(err == ESP_OK){
		err = nvs_erase_key(handle, "ssid");
		if(err == ESP_ERR_NVS_NOT_FOUND){
			err = ESP_OK;
		}
		if(err == ESP_OK){
			(void)nvs_erase_key(handle, "password");
			err = nvs_commit(handle);
		}
		nvs_close(handle);
	}
	nvs_sync_unlock();
	return err;
}

/**
 * @brief the forget (the page's Disconnect, D9, or the 10 s reset): the network in use is zeroed
 * and saved (zero SSID and password blobs: "nothing saved"), any candidate dropped, status.json
 * reads UPDATE_USER_DISCONNECT, and a START_AP is owed (it opens the portal window).
 * LOCAL PATCH (2.1.4 C8): a save that fails (NVS full, say) is followed by the erase of the saved
 * network's keys, or a reboot would rejoin the network the user forgot; a forget that reaches
 * NVS neither way prints an E line, and its save is owed (save_owed). Any save owed from an
 * earlier IP is void once the forget is in NVS.
 */
static void wifi_manager_forget_now(){

	memset(wifi_manager_config_sta, 0x00, sizeof(wifi_config_t));
	taskENTER_CRITICAL(&wm_lock);
	wifi_manager_cand_clear_locked();
	taskEXIT_CRITICAL(&wm_lock);
	user_due = false;
	wifi_manager_status_set(UPDATE_USER_DISCONNECT, WM_CAND_NONE);
	esp_err_t err = wifi_manager_save_sta_config();
	save_owed = false;
	if(err != ESP_OK){
		esp_err_t erase_err = wifi_manager_erase_saved_network();
		if(erase_err == ESP_OK){
			ESP_LOGW(TAG, "forget: the zeroed network not saved (%s) - its keys erased instead", esp_err_to_name(err));
		}
		else{
			save_owed = true;
			ESP_LOGE(TAG, "forget: not saved (%s), not erased (%s) - a reboot may rejoin the network forgotten",
					esp_err_to_name(err), esp_err_to_name(erase_err));
		}
	}
	start_ap_due = true;
}

/**
 * @brief the attempt of kind (CONNECTION_REQUEST_NONE: the link in use, or none) has ended without
 * an IP, for reason: at its STA_DISCONNECTED, or when its event never came. A forget first; then
 * a user's candidate that failed is reported (no retry after it, by design: the network in use is
 * kept, and the app's router retry rejoins it); a candidate that waited for this end goes on
 * (user_due), and the end is not a lost connection then; otherwise it is one (its status, and
 * the retry timer or the AP, as before), its status kept when quiet (a link we left).
 */
static void wifi_manager_attempt_ended(uint8_t kind, uint8_t reason, EventBits_t uxBits, uint8_t *retries, bool quiet){

	if(uxBits & WIFI_MANAGER_REQUEST_DISCONNECT_BIT){
		/* user manually requested a disconnect so the lost connection is a normal event. Clear the flag and restart the AP */
		xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_DISCONNECT_BIT);
		wifi_manager_forget_now();
		return;
	}
	if(kind == CONNECTION_REQUEST_USER){
		wifi_manager_cand_fail(WM_CAND_ACTIVE, reason);
	}
	if(wifi_manager_cand_state() == WM_CAND_WAITING){
		user_due = true;
		return;
	}
	if(kind == CONNECTION_REQUEST_USER){
		return;
	}
	/* LOCAL PATCH (2.1.4 C8): not for the end of a link we left (quiet: an IP not kept), whose
	 * result (a forget, a candidate's failure) was decided before it */
	if(!quiet){
		wifi_manager_status_set(UPDATE_LOST_CONNECTION, WM_CAND_NONE);
	}
	wifi_manager_retry_or_start_ap(uxBits, retries);
}

/**
 * @brief our esp_wifi_disconnect() got no STA_DISCONNECTED in WIFI_MANAGER_ABORT_WAIT_MS. With the
 * STA still connected nothing was left: a forget is dropped (nothing erased, as for a disconnect
 * that fails) and a waiting candidate fails. Otherwise the attempt counts as ended, and the app is
 * told with the STA_DISCONNECTED callback it would have had (WIFI_REASON_ASSOC_LEAVE).
 */
static void wifi_manager_abort_expired(uint8_t *retries){

	uint8_t why = abort_reason;
	bool quiet = leaving_link;
	abort_tick = 0;
	abort_reason = 0;
	leaving_link = false;

	EventBits_t uxBits = xEventGroupGetBits(wifi_manager_event_group);
	if(uxBits & WIFI_MANAGER_WIFI_CONNECTED_BIT){
		ESP_LOGW(TAG, "no STA_DISCONNECTED %d ms after esp_wifi_disconnect() - still connected, nothing changed", WIFI_MANAGER_ABORT_WAIT_MS);
		xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_DISCONNECT_BIT);
		/* the status is the link's (an IP being left kept the result before it: WM_EVENT_STA_GOT_IP),
		 * then a waiting candidate's failure */
		wifi_manager_status_set(UPDATE_CONNECTION_OK, WM_CAND_NONE);
		if(on_uncommitted){
			wifi_manager_status_driver_ssid();
		}
		wifi_manager_cand_fail(WM_CAND_WAITING, 0);
		return;
	}

	ESP_LOGW(TAG, "no STA_DISCONNECTED %d ms after esp_wifi_disconnect() - the attempt counts as ended", WIFI_MANAGER_ABORT_WAIT_MS);
	on_uncommitted = false;		/* not connected: no link left */
	/* should the attempt get its IP after all, that IP is left (WM_EVENT_STA_GOT_IP): what follows
	 * (a forget's erase, a candidate reported failed) has written the attempt off */
	late_ip_leave = true;
	/* should it come late after all, it is not charged to the attempt that may start next */
	TickType_t now = xTaskGetTickCount();
	stale_leave_until = now + pdMS_TO_TICKS(WIFI_MANAGER_STALE_LEAVE_MS);
	if(stale_leave_until == 0){
		stale_leave_until = 1;
	}
	uint8_t kind = attempt_kind;
	attempt_kind = CONNECTION_REQUEST_NONE;
	wifi_manager_attempt_ended(kind, why ? why : (uint8_t)WIFI_REASON_ASSOC_LEAVE, uxBits, retries, quiet);
	if(cb_ptr_arr[WM_EVENT_STA_DISCONNECTED]) (*cb_ptr_arr[WM_EVENT_STA_DISCONNECTED])( (void*)(uintptr_t)WIFI_REASON_ASSOC_LEAVE );
}

/**
 * @brief the deadlines of the connect ownership, at the top of the task's loop: our disconnect's
 * event, a waiting candidate's WIFI_MANAGER_USER_WAIT_MS, a user's attempt's
 * WIFI_MANAGER_USER_ATTEMPT_MS. Acts on those that are due; returns the ticks to the next one,
 * portMAX_DELAY if none (0: go round again at once).
 */
static TickType_t wifi_manager_connect_deadlines(uint8_t *retries){

	TickType_t now = xTaskGetTickCount();
	TickType_t wait = portMAX_DELAY;

	if(abort_tick != 0){
		TickType_t since = now - abort_tick;
		if(since >= pdMS_TO_TICKS(WIFI_MANAGER_ABORT_WAIT_MS)){
			/* the event may be queued already, behind this task's own work (an NVS save, a
			 * server's stop): the queue first, the expiry only with it empty */
			if(uxQueueMessagesWaiting(wifi_manager_queue) != 0){
				return 0;
			}
			wifi_manager_abort_expired(retries);
			return 0;
		}
		return pdMS_TO_TICKS(WIFI_MANAGER_ABORT_WAIT_MS) - since;
	}

	if(attempt_kind != CONNECTION_REQUEST_NONE && wifi_manager_cand_state() == WM_CAND_WAITING){
		TickType_t since = now - user_wait_tick;
		if(since >= pdMS_TO_TICKS(WIFI_MANAGER_USER_WAIT_MS)){
			esp_err_t err = esp_wifi_disconnect();
			if(err == ESP_OK){
				ESP_LOGW(TAG, "user connect: the attempt in flight is ended after %d ms", WIFI_MANAGER_USER_WAIT_MS);
				wifi_manager_abort_mark(0);
				return 0;
			}
			ESP_LOGW(TAG, "user connect: esp_wifi_disconnect failed (%s) - waiting again", esp_err_to_name(err));
			user_wait_tick = (now != 0) ? now : 1;
			since = 0;
		}
		wait = pdMS_TO_TICKS(WIFI_MANAGER_USER_WAIT_MS) - since;
	}

	if(attempt_kind == CONNECTION_REQUEST_USER){
		TickType_t since = now - attempt_tick;
		if(since >= pdMS_TO_TICKS(WIFI_MANAGER_USER_ATTEMPT_MS)){
			esp_err_t err = esp_wifi_disconnect();
			if(err == ESP_OK){
				ESP_LOGW(TAG, "user connect: no IP after %d ms - ended, the network in use stays", WIFI_MANAGER_USER_ATTEMPT_MS);
				wifi_manager_abort_mark(WIFI_MANAGER_REASON_NO_IP);
				return 0;
			}
			ESP_LOGW(TAG, "user connect: esp_wifi_disconnect failed (%s) - tried again later", esp_err_to_name(err));
			attempt_tick = (now != 0) ? now : 1;
			since = 0;
		}
		TickType_t left = pdMS_TO_TICKS(WIFI_MANAGER_USER_ATTEMPT_MS) - since;
		if(left < wait){
			wait = left;
		}
	}
	return wait;
}

/**
 * @brief at an IP: the driver's config is committed as the network in use if it differs from it
 * and adopt is set (a user's candidate, WM_CAND_ACTIVE, got its IP: plan C8, I13), and saved.
 * Returns false when the driver's config differs and is not adopted: the STA is on a network that
 * is not the one in use (nothing is committed or saved then). In a frame of its own (the config,
 * about 0.15 KB, wiped).
 */
static __attribute__((noinline)) bool wifi_manager_commit_driver_config(bool adopt){

	wifi_config_t drv;
	memset(&drv, 0x00, sizeof(drv));
	bool in_use = true;
	bool committed = false;
	esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &drv);
	if(err != ESP_OK){
		ESP_LOGW(TAG, "esp_wifi_get_config failed (%s) - the network in use is kept", esp_err_to_name(err));
	}
	else if(memcmp(drv.sta.ssid, wifi_manager_config_sta->sta.ssid, MAX_SSID_SIZE) != 0 ||
			memcmp(drv.sta.password, wifi_manager_config_sta->sta.password, MAX_PASSWORD_SIZE) != 0){
		if(adopt){
			memcpy(wifi_manager_config_sta->sta.ssid, drv.sta.ssid, MAX_SSID_SIZE);
			memcpy(wifi_manager_config_sta->sta.password, drv.sta.password, MAX_PASSWORD_SIZE);
			wifi_manager_config_sta->sta.channel = 0;	/* the old network's hint is not this one's */
			committed = true;
			save_owed = true;
		}
		else{
			in_use = false;
		}
	}
	wifi_manager_wipe(drv.sta.password, sizeof(drv.sta.password));

	/* the save, now or owed from an earlier IP whose save failed (NVS full, say): the network in use
	 * works, and only a reboot before a save succeeds would lose it */
	if(in_use && save_owed){
		esp_err_t save_err = wifi_manager_save_sta_config();
		save_owed = (save_err != ESP_OK);
		if(save_owed){
			/* the SSID and password are two writes: one can fail, or power can go, between them */
			ESP_LOGE(TAG, "the network in use is not saved (%s) - tried again at the next IP; a reboot before then loads the old network, or the new SSID with the old password",
					esp_err_to_name(save_err));
		}
	}
	if(committed && !save_owed){
		ESP_LOGW(TAG, "user connect: the candidate got its IP - it is the network in use now, saved");
	}
	return in_use;
}

/**
 * @brief leaves the link the STA is on: an IP not to keep (WM_EVENT_STA_GOT_IP). Our disconnect's
 * STA_DISCONNECTED is awaited like any other (abort_tick), and ends the link as a lost one,
 * quietly (leaving_link): status.json keeps the result decided before the IP, and the network in
 * use is tried again as after any link loss (the router retry while the AP is up; after a forget
 * there is none). Returns whether the disconnect went out.
 */
static bool wifi_manager_leave_link(){
	esp_err_t err = esp_wifi_disconnect();
	if(err == ESP_OK){
		wifi_manager_abort_mark(0);
		leaving_link = true;
		return true;
	}
	ESP_LOGW(TAG, "esp_wifi_disconnect failed (%s) - the network not saved stays until its link ends", esp_err_to_name(err));
	return false;
}

/**
 * @brief LOCAL PATCH (2.1.4 C13): at an IP, the router's primary channel into the network in use's
 * config (RAM only: NVS keeps the SSID and password blobs), so a later attempt scans it first. In
 * a frame of its own (the AP record, about 0.1 KB).
 */
static __attribute__((noinline)) void wifi_manager_channel_hint(){
	wifi_ap_record_t ap;
	if(esp_wifi_sta_get_ap_info(&ap) == ESP_OK && ap.primary >= 1 && ap.primary <= 14){
		wifi_manager_config_sta->sta.channel = ap.primary;
	}
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
			.csa_count = WIFI_MANAGER_AP_CSA_COUNT,			/* LOCAL PATCH (2.1.4 C13) */
			.dtim_period = WIFI_MANAGER_AP_DTIM_PERIOD,		/* LOCAL PATCH (2.1.4 C13) */
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

	/* LOCAL PATCH (2.1.4 C13): the scan dwell, once the station has started (logged, not checked:
	 * the driver's defaults stay if it refuses) */
	wifi_scan_default_params_t scan_params;
	memset(&scan_params, 0x00, sizeof(scan_params));
	scan_params.scan_time.active.min = 0;
	scan_params.scan_time.active.max = WIFI_MANAGER_SCAN_ACTIVE_MAX_MS;
	scan_params.scan_time.passive = 0;		/* 0: the default (360 ms) */
	scan_params.home_chan_dwell_time = WIFI_MANAGER_SCAN_HOME_DWELL_MS;
	esp_err_t scan_params_err = esp_wifi_set_scan_parameters(&scan_params);
	if(scan_params_err != ESP_OK){
		ESP_LOGW(TAG, "esp_wifi_set_scan_parameters failed (%s) - the driver's scan times stay", esp_err_to_name(scan_params_err));
	}

	/* LOCAL PATCH (2.1.4 C3, plan D3): no HTTP server here. It runs only while the AP is up, from
	 * START_AP to STOP_AP: with the STA alone it answered the home LAN, where DELETE and POST
	 * /connect.json need no password, and its task and sockets held about 5 KB of heap for
	 * nothing (no client of it but the setup page, user decision D10) */

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
		/* LOCAL PATCH (2.1.4 C6): a START_AP owed by the last message (start_ap_due), first */
		if(start_ap_due){
			start_ap_due = false;
			wifi_manager_order_start_ap();
		}

		/* LOCAL PATCH (2.1.4 C8): a candidate the last message let go on (user_due), after that
		 * message's callback; then the connect ownership's deadlines, which bound the wait */
		if(user_due){
			user_due = false;
			wifi_manager_user_next(&retries);
		}
		TickType_t wait = wifi_manager_connect_deadlines(&retries);

		/* LOCAL PATCH (2.1.4 WP1): with the AP up and one of its servers down, they are started
		 * again every WIFI_MANAGER_AP_SERVERS_RETRY_MS between messages, and the wait for the next
		 * message ends at the next try. Otherwise the task waits for a message as before. */
		if(ap_servers_down){
			TickType_t since = xTaskGetTickCount() - ap_servers_tick;
			if(since >= pdMS_TO_TICKS(WIFI_MANAGER_AP_SERVERS_RETRY_MS)){
				wifi_manager_start_ap_servers(true);
				since = 0;
			}
			if(ap_servers_down && pdMS_TO_TICKS(WIFI_MANAGER_AP_SERVERS_RETRY_MS) - since < wait){
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
					/* LOCAL PATCH (2.1.4 C10b): after a failed scan the page may order another,
					 * WIFI_MANAGER_SCAN_RETRY_MS on; a scan that succeeded with no list (low heap)
					 * keeps the 20 s gap */
					if(scan_status != 0){
						wifi_manager_scan_failed();
					}
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
						wifi_manager_scan_failed();	/* LOCAL PATCH (2.1.4 C10b): the page may order another, 10 s on */
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
					/* LOCAL PATCH (2.1.4 C6): this task never waits on its own queue (it is its only
					 * reader). Should the post not fit, the retry timer connects 5 s later */
					if(wifi_manager_send_message_wait(WM_ORDER_CONNECT_STA, (void*)CONNECTION_REQUEST_RESTORE_CONNECTION, (TickType_t)0) != pdPASS){
						ESP_LOGW(TAG, "restore: queue full - the retry timer connects");
						wifi_manager_start_retry_timer();
					}
				}
				else{
					/* no wifi saved: start soft AP! This is what should happen during a first run */
					ESP_LOGI(TAG, "No saved wifi found on startup. Starting access point.");
					start_ap_due = true;	/* LOCAL PATCH (2.1.4 C6): owed, not posted */
				}

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case WM_ORDER_CONNECT_STA:
				/* LOCAL PATCH (2.1.4 C8): one owner for every attempt (see wm_shared at the top) */
				wifi_manager_order_connect((connection_request_made_by_code_t)(uintptr_t)msg.param, &retries);
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
				 *  LOCAL PATCH (2.1.4 C8): the request bits above are gone. attempt_kind (the attempt in
				 *  flight) and the forget's WIFI_MANAGER_REQUEST_DISCONNECT_BIT decide, in
				 *  wifi_manager_attempt_ended(); a user's candidate that fails reports its reason in
				 *  status.json ("reason"), which the page turns into a text.
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

				/* LOCAL PATCH (2.1.4 C8): the late event of an attempt the loop already ended (its
				 * esp_wifi_disconnect() got no event in time, wifi_manager_abort_expired()): ignored,
				 * callback included (the app had its own then), with the STA not connected. With our
				 * disconnect's reason (ASSOC_LEAVE), or with any reason while no attempt has started
				 * since: nothing else can end then (the attempt's own failure can cross our
				 * disconnect, with its own reason), and the result already decided stands */
				if(stale_leave_until != 0){
					bool stale = (disconnect_reason == WIFI_REASON_ASSOC_LEAVE || attempt_kind == CONNECTION_REQUEST_NONE) &&
							abort_tick == 0 &&
							(int32_t)(stale_leave_until - xTaskGetTickCount()) > 0 &&
							!(xEventGroupGetBits(wifi_manager_event_group) & WIFI_MANAGER_WIFI_CONNECTED_BIT);
					stale_leave_until = 0;
					if(stale){
						ESP_LOGW(TAG, "a late STA_DISCONNECTED of an attempt already ended - ignored");
						break;
					}
				}

				on_uncommitted = false;	/* LOCAL PATCH (2.1.4 C8) */
				late_ip_leave = false;	/* LOCAL PATCH (2.1.4 C8): that attempt has ended, with no IP */

				/* reset saved sta IP */
				wifi_manager_safe_update_sta_ip_string((uint32_t)0);

				/* if there was a timer on to stop the AP, well now it's time to cancel that since connection was lost!
				 * LOCAL PATCH (2.1.4 C12): stopped whether it reads active or not: a re-arm sent from
				 * another task (wifi_manager_ap_stop_in()) may not have reached the timer task yet,
				 * and a stop sent now reaches it after that re-arm */
				xTimerStop( wifi_manager_shutdown_ap_timer, (TickType_t)0 );

				/* LOCAL PATCH (2.1.4 C8): the attempt, or the link, has ended (see
				 * wifi_manager_attempt_ended()): a forget first, then a user's candidate, then a
				 * lost connection. An end our own disconnect asked for reports its reason for a
				 * user's attempt (no IP in time) in place of the driver's. */
				{
					uxBits = xEventGroupGetBits(wifi_manager_event_group);
					uint8_t kind = attempt_kind;
					uint8_t why = (abort_tick != 0 && abort_reason != 0) ? abort_reason : disconnect_reason;
					bool quiet = leaving_link;
					attempt_kind = CONNECTION_REQUEST_NONE;
					abort_tick = 0;
					abort_reason = 0;
					leaving_link = false;
					wifi_manager_attempt_ended(kind, why, uxBits, &retries, quiet);
				}

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])( msg.param );

				break;

			case WM_ORDER_START_AP:
				/* LOCAL PATCH (2.1.4 C6): a function, which the task also runs when it owes itself a
				 * START_AP (start_ap_due) */
				wifi_manager_order_start_ap();
				break;

			case WM_ORDER_STOP_AP:
				ESP_LOGI(TAG, "MESSAGE: ORDER_STOP_AP");


				uxBits = xEventGroupGetBits(wifi_manager_event_group);

				/* before stopping the AP, we check that we are still connected. There's a chance that once the timer
				 * kicks in, for whatever reason the esp32 is already disconnected.
				 */
				if(uxBits & WIFI_MANAGER_WIFI_CONNECTED_BIT){

					/* set to STA only
					 * LOCAL PATCH (2.1.4 WP2): checked. A switch that fails leaves the AP up, and
					 * the stop went on to take its DNS, its HTTP server and its network list away
					 * (plan I11 broken, nothing repaired it), and the app's cloud admission, which
					 * admits no TLS while the AP is up, waited for good. Now the AP keeps all of
					 * them, and the stop is tried again WIFI_MANAGER_STOP_AP_RETRY_MS later through
					 * the shutdown timer, which a lost link stops (the AP then stays up, as after
					 * any lost link). The callback is told, with parameter 1 (the AP is not
					 * stopped): the app ends what waited only for this moment (its portal window,
					 * which pauses BLE leak scanning), as it did when the result was ignored, so a
					 * switch that keeps failing cannot hold it with the STA connected.
					 * LOCAL PATCH (2.1.4 WP2): the stop is under way from just before the switch
					 * (ap_stop_busy), so a mode read as STA from here on finds it so until the
					 * servers and the list below are freed (wifi_manager_ap_stop_done()) */
					ap_stop_busy = true;
					esp_err_t stop_err = esp_wifi_set_mode(WIFI_MODE_STA);
					if(stop_err != ESP_OK){
						ap_stop_busy = false;
						ESP_LOGE(TAG, "ORDER_STOP_AP: esp_wifi_set_mode failed (%s) - AP kept up, stopped again in %d s",
								esp_err_to_name(stop_err), WIFI_MANAGER_STOP_AP_RETRY_MS / 1000);
						xTimerChangePeriod( wifi_manager_shutdown_ap_timer, pdMS_TO_TICKS(WIFI_MANAGER_STOP_AP_RETRY_MS), (TickType_t)0 );
						if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])((void*)(uintptr_t)1);
						break;
					}

					/* stop DNS
					 * LOCAL PATCH (2.1.4 C4): waits up to 1 s for its task to close its socket */
					dns_server_stop();
					ap_servers_down = false;	/* LOCAL PATCH (2.1.4 WP1): no retry with the AP down */

					/* stop HTTP daemon
					 * LOCAL PATCH (2.1.4 C3): not started again (it was, with the STA's settings):
					 * it runs only while the AP is up, and the next START_AP starts it */
					http_app_stop();

					/* LOCAL PATCH (2.1.4 C2b): the network list goes with the AP (+1,489 B of heap).
					 * LOCAL PATCH (2.1.4 C10b): and its ticks (see ap_list_tick), with the HTTP
					 * server, their other user, stopped above */
					ap_list_wanted = false;
					if(wifi_manager_lock_json_buffer( portMAX_DELAY )){
						free(accessp_json);
						accessp_json = NULL;
						ap_list_tick = 0;
						wifi_manager_unlock_json_buffer();
					}
					scan_order_tick = 0;

					/* LOCAL PATCH (2.1.4 WP2): the stop has finished (wifi_manager_ap_stop_done()) */
					TickType_t stop_done = xTaskGetTickCount();
					ap_stop_done_tick = (stop_done != 0) ? stop_done : 1;
					ap_stop_busy = false;

					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);
				}

				break;

			case WM_EVENT_STA_GOT_IP:
				ESP_LOGI(TAG, "WM_EVENT_STA_GOT_IP");
				/* LOCAL PATCH (2.1.4 C2a): the parameter is the IPv4 address (network byte order), not a pointer */
				uint32_t got_ip = (uint32_t)(uintptr_t)msg.param;
				uxBits = xEventGroupGetBits(wifi_manager_event_group);
				bool leave = false;		/* LOCAL PATCH (2.1.4 C8): an IP not to keep, left after the callback */

				/* LOCAL PATCH (2.1.4 C8): the attempt is over. An esp_wifi_disconnect() of ours that
				 * crossed this IP still has its STA_DISCONNECTED to come (abort_tick stays). */
				{
					uint8_t kind = attempt_kind;
					attempt_kind = CONNECTION_REQUEST_NONE;
					stale_leave_until = 0;

					/* save IP as a string for the HTTP server host */
					wifi_manager_safe_update_sta_ip_string(got_ip);

					/* LOCAL PATCH (2.1.4 C8): the config that got this IP is the network in use from
					 * now on, saved if it is new (a user's candidate): before, every IP but the boot
					 * restore's saved the RAM config, which a Connect had already overwritten. Not a
					 * candidate a newer Connect replaced while it connected: the user left it, so it
					 * is not saved, and the newer one leaves it next (user_due, below); the network in
					 * use stays the one before. A save that failed is tried again at the next IP. */
					uint8_t state = wifi_manager_cand_state();
					if(late_ip_leave){
						/* an attempt counted as ended when its disconnect's event did not come
						 * (wifi_manager_abort_expired()): a forget erased its network since, or the
						 * page reported its candidate failed. Not saved, and left */
						ESP_LOGW(TAG, "an IP of an attempt already counted as ended - not saved, left");
						on_uncommitted = true;
						leave = true;
					}
					else if(kind == CONNECTION_REQUEST_USER && (state == WM_CAND_POSTED || state == WM_CAND_WAITING)){
						ESP_LOGW(TAG, "user connect: a candidate a newer Connect replaced got its IP - not saved, left next");
						on_uncommitted = true;
					}
					else if(uxBits & WIFI_MANAGER_REQUEST_DISCONNECT_BIT){
						/* a forget is under way: what this IP's attempt used is not saved */
						ESP_LOGW(TAG, "an IP while a forget is under way - nothing saved");
						on_uncommitted = true;
					}
					else if(on_uncommitted){
						/* a new IP (a DHCP renewal) on a network that was not committed: still not */
					}
					else if(!wifi_manager_commit_driver_config(kind == CONNECTION_REQUEST_USER && state == WM_CAND_ACTIVE)){
						/* not the network in use, and not a user's candidate: an IP no attempt of ours
						 * should get (each automatic one runs with the network in use). Not saved, and
						 * the link is kept */
						ESP_LOGW(TAG, "an IP on a network other than the one in use, not a Connect's - not saved");
						on_uncommitted = true;
					}
					else{
						/* LOCAL PATCH (2.1.4 C13): the router's channel, as the next attempt's hint (RAM only) */
						wifi_manager_channel_hint();
					}
					late_ip_leave = false;
				}

				/* reset number of retries */
				retries = 0;

				/* LOCAL PATCH (2.1.4 C8): an IP not to keep is left now, unless a candidate waits
				 * (it leaves this link for its own, below) or a disconnect of ours is under way */
				bool left = leave && abort_tick == 0 && wifi_manager_cand_state() != WM_CAND_WAITING &&
						wifi_manager_leave_link();

				/* refresh the status with the new IP (LOCAL PATCH 2.1.4 C8: and the candidate whose
				 * attempt got it ends, in the same lock; on a network not committed, its own SSID).
				 * Not for a link being left: the result decided before it (a forget, a failure)
				 * stands, and the page never reads this IP's network as connected */
				if(!left){
					wifi_manager_status_set(UPDATE_CONNECTION_OK, WM_CAND_ACTIVE);
					if(on_uncommitted){
						wifi_manager_status_driver_ssid();
					}
				}

				/* LOCAL PATCH (2.1.4 C4): the DNS hijack is no longer brought down here. It stays up with
				 * the AP until STOP_AP, so a phone that joins or re-joins the AP in its tail (the setup
				 * AP's last 60 s, or the AP left up after the STA lost the link again) is still sent to
				 * the portal (plan I11) */

				/* start the timer that will eventually shutdown the access point
				 * We check first that it's actually running because in case of a boot and restore connection
				 * the AP is not even started to begin with.
				 */
				if((uxBits & WIFI_MANAGER_AP_STARTED_BIT) && !left){	/* LOCAL PATCH (2.1.4 C8): not for a link being left */
					TickType_t t = pdMS_TO_TICKS( WIFI_MANAGER_SHUTDOWN_AP_TIMER );

					/* if for whatever reason user configured the shutdown timer to be less than 1 tick, the AP is stopped straight away */
					if(t > 0){
						/* LOCAL PATCH (2.1.4 C12): with the default period again, which a
						 * wifi_manager_ap_stop_in() at an earlier IP may have changed (a timer keeps
						 * its last period); xTimerChangePeriod() starts it too, as xTimerStart() did.
						 * The callback below may set this IP's own stop (wifi_manager_ap_stop_in()) */
						xTimerChangePeriod( wifi_manager_shutdown_ap_timer, t, (TickType_t)0 );
					}
					else if(wifi_manager_send_message_wait(WM_ORDER_STOP_AP, (void*)NULL, (TickType_t)0) != pdPASS){
						/* LOCAL PATCH (2.1.4 C6): never waits on its own queue; the IP + 75 s
						 * backstop of the app stops the AP then */
						ESP_LOGW(TAG, "AP stop: queue full - not sent");
					}

				}

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])( msg.param );

				/* LOCAL PATCH (2.1.4 C8): a candidate that waited for this attempt goes on now (it
				 * leaves this network for its own), after the callback. The app is told of every IP,
				 * one being left included: its STA_DISCONNECTED callback follows, and should our
				 * disconnect's event never come, the app's view of a link that stays is right */
				if(wifi_manager_cand_state() == WM_CAND_WAITING){
					user_due = true;
				}

				break;

			case WM_ORDER_DISCONNECT_STA:
				ESP_LOGI(TAG, "MESSAGE: ORDER_DISCONNECT_STA");

				/* LOCAL PATCH (2.1.4 C8): the forget (the page's Disconnect, D9 kept, and the 10 s
				 * reset) wins over a user's candidate, which is dropped. An idle STA, with no attempt
				 * and no disconnect of ours under way, posts no disconnect event: its saved network
				 * is erased now (the app no longer posts that event for it). Otherwise the request bit
				 * is set and the STA_DISCONNECTED that follows erases, before anything else it does
				 * (wifi_manager_attempt_ended()); our disconnect's event is awaited, and the loop
				 * erases WIFI_MANAGER_ABORT_WAIT_MS later should none come. */
				taskENTER_CRITICAL(&wm_lock);
				wifi_manager_cand_clear_locked();
				taskEXIT_CRITICAL(&wm_lock);
				user_due = false;
				uxBits = xEventGroupGetBits(wifi_manager_event_group);
				if(!(uxBits & WIFI_MANAGER_WIFI_CONNECTED_BIT) && attempt_kind == CONNECTION_REQUEST_NONE && abort_tick == 0){
					ESP_LOGW(TAG, "ORDER_DISCONNECT_STA: the STA is not connected - the saved network is erased now");
					wifi_manager_forget_now();
				}
				else{
					/* precise this is coming from a user request */
					xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_DISCONNECT_BIT);

					/* order wifi discconect */
					/* LOCAL PATCH (2.1.4 C2d): logged, not ESP_ERROR_CHECK.
					 * LOCAL PATCH (2.1.4 WP1): not with the STA connected (it has its IP), where no
					 * disconnect event follows a failed call: the bit would stay armed, and the next link
					 * loss, maybe days later, would erase the saved network then. The forget is dropped
					 * instead (nothing erased, the STA stays connected), and the callback is not called. */
					esp_err_t disconnect_err = esp_wifi_disconnect();
					if(disconnect_err != ESP_OK){
						if(xEventGroupGetBits(wifi_manager_event_group) & WIFI_MANAGER_WIFI_CONNECTED_BIT){
							xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_DISCONNECT_BIT);
							ESP_LOGW(TAG, "ORDER_DISCONNECT_STA: esp_wifi_disconnect failed (%s) - still connected, nothing erased", esp_err_to_name(disconnect_err));
							break;
						}
						ESP_LOGW(TAG, "ORDER_DISCONNECT_STA: esp_wifi_disconnect failed (%s)", esp_err_to_name(disconnect_err));
					}
					if(abort_tick == 0){
						wifi_manager_abort_mark(0);
					}
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


