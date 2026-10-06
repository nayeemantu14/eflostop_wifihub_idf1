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

@file wifi_manager.h
@author Tony Pottier
@brief Defines all functions necessary for esp32 to connect to a wifi/scan wifis

Contains the freeRTOS task and all necessary support

@see https://idyl.io
@see https://github.com/tonyp7/esp32-wifi-manager
*/

#ifndef WIFI_MANAGER_H_INCLUDED
#define WIFI_MANAGER_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Defines the maximum size of a SSID name. 32 is IEEE standard.
 * @warning limit is also hard coded in wifi_config_t. Never extend this value.
 */
#define MAX_SSID_SIZE						32

/**
 * @brief Defines the maximum size of a WPA2 passkey. 64 is IEEE standard.
 * @warning limit is also hard coded in wifi_config_t. Never extend this value.
 */
#define MAX_PASSWORD_SIZE					64


/**
 * @brief Defines the maximum number of access points that can be scanned.
 *
 * To save memory and avoid nasty out of memory errors,
 * we can limit the number of APs detected in a wifi scan.
 *
 * LOCAL PATCH (2.1.4 C2b): the list keeps the MAX_AP_NUM strongest networks of a scan.
 */
#define MAX_AP_NUM 							15


/**
 * @brief Defines the maximum number of failed retries allowed before the WiFi manager starts its own access point.
 * Setting it to 2 for instance means there will be 3 attempts in total (original request + 2 retries)
 */
#define WIFI_MANAGER_MAX_RETRY_START_AP		CONFIG_WIFI_MANAGER_MAX_RETRY_START_AP

/**
 * @brief Time (in ms) between each retry attempt
 * Defines the time to wait before an attempt to re-connect to a saved wifi is made after connection is lost or another unsuccesful attempt is made.
 */
#define WIFI_MANAGER_RETRY_TIMER			CONFIG_WIFI_MANAGER_RETRY_TIMER


/**
 * @brief Time (in ms) to wait before shutting down the AP
 * Defines the time (in ms) to wait after a succesful connection before shutting down the access point.
 */
#define WIFI_MANAGER_SHUTDOWN_AP_TIMER		CONFIG_WIFI_MANAGER_SHUTDOWN_AP_TIMER


/** @brief Defines the task priority of the wifi_manager.
 *
 * Tasks spawn by the manager will have a priority of WIFI_MANAGER_TASK_PRIORITY-1.
 * For this particular reason, minimum task priority is 1. It it highly not recommended to set
 * it to 1 though as the sub-tasks will now have a priority of 0 which is the priority
 * of freeRTOS' idle task.
 */
#define WIFI_MANAGER_TASK_PRIORITY			CONFIG_WIFI_MANAGER_TASK_PRIORITY

/** @brief Defines the auth mode as an access point
 *  Value must be of type wifi_auth_mode_t
 *  @see esp_wifi_types.h
 *  @warning if set to WIFI_AUTH_OPEN, passowrd me be empty. See DEFAULT_AP_PASSWORD.
 */
#define AP_AUTHMODE 						WIFI_AUTH_WPA2_PSK

/** @brief Defines visibility of the access point. 0: visible AP. 1: hidden */
#define DEFAULT_AP_SSID_HIDDEN 				0

/** @brief Defines access point's name. Default value: esp32. Run 'make menuconfig' to setup your own value or replace here by a string */
#define DEFAULT_AP_SSID 					CONFIG_DEFAULT_AP_SSID

/** @brief Defines access point's password.
 *	@warning In the case of an open access point, the password must be a null string "" or "\0" if you want to be verbose but waste one byte.
 *	In addition, the AP_AUTHMODE must be WIFI_AUTH_OPEN
 */
#define DEFAULT_AP_PASSWORD 				CONFIG_DEFAULT_AP_PASSWORD

/** @brief Defines the hostname broadcasted by mDNS */
#define DEFAULT_HOSTNAME					"AVG_Hub"

/** @brief Defines access point's bandwidth.
 *  Value: WIFI_BW_HT20 for 20 MHz  or  WIFI_BW_HT40 for 40 MHz
 *  20 MHz minimize channel interference but is not suitable for
 *  applications with high data speeds
 */
#define DEFAULT_AP_BANDWIDTH 					WIFI_BW_HT20

/** @brief Defines access point's channel.
 *  Channel selection is only effective when not connected to another AP.
 *  Good practice for minimal channel interference to use
 *  For 20 MHz: 1, 6 or 11 in USA and 1, 5, 9 or 13 in most parts of the world
 *  For 40 MHz: 3 in USA and 3 or 11 in most parts of the world
 */
