#ifndef PORTAL_PRIORITY_H
#define PORTAL_PRIORITY_H
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief True while the portal priority window is open: the Wi-Fi setup (captive) portal is
 *        up and no Wi-Fi credentials are saved (first setup, or after the 10 s reset).
 *
 * BLE scanning is paused for the window so the SoftAP gets the radio: the leak scanner and
 * the valve hunt start nothing new and cancel their own scans and connect attempts, while
 * NimBLE stays initialised and a valve already linked stays linked. One exception: while a
 * leak response (RMLEAK or CLOSE) is pended for an unlinked valve, the valve hunt and its
 * connect run anyway, until the valve links and takes them (app_ble_valve.c). The window
 * opens at wifi_manager's START_AP, with no time cap before setup, and closes when the setup
 * AP stops: about 60 s after the STA gets its IP, so the phone can still load the portal's
 * success status. It closes earlier if the STA loses that Wi-Fi first, since the SoftAP then
 * stays up as a router-fallback portal. It is NOT opened for the fallback AP after failed
 * retries while credentials are still saved (router outage): BLE leak protection stays on
 * there. See app_wifi.c.
 *
 * Lock-free read of a flag written only by the wifi_manager task. Safe from any task
 * (the NimBLE host task included), and returns false before Wi-Fi starts.
 */
bool app_wifi_portal_priority_active(void);

#ifdef __cplusplus
}
#endif

#endif // PORTAL_PRIORITY_H
