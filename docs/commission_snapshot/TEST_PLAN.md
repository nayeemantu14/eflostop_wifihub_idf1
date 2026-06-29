# TEST PLAN — Fast First Snapshot After Commissioning (FW 1.4.4)

Validate that a `provision` (commission) C2D command makes the hub publish a snapshot **within 120 s** —
ideally within seconds when the devices are live — instead of waiting for the 5-min periodic timer, and
that the snapshot carries **complete sensor data** (no null `battery`/`rssi`/`fw_version`).

> Hub under test: `feature/fast-commission-snapshot`, `gateway.fw = 1.4.4`. Includes the window-anchoring
> fix (`777fc65`) and the snapshot-ordering fix (`0aab312`).

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
> `Boot sync: timeout (2 min)` → `IOTHUB: Publishing sync snapshot …` → `TELEMETRY_V2: Pub snapshot …`.

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

## C1 — Latency (headline): snapshot ≤120 s of `provision`
1. Do the **clean-state reset**.
2. Ensure leak sensor A (`3F:59`) is powered and within range.
3. **Start the stopwatch** and send **Provision — 1 live sensor**.
4. Watch serial for the commission flow markers (above), ending in `Publishing sync snapshot` →
   `Pub snapshot`.
5. **P:** a `type:"snapshot"` arrives on the cloud monitor **≤120 s** after the command. Record the
   measured time. *(Reference run: ~63 s.)*

---

## C2 — Early-send when the device is live (don't wait the full window)
1. Do the **clean-state reset**.
2. **Power-cycle sensor A right before provisioning** (pull the battery, reinsert) so it fires its
   power-on boot burst — it will be heard within seconds.
3. Send **Provision — 1 live sensor**.
4. **P:** serial shows `HEALTH_ENGINE: Boot sync: all devices seen` (**not** `timeout`), and the
   snapshot arrives **well under 120 s** — i.e. it does not wait the full window when everything is
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

## C4 — Degraded (a provisioned sensor never advertises)
1. Do the **clean-state reset**.
2. **Make sensor B (`3B:00`) silent** — remove its battery (or take it out of range). Keep sensor A
   (`3F:59`) live.
3. Send **Provision — 1 live + 1 offline sensor**.
4. Let the window run to the deadline.
5. **P (deadline send):** serial shows `HEALTH_ENGINE: Boot sync: timeout (2 min)` ~120 s after the
   provision, then `Publishing sync snapshot`. No hang, no crash, no reboot.
6. **P (mixed payload):** in the snapshot —
   - `3F:59` → `connected:true` with full `battery`/`rssi`/`fw_version`.
   - `3B:00` → `connected:false`, `last_seen_age_s:null`, `battery/rssi/fw_version:null`.
   - `system_health.rating` = `critical` (or degraded) citing the offline sensor.

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

## Result log
| Test | Expected | Observed | Pass? |
|---|---|---|---|
| C0 version 1.4.4 | banner 1.4.4 | `App version: 1.4.4`, `gateway.fw:1.4.4` | ✅ |
| C1 latency ≤120 s | snapshot within 120 s of `provision` | provision uptime 31 s → snapshot uptime 94 s (~63 s) | ✅ |
| C2 early-send | well under 120 s when live | serial `Boot sync: all devices seen` (not timeout) | ✅ |
| C3 completeness + full fields | all N + valve, non-null sensor fields | valve + `3F:59` (batt 60, rssi -33, fw 1.1.0) | ✅ |
| C4 degraded | sends at 120 s timeout; live sensor full, missing sensor null | criteria PASS, but exposed 4 defects (valve-offline-on-reprovision, double-send, ~149 s overshoot, decommission asymmetry) — all fixed; **re-test pending** | 🟡 |
| C5 re-entrancy | ≤1 extra snapshot, no storm, no crash | | |
| C6 regression | 5-min cadence intact, no double-send | | |
| Change 2 tags | tags present service-side | blocked on Brand/Type + DPS edit | ⏳ |

> After C4-C6 pass: `git merge --no-ff feature/fast-commission-snapshot` → master (1.4.4);
> tag `v1.4.4` if requested. **Do not push unless asked.**