#define DEFAULT_AP_CHANNEL 					CONFIG_DEFAULT_AP_CHANNEL



/** @brief Defines the access point's default IP address. Default: "10.10.0.1 */
#define DEFAULT_AP_IP						CONFIG_DEFAULT_AP_IP

/** @brief Defines the access point's gateway. This should be the same as your IP. Default: "10.10.0.1" */
#define DEFAULT_AP_GATEWAY					CONFIG_DEFAULT_AP_GATEWAY

/** @brief Defines the access point's netmask. Default: "255.255.255.0" */
#define DEFAULT_AP_NETMASK					CONFIG_DEFAULT_AP_NETMASK

/** @brief Defines access point's maximum number of clients. Default: 4 */
#define DEFAULT_AP_MAX_CONNECTIONS		 	CONFIG_DEFAULT_AP_MAX_CONNECTIONS

/** @brief Defines access point's beacon interval. 100ms is the recommended default. */
#define DEFAULT_AP_BEACON_INTERVAL 			CONFIG_DEFAULT_AP_BEACON_INTERVAL

/** @brief Defines if esp32 shall run both AP + STA when connected to another AP.
 *  Value: 0 will have the own AP always on (APSTA mode)
 *  Value: 1 will turn off own AP when connected to another AP (STA only mode when connected)
 *  Turning off own AP when connected to another AP minimize channel interference and increase throughput
 */
#define DEFAULT_STA_ONLY 					1

/** @brief Defines if wifi power save shall be enabled.
 *  Value: WIFI_PS_NONE for full power (wifi modem always on)
 *  Value: WIFI_PS_MODEM for power save (wifi modem sleep periodically)
 *  Note: Power save is only effective when in STA only mode
 */
#define DEFAULT_STA_POWER_SAVE 				WIFI_PS_NONE

/**
 * @brief LOCAL PATCH (2.1.4 C6): how long a request from another task (an HTTP handler, the
 * app's wifi_task) waits for room in the wifi_manager queue. It was portMAX_DELAY: an HTTP
 * handler then waited with no bound while the wifi_manager task itself waited in httpd_stop()
 * (STOP_AP), for that handler to return. A request that does not fit in time is not sent: the
 * caller is told (an HTTP request gets 503) and can try again.
 */
#define WIFI_MANAGER_POST_WAIT_MS			200

/**
 * @brief Defines the maximum length in bytes of a JSON representation of an access point.
 *
 *  maximum ap string length with full 32 char ssid: 75 + \\n + \0 = 77\n
 *  example: {"ssid":"abcdefghijklmnopqrstuvwxyz012345","chan":12,"rssi":-100,"auth":4},\n
 *  BUT: we need to escape JSON. Imagine a ssid full of \" ? so it's 32 more bytes hence 77 + 32 = 99.\n
 *  this is an edge case but I don't think we should crash in a catastrophic manner just because
 *  someone decided to have a funny wifi name.
 *
 *  LOCAL PATCH (2.1.4 C2e): this is now the average room per access point in the list's buffer
 *  (MAX_AP_NUM of them), not a bound: a raw SSID (json.h) takes up to 6 bytes a character, and
 *  the list is built with bounds, leaving out an entry that does not fit (a valid list still).
 */
#define JSON_ONE_APP_SIZE					99

/**
 * @brief Defines the maximum length in bytes of a JSON representation of the IP information
 * assuming all ips are 4*3 digits, and all characters in the ssid require to be escaped.
 * example: {"ssid":"abcdefghijklmnopqrstuvwxyz012345","ip":"192.168.1.119","netmask":"255.255.255.0","gw":"192.168.1.1","urc":99}
 * Run this JS (browser console is easiest) to come to the conclusion that 159 is the worst case.
 * ```
 * var a = {"ssid":"abcdefghijklmnopqrstuvwxyz012345","ip":"255.255.255.255","netmask":"255.255.255.255","gw":"255.255.255.255","urc":99};
 * // Replace all ssid characters with a double quote which will have to be escaped
 * a.ssid = a.ssid.split('').map(() => '"').join('');
 * console.log(JSON.stringify(a).length); // => 158 +1 for null
 * console.log(JSON.stringify(a)); // print it
 * ```
 *
 * LOCAL PATCH (2.1.4 C2e): with a raw SSID (json.h) the worst case is {"ssid": (8) + the SSID's
 * JSON string (JSON_SSID_STR_MAX, 194) + ,"raw":1 (8) + ,"ip":"255.255.255.255","netmask":"...",
 * "gw":"...","urc":3}\n (84) + the terminator: 295 bytes (136 more than the 159 of before).
 */
