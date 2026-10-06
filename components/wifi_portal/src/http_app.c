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
#include <esp_heap_caps.h>
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

/* LOCAL PATCH (2.1.4 C12): the Finish hook (http_app_set_finish_hook()), NULL = none. Set once by
 * the app; read by the httpd task. */
static http_app_finish_hook_t volatile finish_hook = NULL;

/* LOCAL PATCH (2.1.4 C12): with no Finish hook set, a Finish stops the SoftAP this long after it
 * (the STA connected only, as every stop) */
#define HTTP_APP_FINISH_STOP_MS		2000

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
static const char http_scan_url[] = WEBAPP_LOCATION "scan.json";		/* LOCAL PATCH (2.1.4 C10b) */
static const char http_finish_url[] = WEBAPP_LOCATION "finish.json";	/* LOCAL PATCH (2.1.4 C12) */
static const char http_watts_logo_url[] = WEBAPP_LOCATION "Watts_Logo.png";
static const char *const http_redirect_url = (sizeof(WEBAPP_LOCATION) == 2) ?
		"http://" DEFAULT_AP_IP : "http://" DEFAULT_AP_IP WEBAPP_LOCATION;

/* LOCAL PATCH (2.1.4 C2g): the room for a request's Host header, terminator included. A longer
 * Host cannot be the AP's or the STA's address (with a port): it gets the redirect. */
#define HTTP_HOST_BUF_SIZE			64

/* LOCAL PATCH (2.1.4 C6): the server's bounds (plan 6.2, section 8's E2 laptop flood).
 * - HTTP_APP_MAX_OPEN_SOCKETS sessions at once (was 10), the least recently used one closed for a
 *   new one (lru_purge_enable). PROVISIONAL: plan 6.3 sets 5 for the release candidate, and the
 *   G-CNA S1 run after WP4 decides between 4, 5 and 7 (G1 re-checks page p90 at that value).
 * - A new session is closed at once while internal DMA-capable heap is below
 *   HTTP_APP_SESSION_MIN_FREE (http_app_open_fn()).
 * - A session whose request named another host gets its 302 and is closed (no keep-alive for
 *   the OS's captive probes, which open a new connection to the portal anyway). */
#define HTTP_APP_MAX_OPEN_SOCKETS	5
#define HTTP_APP_SESSION_MIN_FREE	(12 * 1024)
/* a refused session is logged at most once in this long (with the count since the server start) */
#define HTTP_APP_REFUSE_LOG_MS		10000
/* GET /ap.json copies the network list out only while the largest free block exceeds the copy by
 * this much (wifi_manager's WIFI_MANAGER_HEAP_MARGIN, 2.1.4 WP1): an allocation that can do
 * without is not tried at low heap, where each failure would count in MONITOR's allocfail */
#define HTTP_APP_COPY_MARGIN		4096

/* LOCAL PATCH (2.1.4 C8): the room for an X-Custom-* credential header's value, terminator
 * included: a 64-byte password percent-encoded is 192 characters. A longer value gets 400. */
#define HTTP_APP_CRED_HDR_SIZE		(3 * MAX_PASSWORD_SIZE + 1)

/* LOCAL PATCH (2.1.4 C6): refused sessions since the server start, and when the last W line was
 * printed (httpd task only) */
static uint32_t http_app_refused = 0;
static TickType_t http_app_refuse_log_tick = 0;

/**
 * @brief embedded binary data.
 * @see https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#embedding-binary-data
 * LOCAL PATCH (2.1.4 C7): the text assets are gzipped at build time (CMakeLists.txt,
 * tools/gz_asset.py: deterministic, and the build fails rather than embed a stale one) and sent
 * as they are embedded, with Content-Encoding: gzip: 18.5 KB on the air for the page, where it
 * was 57.4 KB, and each text asset fits one TCP send buffer. The PNG is embedded as it is.
 */
extern const uint8_t style_css_gz_start[] asm("_binary_style_css_gz_start");
extern const uint8_t style_css_gz_end[]   asm("_binary_style_css_gz_end");
extern const uint8_t code_js_gz_start[] asm("_binary_code_js_gz_start");
extern const uint8_t code_js_gz_end[] asm("_binary_code_js_gz_end");
extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");
extern const uint8_t watts_logo_png_start[] asm("_binary_Watts_Logo_png_start");
extern const uint8_t watts_logo_png_end[] asm("_binary_Watts_Logo_png_end");


