# Changelog — eFloStop II Wi-Fi Hub firmware

The firmware version lives only in `PROJECT_VER` (`CMakeLists.txt`). It is reported as `gateway.fw` on every
telemetry message and as `fw_version` in twin reported.

Wire-level detail for the entries below is in:
- `docs/telemetry/telemetry_messages.md`, which has a real example of every message, v5.0;
- the JSON schemas in `docs/telemetry/schemas/`;
- `C2D_COMMANDS.md`.

`docs/telemetry/validate_capture.py` checks an IoT Hub capture against this contract.

---

## 2.1.4 — 2026-09-25

This is a bug-fix and safety release on top of 2.1.3. It fixes the field defects found on 2.1.3, a group of
valve-safety defects found while analysing them, and the defects found in the release review. The root-cause
analysis is in `docs/field_logs/2.1.3/ROOT_CAUSE.md`.

The telemetry schema is still `eflostop.v2`. No key is renamed or removed, and no NVS data changes. Some values
and shapes are new, and parsers must accept them (see *Wire changes*).

### Development builds (2.1.4 plan, from 2026-10-01)

The user approved the 2.1.4 radio and setup-portal plan on 2026-10-01
(`docs/field_logs/2.1.4/RADIO_PORTAL_PLAN.md`; progress in `docs/field_logs/2.1.4/HANDOFF.md` §15). Its
packages land one at a time, each behind a bench gate, and the last one (WP10) folds this part into the
sections below. The first two, WP-V and WP0, change no behaviour. The third, WP1, removes every way the setup
portal could reboot the hub. The fourth, WP2, starts the cloud's TLS only once the setup SoftAP is down, stops
the SoftAP soon after Wi-Fi connects, and keeps the setup web server off the home network. WP2b moves the
MQTT stop off the task that handles leaks, and WP2c moves every cloud publish off it too, onto a sender task of
its own. WP2d makes a busy lock delay a leak decision instead of dropping it. WP2e sends every twin report in
order, newest last, and keeps 16 of the sender's 24 queue places for leak, valve and rules events. WP1, WP2,
WP2b, WP2c, WP2d and WP2e are built and benched together as Build checkpoint 6, of `545b8f2` (HANDOFF
§15h-§15p).

- **The Wi-Fi manager is now part of this repository (WP-V, user decision D11).**
  - The component moved from `managed_components/ankayca__esp32-wifi-manager` (registry 0.0.4 with this
    project's local patches) to `components/wifi_portal`, byte for byte. `main/idf_component.yml` and
    `dependencies.lock` no longer list `ankayca/esp32-wifi-manager`; the other registry components are
    unchanged.
  - A re-resolve, `IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS` or a deleted directory can no longer put the
    upstream copy back silently, and `idf.py fullclean` no longer stops with `ComponentModifiedError`. The
    local patches are ordinary tracked code, marked `LOCAL PATCH`.
  - The component's library is now `libwifi_portal.a`, linked after `main`. Two `__FILE__` strings are 24 B
    shorter each; nothing else in the image changes.
- **No Wi-Fi password in any log line (WP0, C1).** The `http_server` and `wifi_manager` lines that printed the
  password now print its length (`pwd_len`), and the `wifi_manager` lines print the SSID bounded to 32
  characters: a 32-character SSID has no terminator, and the password stored right after it used to print with
  it. Both tags are still capped at WARN, so these lines do not print.
- **Bench diagnostics for gate G0 (WP0; log lines only, no behaviour change).**
  - `CONFIG_APP_BENCH_DIAG` (`main/Kconfig.projbuild`) has no prompt, so its default decides: **y during
    development, n for the release (WP10)**. It sets the Wi-Fi driver's `wifi` log tag back to INFO (the Wi-Fi
    manager turns it off) and prints a warning at boot. The first configure adds `CONFIG_APP_BENCH_DIAG=y` to
    `sdkconfig`, so the file's hash changes once.
  - The monitor task samples the internal DMA-capable heap every second, and failed heap allocations are
    counted from boot.
  - New lines:
    - `MONITOR`: `idma: free=%lu min=%lu largest=%lu min_largest=%lu allocfail=%lu`, right after each `heap:`
      line (`min` and `min_largest`: the lowest since the line before); ` (last: %lu B, caps 0x%lx, %lu B free,
      %s)` is appended when an allocation failed since the line before.
    - `APP_WIFI`: `Wi-Fi channel at AP start: radio %u (SoftAP configured %u), router last seen on %u` (or
      `..., router not joined since boot`), `Wi-Fi channel at IP: radio %u, router %u` and `Wi-Fi channel at
      link loss: radio %u, router was on %u` (only when the link was up).
    - `APP_WIFI`: `SoftAP: station %02X:%02X:%02X:%02X:%02X:%02X got %d.%d.%d.%d, %lu ms after joining` (or
      `... got %d.%d.%d.%d (join not seen)`), at each DHCP lease on the SoftAP.
    - `APP_WIFI`: `portal client %d.%d.%d.%d: first %s, %lu ms after joining` (or `... first %s (no SoftAP join
      seen)`), once per phone and kind: `DNS query`, `captive probe (302 sent)`, `page request`,
      `Connect/Disconnect request`, `network list request`, `status request`.
    - `BLE_LEAK`: `eleak %02X:%02X:%02X:%02X:%02X:%02X burst: n=%u in %u.%02u s, dT %u-%u ms, phy=%s` (or
      `... burst: n=1, phy=%s`), about 1.5-2 s after each advertising burst ends, for the first 4 sensors
      heard: the advertisements heard, the shortest and longest gap between two of them, and their primary
      PHYs (`1M`, `Coded`, `1M+Coded`, `other` and its combinations).
    - `APP_WIFI` (warning, bench builds only): `bench build (APP_BENCH_DIAG): Wi-Fi driver log at INFO - not
      for release`, after `AP SSID:`; then the driver's own `wifi:` lines (channel switches and CSA, SoftAP
      station join and leave, the station's connect states).
  - Unchanged: every existing line, including those the production tool matches. A bench build must not go
    through the production tool with Wi-Fi credentials saved: the driver's connect line carries the router's
    SSID and BSSID, and with an SSID that contains "mac" the tool would take the router's BSSID for the hub's
    Wi-Fi MAC.
  - Memory: `.bss` +121 B, `.data` +8 B and about 40-60 B of permanent heap (one more event handler); no IRAM;
    flash about +5 KB. No new task or timer; the monitor task wakes every second instead of every 10 s.
  - WP1 extends three of these lines (the `idma` line, the channel lines, the scanner heartbeat): see the next
    entry.
