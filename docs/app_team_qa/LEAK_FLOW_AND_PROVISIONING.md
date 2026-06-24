# eFloStop II — Leak Flow & Provisioning (App Team Q&A)

> Answers verified against the hub firmware. Devices in scope: the **valve** and **BLE leak sensors**
> (plus the valve's **own on-board flood probe**). Event names below are the literal `data.event`
> strings in the `eflostop.v2` telemetry.

---

## Part 1 — The leak flow, all cases

There are **two independent things** that happen when a sensor goes wet, and keeping them separate is
the key to the whole flow:

1. **Reporting** — the leak is *always* published as a `leak_detected` event and reflected in the
   snapshot (`ble_leak_sensors[].leak_state = true`). This happens **regardless** of any auto-close
   setting. When the sensor dries it publishes `leak_cleared`.
2. **Shutoff (auto-close)** — the hub closes the valve and asserts the **RMLEAK interlock**. This only
   happens when the rules engine decides the leak is "eligible" (see gates below).

So: **a leak is always reported; the valve is only closed when shutoff is enabled and applies.**

### Auto-close eligibility gates (all must be true to close the valve)
1. Hub is provisioned.
2. **Master auto-close is enabled** (`rules.auto_close_enabled`).
3. The leak source's **type bit is in the trigger mask** (BLE-leak bit, or valve-flood bit).
4. No active **24 h water-access override window**.

### Case matrix

| # | Condition | Reported? | Valve | RMLEAK | Incident latched | 30 s auto-clear (`rmleak_auto_cleared`) | Events emitted |
|---|---|---|---|---|---|---|---|
| **A** | Auto-close **ON**, source in trigger mask, no override | ✅ | **Closes** | **Set** | Yes | ✅ fires 30 s after all sensors dry | `leak_detected` → `auto_close` → `valve_state_changed{closed,rmleak:true}` → (on dry, +30 s) `rmleak_auto_cleared` → `leak_cleared` |
| **B** | Auto-close **OFF** (master disabled) | ✅ | **Stays open** | Not set | **No** | **No** | `leak_detected` … `leak_cleared` only |
| **C** | Auto-close ON but **this source type not in trigger mask** | ✅ | **Stays open** | Not set | No | No | `leak_detected` … `leak_cleared` only |
| **D** | **Override window active** (24 h water access) | ✅ | **Stays open** | Not set by this leak | Yes (latched, close blocked) | n/a while window open | `leak_detected` → `auto_close_blocked_override` (rate-limited) … `leak_cleared` |
| **E** | Valve's **own flood probe** wets | ✅ (as valve leak) | **Closes autonomously** (valve hardware floor) | — | Hub: per gates | — | valve `leak_state` + `valve_state_changed`; hub-side RMLEAK only if gates pass |

> **Case E note (valve-side):** the valve closes on its *own* flood probe **independently of the hub**,
> even hub-offline and even if hub auto-close is disabled — it's a hardware safety floor. The hub-side
> RMLEAK/auto-close reaction to a flood still follows the gates above. Exact valve-button / valve-flood
> internals are valve-firmware behaviour; confirm specifics with the valve FW owner.

---

## Part 2 — Direct answers to your questions

### Q1. Auto-lock disabled — how is the leak handled? (Case B)
**Your assumption is correct.** With master auto-close disabled, when a sensor reports a leak:
- The leak **is reported** (`leak_detected`), and the snapshot shows **which** sensor
  (`ble_leak_sensors[]` with `sensor_id`, `location`, `leak_state:true`). → show your banner.
- The **valve stays open**. The hub does **not** latch an incident and does **not** assert RMLEAK
  (firmware returns early before any of that — `rules_engine.c`, master-enable check).

### Q2. User closes the valve manually (app "Close Valve", or physical valve button) — do we enter the regular leak-handling flow?
**No — and this is an important correction.** A manual close (the `valve_close` C2D command, or the
physical button on the valve) **only closes the valve**. It does **not**:
- assert RMLEAK (so there is **no interlock** — the valve can be reopened freely),
- latch a leak incident,
- start the 30 s timer,
- emit `auto_close` / `rmleak_auto_cleared`.

It emits **only** `valve_state_changed{valve_state:"closed", rmleak:false}`. The sensor's
`leak_detected`/`leak_cleared` reporting continues independently. 

So the "regular flow for leak handling" (RMLEAK interlock + 30 s auto-clear) is entered **only** when the
hub *auto-closes* (Case A). A manual close is just a plain close.

- **Can the user also close physically via the valve button?** Yes. The hub observes the physical valve
  state over BLE and reports it as `valve_state_changed`. Same outcome as the app button — no RMLEAK,
  no incident. *(The valve's single-long-press "override" gesture is a separate thing — it's used to
  re-open the valve and clear RMLEAK during an auto-closed leak, which the hub turns into the 24 h
  override window. It is not a plain close.)*

> **Design flag for the team:** if you want a user-initiated close *during a leak with auto-close off* to
> behave like the protected flow (interlock + auto-clear), that is **not** what `valve_close` does today.
> It would need a new firmware command ("close + assert RMLEAK"). Worth a conversation if the UX calls
> for it.

### Q3. If the user doesn't close it and the leak is fixed (sensors dry), does it auto-clear after 30 s (`rmleak_auto_cleared`)?
**Only in the auto-close flow (Case A) — not when auto-close is disabled (Case B).** The 30 s timer and
the `rmleak_auto_cleared` event exist purely to lift the **RMLEAK interlock** that auto-close sets. If
RMLEAK was never set (auto-close off, or a manual close), there is nothing to auto-clear, and **no
`rmleak_auto_cleared` event is emitted** — the flow is simply `leak_detected` → `leak_cleared`.

What the 30 s actually does (Case A): once **all** sources are dry for 30 continuous seconds, the hub
clears RMLEAK (`rmleak_auto_cleared`) so the valve *can* be reopened again — **it does not reopen the
valve**, it only removes the interlock. (`leak_reset` is the manual instant version of the same thing,
and it is refused while any leak is still active.)

---

## Part 3 — Provisioning

### Q4. MAC pushed → hub returns cmd_ack → provisioned, no further steps?
**Essentially yes — it's a single C2D command.** The hub validates the payload, saves to NVS, marks
itself `PROVISIONED`, and immediately starts acting on it (connect to the valve, whitelist sensors).
A `cmd_ack` with `status:"ok"` is returned. **But two caveats:**
- `status:"ok"` means **the configuration was accepted and stored** — it does **not** confirm the
  physical device was found/connected. Confirm real connectivity from the next **snapshot**:
  `valve.connected:true` / the sensor appearing in `ble_leak_sensors[]` with a `rating`.
- Provisioning is a **merge** — sending `valve_mac` / `ble_leak_sensors` adds/updates them; you can
  provision the valve and sensors in one payload or incrementally.

### Q5. Errors to handle during provisioning
Yes — these cause `cmd_ack status:"error"` (or no effect). Handle them in the app:

| Situation | Hub behaviour |
|---|---|
| Malformed JSON | Rejected → error |
| **Invalid `valve_mac` format** (not `XX:XX:XX:XX:XX:XX`, 17 chars, hex+colons) | **Whole payload rejected** → error |
| Payload has no recognizable fields | Rejected ("no valid provisioning data") → error |
| NVS write/commit failure (rare) | Rejected → error |
| **Invalid sensor MAC inside `ble_leak_sensors[]`** | ⚠️ **Silently skipped** (that entry is not added), command can still ack **ok** for the rest |
| More than **16** BLE sensors | Extra entries truncated with a warning (bulk path) |
| cmd sent **before the hub's clock is synced** (just after boot) | The ack may not be emitted until SNTP sync; retry / wait |

> ⚠️ **Most important:** because invalid sensor MACs are *silently skipped*, **do not treat `cmd_ack:ok`
> as "all sensors added."** Verify the resulting sensor count / list in the snapshot or device twin.

### Q6. QR code contents, and detecting valve-vs-sensor by MAC
**The hub firmware does not parse QR codes at all** (QR handling is entirely app-side), and **it does not
distinguish device type by MAC.** The hub trusts your placement: whatever MAC you send in `valve_mac`
is treated as the valve; whatever you send in `ble_leak_sensors[]` is treated as a leak sensor. There is
**no OUI/prefix/schema rule** in the firmware, and **no error** is raised if a valve's MAC is sent as a
sensor (or vice-versa) — the hub would accept it and simply never see the expected traffic from it.

**Consequences / recommendation:**
- **Device-type discrimination must be enforced app-side.** The firmware provides no backstop for the
  "scanned a valve as a sensor" edge case.
- **Put the device type in the QR payload** (e.g. an explicit `type: "valve" | "leak_sensor"` field, or a
  type prefix), so the app can reject a mismatched scan *before* sending to the hub — rather than relying
  on MAC heuristics.
- If you ever do need a MAC-based hint, the valve and the sensors use different manufacturing OUI ranges,
  but those are **not** validated or guaranteed by the hub — treat an explicit QR type field as the source
  of truth.

---

## Summary of corrections to the original assumptions
- ✅ Auto-close off → banner + which sensor + valve stays open — **correct**.
- ⚠️ Manual close → "regular leak flow" — **no**; manual close has no RMLEAK interlock, no incident, no
  auto-clear. It's just a close.
- ⚠️ "after 30 s of dry → `rmleak_auto_cleared`" — **only** in the auto-close flow; **not** when
  auto-close is disabled (no RMLEAK was ever set).
- ✅ Provision = one command + ack — **yes**, but `ok` = *config stored*, not *device connected*; confirm
  via snapshot, and note invalid sensor MACs are skipped silently.
- ❗ QR/device-type: firmware does **no** QR parsing and **no** type-by-MAC detection — enforce in the app,
  ideally via a type field in the QR.