/* const httpd related values stored in ROM */
const static char http_200_hdr[] = "200 OK";
const static char http_302_hdr[] = "302 Found";
const static char http_400_hdr[] = "400 Bad Request";
const static char http_403_hdr[] = "403 Forbidden";
const static char http_404_hdr[] = "404 Not Found";
const static char http_406_hdr[] = "406 Not Acceptable";
const static char http_409_hdr[] = "409 Conflict";
const static char http_503_hdr[] = "503 Service Unavailable";
const static char http_location_hdr[] = "Location";
const static char http_content_type_html[] = "text/html";
const static char http_content_type_js[] = "text/javascript";
const static char http_content_type_css[] = "text/css";
const static char http_content_type_json[] = "application/json";
const static char http_content_type_png[] = "image/png";
/* LOCAL PATCH (2.1.4 C7): every answer is "no-store" (plan 6.3): httpd has no validators, and the
 * whole page is 18.5 KB, so nothing is cached, and a re-opened sign-in window always gets the
 * page and the status of now. */
const static char http_cache_control_hdr[] = "Cache-Control";
const static char http_cache_control_no_store[] = "no-store";
const static char http_content_encoding_hdr[] = "Content-Encoding";
const static char http_content_encoding_gzip[] = "gzip";
const static char http_vary_hdr[] = "Vary";
const static char http_vary_accept_encoding[] = "Accept-Encoding";
const static char http_connection_hdr[] = "Connection";
const static char http_connection_close[] = "close";

/* LOCAL PATCH (2.1.4 C7): the page's assets, matched on the request's path only (a query such as
 * "?v=2" is ignored) */
typedef struct {
	const char *path;
	const uint8_t *start;
	const uint8_t *end;
	const char *type;
	bool gzip;			/* embedded gzipped: sent with Content-Encoding: gzip */
} http_app_asset_t;

static const http_app_asset_t http_app_assets[] = {
	{ http_root_url,		index_html_gz_start,	index_html_gz_end,		http_content_type_html,	true },
	{ http_js_url,			code_js_gz_start,		code_js_gz_end,			http_content_type_js,	true },
	{ http_css_url,			style_css_gz_start,		style_css_gz_end,		http_content_type_css,	true },
	{ http_watts_logo_url,	watts_logo_png_start,	watts_logo_png_end,		http_content_type_png,	false },
};

/* LOCAL PATCH (2.1.4 C7): the room for a request's Accept-Encoding header, terminator included.
 * A longer one is taken to accept gzip: it lists many codings, and every browser engine the
 * portal serves names gzip. */
#define HTTP_ACCEPT_ENCODING_BUF_SIZE	96



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

/* LOCAL PATCH (2.1.4 C12): the Finish hook */
void http_app_set_finish_hook(http_app_finish_hook_t hook){
	finish_hook = hook;
}

void http_app_note_activity(http_app_activity_t kind, uint32_t client_ip){
	http_app_activity_hook_t hook = activity_hook;
	if(hook){
		hook(kind, client_ip);
	}
}

/**
 * @brief LOCAL PATCH (2.1.4 C7): the request's URI is the path, with nothing after it but a query
 * ("?...") or a fragment ("#..."), which are not matched.
 */
static bool http_app_path_is(const char *uri, const char *path){
	size_t n = strlen(path);
	return strncmp(uri, path, n) == 0 && (uri[n] == '\0' || uri[n] == '?' || uri[n] == '#');
}

/**
 * @brief LOCAL PATCH (2.1.4 C7): sends the answer with its Content-Length: the body for GET (and
 * every other method), the headers only for HEAD (httpd itself sends the body for HEAD too).
 */
static esp_err_t http_app_send(httpd_req_t *req, const char *buf, size_t len){
	return httpd_resp_send(req, (req->method == HTTP_HEAD) ? NULL : buf, (ssize_t)len);
}

