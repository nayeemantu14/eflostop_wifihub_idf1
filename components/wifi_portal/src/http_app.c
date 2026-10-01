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

@file http_app.c
@author Tony Pottier
@brief Defines all functions necessary for the HTTP server to run.

Contains the freeRTOS task for the HTTP listener and all necessary support
function to process requests, decode URLs, serve files, etc. etc.

@note http_server task cannot run without the wifi_manager task!
@see https://idyl.io
@see https://github.com/tonyp7/esp32-wifi-manager
*/


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include "esp_netif.h"
#include <esp_http_server.h>
#include <lwip/sockets.h>

#include "wifi_manager.h"
#include "http_app.h"


/* @brief tag used for ESP serial console messages */
static const char TAG[] = "http_server";

/* @brief the HTTP server handle */
static httpd_handle_t httpd_handle = NULL;

/* function pointers to URI handlers that can be user made */
esp_err_t (*custom_get_httpd_uri_handler)(httpd_req_t *r) = NULL;
esp_err_t (*custom_post_httpd_uri_handler)(httpd_req_t *r) = NULL;

/* LOCAL PATCH (2.1.4 C10a): the activity hook (http_app_set_activity_hook()), NULL = none.
 * Set once by the app; read by the httpd and dns_server tasks. */
static http_app_activity_hook_t volatile activity_hook = NULL;

/* strings holding the URLs of the wifi manager.
 * LOCAL PATCH (2.1.4 C2g): string literals (CONFIG_WEBAPP_LOCATION is one), where http_app_start()
 * malloc()ed a copy of each, unchecked, and http_app_stop() freed them while a request could still
 * be reading them. The redirect is to http://<AP IP>, with the location appended unless it is the
 * root "/", as before. */
static const char http_root_url[] = WEBAPP_LOCATION;
static const char http_js_url[] = WEBAPP_LOCATION "code.js";
static const char http_css_url[] = WEBAPP_LOCATION "style.css";
static const char http_connect_url[] = WEBAPP_LOCATION "connect.json";
static const char http_ap_url[] = WEBAPP_LOCATION "ap.json";
static const char http_status_url[] = WEBAPP_LOCATION "status.json";
static const char http_watts_logo_url[] = WEBAPP_LOCATION "Watts_Logo.png";
static const char *const http_redirect_url = (sizeof(WEBAPP_LOCATION) == 2) ?
		"http://" DEFAULT_AP_IP : "http://" DEFAULT_AP_IP WEBAPP_LOCATION;

/* LOCAL PATCH (2.1.4 C2g): the room for a request's Host header, terminator included. A longer
 * Host cannot be the AP's or the STA's address (with a port): it gets the redirect. */
#define HTTP_HOST_BUF_SIZE			64

/**
 * @brief embedded binary data.
 * @see file "component.mk"
 * @see https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#embedding-binary-data
 */
extern const uint8_t style_css_start[] asm("_binary_style_css_start");
extern const uint8_t style_css_end[]   asm("_binary_style_css_end");
extern const uint8_t code_js_start[] asm("_binary_code_js_start");
extern const uint8_t code_js_end[] asm("_binary_code_js_end");
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t watts_logo_png_start[] asm("_binary_Watts_Logo_png_start");
extern const uint8_t watts_logo_png_end[] asm("_binary_Watts_Logo_png_end");


/* const httpd related values stored in ROM */
const static char http_200_hdr[] = "200 OK";
const static char http_302_hdr[] = "302 Found";
const static char http_400_hdr[] = "400 Bad Request";
const static char http_403_hdr[] = "403 Forbidden";
const static char http_404_hdr[] = "404 Not Found";
const static char http_503_hdr[] = "503 Service Unavailable";
const static char http_location_hdr[] = "Location";
const static char http_content_type_html[] = "text/html";
const static char http_content_type_js[] = "text/javascript";
const static char http_content_type_css[] = "text/css";
const static char http_content_type_json[] = "application/json";
const static char http_content_type_png[] = "image/png";
const static char http_cache_control_hdr[] = "Cache-Control";
const static char http_cache_control_no_cache[] = "no-store, no-cache, must-revalidate, max-age=0";
const static char http_cache_control_cache[] = "public, max-age=31536000";
const static char http_pragma_hdr[] = "Pragma";
const static char http_pragma_no_cache[] = "no-cache";