- **The setup portal can no longer reboot the hub (WP1: C2 a-h, C2b, C4, C5).** Commits `97ce041` … `5bd0762`
  (`components/wifi_portal`, `main/app_wifi`; the diagnostics below also in `main/systemservices` and
  `main/ble_leak_scanner`); details in HANDOFF §15h, the build and the bench gates in §15j-§15k.
  - **No reboot path is left in the portal.** These used to reboot the hub, and no longer do: a Connect from the
    setup page while another connect attempt is still running (the router retry's, or an earlier Connect's); a
    Wi-Fi scan result read at low memory; the SoftAP start's mode switch or an IP read failing; a forget's
    disconnect failing; an allocation failing in the Wi-Fi event handler or in an HTTP request; a UDP datagram
    shorter than 12 B sent to port 53. A DNS query of 80 B or more no longer writes past its buffer. The checks
    that are left run once at boot.
  - **A Connect that meets a running attempt fails at once.** The page shows "Connection failed" and its Retry
    works once the other attempt has ended; what was typed is no longer lost to a reboot. The running attempt goes
    on, and at its IP it keeps its own network. (WP4's C8 queues such a Connect instead; the page keeps its 8 s
    guard until then.)
  - **Captive DNS rewritten.** It answers only on the SoftAP's address, 10.10.0.1 (it used to answer on the home
    LAN too), and from the SoftAP's start to its stop (it used to stop at the IP, so a phone that joined in the
    SoftAP's last 60 s was never sent to the setup page). A and ANY get 10.10.0.1 with a 60 s TTL; AAAA, HTTPS,
    SVCB and every other type get "no data" (they used to get an A record); a query with EDNS0 (Windows, Linux,
    strict-DNS Android) gets a well-formed reply with a minimal OPT record (it used to get a malformed one);
    malformed queries get FORMERR or NOTIMP. No reply to datagrams under 17 B or of 300 B or more, to DNS
    responses, below 10 KB of free internal DMA-capable heap, or past 20 replies a second.
  - **The network list and `status.json` are always valid JSON.** A neighbour's SSID with control characters no
    longer overflows the list buffer (they show as `?`); a 32-character SSID no longer puts the Wi-Fi password
    into `status.json`; an SSID that is not UTF-8 (Latin-1, GBK) is sent as `\u00XX` escapes with `"raw":1` on its
    entry; an empty list is `[]`. The list shows the 15 strongest named networks, one per SSID and security type,
    at the strongest access point's channel. Its buffer exists only while the SoftAP is up.
  - **One retry owner while the SoftAP is up.** The Wi-Fi manager's own retries never run while the SoftAP is
    up; the router fallback's retry (every 33-36 s) is then the only one. The Wi-Fi manager's loop of retries in
    the setup SoftAP's last 60 s, after the new Wi-Fi was lost again, is gone. With the SoftAP down nothing
    changes: 3 retries about 10 s apart, then the fallback SoftAP.
  - **The SoftAP's servers come back.** If the web server or the DNS cannot start when the SoftAP comes up (low
    memory), both are tried again every 5 s while it stays up.
  - **Forget and the 10 s reset.** Unchanged, except that a forget whose disconnect fails while Wi-Fi is
    connected is dropped (nothing is erased; tap Disconnect again). Saving and loading the Wi-Fi settings now
    release their NVS handle and lock on every path: a failed save no longer blocks every later one, and no heap
    leaks at a boot with nothing saved. The NVS layout is unchanged.
  - **Log lines.** New, all warnings or errors (the `wifi_manager` and `http_server` tags stay capped at WARN):
    - `wifi_manager`: `network list: %u access points left out (list buffer full)`, `esp_wifi_scan_get_ap_record
      failed (%s) - network list kept`, `network list: no memory for its %u B - the page lists no network yet` (at
      most once per SoftAP start), `Wi-Fi config not saved to flash (%s)`, `esp_netif_get_ip_info failed (%s) -
      status without addresses`, `ORDER_START_AP: esp_wifi_set_mode failed (%s) - no AP, tried again through the
      retry timer`, `ORDER_DISCONNECT_STA: esp_wifi_disconnect failed (%s)` (and the same with `- still
      connected, nothing erased`), `could not get access to json mutex in WM_EVENT_STA_GOT_IP` (it used to
      abort), `ORDER_CONNECT_STA: %s failed (%s) - attempt not started`, `AP up without its %s - tried again
      every %d s`, `AP servers running again (HTTP and DNS)`.
    - `http_server`: `httpd_start failed (%s)`.
    - `dns_server`: `captive DNS: %s failed (errno %d) - trying again every %d ms`, `captive DNS: DEFAULT_AP_IP
      is not an IPv4 address - not started`, `captive DNS: the stopped task has not ended - not started`, `captive
      DNS: task not created (no memory)`, `captive DNS: task still ending after %d ms - it ends by itself`.
    - Also `APP_WIFI: WiFi Disconnected. Reason: 205` after a connect that could not start (the line itself is
      unchanged).
  - **Removed:** `dns_server: Failed to create socket` and `dns_server: Failed to bind to 53/udp` (each was
    followed by `exit()`, a reboot), and `dns_server: Replying to DNS request for %s from %s`, now a per-query
    DEBUG line that this build compiles out (WP0's `portal client <IP>: first DNS query` shows the first query of
    each phone). `dns_server: DNS Server listening on 53/udp` and `UDP sendto failed: %d` are unchanged.
  - **Changed, bench diagnostics** (the plan's updates of 2026-10-01):
    - `MONITOR`: the `idma:` line ends ` min_ever=%lu`, the allocator's own low of the internal DMA-capable heap
      since boot (the 1 s sampler can miss a short dip); with a failed allocation it follows the `(last: ...)`
      part. What WP0 printed is an exact prefix of the new line.
    - `APP_WIFI`: the three `Wi-Fi channel at ...` lines end `, Wi-Fi scan in flight` while a Wi-Fi scan runs (the
      radio's channel is then the scan's); they are unchanged otherwise.
    - `BLE_LEAK`: while scanning is paused the heartbeat reads `[HEARTBEAT] Scanner alive, whitelist=%d sensors,
      scanning paused for %lu s (%s)`, with `Wi-Fi setup portal` or `Wi-Fi radio hold`; not paused, it is
      unchanged.
  - Every other existing line is unchanged. The production tool matches none of the new or changed lines, and no
    line prints a Wi-Fi password.
  - Memory (estimated from the objects; Build checkpoint 6 measures it): `.bss` about −38 B, `.data` about −6 B;
    free heap at rest about +2.8 KB with Wi-Fi connected and the SoftAP down (about +1.3 KB with it up); flash
    about +3.8 KB; no IRAM; no new task or timer. The DNS task (about 3.4 KB) now also runs through the SoftAP's
    last 60 s after a rejoin.
  - **Known until later packages:** the DNS and the web server run beside the cloud's TLS start in the SoftAP's
    last 60 s after a rejoin, so failed allocations there are expected (WP2 admits the cloud only after the SoftAP
    stops); the web server still answers on the home LAN (WP2, C3); an open network still gets HTTP 400 (WP4).
    WP2 (next entry) closes the first two, and is built with WP1, so no bench image has them.
- **The cloud waits for the setup SoftAP, the SoftAP stops sooner, and the setup web server stays off the home
  network (WP2: cloud admission and the SoftAP's stop; C3; C12's API).** Commits `dc9db75` … `0aae305`
  (`main/iothub`, `main/dps_client`, `main/telemetry`, `main/offline_buffer`, `main/app_wifi`,
  `components/wifi_portal`); details and the bench gates in HANDOFF §15i-§15k.
  - **No TLS beside the SoftAP.** The MQTT and DPS connections start only once Wi-Fi has its IP address, the
    setup SoftAP is down with its web and DNS servers freed (plus 0.5 s), and the internal DMA-capable heap has
    36 KB free with a 12 KB block. On a normal boot there is no SoftAP and that holds at once, so the cloud comes
    up as fast as before. Below those figures the hub waits; after 60 s it connects with an 8 KB block (a
    warning), and after 180 s whatever the heap (an error). After a router outage the hub used to start TLS
    beside the SoftAP right after it rejoined (on the bench: internal heap down to 152 B and 4 failed
    allocations, and the largest free block stuck at 6,400 B afterwards).
  - **A link loss stops MQTT from the hub's cloud task** (`iothub_task`) on its next pass, no longer from the
    Wi-Fi manager's task, and a SoftAP that comes up while the cloud is connected stops it too; the Wi-Fi
    callbacks only set flags. The stop can hold the cloud task, which also evaluates leaks, for about 1-5 s
    (about 10-20 s if a connect was in flight): a leak is acted on that much later, never missed (HANDOFF §15i;
    decided by the user on 2026-10-01). WP2b (next entry) moves the stop itself to the Wi-Fi task: leak
    handling no longer waits for it.
  - **DPS (first commissioning).** A registration in progress gives up within about 1 s when Wi-Fi is lost or
    the SoftAP comes up, and runs again as soon as the cloud is admitted again, with no back-off and no attempt
    counted.
  - **The SoftAP stops soon after Wi-Fi connects.** After an automatic rejoin (a router outage) it stops 0.5 s
    after the IP address with no phone on it; with a phone on it, 20 s after the IP or 10 s after the last phone
    leaves. After a Connect on the setup page it stops 60 s after the IP, or 15 s after the last phone leaves,
    never before 15 s. It used to stay up 60 s in every case. The setup portal's BLE pause ends at that stop, so
    leak scanning resumes sooner after a setup. Any SoftAP still up 75 s after the IP is stopped by the hub (the
    safety net used to cover only the no-credential portal). If the Wi-Fi driver will not leave SoftAP mode, the
    SoftAP keeps its servers, the stop is tried again every 5 s, and BLE scanning resumes anyway.
  - **The setup web server runs only while the SoftAP is up,** and answers only requests to 10.10.0.1 from a
    phone on the SoftAP (10.10.0.0/24): any other request, from the home network included, gets `403 Forbidden`
    and nothing is done. The hub's address on the home network used to serve the setup page, its Connect and its
    Forget. With the SoftAP down nothing listens on port 80. The captive DNS also ignores queries from outside
    the SoftAP's subnet. Free heap at rest with Wi-Fi connected is about 5 KB higher (no web server running).
  - **The MQTT queue is bounded** (`outbox.limit` 12 KB: the largest message, a full hub's snapshot of about
    8 KB, plus 4 KB). An event the queue refuses is kept in the offline buffer and replayed in order while
    connected (every 10 s); a refused snapshot is tried again 5 s later; refused subscriptions at a connect make
    the hub reconnect. The `lifecycle` message and the twin GET are sent again every 5 s while connected until
    MQTT takes them.
  - **Log lines.** New:
    - `IOTHUB`: `cloud admitted %lu.%lu s after the IP (internal DMA free %u B, largest %u B)`, `cloud admission
      deferred: SoftAP up - no TLS or DPS until it stops`, `cloud admission deferred: internal DMA free %u B,
      largest %u B (needs %u / %u)`, `cloud admission withdrawn (%s)` (`WiFi down`, `SoftAP up`); warnings `cloud
      admitted %lu.%lu s after the IP below the heap gate (internal DMA free %u B, largest %u B) - escape %lu
      since boot`, `cloud admission: the SoftAP's stop still not finished after %d s - the heap gate decides`,
      `SoftAP up — stopping MQTT client (free TLS heap for AP/captive portal)`, `Lifecycle not taken by MQTT -
      sent again every %d s while connected`, `Twin GET not taken by MQTT - sent again every %d s while
      connected`; errors `cloud admitted %lu.%lu s after the IP whatever the heap (internal DMA free %u B,
      largest %u B) - escape %lu since boot`, `Subscribe refused (%d %d %d, outbox %d B) - reconnecting`.
    - `DPS`: warning `DPS registration aborted after %lu s: Wi-Fi lost or SoftAP up - tried again once the cloud
      is admitted again`; errors `DPS registration not started (no memory)`, `DPS registration failed (MQTT
      client not created)`.
    - `TELEMETRY_V2` (warnings): `Outbox full - %s kept for replay`, `Outbox full - %s kept for replay, behind
      the buffered ones`, `Outbox full - %s not kept`. `OFFLINE_BUF` (warning): `store: buffer busy - skipped,
      not waited for`.
    - `APP_WIFI`: `SoftAP tail after an automatic rejoin (no station on it) - it stops %d.%d s after the IP`,
      `SoftAP tail after an automatic rejoin (stations on it: %d) - it stops %d s after the IP, or %d s after the
      last station leaves`, `SoftAP tail after a setup-page Connect (stations on it: %d) - it stops %d s after
      the IP, or %d s after the last station leaves (not before %d s)`, `SoftAP tail: %s - it stops %lu.%lu s
      after the IP` (`no station left on it`, `a station on it again`, `its stop not set at the IP, set now`),
      `SoftAP stopped (its servers too) %lu.%lu s after the IP`; warning `SoftAP still up %u s after Wi-Fi
      connected - stopping it` (outside the portal window; should never appear). `portal priority OFF (%s) - BLE
      scanning resumed` has a new reason, `SoftAP stop failed`.
    - `wifi_manager` (warnings and errors print under the tag's WARN cap): warning `AP stop in %lu ms not set
      (timer queue full)`; error `ORDER_STOP_AP: esp_wifi_set_mode failed (%s) - AP kept up, stopped again in
      %d s`. `dns_server`: error `captive DNS: DEFAULT_AP_NETMASK is not an IPv4 netmask - not started`.
  - **Same text, new place or time:** `IOTHUB: WiFi down — stopping MQTT client (free TLS heap for AP/captive
    portal)` now comes from the cloud task, right after `cloud admission withdrawn (WiFi down)`; `IOTHUB: WiFi
    up — restarting MQTT client` now prints at the admission, after `cloud admitted ...` (after a rejoin: once
    the SoftAP has stopped); `IOTHUB: Connected to Azure IoT Hub!` after a setup or a rejoin now follows the
    SoftAP's stop; `IOTHUB: Twin GET requested (rid=%d)` prints only once MQTT took the GET; `APP_WIFI: portal
    priority: setup AP still up %u s after Wi-Fi connected - stopping it` is now the 75 s stop in the portal
    window; `APP_WIFI: portal priority: Wi-Fi connected - BLE scanning stays paused until the setup AP stops
    (about %d s)` still says `60`, now the longest it waits. The `http_server` lines `POST %s` and `DELETE %s`
    (capped at WARN, so not printed) are logged only for phones on the SoftAP; a refused request is a DEBUG line.
  - No line was removed, and every other line is unchanged. The production tool matches none of the new or moved
    lines (none prints at its boot, which has no Wi-Fi), and no line prints a credential.
  - Memory (estimated from the objects; Build checkpoint 6 measures it): `.bss` +86 B, `.data` +1 B; flash about
    +7.9 KB; no IRAM; no new task, timer or allocation; free heap at rest about +5 KB with Wi-Fi connected and
    the SoftAP down. The MQTT queue holds at most 12 KB during a stalled session (it had no limit).
  - **Known until later packages:** the setup page's Finish button and `POST /finish.json` (C12's other half)
    come with WP4, so after a Connect the SoftAP stays up to 60 s while the phone stays joined; the Forget is
    still reachable from a phone on the SoftAP (D9 kept); a twin reported-property update refused by a full
    queue is not sent again until the next report (HANDOFF §15i; fixed by WP2e, which builds it again 5 s
    later).
- **The MQTT stop no longer holds leak handling (WP2b; the user's decision of 2026-10-01 on WP2's stall).**
  Commits `fd682be` … `d0d5284` (`main/iothub`, `main/app_wifi`, `main/telemetry`, `main/offline_buffer`);
  details in HANDOFF §15m, the bench checks in §15k items 7 and 10. Reviewed and voted 3/3 SHIP by the council
  (rtos, safety, cloud), with no firmware change asked; Build checkpoint 6 is of `d0d5284`. This closes WP2's
  open decision on the stall (ADM-3, HANDOFF §15i). At CP6 the bench checks that, at a router pull, the cloud
  task's next line follows `WiFi down — stopping MQTT client` within about 50 ms, and that a leak raised at the
  pull closes the valve as fast as one raised with Wi-Fi up, while the Wi-Fi task may still be inside the stop.
  - **The stop at a link loss, a SoftAP start or a SAS renewal runs on the Wi-Fi helper task** (`wifi_task`),
    no longer on the cloud task (`iothub_task`), which evaluates leaks and commands the valve close: those never
    wait for it. The cloud task asks for the stop and goes on; the Wi-Fi task may be held about 1-5 s (10-30 s
    with a connect in flight) once per outage, SoftAP start or renewal, and the cloud is admitted again only
    once that stop has ended. The SAS renewal stops, re-keys and restarts the client the same way. The twin
    report is sent only while connected.
  - **Buffered events are no longer erased when the connection drops during their replay:** the replay stops
    at the first event it can no longer hand to a live connection, and keeps it and the rest for the next
    connect (an event the cloud did get may then arrive twice).
  - **Log lines.** New: `IOTHUB`: `cloud admission deferred: the last MQTT stop is still under way` (once per
    hold), `MQTT client stopped on wifi_task in %lu.%lu s`; `OFFLINE_BUF` (warning): `MQTT session ended -
    drain stopped at [%s], kept for the next connect`. Same text, new time: `TELEMETRY_V2: MQTT connected =
    false` now prints as the stop is asked, right after the `… — stopping MQTT client (…)` line, and `IOTHUB:
    SAS: token renewed (…)` now follows `MQTT client stopped on wifi_task …`. No line was removed; the
    production tool matches none of these, and none prints a credential.
  - Memory (estimated from the objects; Build checkpoint 6 measures it): `.bss` −75 B, `.data` 0, flash about
    +0.65 KB; no IRAM; no new task, timer or allocation, except about 0.1-0.15 KB once per boot at the first
    stop of a connected session (the Wi-Fi task's first network call).
  - **Known, for the user's decision (HANDOFF §15m):** a live DPS registration (first commissioning, or every
    hub after a provisioning-epoch change) still runs on the cloud task, for up to about 60 s (decided on
    2026-10-02: kept there for now, and made asynchronous before any provisioning-epoch change); a publish on the
    cloud task can wait about 10-20 s on a full network send buffer when the internet link dies silently under
    a connected session (decided on 2026-10-02 and closed by WP2c, next entry).
- **Leak handling no longer waits on the internet: every cloud publish moves to a sender task, `cloud_tx` (WP2c;
  the user's decisions D1-D7 of 2026-10-02).** Commits `f79805d` … `4ed8c77` (`main/iothub`, `main/telemetry`,
  `main/offline_buffer`, `main/app_lora`); details in HANDOFF §15n, the bench checks in §15k items 11-16.
  Reviewed, fixed, and voted 4/4 SHIP by the council (rtos, safety, cloud, wifi-portal).
  - **The cloud task that evaluates leaks (`iothub_task`) no longer publishes anything.** It still makes every
    decision and builds every message (an event's `ts` is still its build time), and hands each one over without
    waiting to a new task, `cloud_tx`, which makes every publish, the offline buffer's replay and every store
    into it. When the internet link died under a working Wi-Fi connection, a publish could hold leak handling
    for about 10-20 s (the bench measured that from a leak to its valve close); now the close is posted within
    milliseconds of the leak, whatever the cloud does. What is left on that path is local: the serial log, the
    flash, and the hub's own locks (WP2d, next entry). A live DPS registration (first commissioning, or after a
    provisioning-epoch change) still runs on the cloud task (decision D2).
  - **The order on the wire is kept.** Buffered events are replayed before each connection's `lifecycle`, and
    live events follow it; the replay also runs before any new event whenever anything is buffered, and every
    10 s while connected. A snapshot is built only while the sender is idle, one at a time, and its heartbeat
    bookkeeping runs when its result comes back; one whose device list changed before it went out is not sent
    and is built again.
  - **At-least-once delivery (decision D3): the cloud drops a message only when it is byte-identical to one it
    already has (the payload, or a hash of it), keeping the first copy** (the user's decision on TC-2,
    2026-10-02, HANDOFF §15n; it replaces D3's first rule, `gateway.id + ts + event + device id`, which also
    merged two different `cmd_ack`s, or two `valve_state_changed`, in the same second). An event whose publish
    sees its connection end is also kept in the offline buffer and replayed, so it can reach the cloud twice,
    and the second copy can arrive after newer events (it used to be lost when the MQTT queue expired it 30 s
    later). Every duplicate is the same bytes, except a pre-sync event whose time stamp could not be saved: its
    copies can differ in `ts` by about 1 s. An event that MQTT accepted into a connection that then dies
    silently can still be lost, as before. Cloud-side detail in `C2D_COMMANDS.md` §3.5.
  - **The MQTT stop and a publish never overlap.** The sender's publishes and the Wi-Fi task's MQTT stop share a
    gate that neither side waits for; a stop that meets a publish runs as soon as it ends (`MQTT stop waits for
    cloud_tx's publish`). The MQTT client frees its queue without a lock as it stops, so the two must not meet.
  - **Health alerts wait, without loss,** while the sender is busy and memory is low (fewer than 16 queued, 8
    since WP2e, and 12 KB free with a 4.5 KB block, unless the sender is idle), so a stalled connection cannot
    pile them up. With 24 messages already queued in a stall, a further event is dropped with an error line that
    names it; the next snapshot carries the state, and the valve close is not affected.
  - **Decommission-all** sends the twin, the final snapshot and the offline buffer's clear through the sender, in
    that order, and waits up to 25 s for them; if the sender is stuck, the hub erases the offline buffer itself
    before it restarts, so no old event is replayed afterwards.
  - **The serial bench keys** `s`, `r`, `d` and `a` (a LoRa test packet, restart RX, the counters, LoRa ACKs on
    and off) are read by the LoRa task; their own task, `uart_cmd_task`, is gone. The same lines; the production
    tool sends none of them.
  - **Log lines.** New:
    - `IOTHUB`: `cloud_tx started (stack %u B, priority %u)` (at boot); warnings `Pub %s took %lu.%lu s
      (msg_id=%d)` (a publish that held the sender 1 s or more), `lifecycle not built in 1 s - live messages go
      first`, `health alerts held - TX busy, internal free %u B, largest %u B`, `SNAP result outstanding for %lu s
      - cloud_tx busy`, `SNAP published while the device set changed - the reconciled one follows now`,
      `decommission: cloud_tx did not finish in %d s - offline buffer erased here`; `MQTT stop waits for
      cloud_tx's publish`; errors `cloud_tx: creation failed - rebooting`, `%s called on iothub_task - refused`.
    - `TELEMETRY_V2`: warnings `Pub %s not confirmed (msg_id=%d) - kept for replay, a duplicate is possible`, `Pub
      %s not confirmed (msg_id=%d) - not kept`, `Pub %s not sent - the device set or session changed during its
      line; built again`; errors `TX queue full (%d) - %s not sent` (for example `TX queue full (24) -
      leak_detected event not sent`), `%s called on iothub_task - refused`. The two `called on iothub_task` lines
      should never print.
  - **Same text, new task or time:** the `TELEMETRY_V2` `Pub …`, `Outbox full …`, `Offline — buffering …`
    and `Draining …` lines, every `OFFLINE_BUF` line, `IOTHUB: Twin reported …`, `IOTHUB: Lifecycle not taken
    by MQTT …` and `IOTHUB: Twin GET requested …` (on a retry) now come from the sender, so their place among
    the cloud task's lines changes; `IOTHUB: SNAP heartbeat=reset …` and `SNAP heartbeat=suppressed
    (publish-failed)` now print when the snapshot's result is read, after its `Pub snapshot` line. No line was
    removed or reworded; the production tool matches none of the new lines, and none prints a credential.
  - Memory (estimated from the objects; Build checkpoint 6 measures it): `.bss` +6.3 KB, the sender's static
    5,120 B stack and its queues (an exception to the plan's "no new task, no heap at rest" rule, approved by the
    user); `.data` 0; flash about +7.7 KB; no IRAM; no heap held at rest. The removed `uart_cmd_task` frees about
    4.4 KB of heap, so about 1.8 KB more internal RAM is in use at rest.
  - **Decided by the user on 2026-10-02 (HANDOFF §15n), and done in WP2e (below):** the dedupe rule is the
    identical payload (TC-2, above); at most 8 of the 24 queue places can be health alerts, so at least 16 stay
    for leak, valve and rules events (SAFE-1); and every twin report is built fresh and sent in order, so an
    older reported value can no longer overwrite a newer one, as it could after a reconnect with a backlog, or
    when a provision's report followed a quick `rules_config` or `set_hub_name` (TW-1).
- **A busy lock no longer drops a leak (WP2d; the user's decision D5 of 2026-10-02).** Commits `dd4eb0f` …
  `fcc0979` (`main/rules_engine`, `main/provisioning_manager`, `main/iothub`); details in HANDOFF §15o, the bench
  check in §15k item 17. Reviewed, fixed, and voted 3/3 SHIP by the council (safety, rtos, cloud).
  - **Before** (2.1.3 too): a wet report that found the rules engine's lock or the provisioning lock busy for
    1 s was dropped, with no latch and no valve close, and a BLE sensor may not report the same state again for
    up to 5 min. **Now:** with provisioning busy, the leak is decided on the last device list and rules the hub
    applied, so a sensor in that list closes the valve as usual, about 2 s after its report, and any other is
    ignored; with the rules lock busy, the report is kept (up to 4, wet and dry in order) and evaluated again
    within about 100 ms of the lock coming free, after any event that was already waiting, so `auto_close` still
    follows `leak_detected`. `leak_detected` goes out at once in both cases. With both locks free nothing changes.
  - The same fallback in three places that read a busy provisioning lock as "auto-close off": cancelling an
    override (it cleared the incident with no close), the valve's reconnect check (it could read a physical
    override and block auto-close for 24 h), and the end of an override window (it skipped the close).
  - **Log lines.** New: `RULES_ENGINE`: warnings `Provisioning busy for 1 s - leak from %s sensor %s decided on
    the last known devices and rules (%s)`, `Rules lock busy for 1 s - wet report from %s sensor %s kept,
    evaluated again next pass`, `Rules lock busy - the copy used while provisioning is busy keeps the old rules`,
    `Provisioning busy for 1 s - the rules copy was not refreshed, the device-set change is retried`; error
    `Rules lock busy - %d leak reports already kept, the %s one from %s sensor %s is lost`. `IOTHUB`: warning
    `Boot: rules engine missed the device list or rules - reading them again in the loop`. None should print in a
    normal run. `RULES_ENGINE: Failed to take mutex` now prints only for a report that is dropped. No line was
    removed; the production tool matches none of these, and none prints a credential.
  - Memory (estimated from the objects): `.bss` +477 B (the copy of the device list and the kept reports),
    `.data` +2 B, flash about +2.7 KB; no IRAM; no new task, timer or allocation.
  - **Known:** a dry report from the valve's own flood probe can still be dropped behind a busy lock when its wet
    one was not kept (pre-existing; the valve then counts as wet until it links again). **Decided by the user on
    2026-10-02 (HANDOFF §15o), for WP3:** keep such dry valve-flood reports on a busy rules lock, and let the
    live provisioning read decide membership in the same lock hold (WP2D-C4). WP2d's `.bss` counts under D5's
    approval.
- **Twin reports in order, and a reserve for safety events in the sender's queue (WP2e; the user's decisions of
  2026-10-02 on TW-1, SAFE-1 and TC-2).** Commits `2ef2b58` … `545b8f2` (`main/iothub`, `main/telemetry`);
  details in HANDOFF §15p, the bench checks in §15k items 14(g) and 15(a). Reviewed, fixed, and voted 3/3 SHIP by
  the council (cloud, rtos, safety).
  - **Every twin report is built fresh by the cloud task and sent in order by the sender.** `rules_config`,
    `set_hub_name`, a desired-properties change and the twin GET's answer at each connect no longer publish a
    report from the MQTT client's task: they ask for one, and the cloud task builds it on its next pass. Reports
    are numbered as they are built, and the sender never writes one older than a report it has already seen, so
    the last report written is the newest. A report the MQTT client refuses with the connection still up is
    built again 5 s later (it used to wait for the next report). A report that finds 8 or more messages queued
    waits, and is built fresh once there is room (checked every 2 s), instead of being lost to a full queue. The
    twin is still reported at every connect and every device-set change, only while connected.
  - **A command's `cmd_ack` now always goes out before the twin report it causes,** as `C2D_COMMANDS.md`
    documents (`rules_config` and `set_hub_name` used to write the twin first). The report follows, normally
    within a second: an app should wait for the reported property to change rather than read it once on the ack.
  - **At least 16 of the sender's 24 queue places stay for leak, valve and rules events.** Health alerts and twin
    reports go into the queue only while fewer than 8 messages wait (health alerts: 16 before), and a snapshot
    only into an idle sender. A decommission-all's twin is skipped when 8 or more wait; the connect after the
    restart reports it.
  - **Log lines.** New, `IOTHUB`: `Twin report %u not sent - report %u, built after it, went first` (an older
    report the sender skips; expected when a device-set change lands on a connect) and the warning `Twin report
    %u not taken by MQTT (msg_id=%d) - built again after %d s`. `IOTHUB: Twin reported (%d): %s` is unchanged and
    still prints for every report sent, now always from the sender; on the serial log it can print before the
    command's `cmd_ack` line, though the ack goes out first. `TELEMETRY_V2: TX queue full (24) - twin not sent`
    no longer prints, and `IOTHUB: health alerts held …` starts at 8 queued messages. No line was removed or
    reworded; the production tool matches none of these, and none prints a credential.
  - Memory (estimated from the objects): `.bss` +29 B, `.data` 0, flash about +0.8 KB; no IRAM; no new task,
    timer or allocation.
  - **Known, for the user's decision (HANDOFF §15p):** twin reports go at QoS 1, so on a slow or reconnecting link
    the MQTT client's own resend can still deliver an older report after a newer one (pre-existing).
- **Not yet folded in below:** the setup-page round of 2026-09-30 (`695283a` … `520b17a`: the page polls its
  network list only while it is used, an open page's scans leave BLE a 4 s window every 12 s, and the page's
  forget erases Wi-Fi with the station idle). The sections below still describe the 30 s / 15 s page chain and
  its `limit` line, which is gone, and, since WP2, the SoftAP stopping about 60 s after the IP address, the
  safety net at about 75-80 s in the portal window only, and MQTT restarting at the IP address (*Fixed*'s router
  retry, *Safety*'s portal window, *Upgrade notes*' serial-log lines); WP10 rewrites them.

### Fixed (2.1.3 field defects)

- **BUG-1: a valve with a flat battery was never rated critical, and an unknown battery read as 0 %.**
  - The valve now has its own battery bands:
    - ≤ 10 % is **critical**, with the reason "Valve battery critical";
    - 11–20 % is **warning**, with "Valve battery low";
    - above 20 % is excellent.

    A valve at 21–35 % used to read "good". These bands apply to the valve only: sensor battery bands are
    unchanged and never reach critical.
  - An unknown valve battery is `null` everywhere, never 0.
  - The valve state is `"unknown"` until the valve's characteristics have been read after a connect.
  - The battery is passed to the health engine on every read, not only when it changes, so a steady low
    reading is always rated.
- **BUG-2: removing one device wiped the health state of every other device.**
  - Removing a device used to reset the survivors' last-seen time, battery, RSSI and rating.
  - It also restarted the boot sync windows. As a result, "Boot sync: timeout" appeared 150 s after every
    removal, extra `boot`/`commission` snapshots were sent, and a genuinely offline sensor could be hidden for
    up to 10 minutes.
  - Now the device table is reconciled: survivors keep their state, only newly added devices get the sync
    window, and a removal arms no extra snapshot.
  - Removed devices are also cleared from:
    - the telemetry caches;
    - the rules engine's active-leak list. A removed wet sensor used to block `leak_reset` and make an
      override cancel or expiry re-close the valve.
- **BUG-3: a hub whose last device was removed published a stale snapshot.** That snapshot still showed the
  removed valve as open and connected, with "All devices healthy". The snapshot now shows only provisioned
  devices.
- **BUG-5: the valve block with no valve provisioned.** It was `{"state":"disconnected","connected":false}`,
  which looked like a real valve that had dropped off. It is now `{}`.
- **BUG-6: a hub with no devices went silent.**
  - Lifecycle, twin, alerts, the offline-buffer drain and every snapshot used to stop.
  - An empty hub now publishes like any other hub:
    - an `event` snapshot on the transition to empty;
    - `heartbeat` snapshots at the interval;
    - one `boot` snapshot per boot or MQTT (re)connect.
  - When the hub becomes empty, the rules engine's state (leak latch, override window) is reset, and so is the
    rules config (`auto_close_enabled` true, `trigger_mask` 7). Both are reset by the command that empties the
    hub, before its ack: the removal's own save writes the default config. A `provision` or `rules_config`
    sent straight after the removal is therefore never overwritten back to true / 7. A `provision` that itself
    leaves no device resets them too; rules keys in that payload apply on top of the defaults.
- **Found on the 2.1.4 bench, also in 2.1.3: after the 10 s auto-clear the cloud kept showing the valve
  locked.**
  - The hub publishes `rmleak_auto_cleared` and its snapshot as it writes RMLEAK=0, but the snapshot's
    `valve.rmleak` changes only when the valve reports the new value back, about 1 s later. That report
    requested no snapshot, so the snapshot read `"rmleak":true` and the next one came with the heartbeat (up
    to 300 s at the default interval).
  - The valve's report of a new RMLEAK value now requests an `event` snapshot. It follows the release
    snapshot within about 5 s (the minimum spacing between snapshots); when the report comes first, the
    release snapshot already reads `false` and is the only one. No event is added.
  - The same gap is closed for `leak_reset`, `override_enable`, an RMLEAK clear or re-assert at a reconnect,
    and an RMLEAK set on a valve that was already closed.
- **Found on the 2.1.4 bench, also in 2.1.3: after a router outage the hub did not rejoin its router, and the
  fallback portal's network list stayed empty.**
  - When the router goes away, the Wi-Fi manager retries 3 times and then opens its SoftAP as a router-outage
    fallback portal, with the credentials still saved. Its SoftAP start stops the retry timer (a local patch,
    `6ad1d7b`, against a scan race), and nothing else asked for a connect. On the 2026-09-29 bench the hub made
    one more attempt about 140 s later, then none, and it stayed offline after the router was back: a power
    cycle, a portal submit or the 10 s reset was the only way back.
  - While Wi-Fi is not connected, the continuous BLE scans (the 1M + Coded leak scan, and on a valve hub the
    valve hunt) leave a Wi-Fi scan almost no air time. On the same bench the fallback portal page stayed on
    "Scanning for networks..." with many networks in range, and the connect attempts, which start with a scan
    for the router, ended "no AP found" (reason 201).
  - **Router retry.** Whenever Wi-Fi is not connected, credentials are saved and the setup portal's window is
    not open, the hub asks the Wi-Fi manager to connect to the configured network once no attempt has started
    or ended for 30 s: about every 33-36 s. That covers the fallback portal, and also a hub that the Wi-Fi
    manager leaves idle with no SoftAP at all (see Known limitations). It rejoins within about 40 s of the
    router's Wi-Fi coming back, and the fallback SoftAP stops about 60 s after that. A retry is never sent while
    an attempt is in flight, nor before the Wi-Fi manager's own first attempt after boot, nor while the portal
    page is open (a submit there sends its own connect; the page counts as open until 10 s after its last
    network-list request), for at most 5 min after the last attempt. A failed retry starts no Wi-Fi manager
    retry and changes nothing in NVS; a successful one saves the credentials only if they changed (the Wi-Fi
    manager's rule).
  - **Wi-Fi radio holds.** While Wi-Fi is not connected, BLE scanning (the leak scan and the valve hunt) pauses
    around each Wi-Fi scan and each connect attempt, then resumes:
    - a connect attempt holds from its start until it fails or gets its IP, for at most 2.5 s; the rest of a
      longer attempt (with the router off an attempt takes about 5 s) runs with BLE on. The router retry's
      hold starts 0.5 s before its attempt and lasts 2.5 s in all. With the leak scanner's 500 ms loop BLE is
      off about 3 s at most, less than the 4 s burst a BLE leak sensor sends when it turns wet;
    - holds never run back to back, the page's scans apart: none starts while another is on or ended less than
      1.5 s ago, so BLE listens at least about 1 s between two;
    - a Wi-Fi scan (the portal page asks for one about every 3.8 s) holds until 4 s after it ends, at most 6 s,
      so an open page's holds chain and its list fills. The chain holds BLE for 30 s from its first scan; then
      BLE listens for 15 s with no hold of any kind (the page's scans, a submit's connect and the Wi-Fi
      manager's own attempts run with BLE on, and the list may fill more slowly; the router retry waits), and
      the page's next scan starts a new chain. A wet BLE sensor (a 2.5 s burst every 15 s) is therefore heard
      at least about every 45 s while a page is open.

    NimBLE stays up, a linked valve stays linked and takes its commands, and a leak close pended for a valve
    that is not linked still hunts it. Unlike the setup portal's window there is no health hold, no task
    priority raise and no `[PORTAL]` line: the sensors' 600 s and the valve's 180 s keep counting. No hold is
    set while Wi-Fi is connected, nor in the portal window (unchanged).
  - **Trade-off (user decisions, 2026-09-29 and 2026-09-30).** During a router outage BLE leak sensors go
    unheard for up to about 3 s at a time: once every 33-36 s with the router retry (about 7-9 % of the time),
    about 25-33 % of the time while the Wi-Fi manager retries on its own (the first 30-40 s of an outage, and
    a whole outage that follows a router flap), and 30 s of every 45 s or so while a portal page is open. See
    Known limitations for the leak latency this costs.
  - The UART log shows each pause and each retry (see *Serial log*).

### Safety

- **P0-a: the hub linked to any nearby eFloStop valve.**
  - With no valve provisioned (or a different one), the hub matched valves by their advertised name and paired
    with the fixed passkey. Auto-close, `valve_open` / `valve_close` and the override paths could then close
    or open a neighbour's valve.
  - Valves are now matched by the provisioned MAC only. A connection to any other valve is dropped at connect,
    before pairing, discovery or any command.
  - With no valve provisioned, every valve command is refused.
  - The valve identity on the wire (rules events included) is always the provisioned MAC.
- **P0-b: a hub with sensors and no valve never started BLE,** so it never heard its BLE leak sensors. BLE now
  starts when a valve **or** at least one BLE sensor is provisioned.
- **P0-c: commands issued with no valve were replayed on the next valve that linked.**
  - They are now refused.
  - When the provisioned valve changes or is removed, every queued, pending or in-flight valve command is
    discarded, and a link still up to the old valve is disconnected.
  - When the valve is removed, a connect in progress is also cancelled. When it is replaced by another valve,
    a connect already in flight to the old one is not cancelled: it completes, and the hub then drops it at
    GAP CONNECT, before pairing, discovery or any command (as in P0-a).
- **N1: leak protection waited for Wi-Fi.**
  - Provisioning, the rules and health engines, and BLE used to start only after the first IP address, then
    up to 120 s of SNTP and the DPS retries.
  - They now start at boot, without Wi-Fi. SNTP runs from the main loop without blocking it. DPS also runs
    from the main loop, but a live registration still blocks it for up to 60 s per attempt (see Known
    limitations). With NTP blocked, the loop polls every 2 s only during the initial 120 s sync window, then
    returns to its normal 30 s cadence.
  - Leak protection now runs before the clock is set, so it can raise events before the first clock sync:
    a `leak_detected`, the `auto_close` that follows it, a health alert. The hub used to discard every such
    event, because its `ts` would have been seconds since power-on. It is now kept in the offline buffer. As
    soon as the clock first syncs, its `ts` is corrected from the hub uptime (the synced clock minus the uptime
    elapsed since the event's `gateway.uptime_s`) and rewritten in flash; it goes out after the first connect,
    in order. Once stamped, it survives a restart like any other buffered event.
  - A pre-sync event still unstamped when the hub restarts (a restart before the clock synced, a software
    reset included) is dropped at replay, because its time cannot be known: which slots hold this boot's
    pre-sync events is tracked in RAM only.
  - Snapshots and lifecycle are still not sent before the first sync; they are regenerated after connect.
- **N1 follow-up: a phone could not join the Wi-Fi setup portal (found on the 2.1.4 bench, 2026-09-29).**
  - On the 2.1.4 development builds, after the 10 s Wi-Fi reset (or at first setup) on a hub with a valve or a
    BLE sensor, the phone listed the `WiFi-Hub-<short id>` network but could not join it (iOS: "Unable to join
    the network"), and the hub never gave a DHCP lease. BLE now starts at boot (N1, P0-b). Its continuous 1M +
    Coded leak scan, and on a valve hub the valve hunt and its connect attempts, took the radio from the SoftAP
    under Wi-Fi/BLE coexistence. 2.1.3 never started BLE in AP mode, so its portal had the radio.
  - **The portal priority window.** While the setup portal is up and no Wi-Fi credentials are saved (first
    setup, or after the 10 s reset erased them), the BLE leak scan and the valve hunt pause: nothing new
    starts, and a scan or connect attempt already running is cancelled within about 1 s. NimBLE stays up, and a
    valve already linked stays linked and takes its commands. There is no time cap before Wi-Fi is set up.
  - **The window closes when the setup SoftAP stops,** about 60 s after the hub gets an IP address from the
    router, and scanning resumes within about 1 s. The phone that submitted the credentials is still on the
    SoftAP during that minute and loads the portal's "Connected!" page from it; resuming the scan at the IP
    would starve the SoftAP again. BLE never stays paused while the hub is on Wi-Fi:
    - if the hub loses that Wi-Fi before the SoftAP stops, the window closes at once. The SoftAP then stays up
      as a router-outage fallback portal with the credentials saved, which keeps BLE scanning (apart from the
      Wi-Fi radio holds, see *Fixed*);
    - if the SoftAP is still up about 75 s after the IP (for example because the Wi-Fi manager did not start
      its 60 s shutdown timer), the hub stops it itself and the window closes, so scanning resumes at most
      about 80 s after the IP.
  - **Leak response outranks the portal.** While a leak close (RMLEAK, then CLOSE) is pended for a valve that is
    not linked, for example after a LoRa leak during setup, the valve hunt and its connect run anyway until the
    valve has taken it (RMLEAK first). The window then holds again, with the link kept.
  - **Health hold.** BLE sensors cannot be heard while scanning is paused. A BLE sensor that was online when the
    pause began stays online, and one not heard yet stays excused ("syncing"). Each gets a full 600 s from the
    moment scanning resumes before it can be declared offline or counted as unheard, so the pause itself raises
    no `device_offline`. A sensor already offline stays offline, and a BLE sensor that leaks during the pause
    is reported in its first burst after the resume.
  - **The valve is held the same way,** because the hub is not looking for it while the hunt is paused. A valve
    not linked yet stays excused ("syncing"), and one whose link drops stays in its "Valve disconnected"
    warning grace, until 180 s (its own offline timeout) after scanning resumes. A valve hub therefore no
    longer reads critical, with a red fleet LED, during Wi-Fi setup only because the hub stopped looking for
    its valve. A valve already offline stays offline, a dropped valve whose last battery reading was 10 % or
    less still reads critical, and the snapshot's valve `connected` and `last_seen_age_s` keep their real
    values.
  - **Except after a leak-close hunt that has not reached the valve.** While a pended leak close runs the valve
    hunt in the window (above), the hub is looking for the valve, so the valve's hold ends 180 s after that
    hunt started, with setup still running. A valve the hunt has not reached by then counts: one never linked
    counts as unheard (critical, red fleet LED, "Valve offline"), and one whose link dropped goes offline
    (`device_offline`) once its own 180 s grace has run too. That stays so after the leak clears and the hunt
    is held again, until the valve links. After the resume the earlier of the two ends applies. With no such
    hunt, setup shows no red for the valve (user decision, 2026-09-29).
  - The first-snapshot gate also waits while a BLE sensor or the valve has not been heard, for the whole pause
    and up to 180 s after it (the valve only while its hold lasts), so the first snapshot after setup does not
    list them as syncing. LoRa sensors are not held.
  - **Task priority.** The Wi-Fi manager task runs at priority 8 during the window (normally 5) and is set back
    to its configured priority when the window closes. The portal's HTTP and DNS server tasks are not raised.
  - **Not for the router-outage fallback portal.** The SoftAP the hub opens after it failed to rejoin its saved
    router has no window and no health hold: BLE keeps scanning there, paused only by the Wi-Fi radio holds of
    the router-rejoin fix (about 3 s at most each, or 30 s of every 45 s or so while a portal page is open; see
    *Fixed*).
  - **The 10 s Wi-Fi reset always erases the saved Wi-Fi credentials,** whatever the hub's Wi-Fi is doing:
    connected, idle on the router-outage fallback portal, or already in the setup portal. The Wi-Fi manager
    erases them only in its disconnect handler, which an idle connection never reaches, so a reset during a
    router outage used to reboot the hub with the old credentials, back onto the fallback portal. That portal
    keeps BLE scanning, so a phone may not be able to join it, and a hub whose router SSID or password had
    changed could not be set up again. The reset now also overwrites the saved SSID and password in NVS
    itself, with the empty values the Wi-Fi manager's own erase writes, and keeps the Wi-Fi manager from
    saving them back before the reboot. The hub comes back in the setup portal, with the portal priority
    window, about 1 s after boot. This is the recovery path when the router's SSID or password changes. The
    SoftAP's own settings and all provisioning (in `nvs_prov`) are kept, and no NVS key is added.
  - **Protection while the window is open** (no credentials saved, and the minute after setup). BLE leak
    sensors are not scanned, and a valve link that is lost is not re-found unless a leak close is pended for
    it. LoRa sensors, the rules engine and an established valve link keep working, so a LoRa leak still closes
    the valve. This is still more than 2.1.3 had: 2.1.3 started neither BLE nor the rules engine until the hub
    had a Wi-Fi IP address (N1), so it had no leak protection at all during setup.
  - The UART log now shows a phone joining and leaving the SoftAP, the window's edges, and the reset's erase
    (see *Serial log*).
- **The override window before the first clock sync.**
  - A window started before the clock synced (a valve long-press while the router is down) was stamped with
    an expiry in 1970. It ended the moment the clock synced, and with no internet it never expired at all.
  - It is now measured on the hub uptime until the clock syncs, then re-based to the synced clock and carried
    on (the rules tick does this within about 30 s of the sync). With no internet it expires after 24 h of
    uptime, through the normal expiry path (auto-close if a leak is still active, RMLEAK before CLOSE).
  - A window restored after a restart from an earlier unsynced power cycle cannot be re-based, because its
    elapsed time is unknown. Until the first sync it is timed from this boot's power-on, never beyond its
    stored expiry. At the first sync it ends, as in 2.1.3, which fails toward auto-close.
  - A window with a real expiry, restored after a power cut that lost the clock, used to block auto-close for
    as long as the site stayed without internet: its expiry could not be compared with anything. It is now
    timed from that power-on. It ends at its real expiry once the clock syncs, or 24 h after the power-on if
    the clock has still not synced by then, through the normal expiry path. A software reset in between does
    not restart that count. Meanwhile `override_remaining_s` and `previous_remaining_s` count down from the
    power-on, and `auto_close_blocked_override` carries `override_remaining_s` (it was omitted).
  - `water_access_override_enabled` omits `expires_ts` while the clock is unsynced (it used to carry the 1970
    instant). `override_remaining_s` (snapshot, `auto_close_blocked_override`) and `previous_remaining_s`
    (`auto_close_reenabled`) are measured on the uptime meanwhile. NVS keeps its keys and their meaning: the
    uptime basis is RAM only, and after the re-base `ovr_expiry` holds a real epoch.
- **N2/N3: the valve's first link-up could be lost.** The queue set is now complete before BLE starts, and every
  queue-set add is checked.
- **N4: packets from LoRa sensors that are not provisioned were evaluated by the rules engine.**
  - They are now checked against provisioning first.
  - A busy provisioning read counts as "provisioned", so a real leak is never dropped.
- **A hub that boots empty clears any persisted leak latch and override.** Without this, a valve provisioned
  later could be blocked from auto-closing for 24 h by an inferred override.
- **A leak on a hub with no provisioned valve no longer publishes `auto_close`.** There is no valve to close,
  and the event reported a shut-off that never happened. `leak_detected` still goes out and the incident still
  latches (when `auto_close_enabled` and the source's trigger bit are set). If provisioning is busy at that
  moment, the hub assumes a valve is present and publishes it. After a valve decommission, auto-close also no
  longer counts the old valve's link, still up until its DISCONNECT, as a reachable valve.
- **`valve_open` and `valve_set_state` open are refused while a leak incident is latched** and no override
  window is active, with the RMLEAK refusal, even when the valve is disconnected or its RMLEAK reads clear. An
  open sent while the valve was out of range used to be accepted, held, and written at the reconnect ahead of
  the close the leak was owed.
- **A close owed to an unreachable valve is held for its reconnect.** When a leak, an `override_cancel` or an
  override expiry with a leak still active needs the valve closed while it is out of range, the hub now queues
  RMLEAK and then CLOSE for the reconnect, replacing any open held for it. It used to start a scan only. If
  every source is dry before the reconnect, both are cancelled.
- **Replacing or removing the valve drops the old valve's leak reading.** A flood seen by the old valve used to
  survive the change: the new, dry valve was closed on its first link (a false `auto_close` with
  `cause:"reconnect"`), or the latched incident was read as a physical override and blocked auto-close for
  24 h. Now the old valve's leak source is forgotten, and when nothing else is wet the incident latch is
  released at once, with no `rmleak_auto_cleared` event and no RMLEAK write. With a sensor still wet the latch
  stays, and the new valve is closed on its first link.
- **A valve re-provisioned right after its decommission is always linked.** A `provision` naming a valve now
  always requests the valve link. One sent before the old link's DISCONNECT used to leave the hub not
  rescanning after the valve's next drop, so the valve went "Valve offline" until a leak or a valve command.
- **A leak that re-latches as the interlock is being released always re-locks the valve.** The rules tick runs
  before the report that woke `iothub_task` is handled, and the hub's RMLEAK cache reads clear only once the
  valve's read-back of the clear lands. A sensor reporting wet again in that moment (just as the auto-clear
  fired, or just after a `leak_reset`) latched a new incident but found the valve "closed with RMLEAK set" and
  did nothing. When the clear then landed, the hub read its own clear as a valve-button override
  (`RMLEAK cleared externally (valve override) — starting 24h override window`) and blocked auto-close for
  24 h while the sensor was wet. A newly latched incident now always re-asserts RMLEAK and CLOSE, queued
  behind the clear so the valve ends locked, and publishes `auto_close`. The race predates 2.1.4.
- **The hub's own RMLEAK clear is no longer read as a valve-button override.**
  - When the hub releases the interlock itself (the 10 s auto-clear, `leak_reset` or `override_enable`) while
    the valve is out of range or still setting up its link, the clear is held for the reconnect. At the
    reconnect the valve still reports its old RMLEAK for a moment, until the held clear is written and read
    back. The hub read that as "valve locked, no incident" and re-latched the incident. When its own clear
    then landed, it read it as a press of the valve button and started a false 24 h
    `water_access_override_enabled{trigger:"button"}` window that blocked auto-close.
  - The hub now remembers, in RAM, a clear it owes the valve until it reads RMLEAK clear back. At the
    reconnect it sends the clear again instead of re-latching, and the clear landing starts nothing. After a
    hub restart that memory is gone and the reconnect re-latches, as in 2.1.3 (fail closed); the 10 s
    auto-clear then releases it once every source is dry.
  - A live button override is now read only when the valve was seen with RMLEAK set during this incident and
    it then goes clear. A lock that never reached the valve (a close that never landed) no longer produces
    `trigger:"button"`.
  - `leak_reset` and the auto-clear queue their clear while they hold the rules lock. A re-latch on another
    task can therefore no longer queue its RMLEAK and close ahead of the clear, which then landed last and
    read as a button press.
  - The inference a reconnect makes from "valve open, RMLEAK clear" (SRS §4.4.2, a press while the hub was
    offline) is unchanged; see Known limitations.

### Changed

- **The RMLEAK interlock auto-clears 10 s after every leak source is dry (it was 30 s).**
  - `AUTO_CLEAR_TIMEOUT_MS` is 10 s. While an auto-clear is pending, `iothub_task` polls every 2 s instead of
    idling for up to 30 s, so the interlock is released about 10–12 s after the last leak source reports dry.
    It used to take 30–60 s: the 30 s dwell plus up to one 30 s idle wait.
  - Only RMLEAK is lifted. The valve never reopens by itself; `valve_open` or the valve button opens it.
  - `rmleak_auto_cleared` carries `clear_after_seconds:10`.
  - The amber "Leak interlock latched" floor after a leak dries now lasts about 10–12 s (it was 30–60 s), on
    the hub rating and on the fleet LED, which follows it.
  - A sensor that goes wet again 10–30 s after drying (up to about 60 s, counting the old idle wait) now
    produces a full clear and re-latch cycle (`rmleak_auto_cleared`, then `auto_close` again), where the old
    dwell used to hold the interlock. The valve stays closed throughout, including for a re-wet at the very
    moment of the clear (see Safety). That re-wet also sends `rmleak_auto_cleared` and then `auto_close`
    (see *Rules events* under Reliability).

### Reliability

- **Valve writes.**
  - A valve or RMLEAK write that the BLE stack refuses is retried up to 3 times, 200 ms and then 400 ms apart.
  - "GATT busy" is not a failure. NimBLE has 4 GATT procedures, and a command write that finds them all in use
    (`BLE_HS_ENOMEM`, rc=6) is retried every 250 ms for up to 5 s without using up one of the 3 attempts. Its
    read-back waits the same way, so the hub's RMLEAK cache is not left stale (a stale clear could read as a
    button override). If the pool is still full after 5 s, the command stays pending and is replayed on the
    same link; it never forces a reconnect. Before this, the RMLEAK and close queued right after a
    reconnect's own replays could find the pool full and force a reconnect, so the valve closed a leak one or
    more 15–40 s reconnect cycles late. A valve that stops answering is still dropped by NimBLE's 30 s GATT
    timeout (that bound applies when the busy error comes from the procedure pool; a busy error from ATT
    buffer exhaustion is retried every ~5 s for as long as the link stays up).
  - After that, the link is dropped and the command is re-applied when the valve reconnects. It used to be
    lost.
  - Forced reconnects are capped at 3 in a row. After that a failed write stays pending for the next natural
    reconnect, and the cap is lifted once a live command write succeeds with nothing left pending (or the
    valve target changes). A command replayed at a reconnect no longer lifts it, so a valve that accepts the
    replays but refuses live writes still reaches the cap.
  - A re-applied command that fails again at reconnect is replayed ahead of any newer command, never after
    it, so it can no longer undo a newer command. A command cancelled or superseded meanwhile is not
    replayed. A newer command written during the reconnect is never overwritten by an older pending one.
  - RMLEAK is applied before the valve command, on every path. One exception, once the reconnect cap is
    reached (or the link could not be dropped): if the RMLEAK write itself keeps failing on a live link, an
    open waits behind it, but a close is still written, because holding a close back during a leak is worse.
  - That order also holds across the end of the link setup. A valve command queued while the link was
    finishing its setup could be written ahead of an RMLEAK command held for that link; it now waits behind
    it. While a held RMLEAK write waits for a busy GATT pool, a held close waits behind it too.
  - A disconnect the controller refuses (`BLE_GAP_EVENT_TERM_FAILURE`) no longer leaves valve commands
    blocked for the rest of the link.
  - A command held for the next link just as the current link finished its setup is replayed on that link at
    once. It could sit until the next reconnect.
  - BLE start-up (`nimble_port_init`) is retried up to 5 times, and each failed attempt now releases the
    NimBLE porting-layer memory it allocated.
  - BLE start requested by two tasks at once (a `provision` and the boot-time valve apply) starts the stack
    once: the start signal is claimed atomically, and the start-up task clears its handle before it exits.
- **Rules events.** A rules event raised by the rules tick (`rmleak_auto_cleared`,
  `water_access_override_expired`, or a button `water_access_override_enabled`) is now published straight
  after the tick, before the same pass handles a leak report. The rules engine holds one pending event, and a
  wet report handled in that pass replaced it with its `auto_close`, so the release never reached the cloud.
  A sensor that goes wet again just as the interlock auto-clears now gives `rmleak_auto_cleared`, then
  `leak_detected` and `auto_close`. The one exception is the pass in which MQTT (re)connects: its offline
  replay and lifecycle go first, and there the event can still be replaced as before. A multi-slot buffer is
  still deferred.
- **Health.**
  - A debounced alert is sent once the debounce has passed, instead of being dropped.
  - A change of the hub rating to, from or within warning and critical with no device event (for example the
    end of the sync window, or a valve battery going critical) publishes an `event` snapshot within seconds,
    instead of waiting for the next heartbeat. So does every valve battery edge. A change between excellent
    and good (a sensor's signal hovering near the weak threshold) waits for the heartbeat, as in 2.1.3.
  - Valve health events carry the MAC of the link they came from. An event from the old valve still queued
    when the valve is replaced is dropped, instead of rating the new valve (for example "Valve battery
    critical", which refuses `valve_open`, or healthy before it ever linked).
  - The valve re-check after a device-set change can raise the valve's leak in health but never clear it. A
    disconnect racing it could show a wet valve as dry for 180 s.
  - Check-ins from devices not in the health table (a sensor just removed, a neighbour's LoRa sensor) no
    longer trigger post-provision `prov_pkt` snapshots.
- **Snapshot.**
  - A snapshot is not published when the health table is busy or memory runs out. It used to publish empty
    device arrays, or a partial document. A snapshot missing any key because an allocation failed is not
    counted as sent either: it is rebuilt whole at the 5 s retry.
  - A snapshot that falls due in the same pass as a C2D `provision` or `decommission` waits one pass (a few
    milliseconds) for the device-set change, so it no longer shows the old device table after the `ok` ack.
- **Out of memory, events.** An event or lifecycle message whose `gateway` or `data` object cannot be
  allocated is dropped, and logged, instead of going out without it. A sub-object that fails to attach is
  freed instead of leaked.
- **Provisioning.**
  - Changes are transactional: a failed NVS save restores the previous configuration instead of leaving RAM and
    flash disagreeing.
  - Duplicate sensor ids in a `provision` payload are ignored.
  - Removing a valve from a hub that has none now reports an error.
  - Applying the provisioned valve to BLE reads and sets the valve target in one provisioning lock hold. A
    retry of an apply that found provisioning busy can therefore never re-target a valve that a C2D
    `provision` or `decommission` removed or replaced meanwhile.
  - A corrupt sensor count in flash is clamped to 16 at load (and logged) instead of overrunning the device
    lists, and every stored MAC string is terminated. Nothing is erased and no NVS data changes.
  - After a device-set change that adds devices, the BLE sensor scanner forgets what it last reported, so a
    sensor removed and re-added within its 10 s whitelist refresh is evaluated again. Re-added wet that way,
    it used to raise no `leak_detected` and no auto-close for up to 5 minutes, and the latch auto-cleared
    while it was still wet.
  - A telemetry-cache purge skipped because provisioning was busy is retried, and owed device-set work is
    polled every 2 s instead of every 30 s.
- **Sensor metadata** is copied out under its lock. Callers used to hold a pointer into the table after the lock
  was released.
- **Offline buffer.**
  - It is protected by a mutex.
  - An event too large for it is refused, instead of being cut into invalid JSON.
  - It is cleared after a decommission-all.
  - A pre-sync event that would grow past 512 B once time-stamped is not written back to flash stamped. It is
    stamped in RAM when it is sent, so every entry stays readable by 2.1.3 after a rollback. If the hub
    restarts between the clock sync and that send, such an event is dropped.
- **Twin reported** is refreshed after every device-set change. A decommission used to leave the twin naming the
  removed device.
- **The UART log no longer prints the site Wi-Fi password** (it came from the Wi-Fi provisioning component's
  INFO logging), **nor the valve's fixed BLE passkey** (it was printed at every BLE start and on pairing).

### Wire changes

Telemetry (`eflostop.v2`):

| Where | Up to 2.1.3 | 2.1.4 |
|---|---|---|
| snapshot `data.valve`, no valve provisioned | `{"state":"disconnected","connected":false}` | `{}` |
| snapshot `data.valve.valve_id` | the MAC of whichever valve was linked | always the provisioned valve's MAC; live fields only from a link to that MAC |
| snapshot `data.valve` before its readings are in | `battery` 0, cached state | `state` `"unknown"`, `battery` `null`, `fw_version` `null` |
| `battery` when unknown: snapshot valve, `valve_state_changed`, leak events | `0` | `null` |
| `system_health.reason` | "All devices healthy" on the one stale snapshot of an emptied hub; "Health data unavailable" on a busy health table | "No devices provisioned" (rating `excellent`); "Valve battery critical" (rating `critical`); "Health data unavailable" is no longer sent |
| hub with no devices | no lifecycle, twin or snapshots | lifecycle (`provisioned:false`), twin, and snapshots: `event` / `heartbeat` / `boot` |
| `device_offline` / `device_recovered` | sent on every non-leak critical edge, named from the rating alone; a debounced alert was dropped | reachability only: `device_offline` is always a lost link; no event for a battery- or leak-driven critical; `device_recovered` may carry `rating:"critical"` (back into a leak); `prev_rating` may equal `rating` (an alert sent after its debounce) |
| rules events `valve_id` | the linked valve's MAC while one was linked, else the provisioned one | always the provisioned valve's MAC |
| events raised before the first clock sync | discarded, never sent | sent after the first connect, in order, `ts` corrected from the hub uptime when the clock first syncs; one left by a restart before the sync is dropped. `ts` is never below 1704067200 |
| `water_access_override_enabled.expires_ts`, window started before the clock synced | an instant in 1970 | omitted (the expiry is about `ts` + `remaining_s`) |
| `override_remaining_s` / `previous_remaining_s`, window started before the clock synced | full duration, omitted or 0 | measured on the hub uptime; snapshots omit `expires_ts` until the window is re-based, within about 30 s of the sync |
| `override_remaining_s` / `previous_remaining_s`, window with a real expiry restored after a power-on that lost the clock | full duration (snapshot), omitted (`auto_close_blocked_override`) or 0 (`auto_close_reenabled`) | counts down from the power-on until the clock syncs; the snapshot omits `expires_ts` meanwhile |
| `water_access_override_expired`, same window | not sent until the clock synced | also sent about 24 h after the power-on if the clock has not synced by then |
| snapshot `data.valve.last_seen_age_s` | seconds since the valve's last changed value or connect, so it grew for hours on a steady, connected valve | `0` while the valve's link is up; seconds since the link dropped once disconnected; `null` if never seen this uptime |
| `device_offline.offline_duration_s`, valve | from the valve's last changed value, plus the 180 s grace: hours on a steady valve | from the link drop, so normally about 180 |
| `auto_close` on a hub with no provisioned valve | sent, with `rmleak_asserted:false` | not sent; `leak_detected` still is |
| `rmleak_auto_cleared.clear_after_seconds` | `30`; RMLEAK lifted 30–60 s after the last leak source read dry | `10`; RMLEAK lifted about 10–12 s after the last leak source reads dry |

Commands (`C2D_COMMANDS.md` §4.1–4.3 and §6.1). The `detail` strings are exact:

| Command | Up to 2.1.3 | 2.1.4 |
|---|---|---|
| `valve_open`, `valve_close`, `valve_set_state` with no valve provisioned | `ok`, and the hub then drove any nearby valve | `error`, "No valve is set up for this hub." |
| `valve_open`, `valve_set_state` open, with the last real valve battery ≤ 10 % | `ok` (the valve stayed shut) | `error`, "Valve battery critical (≤10 %): the valve will not open. Replace the batteries." |
| valve command when the valve command queue is full | `ok` | `error`, "The valve command could not be queued. Try again." |
| `decommission` `{"target":"valve"}` on a hub with no valve | `ok` | `error`, "valve decommission failed" |
| `valve_open`, `valve_set_state` open, while a leak incident is latched and no override window is active, with the valve disconnected or its RMLEAK clear | `ok`; the open was written (held for the reconnect when the valve was disconnected) | `error`, "Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak." |

The RMLEAK refusal's text is unchanged, and it is still sent while the valve's RMLEAK is asserted. The refusal
checks run in this order: no valve, then RMLEAK asserted or an incident latched, then the battery, then the
queue. An `ok` from a valve command still means "queued"; the valve's own report (`valve_state_changed`, the
next snapshot) confirms it.

### Upgrade notes

- **Upgrading from 2.1.3 keeps provisioning.** There is no change to any NVS namespace, key or layout, nor to
  the partition table. (Neither 2.1.3 nor 2.1.4 contains an OTA client; an upgrade that rewrites only the app
  image, such as `idf.py app-flash` or a full `idf.py flash`, which leaves the `nvs` and `nvs_prov`
  partitions alone, keeps everything. Never `erase-flash`.) The valve, sensors, sensor metadata, rules, hub name, DPS cache and snapshot interval all
  carry over.
- **Rolling back to 2.1.3 keeps provisioning too,** for the same reason. The 2.1.3 behaviour returns with it,
  including P0-a. A pre-sync event still in the offline buffer at the rollback and not yet time-stamped (the
  clock never synced, the entry was too close to 512 B to be stamped in flash, or its stamp could not be
  written) is replayed by 2.1.3 unchanged, with a `ts` in 1970; one already stamped keeps its real
  `ts`. 2.1.4 never writes a stamped entry back longer than 512 B, so 2.1.3 can read every entry. An override
  window re-based by 2.1.4 is stored with a real epoch, so 2.1.3 restores it with the right remaining time.
- **A hub that boots with no devices** clears any persisted leak latch and override window on that boot.
- **Cloud and app parsers must accept:**
  - `data.valve` equal to `{}`;
  - `battery: null` on the snapshot valve, `valve_state_changed` and leak events;
  - snapshots and lifecycle from a hub with no devices;
  - `device_recovered` with `rating:"critical"`;
  - `prev_rating` equal to `rating`;
  - the new valve-command error acks, and the RMLEAK refusal while a leak incident is latched;
  - `data.valve.last_seen_age_s` of `0` for a connected valve;
  - no `auto_close` from a hub with no valve: a leak there sends `leak_detected` only;
  - `water_access_override_expired` sent before the first clock sync, so it arrives late like any pre-sync
    event;
  - events that arrive late, after the first connect, with a `ts` earlier than that connect's lifecycle
    message (events raised before the first clock sync);
  - `water_access_override_enabled` without `expires_ts`, and a snapshot with `override_active:true` and no
    `expires_ts` for up to about 30 s after the first clock sync;
  - `data.valve.rmleak` and `data.valve.leak_state` reading `false` while `data.valve.state` is `"unknown"`
    (the link is up but the valve's readings are not in yet), even on a valve locked after a leak. Ignore
    both until the state is known;
  - lifecycle and twin reported with `provisioned:true` and no devices, from a hub that a `provision` emptied
    (a hub emptied by removals reports `false`).
- **Serial log.**
  - Unchanged: every line the production tool and the bench scripts match.
  - A refused valve command now logs its full reason: `VALVE_OPEN refused — <detail>`, and likewise for
    `VALVE_CLOSE` and `VALVE_SET_STATE open|closed`. It used to print `... — valve RMLEAK is asserted`.
  - The fixed BLE passkey is no longer printed. Changed lines (`BLE_VALVE`):
    - `[PASSKEY] INPUT required. Responding with the fixed passkey`
    - `[PASSKEY] DISPLAY action. Responding with the fixed passkey`
    - `[SM] Fixed Passkey: configured (not logged)`
  - New, pre-sync events. `TELEMETRY_V2`: `Time not synced (ts=%ld) - holding %s for replay; stamped when the
    clock syncs` (snapshot and lifecycle keep `Time not synced (ts=%ld) — suppressing %s`). `OFFLINE_BUF`:
    - `Stamped pre-sync event [%s] at clock sync: ts=%lld (%lld s ago)`
    - `Stamped pre-sync event [%s]: ts=%lld (%lld s before this replay)` (only when the stamp at the sync
      could not be written)
    - `Dropped a buffered event from an earlier power cycle that was never time-stamped [%s]` (also after a
      software restart before the clock synced)
    - `Dropped a buffered pre-sync event [%s] - cannot be time-stamped from its uptime_s`
    - `Dropped a buffered pre-sync event [%s] - too long once time-stamped`
    - `Clock not synced - pre-sync event [%s] and %d after it kept for the next drain`

    The last three should never appear in a normal run.
  - New, override window (`RULES_ENGINE`):
    - `Override window stamped before clock sync - timed on uptime (start=%lus) until the clock syncs`
    - `Override window re-based to the synced clock (expiry=%ld, remaining=%lds)`
    - `Override window ran its full duration before the clock synced (elapsed=%lus) - expiring it now`
    - `Override window was stamped before a clock sync in an earlier boot - elapsed time unknown, expiring it now`
    - `NVS: that window was stamped before a clock sync - elapsed time unknown, not restored`
  - New, provisioning (`PROVISIONING`):
    - `LoRa sensor count %u in NVS exceeds %d - clamped`
    - `BLE leak sensor count %u in NVS exceeds %d - clamped`
    - `Failed to take mutex in with_valve_target`
  - New, valve command replay and reconnects (`BLE_VALVE`). Each replay is announced by `[CMD] Replaying
    pending ...` and then logs the usual `[CMD] Writing ...`, so a script counting `[CMD] Writing` as live
    commands also counts replays.
    - `[TASK] CMD: REPLAY_PENDING`
    - `[CMD] Replaying pending %s command=%d`
    - `[CMD] Replay of pending valve commands dropped - the valve target changed`
    - `[CMD] Pending %s command=%d cancelled or superseded meanwhile - not applied`
    - `[CMD] Pending %s command=%d cancelled, superseded or flushed meanwhile - not replayed`
    - `[CMD] Pending %s command=%d kept for the next link`
    - `[CMD] Pending valve command=1 kept behind the RMLEAK command`
    - `[CMD] Pending valve commands not applied - left to the replay already queued`
    - `[CMD] Pending valve commands not applied - replay queued ahead of newer commands`
    - `[CMD] Pending valve commands not applied, command queue full - kept for the next link`
    - `[CMD] Valve=%u not written - kept behind the pending RMLEAK command`
    - `[CMD] valve writes keep failing - no more forced reconnects until a write succeeds`
    - `[CMD] valve write failed %d times (rc=%d) - kept for the next link, no forced reconnect (%s=%u)`
    - `[DISCONNECT] terminate failed status=%d - link stays up (handle=%u)`
  - New, review fixes:
    - `RULES_ENGINE`: `Override window restored after a power-on has run its full duration with no clock sync - expiring it now`
    - `RULES_ENGINE`: `AUTO-CLOSE: no provisioned valve - auto_close event not published`
    - `RULES_ENGINE`: `Valve replaced: old valve leak source %s, %u source(s) still wet, incident %s`, on every
      valve replacement or removal (`dropped` or `not tracked`; `released`, `kept` or `not latched`)
    - `RULES_ENGINE`: `Failed to take mutex (valve replaced)`
    - `HEALTH_ENGINE`: `Valve event from %s dropped - the table's valve is %s`
    - `BLE_VALVE`: `[CMD] %s val=%u pended as the link became ready - replaying it`
    - `TELEMETRY_V2`: `Message not built (%s) - out of memory`
    - `IOTHUB`: `Telemetry-cache purge: provisioning busy, retrying`
    - `PROVISIONING`: `Hub empty: rules config reset to defaults`, after the removal or `provision` that
      empties the hub
  - New, council fixes:
    - `RULES_ENGINE`: `Reconnected: valve RMLEAK active, hub incident clear - RMLEAK clear owed by the hub, not re-latching`
    - `RULES_ENGINE`: `RMLEAK clear read back - the hub's own clear, not a valve override` (only when an
      incident is latched again before the hub's own clear is read back; a plain clear across a relink ends
      silently, or with `Reconnected: no active incident, valve clear`)
    - `RULES_ENGINE`: `RECONNECT: RMLEAK clear not sent - no provisioned valve` (warning) and `RECONNECT:
      RMLEAK clear enqueue FAILED — valve interlock left set` (error)
    - `BLE_VALVE`: `[CMD] %s write: GATT busy - waiting` and `[CMD] %s read-back: GATT busy - waiting`, once
      per wait
    - `BLE_VALVE`: `[CMD] %s=%u kept pending - GATT still busy, replaying it`, after a 5 s wait
    - `BLE_VALVE`: `[CMD] %s=%u held behind the pending RMLEAK command`
    - `BLE_VALVE`: `[CMD] Pending valve command=0 kept behind the RMLEAK command (GATT busy)`

    `GATT busy - waiting` is normal when a reconnect replays commands while new ones are queued. None of
    these should repeat for long.
  - New, Wi-Fi setup portal (the portal priority window, see *Safety*):
    - `APP_WIFI`: `portal priority ON (no Wi-Fi credentials) - BLE scanning paused` (warning), then `portal
      priority: wifi_manager task prio %u -> %u (httpd, dns_server not raised)` (`5 -> 8`)
    - `APP_WIFI`: `portal priority: Wi-Fi connected - BLE scanning stays paused until the setup AP stops (about
      %d s)` (`60`), right after `Connected! IP: %s` when Wi-Fi is set up in the window
    - `APP_WIFI`: `portal priority OFF (%s) - BLE scanning resumed`: `AP stopped` (normally about 60 s after
      `Connected! IP`), or `Wi-Fi lost after setup`
    - `APP_WIFI` (warning): `portal priority: setup AP still up %u s after Wi-Fi connected - stopping it` (the
      safety net; should never appear)
    - `APP_WIFI`: `SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on`
    - `APP_WIFI`: `SoftAP: station %02X:%02X:%02X:%02X:%02X:%02X joined, AID=%u` and `SoftAP: station
      %02X:%02X:%02X:%02X:%02X:%02X left, AID=%u, reason=%u`, on any SoftAP, window or not
    - `HEALTH_ENGINE`: `BLE scanning paused - BLE sensor timeouts held` and `BLE scanning resumed - BLE sensor
      timeouts restart now (%d s)` (`600 s`). They name only the BLE sensors, but the valve's 180 s is held
      and restarts with them.
    - `HEALTH_ENGINE`: `Valve hunt for a pended leak response during the scan pause - valve timeouts count from
      now (%d s)` (`180 s`), once, right after `[PORTAL] Leak response pending …` or `[PORTAL] Valve hunt not
      paused …`: the valve's hold ends 180 s later (see *Safety*). In the same window it prints again only
      after the valve has linked and dropped.
    - `BLE_LEAK`: `Scan paused - Wi-Fi setup portal has the radio` and `Scan resumed - Wi-Fi setup portal closed`
    - `BLE_VALVE`: `[SCAN] Valve scan held - Wi-Fi setup portal has the radio`
    - `BLE_VALVE`: `[PORTAL] Valve hunt paused - Wi-Fi setup portal has the radio` (followed by ` (valve link
      kept)` when the valve is linked), `[PORTAL] Valve hunt stopped - Wi-Fi setup portal has the radio` and
      `[PORTAL] Valve hunt resumed - Wi-Fi setup portal closed`
    - `BLE_VALVE` (warnings): `[PORTAL] Leak response pending - valve hunt runs despite the Wi-Fi setup portal`
      and `[PORTAL] Valve hunt not paused - a leak response is pending`
    - `BLE_VALVE`, when the window opens as a hunt or a connect is starting or running: `[PORTAL] Valve scan
      cancelled - Wi-Fi setup portal opened`, `[PORTAL] Valve connect cancelled - Wi-Fi setup portal opened
      (rc=%d)` and `[PORTAL] Valve connect in flight cancelled (rc=%d)`
    - `BLE_LEAK`: `Scan cancel failed: %d, will retry` (warning; should never appear)

    `portal priority ON` must never appear on the router-outage fallback portal.
  - New, the 10 s Wi-Fi reset (see *Safety*). `RESET_BTN` prints one outcome line about 2 s after `Erasing
    WiFi credentials, then rebooting into AP (commissioning preserved in nvs_prov)...`, just before
    `Rebooting into AP mode...`:
    - `Wi-Fi credentials erased from NVS`
    - `No Wi-Fi credentials saved - nothing to erase` (nothing was ever saved)
    - `Wi-Fi credential erase failed (%s) - rebooting anyway` (error; should never appear)

    The warning `Wi-Fi NVS lock busy for 3 s - erasing without it` can come before it and should never appear
    either. `APP_WIFI` `WiFi Disconnected. Reason: 8` between `Erasing WiFi credentials …` and the outcome
    line still shows that the hub was connected when the button was pressed. With an idle connection it does
    not print, and the credentials are erased all the same.
  - New, the router retry and the Wi-Fi radio holds (see *Fixed*), all `APP_WIFI`:
    - `Wi-Fi radio hold ON (%s) - BLE scanning paused`, with `connect attempt`, `router retry` or `Wi-Fi scan`,
      within about 1 s of the pause starting (for the router retry, 0.5 s before its retry line)
    - `Wi-Fi radio hold OFF after %u s - BLE scanning resumed`, within about 1 s of the pause ending (the
      duration is rounded to the second, ±1 s): 1-4 s for a connect attempt or a router retry, 30 s for an
      open portal page's chain. Pauses that chain, as an open page's scans, print one pair.
    - `Wi-Fi radio hold: %d s limit for the setup page's scans - BLE listens %d s with no hold` (`30`, `15`),
      right after that `OFF` line when an open page's chain reaches its limit (also after a connect attempt's
      `OFF` when a late page request starts the listening time); no `ON` line follows for about 15 s
    - `router fallback: retrying the configured network (attempt %u)`, counted from 1 after each loss of Wi-Fi
      (the count restarts when Wi-Fi connects or the setup window opens), on the fallback portal or with no
      SoftAP. "Configured", not "saved": after a portal submit that failed, the retry uses what was typed.
    - `router fallback: retry deferred - the Wi-Fi setup page is open`, once for each page session in which a
      retry falls due

    None prints while Wi-Fi is connected, apart from the `OFF` line within about 1 s after `Connected! IP`, and
    none in the portal window, apart from at most one pair right at its opening. A boot with saved credentials
    normally prints one `connect attempt` pair near `Connected! IP` (its `OFF` can come first: the pause lasts
    at most 2.5 s).
  - Same text, new conditions (portal window):
    - `HEALTH_ENGINE` `Boot sync: timeout (%lu s) — snapshot gate open; unheard devices still excused for a
      further %lld s`: not while BLE scanning is paused with a BLE sensor or the valve not yet heard, nor
      within 180 s after the resume; the further excuse of such a device then counts from the resume. The valve
      keeps the gate shut only while its hold lasts, which a hunt for a pended leak close in the window ends
      180 s after that hunt.
    - `HEALTH_ENGINE` `Roll-up grace expired (%lu s) — %d unheard device(s) now count`: for a BLE sensor, never
      earlier than 600 s after scanning resumed; for a valve not linked yet, never earlier than 180 s after it,
      unless a hunt for a pended leak close in the window has not reached it: then 180 s after that hunt, with
      setup still running.
    - `BLE_LEAK` `Extended passive scan started (1M + Coded PHY)` and `BLE_VALVE` `[SCAN] Starting scan for
      provisioned valve %s...`: not while the window is open, except the valve hunt for a pended leak close.
  - Same text, new conditions (the router retry and the radio holds):
    - `APP_WIFI` `SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on`: the text is
      kept for the bench scripts, but BLE now pauses for the fallback portal's Wi-Fi scans and connect attempts.
      The router retries follow it (they also run with no SoftAP, see Known limitations).
    - `BLE_LEAK` `Extended passive scan started (1M + Coded PHY)`, and `BLE_VALVE` `[SCAN] Starting scan for
      provisioned valve %s...` while the valve is not linked: also after each radio hold.
    - `APP_WIFI` `WiFi Disconnected. Reason: %d` (`201` while the router is off) and `NET_STATUS` `wifi=0
      mqtt=0`: also after each failed router retry.
    - `BLE_LEAK` `Scan resumed - Wi-Fi setup portal closed`: when scanning really resumes, so after `portal
      priority OFF` if a radio hold is still running then. `BLE_VALVE` `[PORTAL] Valve hunt resumed - Wi-Fi
      setup portal closed` still prints at the window's edge; if a radio hold is still running then, the hunt
      restarts when it ends.
    - `BLE_VALVE` `[SCAN] Valve scan held - Wi-Fi setup portal has the radio` and the `[PORTAL]` cancel lines:
      for the portal window only, as before. A radio hold holds the valve hunt and cancels a valve connect
      silently; a cancelled connect still prints the warning `[CONNECT] Failed status=%d`.
  - Same text, new conditions (council fixes):
    - `RULES_ENGINE` `RMLEAK cleared externally (valve override) — starting 24h override window`: only when
      the valve was seen with RMLEAK set during the incident and no hub clear is owed.
    - `RULES_ENGINE` `Reconnected with %d active leak(s) — valve already closed + RMLEAK asserted, nothing to
      do`: not while a hub clear is owed; `Valve reconnected with %d active leak(s) — executing auto-close`
      prints instead.
    - `BLE_VALVE` `[CMD] Writing %s=%u` and `[CMD] %s write rc=%d (value awaits the valve's own report)` are
      not printed for a retry that polls a busy GATT pool; after a busy poll only a result other than rc=6
      prints. Every other attempt logs both as before.
    - `BLE_VALVE` `[CMD] %s read-back rc=%d - %s unconfirmed`: also when a read-back's 5 s busy wait runs out
      (rc=6) or its lock wait times out (rc=-1).
    - `BLE_VALVE` `[CMD] valve writes keep failing - no more forced reconnects until a write succeeds`: now
      until a live command write succeeds.
    - A rules event raised by the tick logs its `Pub event: ...` (or offline-buffer) line right after the
      tick, before the pass's leak-report lines.
  - Changed number, RMLEAK auto-clear (`RULES_ENGINE`): these print `10s` where they printed `30s`. The
    production tool matches neither; `docs/health_leak_led/TEST_PLAN.md` still quotes the 30 s values.
    - `All sensors clear — auto-clear timer started (%ds)`
    - `AUTO-CLEAR: all sensors clear for %ds — clearing RMLEAK`
  - Same text, new triggers:
    - `TELEMETRY_V2` `Snapshot not built - out of memory`: also when a snapshot key could not be added.
    - `IOTHUB` `Hub empty: rules-engine RAM reset failed - retry owed`: also from the C2D command that empties
      the hub.
    - `BLE_LEAK` `Sensor tracking reset`: also after every device-set change that adds devices.
    - `BLE_VALVE` `[TASK] CMD: CONNECT` and the warning `[SCAN] Already connected`: also when a `provision`
      names the valve already linked.
    - `BLE_VALVE` `[CMD] RMLEAK write not ready. Queuing val=1` and `[CMD] Valve write not ready. Queuing
      val=0`: also when a leak, an override cancel or an override expiry needs an unreachable valve closed.
    - `RULES_ENGINE` lines that print an override window's remaining time (`NVS: restored override window`,
      `OVERRIDE WINDOW CANCELLED`, `Override active — auto-close BLOCKED`, `Override window: remaining=`) print
      the countdown from the power-on for a window restored with the clock lost, instead of -1 or 0.
    - `OFFLINE_BUF` `Stamped pre-sync event [%s]: ts=%lld (%lld s before this replay)`: also for an event too
      close to 512 B to be stamped in flash at the sync.
    - `IOTHUB` `SNAP trigger=event:%s`: new label `rmleak`, for the snapshot requested when the valve reports a
      new RMLEAK value (right after `Event: BLE Update type=4`), at most about 5 s after the snapshot before it
      (see *Fixed*).
  - Gone from the 2.1.4 development builds: `IOTHUB` `Hub empty: rules config reset to defaults failed`. On the
    emptying command, `PROVISIONING` `Setting rules config: auto_close=enabled triggers=0x07` and `Rules config
    saved to NVS` no longer appear; the removal's own save logs `Config saved to NVS successfully`.
  - Gone: the requeue lines of the 2.1.4 development builds, `... not applied (rc=%d) - requeued for retry`,
    `... flushed or superseded meanwhile - not requeued`, `... not written - it follows the requeued RMLEAK
    command`, `... kept for the next link, behind the RMLEAK command` and `Pending %s command=%d not applied,
    command queue full - kept for the next link`.
  - Gone from the 2.1.4 development builds (portal window): the `Wi-Fi connected` reason of `APP_WIFI` `portal
    priority OFF (%s) - BLE scanning resumed`. The window no longer closes at `Connected! IP`.
