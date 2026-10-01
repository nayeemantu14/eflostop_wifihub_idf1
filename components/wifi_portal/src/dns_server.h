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

@file dns_server.h
@author Tony Pottier
@brief Defines an extremly basic DNS server for captive portal functionality.

Contains the freeRTOS task for the DNS server that processes the requests.

LOCAL PATCH (2.1.4 C4): the responder is rewritten (dns_server.c, plan section 6.2a). It
answers on the SoftAP's address only (DEFAULT_AP_IP, port 53), from START_AP to STOP_AP. The
old header's DNS structures and codes, used by nothing else, are gone with the old responder.

@see https://idyl.io
@see https://github.com/tonyp7/esp32-wifi-manager
@see http://www.zytrax.com/books/dns/ch15
*/

#ifndef MAIN_DNS_SERVER_H_
#define MAIN_DNS_SERVER_H_

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Starts the captive DNS task (the hijack: every A query is answered with the SoftAP's
 * address). LOCAL PATCH (2.1.4 C4): does nothing while the task runs, so START_AP can call it
 * with the AP already up. If a stop did not see the task end, it waits up to 1 s more for it,
 * and does not start a second one beside it. No reboot path: a task that cannot be created, or
 * a socket that cannot be opened or bound, is logged (the task retries the socket every 1 s).
 * LOCAL PATCH (2.1.4 WP1): returns whether a task runs after the call (its socket maybe still
 * being retried): false when it could not be created, or a stopped one has not ended, which
 * wifi_manager tries again while the AP is up. wifi_manager task only.
 */
bool dns_server_start();

/**
 * @brief Stops the captive DNS task. LOCAL PATCH (2.1.4 C4): clears its run flag, which the task
 * checks at least every 500 ms; the task closes its own socket and ends itself. Waits up to 1 s
 * for that. Does nothing when no task runs. wifi_manager task only.
 */
void dns_server_stop();


#ifdef __cplusplus
}
#endif


#endif /* MAIN_DNS_SERVER_H_ */
