# TEST PLAN — Fast First Snapshot After Commissioning (FW 1.4.4)

Validate that a `provision` (commission) C2D command makes the hub publish a snapshot **within 120 s** —
ideally within seconds when the devices are live — instead of waiting for the 5-min periodic timer.

> Hub under test: `feature/fast-commission-snapshot` (combined firmware, `gateway.fw = 1.4.4`). The board can
> stay on the existing `nvs_prov` layout from the 1.4.3 test, so a **plain flash (no erase) preserves
> commissioning** — but the test re-sends `provision` anyway (that's the trigger).

## Tooling
- Serial: `idf.py -p <PORT> monitor` — watch `IOTHUB`, `HEALTH_ENGINE`, `TELEMETRY_V2`.
- Cloud: `az iot hub monitor-events -n <hub> -d <gw> --timeout 0` (leave running) + a stopwatch.
- Send the `provision` command (app, or `az iot device c2d-message send`) with the envelope:
  ```json
  {"schema":"eflostop.cmd","ver":1,"id":"prov-002","cmd":"provision",
   "payload":{"valve_mac":"00:80:E1:27:F7:BB","ble_leak_sensors":["00:80:e1:2a:3b:00","00:80:e1:2a:3f:59"]}}
  ```

## Build & flash
```
idf.py build                      # confirm version 1.4.4
idf.py -p <PORT> flash monitor    # no erase needed if board already has nvs_prov (1.4.3)
```
Confirm boot banner `App version: 1.4.4` and `NVS_STORE: … 'nvs_prov' ready`. Get the hub online (MQTT up).

## Tests
Legend **P** = pass criteria.

### C1 — Latency (headline)
1. With T-MON running, **start a stopwatch** and send the `provision` command.
2. Serial: `C2D cmd='provision'` → `Commission: fast snapshot armed (publishes on all-devices-seen, else <=120s)`
   → `HEALTH_ENGINE: Boot sync: all devices seen` (early) **or** `Boot sync: timeout (2 min)` (deadline) →
   `Publishing sync snapshot (boot/commission window complete)` → `TELEMETRY_V2: Pub snapshot…`.
3. **P:** a `type:"snapshot"` message arrives on T-MON **≤120 s** after the command. Record the measured time.

### C2 — Early-send when devices are live
1. Ensure the valve is connected and the leak sensors have advertised recently (or power-cycle a sensor right
   before to trigger its boot burst).
2. **P:** the snapshot arrives **well under 120 s** (serial shows `all devices seen`, not `timeout`) — i.e. it
   does not wait the full window when everything is already heard.

### C3 — Completeness
1. Provision **N** sensors (vary N).
2. **P:** all N devices appear in the snapshot `data.ble_leak_sensors` / `lora_sensors` (+ the valve).

### C4 — Degraded (missing sensor)
1. Keep one BLE-leak sensor dry **and** not advertising (out of range / battery out).
2. **P:** the snapshot still sends at the **120 s deadline** (serial `Boot sync: timeout`), with that sensor
   `connected:false` / null fields — no hang, no silent drop.

### C5 — Re-entrancy
1. Send `provision` **twice** in quick succession (and/or rely on Azure QoS-1 redelivery).
2. **P:** at most one extra snapshot; no crash, no snapshot storm.

### C6 — Regression (no double-send, periodic intact)
1. After the commission snapshot, leave the hub idle.
2. **P:** the normal **5-min periodic** snapshot cadence continues; the commission snapshot did **not** cause a
   duplicate (two near-identical-`ts` snapshots) beyond the existing boot/periodic behavior.

## Change 2 — twin tags (Azure-side, after enrollment config is applied)
1. Set `initialTwin.tags = {"DeviceBrand":"<TBD>","DeviceModel":"eFloStop","DeviceType":"<TBD>"}` on the DPS
   **enrollment group** (Azure portal / service SDK). For already-provisioned hubs, back-fill:
   `az iot hub device-twin update -n <hub> -d <gw> --tags '{"DeviceBrand":"<TBD>","DeviceModel":"eFloStop","DeviceType":"<TBD>"}'`.
2. **P:** `az iot hub device-twin show -n <hub> -d <gw> --query tags` shows the three tags. Confirm the device's
   **reported properties are unchanged** and it operates identically (tags are not device-visible — verify on
   the service side only).

## Result log
| Test | Expected | Observed | Pass? |
|---|---|---|---|
| C0 version 1.4.4 | banner 1.4.4 | | |
| C1 latency ≤120 s | snapshot within 120 s of `provision` | | |
| C2 early-send | well under 120 s when live | | |
| C3 completeness | all N + valve present | | |
| C4 degraded | sends at 120 s, missing sensor null | | |
| C5 re-entrancy | ≤1 extra snapshot, no storm | | |
| C6 regression | 5-min cadence intact, no double-send | | |
| Change 2 tags | tags present service-side | | |

> After C1–C6 pass: `git merge --no-ff feature/fast-commission-snapshot` → master (1.4.4); do not push unless asked.
