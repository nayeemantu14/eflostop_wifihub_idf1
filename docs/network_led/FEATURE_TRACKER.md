# FEATURE TRACKER — Network-Status RGB LED (3 states)

> Continuity file so a fresh chat can resume without re-reading everything. Update at every milestone.

## What we're building
Replace the hub's 2-state network status LED (red = not connected, blue = connected) with **three**
states on the single WS2812 on **GPIO38**, driven by the existing queue-fed `led_task`:

| State | Condition | LED effect |
|---|---|---|
| **No internet** | WiFi down / no STA IP | **ramp red** (`rampRED`, unchanged) |
| **Connecting** | got IP, but Azure IoT Hub / MQTT not yet connected | **beat blue @ 500 ms** (`beatBLUE`, inter-beat 2000→500) |
| **Fully connected** | WiFi up **and** MQTT connected | **ramp blue** (`rampBLUE`, NEW — same fade as red on the blue channel) |

The green LoRa-activity pulse stays an **independent one-shot overlay** (unchanged).

The hub status LED is **firmware-internal UX** — it is NOT part of the cloud/app SRS, so **no SRS change**.

## Branch
`feature/network-led-states` (hub repo). No valve / sensor / cloud changes. Merge to `master` with `--no-ff` **after the bench test passes**. Do not push unless asked.

## Repos / key paths
- Hub (ESP32-S3, ESP-IDF v5.5.1): `c:\Work\Projects\EfloStop 2\Firmware\Production\eFloStop_WiFiHub_idf1`
- Build/flash is done by the **user** (assistant cannot run `idf.py`). Testing = user flashes, observes the LED, shares serial logs.

