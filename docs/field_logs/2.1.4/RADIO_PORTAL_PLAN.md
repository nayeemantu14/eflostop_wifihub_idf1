# eFloStop II Hub FW 2.1.4: final proposal for leak protection and a Wi-Fi setup that works on iPhone and Android

> **Approved by the user on 2026-10-01** ("Approve, start with WP-V/WP0"), with the recommended option on every decision in §13 **except D9, which is not approved: Forget stays** (`DELETE /connect.json` and the page's Disconnect are kept, so the D9 parts of §6.2 C9, §9 and §13 do not apply). The user's answers: D1, some leak sensors in the field are in 1M mode (mixed fleets are real); D2, leak protection keeps running in the reset portal; D10, nothing but the setup page calls the portal API.
>
> Copied unchanged from the council's scratchpad (`PROPOSAL_2_1_4.md`): nothing below this note is edited, so "proposal", "for your approval" and "nothing has been edited" read as of 2026-10-01, and its `managed_components/ankayca__esp32-wifi-manager` paths are now `components/wifi_portal`. Progress and the status of each package: `HANDOFF.md` §15.
>
> **Update 2026-10-02 (WP2c and WP2d; WP2e's `.bss` decision).** Two dated notes are added below, and nothing else: after §3's invariant table (I10: the user approved a task swap as an exception, WP2c's decision D1 (i)) and after §10's table (the `.bss` gate). Details: `HANDOFF.md` §15n and §15o.

> **Update 2026-10-06 (WP3, WP4, WP5 and WP6 committed).** Two more dated notes are added below, and nothing else: after §4.9's guard example (where the seven lines landed, and what the integrator measured) and after §10's `.bss` note (the gate with WP3-WP6). Details: `HANDOFF.md` §15q-§15t.

**Status.** This is a proposal for your approval (answer 8). Nothing has been edited, built or flashed. The only files written are model runs in the scratchpad.

**Scope.** Branch `fix/2.1.4` at `520b17a`, ESP32-S3, ESP-IDF 5.5.1. Every item lands in 2.1.4.

**Basis.**
- The council's `FINAL_DESIGN.md`, with all 18 red-team findings folded in.
- The phone-compatibility review and the component-patch review, both done on 2026-10-01.
- The review of this proposal on 2026-10-01. All 29 points are applied. Point 7 is applied with one correction: LR_AP has the same Coded share as SERVE-B, so a 4 s wetting in LR_AP is detected .968 at p_loss 0.3, not "≥ .99 in every state".
- Your binding answers of 2026-10-01.

Where this document and `FINAL_DESIGN.md` disagree, this document wins. "Council D…" refers to a decision number in `FINAL_DESIGN.md`. A plain "D…" refers to §13 of this document.

**Evidence tags.**

| Tag | Meaning |
|---|---|
| **[log]** | Field or bench capture: E1-E4 and the 2.1.3 UART log |
| **[code]** | Read in the repo or in the IDF source (line numbers are at `520b17a`) |
| **[model]** | The council's leak model, unchanged, with the new runs listed in the appendix |
| **[platform]** | OS or driver behaviour taken from platform knowledge, not yet benched |
| **[arith]** | Simple arithmetic, not a simulation |

**Notation.** "Cadence 15 s" means the sensor's wet heartbeat period. It is not the Coded PHY coding factor S=8.

---

## 0. Executive summary

### The problem

1. **One 2.4 GHz radio.** Wi-Fi and BLE share it under software coexistence. When the STA is idle (router outage, first setup, the 10 s reset), a continuous BLE scan owns the radio and the SoftAP gets no air.
   - A phone that joined got no DHCP lease for 95.2 s (E4), and in E1 none in 500 s [log].
   - With BLE off and the AP on its channel, the same phone got a lease in 0.12 s (E3) [log].
2. **The workaround costs leak protection.** The branch pauses BLE for the portal. After a 10 s reset with sensors provisioned, the pause has **no time cap**, so leak detection is 0 % for as long as the portal is up [code].
3. **The portal fails on its own, independent of the radio** [code]:
   - the DNS hijack stops at GOT_IP;
   - EDNS0 replies are malformed;
   - a UDP datagram shorter than 12 B to port 53 reboots the hub;
   - a Submit that lands in a connect attempt reboots the hub;
   - open networks and non-ASCII credentials cannot be entered;
   - a reopened sign-in page never shows the result.
4. **TLS starting beside the SoftAP** drove internal heap to 152 B with 4 failed allocations (E4) [log].
5. **Normal operation has a latent defect.** In about 1.5 % of sensor-timing cases a leak is never detected, because the scan is phase-locked to the sensor heartbeats [model].

### What 2.1.4 does

1. **One radio policy and one BLE scan owner.**
   - The policy is recomputed from facts.
   - The leak-scan task is the only code that starts or stops a scan.
   - NimBLE's hidden connect re-attempt is turned off.
2. **Windowed radio only while the STA is idle and the AP is up:**
   - AP_IDLE: 0.6 s Coded / 0.3 s Wi-Fi.
   - SERVE (a user on the page): the **densest Coded geometry that passes the phone gates**. The provisional choice is 0.6 s Coded / 0.6 s Wi-Fi. The council had 0.6 / 1.2; this changed because of your answer (7).
   - Everywhere else: continuous Coded-weighted scanning with a 1 s dither, which removes the phase lock.
3. **Every pulse is at most 2.8 s and is followed by at least 1.2 s of Coded.** A pulse is a Coded stop longer than the profile's own gap. 2.8 + 1.2 s fits inside the sensor's 4.0 s leak-edge burst. A join assist gives the phone a clean lease. It also gives a clean captive probe when the probe comes within about 2.5 s of the join.
4. **No TLS while the SoftAP is up.** Cloud bring-up waits for AP stop plus a gate on internal DMA-capable heap.
5. **The portal is fixed at the source** (component edits, now approved). There are 14 patches, C0-C13:
   - DNS rewritten, kept until STOP_AP and bound to the AP IP;
   - no reboot path left;
   - credentials take effect only when they work, and can include UTF-8 or no password;
   - the page is gzipped: 57.4 → 18.5 KB on the air and −39 KB of flash;
   - httpd runs only while the AP is up, and only on the AP interface.
6. **Seamless on phones:**
   - explicit CSA, so a phone follows the channel change;
   - a page that renders its result from `status.json` first;
   - a **Finish** button that stops the AP, which makes the iOS and Android sign-in windows close themselves.
7. **Seven sdkconfig lines** with compile-time guards, including the no-credential AP on channel 11. A memory set, measured one line at a time, is the last package.

### Guarantees

Figures are at cadence 15 s, with p_loss 0.3 / 0.5, and sensor FW 1.1.0 unchanged.

- **A leak that keeps the probe wet is never missed, only delayed.** This covers Coded sensors, and 1M sensors whose PHY is known.
  - Every heartbeat burst outside a pulse contains a Coded window (I1).
  - Pulses are ≤ 2.8 s, ≥ 6 s apart and ≤ 12 s per 60 s (I2, I2b).
  - Normal operation: p99 2.5 / 3.5 s. Today about 1.5 % of timing cases are never detected.
  - Portal states: p99 ≤ 16 s (p_loss 0.3) and ≤ 32 s (p_loss 0.5). This holds with one LIST per minute or a user Rescan every 20 s; LR_AP sets both maxima. At the I2b ceiling, with 2.5 s pulses every 12.5-14.5 s, p99 rises to 30.5 / 32.5 s.
  - Worst alignment in SERVE-A (a Submit on the leak edge): p99 17-30.5 s at p_loss 0.3 and 31.5-45.5 s at 0.5.
  - The worst rows in the design are LR_AP with a Submit on another sensor's leak edge at p_loss 0.5: p99 45.5-47.5 s and p99.9 76-90.5 s. LR_AP runs only while a leak incident is open and the valve is unreachable, for at most 10 min per incident.
- **A single wetting shorter than one edge burst (4 s) that dries by itself can be missed.** FW 1.1.0 puts `leak=1` on the air only while the probe is wet. Below about 2 s, even continuous Coded scanning misses some.
  - Even 100 % Coded scanning misses a 1 s wetting 2.8 % of the time (p_loss 0.3) and 12 % of the time (0.5) [model].
  - The hub can only reduce this. It does so through the SERVE rung rule (D8). The only complete fix is on the sensor side, and that is outside this release.
- **Memory:**
  - 0 failed allocations, internal-DMA free ≥ 8 KB and largest block ≥ 4.5 KB through the E2 and E4 replays;
  - about +12 KB of free heap while connected, before the memory set;
  - flash about −34 KB.
- **Phones:**
  - 0 reboots;
  - tap → joined ≤ 5 s p95, join → lease ≤ 3 s p95;
  - the sign-in window opens by itself (iPhone ≤ 8 s after the join, Pixel and Samsung ≤ 10 s);
  - Submit → result ≤ 10 s;
  - Finish → phone back on home Wi-Fi ≤ 15 s.

### What rests on what

| Basis | Items |
|---|---|
| Proven [log] | The two conditions for a lease: BLE off, and the AP on its channel (E3, E4). First setup working end to end with BLE idle (2.1.3 log). The TLS-beside-AP heap collapse (E4). The laptop flood (E2) |
| Read in code | Every reboot path and portal defect listed in §6 |
| Modelled | All leak figures. p_loss and the sensors' real advert interval (Ta) are **assumptions** |
| G0 and G1 must measure before the geometry is fixed | Ta per sensor; p_loss per mode; join / lease / DNS / probe / page latencies per phone; CSA behaviour; the AP channel in fallback; the AP-start heap dip; page-load time per SERVE rung |

### Plan

Twelve packages (WP-V, WP0-WP10) in one release, each behind a bench gate:
- vendor the component;
- instrumentation;
- make the portal unable to reboot the hub;
- cloud and AP lifecycle;
- sdkconfig;
- portal intake and phone UX, followed by a development checkpoint tag that is never shipped as is;
- scan executor;
- NORMAL de-lock;
- lab ladder (G1);
- radio policy in production;
- memory set;
- documents.

### What I need from you

14 decisions (§13). Two need facts only you have:
- **D1:** are any sensors in the field in 1M mode?
- **D10:** does any app or tool call the portal's HTTP API?

You excluded portal-on-demand and a WPA2 password on the setup AP. They appear only as exclusions (§9, §14).

---

## 1. How your answers change the council design

| Answer | Effect |
|---|---|
| **(1) Hub-only.** FW 1.1.0 timings confirmed; PHY set per sensor by LR_BUT | **Cadence is fixed at 15 s.** 8 s appears only as a side note (§5.1). Removed from the plan: the council's §3.8 "FW 1.2 contract", council D1 (cadence) and council D15's sensor-side fix. The FW 1.1.0 timings become **documented hub assumptions**, and the `_Static_assert`s in §4.8 depend on them. Short wettings stay a residual that the hub can only reduce (§5.2). Per-sensor PHY learning stays because both PHYs can exist (D1, §13). A learned PHY is persisted and never decays with time |
| **(2) No portal-on-demand, no WPA2** | Portal-on-demand and a WPA2 password on the setup AP are excluded by the user. The setup AP stays open; §9 states the residual |
| **(3) Component and sdkconfig edits approved** | These parts of the council design are superseded: §1 ("no managed_components change"), the §6 paragraph "Not removed in 2.1.4", the §7 rows, the §8 row "code.js / wifi_manager.c: none", council D9 and council D16 step 2. Patches C0-C13 land in 2.1.4 (§6). Branch code that existed only to work around component defects is deleted (§6.4) |
| **(4) Everything in 2.1.4** | The council's 2.1.5 list is sorted in §14. Moved into 2.1.4: vendoring, all component fixes, transactional credentials, the activity hook, gzip, the httpd and DNS bounds, persisted PHY bits, the widened leak-response trigger, the memory set. Council D7 (release split) is closed. WP0-WP4 end at a development checkpoint tag that is never shipped as is, because it still has the uncapped no-credential pause (§11) |
| **(5) Seamless on iPhone and Android** | New §7 and new gate G-CNA. Added: DNS kept to STOP_AP, explicit CSA, a status-first page, Finish, failure reasons |
| **(6) Memory matters** | Added: C2b, C6, C7 (−39 KB flash), WP9 (memory set), and a target floor for WP9 (§8) |
| **(7) Delayed shutoff is acceptable; a missed leak is not** | SERVE is chosen by the rule "densest Coded that passes the phone gates", provisionally C0.6/W0.6 instead of C0.6/W1.2. The leak-response trigger is widened, and the LR overlay is bounded to 10 min per incident. Short wettings are stated against the physical ceiling. The bounded dead-valve response (D5) delays shutoff and does not miss the leak |
| **(8) Propose first** | This document. No code before approval |

---

## 2. What is proven, what is modelled, what must be measured

### 2.1 Bench and field evidence [log]

| ID | Capture | What it shows |
|---|---|---|
| E1 | Phone on the fallback AP with BLE scanning and the STA idle | No lease in 500 s |
| E2 | Laptop that remembers the SSID, on the fallback AP | Heap fell to 1,184 B |
| E3 | First setup, and a rejoin | With BLE paused and the AP on its channel: lease 0.12 s after the join, captive DNS 0.16 s after the lease. The lease came 14.3 s after the DNS hijack had stopped |
| E4 | Router outage | No lease for 95.2 s. The only lease came 0.8 s into the first BLE stop in which the AP also stayed on its channel. The hijack was usable for 2.95 s after that lease. TLS in the AP tail: min_ever 152 B, 4 failed allocations, cloud back 61.6 s after the rejoin |
| 2.1.3 log | iPhone first setup, BLE idle | Worked end to end: join 26.663 s, lease 27.833 s (1.17 s after the join), `captive.apple.com` 29.963 s (3.30 s after the join), Submit 47.363 s |

### 2.2 Read in code [code]

- The reboot paths:
  - `wifi_manager.c:1074/1081/999`;
  - `dns_server.c` `exit()`, and the `length-12` underflow on a datagram shorter than 12 B;
  - five unchecked mallocs.
- The component's endless retry loop (`:1186-1200`).
- NimBLE's hidden connect re-attempt (`ble_hs_hci_evt.c:262-278`, `ble_gap.c:1211, 1567-1571`).
- The DNS stop at GOT_IP (`:1308`).
- The byte-level EDNS0 malformation (model `constrained/dns_emul.py`).
- The HTTP 400 for open networks (`http_app.c:165`).
- A 32-byte SSID running into the password in `status.json` (`:414-417`).
- A heap overflow from beaconed SSIDs containing control characters (N4).
- The component silences the Wi-Fi driver log: `wifi_manager.c:179` calls `esp_log_level_set("wifi", ESP_LOG_NONE)`.

### 2.3 Modelled [model]

- Every latency, short-wetting and dry-health figure in §5.
- The assumptions:
  - Ta uniform on 312.5-437.5 ms, plus advDelay 0-10 ms;
  - p_loss 0.3 (baseline) and 0.5 (stress);
  - TAIL at p_loss 0.65.
- p_loss and Ta are **uncalibrated**.
- The TCP-retransmit alignment behind the SERVE geometries is arithmetic only [arith], not modelled.

### 2.4 G0 and G1 must measure these before any geometry is frozen

| Quantity | Gate | Why it matters |
|---|---|---|
| Ta per sensor (inter-advert delta) | G0 | Whether any sensor sits in the router-beacon correlation band of 404-412 ms (§5.5) |
| Adverts heard per burst, per mode | G0 | Gives p_loss. The model is re-run with it before WP8 |
| Join → lease, tap → join, lease → DNS → 302 → page, per phone | G0 (activity-hook log line) | The baseline for G1 and G-CNA |
| Join → first DNS per phone in state S2 | G1 | Whether the join assist covers the probe (§4.4, §6.3) |
| The AP-start heap dip (21,288 → 3,344 in E4) | G0 | Decides whether the START_AP list pre-scan may be enabled |
| CSA behaviour: does `csa_count = 0` mean "no CSA"? | G0. WP0 sets the `wifi` tag back to INFO, overriding `wifi_manager.c:179`, so the driver prints `<connect>csa … csa_count:N` | Decides between 3 and 5 for C13 |
| The SoftAP channel in fallback (WP0 logs the AP and STA channels) | G0 | Checks the [platform] claim in §4.5 that the fallback AP follows the router's channel |
| Page-load time, ping loss and sign-in auto-open per SERVE rung | G1 | Selects the SERVE rung (§4.3) |
| Android sign-in latency after a DNS query lost in a BLE window | G1 | Decides contingency K1 (§4.4) |
| httpd socket cap: 4, 5 or 7 | G-CNA S1 (after WP4) | The shipped value. G1 re-checks page p90 at that cap |
| NimBLE controller cost of about 86k scan restarts a day | G4 | Dither period D = 1 s or 3 s |

---

## 3. Invariants (enforced in code or on the bench)

| # | Invariant | Enforcement |
|---|---|---|
| I1 | **Burst coverage.** For each PHY that needs coverage (Coded always; 1M only for a *known*-1M sensor), every window is ≥ 0.6 s and every gap plus jitter is ≤ 1.4 s. With L = 0.448 + 0.1 s: W ≥ L + 0.05 and G ≤ 2.5 − 2L. So every burst of 2.5 s or more outside a pulse contains an unbroken Coded segment of at least one advert interval | X-macro `_Static_assert`s (§4.8). A boot self-test pins NORMAL if a check fails |
| I2 | **Blind budget.** At most **2.8 s** without a Coded scan, counted from the end of the last Coded window. Every blind span longer than the profile's own Coded gap is followed by a **≥ 1.2 s Coded recovery window**. 2.8 + 1.2 ≤ 4.0 s, the edge burst | The executor clips every pulse deadline. `_Static_assert(BLIND_MAX + RECOVERY <= BURST_EDGE)` |
| I2b | **Pulse rate.** ≥ 6 s of profile time between pulses (SUBMIT exempt). ≤ 12 s of blind time per rolling 60 s. Every re-arm is jittered | A deadline helper; the duty watchdog counts it |
| I3 | **BLE fails on.** Every pause is a deadline that the executor expires itself | `ulTaskNotifyTake(min(next edge, deadline, 1 s))` |
| I4 | **No TLS or DPS while the SoftAP is up**, including DPS's private client | Admission on `iothub_task`; DPS abort hook |
| I5 | **One BLE scan owner, and no host-internal connect** | Only the leak-scan task calls `ble_gap_ext_disc` / `ble_gap_disc_cancel`. `CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT=n`, with a compile guard |
| I6 | **State is recomputed from facts** at least once a second | A missed event costs at most one pass |
| I7 | **The SoftAP goes off-channel only for:** a Submit; a hub LIST scan (≤ 1 per 60 s, plus a user Rescan ≥ 20 s apart, within I2b); an app router retry. Never within 10 s of a join that has no lease yet (SUBMIT exempt), and never during a join assist. **No scan is ordered by a page poll** (C10b) | Gates in `radio_policy` |
| I8 | **Wi-Fi floor in AP modes** (except LR_AP's first 30 s): no BLE run longer than 600 ms, and a Wi-Fi slot ≥ 300 ms in every period | X-macro asserts |
| I9 | **One Wi-Fi retry owner while the AP is up.** The component never starts its retry timer while `AP_STARTED_BIT` is set | C5, in the component |
| I10 | **No new task, no heap at rest, 0 IRAM** through WP8. DIRAM `.text` = 113,387 B. WP9 re-baselines on purpose | Build gate after every package |
| I11 | **Captive detection holds from START_AP to STOP_AP.** DNS and httpd stay up on the AP interface. Every foreign Host gets `302 → http://10.10.0.1`. Never 204 and never "Success" | C3, C4; G-CNA |
| I12 | **No reboot path in the portal stack.** No `ESP_ERROR_CHECK`, `abort()` or `exit()` on a runtime path in `wifi_manager.c`, `http_app.c` or `dns_server.c`. Malformed input is dropped or refused | C2, C4; G-FAULT, P-13 |
| I13 | **Credentials.** Never logged. Never written to the live config or to NVS until the candidate gets an IP. On failure the previous network stays | C1, C8; G8x |
| I14 | **Load-bearing sdkconfig values are asserted at compile time** (`#if … #error`), because `sdkconfig` is untracked | §4.9 |

> **Update 2026-10-02: an approved exception to I10 (WP2c, the user's decision D1 (i); `HANDOFF.md` §15n).** To take every cloud publish off `iothub_task` (LS-1: on a dead WAN under a connected Wi-Fi a publish held leak handling for 10-20 s), WP2c adds a **static task, `cloud_tx`** (5,120 B stack and its TCB in `.bss`, priority 3), and **removes `uart_cmd_task`** (4,096 B of stack on the heap, priority 5), whose four bench keys `lora_task` now polls without waiting. The task count is unchanged, `cloud_tx` holds no heap at rest, and IRAM stays 0 (DIRAM `.text` = 113,387 B). The cost: `.bss` +6,301 B, so, net of `uart_cmd_task`'s freed stack, TCB and headers (about 4.4-4.5 KB of heap), about 1.8 KB more internal RAM in use at rest. I10 otherwise stands through WP8. WP9 may trim `cloud_tx` to 4,096 B if the T6-11 high-water mark shows 1.5 KB or more free.

---

## 4. Radio policy

### 4.1 Inputs (each has exactly one writer)

| Fact | Writer | Source |
|---|---|---|
| `sta_ip`, `ap_up` | wifi_manager task (flags only); cross-checked by wifi_task on every pass | callbacks; `esp_wifi_get_mode()` |
| Station table: 4 × 16 B (MAC, join, lease, first DNS, last assist, first 302/page, flags) | default event loop (store and notify only) | `AP_STACONNECTED` / `STADISCONNECTED`, and `IP_EVENT_AP_STAIPASSIGNED` (carries the MAC in 5.5.1, verified). Pruned against `esp_wifi_ap_get_sta_list()` |
| `page_active_tick`, `hot_tick`, first DNS / 302 / page per station | **the activity hook (C10a)**. The httpd task writes PAGE, API, PROBE_302 and STATUS. The DNS task writes only the first-DNS tick per station. This replaces the council's `cb_scan_start` stamp and `s_self_scan` | `page_active`: any page or API request except `status.json`. `hot`: page, a user-initiated API call (not `?bg=1`), a probe 302 |
| `submit_in_flight` | wifi_manager task | the C8 callback with kind USER; cleared at GOT_IP or failure |
| Pulse deadlines: JOIN / SUBMIT / RETRY / LIST / CONNECT | as in the council design | ticks |
| `leak_response_pending` | valve module | **Widened and bounded.** True when the valve is provisioned **and** the valve is unlinked **and** either RMLEAK / CLOSE is pended **or** (`g_leak_incident_active` **and** the interlock is not confirmed). The LR overlay (LR_AP, NORMAL_LR) runs at most **10 min per incident**, then the hub returns to its normal mode. NORMAL's 20 % passive 1M re-finds a powered valve in 1-3 s. While the incident is latched, AP modes keep their discovery slots without back-off. `g_leak_incident_active` stays set until LEAK_RESET (`rules_engine.c:60`), which is why the time bound is needed |
| BLE demand, per-sensor PHY | leak task, valve module | PHY learned from `ext_disc.prim_phy` and **persisted** (≤ 112 B NVS, written only on change). The PHY changes only when the sensor is heard on the other PHY; there is no time decay. Unknown = never heard since provisioning |

### 4.2 Modes (first match wins)

Two derived terms:
- `windowed` = AP up **and** STA not connected.
- `serve` = page active in the last 60 s, **or** a submit in flight, **or** a provisional SERVE (a lease in the last 60 s; at most once per MAC per 10 min).

| # | Mode | Condition | BLE pattern | Wi-Fi | Largest BLE block | Cloud |
|---|---|---|---|---|---|---|
| 1 | BLE_IDLE | No BLE leak sensor provisioned, **and** either no valve provisioned or the valve link verified by `ble_gap_conn_find()` | none | 100 % | – | per §4.6 |
| 2 | LR_AP | leak response pending (§4.1; ≤ 10 min per incident), `windowed` | first 30 s: [1M 1.2][C 0.6]; then [1M 0.6][W 0.3][C 0.6][W 0.3] | 0 % (SUBMIT honoured), then 33 % | 1.8 s, then 0.6 s | no TLS |
| 3 | NORMAL_LR | leak response pending (§4.1; ≤ 10 min per incident), not `windowed` | [1M 1.0][C 0.6], alternating, dithered | coexistence share | – | if the AP is down |
| 4 | NORMAL, including **TAIL** (AP up, STA connected) | not `windowed` | N_CODED: 1M 160/32 + Coded 160/128, duration 1 s, restart after U(0, 100 ms). N_MIXED if a known-1M sensor exists | coexistence share | – | if the AP is down; none in TAIL |
| 5 | SERVE | `windowed` and `serve` | **the rung chosen by G1 (§4.3). Provisional SERVE-A: [C 0.6][W 0.6]; every 3rd period [C 0.6][W 0.3][M 0.3][W 0.3] when discovery is needed. AP_K1M with a known-1M sensor** | 50 % (40 % in discovery periods; 33 % with a known-1M sensor) | 0.6 s | no TLS or DPS |
| 6 | AP_IDLE | `windowed`, otherwise | [C 0.6][W 0.3]; every 2nd period adds [M 0.3][W 0.3] for discovery; AP_K1M with a known-1M sensor | 33 % (40 % in discovery periods) | 0.6 s | no TLS or DPS |

The following council rules are kept, with the changes marked:
- **Discovery.** A slot runs only when a sensor's PHY is unknown, or when the valve is unlinked outside LR. A discovery slot gives no coverage guarantee.
- **Back-off.** After 10 min unheard, or health-offline, discovery runs only every 6th period. **Exception:** while a leak incident is latched, AP modes keep their discovery slots without back-off.
- **Sensor-starvation guard.** A sensor unheard for ≥ 250 s gets AP_IDLE density with discovery suspended for 110 s, at most once per 600 s. **For a known-1M or unknown-PHY sensor, the guard runs AP_K1M instead of suspending discovery.**
- **Period rule.** No profile sequence length divides 8, 15 or 100 s. For example, SERVE-A with discovery = 1.2 + 1.2 + 1.5 = 3.9 s. With I1 in place, phase lock cannot blind a burst anyway.
- **TAIL runs NORMAL.**

### 4.3 SERVE geometry: the rung rule (answer 7)

G1 benches these rungs on the lab image. G1 runs on a build that includes C7 (gzip) and C10b (cache-only `/ap.json`).

| Rung | Pattern | Coded share | Short wetting 1 s, detection at p_loss 0.3 / 0.5 [model] | TCP retransmit at +1 s [arith] |
|---|---|---|---|---|
| AP_IDLE density (no SERVE) | C0.6 / W0.3 | 67 % | .860 / .700 | mostly lands in BLE again; worst case about 7 s |
| **SERVE-A-thin** | SERVE-A for 10 s after a **hot** event (page, user API, probe 302, submit); AP_IDLE density otherwise while the page is active | 50-67 % | between A and AP_IDLE | as A while hot |
| **SERVE-A** (provisional) | C0.6 / W0.6 (P 1.2 s) | 50 % | .778 / .597 | for a loss at t ∈ [0.2, 0.6], the +1 s retry lands in the next Coded window and the +3 s retry lands in Wi-Fi; worst case about 3 s for a SYN |
| SERVE-C (lab only, breaks I8) | C1.0 / W1.0 | 50 % | .716 / .568 | the +1 s retry always lands in Wi-Fi |
| SERVE-B (council) | C0.6 / W1.2 (P 1.8 s) | 33 % | .560 / .419 | the +1 s retry always lands in Wi-Fi |

**Rule.** Ship the rung with the **highest Coded share** that passes, in state S2 (§7.5), on all four phone classes, 10 runs each. The thresholds:
- G1's UX thresholds: tap → join ≤ 5 s p95 with 0 "Unable to join"; join → lease ≤ 3 s p95;
- sign-in auto-open per P-4;
- page ≤ 3 s after the sign-in window opens;
- list ≤ 5 s;
- result ≤ 10 s p90;
- ping loss ≤ BLE duty + 10 points.

Ties go to the better short-wetting figure (A beats C). SERVE-C ships only if you accept an I8 exemption for associated stations; otherwise it is data only.

**Why a denser SERVE than the council's is now plausible:**
- gzip cuts a page load from about 40 to about 14 TCP segments;
- each asset fits in one send buffer (5,760 B);
- `/ap.json` no longer scans (C10b, in the build that G1 measures).

The phone therefore needs far less air per page.

### 4.4 Pulses: the only Coded stops longer than a profile's own gap

| Pulse | Starts | Ends at the earliest of | Aligned by the hub? | Limits |
|---|---|---|---|---|
| **JOIN_ASSIST** | `AP_STACONNECTED` | **max(lease + 1.5 s, first 302 or page served to that station + 0.3 s)**; the blind budget; the station leaves | no | Per MAC: ≥ 30 s + U(0, 10) s apart; ≤ 2 per rolling 60 s. One extra after recovery if the first overlapped a STA attempt and gave no lease. Under LR: none in the first 30 s, then 1 per 60 s |
| **SUBMIT** | the C8 callback, kind USER | GOT_IP; failure; the blind budget | no (the lead-in is one executor wake) | Always honoured, also under LR; exempt from spacing |
| **RETRY** (`router_retry` only) | executor grant at the end of a Coded window | 1.5 s | yes | 30 s after the previous attempt **ended** + U(0, 5) s. Deferred while the page is active or a submit is in flight (≤ 5 min cap). Only while the AP is up |
| **LIST** (hub scan) | grant at a window end | SCAN_DONE; 2.5 s | yes | Triggered by the first page request when the cache is empty or ≥ 60 s + U(0, 10) s old, and by a user **Rescan** (`POST /scan.json`) when the cache is ≥ 20 s old and I2b allows it. Internal-DMA ≥ 24 KB at issue (a const; G0 re-derives it). The START_AP pre-scan stays off until G0 has attributed the AP-start dip |
| **CONNECT** (valve claim) | grant at a window end (`DISC_COMPLETE` in NORMAL) | CONNECT event; 1.5 s (2.5 s under LR) | yes | Back-off 10 → 30 → 60 → 300 s, reset by a link held for 60 s. Deferred in SERVE unless LR |

**Clipped join assist.** Suppose the blind budget clips the assist while the station has a lease but has not yet sent DNS or received a 302. Then provisional SERVE starts with its Wi-Fi slot right after the recovery window. This case is expected: in the 2.1.3 iPhone log the first DNS came 3.3 s after the join, beyond the 2.8 s budget.

**Grant protocol and pre-fallback attempts: unchanged.** The requester waits at most 2 s for a grant, then proceeds without a pulse and logs a warning. The component's first three attempts after a link loss run without a pulse (G7 verifies the rejoin rate).

**Contingency K1** (only if G1 shows Android sign-in > 10 s p90 in S2):
- The first DNS query from a station that has not been served a page opens a 1.5 s assist.
- Limit: once per station per 60 s, within I2 and I2b.
- Reason: Android's DNS retry after a lost query is about 5 s [platform], against about 1 s on iOS.

### 4.5 Wi-Fi-side policy

- **Scan dwell.** Active scans of 0-60 ms, home dwell 100 ms. The component sets these once, after `esp_wifi_start()` (C13). The council's planned app-side timing workaround is therefore not needed.
- **List cache.** `/ap.json` serves the cache only (C10b, from WP4). From WP8 a real scan runs only as a LIST pulse. An empty list shows "No networks found - Rescan / Enter manually" after 8 s.
- **Channel hint.**
  - At GOT_IP the component writes the connected AP's primary channel into the RAM STA config. It is never saved (C13).
  - The page sends `X-Custom-chan` from the list entry, so a Submit scans that channel first.
- **No-credential AP on channel 11.** `CONFIG_DEFAULT_AP_CHANNEL=11` (it is 1 today). 2452-2472 MHz is clear of BLE advertising channels 37/38. It is **[platform] and unverified** that the fallback AP follows the router's channel. With the STA not connected, the SoftAP may run on its configured channel. G0 records the AP channel in fallback.
- **One retry owner:** C5 (I9). LOCAL PATCH 1 (the stop at START_AP) stays.
- **Retry deferral** keys only on page activity or a submit in flight, never on a join or a lease. A remembered laptop cannot hold the hub off its router.

### 4.6 AP lifecycle and cloud admission

One component API, `wifi_manager_ap_stop_in(ms)` (C12), replaces the council's planned `extern` poke of `wifi_manager_shutdown_ap_timer`. It re-arms the single shutdown timer, and only when `WIFI_CONNECTED_BIT` is set.

| Situation at GOT_IP | AP stop |
|---|---|
| Automatic rejoin, 0 stations | IP + 0.5 s |
| Automatic rejoin, ≥ 1 station | IP + 20 s, or 10 s after the last station leaves |
| User Submit, then **Finish** tapped | max(IP + 5 s, Finish + 2 s) |
| User Submit, no Finish | IP + 60 s, or 15 s after the last station leaves, never before IP + 15 s (grace for a re-join after the channel switch) |
| Backstop (wifi_task, from facts) | AP still up with the STA connected at IP + 75 s: STOP_AP once |
| STA lost during the tail | The AP stays up; the component starts no retry timer (C5); `router_retry` owns the retries |

**Unchanged from the council design:**
- Callbacks only set flags.
- `iothub_task` stops MQTT at once on a link loss.
- **Cloud admission** in `net_maintain` requires:
  - `sta_ip && mode == STA`;
  - internal-DMA free ≥ 36 KB and largest block ≥ 12 KB (consts; G0 re-derives them).
- **Escapes:** after 60 s, admit if the largest block is ≥ 8 KB (warning); after 180 s, admit anyway (error, counted).
- The DPS abort hook waits in 1 s slices.
- `outbox.limit` about 4 KB; the lifecycle is republished on CONNECTED.

**Council items that were never built, now done at the source instead:**
- the `http_app_stop()` calls in `cb_ap_stopped` and at the first boot GOT_IP (C3);
- the `extern` timer pokes (C5, C12).

### 4.7 Mechanics (RTOS)

**Unchanged from the council's §3.6:**
- The `ble_leak_scan` task is the executor: notification-driven, priority 4 → 6, 3,072 B stack.
- Continuous profiles: `ext_disc(duration = 100)` and a dithered restart on `DISC_COMPLETE`.
- Windowed profiles: one `ext_disc` per slot.
- Start failures are retried within 50 ms and counted against the budget.
- The event-loop handler only stores and notifies.
- The valve task uses grants, checks for a stale handle with `ble_gap_conn_find`, and handles `REATTEMPT_COUNT` defensively.
- Logs:
  - one INFO line per mode change;
  - NimBLE at WARN;
  - a 60 s summary: seconds per mode, BLE-on achieved against expected (the duty watchdog), pulses and blind seconds, joins with join → lease, adverts per sensor, failed allocations, admission escapes, LR overlay time.

**New:**
- A per-station portal line from the activity hook: `portal: 10.10.0.x lease→dns N ms (host), →302 N ms, →page N ms`.
- The hook only stores ticks. It runs on two tasks:
  - on the httpd task for PAGE, API, PROBE_302 and STATUS;
  - on the DNS task for DNS.

  The DNS task writes only the first-DNS tick per station, and httpd writes the rest.

### 4.8 Profile table as code

```c
#define ADV_SMAX_MS    448   /* FW 1.1.0: Ta max 437.5 + advDelay 10 - documented hub assumption */
#define JITTER_MS      100
#define BURST_MIN_MS  2500   /* FW 1.1.0 heartbeat burst */
#define BURST_EDGE_MS 4000   /* FW 1.1.0 leak-edge burst */
#define L_MS          (ADV_SMAX_MS + JITTER_MS)            /* 548  */
#define W_MIN_MS      (L_MS + 50)                          /* 598  */
#define GAP_MAX_MS    (BURST_MIN_MS - 2 * L_MS)            /* 1404 */
#define BLIND_MAX_MS  2800
#define RECOVERY_MS   1200
_Static_assert(BLIND_MAX_MS + RECOVERY_MS <= BURST_EDGE_MS, "I2");
_Static_assert(RECOVERY_MS >= 2 * L_MS, "I2: recovery spans two advert intervals");
#define PROFILES(X) \
  X(SERVE_A,      C,600,  W,600,  _,0,   _,0)   \
  X(SERVE_A_DISC, C,600,  W,300,  M,300, W,300) \
  X(SERVE_B,      C,600,  W,1200, _,0,   _,0)   \
  X(SERVE_B_DISC, C,600,  W,600,  M,300, W,300) \
  X(APIDLE,       C,600,  W,300,  _,0,   _,0)   \
  X(APIDLE_DISC,  C,600,  W,300,  M,300, W,300) \
  X(AP_K1M,       C,600,  W,300,  M,600, W,300) \
  X(LR_AP_30,     M,1200, C,600,  _,0,   _,0)   \
  X(LR_AP,        M,600,  W,300,  C,600, W,300)
/* CHECK(): Coded window >= W_MIN_MS; period - Coded + JITTER <= GAP_MAX_MS (all rows pass:
 * gaps 700/1000/1300/1300/400/1000/1300/1300/1300 ms). I8 checks are generated from the same rows:
 * every BLE run <= 600 ms and a W slot >= 300 ms per period (LR_AP_30 exempt).
 * SERVE_RUNG is a #define fixed after G1; SERVE_C (1000/1000) exists only under APP_RADIO_LAB. */
```

### 4.9 sdkconfig

The first six lines land in WP3; the channel line lands in WP4 with its guard. `sdkconfig` is untracked. The seven lines go into the tracked `sdkconfig.defaults`, each with a one-line comment (D12). That file already holds `LWIP_MAX_SOCKETS=16`, `HTTPD_MAX_REQ_HDR_LEN=1536`, `HTTPD_ERR_RESP_NO_DELAY` and `MBEDTLS_DYNAMIC_BUFFER`, each with its comment.

| Line | Today | Why |
|---|---|---|
| `CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=2048` | 4096 | Largest contiguous block per TLS write: 4,437 → 2,389 B |
| `# CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE is not set` | y | About +4 KB while connected |
| `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=16` | 32 | Halves the E2 buffer ceiling (about 109 → 54 KB) |
| `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=16` | 32 | Same |
| `# CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT is not set` | y | I5 (red-team SR-1) |
| `CONFIG_DEFAULT_AP_MAX_CONNECTIONS=4` | 4 | Unchanged; asserted |
| `CONFIG_DEFAULT_AP_CHANNEL=11` | 1 | D7 |

Each line gets a compile guard, for example:

```c
#if CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT
#error "I5: NimBLE connect re-attempt must be off"
#endif
```

> **Update 2026-10-06: the seven lines are in (`HANDOFF.md` §15q, §15t).** All seven, the channel line included, landed together in WP3's `fa05390` (`sdkconfig.defaults`, and six `#error` guards plus the unchanged connection count's in `main/main.c`); WP4 then set the component's Kconfig default for `CONFIG_DEFAULT_AP_CHANNEL` to 11 as well. A local `sdkconfig` written before then still holds the old values and stops the build at six of the guards, by design: `HANDOFF.md` §15t gives the regeneration (delete the eight affected lines, `idf.py reconfigure`), which the integrator ran with kconfgen in a scratch folder: the result differs from the old file in exactly those values, plus `CONFIG_BT_NIMBLE_MAX_CONN_REATTEMPT` and the `TLS1_3` comment line, which drop with their dependencies. Measured on the IDF objects compiled both ways: the re-attempt line also removes NimBLE's `ble_adv_reattempt` (1,900 B), `ble_conn_reattempt` (100 B) and `reattempt_conn` (8 B), so the table in §10 understates this row's `.bss` by about 2 KB (−2,008 B, not about −40 B); flash about −1.8 KB (code −1,491 B, `.rodata` −287 B), with `ssl_tls.c` 274 B larger without the kept peer certificate. D7's channel reaches every hub, commissioned or not: the component configures the SoftAP once, at its task's start and from the RAM default, before it reads the NVS settings blob (whose stored channel, 1 on older hubs, is read but never applied).

Lab-only (Kconfig `APP_RADIO_LAB`, default n), C11:
- `esp_wifi_config_11b_rate(WIFI_IF_AP, true)`;
- `scan_config.coex_background_scan = true`.

---

## 5. Leak-detection guarantees (cadence 15 s, sensor FW 1.1.0)

### 5.1 A persistent leak: delayed, never missed

A leak that keeps the probe wet produces a 4.0 s edge burst and then a 2.5 s burst every 15 s.
- I1 puts a Coded window inside every burst that falls outside a pulse.
- I2 and I2b bound every pulse and its rate.
- So the chance of still being undetected falls with every heartbeat. **No state has "never" any more.**
- This holds for Coded sensors, and for 1M sensors whose PHY is known (§5.5 item 5).

Latencies are median / p90 / p99 / p99.9 in seconds [model].

| State | P(≤ 10 s) at 0.3 / 0.5 | Latency at p_loss 0.3 | Latency at p_loss 0.5 | worst60 at 0.3 / 0.5 |
|---|---|---|---|---|
| **Today**, NORMAL dual 160/80 | .974 / – | 0.5 / 2.5 / **∞ / ∞** | – | **0** |
| **Today**, no-credential portal (sensors provisioned) | **0** | never | never | 0 |
| NORMAL N_CODED | .9997 / .995 | 0.5 / 1.0 / **2.5** / 3.5 | 0.5 / 2.0 / 3.5 / 16.5 | 1.000 / 1.000 |
| NORMAL + valve claims before back-off | .998 / .988 | 0.5 / 1.5 / 3.0 / 15.5 | 0.5 / 2.5 / 15.5 / 17.5 | 1.000 / .9999 |
| N_MIXED (known-1M sensor) | .997 / .967 | 0.5 / 2.0 / 3.5 / 16.0 | 1.0 / 3.0 / 16.5 / 32.0 | 1.000 / .9996 |
| TAIL (N_CODED at p_loss 0.65) | .970 | 1.0 / 3.0 / 16.5 / 31.5 | – | .9997 |
| AP_IDLE | .9993 / .987 | 0.5 / 1.5 / **3.0** / 4.0 | 0.5 / 2.5 / 15.5 / 17.5 | 1.000 / 1.000 |
| AP_IDLE + discovery | .994 / .958 | 0.5 / 2.0 / 4.0 / 16.5 | 1.0 / 3.0 / 17.0 / 32.5 | 1.000 / .9987 |
| AP_IDLE + RETRY 1.5 s per 30-35 s | .998 / .980 | 0.5 / 1.5 / 3.5 / 15.5 | 0.5 / 2.5 / 15.5 / 30.5 | 1.000 / .9998 |
| AP_IDLE, JOIN at the edge (worst) | .934 / .838 | 3.5 / 4.0 / 16.5 / 30.5 | 3.5 / 15.5 / 30.5 / 45.5 | – |
| **SERVE-A** C0.6 / W0.6 | .9939 / .9575 | 0.5 / 2.0 / **4.0** / 16.5 | 1.0 / 3.0 / 17.0 / 32.5 | 1.000 / .9987 |
| SERVE-A + discovery (mean period 1.3 s) | .9918 / .9475 | 0.5 / 2.0 / 4.0 / 17.0 | 1.0 / 3.5 / 17.5 / 45.5 | 1.000 / .9979 |
| SERVE-A + LIST 2.5 s per 60-70 s | .988 / .945 | 0.5 / 2.0 / 15.5 / 17.0 | 1.0 / 3.5 / 17.5 / 45.5 | 1.000 / .9975 |
| SERVE-A + Rescan 2.5 s every 20-25 s | .976 / .919 | 0.5 / 2.5 / 15.5 / 30.5 | 1.0 / 4.0 / 30.5 / 46.5 | .9998 / .9953 |
| SERVE-A at the I2b ceiling (2.5 s pulses every 12.5-14.5 s) | .957 / .879 | 1.0 / 3.5 / 30.5 / 32.5 | 1.5 / 15.5 / 32.5 / 60.5 | .9997 / .9926 |
| SERVE-A, SUBMIT at the edge (worst) | .940 / .847 | 3.5 / 4.0 / 17.0 / 31.5 | 3.5 / 16.0 / 31.5 / 47.0 | – |
| same, recovery thinned by coexistence | .707 / .568 | 3.5 / 16.0 / 30.5 / 32.5 | 4.0 / 17.5 / 45.5 / 60.5 | – |
| SERVE-B C0.6 / W1.2 (council) | .968 / .880 | 1.0 / 3.0 / 16.0 / 31.0 | 1.5 / 15.5 / 32.0 / 61.5 | .9996 / .989 |
| SERVE-B, SUBMIT at the edge (worst) | .938 / .845 | 3.5 / 4.0 / 31.0 / 45.5 | 3.5 / 16.5 / 45.5 / 76.0 | – |
| same, recovery thinned by coexistence | .705 / .566 | 3.5 / 16.5 / 33.0 / 47.0 | 4.0 / 31.0 / 47.5 / 90.5 | – |
| LR_AP (other sensors; Coded 0.6 per 1.8 s, as SERVE-B) | .968 / .880 | 1.0 / 3.0 / 16.0 / 31.0 | 1.5 / 15.5 / 32.0 / 61.5 | .9996 / .989 |
| LR_AP + SUBMIT at the edge (worst; as the SERVE-B SUBMIT rows) | .938 / .845 (thinned .705 / .566) | 3.5 / 4.0 / 31.0 / 45.5 (thinned 3.5 / 16.5 / 33.0 / 47.0) | 3.5 / 16.5 / 45.5 / 76.0 (thinned 4.0 / 31.0 / 47.5 / 90.5) | – |
| NORMAL_LR | .977 / .904 | 1.0 / 2.5 / 16.0 / 30.5 | 1.5 / 4.0 / 31.0 / 47.5 | .9998 / .9935 |

**Reading the table:**
- **Normal operation** goes from p99 = never to 2.5 s. This fixes a latent field defect, and the fix does not depend on the portal.
- **Changing SERVE from B to A** (answer 7):
  - portal p99 at p_loss 0.3: 16 → 4 s;
  - worst alignment at p_loss 0.3: p99 31 → 17 s;
  - worst alignment at p_loss 0.5: p99 45.5 → 31.5 s.
- **Pulse rate matters.** With one LIST per minute or a user Rescan every 20 s, portal p99 stays ≤ 16 s (0.3) / ≤ 32 s (0.5), and LR_AP sets both maxima. At the I2b ceiling, with 2.5 s pulses every 12.5-14.5 s, p99 reaches 30.5 / 32.5 s.
- **The worst rows are LR_AP with a Submit on another sensor's leak edge at p_loss 0.5:** p99 45.5-47.5 s and p99.9 76-90.5 s. LR_AP exists only while a leak incident is open and the valve is unreachable, for at most 10 min per incident.
- **Outside LR, the worst is SERVE-A** with a Submit on the edge and a thinned recovery: p99.9 60.5 s. SERVE-A at the I2b ceiling is also 60.5 s.
- All of these are delays. The leak is still detected.

**Side note, cadence 8 s** (not the product cadence; p99 at p_loss 0.3 / 0.5):

| Row | p99 (s) |
|---|---|
| NORMAL | 2.5 / 3.5 |
| AP_IDLE | 3.0 / 8.5 |
| SERVE-A | 4.0 / 10.5 |
| SERVE-B | 9.5 / 18.5 |
| SERVE-A with Submit at the edge | 10.0 / 18.5 (full recovery); 17.0 / 25.5 (thinned) |

### 5.2 Short wettings: the physical ceiling

FW 1.1.0 advertises `leak=1` only while the probe is wet. The dry edge rewrites the payload at once. A wetting of length d is therefore on the air for only d seconds, and **no receiver can guarantee to catch it**.

P(detected) for d = 0.3 / 0.6 / 1.0 / 2.0 / 4.0 s [model]:

| Scheme | p_loss 0.3 | p_loss 0.5 |
|---|---|---|
| **Ceiling: 100 % Coded, continuous** | .69 / .90 / **.97** / 1.00 / 1.00 | .49 / .74 / **.88** / .98 / 1.00 |
| NORMAL N_CODED | .54 / .80 / .92 / .99 / 1.00 | .39 / .63 / .79 / .94 / 1.00 |
| Today, NORMAL | .34 / .57 / .74 / .91 / .97 | .24 / .43 / .59 / .80 / .95 |
| AP_IDLE | .42 / .73 / .86 / .98 / 1.00 | .30 / .54 / .70 / .89 / .99 |
| **SERVE-A** | .32 / .56 / **.78** / .93 / .99 | .23 / .41 / .60 / .81 / .96 |
| SERVE-B (council); LR_AP has the same Coded share | .21 / .37 / .56 / .83 / .97 | .15 / .27 / .42 / .67 / .88 |
| Inside a pulse (≤ 2.8 s) | 0 | 0 |

**What this means under answer (7):**
1. **A wetting that lasts one edge burst (4 s) or longer is detected** in these proportions:
   - p_loss 0.3: ≥ .99 in NORMAL, AP_IDLE and SERVE-A;
   - p_loss 0.5: NORMAL .995, AP_IDLE .987, SERVE-A .958;
   - SERVE-B, and LR_AP with the same Coded share: .968 / .879.

   Persistent leaks follow §5.1.
2. **A dripping source makes repeated edges,** and each edge is a new chance.
3. **The residual is a single wetting shorter than one edge burst (4 s) that dries by itself.** Below about 2 s, even continuous Coded scanning misses some. A 2 s wetting in SERVE-A is missed 7 % of the time (p_loss 0.3) and 19 % (0.5). The residual is largest while a phone is being served, which lasts minutes per setup or outage.

The hub-side lever is the SERVE rung (D8). SERVE-A-thin would move idle-page time to AP_IDLE density (.86 for a 1 s wetting). The only complete fix is a wet latch on the sensor. It is not in this release (answer 1), and this is the only place this document says so.

### 5.3 The dead valve (E3/E4: valve battery flat)

- **Leak detection does not depend on the valve.** There is no 100 %-duty hunt and no 30 s NULL-parameter initiator anywhere.
- **Leak pending,** with the trigger of §4.1: valve provisioned, valve unlinked, and either RMLEAK / CLOSE pended or an incident latched with the interlock not confirmed (WP6):
  - LR_AP runs 1M at 67 % for 30 s. It then runs [1M 0.6][W 0.3][C 0.6][W 0.3]: 33 % 1M with a 33 % Wi-Fi share, one join assist per 60 s, and router connects.
  - A valve powered during the incident is heard in about 1-4 s and claimed in ≤ 2.5 s.
  - Then **RMLEAK is written, then CLOSE**, in the unchanged order.
  - Meanwhile the hub can rejoin the router and send the cloud alert.
  - **The LR overlay ends 10 min after the incident.** The hub then returns to its normal mode. NORMAL's 20 % passive 1M re-finds a powered valve in 1-3 s. While the incident is latched, AP modes keep their discovery slots without back-off. A pended RMLEAK / CLOSE stays pended and is written at the next claim.
  - Shutoff is **delayed** until the valve is reachable; the leak is not missed (D5).
- **No leak pending:**
  - NORMAL re-finds a powered valve in about 1-3 s;
  - AP modes use discovery slots, about 5 s in AP_IDLE;
  - in SERVE, claims wait until the page is idle;
  - the valve goes CRITICAL after 3 min.

### 5.4 Dry-sensor health

A false offline means a sensor unheard for 600 s or more. Rates per 24 h:

| Profile | p_loss 0.3 | p_loss 0.5 |
|---|---|---|
| Today, NORMAL | 3.6e-2 (max 1.0) | 5.6e-2 (max 1.0) |
| N_CODED | 2.5e-10 | 3.1e-6 |
| N_MIXED | 1.4e-4 | 3.6e-2 (max .23) |
| AP_IDLE | 4.1e-9 | 1.0e-4 |
| AP_IDLE + discovery | 6.8e-5 | 2.5e-2 (max .20), bounded by the back-off |
| SERVE-A held 24 h | 6.8e-5 | 2.5e-2 |
| NORMAL_LR held 24 h | 2.9e-3 (max .035) | 1.5e-1 (max .80) |

Two rows describe holds that cannot happen:
- **SERVE for 24 h.** SERVE needs page activity, and code.js stops polling after 60 s without a touch.
- **NORMAL_LR for 24 h.** The LR overlay ends 10 min after the incident. That cap is what keeps the widened trigger from producing the last row.

The health hold is removed in the **same commit** as the uncapped pause (WP8).

### 5.5 Residual leak risks, stated plainly

1. **Short wettings** while serving (§5.2). Hub-side, the SERVE rung reduces them; the sensor-side fix is outside this release.
2. **p_loss and Ta are uncalibrated.** G0 measures both, and the model is re-run before WP8.
3. **Router beacon (TBTT) correlation in NORMAL and TAIL.** A sensor whose Ta is about 404-412 ms loses correlated adverts. At cadence 15 s this lasts about one heartbeat (p99 about 16 s instead of 2.5 s). G0's Ta tells whether any sensor sits in that band.
4. **A provisioned sensor that is never heard** stays "unknown". It costs discovery slots (backed off) and is reported offline.
5. **A mixed fleet** (known-1M sensors, D1) weakens NORMAL's dry figure at high loss and cuts AP-mode Wi-Fi to 33 %. An unknown-PHY 1M sensor in an AP mode is covered only by discovery slots. This case is not modelled.

---

## 6. The setup portal: component work in 2.1.4

### 6.1 How the component is carried

**Why the component builds with local edits today.** Its `.component_hash` matches `dependencies.lock`, and strict checksums are off, so edits are tolerated [code].

**The weaknesses of staying in place:**
- `CHECKSUMS.json` is stale, so `idf.py fullclean` already raises `ComponentModifiedError`.
- `IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS=1`, or deleting the directory, silently restores pristine 0.0.4.

**Recommendation (D11):**
- Make **the first commit of this work on `fix/2.1.4` a pure move** to `components/wifi_portal`, with byte-identical files.
- Drop `ankayca/esp32-wifi-manager` from `main/idf_component.yml` and re-solve `dependencies.lock`. That needs network access once, on your machine.
- `main/CMakeLists.txt` has no `REQUIRES`, so nothing else changes.
- `.text` and `.bss` stay identical; `__FILE__` strings shift `.rodata` by tens of bytes.

If you decline, patch in place:
- Leave `.component_hash` and `CHECKSUMS.json` untouched.
- Add C0: `WIFI_MANAGER_LOCAL_PATCH_LEVEL`, with an `#error` in `app_wifi.c` below the expected level.
- Keep a patch register in HANDOFF.

### 6.2 Patch catalogue (merged from both reviews)

| ID | What | Where (`520b17a`) | Enables or removes | Risk |
|---|---|---|---|---|
| C0 | Patch-level guard (only if not vendored) | `wifi_manager.h`, `app_wifi.c` | A silent re-resolve becomes a build error | none |
| C1 | No credentials in logs: SSID and `pwd_len` only | `http_app.c:177`; `wifi_manager.c:267/377` (INFO), `:291/379` | The WARN caps in `main.c:69-70` become optional. Re-run the production-tool boot-log parse | very low |
| C2 | **Abort paths and memory safety.** (a) No mallocs in the event handler (`:585/661/731`); pass scalars. (b) Soft-fail `:999` plus `esp_wifi_clear_ap_list()`, also when SCAN_DONE status ≠ 0. (c) Soft-fail `:1074/:1081`: `ESP_ERR_WIFI_STATE` counts as a failed attempt, and bits are set only after a start. (d) `:421/:1236/:1340` logged. (e) Bounded `snprintf` JSON builders. Control characters in SSIDs are replaced (fixes N4, the heap overflow). Invalid UTF-8 bytes are emitted as `\u00XX` and the entry is marked `"raw":1`. (f) NVS handle and lock released on every path (`:306`, `:339-368`). (g) Host and POST headers in stack buffers, URL strings as literals. (h) Queue depth 3 → 8 | wifi_manager.c, http_app.c, json.c | Closes 14 reboot paths. Removes code.js `RESUME_GUARD_MS` (8 s). A neighbouring AP with Latin-1 bytes in its SSID no longer makes `/ap.json` or `status.json` invalid JSON | low |
| C2b | AP-list memory: records read one at a time into a compact stack array; `accessp_json` allocated only while the AP is up | `:193-194`, `:992-1016` | Free heap +1.4 KB always, +2.9 KB in NORMAL | low-medium |
| C3 | httpd only while the AP is up (no start at boot or after STOP_AP). Every handler checks the socket's local address (IPv4 or v4-mapped) and returns 403 otherwise | `:971`, `:1268-1269`; `http_app.c:122/145/216` | Home-LAN exposure closed without timing assumptions; about +5 KB free in STA mode. The council's planned app-side `http_app_stop` calls are not needed | low |
| C4 | **DNS kept START_AP → STOP_AP** (delete `:1308`), **bound to 10.10.0.1**, rewritten (§6.2a) | `dns_server.c/.h`, `wifi_manager.c:1307-1308` | Captive detection in the TAIL, after a re-join and in fallback (fixes N8). EDNS0 clients work. No crash on short datagrams | medium |
| C5 | The component's retry timer starts only while `AP_STARTED_BIT` is clear | `:1193-1194` | I9 at the source; the council's planned app-side `xTimerStop` is not needed | low |
| C6 | httpd bounds: `max_open_sockets` 10 → **5 in the RC** (the G-CNA ladder 4/5/7 decides); `open_fn` refuses a new session below 12 KB internal-DMA free; foreign-Host sessions closed after the 302; a separate status-JSON mutex, copied to the stack and sent unlocked; queue posts bounded at 200 ms (else 503) | `http_app.c:443-514` and the handlers | Bounds the E2 route; removes an `httpd_stop` deadlock class | medium |
| C7 | **gzip at build time** (CMake + `gz_asset.py`, `mtime=0`); `Content-Encoding: gzip`, `Vary`; `Cache-Control: no-store` on everything; HEAD sends headers only; the URI is matched on its path only; `compress.bat` deleted | CMakeLists `:5`; `http_app.c:75-82, 253-277, 368-376` | 57,383 → 18,520 B on the air; **flash −38.9 KB**; each text asset fits one send buffer | low |
| C8 | **Connect ownership and transactional credentials.** Request kinds USER / APP_RETRY / AUTO / RESTORE. A USER request waits for the running attempt (≤ 8 s, then aborts it). POST writes a *candidate*; live config and NVS change only at the candidate's IP; failure keeps the old network. Intake: `X-Custom-enc: pct` percent-decoding into `ssid[33]` / `pwd[65]`; raw headers still accepted when the flag is absent; empty password allowed (open networks); lengths checked after decoding; 400 with a JSON reason; `X-Custom-chan`. `status.json` gains `"reason"` and `"pend"`, and prints the SSID with `strnlen(…, 32)`. Forget: an idle STA is erased directly. The NVS layout stays as it is, because `reset_button.c`'s `erase_wifi_credentials()` depends on it: namespace `"espwifimgr"`, blobs `"ssid"` (32 B) and `"password"` (64 B), empty SSID = nothing saved | `:763-773`, `:1057-1221`, `:1277-1345`; `http_app.c:122-213` | Deletes `s_retry_sent/seen`, `retry_pending`, `forget_post`, `s_forget_pending`. Fixes the wrong-password hazard at `app_wifi.c:355-360` | **high** |
| C9 | **Page** (code.js, index.html): fetch `status.json` **before rendering** (urc 0 → success view with Finish; `pend` → "Connecting to «SSID»"; urc 3 → fallback banner); percent-encode (for `"raw":1` entries, code points ≤ 0xFF are encoded as single percent-encoded bytes); open networks; password checks (8-63, or 64 hex); reason texts; 30 s connect timeout with Retry; empty-list state; Rescan; `?bg=1` on background polls; `status.json` polled every 950 ms only while connecting, otherwise every 3.8 s and only while the user is active; manual SSID not trimmed; no Disconnect button (D9) | code.js, index.html | Seamless re-open and failure handling | low |
| C10a | Activity hook `http_app_set_activity_hook(fn)`: kinds DNS, PROBE_302, PAGE, API_USER, API_BG, STATUS, with the client IPv4. Called from the httpd task (PAGE, API, PROBE_302, STATUS) and from the DNS task (DNS) | http_app.c, dns_server.c | Feeds `page_active`, `hot`, the first-DNS tick and the per-station line. Replaces the council's planned `cb_scan_start` stamp and `s_self_scan` | low |
| C10b | `GET /ap.json` serves the cache only (delete `:298-299`); new `POST /scan.json`. **Lands in WP4 with C9.** In WP4, the first page request with an empty or stale cache and `POST /scan.json` (≥ 20 s apart) each order a plain scan. In WP8 both become LIST pulse requests | http_app.c | No off-channel time per poll (I7). G1 measures a build that has it | medium |
| C11 | Lab knobs: 11b rate on the AP; `coex_background_scan` | `:917-979` | G1 data only | none in production |
| C12 | `wifi_manager_ap_stop_in(ms)`; `POST /finish.json` (only with the STA connected, else 409) | wifi_manager.c/.h, http_app.c | Finish; replaces the council's planned `extern` timer use | low |
| C13 | Wi-Fi parameters owned by the component: `ap_config.ap.csa_count = 3` (G0 decides 3 or 5) and `dtim_period = 1`, set explicitly (`:928-936` leaves them 0); scan dwell after `esp_wifi_start()`; channel hint at GOT_IP | `:928-979`, `:1295-1330` | Phones follow the channel switch; the council's planned app-side dwell workaround is not needed | low-medium |

#### 6.2a DNS responder specification (C4)

- **Stack and buffer.** 3,072 B stack as today, no heap. A 300 B receive buffer, never written beyond what was received.
- **Drop without reply:**
  - fewer than 17 B;
  - 300 B or more (the datagram may be truncated);
  - QR = 1;
  - internal-DMA free below 10 KB;
  - more than 20 replies per second.
- **Error replies (header only, QDCOUNT 0):**
  - OPCODE ≠ 0 → NOTIMP;
  - QDCOUNT ≠ 1, or AN or NS ≠ 0 → FORMERR;
  - a QNAME that fails the bounds walk (label ≤ 63, total ≤ 255, no compression pointers), or a question truncated before QTYPE / QCLASS → FORMERR.
- **Normal reply:**
  - the header, then the question copied exactly, never any bytes after it;
  - for QTYPE A or ANY with class IN or ANY, one answer: `c00c 0001 0001 0000003c 0004 0a0a0001` (**TTL 60 s**);
  - every other type (AAAA, HTTPS, SVCB, PTR, …) gets NOERROR with no answer;
  - if the query carried an OPT record, a minimal 11-byte one is appended: `00 0029 0200 00000000 0000`, with ARCOUNT = 1.
- **Flags:** QR, AA, RD copied, RA = 1, AD = 0, CD copied. The model output for an EDNS query is `1234 8580 0001 0001 0000 0001 <q> c00c…0a0a0001 0000290200000000000000`.
- **Lifecycle:**
  - bind `10.10.0.1:53` with `SO_REUSEADDR`;
  - if `socket()` or `bind()` fails, retry every 1 s;
  - `SO_RCVTIMEO` 500 ms plus a run flag; the task closes its own socket and exits; the stopper waits ≤ 1 s;
  - no `vTaskDelete` and no `exit()`.
- **Logging:** DEBUG per query; the activity hook records the first query per client.

### 6.3 Where the two reviews disagreed, and the choice

| Topic | Phone review | Patch review | Chosen | Why |
|---|---|---|---|---|
| DNS TTL | 60 s | 0 | **60 s** | Fewer queries under windowed radio, where Android loses about 5 s per lost query. OS and browser caches are per network or flushed on a network change. The phone uses the IP literal after the 302 |
| OPT in the reply | echo a minimal OPT | ARCOUNT 0 | **echo minimal OPT** | Both are RFC 6891-valid. Echoing avoids a downgrade-and-retry round trip by EDNS stubs (Windows, Linux) |
| 302 body | empty (proven with iOS) | short HTML | **empty** (`Content-Length: 0`) | Keep what the 2.1.3 log proves |
| Client without gzip | small plain page | 406 | **gzip when Accept-Encoding is absent or lists gzip; 406 only for an explicit identity-only request** | RFC 9110: an absent header means any coding is acceptable. Every target engine sends gzip |
| Caching | `no-store` on html/js/json | `no-cache` on html/js/css | **`no-store` on all** | httpd has no validators; 18 KB is cheap to refetch |
| Socket cap | 7 | 4 | **5 in the RC, ladder 4/5/7** | With I4 no MQTT socket competes during the AP. The cap bounds buffered data. G-CNA S1 decides |
| Encoding flag | `X-Custom-enc: uri` | `pct` | **`pct`** | – |
| Connect timeout on the page | 25 s | 30 s | **30 s** | SUBMIT ≤ 2.8 s plus coexistence; result ≤ 10 s typical |
| Submit fix | soft-fail and re-post | ownership queue (C8) | **C2c first (WP1), then C8 (WP4)** | C2c gives a safe intermediate build; C8 makes a Submit lossless |
| Join-assist end | lease + 1.5 s → max(lease + 1.5, first 302 or page + 0.3) | – | **the latter**, still clipped by the 2.8 s budget | It covers the probe only when join → probe ≤ about 2.5 s (E3: yes; 2.1.3 iPhone: 3.3 s, no). Otherwise iOS retries in about 1 s and Android in about 5 s (K1). A clipped assist hands over to provisional SERVE's Wi-Fi slot right after recovery (§4.4) |

### 6.4 What the component fixes let the branch delete

**Deleted from the branch** (code that exists at `520b17a`):

| Deleted | Replaced by | Package |
|---|---|---|
| code.js `RESUME_GUARD_MS` and `pollingSince` | C2c, C8 | WP4 |
| `s_retry_sent`, `s_retry_seen`, `retry_pending` and their rationale (`app_wifi.c:232-233, 374-442, 743, 824-878`) | the C8 callback kind | WP4 |
| `forget_post`, `s_forget_pending`, the post in `cb_disconnect_sta` (`:512-558, 713-722`) | C8 forget. The 10 s reset keeps calling `wifi_manager_disconnect_async()` | WP4 |
| The scan-stop path in `cb_scan_start` (`:467-497`) | C10b and the LIST pulse; deleted with the page chain and the holds | WP8 |

**Council items that were never built,** and are now done at the source:

| Council plan | Now |
|---|---|
| App-side `extern` stop of `wifi_manager_retry_timer` (council WP2) | C5 |
| App-side `extern` re-arm of `wifi_manager_shutdown_ap_timer` | C12 |
| App `http_app_stop()` in `cb_ap_stopped` and at the first boot GOT_IP | C3 |
| `s_self_scan` | C10a, C10b |
| App scan-dwell call and channel-hint write | C13 |
| Council D16 step 2 (LOCAL PATCH fallback) | C2 is in the release |

**`reset_button.c`:** the code is unchanged. Its NVS-lock and leak workarounds become redundant after C2f, but they are harmless. C8 keeps the keys, sizes and semantics that `erase_wifi_credentials()` relies on: `"espwifimgr"`, `"ssid"` 32 B, `"password"` 64 B, and empty SSID = nothing saved. G8x checks a 10 s reset with the STA idle, connecting and connected. The comment block on `execute_wifi_reset()` still describes `forget_post` and the portal priority window. It is updated in WP4 (forget) and WP8 (window).

---

## 7. iPhone and Android

### 7.1 What each OS does, and what the hub must do

**iPhone and iPad (iOS 17-26; macOS is the same)** [platform unless marked]
- **Probe.** DNS for `captive.apple.com` (A, plus HTTPS(65) or AAAA, plus DDR `_dns.resolver.arpa` [log]), then `GET /hotspot-detect.html`. Any body other than "Success" means captive, and the CNA sheet opens by itself.
- **If DNS or TCP fails:** "No Internet Connection" and **no sheet**.
- **CNA behaviour:**
  - a sandboxed WebView with no persistent storage;
  - it never offers "Done" here, because the network never gains internet; Cancel offers "Use Without Internet" or "Use Other Network";
  - **it closes when the association is lost.**
- **DNS retry** after a lost query: about 1 s.
- **Timing [log, 2.1.3]:** lease 1.17 s after the join, first DNS 3.30 s after the join.
- **MAC.** Private Wi-Fi Address; "Rotating" can change the MAC at re-join. Per-MAC limits are best effort, and the global caps bound the radio.
- **The hub must:**
  - answer A for every name (TTL 60);
  - return NODATA for HTTPS, AAAA and SVCB;
  - send a 302 to `http://10.10.0.1` for foreign hosts;
  - keep all of this up until STOP_AP;
  - keep the page free of external resources (already true);
  - render the result from `status.json` on load.

**Android AOSP (Pixel, Android 13-16)** [platform]
- **Probe.** `http://connectivitycheck.gstatic.com/generate_204` in parallel with `https://www.google.com/generate_204`; fallbacks `http://www.google.com/gen_204` and `play.googleapis.com`.
  - A 200-399 that is not 204 means captive: the "Sign in to Wi-Fi network" notification, and the sign-in page opens by itself when the user joined from Settings.
  - The HTTPS probe gets a fast RST from 10.10.0.1:443, which is correct.
- **If both probes fail** (no DNS): "no internet", re-evaluated after 1, 2, 4 s and so on, and possibly "stay connected?".
- **With mobile data on,** only the sign-in page, which is bound to the Wi-Fi, reliably reaches 10.10.0.1. **Detection must be reliable.**
- **DNS retry** after a lost query: about 5 s. This is the reason for the join-assist end rule and contingency K1.
- **Private DNS:**
  - Automatic: DoT to :853 gets an RST, then cleartext.
  - Strict: detection uses the bypass path. Whether that path sends EDNS0 is unverified, and C4 makes that moot (P-12).
- **MAC.** Randomised per SSID, possibly per join on open networks.

**Samsung One UI 6/7** [platform]
- The AOSP NetworkStack with Samsung's sign-in UI; it opens by itself on a user-initiated join.
- "Switch to mobile data" moves only the default route.
- Samsung-specific probe hosts do not matter: every name resolves to the hub and every foreign Host gets the 302. The portal log line records the hosts actually used.

**Android 10-11 and OEM skins** [platform]
- Same flow. OEM probe hosts are covered by hijack-all.
- The sign-in page may not open by itself; a notification is guaranteed.

**Windows 11** [platform]
- NCSI resolves `www.msftconnecttest.com` and fetches `/connecttest.txt`.
- A 302 means "Action needed", and the browser opens.
- NCSI sends EDNS0, so today's malformed replies probably break it; C4 fixes that.
- A remembered SSID auto-joins; that is the E2 flood.

### 7.2 Where it breaks today

| # | Break | Affects | Evidence | Fix |
|---|---|---|---|---|
| B1 | DNS stops at GOT_IP; a phone that re-joins in the tail cannot be detected | all | `:1308`; E3, E4 [log] | C4 |
| B2 | EDNS0 replies malformed; AAAA / HTTPS answered with an A record | Windows, Linux, possibly strict-DNS Android | byte model [code] | C4 |
| B3 | A datagram < 12 B to port 53 reboots the hub; a query ≥ 80 B writes past `data[80]` | anyone on the open AP | [code] | C4 |
| B4 | A Submit during an attempt reboots the hub; the page adds an 8 s guard | all | `:1074/:1081` [code] | C2c, C8 |
| B5 | Open network → HTTP 400 and an endless spinner; a non-Latin-1 character makes `fetch` throw; Latin-1 is sent as the wrong bytes; a trailing space in a password is trimmed; a 32-character SSID leaks the password into invalid JSON; a neighbouring SSID with invalid UTF-8 empties the list | all | [code] | C2e, C8, C9 |
| B6 | The channel switch at Submit: `csa_count = 0` | all | `:928-936` [code] | C13 |
| B7 | A reopened sign-in page shows the network list, not the result | all | code.js `:188-217` [code] | C9 |
| B8 | The radio: no lease while BLE scans with the STA idle | all, when sensors are provisioned | E1, E4 [log] | §4 |
| B9 | `/ap.json` orders an all-channel scan on every 3.8 s poll | all | `http_app.c:298` [code] | C10b |

### 7.3 The flow after the change

**First setup (nothing provisioned: BLE_IDLE).** Timings come from E3 and the 2.1.3 log [log]. Everything else is an estimate that G-CNA measures.

| Step | iPhone | Android | Hub | Target |
|---|---|---|---|---|
| 1. Tap `WiFi-Hub-<id>` in Settings | joins | joins | lease | lease ≤ 1.5 s after the join (E3 0.12 s; 2.1.3 1.17 s) |
| 2. OS probe | DNS, then 302 | DNS, then 302 on the HTTP probe | A = 10.10.0.1; 302 | DNS 0.2-2.2 s after the lease |
| 3. Sign-in UI | CNA sheet opens | sign-in page opens (notification on older versions) | serves 18.5 KB gzipped | iPhone ≤ 8 s after the join; Pixel / Samsung ≤ 10 s |
| 4. Page | `status.json` first, then the list (from the cache, or a scan on the first request) | same | – | list ≤ 3 s from the cache, ≤ 5 s when a scan runs first |
| 5. Connect | instant (no 8 s guard); percent-encoded; channel hint | same | candidate; STA scans the hinted channel first | – |
| 6. Channel switch | follows the CSA, or drops and re-joins, and the CNA re-opens showing **the result** | same, via the sign-in page | SoftAP moves with `csa_count` 3; DNS and httpd still up | – |
| 7. Result | "WiFiHub is connected to «SSID»" plus **Finish**; or "Wrong password" / "Network not found - is it 2.4 GHz and in range?" | same | candidate committed only at IP | ≤ 10 s |
| 8. Finish | AP stops, the CNA closes, the phone rejoins home Wi-Fi | sign-in closes, phone back on home Wi-Fi or mobile data | AP stops at max(IP + 5 s, Finish + 2 s); cloud admitted after STOP_AP | ≤ 15 s |

Without Finish, the AP stops at IP + 60 s, or 15 s after the phone leaves ("Use Other Network").

**10 s reset with sensors provisioned.**
1. The hub reboots into AP_IDLE (0.6 Coded / 0.3 Wi-Fi).
2. The phone joins. U1b is measured by G1.
3. JOIN_ASSIST: BLE off, AP on its channel. The lease should take ≤ 1.5 s; the 2.8 s cap is the hard bound. The assist ends at the first 302 or page + 0.3 s, or at the blind budget, whichever comes first.
4. 1.2 s of Coded recovery.
5. Provisional SERVE, then SERVE. If the assist was clipped before the probe, provisional SERVE starts with its Wi-Fi slot.
6. Steps 4-8 of the first-setup table follow. The SUBMIT pulse is ≤ 2.8 s.

Leak protection runs throughout this flow (§5). Today it is 0 % here.

**Router outage (fallback AP, STA idle).**
- The page opens with the banner "WiFiHub lost its connection to «SSID». It keeps retrying. Choose a network to change it."
- When the router returns: "WiFiHub reconnected to «SSID»". The AP stops per §4.6.
- A wrong password during an outage leaves the old router in place (C8), so the hub rejoins it when it comes back.

### 7.4 What makes it seamless, as a checklist

1. DNS answered correctly from START_AP to STOP_AP, on the AP IP only (C4).
2. The 302 for every foreign Host, never 204 or "Success" (unchanged; I11).
3. A radio window where the phone needs it. JOIN_ASSIST covers the first probe when it comes within the blind budget; otherwise provisional SERVE's Wi-Fi slot follows right after recovery. SERVE BLE blocks are ≤ 0.6 s (§4).
4. A small page: 18.5 KB, one send buffer per asset (C7).
5. A lossless, reboot-free Connect: no guard delay, UTF-8, open networks, candidate credentials (C2c, C8, C9).
6. Surviving the channel switch: explicit CSA; the result rendered from `status.json` on any re-open (C13, C9).
7. A self-closing finish: Finish stops the AP, and both OSes close the sign-in window on association loss (C12).
8. Specific failure texts from the disconnect reason (C8, C9).

### 7.5 Device test matrix (gate G-CNA)

**Devices, all with mobile data ON:**
- a recent iPhone, iOS 18/26, Private Address "Rotating";
- a Pixel, Android 15/16, Private DNS Automatic, then Strict (`dns.google`);
- a Samsung Galaxy, One UI 6/7, "Switch to mobile data" on;
- an Android 10-11 phone, preferably a non-Google skin;
- a Windows 11 laptop;
- optionally macOS, and Linux with `dig`.

**States:**
- S1: first setup, BLE_IDLE.
- S2: 10 s reset with sensors provisioned; valve unpowered first, then linked.
- S3: router-outage fallback.

**When:** S1 after WP4 (this run also fixes the httpd socket cap); S2 and S3 after WP8. Steps P-1 to P-6 run 10 times per device per state.

| Step | Action | Pass (hub UART evidence) |
|---|---|---|
| P-1 | Tap the SSID | joined ≤ 5 s p95; 0 "Unable to join" |
| P-2 | – | lease ≤ 3 s after the join |
| P-3 | – | first DNS for the probe host ≤ 3 s after the lease (portal line) |
| P-4 | – | Sign-in UI opens by itself: iPhone ≤ 8 s after the join; Pixel and Samsung ≤ 10 s; Android 10-11 notification ≤ 15 s; Windows browser ≤ 15 s. **Never "No internet" without a sign-in path** |
| P-5 | – | page rendered ≤ 3 s after the UI opens; list non-empty ≤ 5 s, or the empty state at 8 s |
| P-6 | Correct WPA2 password | result ≤ 10 s; **0 reboots**; Connect sent at once |
| P-7 | Open network; `Café 5`; emoji SSID (test router); password with a trailing space; 8, 63 and 64-hex passwords; 32-character SSID; wrong password; a neighbouring SSID with Latin-1 bytes | each valid case succeeds; "Wrong password" ≤ 20 s; `status.json` never contains password bytes; `/ap.json` stays valid JSON; 0 reboots; the previous network is kept after a failure |
| P-8 | Router on channel 1 or 6, AP on 11 | either no `left, reason=` between Submit and IP, or the re-opened UI shows **success** ≤ 10 s after the re-join. Record `csa_count` |
| P-9 | Tap Finish | STOP_AP ≤ 3 s; phone on home Wi-Fi ≤ 15 s; the sign-in window closes by itself |
| P-10 | Android: dismiss the sign-in, reopen it from the notification | the page loads |
| P-11 | Router off (fallback), then on | P-1 to P-5 pass; "lost connection", then "reconnected"; AP stop ≤ 20 s with the phone on it, ≤ 1 s with no station |
| P-12 | Pixel with Strict Private DNS | P-4 passes |
| P-13 | From the laptop: `dig @10.10.0.1` for A (EDNS and `+noedns`), AAAA, HTTPS, `_dns.resolver.arpa` SVCB; a 250-character name; UDP datagrams of 1 B, 11 B and 600 B; a query to the hub's LAN IP | A = 10.10.0.1, TTL 60, no "extra bytes" warning; NOERROR / 0 answers for the others; odd datagrams dropped; no LAN answer; **no reboot** |
| P-14 | `curl -I http://10.10.0.1/`; `curl --compressed http://example.com/`; 20 parallel connections; `curl -X DELETE` and `POST` from the home LAN, in the TAIL and after STOP_AP | HEAD returns headers only; 302; no failed page load; LAN refused or 403, with credentials intact |
| P-15 | Throughout | internal-DMA free ≥ 8 KB; 0 failed allocations; heap flat within ±1 KB over 10 AP cycles |

---

## 8. Memory

"Internal-DMA" means `MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`. The pass/fail figure is the **failed-allocation count**. `min_ever` is only a lower bound.

| State | Today (E4) free / largest | 2.1.4 through WP8 (estimate) | With WP9, every line shipped (estimate; measured one line at a time) |
|---|---|---|---|
| Connected, steady | 33.2 / 16.4 KB | **≈ 45 KB** / ≥ 16 KB (httpd off +5, peer certificate not kept +4, C2b +2.9) | ≈ 85-95 KB |
| STA down, MQTT stopped | 50.3 KB | ≈ 58 KB | ≈ 100-110 KB |
| Fallback AP + 1 idle station | 41.6 KB | ≈ 43 KB | ≈ 85-95 KB |
| AP + a phone using the page | 34.5 → 15.8 KB | **≥ ≈ 24 KB**: no TLS; 16/16 buffers; one-buffer responses; 5 sockets | ≥ ≈ 65 KB |
| TLS handshake minimum | **152 B** | admitted only with the AP down and ≥ 36 KB free; min ≥ ≈ 9 KB, ≈ 20 KB expected | ≥ ≈ 50 KB |
| Contiguous block per TLS write | 4,437 B | 2,389 B | same |
| E2 laptop flood | 1,184 B | bounded: 16/16 buffers; `open_fn` gate at 12 KB; DNS gate at 10 KB plus 20/s; socket cap; foreign-Host close; **no reboot on NO_MEM** (C2) | same |

**Floors:**
- **2.1.4 hard floor (G3):** internal-DMA ≥ 8 KB and largest block ≥ 4.5 KB at every 1 s sample through the E2 and E4 replays, with 0 failed allocations.
- **If WP9 ships (G4b):** ≥ 16 KB / ≥ 8 KB with 0 failures over a 24 h soak that includes 10 outages, 10 portal sessions and a laptop.

**By construction vs measured:**
- **By construction:** TLS never overlaps the AP, and the portal stack has no panic path left (C2, C4).
- **Measured:** allocations inside esp-mqtt, NimBLE and the Wi-Fi driver.

**G3 contingency chain,** applied in order until the replay passes:
1. `DYNAMIC_RX_BUFFER_NUM` 12.
2. httpd socket cap 4.
3. Scoped idle-station deauth: only stations that never made an HTTP request, with a short deny, never the last submitter. That would let the station cap drop to 2-3.

The station cap stays **4** until then (red-team SR-10: a cap of 2 lets remembered devices lock the user out).

**WP9 memory set.** One line per build; a line ships only if it pays and its gate passes.

| Line | Today | Estimate | Guarded by |
|---|---|---|---|
| `# CONFIG_ESP_WIFI_IRAM_OPT is not set` and `# CONFIG_ESP_WIFI_RX_IRAM_OPT is not set` | y / y | **+27 KB or more** (Kconfig help, `esp_wifi/Kconfig:280, :298`: > 10 KB and > 17 KB) | G-CNA page-load p90 within +20 %; TLS publish regression; the IRAM gate re-baselined |
| `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6` | 10 | +6.4 KB | G-CNA; G3 |
| `CONFIG_BT_NIMBLE_TRANSPORT_EVT_COUNT=12` | 30 | +4.6 KB | **VAL-01 and G6b.** These buffers carry every HCI event except advertising reports, which use `EVT_DISCARD_COUNT` (8). Secondary check: G2 adverts per burst within ±5 % of G0 |
| `CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT=8`, `CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT=12` | 24 / 24 | +4 / +3.8 KB | G6b, VAL-01 |
| `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1` | 3 | small | only if exactly one valve is ever linked |
| Roles `PERIPHERAL` / `BROADCASTER` = n | y | small | only if it links: the hub calls `ble_svc_gap_init` / `ble_svc_gatt_init` but never advertises |
| `CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=y`; `CONFIG_BT_NIMBLE_LOG_LEVEL_WARNING=y` | n / INFO | small | TLS / DPS regression |

If every line ships, the set frees an estimated **+40-50 KB**; the two IRAM lines alone free more than 27 KB. The 16 KB DCache change is not proposed unless the floor still fails.

---

## 9. Security of the setup AP (WPA2 excluded by the user)

| Exposure | Today | 2.1.4 |
|---|---|---|
| httpd on the home LAN with unauthenticated `DELETE` / `POST /connect.json` | always | **Closed:** httpd runs only while the AP is up, and every handler answers only on the AP interface (C3) |
| DELETE → erase → uncapped BLE pause and health hold (a silent leak-protection kill) | open | **Closed:** the no-credential portal keeps scanning (D2), and the DELETE route is removed (D9) |
| DNS: crash on < 12 B, overflow on ≥ 80 B, bound to the STA IP (LAN hijack if kept) | open | **Closed** (C4) |
| Heap overflow from beaconed SSIDs with control characters (N4) | open | **Closed** (C2e) |
| Password in log format strings | hidden only by a WARN cap | **Removed** (C1) |
| Anyone in range of the open AP can submit credentials | open | **Residual, accepted with the WPA2 exclusion.** A failing submit leaves the working network intact (C8). A succeeding submit to the attacker's own AP takes the hub off the cloud; TLS validation prevents data exposure, and local leak protection is unaffected |
| Remembered devices re-join the open AP (E2) | 4 stations, flood | 4 stations kept; the flood is bounded (§8). Remembered devices cannot hold SERVE or defer retries |

**Housekeeping, not firmware:** a Wi-Fi password is in `docs/field_logs/2.1.3/UART logs.txt:137`, and commit `6b84ae3` holds one (per HANDOFF). Redact both before any push.

---

## 10. What changes where (static deltas; estimates, recorded per WP)

| Where | Change | Flash | `.bss` |
|---|---|---|---|
| **New** `main/radio_policy/radio_policy.{c,h}` (replaces `portal_priority.h`) | Fact inputs, modes, X-macro table and asserts plus self-test, pulses, blind budget, grants, rung and "hot" logic, LR overlay cap, AP-tail policy through C12, 60 s summary | +4.6 KB | +190 B |
| `main/app_wifi/app_wifi.c` | Delete the window, priority raise, hold families, page chain, QUIET, long listen (about 450 lines), and §6.4's branch list. Callbacks become flags only. Register `AP_STAIPASSIGNED` and the activity hook. Keep `router_retry()` (grant, jitter, page-keyed deferral) and the station log | −4.1 KB | −80 B |
| `main/ble_leak_scanner/app_ble_leak.c` | Single executor, profiles, recovery, PHY learning **and persistence** (no decay), per-sensor counters, starvation guard, duty watchdog, priority 6 | +2.1 KB | +80 B |
| `main/ble_valve/app_ble_valve.c` | Remove its own scan; passive hunt; claims by grant; stale-handle check; LR overlay; **widened LR trigger with the 10 min cap** | −0.9 KB | +8 B |
| `health_engine.c`; `app_iothub.c`; `dps_client.c`; `monitoring.c`; `main.c` and Kconfig | As in the council design: stop the hold calls; admission; DPS abort hook; failed-alloc counters; log levels; `APP_RADIO_LAB`; sdkconfig guards | +1.7 KB | +44 B |
| Component `wifi_manager.c/.h` | C1, C2, C2b, C3, C4 (stop), C5, C8, C12, C13 | +2.3 KB | +120 B |
| Component `http_app.c` | C1, C2g, C3, C6, C7 (headers), C8 intake, C10a/b, C12 | +0.8 KB | +8 B |
| Component `dns_server.c/.h` | C4 rewrite | −0.1 KB | +12 B |
| Component page and CMake | C7 gzip (−38.9 KB); C9 (+0.3 KB after gzip) | −38.6 KB | 0 |
| sdkconfig | §4.9 | about −1.5 KB | about −40 B |
| **Total through WP8** | | **≈ −34 KB** | **≈ +340 B** (gate re-baselined to ≤ +450 B) |

- IRAM: 0 expected. DIRAM `.text` stays 113,387 B through WP8.
- New links to watch at the IRAM gate: `esp_wifi_clear_ap_list`, `esp_wifi_scan_get_ap_record`, `lwip_getsockname` / `getpeername`, `httpd_sess_trigger_close`, `esp_wifi_ap_get_sta_list`, `esp_wifi_set_scan_parameters`, `esp_wifi_sta_get_ap_info`, `heap_caps_register_failed_alloc_callback`, `ble_gap_conn_find`.
- No new task and no new timer.
- The DNS task now also lives through the tail: ≤ 60 s, or ≤ 2 s after Finish.
- Free heap at rest while connected: about +12 KB.

> **Update 2026-10-02: the `.bss` gate (`HANDOFF.md` §15n, §15o).** Two packages that are not in the table above were added before CP6. WP2c's +6,301 B (`cloud_tx`'s static stack and TCB, its two static queues, the busy mutex and the publish gate, the snapshot's flight context) is outside the ≤ +450 B gate: the I10 exception the user approved (§3). WP2d's +477 B (the rules engine's copy of the last applied device set, about 376 B, its four kept leak reports and flags; the user's decision D5: a busy lock no longer drops a wet report) counts against it. Recorded per package against `520b17a`: WP0 +121, WP1 −38, WP2 +86, WP2b −75, WP2c +6,301, WP2d +477 B (object figures; CP6 measures the link). Without WP2c that is **+571 B, 121 B over the gate** before WP3-WP8's planned ≈ +340 B: whether WP2d's copy counts under D5's approval, or WP9 recovers it, is the user's call (`HANDOFF.md` §15o). Flash, also outside the table: WP2c about +7.7 KB, WP2d about +2.7 KB (objects, `.text` and `.rodata`). **Decided by the user on 2026-10-02: WP2d's +477 B is recorded under D5's approval, like WP2c's under I10's exception.** With WP2e's +29 B (the twin report's counters and flags, the user's TW-1 decision of the same day; flash about +0.8 KB; `HANDOFF.md` §15p) the gate stands at **+123 B**, so WP3-WP8's ≈ +340 B would end about 13 B over: for WP9.

> **Update 2026-10-06: the `.bss` gate with WP3-WP6 (`HANDOFF.md` §15q-§15t).** Object figures at `-Og` against `545b8f2`, the same regenerated `sdkconfig.h` on both sides: WP3 core +19 B (`app_iothub.c` +5, `telemetry_v2.c` +4, `provisioning_manager.c` +9, `rules_engine.c` +1), WP4 +57 B (`wifi_manager.c` +36, `http_app.c` +12, `app_wifi.c` +9; `.data` +8 B, the status spinlock), the BLE stream (WP3's valve items, WP5, WP6) +79 B (`app_ble_leak.c` +50, `app_ble_valve.c` +29; `.data` +2 B): **+155 B**, so the gate reads **+278 B** before the `sdkconfig` lines. The re-attempt line's NimBLE tables, −2,008 B (§4.9's note), bring it to **about −1.73 KB against `520b17a`**: the 13 B shortfall foreseen for WP9 is gone, with about 2.2 KB of room for WP7-WP8's planned `radio_policy` (+190 B) and the rest of the table. Flash against `545b8f2` (objects; CP7 measures the link): code +18.2 KB, `.rodata` +6.5 KB, the gzipped page −37.2 KB (57,399 → 20,204 B embedded), newly linked library code about +0.75 KB, the `sdkconfig` lines about −1.8 KB: about **−13.5 KB** in all. IRAM 0: no changed object has an IRAM, DRAM or RTC section, and every newly linked function is in flash or was linked already (`calloc`, `realloc`, `esp_random` in IRAM since CP5).

---

## 11. Work packages (one release, a gate after each)

Every package runs the build gate: DIRAM `.text`, `.bss` and flash are recorded.

| WP | Content | Risk | Gate |
|---|---|---|---|
| **WP-V** | Pure move to `components/wifi_portal`, manifest and lock (D11); the first commit of this work on `fix/2.1.4`. Otherwise C0 in place | low | identical `.text` / `.bss`; 10 s reset ×3 |
| **WP0** | Instrumentation, no behaviour change: failed-alloc counters, internal-DMA sampler, `AP_STAIPASSIGNED`, adverts per burst and inter-advert delta per sensor, **C10a hook (store and log only)**, C1. `esp_log_level_set("wifi", ESP_LOG_INFO)` right after `wifi_manager_start()` in the instrumentation build, so the driver's `csa_count` line is visible. AP and STA channels logged at every AP start, STA connect and STA loss | low | **G0** |
| **WP1** | The portal cannot reboot the hub: C2 (a-h), C2b, C4, C5 | medium | **G-FAULT** (WP1 subset), G8, P-13, 10 s reset ×10 |
| **WP2** | Cloud admission and AP lifecycle: flag-only callbacks, admission, DPS abort hook, `outbox.limit`, lifecycle republish; C3; C12 (API only); the app AP-tail policy | medium | G3-lite (E4 replay), DPS router-pull test, first commissioning, P-14 LAN part |
| **WP3** | sdkconfig lines in `sdkconfig.defaults` and compile guards (§4.9, D12); valve stale-handle check and `REATTEMPT_COUNT` handling | low-medium | TLS / DPS / C2D / snapshot regression; heap table; **G6b** |
| **WP4** | Portal intake and phone UX: C6, C7, C8, C9, **C10b** (cache-only `/ap.json`; the first-page trigger and `POST /scan.json` order a plain scan ≥ 20 s apart), C12 (Finish), C13, channel 11. The WP4 deletions of §6.4's branch list. The `reset_button.c` forget comment | **high** (C8) | **G8x** (including the `%`-sequence and encoded-password fuzz), **G-CNA in S1** (fixes the socket cap), P-7, G3 (E2) |
| *Tag* | **Development checkpoint, never shipped as is:** it still has the uncapped no-credential pause. If the release has to be cut before WP8, the fallback below (capped pause) applies | – | – |
| **WP5** | Single BLE scan executor, behaviour-equivalent | medium-high | VAL-01, **P11, P14**; 100 valve power cycles with 0 `rc=2` and relink ≤ 10 s; E4 replay unchanged |
| **WP6** | NORMAL de-lock (N_CODED / N_MIXED, 1 s dither), valve claim policy, persisted PHY bits (no decay), widened LR trigger with the 10 min overlay cap | medium | G2 NORMAL (short wettings included); G4 NORMAL 24 h. Fallback: dither D = 3 s |
| **WP7** | Lab image: `radio_policy` with the ladder (`APP_RADIO_LAB`), C11. Built on WP4, so it includes C7 and C10b | low | **G1**: fixes the SERVE rung, AP_IDLE, the join-assist end rule, TAIL and K1. G1 re-checks page p90 at the socket cap that G-CNA S1 fixed |
| **WP8** | `radio_policy` in production. The first-page trigger and `POST /scan.json` become LIST pulse requests. Delete the holds, the page chain and the `cb_scan_start` scan-stop path, and remove the health hold in the **same commit**. The `reset_button.c` window comment. The model is re-run with G0's Ta and p_loss first | high | G2 (all states), G3, G3b, G4 AP_IDLE 24 h, G5, G6, G7, **G-CNA S2 / S3**, regression |
| **WP9** | Memory set (§8), one line per build | medium | **G-M** per line; G4b if the WP9 floor is claimed |
| **WP10** | Documents: CHANGELOG; HANDOFF §15 (these decisions); `MANUAL_TEST_PLAN.md` (T4-10 rewritten, G-CNA, G-FAULT, G8x, G6b, G3b); component change register; the FW 1.1.0 timing assumptions the hub's asserts depend on | none | – |

**Fallback if G1 finds no geometry that meets the phone gates:**
- Ship WP0-WP6 plus the JOIN_ASSIST and SUBMIT pulses under the blind budget, with NORMAL profiles everywhere.
- Keep the no-credential pause only in a capped form; the uncapped form stays forbidden.
- Take a dedicated BLE radio to the next PCBA review.

---

## 12. Bench gates

Run everything first with the **valve unpowered**, then with the valve linked.

| Gate | Test | Pass |
|---|---|---|
| **G0** | Instrumentation build: E4 replay, E1 re-run, first setup, 10 s reset | Baselines recorded (§2.4); AP-start dip attributed; `csa_count` read from the driver log; AP channel in fallback recorded; model re-run |
| **G-FAULT** (new) | WP1 subset: internal heap held < 1 KB for 5 s while a phone polls and the router power-cycles; a second ESP32 beacons 10 SSIDs of 32 × 0x01; 1,000 fuzzed UDP datagrams to :53; HTTP header fuzz (long Host, missing headers). The `%`-sequence and 64-character encoded-password fuzz runs with G8x after WP4 | 0 panics, 0 corruption; 400s where due |
| **G-CNA** (new) | The §7.5 matrix | all rows |
| **G1** | Lab ladder on a build with C7 and C10b: SERVE rungs (A-thin, A, B, C, AP_IDLE density); AP_IDLE 0.6/0.3 against 0.6/0.6; join assist on and off; AP_K1M and discovery; TAIL under N_CODED; phone → 10.10.0.1 ping at 5/s; 10 joins per phone per rung; join → first DNS per phone in S2 | the §4.3 thresholds; the rung is fixed in the table before WP8 |
| **G2** | 20 random-time drips per state (NORMAL, AP_IDLE, SERVE, JOIN at the edge, SUBMIT at the edge, LR_AP, NORMAL_LR) and 10 short wettings (0.5 s and 1 s) per state | All sustained drips detected: NORMAL ≤ 5 s, AP_IDLE ≤ 20 s, SERVE ≤ 35 s, **none > 60 s**. Short wettings within ±15 points of §5.2. Valve close ≤ detection + 2 s when linked |
| **G3** | E4 replay and E2 replay (a laptop for 10 min) | 0 failed allocations; ≥ 8 KB / ≥ 4.5 KB; with 0 stations, AP down ≤ 1 s after the IP and MQTT ≤ 5 s after it. On failure: the §8 chain |
| **G3b** | 30 router power cycles, half with a phone on the AP | No downward trend > 1 KB; 0 escape admissions; admission ≤ 5 s |
| **G4 / G4b** | 24 h soaks: AP_IDLE (router off, 3 dry sensors, valve unpowered) and NORMAL. G4b adds 10 outages, 10 portal sessions and a laptop | 0 false offlines; 0 scan-start failures > 2 s; flat heap; no NimBLE assert; duty watchdog quiet; G4b floor ≥ 16 / 8 KB |
| **G5** | A phone forget/rejoin loop for 5 min while a sensor is wet; a laptop remembering the SSID, router off 2 min then on | ≤ 2 assists per 60 s; blind ≤ 2.8 s and recovery ≥ 1.2 s every time; rejoin ≤ 40 s |
| **G6** | Leak with a dead valve, phone on the portal, router off then on | Lease ≤ 33 s after the incident (≤ 3 s after LR_AP_30 ends), then ≤ 3 s per join; cloud alert delivered; LR overlay ends ≤ 10 min after the incident; valve powered → linked ≤ 5 s → RMLEAK then CLOSE |
| **G6b** | Induce 0x3E (power-cycle the valve at CONNECT) ×20 with the scan running | relink ≤ 10 s; 0 "Already connected"; a pended CLOSE is written |
| **G7** | List completeness; mesh router; router channel change; hidden SSID; 10 s router blip ×10 | ≥ 80 % of APs above −80 dBm listed; rejoin ≤ 75 s after a channel change; blip rejoin ≥ 95 % per attempt |
| **G8** | Rejoin with a phone on the AP, pull the router within 20 s, keep the page open 5 min, Submit ×10 | **0 reboots**; no attempts closer than 30 s apart |
| **G8x** (new) | G8 ×10; Submit during an app retry ×10; wrong password during an outage, then the router returns; switch and rollback in the tail; forget and 10 s reset with the STA idle, connecting and connected (NVS checked after a reboot); UTF-8 SSID; password with spaces and `%`; open network; `%`-sequence and 64-character encoded-password header fuzz | the old router is rejoined ≤ 40 s after a wrong-password failure; NVS changes only on success; 0 reboots |
| **G-M** (new) | Per WP9 line | heap delta recorded; VAL-01 and G6b for the NimBLE lines; adverts per burst ±5 %; G-CNA page p90 ≤ +20 %; TLS / DPS regression |
| **Regression** | T4-03, rewritten T4-10, VAL-01, EC-1, **P11, P14**, 10 s reset ×10, first commissioning with DPS (router pulled mid-DPS), production-tool boot-log parse | stable heap at each portal boot; IRAM and `.bss` gates |

---

## 13. Decisions for you

Approving this proposal also supersedes HANDOFF §14c decisions 1-2 (holds and the page chain), per council D3. Every pulse (a Coded stop longer than the profile's own gap) becomes ≤ 2.8 s, followed by ≥ 1.2 s of recovery.

| # | Question | Recommendation | Consequence |
|---|---|---|---|
| **D1** | Are any sensors in the field in 1M legacy mode (LR_BUT high)? | **Please answer.** The design handles both through PHY learning | With 1M sensors at a site, AP modes get 33 % Wi-Fi, and NORMAL's dry figure at p_loss 0.5 is 3.6e-2 per 24 h (max .23). A 1M sensor not yet heard since provisioning is covered in AP modes only by discovery slots. If any exist, set them to Coded at the next service visit |
| **D2** | The no-credential portal while sensors are provisioned (after the 10 s reset): (a) today's uncapped pause; (b) a capped pause; (c) keep scanning under station-keyed modes | **(c)** | Leak detection continues during setup (today 0 %). First setup with nothing provisioned is unchanged. The reset UX rests on G1 |
| **D3** | httpd off the home LAN (only while the AP is up, only on the AP interface) | **Yes**, if nothing on the LAN uses it (the production tool reads UART) | About +5 KB free; closes the unauthenticated LAN wipe |
| **D4** | Cloud only after the AP stops | **Yes** | MQTT comes up about 1-3 s after STOP_AP: IP + 1.5-3.5 s with no station; Finish + 3-5 s; IP + 60-63 s if the page is left open (IP + 75 s at the backstop). Local shutoff is unaffected; alerts queue in the ≤ 4 KB outbox |
| **D5** | Dead-valve leak response: unbounded (today), or bounded, with the trigger widened to "valve provisioned and unlinked, and RMLEAK / CLOSE pended or an incident latched with the interlock not confirmed" | **Bounded plus widened** | LR_AP runs 30 s at 67 % 1M; the overlay ends 10 min after the incident. Shutoff waits for the valve (delayed, not missed). A pended RMLEAK / CLOSE is written at the next claim. The portal and the cloud alert keep working |
| **D6** | NimBLE connect re-attempt | **Off**, plus the stale-handle check | Every failed link reaches the app; relink ≤ 10 s; P11 / P14 / VAL-01 re-run |
| **D7** | Channel 11 for the no-credential AP | **11** | Zero cost; clear of BLE 37/38. It is [platform] and unverified that the fallback AP follows the router's channel; G0 records it |
| **D8** | Short wettings while a phone is served: (a) the rung rule, provisionally SERVE-A; (b) the council's SERVE-B, UX first; (c) no SERVE, AP_IDLE density throughout | **(a)** | A 1 s wetting while served is detected .78 / .60 with A, against .56 / .42 with B. Ceiling at 100 % scan: .97 / .88. Option (c) likely fails the phone gates [arith]; G1 shows it |
| **D9** | Remove `DELETE /connect.json` and the page's Disconnect button | **Yes** | Closes an unauthenticated wipe from the open AP. The 10 s reset and a new Submit cover every legitimate flow |
| **D10** | Does the Watts Home app, the production tool or any script call `/connect.json`, `/ap.json`, `/status.json` or DELETE, or fetch the page assets? | **Please answer** | Raw `X-Custom-*` headers keep working. Clients without Accept-Encoding get gzip (RFC 9110); an explicit identity-only request gets 406. `/ap.json` no longer starts a scan (C10b) |
| **D11** | Vendor the component (changes `main/idf_component.yml` and `dependencies.lock`) | **Yes, as the first commit of this work on `fix/2.1.4`** (pure move) | No silent revert; `fullclean` works. If no: patch in place plus the C0 guard |
| **D12** | Record the sdkconfig changes. `sdkconfig` is untracked, and `sdkconfig.defaults` does not override an existing `sdkconfig` | **Yes:** add the seven lines, each with a one-line comment, to the tracked `sdkconfig.defaults`. Use `idf.py save-defconfig` only to diff, because it would rewrite the file with every non-default value and drop the existing comments. Add the compile guards (I14) | A fresh clone reproduces the build; a stale local `sdkconfig` fails to compile instead of shipping without the lines |
| **D13** | A Finish button stops the AP about 2 s after the tap (never before IP + 5 s) | **Yes** | The iOS CNA and the Android sign-in close by themselves; the phone returns to home Wi-Fi |
| **D14** | Memory set (WP9) in 2.1.4, one line at a time | **Yes, as the last package** | An estimated +40-50 KB free if every line ships; the two IRAM lines alone free more than 27 KB. IRAM_OPT=n is gated by page times. The NimBLE buffer cuts are gated by VAL-01 and G6b, with adverts per burst as a secondary check. The IRAM gate is re-baselined |

---

## 14. Not in this release

| Item | Status | Reason |
|---|---|---|
| Portal-on-demand button; WPA2 password on the setup AP | **Excluded by the user** | – |
| Sensor firmware changes (wet latch, longer edge burst, tighter advert spacing, 8 s cadence) | Out of this release (answer 1) | The hub documents the FW 1.1.0 timings it depends on |
| `mem_gov` admission token | Not carried | The admission gate plus I4 cover the co-residency it targeted |
| Twin-tunable profile table | Not carried | It would make safety bounds remotely tunable; the fixed table plus asserts is the safer design |
| Gated manual MQTT reconnect | Not carried | esp-mqtt reconnects happen only while admission holds, which is only with the AP down |
| Scoped idle-station deauth | Contingency only (§8 chain, step 3) | Needed only if G3 fails after the bounds |
| 16 KB DCache change | Not proposed | Only if the WP9 floor still fails |
| DHCP option 114; answering "Success" or 204 after setup; a DNS allow-list; a hostname instead of the IP redirect | Rejected | No benefit for iOS or Android; or it would route phone traffic into a network with no internet; or it would miss OEM probes |
| Dedicated BLE radio; BLE provisioning through a companion app | Next PCBA and later | Only a second radio gives both sides 100 % |

---

## Appendix: model and reference files

**Council leak model, unchanged:**
- `…\scratchpad\council\leakmath\leak_detect_model.py`
- `…\lead\lead_model.py`
- `…\redteam\rt_short_episode.py`, `…\redteam\rt_connect_rate2.py`
- `…\final\final_model.py`, `…\final\lr_model.py`

**Runs used here:**
- `…\final\final_base.txt`, `final_blind.txt`, `short_ep.txt`, `lr_model.txt` (council).
- New for this proposal, in `…\scratchpad\council\constrained\leak\`:
  - `serve_ladder.py` → `short_ladder.txt` (the ceiling and the SERVE rungs against short wettings) and `wet_ladder.txt` (SERVE-A persistent-leak latency);
  - `serve_a_pulses.py` → `serve_a_pulses.txt` (SERVE-A with LIST and with SUBMIT on the edge).
- From the proposal review, in `…\scratchpad\council\constrained\review\` (printed output, re-run for this document):
  - `i2b_limit.py`: SERVE-A with a Rescan every 20-25 s, at the I2b ceiling, and with discovery (§5.1);
  - `lr_dry.py`: NORMAL_LR held 24 h, dry false-offline rates (§5.4).

**DNS byte models:** `…\scratchpad\council\constrained\dns_emul.py` (today's `dns_server.c`) and `dns_proposed.py` (§6.2a).

**gzip measurements:** `…\scratchpad\council\constrained\gz\`.

The full scratchpad path is `C:\Users\antun\AppData\Local\Temp\claude\c--Work-Projects-EfloStop-2-Firmware-Production-eFloStop-WiFiHub-idf1\ee520eed-0dc0-45ef-bdfe-91bf0b44762d\scratchpad\council\`.