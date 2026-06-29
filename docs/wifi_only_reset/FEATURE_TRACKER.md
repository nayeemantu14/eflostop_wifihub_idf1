# FEATURE TRACKER — WiFi-Only Reset Button + Dedicated Commissioning NVS Partition

> Continuity file so a fresh chat can resume without re-reading everything. Update at every milestone.

## What we're building
Make the hub's **physical reset button** a *WiFi-credentials-only* reset that can **never** wipe
commissioning/identity, and harden the NVS layout so no boot-time recovery path can either:

| Aspect | Before | After (this branch) |
|---|---|---|
| **Button action** | clears WiFi creds, then `esp_restart()` | clears WiFi creds **only**, then **reboots** into AP/captive-portal (commissioning in `nvs_prov` survives the reboot) |
| **Hold time** | 5 s | **10 s** (avoid accidental activation) |
| **Commissioning storage** | default `"nvs"` partition (shared with WiFi creds) | **dedicated `"nvs_prov"` partition** (button + default-partition erase can't reach it) |
| **Full factory reset** | (implicit, via any full erase) | **app-only** via C2D `decommission` |

The button is **firmware-internal UX** — it is NOT part of the cloud/app SRS, so **no SRS change**. The
factory-reset-via-app contract is already covered by the existing `decommission` C2D path.

## Branch
`feature/wifi-only-reset-nvs-partition` (hub repo). No valve / sensor / cloud-contract changes. Merge to
`master` with `--no-ff` **after the bench test passes**. Do not push unless asked.

## Repos / key paths
- Hub (ESP32-S3, ESP-IDF v5.5.1): `c:\Work\Projects\EfloStop 2\Firmware\Production\eFloStop_WiFiHub_idf1`
- Build/flash is done by the **user** (assistant cannot run `idf.py`). Testing = user flashes, holds the button,
  observes serial + Azure telemetry, power-cycles to confirm survival.

## Root cause — why this is more than a button tweak
The button itself was **already WiFi-only**: `execute_wifi_reset()` called
`wifi_manager_disconnect_async()`, whose worker `memset`s the WiFi config to 0 and saves it — touching only
the wifi_manager namespace in the default `"nvs"` partition. **It never erased commissioning.**

The real exposure was the **default-partition full-erase recovery in `app_main()`**:

```c
esp_err_t ret = nvs_flash_init();
if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());   // <-- unconditional, ERASES THE WHOLE DEFAULT PARTITION
    ret = nvs_flash_init();
}
```

That `nvs_flash_erase()` wipes **every namespace** in the default partition — and pre-split, commissioning
(`provision`, `hub_ident`, `dps_cache`, `sen_meta`, `rules_eng`) **lived right there alongside the WiFi
creds and the offline buffer** in a tiny **16 KB (`0x4000`)** partition. A feasibility analysis confirmed
that on a sufficiently full / version-bumped / corrupted default partition, this branch could fire on a
*later, unrelated boot* and silently take out commissioning — with no button press involved. So a button
fix alone was insufficient; **the durable data had to leave the partition that gets erased.**

**Fix:** move commissioning/identity into a **dedicated `nvs_prov` partition**. The recovery `nvs_flash_erase()`
above now only ever erases the default partition (WiFi creds + offline buffer — both reconstructible), and the
new `nvs_store_init()` corruption-recovery erases **only** `nvs_prov`. Neither path can cross into the other.

## Design decision — dedicated partition over "be careful" (chosen)
We rejected the alternatives of (a) making the default-partition erase *conditional/selective* (NVS has no
"erase all namespaces except these" primitive — full-partition erase is the only recovery) and (b) just
trusting the 16 KB partition not to fill (commissioning + offline buffer + WiFi creds + BLE bonds churn).
A **physical partition boundary** is the only structural guarantee that a WiFi-creds erase and a commissioning
erase are independent events. The split is enforced by the API: commissioning modules call
`nvs_open_from_partition(NVS_PROV_PARTITION, …)`; the default partition keeps WiFi creds + offline buffer.

## Deliverables (status)
| File | Purpose | Status |
|---|---|---|
| `main/nvs_store/nvs_store.{c,h}` | dedicated-partition init + scoped corruption-recovery | ✅ DONE |
| `partitions.csv` | `+ nvs_prov, data, nvs, , 0x4000` after default `nvs` | ✅ DONE |
| `main/main.c` | `nvs_store_init()` after `nvs_flash_init()`, before `hub_identity_init()` | ✅ DONE |
| 5 commissioning modules → `nvs_open_from_partition` | move namespaces into `nvs_prov` | ✅ DONE |
| `main/wifi_reset/reset_button.c` | 10 s hold + clear-creds-then-reboot into AP | ✅ DONE |
| `main/CMakeLists.txt` | register `nvs_store` (SRCS + INCLUDE_DIRS) | ✅ DONE |
| `CMakeLists.txt` | `PROJECT_VER` 1.4.2 → **1.4.3** | ✅ DONE |
| `docs/wifi_only_reset/FEATURE_TRACKER.md` | this file | ✅ live |
| `docs/wifi_only_reset/TEST_PLAN.md` | bench procedure | ✅ DONE |

## Milestone status
- **DESIGN:** ✅ done (root-cause feasibility analysis → dedicated-partition split chosen; 10 s hold; reboot-into-AP for captive-portal heap responsiveness).
- **IMPLEMENT:** ✅ done on `feature/wifi-only-reset-nvs-partition`. All five commissioning modules verified on
  the as-built tree to use `nvs_open_from_partition(NVS_PROV_PARTITION, …)`; only `offline_buffer.c` still
  uses plain `nvs_open` (default partition, intentional). `main.c` init order confirmed
  `nvs_flash_init()` → `nvs_store_init()` → `hub_identity_init()`.
- **BUILD (user):** ⏳ pending — assistant cannot run `idf.py`.
- **BENCH TEST (user):** ⏳ pending — see `TEST_PLAN.md`.
- **MERGE:** ⏳ pending bench pass — `git merge --no-ff` → `master`, do not push unless asked.

## Change set (implemented, on feature branch)
- **`main/nvs_store/nvs_store.{c,h}`** (NEW): `#define NVS_PROV_PARTITION "nvs_prov"` + `void nvs_store_init(void)`.
  `nvs_store_init()` calls `nvs_flash_init_partition(NVS_PROV_PARTITION)`; on `ESP_ERR_NVS_NO_FREE_PAGES` /
  `ESP_ERR_NVS_NEW_VERSION_FOUND` it logs a warning and calls `nvs_flash_erase_partition(NVS_PROV_PARTITION)`
  then re-inits — **erasing ONLY `nvs_prov`, never the default partition**. If the partition is missing or
  still un-initable (e.g. a 1.4.3 image flashed over an old layout that has no `nvs_prov`), `nvs_store_init()`
  **fails soft**: it logs `ESP_LOGE` and returns rather than `ESP_ERROR_CHECK`-panicking, so the hub still
  boots (WiFi/BLE work) and comes up unprovisioned instead of boot-looping. Header documents the contract
  (commissioning lives here, factory reset is app-only via C2D `decommission`).
- **`partitions.csv`**: added `nvs_prov, data, nvs, , 0x4000,` immediately **after** the default
  `nvs, data, nvs, , 0x4000,` row (both 16 KB). `otadata` / `phy_init` / `factory` / `ota_0` / `ota_1`
  unchanged after it.
- **`main/main.c`**: `#include "nvs_store/nvs_store.h"`; `nvs_store_init()` called right after the
  default-partition `nvs_flash_init()` block and **before** `hub_identity_init()` (which is the first
  consumer of commissioning data). The default-partition erase fallback at `nvs_flash_erase()` is left as-is —
  it is now harmless to commissioning because commissioning no longer lives in that partition.
- **5 commissioning modules** — namespace open changed from `nvs_open(<ns>, …)` to
  `nvs_open_from_partition(NVS_PROV_PARTITION, <ns>, …)` (each module opens only its own namespace, so the
  change is mechanical and uniform):
  - `main/provisioning_manager/provisioning_manager.c` — ns `"provision"` (valve MAC, LoRa sensor IDs, BLE
    leak MACs, rules-enable/trigger). 4 sites: read, write, erase, rules-persist.
  - `main/hub_identity/hub_identity.c` — ns `"hub_ident"` (Gateway ID derive / hub name). 3 sites.
  - `main/dps_client/dps_client.c` — ns `"dps_cache"` (hub_host, dev_id, dev_key, cached). 3 sites (load,
    save, clear).
  - `main/sensor_meta/sensor_meta.c` — ns `"sen_meta"`. 3 sites.
  - `main/rules_engine/rules_engine.c` — ns `"rules_eng"` (`ovr_state` / `ovr_expiry` / `incident`). 6 sites
    incl. `override_clear_nvs()` and `rules_engine_clear_persistent_state()`.
  Each of the five now `#include "nvs_store/nvs_store.h"`.
- **`main/wifi_reset/reset_button.c`**: `HOLD_TIME_MS` 5000 → **10000** (10 s). `execute_wifi_reset()` clears
  the WiFi creds (`wifi_manager_disconnect_async()`), waits 2 s for the NVS commit, then **`esp_restart()`**.
  Rebooting is safe now that commissioning lives in `nvs_prov` (it survives the reboot — proven on bench), and
  a fresh boot gives the SoftAP captive portal a **pristine heap (~130 KB vs ~40 KB)**, so it stays responsive
  under a phone's DNS/HTTP probe storm. (See "AP responsiveness" below for why no-reboot was abandoned.)
- **`main/iothub/app_iothub.c` + `main/app_wifi/app_wifi.c`** (AP-responsiveness fix): `iothub_suspend_mqtt()` /
  `iothub_resume_mqtt()` stop the esp-mqtt client on STA-down and restart it on STA-up, so MQTT doesn't thrash
  TLS handshakes (heap fragmentation) during any WiFi outage. Wired into `cb_connection_lost` / `cb_connection_ok`.
- **`sdkconfig.defaults` + `sdkconfig`**: `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y` — frees the ~16 KB mbedTLS handshake
  buffers between sessions, lowering peak/fragmented heap (relieves a near-OOM `min_ever` floor on this hub).
- **`main/CMakeLists.txt`**: `+ nvs_store/nvs_store.c` (SRCS) and `+ nvs_store` (INCLUDE_DIRS).
- **`CMakeLists.txt`** (top level): `PROJECT_VER` 1.4.2 → **1.4.3** (single source — banner / OTA header /
  telemetry `gateway.fw` / twin read it at runtime via the app descriptor).

## NOT touched (intentional)
- **`main/offline_buffer/offline_buffer.c`** — ns stays in the **default** `nvs`. The offline event buffer is
  transient and fully reconstructible; it must NOT consume `nvs_prov` budget. Left on plain `nvs_open`.
- **`managed_components/.../wifi_manager.c`** — managed component carrying **LOCAL PATCHES**; its WiFi creds
  intentionally stay in the default partition (that is exactly what the button is allowed to clear). Do **not**
  edit it here; re-apply the local patches if the component is re-resolved.

## ⚠️ UPGRADE NOTE (one-time, when flashing 1.4.3 onto an already-deployed hub)
This build **changes the partition table** (adds `nvs_prov`) **and moves commissioning into it**. A normal app
flash will not migrate the old commissioning out of the default partition, and the default-partition layout/size
also shifts. Therefore upgrading a unit requires a **full chip erase + flash**, followed by a **one-time
re-commission via the app** (re-pair the valve + sensors, re-assign the hub name; DPS re-registers from the
group key on first boot, then re-caches into `nvs_prov`).

This is acceptable because **OTA is not live** — units are cable-flashed in production anyway, so they are
erased+flashed as part of the normal bring-up. No automatic NVS migration is shipped (a default→prov copy
shim was considered and rejected: it adds permanent boot-time complexity to serve a window that, with
cable-flash, never occurs in the field). After this one-time step, the data is durable: WiFi-creds resets and
default-partition recovery can no longer touch it.

## Hard constraints (carry into every change)
1. **No cloud/app contract change** — `decommission` remains the only full-reset path; the button is strictly
   WiFi-creds-only. No new C2D commands, no telemetry-field changes.
2. **`offline_buffer` stays on the default partition** — never move it into `nvs_prov`.
3. **`wifi_manager` is a managed component with LOCAL PATCHES** — not touched; re-apply if it re-resolves.
4. **Version single-sourced** in `PROJECT_VER` only — never hardcode a version string.
5. **Reboot on WiFi reset is intentional** — the button clears creds, then `esp_restart()` so the captive
   portal comes up on a clean heap. Safe because commissioning is in `nvs_prov` (survives reboot); the
   default-partition recovery can only touch WiFi/offline data, never commissioning. **Do not** revert to the
   no-reboot drop-to-AP — it left the captive portal heap-starved (see "AP responsiveness").
6. Commit only at the milestone boundary **after explicit approval**; never push.

## Known/intended behaviour notes (for the test plan)
- A press shorter than **10 s** does nothing — the one-shot hold timer is cancelled on early release, and the
  timer-expiry handler re-checks `button_is_pressed()` before acting.
- After a successful reset the hub **reboots** (~2–3 s) and comes up in AP mode (SSID `WiFi-Hub-XXXX`); WiFi
  creds must be re-entered via the captive portal. The reboot is intentional — see "AP responsiveness" below.
- Commissioning survival is the headline: valve + sensor pairing, hub identity (Gateway ID is MAC-derived and
  immutable; hub name is in `nvs_prov`), and the DPS cache all persist across the reset **and** across a
  subsequent power-cycle, because they live in `nvs_prov` which the reset never touches.
- A corrupt / full **default** partition triggers only the default-partition `nvs_flash_erase()` (loses WiFi
  creds + offline buffer) — commissioning in `nvs_prov` is unaffected.

## AP responsiveness — why the button reboots (investigation 2026-06-26)
Bench testing the no-reboot variant showed the captive portal was slow/flaky to join after a button reset, but
fine on a fresh power-on. Root cause = **heap headroom**, not the WiFi/AP code:
- **Cold-boot AP:** ~130 KB free / ~86 KB largest block — BLE valve connection, leak scanner, telemetry caches,
  offline buffer, and MQTT haven't spun up yet, so the captive portal has a pristine heap.
- **No-reboot AP:** the live stack stays loaded (~40 KB free / ~24 KB largest, dipping to ~1 KB), and a phone's
  DNS/HTTP probe storm pushes the http server to `httpd_sock_err`.

Two heap fixes were applied first and did help (they removed the `-0x7F00` MQTT-TLS thrash): MQTT suspend/resume
on STA down/up, and `MBEDTLS_DYNAMIC_BUFFER`. But neither reclaims the ~80–90 KB held by the live BLE/telemetry
stack. The decisive fix is to **reboot on reset**, reproducing the clean-heap power-on automatically (which is
exactly what a manual power-cycle did). Safe now that commissioning is in `nvs_prov` — the original no-reboot
rationale (dodging the default-partition NVS wipe) is obsolete. Kept the two heap fixes too: they still help
ordinary WiFi drops and overall heap headroom.