esp_err_t http_app_set_handler_hook( httpd_method_t method,  esp_err_t (*handler)(httpd_req_t *r)  ){

	if(method == HTTP_GET){
		custom_get_httpd_uri_handler = handler;
		return ESP_OK;
	}
	else if(method == HTTP_POST){
		custom_post_httpd_uri_handler = handler;
		return ESP_OK;
	}
	else{
		return ESP_ERR_INVALID_ARG;
	}

}


/* LOCAL PATCH (2.1.4 C10a): the activity hook */
void http_app_set_activity_hook(http_app_activity_hook_t hook){
	activity_hook = hook;
}

void http_app_note_activity(http_app_activity_t kind, uint32_t client_ip){
	http_app_activity_hook_t hook = activity_hook;
	if(hook){
		hook(kind, client_ip);
	}
}

/* a socket address of the server's: with lwIP IPv6 on (CONFIG_LWIP_IPV6) its socket is IPv6, so
 * an IPv4 peer comes as an IPv4-mapped address (::ffff:a.b.c.d) */
typedef union {
	struct sockaddr sa;
	struct sockaddr_in in4;
#if LWIP_IPV6
	struct sockaddr_in6 in6;
#endif
} http_app_sockaddr_t;

/**
 * @brief the IPv4 address in network byte order of an AF_INET address, or of an IPv4-mapped
 * IPv6 one; 0 for anything else (a native IPv6 address included).
 */
static uint32_t http_app_ipv4_of(const http_app_sockaddr_t *addr){

	uint32_t ip = 0;

	if(addr->sa.sa_family == AF_INET){
		ip = addr->in4.sin_addr.s_addr;
	}
#if LWIP_IPV6
	else if(addr->sa.sa_family == AF_INET6){
		static const uint8_t v4_mapped[12] = { 0,0,0,0, 0,0,0,0, 0,0,0xff,0xff };
		if(memcmp(addr->in6.sin6_addr.s6_addr, v4_mapped, sizeof(v4_mapped)) == 0){
			memcpy(&ip, &addr->in6.sin6_addr.s6_addr[12], sizeof(ip));
		}
	}
#endif
	return ip;
}

/**
 * @brief LOCAL PATCH (2.1.4 C3, plan D3): the request came to the SoftAP's own address
 * (DEFAULT_AP_IP) from a client in the SoftAP's subnet (DEFAULT_AP_NETMASK), both IPv4 or
 * IPv4-mapped; a native IPv6 address on either side fails. *client_ip is the client's IPv4
 * address in network byte order, 0 if unknown (the activity hook's). The local address alone
 * would not do: lwIP takes a packet for any of the hub's addresses on any interface, so a
 * home-LAN host with a route to the SoftAP's subnet through the hub's STA address reaches
 * 10.10.0.1:80 while the SoftAP is up (HANDOFF 15h, WP1 risk 8).
 */
static bool http_app_on_ap(httpd_req_t *req, uint32_t *client_ip){

	http_app_sockaddr_t local, peer;
	socklen_t len;
	struct in_addr ap_ip, ap_mask;

	*client_ip = 0;
	int fd = httpd_req_to_sockfd(req);
	if(fd < 0){
		return false;
	}
	len = sizeof(peer);
	if(getpeername(fd, &peer.sa, &len) == 0){
		*client_ip = http_app_ipv4_of(&peer);
	}
	len = sizeof(local);
	if(getsockname(fd, &local.sa, &len) != 0 ||
			inet_pton(AF_INET, DEFAULT_AP_IP, &ap_ip) != 1 || inet_pton(AF_INET, DEFAULT_AP_NETMASK, &ap_mask) != 1){
		return false;
	}
	return http_app_ipv4_of(&local) == ap_ip.s_addr && *client_ip != 0 &&
			(*client_ip & ap_mask.s_addr) == (ap_ip.s_addr & ap_mask.s_addr);
}

