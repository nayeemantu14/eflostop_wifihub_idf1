# TEST PLAN — Fast First Snapshot After Commissioning (FW 1.4.4)

Validate that a `provision` (commission) C2D command makes the hub publish a snapshot **within 150 s** —
ideally within seconds when the devices are live, and as soon as a late device is heard via the
incremental refresh — instead of waiting for the 5-min periodic timer, and that the snapshot carries
**complete sensor data** (no null `battery`/`rssi`/`fw_version`).

> Hub under test: `feature/fast-commission-snapshot`, `gateway.fw = 1.4.4`. Includes: fast snapshot
> (`aae5c8b`), window anchored to provision (`777fc65`), snapshot-after-cache ordering (`0aab312`),
> valve-offline-on-reprovision + double-send + timeout-granularity + decommission re-arm (`d49d604`),
> incremental commission refresh + commission window + cache/health consistency + whitelist-log cleanup
> (`50fb729`), commission window tuned to **150 s** (`8112f0c`).
>
> **Commission window is now 150 s** (`HEALTH_COMMISSION_SYNC_TIMEOUT_MS`); the boot window stays 120 s.
> A device heard after the window still self-reports via an **incremental refresh snapshot** within ~2 s.

---

## 0. Prerequisites & tooling

| Item | Value |
|---|---|
| Hub branch / version | `feature/fast-commission-snapshot`, **1.4.4** |
| Valve MAC | `00:80:E1:27:F7:BB` |
| Leak sensor A (keep LIVE) | `00:80:e1:2a:3f:59` |
| Leak sensor B (use for OFFLINE case) | `00:80:e1:2a:3b:00` |
| Serial port | `COM4` (adjust if different) |

**Tools to have running before each test:**
1. **Serial monitor** — watch `IOTHUB`, `HEALTH_ENGINE`, `TELEMETRY_V2`, `BLE_LEAK`:
   ```
   idf.py -p COM4 monitor
   ```
2. **Cloud monitor** — VS Code *Azure IoT Hub → Start Monitoring Built-in Event Endpoint* on
   `GW-34B7DA6AAD54`, or:
   ```
   az iot hub monitor-events -n wd-core-iothub-poc -d GW-34B7DA6AAD54 --timeout 0
   ```
3. **A stopwatch** (phone) for the latency tests.

**C2D command envelopes** (send via the app, the Azure IoT Toolkit "Send C2D Message", or
`az iot device c2d-message send -n wd-core-iothub-poc -d GW-34B7DA6AAD54 --data '<json>'`):

- **Decommission-all** (reset to a clean state between tests):
  ```json
  {"schema":"eflostop.cmd","ver":1,"id":"decom-all","cmd":"decommission","payload":{"target":"all"}}
  ```
- **Provision — 1 live sensor** (C1, C2, C3, C5, C6):
  ```json
  {"schema":"eflostop.cmd","ver":1,"id":"prov-1","cmd":"provision","payload":{"valve_mac":"00:80:E1:27:F7:BB","ble_leak_sensors":["00:80:e1:2a:3f:59"]}}
  ```
- **Provision — 1 live + 1 offline sensor** (C4):
  ```json
  {"schema":"eflostop.cmd","ver":1,"id":"prov-c4","cmd":"provision","payload":{"valve_mac":"00:80:E1:27:F7:BB","ble_leak_sensors":["00:80:e1:2a:3b:00","00:80:e1:2a:3f:59"]}}
  ```

> **Serial markers to recognise** (the commission flow):
> `C2D cmd='provision'` → `PROVISIONING: Provisioning completed` → `HEALTH_ENGINE: Device table loaded`
> → `IOTHUB: Commission: fast snapshot armed …` → (advert) `BLE_LEAK: eleak …` →
> `IOTHUB: Event: BLE Leak …` → `HEALTH_ENGINE: Boot sync: all devices seen` **or**
> `Boot sync: timeout (150 s)` → `IOTHUB: Publishing sync snapshot …` → `TELEMETRY_V2: Pub snapshot …`.
> A device heard *after* the window logs `IOTHUB: Publishing commission refresh snapshot (device heard, N/N seen)`.
> `BLE_LEAK: Whitelist reloaded` should now appear **only** on a provision/decommission, not every 10 s.

