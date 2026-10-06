# wifi_portal: the change register

This component is the setup portal of the eFloStop II Wi-Fi Hub: the Wi-Fi manager, its HTTP server, its captive
DNS responder and the setup page. It started as the registry component `ankayca/esp32-wifi-manager` 0.0.4 (a
packaging of tonyp7's `esp32-wifi-manager`, MIT, `LICENSE.md`) and is carried in this repository since hub firmware
2.1.4. This file records every change made to it, so that nobody has to diff it against upstream to know what is
ours. `README.md` is upstream's and describes upstream's behaviour, not this copy's.

**How changes are marked.** Every changed spot in the sources carries `LOCAL PATCH (2.1.4 <id>)`, where `<id>` is a
patch of the 2.1.4 plan's catalogue (`docs/field_logs/2.1.4/RADIO_PORTAL_PLAN.md` §6.2: C1-C13) or a work package
(WP1, WP2, WP7, WP8). The two older patches are marked `LOCAL PATCH:` with no id. A change goes in this register in
the same commit as the code, with its id.

**Ownership rules** (plan §6.1, decision D11, `HANDOFF.md` 15a):
- The component is plain tracked code in `components/wifi_portal`. It is not in `main/idf_component.yml` or
  `dependencies.lock`, so no re-resolve, `IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS` or deleted folder can put the
  upstream copy back. There is no upstream to merge from: a fix is made here and recorded here.
- The NVS layout is fixed: namespace `"espwifimgr"`, blobs `"ssid"` (32 B) and `"password"` (64 B), an empty SSID
  meaning nothing saved. `main/wifi_reset/reset_button.c`'s `erase_wifi_credentials()` writes those keys directly,
  and 2.1.3 hubs read them after a rollback. Do not change them.
- The `"settings"` blob (including `ap_channel`) is still loaded at start, but the SoftAP is configured once, before
  the load, with `CONFIG_DEFAULT_AP_CHANNEL` (11): the stored channel (1 on hubs set up before 2.1.4) is read and
  never applied (`HANDOFF.md` 15t finding 1). No later change should re-apply `wifi_settings` to the SoftAP without
  deciding that on purpose.

## 1. Before 2.1.4 (on the registry copy, kept)

| Marker | What | Where |
|---|---|---|
| `LOCAL PATCH:` (`6ad1d7b`) | `WM_ORDER_START_AP` stops `wifi_manager_retry_timer`, so a STA connect cannot race the captive portal's scans. Since 2.1.4 the app's router retry (`main/app_wifi/app_wifi.c`) rejoins the router while the SoftAP is up (I9 is kept by C5 below) | `src/wifi_manager.c` (about line 1221) |
| `LOCAL PATCH:` | `WM_ORDER_START_WIFI_SCAN` soft-fails on `ESP_ERR_WIFI_STATE` instead of `ESP_ERROR_CHECK`-aborting (a reboot during phone-driven setup on the ESP32-S3) | `src/wifi_manager.c` (about line 2346) |

## 2. 2.1.4: the plan's catalogue as built (C0-C13)

Plan references are to `RADIO_PORTAL_PLAN.md` §6.2 (the catalogue), §6.2a (the DNS specification) and §6.3 (the
choices); the `HANDOFF.md` section has the reviews, the councils and the bench expectations. Commits are on
`fix/2.1.4`, oldest first; "fixes" are review fixes of the same item.

| ID | As built | Files | Commits | HANDOFF |
|---|---|---|---|---|
| **C0** | **Not built.** The patch-level guard was for patching in place; the component is vendored instead (D11) | – | – | 15a |
| **C1** | No credential in any log line: the SSID (bounded to 32 characters) and the password's length (`pwd_len`) only. The `wifi_manager` and `http_server` tags stay capped at WARN by `main.c` | `src/wifi_manager.c`, `src/http_app.c` | `607df82`; `7dc35ff` | 15a |
| **C2a** | No `malloc` in the Wi-Fi event handler: event values are posted as scalars | `src/wifi_manager.c/.h` | `97ce041` | 15g, 15h |
| **C2b** | The AP list: records read one at a time into a compact stack array; the list buffer allocated only while the AP is up and only with heap to spare; the list copied out and sent with the lock given back | `src/wifi_manager.c/.h`, `src/http_app.c` | `541eaa4`, `b67e5bf`; `f9a55ef`, `c5f815c`, `405b361` | 15h, 15r |
| **C2c** | A connect that cannot start (`ESP_ERR_WIFI_STATE`) is a failed attempt, never an abort; event bits set only after a start | `src/wifi_manager.c` | `4d67b79`; `7dc35ff` | 15h |
| **C2d** | The remaining runtime `ESP_ERROR_CHECK`s are logged, boot allocations checked | `src/wifi_manager.c` | `9583236`; `f9a55ef`, `7dc35ff` | 15h |
| **C2e** | Bounded JSON builders for the network list and `status.json`; control characters in SSIDs replaced (the N4 heap overflow); invalid UTF-8 bytes as `\u00XX` with `"raw":1` | `src/json.c/.h`, `src/wifi_manager.c/.h` | `488c13d`; `7dc35ff` | 15h |
| **C2f** | The NVS handle and lock released on every save and load path | `src/wifi_manager.c` | `87c3178` | 15h |
| **C2g** | HTTP header buffers on the stack, URLs as string literals | `src/http_app.c` | `6c69ac3` | 15h |
| **C2h** | The Wi-Fi manager's queue 8 messages deep (was 3) | `src/wifi_manager.c/.h`, the demo example | `97ce041` | 15h |
| **C3** | The HTTP server runs only while the SoftAP is up; every handler, and the DNS responder, answers only clients on the SoftAP (403 otherwise; a LAN session is refused at accept); a POST or DELETE is logged only once it passed that check | `src/http_app.c/.h`, `src/dns_server.c/.h`, `src/wifi_manager.c` | `62983bb`, `30fb932`, `a45fc1f`; `3392935` | 15i, 15r |
| **C4** | The captive DNS responder rewritten to §6.2a: bound to 10.10.0.1 from START_AP to STOP_AP, bounds-walked questions, A answers with TTL 60 s, NOERROR with no answer for other types, minimal OPT echo, drop rules (short, long, QR, low heap, > 20 replies/s); the servers retried while either is down | `src/dns_server.c/.h`, `src/wifi_manager.c` | `d300079`; `bf45701` | 15h |
| **C5** | The component's retry timer starts only while `AP_STARTED_BIT` is clear (I9) | `src/wifi_manager.c` | `c8b8c7d`; `4d67b79` | 15h |
| **C6** | httpd bounds: `HTTP_APP_MAX_OPEN_SOCKETS` 5 (G-CNA S1 picks 4, 5 or 7), `open_fn` refuses a session below 12 KB of internal DMA heap (`HTTP_APP_SESSION_MIN_FREE`), a foreign-Host session closed after its 302, a status mutex of its own, every post to the Wi-Fi manager's queue bounded at 200 ms (`WIFI_MANAGER_POST_WAIT_MS`, else 503), body reads timed out | `src/http_app.c`, `src/wifi_manager.c/.h` | `f9a55ef`; `bbc0924`, `3392935`, `92436b5`, `5fb20ca` | 15r |
| **C7** | gzip at build time (`tools/gz_asset.py`, `mtime` 0, whole-line JS comments left out): `Content-Encoding: gzip`, `Vary`, `Cache-Control: no-store` on everything, HEAD sends headers only, the URI matched on its path, 406 only for an identity-only `Accept-Encoding`; `src/compress.bat` deleted | `CMakeLists.txt`, `tools/gz_asset.py`, `src/http_app.c` | `146498c`; `96ce42f`, `a12e3a7`, `66ca418`, `86072f0`, `03720ce` | 15r |
| **C8** | Connect ownership and transactional credentials: request kinds USER / APP_RETRY / AUTO / RESTORE; a USER request waits for the running attempt (≤ 8 s, then aborts it); the page's Connect writes a *candidate*, saved only at its IP (a failure keeps the network in use); intake by `X-Custom-enc: pct` percent-decoding (raw headers still accepted), empty passwords for open networks, lengths after decoding, control bytes refused, 400 with a JSON reason; `status.json` gains `reason` and `pend`; the forget erases an idle STA directly (D9 kept, below). The CONNECT_STA callback's parameter is `kind \| WIFI_MANAGER_CONNECT_NOT_STARTED` | `src/wifi_manager.c/.h`, `src/http_app.c` | `7dc35ff`; `f84500e`, `e7afc09`, `f09535c`, `94c8a41`, `a5e5ae2`, `727ad53`, `767d008`, `35d3192`, `b245ba1`, `802903f`, `d085c79`, `9ba8e9c` | 15r |
| **C9** | The setup page rebuilt on `status.json`: the view picked before rendering, percent-encoded intake, open networks, password checks, reason texts, a 30 s Connect timeout, the empty-list state after 8 s, Rescan, Finish, `?bg=1` on background polls, one request of each kind at a time, a later status reply allowed to pick the view. **The Disconnect button is kept (D9)** | `src/code.js`, `src/index.html`, `src/style.css` | `93f7162`; `4583361`, `6c82af4`, `609b034`, `2b3a501`, `7af07cf`, `cab75aa`, `2b594ce` | 15r |
| **C10a** | The activity hook `http_app_set_activity_hook()`: kinds DNS, PROBE_302, PAGE, API_USER, API_BG, STATUS with the client's IPv4, from the httpd and DNS tasks | `src/http_app.c/.h`, `src/dns_server.c` | `fbd8537`; `d300079`, `30fb932` | 15a |
| **C10b** | `GET /ap.json` serves the cached list only; `POST /scan.json` (the page's Rescan, ≥ 20 s apart: `WIFI_MANAGER_SCAN_GAP_MS`, which is load-bearing for the M4 leak bound, `HANDOFF.md` 15w); the first page request with an empty or stale (60 s) list orders one scan; a failed scan backed off 10 s; `wifi_manager_scan_request()` the single entry; 503 when the heap has no room | `src/http_app.c/.h`, `src/wifi_manager.c/.h` | `b0b2326`; `bbc0924`, `d7ebf15`, `c5f815c`, `2077bb1`, `e08d9a8`, `df08217`, `5fb20ca`, `405b361` | 15r |
| **C11** | Lab knobs for G1 only (`CONFIG_APP_RADIO_LAB`, never in production): the SoftAP's 11b rates off (`wifi_manager_lab_set_ap_11b_off()`), `coex_background_scan` on the list scan (`wifi_manager_lab_set_coex_bg_scan()`); line-neutral hook lines, the functions at the file's end; with the option off every object is byte-identical | `src/wifi_manager.c/.h` | `71398db` | 15u, 15v |
| **C12** | `wifi_manager_ap_stop_in(ms)` (the app owns the AP tail); `POST /finish.json` (only with the STA connected, else 409) through `http_app_set_finish_hook()`: the SoftAP stops at max(IP + 5 s, tap + 2 s) | `src/wifi_manager.c/.h`, `src/http_app.c/.h` | `611b8d3`; `e7a7c9a` | 15i, 15r |
| **C13** | Wi-Fi parameters owned by the component: `csa_count` `WIFI_MANAGER_AP_CSA_COUNT` 3 and `dtim_period` 1 set explicitly; the scan dwell (active 0-60 ms, home 100 ms) after `esp_wifi_start()`; the connected AP's channel written to the RAM STA config at GOT_IP, never saved | `Kconfig`, `src/wifi_manager.c` | `d616bb2`; `f84500e` | 15r |

## 3. 2.1.4: changes outside the catalogue

| Package | What | Files | Commits | HANDOFF |
|---|---|---|---|---|
| WP-V | The component moved byte for byte from `managed_components/ankayca__esp32-wifi-manager` (D11); the registry entry dropped from the manifest and the lock | the whole folder, `.component_hash`, `CHECKSUMS.json`, `.gitignore` | `4f14a23`, `b74a891` | 15a |
| WP1 | A flag for a Wi-Fi scan in flight on the app's channel lines (`wifi_manager_scan_in_flight()`, a bench diagnostic); a forget whose disconnect fails on a connected STA is dropped; the network list answers 503 when the heap has no room, and is copied with WP1's margin | `src/wifi_manager.c/.h`, `src/http_app.c` | `56c2c4d`, `5bd0762`, `7fed411`, `ffd06ba`, `92436b5` | 15g, 15h, 15r |
| WP2 | STOP_AP keeps the AP whole when it cannot leave APSTA mode (retried every 5 s) and the app learns when STOP_AP has freed the AP and its servers (`wifi_manager_ap_stop_done()`) | `src/wifi_manager.c/.h` | `0939a4d`, `ba9fb7c`, `8cc3eea` | 15i |
| WP8 | `wifi_manager_set_scan_gate()`: the app's gate (the LIST pulse request, the 24 KB heap floor, I7's joining station, a STA attempt in flight) runs right before every `esp_wifi_scan_start()`; the portal-window comments removed with the window (D2) | `src/wifi_manager.c/.h` | `e831f3e`, `dd19d6b` | 15u |
| Page round (before the plan) | The setup page polls its network list only while it is used | `src/code.js` | `695283a` | 15a |

## 4. D9: Forget and Disconnect are kept

The plan proposed removing `DELETE /connect.json` and the page's Disconnect button (§6.2 C9, §9, D9); **the user
kept them** (2026-10-01). C9 keeps the button, and C8's forget (an idle STA erased directly, a connected or
connecting one at its disconnect) serves both it and the 10 s reset's `wifi_manager_disconnect_async()`. C3 keeps
the DELETE off the home LAN. On the open setup SoftAP anyone in range can still make the hub forget its Wi-Fi;
leak protection keeps running (D2), and the hub is off the cloud until Wi-Fi is set up again (`HANDOFF.md` 15, 15x).