#define JSON_IP_INFO_SIZE 					295

/**
 * @brief LOCAL PATCH (2.1.4 C8): the room wifi_manager_status_json() needs: the status of before
 * (JSON_IP_INFO_SIZE, its terminator included), ,"reason":255 (13) and ,"pend": (8), and the
 * candidate's SSID as a JSON string (194, JSON_SSID_STR_MAX in json.h).
 */
#define WIFI_MANAGER_STATUS_JSON_SIZE		510


/**
 * @brief defines the minimum length of an access point password running on WPA2
 */
#define WPA2_MINIMUM_PASSWORD_LENGTH		8


/**
 * @brief Defines the complete list of all messages that the wifi_manager can process.
 *
 * Some of these message are events ("EVENT"), and some of them are action ("ORDER")
 * Each of these messages can trigger a callback function and each callback function is stored
 * in a function pointer array for convenience. Because of this behavior, it is extremely important
 * to maintain a strict sequence and the top level special element 'MESSAGE_CODE_COUNT'
 *
 * @see wifi_manager_set_callback
 */
typedef enum message_code_t {
	NONE = 0,
	WM_ORDER_START_HTTP_SERVER = 1,
	WM_ORDER_STOP_HTTP_SERVER = 2,
	WM_ORDER_START_DNS_SERVICE = 3,
	WM_ORDER_STOP_DNS_SERVICE = 4,
	WM_ORDER_START_WIFI_SCAN = 5,
	WM_ORDER_LOAD_AND_RESTORE_STA = 6,
	WM_ORDER_CONNECT_STA = 7,
	WM_ORDER_DISCONNECT_STA = 8,
	WM_ORDER_START_AP = 9,
	WM_EVENT_STA_DISCONNECTED = 10,
	WM_EVENT_SCAN_DONE = 11,
	WM_EVENT_STA_GOT_IP = 12,
	WM_ORDER_STOP_AP = 13,
	WM_MESSAGE_CODE_COUNT = 14 /* important for the callback array */

}message_code_t;

/**
 * @brief simplified reason codes for a lost connection.
 *
 * esp-idf maintains a big list of reason codes which in practice are useless for most typical application.
 */
typedef enum update_reason_code_t {
	UPDATE_CONNECTION_OK = 0,
	UPDATE_FAILED_ATTEMPT = 1,
	UPDATE_USER_DISCONNECT = 2,
	UPDATE_LOST_CONNECTION = 3
}update_reason_code_t;

/**
 * @brief The kinds of connect attempt (LOCAL PATCH 2.1.4 C8: one owner for all of them, the
 * wifi_manager task; see wifi_manager.c).
 *  - USER: the setup page's Connect, with its candidate (wifi_manager_connect_user_async());
 *  - AUTO_RECONNECT: this component's retry timer, only while the SoftAP is down (C5);
 *  - RESTORE_CONNECTION: the boot's, with the saved network;
 *  - APP_RETRY: the app's router retry (wifi_manager_retry_async()), with the network in use.
 */
typedef enum connection_request_made_by_code_t{
	CONNECTION_REQUEST_NONE = 0,
	CONNECTION_REQUEST_USER = 1,
	CONNECTION_REQUEST_AUTO_RECONNECT = 2,
	CONNECTION_REQUEST_RESTORE_CONNECTION = 3,
	CONNECTION_REQUEST_APP_RETRY = 4,
	CONNECTION_REQUEST_MAX = 0x7fffffff /*force the creation of this enum as a 32 bit int */
}connection_request_made_by_code_t;

/**
 * @brief LOCAL PATCH (2.1.4 C8): WM_ORDER_CONNECT_STA's callback parameter: the order's kind
 * (connection_request_made_by_code_t) in WIFI_MANAGER_CONNECT_KIND_MASK, and
 * WIFI_MANAGER_CONNECT_NOT_STARTED when the order started no attempt.
 */
#define WIFI_MANAGER_CONNECT_KIND_MASK		0xFFu
#define WIFI_MANAGER_CONNECT_NOT_STARTED	0x100u

/**
 * @brief LOCAL PATCH (2.1.4 C8): status.json's "reason" for a user's attempt that got no IP within
 * its time (an IEEE or ESP-IDF disconnect reason otherwise; those end at 212).
 */