/**
 * @brief LOCAL PATCH (2.1.4 C3): the answer to a request that did not come to the SoftAP from
 * its subnet (http_app_on_ap()): 403 with no body; nothing is done, read or reported to the
 * activity hook. Logged at DEBUG only, so a LAN scan cannot fill the log.
 */
static esp_err_t http_app_refuse(httpd_req_t *req){
	ESP_LOGD(TAG, "method %d %s refused (403): not to the SoftAP from its subnet", (int)req->method, req->uri);
	httpd_resp_set_status(req, http_403_hdr);
	httpd_resp_send(req, NULL, 0);
	return ESP_OK;
}


static esp_err_t http_server_delete_handler(httpd_req_t *req){

	ESP_LOGI(TAG, "DELETE %s", req->uri);

	/* LOCAL PATCH (2.1.4 C3): only for a client on the SoftAP (the portal's forget, D9, is kept) */
	uint32_t client_ip;
	if(!http_app_on_ap(req, &client_ip)){
		return http_app_refuse(req);
	}

	/* DELETE /connect.json */
	if(strcmp(req->uri, http_connect_url) == 0){
		http_app_note_activity(HTTP_APP_ACT_API_USER, client_ip); /* LOCAL PATCH (2.1.4 C10a) */
		wifi_manager_disconnect_async();

		httpd_resp_set_status(req, http_200_hdr);
		httpd_resp_set_type(req, http_content_type_json);
		httpd_resp_set_hdr(req, http_cache_control_hdr, http_cache_control_no_cache);
		httpd_resp_set_hdr(req, http_pragma_hdr, http_pragma_no_cache);
		httpd_resp_send(req, NULL, 0);
	}
	else{
		httpd_resp_set_status(req, http_404_hdr);
		httpd_resp_send(req, NULL, 0);
	}

	return ESP_OK;
}


