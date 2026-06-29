# TEST PLAN — WiFi-Only Reset Button + Dedicated Commissioning NVS Partition

Bench procedure to validate that the physical reset button clears **only** WiFi credentials (10 s hold, then
reboots into AP) while **commissioning/identity survive** — and that the survival holds across the reset's
reboot, a power-cycle, and a near-full / corrupt default partition. Two truths are checked together: what you see on the
**serial console** (button state machine, NVS partition init/erase logs) and what you see in **Azure
telemetry** (the hub still reports its valve/sensors/identity after the reset).

> Hub firmware under test: this branch (`feature/wifi-only-reset-nvs-partition`, `gateway.fw = 1.4.3`).
> The button is the **WiFi-credentials-only** reset; full factory reset is **app-only** via C2D `decommission`.

---

## 0. Prerequisites & setup

**Hardware**
- Hub flashed with this branch (full chip erase + flash — see the UPGRADE NOTE in `FEATURE_TRACKER.md`),
  **online** (MQTT up, SNTP synced — confirm a snapshot shows a real `ts` and `gateway.fw":"1.4.3"`).
- A **valve** (powered, bonded, BLE-connected) **and** ≥1 **BLE leak sensor**, both **provisioned** to this hub.
- A serial connection to watch the console.
- Access to the hub's reset button, and a stopwatch (to distinguish a <10 s tap from a ≥10 s hold).
- Ability to power-cycle the hub.

**Tooling**
```bash
az extension add --name azure-iot          # once
# convenience vars (fill in):
HUB=<your-iot-hub-name>
DEV=<your-device-id>                        # = gateway id, e.g. GW-34B7DA6AAD54
```

**Two terminals**
- **T-MON** (leave running): `az iot hub monitor-events -n $HUB -d $DEV --properties anno sys --timeout 0`
- **Serial**: `idf.py -p <PORT> monitor` — watch tags:
  - `NVS_STORE` — partition init / scoped erase. Expected good-boot line:
    `commissioning NVS partition 'nvs_prov' ready`. The scoped-erase warning is:
    `commissioning partition 'nvs_prov' full/changed — erasing ONLY this partition`.
  - `RESET_BTN` — the button state machine:
    - `Button pressed — starting 10000 ms hold timer`
    - `Button released early — timer cancelled` (a tap)
    - `10-second hold confirmed — executing WiFi reset` *(log text: "5-second hold confirmed" predates the
      10 s change in the source string — confirm the **10000 ms timer** line above; the hold is 10 s)*
    - `=== LONG PRESS CONFIRMED — CLEARING WIFI CREDENTIALS ===`
    - `Erasing WiFi credentials + starting AP for reconfiguration (commissioning preserved)...`
  - `APP_WIFI` — `Connected! IP: ...` and AP-mode start.
  - `PROVISIONING` / `HUB_IDENT` / `DPS` — loads of valve/sensor/identity/cache after a reset and after a
    power-cycle (proof of survival).

**Reading state:** the hub sends a snapshot every 5 min; to get one on demand, just watch T-MON (the
`gateway` object carries identity; the snapshot carries valve/sensor state). Optionally lower
`snapshot_interval_s` to 60 via a Device Twin desired update for faster snapshots during testing.

**Send helper** (only needed for the factory-reset test, T5):
```bash
send () { az iot device c2d-message send -n "$HUB" -d "$DEV" \
          --data "{\"schema\":\"eflostop.cmd\",\"ver\":1,\"id\":\"$1\",\"cmd\":\"$2\"}" ; }
# usage: send <correlation-id> <command>
```

---

## 1. Test matrix

Legend: **P** = pass criteria. Fill the Result column at the bottom.

### T0 — Version / baseline sanity
1. Flash, open serial, watch T-MON for any telemetry envelope.
- **P:** boot banner reports **1.4.3**; `gateway.fw` is `"1.4.3"` in every envelope; serial shows
  `NVS_STORE: commissioning NVS partition 'nvs_prov' ready` exactly once at boot (no scoped-erase warning on
  a healthy unit).