#define WIFI_MANAGER_REASON_NO_IP			250

/**
 * The actual WiFi settings in use
 */
struct wifi_settings_t{
	uint8_t ap_ssid[MAX_SSID_SIZE];
	uint8_t ap_pwd[MAX_PASSWORD_SIZE];
	uint8_t ap_channel;
	uint8_t ap_ssid_hidden;
	wifi_bandwidth_t ap_bandwidth;
	bool sta_only;
	wifi_ps_type_t sta_power_save;
	bool sta_static_ip;
	esp_netif_ip_info_t sta_static_ip_config;
};
extern struct wifi_settings_t wifi_settings;


/**
 * @brief Structure used to store one message in the queue.
 */
typedef struct{
	message_code_t code;
	void *param;
} queue_message;


/**
 * @brief returns the current esp_netif object for the STAtion
 */
esp_netif_t* wifi_manager_get_esp_netif_sta();

/**
 * @brief returns the current esp_netif object for the Access Point
 */
esp_netif_t* wifi_manager_get_esp_netif_ap();


/**
 * Allocate heap memory for the wifi manager and start the wifi_manager RTOS task
 */
void wifi_manager_start();

/**
 * Frees up all memory allocated by the wifi_manager and kill the task.
 */
void wifi_manager_destroy();

/**
 * Main task for the wifi_manager
 */
void wifi_manager( void * pvParameters );


/**
 * @brief the network list's JSON. LOCAL PATCH (2.1.4 C2b): NULL while the AP is down (it exists
 * from START_AP to STOP_AP). Read it under wifi_manager_lock_json_buffer().
 */
char* wifi_manager_get_ap_list_json();

/**
 * @brief LOCAL PATCH (2.1.4 C8, C6): writes status.json into out (size at least
 * WIFI_MANAGER_STATUS_JSON_SIZE) and returns its length, the terminator excluded (0, out "", for
 * a smaller buffer). Its own lock, held only to copy the status out: never while formatting or
 * sending, and not the network list's (wifi_manager_lock_json_buffer()). Any task.
 *  {"ssid":S[,"raw":1],"ip":A,"netmask":A,"gw":A,"urc":U,"reason":R,"pend":P}
 *  - ssid, urc: urc (update_reason_code_t) for the network in use, or UPDATE_FAILED_ATTEMPT for a
 *    user's candidate that failed, with its SSID; the addresses for urc 0 only ("0" otherwise);
 *  - reason: that failure's disconnect reason (WIFI_MANAGER_REASON_NO_IP: no IP in time), 0 if
 *    none;
 *  - pend: the SSID of a user's candidate not yet decided (stored, waiting or connecting), "" if
 *    none. While it is set, ssid and urc are still the hub's state of before.
 * "{}" before any status (a boot with nothing decided yet) with no candidate.
 */
size_t wifi_manager_status_json(char *out, size_t size);

/**
 * @brief LOCAL PATCH (2.1.4 WP1, a bench diagnostic): true while a scan the wifi_manager task
 * started (WM_ORDER_START_WIFI_SCAN) has not had its WM_EVENT_SCAN_DONE taken by that task. The
 * radio is then on the scan's channels part of the time, not the AP's or the router's. For the
 * wifi_manager task and its callbacks only (no lock).
 */
bool wifi_manager_scan_in_flight();


/**
 * @brief asks for a Wi-Fi scan. LOCAL PATCH (2.1.4 C6): false when the request did not fit in the
 * queue within WIFI_MANAGER_POST_WAIT_MS.
 */
bool wifi_manager_scan_async();

/**
 * @brief LOCAL PATCH (2.1.4 C10b): a scan for the setup page's network list, which GET /ap.json
 * only reads (a cache). rescan false: the page's load, which orders one only when the list is
 * empty or older than 60 s; rescan true: the page's Rescan (POST /scan.json). Either way at least
 * 20 s after the last one ordered here, or 10 s after one that did not start, ended failed, or
 * did not fit in the queue. Returns 1 when one was ordered, 0 when none was due
 * (*wait_ms, when not NULL: the ms until a Rescan may order one, 0 if the list is fresh), -1 when
 * the order did not fit in the queue. The httpd task only.
 */
int wifi_manager_scan_request(bool rescan, uint32_t *wait_ms);