static esp_err_t http_server_post_handler(httpd_req_t *req){


	esp_err_t ret = ESP_OK;

	ESP_LOGI(TAG, "POST %s", req->uri);

	/* LOCAL PATCH (2.1.4 C3): only for a client on the SoftAP; a user hook's URIs too */
	uint32_t client_ip;
	if(!http_app_on_ap(req, &client_ip)){
		return http_app_refuse(req);
	}

	/* POST /connect.json */
	if(strcmp(req->uri, http_connect_url) == 0){

		http_app_note_activity(HTTP_APP_ACT_API_USER, client_ip); /* LOCAL PATCH (2.1.4 C10a) */

		/* buffers for the headers
		 * LOCAL PATCH (2.1.4 C2g): on the stack, where two malloc()s per request were used unchecked.
		 * The password's copy is cleared before the handler returns. */
		size_t ssid_len = 0, password_len = 0;
		char ssid[MAX_SSID_SIZE + 1];
		char password[MAX_PASSWORD_SIZE + 1];
		wifi_config_t* config = wifi_manager_get_wifi_sta_config();

		/* len of values provided */
		ssid_len = httpd_req_get_hdr_value_len(req, "X-Custom-ssid");
		password_len = httpd_req_get_hdr_value_len(req, "X-Custom-pwd");

		/* get the actual value of the headers */
		bool valid = ssid_len && ssid_len <= MAX_SSID_SIZE && password_len && password_len <= MAX_PASSWORD_SIZE &&
				httpd_req_get_hdr_value_str(req, "X-Custom-ssid", ssid, sizeof(ssid)) == ESP_OK &&
				httpd_req_get_hdr_value_str(req, "X-Custom-pwd", password, sizeof(password)) == ESP_OK;

		if(valid && config != NULL){

			memset(config, 0x00, sizeof(wifi_config_t));
			memcpy(config->sta.ssid, ssid, ssid_len);
			memcpy(config->sta.password, password, password_len);
			/* LOCAL PATCH (2.1.4 C1): no credential in the log, the password's length only */
			ESP_LOGI(TAG, "ssid: %s, pwd_len: %u", ssid, (unsigned)password_len);
			ESP_LOGD(TAG, "http_server_post_handler: wifi_manager_connect_async() call");
			wifi_manager_connect_async();

			httpd_resp_set_status(req, http_200_hdr);
			httpd_resp_set_type(req, http_content_type_json);
			httpd_resp_set_hdr(req, http_cache_control_hdr, http_cache_control_no_cache);
			httpd_resp_set_hdr(req, http_pragma_hdr, http_pragma_no_cache);
			httpd_resp_send(req, NULL, 0);

		}
		else if(valid){
			/* LOCAL PATCH (2.1.4 C2g): no STA config to write into (never, once the start succeeded) */
			httpd_resp_set_status(req, http_503_hdr);
			httpd_resp_send(req, NULL, 0);
		}
		else{
			/* bad request the authentification header is not complete/not the correct format */
			httpd_resp_set_status(req, http_400_hdr);
			httpd_resp_send(req, NULL, 0);
		}

		memset(password, 0x00, sizeof(password));

	}
	else{

		if(custom_post_httpd_uri_handler == NULL){
			httpd_resp_set_status(req, http_404_hdr);
			httpd_resp_send(req, NULL, 0);
		}
		else{

			/* if there's a hook, run it */
			ret = (*custom_post_httpd_uri_handler)(req);
		}
	}

	return ret;
}