/**
 * @brief LOCAL PATCH (2.1.4 C10b): the request's query has the parameter param ("bg=1"): after
 * the "?", between "&"s. The page marks the requests it makes on its own (its polls) with "bg=1",
 * so the activity hook can tell them from the user's (http_app_activity_t).
 */
static bool http_app_query_has(const char *uri, const char *param){
	const char *q = strchr(uri, '?');
	size_t n = strlen(param);
	while(q != NULL){
		q++;
		if(strncmp(q, param, n) == 0 && (q[n] == '\0' || q[n] == '&' || q[n] == '#')){
			return true;
		}
		q = strchr(q, '&');
	}
	return false;
}

/**
 * @brief LOCAL PATCH (2.1.4 C7, plan 6.3): the client takes a gzipped answer: its Accept-Encoding
 * header is absent (RFC 9110: then any coding is acceptable), or it lists gzip, x-gzip or "*"
 * with a q-value other than 0. False only for an explicit refusal, an identity-only or empty
 * list, which gets 406. Codings are compared case-insensitively.
 */
static bool http_app_gzip_accepted(httpd_req_t *req){

	char ae[HTTP_ACCEPT_ENCODING_BUF_SIZE];
	size_t len = httpd_req_get_hdr_value_len(req, "Accept-Encoding");
	if(len >= sizeof(ae)){
		return true;
	}
	if(httpd_req_get_hdr_value_str(req, "Accept-Encoding", ae, sizeof(ae)) != ESP_OK){
		return true;	/* absent */
	}

	const char *p = ae;
	while(*p){
		/* one coding: its name, then its parameters up to the next comma */
		while(*p == ' ' || *p == '\t' || *p == ','){
			p++;
		}
		const char *name = p;
		while(*p && *p != ';' && *p != ',' && *p != ' ' && *p != '\t'){
			p++;
		}
		size_t name_len = (size_t)(p - name);
		bool gzip = (name_len == 4 && strncasecmp(name, "gzip", 4) == 0) ||
				(name_len == 6 && strncasecmp(name, "x-gzip", 6) == 0) ||
				(name_len == 1 && name[0] == '*');
		bool q_zero = false;
		while(*p && *p != ','){
			if((*p == 'q' || *p == 'Q') && p[1] == '='){
				/* q=0, q=0., q=0.0, q=0.00 or q=0.000 refuses the coding */
				const char *q = p + 2;
				if(*q == '0'){
					q++;
					if(*q == '.'){
						q++;
						while(*q == '0'){
							q++;
						}
					}
					q_zero = (*q < '0' || *q > '9');
				}
				p = q;
				continue;
			}
			p++;
		}
		if(gzip && !q_zero){
			return true;
		}
	}
	return false;
}

/**
 * @brief LOCAL PATCH (2.1.4 C7): serves one of the page's assets (GET or HEAD).
 */