/**
 * @brief LOCAL PATCH (2.1.4 C10b): the network list has been rebuilt from a scan since it was
 * allocated, in this AP session. Until it has, the page's background reads of GET /ap.json order
 * the scan too (wifi_manager_scan_request(false, ...): 20 s after the last order, or 10 s after
 * one that failed): the order of the page's load can fail (a scan cannot start while a connect
 * attempt runs, ESP_ERR_WIFI_STATE, and an attempt that starts stops a running scan), and nothing
 * else would order it again. That holds while the list has no buffer too (low heap): each
 * SCAN_DONE tries the allocation again before it reads the records. Any task; a stale read costs
 * one order too many, or one a poll late.
 */
bool wifi_manager_ap_list_built();

/**
 * @brief LOCAL PATCH (2.1.4 WP1): the largest free block with the caps has room for size bytes
 * and WIFI_MANAGER_HEAP_MARGIN (4 KB) more: the one test for an allocation that can do without
 * (the network list, a server's restart, the HTTP server's copy of the list), so a low heap sees
 * no failed allocation from them (MONITOR's allocfail), and keeps that margin for lwIP and the
 * Wi-Fi driver. Any task.
 */
bool wifi_manager_heap_has(uint32_t caps, size_t size);


/**
 * @brief saves the current STA wifi config to flash ram storage.
 */
esp_err_t wifi_manager_save_sta_config();

/**
 * @brief fetch a previously STA wifi config in the flash ram storage.
 * @return true if a previously saved config was found, false otherwise.
 */
bool wifi_manager_fetch_wifi_sta_config();

wifi_config_t* wifi_manager_get_wifi_sta_config();


/**
 * @brief LOCAL PATCH (2.1.4 C8): the setup page's Connect: stores the candidate (ssid_len 1-32
 * bytes, password_len 0-64, 0 for an open network; channel: the page's hint, 0 for none) and
 * queues a USER order, waiting WIFI_MANAGER_POST_WAIT_MS at most. The network in use and NVS
 * change only when the candidate gets an IP; a candidate that fails is reported in status.json
 * and dropped, and the network in use stays. A newer call replaces any earlier candidate: one
 * that waits is dropped; one that is connecting is ended (after at most 8 s), and is not saved
 * even if it gets its IP. False for bad lengths, or when the order did not fit in the queue (the
 * candidate is taken back then). Any task; the bytes are copied.
 */
bool wifi_manager_connect_user_async(const uint8_t *ssid, size_t ssid_len, const uint8_t *password, size_t password_len, uint8_t channel);

/**
 * @brief LOCAL PATCH (2.1.4 C8): the app's router retry: an APP_RETRY order, with the network in
 * use. It starts nothing while the STA is connected, an attempt runs or a user's candidate waits
 * (the callback says NOT_STARTED). False when it did not fit in the queue within
 * WIFI_MANAGER_POST_WAIT_MS.
 */
bool wifi_manager_retry_async();

/**
 * @brief requests a wifi scan
 */
void wifi_manager_scan_awifi_manager_send_messagesync();

/**
 * @brief requests to disconnect and forget about the access point.
 * LOCAL PATCH (2.1.4 C6): false when the request did not fit in the queue within
 * WIFI_MANAGER_POST_WAIT_MS (nothing is sent then).
 */
bool wifi_manager_disconnect_async();

/**
 * @brief LOCAL PATCH (2.1.4 C12): the SoftAP stops in ms milliseconds (rounded up to a tick, at
 * least one), for the app's AP-tail policy (plan section 4.6): re-arms the single AP-shutdown
 * timer, whose STOP_AP stops the AP only with the STA connected. Only while the STA is
 * connected (WIFI_MANAGER_WIFI_CONNECTED_BIT): false otherwise, and false with a W line when
 * the timer task's queue is full. A later call replaces the deadline, sooner or later; each
 * GOT_IP with the AP up arms the default WIFI_MANAGER_SHUTDOWN_AP_TIMER again first, and a lost
 * link stops the timer (the AP stays up). Never waits, from any task: a GOT_IP callback on the
 * wifi_manager task (a post to its own queue could wait for good), wifi_task, an HTTP handler.
 */
bool wifi_manager_ap_stop_in(uint32_t ms);

/**
 * @brief LOCAL PATCH (2.1.4 WP2): the SoftAP's stop has finished, for the app's cloud admission
 * (no TLS until the AP's memory is back, plan section 4.6). false while a STOP_AP is under way:
 * from just before its switch to STA mode until its DNS task, HTTP server and network list are
 * gone (the DNS stop waits up to 1 s, the HTTP stop until a running handler returns). true
 * otherwise, with *ms_since (when not NULL) set to the time since the last STOP_AP finished,
 * UINT32_MAX if none has (saturating). A STOP_AP whose switch fails clears it at once (the AP
 * stays up, and the mode says so). Never waits, from any task. Read it after the mode: a mode
 * read as STA during a stop then always finds the stop under way.
 */
