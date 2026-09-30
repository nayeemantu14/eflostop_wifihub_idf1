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
 * there, apart from the short holds of app_wifi_radio_hold_active(). See app_wifi.c.
 *
 * Lock-free read of a flag written only by the wifi_manager task. Safe from any task
 * (the NimBLE host task included), and returns false before Wi-Fi starts.
 */
bool app_wifi_portal_priority_active(void);

/**
 * @brief True while a Wi-Fi radio hold runs: the STA is not connected and Wi-Fi is scanning
 *        or trying to connect, for a few seconds at a time.
 *
 * BLE scanning pauses for it as for the portal priority window, so a Wi-Fi scan (the portal
 * page's network list) or a connect attempt (whose first step is a scan for the router) gets
 * the radio: the leak scanner and the valve hunt start nothing new and cancel their own scans
 * and connect attempts, and the valve hunt still runs while a leak response is pended. Unlike
 * the window: no health hold, no [PORTAL] log lines and no valve go-red stamp. A hold lasts
 * from a scan until a few seconds after it (an open portal page asks for a scan about every
 * 3.8 s, so its holds chain, for 30 s at most: then BLE listens 15 s with no hold of any
 * kind, before the next chain), or from a connect attempt until it fails, or its IP, for at
 * most 2.5 s; for the hub's own router retry (30 s after the last attempt, on a router-fallback
 * AP) from half a second before the attempt, 2.5 s in all. Holds never run back to back, an
 * open page's scans apart. It is never on while the STA is connected, and none starts in the
 * portal window. See app_wifi.c.
 *
 * Lock-free: reads tick deadlines, each written by a single task. Safe from any task (the
 * NimBLE host task included), and returns false before Wi-Fi starts.
 */
bool app_wifi_radio_hold_active(void);

#ifdef __cplusplus
}
#endif

#endif // PORTAL_PRIORITY_H