static esp_err_t http_server_get_handler(httpd_req_t *req){

    /* LOCAL PATCH (2.1.4 C2g): the Host header in a buffer on the stack, where a malloc() of its
     * length was made per request and used unchecked. A Host that does not fit (HTTP_HOST_BUF_SIZE)
     * is read as an empty one, which gets the redirect, as an unreadable one always did. */
    char host[HTTP_HOST_BUF_SIZE];
    bool has_host = false;
    size_t buf_len;
    esp_err_t ret = ESP_OK;

    ESP_LOGD(TAG, "GET %s", req->uri);

    /* LOCAL PATCH (2.1.4 C3): only for a client on the SoftAP (HEAD too, which shares this
     * handler; a user hook's URIs too) */
    uint32_t client_ip;
    if(!http_app_on_ap(req, &client_ip)){
    	return http_app_refuse(req);
    }

    host[0] = '\0';
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
    	has_host = true;
    	if(buf_len > sizeof(host) || httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK){
    		/* if something is wrong we just 0 the whole memory */
    		memset(host, 0x00, sizeof(host));
    	}
    }

	/* determine if Host is from the STA IP address
	 * LOCAL PATCH (2.1.4 C3): a request to the STA's address is refused above, so this now only
	 * spares a request to the SoftAP whose Host names the STA's address the redirect */
	bool access_from_sta_ip = false;
	if(has_host && wifi_manager_lock_sta_ip_string(portMAX_DELAY)){
		const char *sta_ip = wifi_manager_get_sta_ip_string();
		access_from_sta_ip = (sta_ip != NULL && strstr(host, sta_ip) != NULL);
		wifi_manager_unlock_sta_ip_string();
	}


	if (has_host && !strstr(host, DEFAULT_AP_IP) && !access_from_sta_ip) {

		/* Captive Portal functionality */
		/* 302 Redirect to IP of the access point */
		http_app_note_activity(HTTP_APP_ACT_PROBE_302, client_ip); /* LOCAL PATCH (2.1.4 C10a): this and the calls below */
		httpd_resp_set_status(req, http_302_hdr);
		httpd_resp_set_hdr(req, http_location_hdr, http_redirect_url);
		httpd_resp_send(req, NULL, 0);

	}
	else{

		/* GET /  */
		if(strcmp(req->uri, http_root_url) == 0){
			http_app_note_activity(HTTP_APP_ACT_PAGE, client_ip);
			httpd_resp_set_status(req, http_200_hdr);
			httpd_resp_set_type(req, http_content_type_html);
			httpd_resp_send(req, (char*)index_html_start, index_html_end - index_html_start);
		}
		/* GET /code.js */
		else if(strcmp(req->uri, http_js_url) == 0){
			http_app_note_activity(HTTP_APP_ACT_PAGE, client_ip);
			httpd_resp_set_status(req, http_200_hdr);
			httpd_resp_set_type(req, http_content_type_js);
			httpd_resp_send(req, (char*)code_js_start, code_js_end - code_js_start);
		}
		/* GET /style.css */
		else if(strcmp(req->uri, http_css_url) == 0){
			http_app_note_activity(HTTP_APP_ACT_PAGE, client_ip);
			httpd_resp_set_status(req, http_200_hdr);
			httpd_resp_set_type(req, http_content_type_css);
			httpd_resp_set_hdr(req, http_cache_control_hdr, http_cache_control_cache);
			httpd_resp_send(req, (char*)style_css_start, style_css_end - style_css_start);
		}
		/* GET /Watts_Logo.png */
		else if(strcmp(req->uri, http_watts_logo_url) == 0){
			http_app_note_activity(HTTP_APP_ACT_PAGE, client_ip);
			httpd_resp_set_status(req, http_200_hdr);
			httpd_resp_set_type(req, http_content_type_png);
			httpd_resp_set_hdr(req, http_cache_control_hdr, http_cache_control_cache);
			httpd_resp_send(req, (char*)watts_logo_png_start, watts_logo_png_end - watts_logo_png_start);
		}
		/* GET /ap.json */
		else if(strcmp(req->uri, http_ap_url) == 0){

			http_app_note_activity(HTTP_APP_ACT_API_BG, client_ip);

			/* if we can get the mutex, write the last version of the AP list */
			if(wifi_manager_lock_json_buffer(( TickType_t ) 10)){

				httpd_resp_set_status(req, http_200_hdr);
				httpd_resp_set_type(req, http_content_type_json);
				httpd_resp_set_hdr(req, http_cache_control_hdr, http_cache_control_no_cache);
				httpd_resp_set_hdr(req, http_pragma_hdr, http_pragma_no_cache);
				/* LOCAL PATCH (2.1.4 C2b): no list while the AP is down: an empty one */
				const char* ap_buf = wifi_manager_get_ap_list_json();
				if(ap_buf == NULL){
					ap_buf = "[]\n";
				}
				httpd_resp_send(req, ap_buf, strlen(ap_buf));
				wifi_manager_unlock_json_buffer();
			}
			else{
				httpd_resp_set_status(req, http_503_hdr);
				httpd_resp_send(req, NULL, 0);
				ESP_LOGE(TAG, "http_server_netconn_serve: GET /ap.json failed to obtain mutex");
			}

			/* request a wifi scan */
			wifi_manager_scan_async();
		}
		/* GET /status.json */
		else if(strcmp(req->uri, http_status_url) == 0){

			http_app_note_activity(HTTP_APP_ACT_STATUS, client_ip);

			if(wifi_manager_lock_json_buffer(( TickType_t ) 10)){
				char *buff = wifi_manager_get_ip_info_json();
				if(buff){
					httpd_resp_set_status(req, http_200_hdr);
					httpd_resp_set_type(req, http_content_type_json);
					httpd_resp_set_hdr(req, http_cache_control_hdr, http_cache_control_no_cache);
					httpd_resp_set_hdr(req, http_pragma_hdr, http_pragma_no_cache);
					httpd_resp_send(req, buff, strlen(buff));
					wifi_manager_unlock_json_buffer();
				}
				else{
					/* LOCAL PATCH (2.1.4 C2g): the json lock is given back here too (it was kept) */
					wifi_manager_unlock_json_buffer();
					httpd_resp_set_status(req, http_503_hdr);
					httpd_resp_send(req, NULL, 0);
				}
			}
			else{
				httpd_resp_set_status(req, http_503_hdr);
				httpd_resp_send(req, NULL, 0);
				ESP_LOGE(TAG, "http_server_netconn_serve: GET /status.json failed to obtain mutex");
			}
		}
		else{

			if(custom_get_httpd_uri_handler == NULL){
				httpd_resp_set_status(req, http_404_hdr);
				httpd_resp_send(req, NULL, 0);
			}
			else{

				/* if there's a hook, run it */
				ret = (*custom_get_httpd_uri_handler)(req);
			}
		}

	}

    return ret;

}