## Design decision — connection-state coordinator (chosen)
Rather than have each event blindly send a colour, a tiny **coordinator module** owns the two facts that
decide the LED and derives the colour from the *full* truth under a mutex. The WiFi and MQTT events fire on
**different FreeRTOS tasks in an unspecified order**, so deriving from both flags (instead of "send blue on
mqtt event / red on wifi event") is what makes the ordering irrelevant.

- `!wifi` → `LED_CMD_NO_INTERNET` (ramp red)
- `wifi && !mqtt` → `LED_CMD_CONNECTING` (beat blue)
- `wifi && mqtt` → `LED_CMD_CONNECTED` (ramp blue)
- **Invariant:** mqtt-up only matters while wifi is up, so `net_status_set_wifi(false)` also clears the mqtt
  flag (the TLS/MQTT session can't survive WiFi loss even before esp-mqtt fires `MQTT_EVENT_DISCONNECTED`).
  Without this, a WiFi flap would briefly show a **false "connected"** (ramp blue) on reconnect.

This was validated by a design panel + a 2-lens adversarial review (concurrency/correctness + build/integration)
before any code was written. Findings folded in: M1 (clear-mqtt-on-wifi-down), NULL-guard the mutex,
interruptible animations, `'F'` in **both** led_task switches, explicit `semphr.h`, single LED authority.

## Deliverables (status)
| File | Purpose | Status |
|---|---|---|
| `main/net_status/net_status.{c,h}` | connection-state coordinator → LED command | ✅ DONE |
| `main/rgb/rgb.{c,h}` | `rampBLUE`, 500 ms beat, `'F'` state, interruptible animations, `LED_CMD_*` codes | ✅ DONE |
| `main/app_wifi/app_wifi.c` | wifi up/down → `net_status_set_wifi()` (removed direct LED sends) | ✅ DONE |
| `main/iothub/app_iothub.c` | MQTT connect/disconnect → `net_status_set_mqtt()` (NEW hook) | ✅ DONE |
| `main/main.c` | `net_status_init()` after `setupLEDTask()` | ✅ DONE |
| `main/CMakeLists.txt` | register `net_status` (SRCS + INCLUDE_DIRS) | ✅ DONE |
| `CMakeLists.txt` | `PROJECT_VER` 1.4.1 → **1.4.2** | ✅ DONE |
| `docs/network_led/FEATURE_TRACKER.md` | this file | ✅ live |
| `docs/network_led/TEST_PLAN.md` | bench observation procedure | ✅ DONE |

## Milestone status
- **DESIGN + REVIEW:** ✅ done (coordinator chosen; adversarial review folded in). No code written before review.
- **IMPLEMENT:** ✅ done on `feature/network-led-states` (commit `33f6ae1`). **Not yet built/flashed** — user does that.
- **CODE VERIFY:** ✅ adversarial bug-hunt on the *actual* committed code (3 lenses: compile / runtime-concurrency / spec) returned **zero** findings; gate verdict **safe to flash as-is**. Confirmed: net_status.c includes complete (`semphr.h` explicit); `bool` reachable via FreeRTOS→portmacro→`<stdbool.h>`; `'F'` in both led_task switches; mutex NULL-guard + no give-without-take; CMake registration resolves; PROJECT_VER 1.4.2 single-source. Only note: pre-existing cosmetic unreachable `vTaskDelete(NULL)` after `while(1)` (no `-Werror` trap).
- **BUILD (user):** ✅ clean build on FW 1.4.2.
- **BENCH TEST (user):** ✅ **PASS 2026-06-24** — user confirmed all three states + transitions behave as intended ("working perfectly like I wanted it"). Boot=red ramp, WiFi up=blue beat 500 ms, MQTT up=blue ramp, MQTT drop=blue beat, WiFi drop=red ramp; LoRa green overlay intact.
- **MERGE:** ✅ `git merge --no-ff feature/network-led-states` → `master` (2026-06-24). **Not pushed** (per directive — push only when asked).

## Change set (implemented, on feature branch)
- **`rgb.h`**: added `LED_CMD_NO_INTERNET 'R'`, `LED_CMD_CONNECTING 'B'`, `LED_CMD_CONNECTED 'F'`,
  `LED_CMD_LORA_PULSE 'G'`, `LED_CMD_CLEAR 'C'`; declared `rampBLUE`.
- **`rgb.c`**: factored shared `rampColor(strip, blue)` (no array duplication) → `rampRED`/`rampBLUE` thin
  wrappers; `beatBLUE` inter-beat **2000→500 ms**; added `led_cmd_pending()` (non-blocking `xQueuePeek`) and
  poll it between every animation step so a state change is reflected in **~≤0.5 s** instead of after a full
  ~4.5 s cycle (and the depth-10 queue stays drained during a flap); `led_task` now handles `'F'` in **both**
  the receive switch (sets `stateColor`) and the animation switch (plays `rampBLUE`); switches use the
  `LED_CMD_*` macros. Default boot state = ramp red.
- **`net_status.{c,h}`** (NEW): `s_wifi_up`/`s_mqtt_up` + mutex; `net_status_init/set_wifi/set_mqtt`; derive +
  `xQueueSend` under the mutex; setters NULL-guard the mutex; `set_wifi(false)` also clears mqtt; explicit
  `#include "freertos/semphr.h"`. Logs `NET_STATUS wifi=.. mqtt=..` on each change.
- **`app_wifi.c`**: `cb_connection_ok` → `net_status_set_wifi(true)`; `cb_connection_lost` →
  `net_status_set_wifi(false)`; `wifi_task` no longer sends the startup `'R'` (init latches it). Removed the
  now-unused direct `xQueueSend(ledQueue,...)` + `rgb.h`/`queue.h` includes (FreeRTOS symbols still come via
  `app_wifi.h`).
- **`app_iothub.c`**: `#include "net_status/net_status.h"`; `MQTT_EVENT_CONNECTED` → `net_status_set_mqtt(true)`;
  `MQTT_EVENT_DISCONNECTED` → `net_status_set_mqtt(false)`.
- **`main.c`**: `#include "net_status/net_status.h"`; `net_status_init()` immediately after `setupLEDTask()`
  (additive — no reorder of existing inits).
- **CMake**: `main/CMakeLists.txt` += `net_status/net_status.c` (SRCS) + `net_status` (INCLUDE_DIRS); top-level
  `PROJECT_VER` 1.4.1 → **1.4.2** (single source — banner/OTA/telemetry read it at runtime).
- **`app_lora.cpp`**: untouched (still sends raw `'G'`, which equals `LED_CMD_LORA_PULSE`).

## Hard constraints (carry into every change)
1. **Match the existing FreeRTOS pattern** — queue-fed `led_task`, `xQueueSend`/`xQueueReceive`,
   `vTaskDelay(pdMS_TO_TICKS())`. No busy-waits, no blocking in callbacks (the mutex critical section is
   microseconds and never nests → leaf lock, no deadlock; safe inside the esp-mqtt event callback).
2. **Single LED authority** — only `net_status` drives the R/B/F network states; LoRa `'G'` is an independent
   overlay. Don't reintroduce direct R/B sends elsewhere.
3. **Version single-sourced** in `PROJECT_VER` only — never hardcode a version string.
4. **Don't reorder `app_main` init.** `net_status_init()` must stay after `setupLEDTask()` (ledQueue exists)
   and before `app_wifi_start()` (callbacks that call the setters).
5. **`wifi_manager` is a managed component with LOCAL PATCHES** — not touched here; re-apply if it re-resolves.

## Known/intended behaviour notes (for the test plan)
- Connecting (beat blue) and connected (ramp blue) are **both blue** by spec — distinguished by **cadence**
  (3 quick beats vs a smooth breathe). Reviewer suggested green-for-connected; rejected because the task table
  prescribes ramp blue and green is the LoRa overlay.
- Transition latency is now ~≤0.5 s (one animation step), not instant — acceptable for a status indicator.
- esp-mqtt may take seconds to notice a dropped TCP session and fire `MQTT_EVENT_DISCONNECTED`; the WiFi-down
  path clears mqtt immediately so the LED never lags into a false "connected".
