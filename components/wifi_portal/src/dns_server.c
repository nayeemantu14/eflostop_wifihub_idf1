/*
Copyright (c) 2019 Tony Pottier

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

@file dns_server.c
@author Tony Pottier
@brief Defines an extremely basic DNS server for captive portal functionality.
It's basically a DNS hijack that replies to the esp's address no matter which
request is sent to it.

Contains the freeRTOS task for the DNS server that processes the requests.

LOCAL PATCH (2.1.4 C4): rewritten to the plan's responder specification
(docs/field_logs/2.1.4/RADIO_PORTAL_PLAN.md section 6.2a). The old responder rebooted the hub
on a datagram shorter than 12 B (a memcpy of length - 12 bytes) and when its socket or bind
failed (exit()), wrote one byte past its 80 B buffer, answered every type (AAAA, HTTPS, ...)
with an A record, appended that answer after an EDNS0 query's OPT record (a reply with bytes
no count covers), bound to the STA's address (0.0.0.0 while the STA had none, so it answered
on the home LAN too), and was killed from the wifi_manager task with vTaskDelete() before its
socket was closed.

@see https://idyl.io
@see https://github.com/tonyp7/esp32-wifi-manager
*/

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <lwip/sockets.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>

#include "wifi_manager.h"
#include "dns_server.h"
#include "http_app.h"

static const char TAG[] = "dns_server";

/* The task: no heap of its own beyond its stack, both buffers below on that stack */
#define DNS_TASK_STACK			3072
#define DNS_RCV_TIMEOUT_MS		500		/* SO_RCVTIMEO: how often the task checks its run flag */
#define DNS_RETRY_MS			1000	/* socket() or bind() failed: tried again after this */
#define DNS_RETRY_STEP_MS		100		/* that wait's steps, each followed by a run flag check */
#define DNS_STOP_WAIT_MS		1000	/* dns_server_stop() waits at most this for the task to end */

/* What is answered (plan section 6.2a) */
#define DNS_RX_BUF_SIZE			300		/* a datagram this long may have been cut short: dropped */
#define DNS_HDR_LEN				12
#define DNS_MIN_QUERY			17		/* the header, the root name (1), QTYPE and QCLASS (4) */
#define DNS_LABEL_MAX			63		/* a larger length byte is a compression pointer (0xC0) or reserved */
#define DNS_NAME_MAX			255		/* the labels with their length bytes, the root's zero not counted */
#define DNS_ANSWER_LEN			16
#define DNS_OPT_LEN				11
#define DNS_MIN_IDMA_FREE		(10 * 1024)	/* internal DMA-capable heap below this: no reply */
#define DNS_MAX_REPLIES_PER_S	20

/* the longest reply: the header, the longest question the name walk lets through (the name, its
 * root zero, QTYPE and QCLASS), one answer and the OPT record: 299 B */
#define DNS_TX_BUF_SIZE			(DNS_HDR_LEN + DNS_NAME_MAX + 1 + 4 + DNS_ANSWER_LEN + DNS_OPT_LEN)

/* RFC 1035 and RFC 6891 values */
#define DNS_RCODE_FORMERR		1
#define DNS_RCODE_NOTIMP		4
#define DNS_TYPE_A				1
#define DNS_TYPE_OPT			41
#define DNS_QTYPE_ANY			255
#define DNS_CLASS_IN			1
#define DNS_QCLASS_ANY			255
#define DNS_TTL_S				60
#define DNS_EDNS_UDP_SIZE		512

_Static_assert(DNS_TX_BUF_SIZE == 299, "the reply buffer holds the longest reply");
_Static_assert(DNS_TTL_S <= 0xFF, "the TTL is written as one byte");

/* The answer to an A or ANY query, the SoftAP's address after it: NAME (a pointer to the
 * question's name, offset 12), TYPE A, CLASS IN, TTL 60 s, RDLENGTH 4 */
static const uint8_t dns_answer_head[DNS_ANSWER_LEN - 4] = {
	0xC0, DNS_HDR_LEN, 0x00, DNS_TYPE_A, 0x00, DNS_CLASS_IN, 0x00, 0x00, 0x00, DNS_TTL_S, 0x00, 0x04
};