/* URI wild card for any GET request */
static const httpd_uri_t http_server_get_request = {
    .uri       = "*",
    .method    = HTTP_GET,
    .handler   = http_server_get_handler
};

static const httpd_uri_t http_server_post_request = {
	.uri	= "*",
	.method = HTTP_POST,
	.handler = http_server_post_handler
};

static const httpd_uri_t http_server_delete_request = {
	.uri	= "*",
	.method = HTTP_DELETE,
	.handler = http_server_delete_handler
};

/**
 * @brief HEAD handler – needed for captive portal detection (iOS sends HEAD to probe connectivity).
 * Delegates to the GET handler; ESP-IDF httpd automatically suppresses the response body for HEAD.
 */
static const httpd_uri_t http_server_head_request = {
	.uri	= "*",
	.method = HTTP_HEAD,
	.handler = http_server_get_handler
};


void http_app_stop(){

	if(httpd_handle != NULL){

		/* stop server (LOCAL PATCH 2.1.4 C2g: the URLs are literals, nothing to free) */
		httpd_stop(httpd_handle);
		httpd_handle = NULL;
	}
}


bool http_app_start(bool lru_purge_enable){

	esp_err_t err;

	if(httpd_handle == NULL){

		httpd_config_t config = HTTPD_DEFAULT_CONFIG();

		/* this is an important option that isn't set up by default.
		 * We could register all URLs one by one, but this would not work while the fake DNS is active */
		config.uri_match_fn = httpd_uri_match_wildcard;
		config.lru_purge_enable = lru_purge_enable;

		/* Captive portals generate many simultaneous requests from the OS
		 * (probes, DNS, parallel asset loads). Increase socket capacity and
		 * shorten timeouts so stale connections are recycled quickly. */
		config.max_open_sockets = 10;
		config.recv_wait_timeout = 4;
		config.send_wait_timeout = 4;

		err = httpd_start(&httpd_handle, &config);

	    if (err == ESP_OK) {
	        ESP_LOGI(TAG, "Registering URI handlers");
	        httpd_register_uri_handler(httpd_handle, &http_server_get_request);
	        httpd_register_uri_handler(httpd_handle, &http_server_post_request);
	        httpd_register_uri_handler(httpd_handle, &http_server_delete_request);
	        httpd_register_uri_handler(httpd_handle, &http_server_head_request);
	    }
	    else {
	        /* LOCAL PATCH (2.1.4 C2g): a failed start was silent (httpd_handle stays NULL: the next
	         * START_AP tries again, and LOCAL PATCH 2.1.4 WP1: with the AP up, wifi_manager's own
	         * retry, wifi_manager_start_ap_servers()) */
	        ESP_LOGE(TAG, "httpd_start failed (%s)", esp_err_to_name(err));
	    }
	}

	return httpd_handle != NULL;
}
