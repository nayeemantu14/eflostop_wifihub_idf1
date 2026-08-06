# FW 1.8.0 — Bench Validation Plan
### SAS token lifecycle + Azure DPS migration + post-review hardening

**Build under test:** `PROJECT_VER = 1.8.0` · **Hub:** `resi-apex-iot-dev.azure-devices.net` ·
**DPS ID scope:** `0ne0124BF06` · **Provisioning epoch:** 2

Two capture streams per case: **UART** (idf_monitor) and **cloud** (`az iot hub monitor-events`).
Paste both back for validation.

Cloud monitor (Cloud Shell / PowerShell, one line):
```
az iot hub monitor-events --hub-name resi-apex-iot-dev --resource-group resi-apex-rg-dev --device-id GW-50787D0D96DC --properties all --timeout 0
```

> ⚠️ **Cases T2, T3, T8, T9 require a full POWER-CYCLE, not the reset button.** The ESP32-S3 RTC keeps
> running across a software reset, so a soft reset leaves the clock already valid and silently skips the
> exact path under test.

---

## Gate 0 — build identity (must pass before anything else)

| Assert | Where |
|---|---|
| `App version: 1.8.0` | UART (ESP-IDF banner) |
| `Firmware version: v1.8.0` | UART (hub_identity banner) |
| `"fw":"1.8.0"` in every envelope | cloud |
| twin reported `fw_version` = `1.8.0` | `az iot hub device-twin show` |

If any disagree, stop — `PROJECT_VER` is the single source and something else is wrong.

---

## T1 — Normal boot (baseline / regression)

Clock available, DPS cache already at epoch 2 (i.e. second boot after the migration).

**UART, in order:**
```
DPS: Loaded cached assignment from NVS
DPS: hub=resi-apex-iot-dev.azure-devices.net device=GW-50787D0D96DC
TELEMETRY_V2: Client attached: device=GW-50787D0D96DC
IOTHUB: SAS: token renewed (valid 24 h, expires ts=…)
IOTHUB: Connected to Azure IoT Hub!
```
**Must NOT appear:** `Clock not synced`, `DPS failed`, `DPS unavailable`, any `Pub … failed`.

**Cloud:** lifecycle `online`, then a boot/fast snapshot.

---

## T2 — NTP blocked at boot ⚠️ power-cycle

Block outbound **UDP/123** at the router, then power-cycle.

**UART:**
```
IOTHUB: SNTP initial sync failed — starting 60s retry timer
DPS: Clock not synced — deferring DPS registration        ← only if the cache is NOT epoch-2
IOTHUB: Clock not synced (ts=…) — holding MQTT until SNTP lands
```
**Must NOT appear:** `Connected to Azure IoT Hub!`, and **no repeated TLS handshake churn**.

**Cloud:** nothing (correct).

*This is the original Failure A. On 1.7.0 the same test produces an endless 401 reconnect loop.*

---

## T3 — Recovery when the clock arrives ⚠️ continues from T2

Unblock UDP/123 while the hub is still running. Do not reboot.

**UART, within ~90 s** (≤60 s SNTP retry + ≤30 s loop):
```
IOTHUB: SNTP now synced (ts=…) — stopping retry timer
IOTHUB: SAS: clock valid (ts=…) — minting first token, starting MQTT
IOTHUB: SAS: token renewed (valid 24 h, expires ts=…)
IOTHUB: Connected to Azure IoT Hub!
```
**Cloud:** device appears; buffered events drain (`Draining N offline event(s) before lifecycle...`).

**This is the single most important assertion in the plan** — it is the behaviour that previously
required a power-cycle.

---

## T4 — Token renewal (Failure B) — needs a temporary constant change

18 h is not benchable. In [`app_iothub.h`](../../main/iothub/app_iothub.h):
```c
#define SAS_TTL_SEC           600   // TEST ONLY — restore to (24 * 3600)
#define SAS_RENEW_MARGIN_SEC  300   // TEST ONLY — restore to (6 * 3600)
```
Renewal then fires ~5 min after connect, repeatedly. **Run ≥4 cycles.**

**UART per cycle:**
```
IOTHUB: SAS: within 0 h of expiry — renewing      ← "0 h" is integer division under test values
IOTHUB: Disconnected.                              ← may be absent; stop() dispatches no event
IOTHUB: SAS: token renewed (valid 0 h, expires ts=…)
IOTHUB: Connected to Azure IoT Hub!
```
**Assert:** stable across all 4 cycles, no heap decline, **no `Pub … failed`**, no reboot.
**Cloud:** telemetry resumes each cycle with only a few seconds' gap; no 401 in IoT Hub diagnostics.