/* The minimal OPT record, for a query that carried one: the root name, TYPE OPT, CLASS (the UDP
 * payload size) 512, TTL (extended RCODE, version and flags, DO included) 0, RDLENGTH 0 */
static const uint8_t dns_opt[DNS_OPT_LEN] = {
	0x00, 0x00, DNS_TYPE_OPT, DNS_EDNS_UDP_SIZE >> 8, DNS_EDNS_UDP_SIZE & 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* The task's run flag: dns_server_start() sets it, dns_server_stop() clears it, and the task
 * serves while it is set. wifi_manager task writes, the DNS task reads. */
static volatile bool dns_run = false;

/* A DNS task exists: dns_server_start() sets it just before creating the task, and the task
 * clears it as its last act, once its socket is closed (a new task can then bind the port). */
static volatile bool dns_task_alive = false;


static inline uint16_t dns_rd16(const uint8_t *p){
	return (uint16_t)((p[0] << 8) | p[1]);
}

/**
 * @brief Writes the reply to the query q, n bytes long (DNS_MIN_QUERY <= n < DNS_RX_BUF_SIZE,
 * QR clear: the caller's drops), into r (DNS_TX_BUF_SIZE bytes) and returns its length. Reads q
 * only below n. ap_ip is the SoftAP's IPv4 address in network byte order.
 *
 * Every reply: the query's ID; QR, AA, RD copied, OPCODE 0, TC 0; RA, Z 0, AD 0, CD copied.
 * - OPCODE not 0: NOTIMP, the header only (all counts 0).
 * - QDCOUNT not 1, or ANCOUNT or NSCOUNT not 0: FORMERR, the header only.
 * - A question name that fails the walk (a label over 63 bytes, which includes compression
 *   pointers, over 255 bytes in all, or past the datagram's end), or a question cut short
 *   before its QTYPE and QCLASS: FORMERR, the header only.
 * - Otherwise NOERROR: the header, the question copied exactly and nothing after it from the
 *   query; for QTYPE A or ANY with QCLASS IN or ANY, one A answer (TTL 60 s); every other
 *   type (AAAA, HTTPS, SVCB, PTR, ...) no answer; if the query's first additional record,
 *   right after the question, is an OPT record, the minimal one (ARCOUNT 1).
 */
static int dns_build_reply(const uint8_t *q, int n, uint8_t *r, uint32_t ap_ip){

	const uint16_t qdcount = dns_rd16(&q[4]);
	const uint16_t ancount = dns_rd16(&q[6]);
	const uint16_t nscount = dns_rd16(&q[8]);
	const uint16_t arcount = dns_rd16(&q[10]);
	int p = DNS_HDR_LEN;	/* the name walk's position in q */
	int name_len = 0;		/* the labels' bytes so far, with their length bytes */

	/* the header, counts 0 */
	r[0] = q[0];
	r[1] = q[1];
	r[2] = 0x80 | 0x04 | (q[2] & 0x01);	/* QR, AA, RD copied */
	r[3] = 0x80 | (q[3] & 0x10);		/* RA, CD copied, RCODE NOERROR */
	memset(&r[4], 0x00, DNS_HDR_LEN - 4);

	if(((q[2] >> 3) & 0x0F) != 0){
		r[3] |= DNS_RCODE_NOTIMP;
		return DNS_HDR_LEN;
	}
	if(qdcount != 1 || ancount != 0 || nscount != 0){
		goto formerr;
	}

	/* the question's name: every step moves on at least 2 bytes, inside the datagram */
	for(;;){
		if(p >= n){
			goto formerr;
		}
		uint8_t label = q[p];
		if(label == 0){
			p++;
			break;
		}
		if(label > DNS_LABEL_MAX || p + 1 + label > n){
			goto formerr;
		}
		name_len += 1 + label;
		p += 1 + label;
		if(name_len > DNS_NAME_MAX){
			goto formerr;
		}
	}
	if(p + 4 > n){
		goto formerr;
	}

	const uint16_t qtype = dns_rd16(&q[p]);
	const uint16_t qclass = dns_rd16(&q[p + 2]);
	const int qend = p + 4;
	const bool edns = (arcount >= 1) && (qend + DNS_OPT_LEN <= n) && (q[qend] == 0) &&
			(dns_rd16(&q[qend + 1]) == DNS_TYPE_OPT);
	const bool answer = (qtype == DNS_TYPE_A || qtype == DNS_QTYPE_ANY) &&
			(qclass == DNS_CLASS_IN || qclass == DNS_QCLASS_ANY);
	int len = qend;

	r[5] = 1;					/* QDCOUNT */
	r[7] = answer ? 1 : 0;		/* ANCOUNT */
	r[11] = edns ? 1 : 0;		/* ARCOUNT */
	memcpy(&r[DNS_HDR_LEN], &q[DNS_HDR_LEN], qend - DNS_HDR_LEN);
	if(answer){
		memcpy(&r[len], dns_answer_head, sizeof(dns_answer_head));
		memcpy(&r[len + sizeof(dns_answer_head)], &ap_ip, 4);
		len += DNS_ANSWER_LEN;
	}
	if(edns){
		memcpy(&r[len], dns_opt, DNS_OPT_LEN);
		len += DNS_OPT_LEN;
	}
	return len;

formerr:
	r[3] |= DNS_RCODE_FORMERR;
	return DNS_HDR_LEN;
}

/**
 * @brief Builds and sends the reply to the query q. In a frame of its own, so the reply's buffer
 * is on the stack only while it is built and sent, not while the activity hook logs. Returns
 * sendto()'s result.
 */
static __attribute__((noinline)) int dns_send_reply(int fd, const uint8_t *q, int n,
		const struct sockaddr_in *client, uint32_t ap_ip){

	uint8_t r[DNS_TX_BUF_SIZE];
	int len = dns_build_reply(q, n, r, ap_ip);

	ESP_LOGD(TAG, "%d B query from 0x%08lx: %d B reply, rcode %u, %u answer", n,
			(unsigned long)client->sin_addr.s_addr, len, (unsigned)(r[3] & 0x0F), (unsigned)r[7]);
	return sendto(fd, r, len, 0, (const struct sockaddr *)client, sizeof(*client));
}

/**
 * @brief The socket: UDP, SO_REUSEADDR, SO_RCVTIMEO and bound to the SoftAP's address, port 53.
 * Returns it, or -1 after closing what was opened; the first failure of a task is logged
 * (*logged), the retries are not.
 */
static int dns_open(uint32_t ap_ip, bool *logged){

	const int reuse = 1;
	const struct timeval rcv_timeout = { .tv_sec = 0, .tv_usec = DNS_RCV_TIMEOUT_MS * 1000 };
	struct sockaddr_in local;
	const char *failed = NULL;

	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(fd < 0){
		failed = "socket()";
	}
	else if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0 ||
			setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv_timeout, sizeof(rcv_timeout)) != 0){
		/* without the timeout the task could not see its stop */
		failed = "setsockopt()";
	}
	else{
		memset(&local, 0x00, sizeof(local));
		local.sin_family = AF_INET;
		local.sin_port = htons(53);
		local.sin_addr.s_addr = ap_ip;
		if(bind(fd, (struct sockaddr *)&local, sizeof(local)) != 0){
			failed = "bind()";
		}
	}

	if(failed){
		if(!*logged){
			ESP_LOGW(TAG, "captive DNS: %s failed (errno %d) - trying again every %d ms", failed, errno, DNS_RETRY_MS);
			*logged = true;
		}
		if(fd >= 0){
			close(fd);
		}
		return -1;
	}
	return fd;
}