- **The NimBLE bond store** may still hold a bond to a neighbour's valve made under 2.1.3's name match. It is no
  longer used, and 2.1.4 does not delete it.
- **Not changed in 2.1.4:**
  - On a hub with sensors and no valve, a leak still latches the leak incident (when `auto_close_enabled` and
    the source's trigger bit are set), so `rmleak_auto_cleared` (10 s after every sensor is dry) and
    `rmleak_cleared` (after `leak_reset`) are still sent, without `valve_id`, although there is no valve
    interlock to clear.
  - Deferred to a later release:
    - the legacy keyword scan of C2D payloads;
    - command-id de-duplication;
    - the single-slot rules-event buffer;
    - more than 16 simultaneous leak sources;
    - sensors going unheard during a valve connect attempt;
    - LoRa driver hardening;
    - removing the valve bond on decommission.
- **Known limitations.**
  - A live DPS registration still blocks `iothub_task` for up to 60 s per attempt. While it runs, leak
    evaluation, auto-close and the rules tick wait for it. It happens only when the hub has no valid DPS cache:
    the first boot, after decommission all, or after a provisioning-epoch change. Moving DPS to its own task is
    future work.
  - Between a valve target change and the old link's DISCONNECT, C2D checks can read the old valve's cached
    state (RMLEAK, battery, connected), and during a swap from one valve to another so can the rules engine.
    That window is about 0.5 s on the bench, and up to the 5 s supervision timeout when the valve does not
    answer the terminate. After a decommission, auto-close no longer counts that link as a reachable valve.
  - Snapshot heap: cJSON prints a snapshot into a buffer that grows by doubling, so a snapshot needs about
    twice its printed size in one heap block. With NimBLE running the largest free block can be as small as
    7.5 KB (2.1.3 field log), so a hub with about 20 or more devices may fail to publish snapshots, retrying
    every 5 s. NimBLE now also runs on a sensors-only hub with a BLE sensor (P0-b), which 2.1.3 did not. Heap
    tuning is deferred to 2.1.5.
  - Heap budget: 2.1.4 uses about 181 B more static RAM than 2.1.3 (`.bss` +160 B, `.data` +16 B at build
    checkpoint 2, plus about 5 B of `.bss` from the council fixes), plus about 50 B of
    permanent heap for the two per-tag log levels set at boot. Both come out of the heap (2.1.3 field
    minimum: 2972 B free). The NimBLE host task also uses about 54 B more of its fixed stack on the valve
    notify path. The captive-portal fix and "go red" add 24 B of `.bss` (36,304 B at build checkpoint 4,
    36,280 B at checkpoint 3) and about 40 B of permanent heap (the SoftAP station-log event handler). The
    RMLEAK snapshot fix adds no static RAM. The router-rejoin fix adds about 32-40 B of `.bss` (36,336 or
    36,344 B expected at build checkpoint 5) and no heap.
  - An override started before the clock synced and then restored after a software reset cannot be re-based,
    because its elapsed time is unknown, so it ends at the first clock sync, possibly hours early. That fails
    toward auto-close.
  - An override restored after a power cut with no internet ends 24 h after the power-on if the clock has
    not synced by then. That is later than its real expiry, since the hub cannot know how long the power was
    off.
  - Captive portal after a Wi-Fi reset: on a hub with a valve or a BLE sensor, NimBLE now starts at boot and
    stays up beside the SoftAP portal, which therefore has less free heap than on 2.1.3.
    - While no Wi-Fi credentials are saved, and for about a minute after Wi-Fi is set up, BLE scanning pauses
      so a phone can join and finish (see *Safety*). During that pause BLE leak sensors are not heard, and a
      lost valve link is not re-found unless a leak close is pended for it. LoRa sensors and an established
      valve link keep working.
    - The pause has no time cap before setup. If setup is abandoned for hours, a BLE sensor that fails
      meanwhile is reported offline only 600 s after scanning resumes, and a valve that fails, 180 s after.
    - On a valve hub the valve is not linked during a portal opened at boot, unless a leak close is pended. It
      reads "syncing" (white fleet LED) until it links, and counts as unheard only if it is still not linked
      180 s after scanning resumes, or 180 s after a leak-close hunt that has not reached it (next item). A
      valve whose link drops in the window reads "Valve disconnected" (yellow) for as long, with the same
      exception. A valve open, or an RMLEAK clear, pended meanwhile waits for the window to close.
    - A pended leak close runs the valve hunt in the window, and if that hunt has not reached the valve 180 s
      after it started, the valve counts, with setup still running: the fleet LED turns red ("Valve offline"),
      and a valve whose link had dropped raises `device_offline`. That stays so after the leak clears, until
      the valve links, so a short leak during setup with the valve off or out of range leaves the hub red. Any
      pended close runs the hunt, so a cloud `valve_close` pended between the IP and the SoftAP stopping does
      the same with no leak. Accepted (user decision, 2026-09-29): the hub tried to close the valve and could
      not reach it.
    - On a hub with BLE sensors or a valve, the first snapshot after setup waits for scanning to resume and
      then for those devices to be heard (at most 180 s after the resume), so it normally comes about 60 s or
      more after the hub gets its IP. The lifecycle message and the buffered events still go out at the
      connect.
    - The router-outage fallback portal (credentials still saved) has no window. BLE pauses only for the
      page's Wi-Fi scans and the connect attempts (see *Fixed*), so a phone's join and its DHCP lease, before
      the page's first scan, still compete with the BLE scan: on the 2026-09-29 bench the lease took 17 s, and
      a join may fail. The 10 s reset then brings the hub back in the setup portal, with the window (see
      *Safety*).
    - The portal page's own disconnect button erases the credentials only while the hub is connected to the
      router, as in 2.1.3 (the Wi-Fi manager component): on the fallback portal, with the connection idle, it
      erases nothing at once. The router retry then rejoins with the old credentials, and the next loss of
      Wi-Fi erases them and opens the setup portal. Use the 10 s reset there.
  - **Router outage: BLE pauses for Wi-Fi (the router-rejoin fix, see *Fixed*; accepted, user decisions
    2026-09-29 and 2026-09-30).**
    - The BLE leak sensor (firmware 1.1.0) advertises in bursts: for 4 s when it turns wet, then for 2.5 s
      every 15 s while wet (every 100 s while dry), about one advertisement every 0.3-0.45 s. A connect
      attempt's pause keeps BLE off about 3 s at most and leaves at least about 1 s of the 4 s burst outside
      it, but a wetting whose first burst falls on a pause can still go unheard; it is then heard at its next
      wet burst, about 15 s later. While a portal page is open the pauses last 30 s, and a wetting is heard
      within about 45 s. A wetting shorter than that (about 15 s, or about 45 s with a page open) can be
      missed altogether: no latch and no auto-close. LoRa sensors and a linked valve are not affected.
    - An open portal page pauses BLE for 30 s of every 45 s or so while the router is down, with no health
      hold. A dry BLE sensor (a 2.5 s burst every 100 s) can then keep falling in the pauses: with a page left
      open for about 10 min or more it can go unheard for more than 600 s and be reported offline
      (`device_offline`, red fleet LED) until the page is closed. That is a false alarm: a wet sensor is still
      heard within about 45 s. The fallback SoftAP is open (no password), so a laptop that once joined it can
      rejoin by itself and open the page. The router retry waits for the page for at most 5 min after the last
      attempt, plus up to 17.5 s to stay out of BLE's 15 s listening time.
    - If the hub loses its router while the SoftAP is still up (within about 60 s of a rejoin or of a setup,
      for example a router that restarts twice), the Wi-Fi manager keeps retrying about every 7-10 s for the
      whole outage, each attempt with its pause, so BLE is paused about 25-33 % of the time until the router
      is back. The router retry adds nothing there.
    - A portal submit that lands while a router retry's attempt is in flight reboots the hub (the Wi-Fi
      manager's `ESP_ERROR_CHECK` on a station that is still connecting), and what was typed is not saved.
      Retries are not sent while the page is open, but the page counts as open only while it asks for the
      network list: a phone that puts the page in the background or locks its screen for more than about 10 s
      stops asking, and a submit made just after it returns can meet a retry. A page left open for more than
      5 min can meet one too. A submit during the Wi-Fi manager's own retries could always do this.
    - A portal submit that lands in BLE's 15 s listening time runs its connect with BLE on and may fail (no AP
      found) with the right password. Submit it again; once the page is closed the router retry also tries
      what was typed.
    - A mistyped password submitted on the fallback page (accepted, user decision 2026-09-30): the retries use
      what was typed, not the saved credentials, so the hub does not rejoin until the right password is
      submitted or the hub is restarted (a power cycle reloads the saved credentials).
    - A portal submit while Wi-Fi is connected (in the setup SoftAP's last minute, or on the page at the router
      address) is ignored by the Wi-Fi manager but replaces the configuration in RAM, and after the SoftAP has
      stopped the next loss of Wi-Fi starts no Wi-Fi manager retry and no fallback SoftAP (pre-existing, the
      Wi-Fi manager component). The router retry now covers this: the hub rejoins when the router is back if
      what was submitted matches the router. If it does not (a wrong password, another network), no SoftAP
      comes up to correct it from, and only a restart or the 10 s reset brings the hub back.
    - At every boot with saved credentials the first connect attempt pauses BLE, so the first leak scan and
      the valve hunt start when that attempt ends or 2.5 s after it started, whichever is first: at most about
      3-4 s after boot.
    - The fallback SoftAP is open, and every station on it costs the hub heap. On the 2026-09-29 bench a laptop
      that kept rejoining it, and flooding the portal's DNS server with its apps' lookups, took the lowest free
      heap (`MONITOR` `min_ever`) to 1,184 B (the 2.1.3 field minimum is 2,972 B). The router-rejoin fix
      neither causes nor removes this; it only ends the exposure sooner once the router is back.
  - Up to 16 events fit in the offline buffer. A long outage before the first clock sync can overwrite the
    oldest held events, as it already could after the sync.
  - **A leak latched while the valve was out of reach can still be read as a button press at the reconnect
    (deferred to 2.1.5).** If the hub then restarts, or the valve reconnects less than about 10 s after every
    sensor dried, the reconnect finds the valve open with RMLEAK never applied. It reads that as a press of
    the valve button while the hub was offline (the SRS §4.4.2 cross-reboot inference, unchanged from 2.1.3)
    and starts a 24 h `water_access_override_enabled{trigger:"button"}` window. Auto-close is blocked for that
    window. After a restart the sensor may still be wet, and its next wet report is then blocked too
    (`auto_close_blocked_override`), so the valve stays open during the leak until the window ends, the
    override is cancelled, or someone closes the valve.
  - **After a hub restart during a leak, the interlock can be released for one wet report (accepted).** The
    restart empties the hub's list of wet sources, so the first dry report from any device starts the 10 s
    auto-clear before a still-wet sensor is heard again. RMLEAK can then be released (`rmleak_auto_cleared`)
    for up to one wet burst of that sensor: about 15 s for a BLE sensor, minutes for a LoRa sensor. A
    `valve_open` in that gap is accepted. The valve stays closed unless it is opened, and the interlock
    re-latches (`auto_close`, closing the valve again) when the wet sensor is heard.
  - **A slow RMLEAK read-back can still be read as a button press (pre-existing, planned for 2.1.5).** When
    the hub re-asserts RMLEAK right after its own clear (a re-wet at the moment of an auto-clear, or at a
    reconnect while a wet source is outside the trigger mask), the hub can take the valve's pre-clear 1 as
    confirmation. If the re-assert's read-back then lands more than 5 s later (about twice the bench round
    trip, marginal RF), the clear's 0 is read as a press: a false `water_access_override_enabled{trigger:
    "button"}` and auto-close blocked for 24 h. The valve itself still ends closed with RMLEAK set.
  - Found in the final review and planned for 2.1.5:
    - With the forced-relink cap engaged, a live command that fails on a ready link stays pended until a newer
      command or the next reconnect (2.1.3 dropped it). A live command held behind a pended RMLEAK can wait the
      same way if the 10-deep command queue is full. Under a sustained busy GATT pool one command can hold the
      valve task for about 10 s.
    - A leak re-assert decided in evaluate_leak is queued after the rules lock is released, so a `leak_reset`
      that lands in that gap is overridden (the valve stays locked; fails closed).
    - A leak evaluated while provisioning is busy for more than 1 s (for example during a C2D save) is dropped
      by the rules engine, so the valve is not closed until that sensor reports again (up to 5 minutes for a
      BLE sensor; a LoRa sensor's next packet).
    - The rules reset after a valve replacement is not retried when the rules lock is busy for more than 1 s,
      and a failed rules reset of an emptied hub (lock busy for more than 5 s) is not retried when a
      `provision` arrives first. The old valve's leak source, or a stale latch or override window, can then
      survive into the new setup.
    - A valve that went offline with its flood probe wet and comes back dry after more than 180 s sends a
      late `device_offline` as it reconnects, then `device_recovered` 60–90 s later.
    - After a decommission and re-provision of the same valve within one loop pass, a valve that does not
      relink can stay connected and excellent in health (and green on the LED) with no link.
    - A hub name set for the first time while a snapshot is built fails that snapshot with a misleading
      `Snapshot not built - out of memory`; it is rebuilt at the 5 s retry.
    - Twin reported can read `provisioned:false`, `valve_id` null and device counts of 0 (a
      decommissioned-looking twin) when provisioning is busy for more than 1 s; the next device-set change
      republishes it. The lifecycle `provisioned` flag has the same exposure.
    - A `provision` that adds no device (an identical re-send, a rules-only provision) no longer restarts the
      commission snapshot and the post-provision snapshot pulse, as 2.1.3 did; only newly added devices do.
      The command still gets its own `event` snapshot.
    - When a reconnect replays both a held RMLEAK and a held valve command, the valve command's read-back finds
      the GATT pool full (`[CMD] valve read-back rc=6 - position unconfirmed`); the valve's own state
      notification then reports the position.