## 5. Known limits of this component (2.1.4)

- Enterprise and OWE networks are not marked in the list; a Connect to one fails with `Security not supported`, and
  OWE rows ask for a password (`HANDOFF.md` 15r residual 7).
- The 60 ms scan dwell applies to connect and rejoin scans too (count reason 201, NO_AP_FOUND, on a weak router:
  G7) (15r residual 8).
- httpd's LRU purge picks a just-accepted session first: a 6th parallel connection can reset one whose request is in
  flight (G-CNA S1) (15r residual 9).
- The page: no `autocorrect="off" spellcheck="false"` on the password and hidden-SSID inputs; an empty Rescan leaves
  dead rows up to 8 s; a resumed raw (Latin-1) SSID that has left the list is re-sent as UTF-8 (15r residual 10).
- "Other Network" after a success can start a switch late in the 60 s tail (PH-7, the user's decision, 15x).
- An IP of a written-off attempt arriving after a new USER attempt started is credited to the new one (15r
  residual 2).
- The provisional constants, each named in the sources: `HTTP_APP_MAX_OPEN_SOCKETS` 5 and `HTTP_APP_SESSION_MIN_FREE`
  12 KB (G-CNA S1, G3), `WIFI_MANAGER_AP_CSA_COUNT` 3 (G0, P-8), the scan dwell (G7), `WIFI_MANAGER_SCAN_GAP_MS`
  20 s and the list's 60 s staleness (plan C10b, 4.4).

## 6. Change log of this register

| Date | Change |
|---|---|
| 2026-10-07 | First version (WP10), at hub firmware `b651701` (CP7). |