/**
 * @brief Serves queries on fd while the run flag is set.
 */
static void dns_serve(int fd, uint32_t ap_ip){

	uint8_t q[DNS_RX_BUF_SIZE];		/* written by recvfrom() only, never past what it received */
	struct sockaddr_in client;
	TickType_t window_start = xTaskGetTickCount();
	unsigned window_replies = 0;	/* replies sent since window_start */

	while(dns_run){

		socklen_t client_len = sizeof(client);
		int n = recvfrom(fd, q, sizeof(q), 0, (struct sockaddr *)&client, &client_len);
		if(n < 0){
			/* the timeout: the run flag is checked again. Any other error: a pause first, so a
			 * socket that keeps failing cannot keep this task busy */
			if(errno != EAGAIN && errno != EWOULDBLOCK){
				vTaskDelay(pdMS_TO_TICKS(DNS_RETRY_STEP_MS));
			}
			continue;
		}

		/* dropped without a reply: too short to be a query, maybe cut short, a response */
		if(n < DNS_MIN_QUERY || n >= DNS_RX_BUF_SIZE || (q[2] & 0x80)){
			ESP_LOGD(TAG, "%d B datagram dropped (length or QR)", n);
			continue;
		}
		/* dropped without a reply: low internal DMA-capable heap (every reply needs a TX buffer
		 * from it), or more than DNS_MAX_REPLIES_PER_S replies in this second */
		if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) < DNS_MIN_IDMA_FREE){
			ESP_LOGD(TAG, "%d B query dropped (internal DMA heap low)", n);
			continue;
		}
		TickType_t now = xTaskGetTickCount();
		if((now - window_start) >= pdMS_TO_TICKS(1000)){
			window_start = now;
			window_replies = 0;
		}
		if(window_replies >= DNS_MAX_REPLIES_PER_S){
			ESP_LOGD(TAG, "%d B query dropped (reply rate)", n);
			continue;
		}
		window_replies++;

		/* LOCAL PATCH (2.1.4 C10a): the activity hook sees each query answered, and its sender */
		http_app_note_activity(HTTP_APP_ACT_DNS, client.sin_addr.s_addr);

		int err = dns_send_reply(fd, q, n, &client, ap_ip);
		if(err < 0){
			ESP_LOGE(TAG, "UDP sendto failed: %d", err);
		}
	}
}