### T1 — Capture the pre-reset commissioning baseline
1. Confirm the hub is fully commissioned: valve bonded + BLE-connected, ≥1 sensor provisioned, a hub name set.
2. From T-MON, record: `gateway.id` (Gateway ID), `gateway.short_id`, `gateway.name` (hub name), the valve
   state, and the sensor list/count. Note the DPS-derived `dev_id` from the boot serial.
- **P:** all of the above are present and correct. *(This is the survival reference for T2/T3.)*

### T2 — Happy path: 10 s hold clears WiFi only, reboots into AP, commissioning survives  *(headline)*
1. **Press and hold** the reset button. On serial confirm `RESET_BTN: Button pressed — starting 10000 ms
   hold timer`. Keep holding past 10 s.
- **P (a) — clears creds + reboots into AP, still provisioned:** at ~10 s, serial shows
  `10-second hold confirmed — executing WiFi reset` → `=== LONG PRESS CONFIRMED — CLEARING WIFI CREDENTIALS ===`
  → `Erasing WiFi credentials, then rebooting into AP (commissioning preserved in nvs_prov)...` →
  `Rebooting into AP mode...`, then a **reboot** (`rst:0xc (RTC_SW_CPU_RST)`, boot banner, `NVS_STORE:
  commissioning NVS partition 'nvs_prov' ready`). The hub comes up in **AP mode** (no saved WiFi creds), the
  SSID `WiFi-Hub-XXXX` appears in your phone's list, and **the captive portal is responsive** (the reboot gives
  it a clean heap). **Crucially, on this same boot serial MUST show `PROVISIONING: State: PROVISIONED` with the
  valve + sensors loaded from `nvs_prov` — the reboot does NOT forget commissioning.**
- **P (b) — WiFi creds gone:** the hub does **not** auto-reconnect to the old AP. Connect to `WiFi-Hub-XXXX`,
  open the captive portal, and confirm you **must re-enter** WiFi credentials (the previous SSID is not
  silently restored). Re-enter creds; the hub joins and comes back online.
- **P (c) — commissioning SURVIVES:** after it rejoins, T-MON shows the **same** `gateway.id`,
  `gateway.short_id`, `gateway.name`, and the **same** valve + sensor list as the T1 baseline — with **no
  re-provisioning**. Serial on the AP→STA path shows `PROVISIONING`/`HUB_IDENT`/`DPS` loading the existing
  records (no "first boot" / "namespace not found"). The DPS cache is reused (no fresh registration round-trip).