**"Clean-state" reset procedure (do this before each test below):**
1. Send the **decommission-all** command.
2. Wait for `cmd_ack decommission status:ok` (cloud) and the hub to auto-restart (serial shows a fresh
   boot banner → `Hub is UNPROVISIONED`). DPS will re-register (`DPS: Assigned hub=…`).
3. Confirm serial: `IOTHUB: Connected to Azure IoT Hub!` and `Hub is UNPROVISIONED - waiting for
   provisioning JSON`.

---

## C0 — Build & version sanity
1. `idf.py build` — confirm it reports version **1.4.4**.
2. `idf.py -p COM4 flash monitor`.
3. **P:** boot banner shows `App version: 1.4.4`; first lifecycle/snapshot shows `"fw":"1.4.4"`;
   `NVS_STORE: commissioning NVS partition 'nvs_prov' ready`.

---

## C1 — Latency (headline): snapshot ≤150 s of `provision`
1. Do the **clean-state reset**.
2. Ensure leak sensor A (`3F:59`) is powered and within range.
3. **Start the stopwatch** and send **Provision — 1 live sensor**.
4. Watch serial for the commission flow markers (above), ending in `Publishing sync snapshot` →
   `Pub snapshot`.
5. **P:** a `type:"snapshot"` arrives on the cloud monitor **≤150 s** after the command (usually much
   sooner — early-send when the sensor is heard). Record the measured time. *(Reference run: ~63 s.)*

---

## C2 — Early-send when the device is live (don't wait the full window)
1. Do the **clean-state reset**.
2. **Power-cycle sensor A right before provisioning** (pull the battery, reinsert) so it fires its
   power-on boot burst — it will be heard within seconds.
3. Send **Provision — 1 live sensor**.
4. **P:** serial shows `HEALTH_ENGINE: Boot sync: all devices seen` (**not** `timeout`), and the
   snapshot arrives **well under 150 s** — i.e. it does not wait the full window when everything is
   already heard.

---

## C3 — Completeness + full fields (the fixed bug)
1. Do the **clean-state reset**.
2. Send **Provision — 1 live sensor** (sensor A live).
3. Inspect the commission snapshot in the cloud monitor.
4. **P (completeness):** `data.valve` present + `data.ble_leak_sensors` contains `3F:59`.
5. **P (no null race):** the `3F:59` entry has **non-null** `battery`, `rssi`, and `fw_version`
   (e.g. `battery:60, rssi:-33, fw_version:"1.1.0"`), with `connected:true`, `last_seen_age_s:0`.
6. **P (serial ordering):** UART shows `IOTHUB: Event: BLE Leak …` (cache update) **before**
   `IOTHUB: Publishing sync snapshot …` in the same burst.

---

## C4 — Degraded (a provisioned sensor never advertises) — also validates #1/#2/#3
1. Do the **clean-state reset**.
2. **Make sensor B (`3B:00`) silent** — remove its battery (or take it out of range). Keep sensor A
   (`3F:59`) live.