**→ Restore both constants and rebuild before continuing.**

---

## T5 — WiFi reset / captive portal (suspend/resume interlock)

10 s button hold → AP mode → rejoin.

**UART:**
```
IOTHUB: WiFi down — stopping MQTT client (free TLS heap for AP/captive portal)
… captive portal …
IOTHUB: WiFi up — restarting MQTT client            ← token still fresh
   OR
IOTHUB: WiFi up — MQTT held pending SAS token refresh
IOTHUB: SAS: … renewing → token renewed → Connected  ← within 30 s
```
Either branch is correct. **Failure = MQTT never comes back.**

---

## T6 — Safety path while MQTT is held ⚠️ the one that matters most

During the **T2** hold window (MQTT never connected), **wet a leak sensor**.

**Assert:**
- Valve **auto-closes** (`RULES_ENGINE` output, valve physically closes)
- `TELEMETRY_V2: Offline — buffering … event` — the event is **buffered, not dropped**
- After T3 reconnect: `Offline drain complete: N event(s) replayed`, and the leak event appears in cloud

*This proves `sas_maintain()`'s placement after Phase 2 doesn't delay leak response, and that the
NULL-client telemetry path buffers rather than discards.*

---

## T7 — Heap

Compare against the 1.7.x baseline (`free≈37368 / largest_blk≈23552`).

**Assert:** flat over ≥10 min. No `Heap dropped` warnings beyond the normal BLE/TLS bring-up spike.

---

## T8 — DPS unreachable at boot ⚠️ power-cycle — **new, from review finding 1**

Simulate by blocking outbound **8883/443**, or temporarily setting `AZURE_DPS_ID_SCOPE` to a bad value.
Power-cycle.

**UART:**
```
IOTHUB: DPS failed (attempt 1/6), retry in 5s
…
IOTHUB: DPS failed (attempt 5/6), retry in 30s
IOTHUB: DPS unavailable after 6 attempts — entering event loop without cloud. Leak detection and valve auto-close run normally; DPS retried every 5 min.
IOTHUB: QueueSet Initialized. Event loop starting...
```

**Then, with DPS still unreachable — wet a leak sensor:**
- **Valve MUST auto-close.**
- Event must be buffered.

**This is the whole point of the fix.** On the pre-fix build `iothub_task` never reaches its event
loop here, so `rules_engine_evaluate_leak()` never runs and the valve never closes.

---

## T9 — DPS recovery ⚠️ continues from T8

Unblock (or restore the ID scope + reflash). Do not reboot if only the network was blocked.

**UART, within ~5 min:**
```
IOTHUB: DPS: retrying registration...
DPS: No cached assignment, performing DPS registration...
DPS: Assigned hub=resi-apex-iot-dev.azure-devices.net device=GW-50787D0D96DC
DPS: Cached assignment in NVS (provisioning epoch 2)
IOTHUB: DPS: registration recovered — cloud path up
TELEMETRY_V2: Client attached: device=GW-50787D0D96DC
IOTHUB: Connected to Azure IoT Hub!
```
**Cloud:** buffered T8 leak event drains and appears.

---

## T10 — Migration from epoch 1 (only reproducible once per hub)

If you still have a hub carrying a **pre-1.8.0** DPS cache, capture its first boot:
```
DPS: Cached assignment is from provisioning epoch 0, firmware expects 2 — discarding and re-registering
DPS: No cached assignment, performing DPS registration...
DPS: Cached assignment in NVS (provisioning epoch 2)
```
**Assert:** commissioning survives — valve MAC, sensor list, rules and hub name all intact in the next
snapshot. (`epoch 0` is correct for a legacy cache: the key didn't exist.)

---

## Results

| Case | Pass/Fail | Notes |
|---|---|---|
| Gate 0 — version | | |
| T1 — normal boot | | |
| T2 — NTP blocked | | |
| T3 — clock recovery | | |
| T4 — renewal ×4 | | |
| T5 — WiFi reset | | |
| T6 — leak while held | | |
| T7 — heap | | |
| T8 — DPS unreachable + leak | | |
| T9 — DPS recovery | | |
| T10 — epoch migration | | |

**Must pass before commit:** Gate 0, T1, T2, T3, T6, T8.
T4/T5/T7/T9 are strongly recommended. T10 is opportunistic.

**Not yet verified at all:** the build compiles — this was written without an ESP-IDF environment.
A clean `idf.py build` is the real Gate 0.
