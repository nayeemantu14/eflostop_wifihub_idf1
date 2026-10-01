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

@file http_app.h
@author Tony Pottier
@brief Defines all functions necessary for the HTTP server to run.

Contains the freeRTOS task for the HTTP listener and all necessary support
function to process requests, decode URLs, serve files, etc. etc.

@note http_server task cannot run without the wifi_manager task!
@see https://idyl.io
@see https://github.com/tonyp7/esp32-wifi-manager
*/

#ifndef HTTP_APP_H_INCLUDED
#define HTTP_APP_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <esp_http_server.h>

#ifdef __cplusplus
extern "C" {
#endif


/** @brief Defines the URL where the wifi manager is located
 *  By default it is at the server root (ie "/"). If you wish to add your own webpages
 *  you may want to relocate the wifi manager to another URL, for instance /wifimanager
 */
#define WEBAPP_LOCATION 					CONFIG_WEBAPP_LOCATION


/**
 * @brief spawns the http server
 * LOCAL PATCH (2.1.4 WP1): does nothing while it runs. Returns whether it runs after the call:
 * false when httpd_start() failed (logged), for lack of memory, which wifi_manager tries again
 * while the AP is up.
 * LOCAL PATCH (2.1.4 C3): wifi_manager runs the server only while the AP is up: it starts it at
 * START_AP and stops it at STOP_AP, and no longer at boot or after STOP_AP.
 */
bool http_app_start(bool lru_purge_enable);

/**
 * @brief stops the http server 
 */
void http_app_stop();

/** 
 * @brief sets a hook into the wifi manager URI handlers. Setting the handler to NULL disables the hook.
 * @return ESP_OK in case of success, ESP_ERR_INVALID_ARG if the method is unsupported.
 */
esp_err_t http_app_set_handler_hook( httpd_method_t method,  esp_err_t (*handler)(httpd_req_t *r)  );


/* LOCAL PATCH (2.1.4 C10a): the activity hook */

/**
 * @brief What a portal client did, as reported to the activity hook.
 */
typedef enum http_app_activity_t {
	HTTP_APP_ACT_DNS = 0,		/**< a query to the captive DNS (dns_server task) */
	HTTP_APP_ACT_PROBE_302,		/**< a request for another host, answered with the 302 to the portal */
	HTTP_APP_ACT_PAGE,			/**< the page or one of its assets */
	HTTP_APP_ACT_API_USER,		/**< a user action on the page: POST or DELETE /connect.json */
	HTTP_APP_ACT_API_BG,		/**< a request the page makes on its own: GET /ap.json */
	HTTP_APP_ACT_STATUS,		/**< GET /status.json */
	HTTP_APP_ACT_COUNT
} http_app_activity_t;

/**
 * @brief The activity hook. Called on the httpd task for every request the portal answers itself
 * (PROBE_302, PAGE, API_USER, API_BG, STATUS: not a user hook's URI nor a 404), at its start,
 * and on the dns_server task for every DNS query it answers (DNS): so on two tasks at once.
 * client_ip is the client's IPv4 address in network byte order (as esp_ip4_addr_t.addr),
 * 0 if unknown. The request waits for the hook: it must be short and must not block.
 */
typedef void (*http_app_activity_hook_t)(http_app_activity_t kind, uint32_t client_ip);

/**
 * @brief sets the activity hook. NULL (the default) disables it. A plain store: it can be set
 * before wifi_manager_start().
 */
void http_app_set_activity_hook(http_app_activity_hook_t hook);

/**
 * @brief reports one activity to the hook, if one is set. For the component's own tasks
 * (the dns_server task).
 */
void http_app_note_activity(http_app_activity_t kind, uint32_t client_ip);


#ifdef __cplusplus
}
#endif

#endif