### T3 — Survival across a power-cycle (durability proof)
1. Immediately after T2 (creds re-entered, online), **power-cycle the hub**.
- **P:** on boot, serial shows `NVS_STORE: commissioning NVS partition 'nvs_prov' ready` (no scoped erase),
  and `PROVISIONING`/`HUB_IDENT`/`DPS` load the existing valve/sensor/identity/cache. T-MON's first envelope
  carries the **same** identity + valve/sensor state as T1. *(Confirms the survival in T2 wasn't just RAM
  state — it's persisted in `nvs_prov` and reloaded from flash.)*

### T4 — Accidental short press does nothing  *(no false activation)*
1. **Tap** the button (press and release in well under 10 s). Repeat a couple of times, varying duration up to
   ~8 s.
- **P:** serial shows `Button pressed — starting 10000 ms hold timer` then `Button released early — timer
   cancelled` for each tap. **No** `LONG PRESS CONFIRMED`, **no** AP start, **no** creds change — the hub stays
   connected to its existing WiFi and keeps streaming telemetry uninterrupted.
2. (Optional) Hold for ~9 s then release just before 10 s.
- **P:** still cancelled — no reset.

### T5 — Full factory reset is app-only (C2D `decommission`)
*Confirms the only path that wipes commissioning is the app command — not the button.*
1. With the hub fully commissioned and online, `send dec decommission` *(use the actual decommission command
   name/payload your build expects)*.
- **P:** the hub clears commissioning: identity, provisioning, DPS cache, sensor_meta, and rules-engine state
  are wiped (serial shows the decommission/clear logs; `hub_identity_clear()` + `dps_clear_cache()` +
  `rules_engine_clear_persistent_state()` run). On the next snapshot the gateway no longer reports a paired
  valve/sensor or hub name, and DPS re-registers on the next boot. *(This is the ONLY full-reset path — the
  button can never reach it.)*
2. Re-commission via the app afterwards to restore the test rig.

### T6 — Default-partition near-full / corrupt does NOT lose commissioning  *(root-cause regression)*
*Reproduces the original exposure: a default-partition full-erase recovery must no longer take commissioning
with it.*
1. Force the **default** `nvs` partition into the recovery branch. Easiest bench method: with the unit powered
   off, use `esptool`/`parttool` to **erase only the default `nvs` partition region** (leave `nvs_prov`
   intact), e.g. erase the flash range mapped to the `nvs` entry in `partitions.csv`. *(Alternative: fill the
   default partition until `nvs_flash_init()` returns `ESP_ERR_NVS_NO_FREE_PAGES` — the offline buffer + WiFi
   creds churn there.)*
2. Boot the hub.
- **P:** serial shows the **default-partition** recovery fire (`nvs_flash_erase()` path in `app_main`, i.e.
  WiFi creds + offline buffer are gone → the hub goes to AP / asks for WiFi), **but** `NVS_STORE:
  commissioning NVS partition 'nvs_prov' ready` prints with **no** scoped-erase warning, and
  `PROVISIONING`/`HUB_IDENT`/`DPS` still load the existing valve/sensor/identity/cache. After you re-enter
  WiFi, T-MON reports the **same** identity + valve/sensor list as T1. *(Before the partition split, this
  exact recovery branch wiped commissioning too — that is the bug this feature closes.)*
3. (Optional inverse) Corrupt/erase **`nvs_prov`** instead and boot.
- **P:** `NVS_STORE` logs the scoped erase (`erasing ONLY this partition`) and the hub asks for a one-time
  re-commission — while the **default** partition (WiFi creds) is untouched, i.e. it stays joined to WiFi.
  *(Confirms the two partitions fail independently in both directions.)*

---

## 2. Results log

| Test | Result (pass/fail) | Notes / captured serial + JSON |
|---|---|---|
| T0 version 1.4.3 + nvs_prov ready | | |
| T1 baseline captured | | |
| T2 10 s hold → reboot into AP, creds gone, commissioning survives | | |
| T3 survival across power-cycle | | |
| T4 short press does nothing | | |
| T5 factory reset via C2D decommission | | |
| T6 default-partition corrupt → commissioning survives | | |

---

## 3. Notes / gotchas
- T2 now expects a **reboot** after the 10 s hold (`rst:0xc (RTC_SW_CPU_RST)` + boot banner). The button clears
  WiFi creds, then `esp_restart()` so the captive portal comes up on a clean heap (responsive). The decisive
  check is that the **post-reboot boot still shows `PROVISIONING: State: PROVISIONED`** — commissioning lives in
  `nvs_prov` and must survive the reboot.
- If T2(c)/T3 ever shows "first boot" / "namespace not found" for provisioning or identity, commissioning was
  lost — STOP and check that all five modules open via `nvs_open_from_partition(NVS_PROV_PARTITION, …)` and
  that `nvs_store_init()` runs **before** `hub_identity_init()` in `app_main`.
- After all tests pass: merge `feature/wifi-only-reset-nvs-partition` → `master` with `git merge --no-ff`. Do
  **not** push unless asked. Update `FEATURE_TRACKER.md` milestone status (BUILD/BENCH → done).
