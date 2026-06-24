# TEST PLAN — Network-Status RGB LED (3 states)

Bench procedure to validate the GPIO38 status LED end-to-end. The LED is **firmware-internal UX** (not in
the cloud SRS), so this is an **observation** test: you flash, watch the single WS2812 on the hub, and
confirm each transition against the **serial console**. No Azure/app tooling required (though MQTT must be
reachable to reach the "fully connected" state).

> Hub firmware under test: this branch (`feature/network-led-states`, `gateway.fw = 1.4.2`).

---

## 0. Prerequisites & setup
**Hardware**
- Hub flashed with this branch. A way to control its internet:
  - a WiFi AP you can **turn off / move out of range** (to drop the STA), and
  - optionally a way to **block outbound MQTT** (e.g. firewall TCP 8883, or pull the WAN) to drop only the
    IoT-Hub session while WiFi stays up.
- (Optional, for the green overlay check) a provisioned LoRa sensor that transmits.

**Serial**: `idf.py -p <PORT> monitor`. Key lines to watch:
- Boot banner — confirm `Project version: 1.4.2` (or `App version: 1.4.2`).
- `NET_STATUS: wifi=<0|1> mqtt=<0|1>` — printed on **every** state change. This is the ground truth the LED
  should match.
- `APP_WIFI: Connected! IP: ...` / `APP_WIFI: WiFi Disconnected. Reason: ...`
- `IOTHUB: Connected to Azure IoT Hub!` / `IOTHUB: Disconnected.`

**LED legend (what you should see)**
| LED | Meaning | serial truth |
|---|---|---|
| **Red, smooth fade up/down** | No internet | `wifi=0 mqtt=0` |
| **Blue, 3 quick beats (~500 ms apart)** | Connecting (got IP, IoT Hub not up) | `wifi=1 mqtt=0` |
| **Blue, smooth fade up/down** | Fully connected | `wifi=1 mqtt=1` |
| **Green, single short blip** | LoRa packet received (overlay, returns to base) | — |

> Cadence is the tell between the two blues: **beats** = connecting, **breathe/fade** = connected.

---

## 1. Test matrix
Legend: **P** = pass criteria. Fill the Result column at the bottom.

### T0 — Version sanity
1. Flash, open serial. **P:** boot banner reports **1.4.2** (no hardcoded version drift).

### T1 — Boot with no usable WiFi → No internet
1. Power on with the AP **off** (or wrong credentials). **P:** LED is **ramp red** from boot; serial shows
   `NET_STATUS: wifi=0 mqtt=0`.

### T2 — WiFi associates / gets IP → Connecting
1. Turn the AP on (or provision creds) so the hub gets an IP, but before MQTT comes up.
2. **P:** at `APP_WIFI: Connected! IP:` the LED switches to **blue beat (500 ms)**; serial `wifi=1 mqtt=0`.
   (This window may be short if MQTT connects quickly — the serial line confirms it occurred.)

### T3 — IoT Hub MQTT connects → Fully connected
1. Let bring-up finish (SNTP → DPS → MQTT).
2. **P:** at `IOTHUB: Connected to Azure IoT Hub!` the LED switches to **blue ramp/breathe**; serial
   `NET_STATUS: wifi=1 mqtt=1`. Transition appears within ~≤0.5 s of the log line (animations are interruptible).

### T4 — MQTT drops, WiFi stays up → back to Connecting
1. Block outbound MQTT only (firewall TCP 8883 / pull WAN but keep the AP up).
2. Wait for `IOTHUB: Disconnected.` (esp-mqtt may take a few seconds / a keepalive interval to notice).
3. **P:** LED returns to **blue beat**; serial `wifi=1 mqtt=0`.
4. Restore MQTT → **P:** LED returns to **blue ramp**; `wifi=1 mqtt=1`.

### T5 — WiFi drops → No internet (and no false "connected" on reconnect)
1. Turn the AP off / move out of range.
2. **P:** at `APP_WIFI: WiFi Disconnected.` the LED switches to **ramp red**; serial `NET_STATUS: wifi=0 mqtt=0`
   (note **mqtt forced to 0 immediately**, even though `IOTHUB: Disconnected.` may not have printed yet).
3. Turn the AP back on. **P:** LED shows **blue beat** (connecting) first, then **blue ramp** once
   `IOTHUB: Connected` prints. **It must NOT jump straight from red to blue-ramp** (that would be the M1 bug —
   a false "connected" before MQTT re-establishes).

### T6 — LoRa green overlay still works (if a sensor is available)
1. With the hub in any base state (red/connecting/connected), trigger a LoRa packet.
2. **P:** a single **green blip** interrupts, then the LED **returns to the same base animation** (incl. blue
   ramp when fully connected). `stateColor` is unchanged by the overlay.

### T7 — Flap stress (optional)
1. Toggle WiFi/MQTT a few times in quick succession.
2. **P:** the LED always settles on the animation matching the **final** `NET_STATUS: wifi=.. mqtt=..` line —
   no stuck/contradictory state, no permanent wrong colour (validates the derive-from-truth + interruptible
   animation handling a backed-up queue).

---

## Result log
| Test | Expected | Observed | Pass? | FW |
|---|---|---|---|---|
| T0 version 1.4.2 | banner 1.4.2 | | | |
| T1 boot no-wifi → red ramp | red ramp, wifi=0 mqtt=0 | | | |
| T2 got IP → blue beat 500ms | blue beat, wifi=1 mqtt=0 | | | |
| T3 MQTT up → blue ramp | blue ramp, wifi=1 mqtt=1 | | | |
| T4 MQTT drop → blue beat → restore | beat then ramp | | | |
| T5 WiFi drop → red; reconnect via beat | red; beat→ramp (no false connected) | | | |
| T6 LoRa green overlay returns to base | green blip → base | | | |
| T7 flap settles to final truth | matches last NET_STATUS line | | | |

> After all pass: merge `feature/network-led-states` → `master` with `git merge --no-ff`. Do **not** push
> unless asked. Update `FEATURE_TRACKER.md` milestone status.