3. Send **Provision — 1 live + 1 offline sensor**.
4. Let the window run to the deadline.
5. **P (deadline send, #3):** serial shows `HEALTH_ENGINE: Boot sync: timeout (150 s)` at **~150 s**
   after the provision (not ~120 s and not overshooting toward ~180 s), then `Publishing sync snapshot`.
   No hang, no crash, no reboot.
6. **P (valve NOT offline, #1):** in the snapshot the **valve** is `connected:true`,
   `rating:"excellent"`, `last_seen_age_s` a real number — **not** "Valve offline"; `system_health.reason`
   cites only the offline **sensor**, not the valve.
7. **P (single snapshot, #2):** exactly **one** snapshot at the deadline — no second byte-identical
   snapshot back-to-back.
8. **P (mixed payload):** in the snapshot —
   - `3F:59` → `connected:true` with full `battery`/`rssi`/`fw_version`.
   - `3B:00` → `connected:false`, `last_seen_age_s:null`, `battery/rssi/fw_version:null`.
   - `system_health.rating` = `critical` citing the 1 offline sensor.

---

## C5 — Re-entrancy (double `provision`, QoS-1 redelivery)
1. Do the **clean-state reset**.
2. Send **Provision — 1 live sensor** with `id:"prov-c5a"`, then **again within ~3 s** with
   `id:"prov-c5b"` (same payload).
3. **P:** no crash / no reboot; serial may show `Commission: fast snapshot armed` for each, but the
   snapshots do **not** pile up — **at most one extra** snapshot total (a re-arm before the first
   snapshot fires → a single snapshot; a re-arm after → at most one additional). No snapshot storm
   (not 3+ in seconds).

---

## C6 — Regression (5-min periodic intact, no double-send)
1. Do the **clean-state reset**, then send **Provision — 1 live sensor** and let the commission
   snapshot publish (per C1).
2. Leave the hub **idle ~6 minutes**, untouched.
3. **P (periodic intact):** the normal **5-min periodic** snapshot fires (`TELEMETRY_V2: Pub snapshot`
   ~300 s after `Snapshot timer started`), proving the commission path didn't disturb the periodic
   timer.
4. **P (no double-send):** there is **no** duplicate commission snapshot between the commission
   snapshot and the first periodic one (no two near-identical-`ts` snapshots back-to-back).

---

## C7 — Incremental refresh: late sensor self-reports (the headline fix, A) — *most important*
Deterministically forces a sensor to be heard **after** the window closes, then checks it still reports.
1. Do the **clean-state reset**.
2. **Power sensor A (`3F:59`) OFF** (battery out) so it cannot be heard during the window.
3. Send **Provision — 1 live sensor** (the now-off `3F:59`). Start the stopwatch.
4. Wait for the window to time out: serial `Boot sync: timeout (150 s)` → `Publishing sync snapshot` at
   ~150 s. **First snapshot shows `3F:59` `connected:false` / null** (expected — it was off).
5. **Now power sensor A ON** (reinsert battery) → it fires its power-on boot burst.
6. **P (A — refresh):** within ~2 s of the hub hearing it (serial `BLE_LEAK: eleak …3F:59`), serial logs
   `IOTHUB: Publishing commission refresh snapshot (device heard, 2/2 seen)` and a **new snapshot** arrives
   on the cloud with `3F:59` now `connected:true` + full `battery`/`rssi`/`fw_version`. **This is the
   "sensor data never arrives" fix** — the data now arrives on its own, not only at the 5-min periodic.
7. **P (self-limit):** once all devices are seen, no further refresh snapshots stream (the grace closes).

## C8 — Cache/health consistency on re-provision (D)
1. Do the **clean-state reset**, send **Provision — 1 live sensor** (`3F:59` live), let it go
   `connected:true` with data (per C1/C3).
2. **Re-provision** with a *different* list that adds the offline `3B:00`: send **Provision — 1 live +
   1 offline sensor**. This reloads the device table (wipes seen-state).
3. Inspect the **next** snapshot **before** `3F:59` is re-heard (the moment right after re-provision).
4. **P:** `3F:59` reported `connected:false` must have **null** `battery`/`rssi`/`fw_version` — **not**
   stale values (e.g. not `connected:false` with `battery:61`). Once `3F:59` is re-heard, a refresh
   snapshot (C7) shows it `connected:true` with data again.

## C9 — Decommission reflects removal + valve stays online (#4, #1, E)
1. Do the **clean-state reset**, send **Provision — 1 live + 1 offline sensor** (so 2 sensors provisioned).
2. **Decommission one BLE sensor**:
   ```json
   {"schema":"eflostop.cmd","ver":1,"id":"decom-ble","cmd":"decommission","payload":{"target":"ble","sensor_id":"00:80:e1:2a:3b:00"}}
   ```
3. **P (#4 — reflects removal):** within the window a snapshot publishes showing only the remaining
   sensor (`3B:00` gone) — not waiting for the 5-min periodic.
4. **P (#1 — valve stays online):** the valve remains `connected:true` / `rating:"excellent"` across the
   decommission (it must not flip to "offline").
5. **P (E — log):** `BLE_LEAK: Whitelist reloaded: 1 sensor(s)` is logged **once** on the change — and
   across the whole session it does **not** spam every 10 s.

---

## Change 2 — Twin tags (Azure-side, NO firmware) — *blocked, do when values arrive*
> Needs `DeviceBrand` / `DeviceType` strings from the app team (`DeviceModel = "eFloStop"`).
1. Set `initialTwin.tags = {"DeviceBrand":"<TBD>","DeviceModel":"eFloStop","DeviceType":"<TBD>"}` on the
   DPS **enrollment group** (Azure portal / service SDK). For already-provisioned hubs, back-fill:
   ```
   az iot hub device-twin update -n wd-core-iothub-poc -d GW-34B7DA6AAD54 \
     --tags '{"DeviceBrand":"<TBD>","DeviceModel":"eFloStop","DeviceType":"<TBD>"}'
   ```
2. **P:** `az iot hub device-twin show -n wd-core-iothub-poc -d GW-34B7DA6AAD54 --query tags` shows the
   three tags; the device's **reported properties are unchanged** and it operates identically (tags are
   service-side only, not device-visible).

---

## Result log (bench run on 150 s build; verified vs UART+monitor by workflow `wf_f5fa217f`)
| Test | Validates | Observed | Pass? |
|---|---|---|---|
| C0 version/build | build compiles, 1.4.4 | runs; `App version: 1.4.4`, `fw:1.4.4` everywhere; uptime 0→670 s | ✅ |
| C1 latency ≤150 s | fast snapshot | commission snapshots within window (re-prov heard at 138 s, published 150 s) | ✅ |
| C2 early-send | early-send | all-seen early-send proven (decom→0 sensors → snapshot @uptime418, valve-only) | ✅ |
| C3 completeness + full fields | `0aab312` | heard sensors carry full battery/rssi/fw (boot @123 s, final @642 s), no nulls | ✅ |
| C4 degraded | `#1`+`#2`+`#3` | boot timeout **120 s** @122.8 s; **valve online/excellent**; single snapshot; `3b:00` null | ✅ |
| C5 re-entrancy | re-entrancy | not stress-tested (decom×2 same id ~180 s apart handled fine; rapid double-provision not run) | 🟡 |
| C6 regression | `#2` / periodic | 5-min periodic fired @306 s & @606 s; no back-to-back duplicate snapshots | ✅ |
| **C7 incremental refresh** | **A** | **not exercised** (sensor heard at 138 s, inside window). **Code-reviewed `wouldFire=true` (high conf)** — needs the deliberate power-off→timeout→power-on test | 🟡 |
| C8 cache consistency | D | re-prov snapshot @606 s: `3f:59` `connected:false` with **null** battery/rssi/fw (no stale 59) | ✅ |
| C9 decommission | `#4`+`#1`+E | removals reflected (@386 s, @418 s); valve stayed online; `Whitelist reloaded` only on the 3 real changes | ✅ |
| B (separate window) | 150 s commission | commission timeouts **150 s** @386.9 s & @643.1 s; boot stays 120 s — distinct values | ✅ |
| Stability | no crash/leak | single POWERON boot, no panic/abort; heap stable ~41 KB, min_ever ~30.6 KB (no leak) | ✅ |
| Change 2 tags | Azure-side | blocked on Brand/Type + DPS edit | ⏳ |

> **9/11 validated; C5 + C7 partial.** C7 (incremental refresh A) is **code-confirmed** correct but not yet
> hardware-exercised — run the deliberate test (power sensor OFF → let the 150 s window time out → power it
> ON → expect `Publishing commission refresh snapshot`). C5: run a rapid double-`provision`.
>
> **New finding (separate from this feature, MAJOR latent risk):** a malformed/truncated C2D *envelope* is
> force-classified as `provision` (ver=0) by the brace-leading catch-all in `c2d_commands.c`, and sends **no
> cmd_ack** → cloud sees a silent failure (here the truncated decommission at UART L443 was safely rejected
> by the JSON re-parse, but a brace-blob with provisioning-like keys could trigger an unintended re-provision).
> Fix: only fall back to `provision` when the body parses as JSON *without* a `cmd` field, and nack unparseable
> envelopes. Decide whether to fix in this branch or a separate ticket.
>
> After C5/C7 pass: `git merge --no-ff feature/fast-commission-snapshot` → master (1.4.4); tag `v1.4.4` if
> requested. **Do not push unless asked.**