/**
 * @brief The DNS task: opens its socket (again every DNS_RETRY_MS until it can), serves until its
 * run flag is cleared, closes its socket and ends itself. No exit(), and nothing else deletes it.
 */
static void dns_server(void *pvParameters){

	(void)pvParameters;
	struct in_addr ap_addr = { 0 };
	bool open_failure_logged = false;
	int fd = -1;

	/* the SoftAP's address: where the hijack sends every name, and the only one it answers on */
	if(inet_pton(AF_INET, DEFAULT_AP_IP, &ap_addr) != 1){
		ESP_LOGE(TAG, "captive DNS: DEFAULT_AP_IP is not an IPv4 address - not started");
	}
	else{
		while(dns_run && fd < 0){
			fd = dns_open(ap_addr.s_addr, &open_failure_logged);
			for(int waited = 0; fd < 0 && dns_run && waited < DNS_RETRY_MS; waited += DNS_RETRY_STEP_MS){
				vTaskDelay(pdMS_TO_TICKS(DNS_RETRY_STEP_MS));
			}
		}
	}

	if(fd >= 0){
		ESP_LOGI(TAG, "DNS Server listening on 53/udp");
		dns_serve(fd, ap_addr.s_addr);
		close(fd);
	}

	/* the socket is closed: dns_server_start() may create the next task */
	dns_task_alive = false;
	vTaskDelete(NULL);
}

/* waits up to DNS_STOP_WAIT_MS for the task to end */
static void dns_server_wait_end(void){
	TickType_t start = xTaskGetTickCount();
	while(dns_task_alive && (xTaskGetTickCount() - start) < pdMS_TO_TICKS(DNS_STOP_WAIT_MS)){
		vTaskDelay(1);
	}
}

bool dns_server_start(){

	if(dns_task_alive){
		if(dns_run){
			return true;	/* it runs: START_AP with the AP already up */
		}
		/* a stop that did not see the task end: never two tasks on the port */
		dns_server_wait_end();
		if(dns_task_alive){
			ESP_LOGW(TAG, "captive DNS: the stopped task has not ended - not started");
			return false;
		}
	}

	dns_run = true;
	dns_task_alive = true;
	if(xTaskCreate(&dns_server, "dns_server", DNS_TASK_STACK, NULL, WIFI_MANAGER_TASK_PRIORITY-1, NULL) != pdPASS){
		dns_run = false;
		dns_task_alive = false;
		/* LOCAL PATCH (2.1.4 WP1): wifi_manager tries again while the AP is up */
		ESP_LOGE(TAG, "captive DNS: task not created (no memory)");
		return false;
	}
	return true;
}

void dns_server_stop(){

	if(!dns_task_alive){
		return;
	}
	dns_run = false;
	dns_server_wait_end();
	if(dns_task_alive){
		ESP_LOGW(TAG, "captive DNS: task still ending after %d ms - it ends by itself", DNS_STOP_WAIT_MS);
	}
}