bool wifi_manager_ap_stop_done(uint32_t *ms_since);

/**
 * @brief Tries to get access to json buffer mutex.
 *
 * The HTTP server can try to access the json to serve clients while the wifi manager thread can try
 * to update it. These two tasks are synchronized through a mutex.
 *
 * The mutex is used by both the access point list json and the connection status json.\n
 * These two resources should technically have their own mutex but we lose some flexibility to save
 * on memory.
 *
 * This is a simple wrapper around freeRTOS function xSemaphoreTake.
 *
 * @param xTicksToWait The time in ticks to wait for the semaphore to become available.
 * @return true in success, false otherwise.
 */
bool wifi_manager_lock_json_buffer(TickType_t xTicksToWait);

/**
 * @brief Releases the json buffer mutex.
 */
void wifi_manager_unlock_json_buffer();

/* LOCAL PATCH (2.1.4 C8): wifi_manager_generate_ip_info_json() and wifi_manager_clear_ip_info_json()
 * are gone: the wifi_manager task keeps the status itself, and wifi_manager_status_json() reads it. */

/**
 * @brief Clear the list of access points (LOCAL PATCH 2.1.4 C2b: nothing while the AP is down).
 * The list itself is rebuilt inside wifi_manager.c after each scan.
 * @note This is not thread-safe and should be called only if wifi_manager_lock_json_buffer call is successful.
 */
void wifi_manager_clear_access_points_json();


/**
 * @brief Start the mDNS service
 */
void wifi_manager_initialise_mdns();


bool wifi_manager_lock_sta_ip_string(TickType_t xTicksToWait);
void wifi_manager_unlock_sta_ip_string();

/**
 * @brief gets the string representation of the STA IP address, e.g.: "192.168.1.69"
 */
char* wifi_manager_get_sta_ip_string();

/**
 * @brief thread safe char representation of the STA IP update
 */
void wifi_manager_safe_update_sta_ip_string(uint32_t ip);


/**
 * @brief Register a callback to a custom function when specific event message_code happens.
 *
 * LOCAL PATCH (2.1.4 C2a): the callback's parameter is a scalar carried in the pointer, never a
 * pointer to read (cast it with (uintptr_t)):
 *  - WM_EVENT_STA_GOT_IP: the STA's IPv4 address in network byte order (esp_ip4_addr_t.addr);
 *  - WM_EVENT_STA_DISCONNECTED: the disconnect reason (wifi_err_reason_t), 0 if none was given;
 *    once per attempt or link that ended (LOCAL PATCH 2.1.4 C8: also, with
 *    WIFI_REASON_ASSOC_LEAVE, for one ended by our esp_wifi_disconnect() whose event never came);
 *  - WM_ORDER_CONNECT_STA (LOCAL PATCH 2.1.4 C8): once per order, when an attempt starts (its
 *    kind) or when the order starts none (its kind | WIFI_MANAGER_CONNECT_NOT_STARTED): the STA
 *    connected, an attempt under way, a user's candidate first, or the driver refused it. A USER
 *    order that waits is reported when it is decided. No STA_DISCONNECTED follows an order that
 *    started nothing;
 *  - WM_EVENT_SCAN_DONE: the scan's status, 0 = success;
 *  - WM_ORDER_STOP_AP (LOCAL PATCH 2.1.4 WP2): 0 = the AP and its servers are stopped; 1 = the
 *    switch to STA mode failed, so the AP keeps its servers and the stop is tried again
 *    WIFI_MANAGER_STOP_AP_RETRY_MS later (5 s);
 *  - every other message: NULL.
 */
void wifi_manager_set_callback(message_code_t message_code, void (*func_ptr)(void*) );


BaseType_t wifi_manager_send_message(message_code_t code, void *param);
BaseType_t wifi_manager_send_message_to_front(message_code_t code, void *param);

/**
 * @brief LOCAL PATCH (2.1.4 C6): wifi_manager_send_message() waiting at most wait ticks for room
 * in the queue: pdPASS if the message was queued. For other tasks (the wifi_manager task never
 * posts to its own queue with a wait: it is the queue's only reader).
 */
BaseType_t wifi_manager_send_message_wait(message_code_t code, void *param, TickType_t wait);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MANAGER_H_INCLUDED */