static esp_err_t http_app_send_asset(httpd_req_t *req, const http_app_asset_t *asset){

	if(asset->gzip){
		if(!http_app_gzip_accepted(req)){
			httpd_resp_set_status(req, http_406_hdr);
			return httpd_resp_send(req, NULL, 0);
		}
		httpd_resp_set_hdr(req, http_content_encoding_hdr, http_content_encoding_gzip);
		httpd_resp_set_hdr(req, http_vary_hdr, http_vary_accept_encoding);
	}
	httpd_resp_set_status(req, http_200_hdr);
	httpd_resp_set_type(req, asset->type);
	return http_app_send(req, (const char*)asset->start, (size_t)(asset->end - asset->start));
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
 * @brief LOCAL PATCH (2.1.4 C3, plan D3): the socket fd is connected to the SoftAP's own address
 * (DEFAULT_AP_IP) from a client in the SoftAP's subnet (DEFAULT_AP_NETMASK), both IPv4 or
 * IPv4-mapped; a native IPv6 address on either side fails. *client_ip is the client's IPv4
 * address in network byte order, 0 if unknown (the activity hook's). The local address alone
 * would not do: lwIP takes a packet for any of the hub's addresses on any interface, so a
 * home-LAN host with a route to the SoftAP's subnet through the hub's STA address reaches
 * 10.10.0.1:80 while the SoftAP is up (HANDOFF 15h, WP1 risk 8). Used for each new session
 * (http_app_open_fn()) and again for each request (http_app_on_ap()).
 */
static bool http_app_fd_on_ap(int fd, uint32_t *client_ip){

	http_app_sockaddr_t local, peer;
	socklen_t len;
	struct in_addr ap_ip, ap_mask;

	*client_ip = 0;
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

/* the request's session is the SoftAP's (http_app_fd_on_ap()) */
static bool http_app_on_ap(httpd_req_t *req, uint32_t *client_ip){
	*client_ip = 0;
	int fd = httpd_req_to_sockfd(req);
	return fd >= 0 && http_app_fd_on_ap(fd, client_ip);
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


/**
 * @brief LOCAL PATCH (2.1.4 C8): fills n bytes with zeros with stores the compiler keeps (the
 * password's copies on the stack; a memset() of a buffer about to go out of scope may be removed).
 */
static void http_app_wipe(void *p, size_t n){
	volatile uint8_t *v = (volatile uint8_t*)p;
	while(n--){
		*v++ = 0;
	}
}

static int http_app_hex(char c){
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/**
 * @brief LOCAL PATCH (2.1.4 C8): reads the credential header field into out (at most out_max
 * bytes) and its length into *out_len (0 when the header is absent or empty). With pct (the page's
 * "X-Custom-enc: pct") the value is percent-decoded: "%XX" is the byte XX, every other character
 * is itself ("+" included); without it the value is taken as it is (an older page's raw header,
 * whose spaces at either end HTTP itself drops). The length is checked after decoding. tmp
 * (HTTP_APP_CRED_HDR_SIZE) holds the value meanwhile; the caller wipes it.
 * @return NULL, or the reason for the 400: "enc" (a "%" without two hex digits after it), or what
 * (a value too long, or one holding a NUL byte).
 */
static const char *http_app_read_cred(httpd_req_t *req, const char *field, bool pct, char *tmp,
		uint8_t *out, size_t out_max, size_t *out_len, const char *what){

	*out_len = 0;
	size_t len = httpd_req_get_hdr_value_len(req, field);
	if(len == 0){
		return NULL;
	}
	if(len >= HTTP_APP_CRED_HDR_SIZE || httpd_req_get_hdr_value_str(req, field, tmp, HTTP_APP_CRED_HDR_SIZE) != ESP_OK){
		return what;
	}

	size_t o = 0;
	for(size_t i = 0; i < len; i++){
		int byte = (unsigned char)tmp[i];
		if(pct && byte == '%'){
			/* tmp ends with its terminator at len: a "%" at the end reads it, never past it */
			int hi = http_app_hex(tmp[i + 1]);
			int lo = (hi >= 0) ? http_app_hex(tmp[i + 2]) : -1;
			if(hi < 0 || lo < 0){
				return "enc";
			}
			byte = (hi << 4) | lo;
			i += 2;
		}
		if(byte == 0 || o >= out_max){
			return what;
		}
		out[o++] = (uint8_t)byte;
	}
	*out_len = o;
	return NULL;
}

/**
 * @brief LOCAL PATCH (2.1.4 C8): POST /connect.json, the page's Connect: the intake of a candidate.
 * Headers: X-Custom-ssid (1-32 bytes, no control character: no byte below 0x20, nor 0x7F, which
 * would reach the UART log raw), X-Custom-pwd (0-64 bytes: empty or absent for an open
 * network; 64 bytes only as 64 hex digits, a WPA PSK), X-Custom-enc: pct (both percent-encoded;
 * absent: both raw, as before), X-Custom-chan (the network's channel, 1-14, a hint only; anything
 * else is ignored). Lengths are checked after decoding. 400 with {"err":"ssid"|"pwd"|"enc"} for a
 * bad request, 503 when wifi_manager's queue has no room (C6), 200 once the candidate is queued:
 * status.json then shows it in "pend" until it is decided. The network in use changes only when
 * the candidate gets an IP (wifi_manager_connect_user_async()). In a frame of its own; the
 * password's copies are wiped before it returns. Never logs a credential (C1).
 */
static __attribute__((noinline)) esp_err_t http_app_post_connect(httpd_req_t *req){

	char tmp[HTTP_APP_CRED_HDR_SIZE];
	uint8_t ssid[MAX_SSID_SIZE];
	uint8_t pwd[MAX_PASSWORD_SIZE];
	size_t ssid_len = 0, pwd_len = 0;
	const char *err = NULL;
	bool pct = false;

	/* the encoding flag: "pct", or absent */
	size_t enc_len = httpd_req_get_hdr_value_len(req, "X-Custom-enc");
	if(enc_len){
		pct = enc_len < sizeof(tmp) && httpd_req_get_hdr_value_str(req, "X-Custom-enc", tmp, sizeof(tmp)) == ESP_OK &&
				strcasecmp(tmp, "pct") == 0;
		if(!pct){
			err = "enc";
		}
	}
	if(err == NULL){
		err = http_app_read_cred(req, "X-Custom-ssid", pct, tmp, ssid, sizeof(ssid), &ssid_len, "ssid");
	}
	if(err == NULL && ssid_len == 0){
		err = "ssid";
	}
	for(size_t i = 0; err == NULL && i < ssid_len; i++){
		if(ssid[i] < 0x20 || ssid[i] == 0x7F){
			err = "ssid";
		}
	}
	if(err == NULL){
		err = http_app_read_cred(req, "X-Custom-pwd", pct, tmp, pwd, sizeof(pwd), &pwd_len, "pwd");
	}
	if(err == NULL && pwd_len == MAX_PASSWORD_SIZE){
		for(size_t i = 0; i < pwd_len; i++){
			if(http_app_hex((char)pwd[i]) < 0){
				err = "pwd";
				break;
			}
		}
	}

	/* the channel hint */
	uint8_t chan = 0;
	size_t chan_len = httpd_req_get_hdr_value_len(req, "X-Custom-chan");
	if(err == NULL && chan_len > 0 && chan_len <= 2 && httpd_req_get_hdr_value_str(req, "X-Custom-chan", tmp, sizeof(tmp)) == ESP_OK){
		int c = 0;
		for(size_t i = 0; i < chan_len && tmp[i] >= '0' && tmp[i] <= '9'; i++){
			c = c * 10 + (tmp[i] - '0');
		}
		chan = (c >= 1 && c <= 14) ? (uint8_t)c : 0;
	}
	http_app_wipe(tmp, sizeof(tmp));

	if(err != NULL){
		char body[24];
		int n = snprintf(body, sizeof(body), "{\"err\":\"%s\"}", err);
		ESP_LOGW(TAG, "POST connect.json refused (400): %s", err);
		httpd_resp_set_status(req, http_400_hdr);
		httpd_resp_set_type(req, http_content_type_json);
		httpd_resp_send(req, body, (n > 0 && (size_t)n < sizeof(body)) ? n : 0);
	}
	else{
		/* LOCAL PATCH (2.1.4 C1): no credential in the log, the password's length only */
		ESP_LOGI(TAG, "ssid: %.*s, pwd_len: %u, chan: %u", (int)ssid_len, (const char*)ssid, (unsigned)pwd_len, (unsigned)chan);
		bool queued = wifi_manager_connect_user_async(ssid, ssid_len, pwd, pwd_len, chan);
		httpd_resp_set_status(req, queued ? http_200_hdr : http_503_hdr);
		httpd_resp_set_type(req, http_content_type_json);
		httpd_resp_send(req, NULL, 0);
	}

	http_app_wipe(pwd, sizeof(pwd));
	return ESP_OK;
}

/**
 * @brief LOCAL PATCH (2.1.4 C8, C6): GET /status.json: formatted on this task's stack from a copy
 * taken under the status's own lock, and sent with no lock held (wifi_manager_status_json()).
 */
static __attribute__((noinline)) esp_err_t http_app_send_status(httpd_req_t *req){
	char buf[WIFI_MANAGER_STATUS_JSON_SIZE];
	size_t len = wifi_manager_status_json(buf, sizeof(buf));
	httpd_resp_set_status(req, http_200_hdr);
	httpd_resp_set_type(req, http_content_type_json);
	return http_app_send(req, buf, len);
}


static esp_err_t http_server_delete_handler(httpd_req_t *req){

	/* LOCAL PATCH (2.1.4 C3): only for a client on the SoftAP (the portal's forget, D9, is kept).
	 * The request line after the check: a refused request logs at DEBUG only */
	uint32_t client_ip;
	if(!http_app_on_ap(req, &client_ip)){
		return http_app_refuse(req);
	}

	ESP_LOGI(TAG, "DELETE %s", req->uri);
	httpd_resp_set_hdr(req, http_cache_control_hdr, http_cache_control_no_store);	/* LOCAL PATCH (2.1.4 C7) */

	/* DELETE /connect.json */
	if(http_app_path_is(req->uri, http_connect_url)){
		http_app_note_activity(HTTP_APP_ACT_API_USER, client_ip); /* LOCAL PATCH (2.1.4 C10a) */

		/* LOCAL PATCH (2.1.4 C6): 503 when wifi_manager's queue has no room within its bound */
		httpd_resp_set_status(req, wifi_manager_disconnect_async() ? http_200_hdr : http_503_hdr);
		httpd_resp_set_type(req, http_content_type_json);
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

	/* LOCAL PATCH (2.1.4 C3): only for a client on the SoftAP; a user hook's URIs too. The
	 * request line after the check: a refused request logs at DEBUG only */
	uint32_t client_ip;
	if(!http_app_on_ap(req, &client_ip)){
		return http_app_refuse(req);
	}

	ESP_LOGI(TAG, "POST %s", req->uri);
	httpd_resp_set_hdr(req, http_cache_control_hdr, http_cache_control_no_store);	/* LOCAL PATCH (2.1.4 C7) */

	/* POST /connect.json */
	if(http_app_path_is(req->uri, http_connect_url)){

		http_app_note_activity(HTTP_APP_ACT_API_USER, client_ip); /* LOCAL PATCH (2.1.4 C10a) */

		/* LOCAL PATCH (2.1.4 C8): the candidate's intake (it wrote the network in use directly,
		 * with no decoding, no open network and a dead-store memset of its password copy) */
		ret = http_app_post_connect(req);
	}
	/* POST /scan.json: LOCAL PATCH (2.1.4 C10b), the page's Rescan. 200 with {"scan":1} when a scan
	 * was ordered, {"scan":0,"in":S} when the last was less than 20 s ago (S: seconds to wait),
	 * 503 when wifi_manager's queue had no room. The page then reads GET /ap.json again. */
	else if(http_app_path_is(req->uri, http_scan_url)){
		http_app_note_activity(HTTP_APP_ACT_API_USER, client_ip);
		uint32_t wait_ms = 0;
		int r = wifi_manager_scan_request(true, &wait_ms);
		if(r < 0){
			httpd_resp_set_status(req, http_503_hdr);
			httpd_resp_send(req, NULL, 0);
		}
		else{
			char body[32];
			int n = (r > 0) ? snprintf(body, sizeof(body), "{\"scan\":1}") :
					snprintf(body, sizeof(body), "{\"scan\":0,\"in\":%lu}", (unsigned long)((wait_ms + 999) / 1000));
			httpd_resp_set_status(req, http_200_hdr);
			httpd_resp_set_type(req, http_content_type_json);
			httpd_resp_send(req, body, (n > 0 && (size_t)n < sizeof(body)) ? n : 0);
		}
	}
	/* POST /finish.json: LOCAL PATCH (2.1.4 C12), the page's Finish. Only with the STA connected:
	 * the Finish hook (the app's AP-tail policy) sets the SoftAP's stop, about 2 s on, and 200
	 * {"finish":1} goes out before it; 409 {"err":"not connected"} otherwise (the hook said no, or
	 * with no hook the stop could not be set). The sign-in windows of iOS and Android close when
	 * the SoftAP goes. */
	else if(http_app_path_is(req->uri, http_finish_url)){
		http_app_note_activity(HTTP_APP_ACT_API_USER, client_ip);
		http_app_finish_hook_t hook = finish_hook;
		bool done = (hook != NULL) ? hook() : wifi_manager_ap_stop_in(HTTP_APP_FINISH_STOP_MS);
		static const char finished[] = "{\"finish\":1}";
		static const char not_connected[] = "{\"err\":\"not connected\"}";
		httpd_resp_set_status(req, done ? http_200_hdr : http_409_hdr);
		httpd_resp_set_type(req, http_content_type_json);
		httpd_resp_send(req, done ? finished : not_connected, HTTPD_RESP_USE_STRLEN);
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

    httpd_resp_set_hdr(req, http_cache_control_hdr, http_cache_control_no_store);	/* LOCAL PATCH (2.1.4 C7) */

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
	if(has_host && wifi_manager_lock_sta_ip_string(pdMS_TO_TICKS(WIFI_MANAGER_POST_WAIT_MS))){	/* LOCAL PATCH (2.1.4 C6): bounded */
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
		/* LOCAL PATCH (2.1.4 C6): the session is closed after its 302 (said so, then done by the
		 * server task once this handler has returned and the answer is out) */
		httpd_resp_set_hdr(req, http_connection_hdr, http_connection_close);
		httpd_resp_send(req, NULL, 0);
		httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));

	}
	else{

		/* LOCAL PATCH (2.1.4 C7): GET / and the page's assets, from the table */
		const http_app_asset_t *asset = NULL;
		for(size_t i = 0; i < sizeof(http_app_assets) / sizeof(http_app_assets[0]); i++){
			if(http_app_path_is(req->uri, http_app_assets[i].path)){
				asset = &http_app_assets[i];
				break;
			}
		}

		if(asset != NULL){
			http_app_note_activity(HTTP_APP_ACT_PAGE, client_ip);
			http_app_send_asset(req, asset);
		}
		/* GET /ap.json */
		else if(http_app_path_is(req->uri, http_ap_url)){

			/* LOCAL PATCH (2.1.4 C10b): the page's own polls carry "bg=1". Its first read, once
			 * the page has loaded (not GET /, whose assets then had the radio to themselves), orders
			 * a scan when the list is empty or stale and none was ordered in the last 20 s */
			bool bg = http_app_query_has(req->uri, "bg=1");
			http_app_note_activity(bg ? HTTP_APP_ACT_API_BG : HTTP_APP_ACT_API_USER, client_ip);
			if(!bg && req->method == HTTP_GET){
				wifi_manager_scan_request(false, NULL);
			}

			/* if we can get the mutex, write the last version of the AP list */
			if(wifi_manager_lock_json_buffer(( TickType_t ) 10)){

				httpd_resp_set_status(req, http_200_hdr);
				httpd_resp_set_type(req, http_content_type_json);
				/* LOCAL PATCH (2.1.4 C2b): no list while the AP is down: an empty one */
				const char* ap_buf = wifi_manager_get_ap_list_json();
				if(ap_buf == NULL){
					ap_buf = "[]\n";
				}
				/* LOCAL PATCH (2.1.4 C6): copied out, and sent with the lock given back: a send to a
				 * slow phone (up to send_wait_timeout, 4 s) held it, and the wifi_manager task, which
				 * waits 1 s for it, dropped a fresh scan. Sent under the lock as before only when
				 * the heap has no room for the copy (about 1.5 KB, freed at once): the copy is tried
				 * only with HTTP_APP_COPY_MARGIN to spare, so a low heap (the E2 flood) sees no
				 * failed allocation per poll */
				size_t ap_len = strlen(ap_buf);
				char *copy = NULL;
				if(heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT) >= ap_len + 1 + HTTP_APP_COPY_MARGIN){
					copy = malloc(ap_len + 1);
				}
				if(copy != NULL){
					memcpy(copy, ap_buf, ap_len + 1);
					wifi_manager_unlock_json_buffer();
					http_app_send(req, copy, ap_len);	/* LOCAL PATCH (2.1.4 C7): HEAD aware */
					free(copy);
				}
				else{
					http_app_send(req, ap_buf, ap_len);
					wifi_manager_unlock_json_buffer();
				}
			}
			else{
				httpd_resp_set_status(req, http_503_hdr);
				httpd_resp_send(req, NULL, 0);
				ESP_LOGE(TAG, "http_server_netconn_serve: GET /ap.json failed to obtain mutex");
			}

			/* LOCAL PATCH (2.1.4 C10b): no scan here any more. It ordered an all-channel scan at
			 * every poll (each one off the SoftAP's channel, plan I7): the list is a cache, filled
			 * by the page's load and its Rescan (POST /scan.json) */
		}
		/* GET /status.json */
		else if(http_app_path_is(req->uri, http_status_url)){

			http_app_note_activity(HTTP_APP_ACT_STATUS, client_ip);

			/* LOCAL PATCH (2.1.4 C8, C6): its own lock, not the network list's: never a 503 for a
			 * list being rebuilt, and no lock held while it is sent */
			ret = http_app_send_status(req);
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
 * Delegates to the GET handler. LOCAL PATCH (2.1.4 C7): httpd does not leave the body out for HEAD
 * (httpd_resp_send() sends whatever it is given), so on a kept-alive connection the client read it
 * as the start of the next answer: the GET handler now sends HEAD the headers only, with the
 * Content-Length a GET gets (http_app_send()).
 */
static const httpd_uri_t http_server_head_request = {
	.uri	= "*",
	.method = HTTP_HEAD,
	.handler = http_server_get_handler
};


/**
 * @brief LOCAL PATCH (2.1.4 C6): httpd's open_fn, for each new session (httpd task). A session
 * that is not the SoftAP's (http_app_fd_on_ap(): a home-LAN host on the STA's address, say) is
 * shut down (shutdown(SHUT_RDWR)) before any of its request is read: such a host could otherwise
 * trickle its headers or a long body, and hold the one httpd task, and with it the wifi_manager
 * task in STOP_AP's httpd_stop(), for as long as it liked (each handler's 403 came only after the
 * whole request was read). Logged at DEBUG only, as that 403, so a LAN scan cannot fill the log.
 * Below HTTP_APP_SESSION_MIN_FREE of internal DMA-capable heap a SoftAP session is shut down the
 * same way, so a flood of connections (the E2 laptop) cannot take the heap the Wi-Fi driver
 * needs. A shut-down socket fails its first read, and httpd deletes the session itself on its
 * next pass, before any request is served. Always ESP_OK, whatever shutdown() returns (it fails
 * for a peer that has reset already, whose first read then fails all the same): httpd closes a
 * session whose open_fn fails twice (httpd_sess_new(), then httpd_accept_conn()), and a socket
 * another task opened between the two closes would be the second one's.
 * httpd_sess_trigger_close() would not do: httpd skips the close of a session that has served no
 * request yet (lru_counter 0, "race condition").
 */
static esp_err_t http_app_open_fn(httpd_handle_t hd, int sockfd){

	uint32_t client_ip;
	if(!http_app_fd_on_ap(sockfd, &client_ip)){
		ESP_LOGD(TAG, "session closed at once: not to the SoftAP from its subnet");
		shutdown(sockfd, SHUT_RDWR);
		return ESP_OK;
	}

	size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
	if(free_dma >= HTTP_APP_SESSION_MIN_FREE){
		return ESP_OK;
	}

	http_app_refused++;
	TickType_t now = xTaskGetTickCount();
	if(http_app_refused == 1 || now - http_app_refuse_log_tick >= pdMS_TO_TICKS(HTTP_APP_REFUSE_LOG_MS)){
		http_app_refuse_log_tick = now;
		ESP_LOGW(TAG, "session closed: internal DMA free %u B (needs %u) - %lu since the server start",
				(unsigned)free_dma, (unsigned)HTTP_APP_SESSION_MIN_FREE, (unsigned long)http_app_refused);
	}
	shutdown(sockfd, SHUT_RDWR);
	return ESP_OK;
}


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
		 * shorten timeouts so stale connections are recycled quickly.
		 * LOCAL PATCH (2.1.4 C6): 5 sessions (was 10) and the heap gate on each new one */
		config.max_open_sockets = HTTP_APP_MAX_OPEN_SOCKETS;
		config.open_fn = http_app_open_fn;
		config.recv_wait_timeout = 4;
		config.send_wait_timeout = 4;
		http_app_refused = 0;

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
