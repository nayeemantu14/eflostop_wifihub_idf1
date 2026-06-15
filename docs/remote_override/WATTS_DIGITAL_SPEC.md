# eFloStop II — Remote Water-Access Override (`override_enable`)
### Integration spec for the Watts Digital app + backend

**Status:** draft for implementation · **Hub firmware:** `gateway.fw ≥ 1.4.0` · **Schema:** unchanged
(`eflostop.cmd` / `eflostop.v2`, additive only) · **Audience:** app + backend engineers (no device-firmware
knowledge required).

Proposed SRS placement: **§4.4.3** (feature + APP-FR rows), **§4.10** (one notification row), **§5.2.5** (one
event-field note), **§5.3** (one command row), **§2.7** (additive-exception note), **§10/§11** (tests +
traceability).

---

## 1. What this is — and the key point: it is *redundant* with the physical valve button

eFloStop already lets a user override an auto-close **at the valve**: pressing the physical button on the
valve opens it during an active leak and starts a **24-hour water-access override window** (auto-close is
paused; leaks are still detected and reported). That escape hatch only works if the user can physically reach
the valve — often impossible (crawl spaces, locked utility rooms).

**`override_enable` is the exact same action, reachable from the app.** It is a second, redundant trigger for
one behaviour that already exists in two places:

| | Physical valve button | App command `override_enable` |
|---|---|---|
| Who/where | User standing at the valve | User anywhere, via the app |
| End-state | Valve open · interlock cleared · 24 h window started · leaks still reported | **Identical** |
| Telemetry | `water_access_override_enabled` `trigger:"button"` | `water_access_override_enabled` `trigger:"c2d_command"` |
| Ends by | 24 h expiry **or** `override_cancel` | **Identical** |

The hub runs **one** override path; the only difference between the two triggers is the `trigger` field in
the resulting event. Nothing else about the override changes — the existing banner, countdown, and Cancel
Override (APP-FR-040..042) are reused unchanged.

**User story:** *"My valve auto-closed for a leak I already know about, the valve is in a crawl space I can't
get to, and I need water now. I tap 'Open with 24-hour Override' in the app and water comes back on, with
auto-close paused for 24 hours — exactly as if I'd walked over and pressed the button."*

> This deliberately pauses automatic flood protection during a known leak. It is a high-consequence action;
> §6 (App UX) requires a double confirmation with explicit liability copy.

---

## 2. The command — `override_enable`

Standard `eflostop.cmd` envelope, **no payload**, correlation `id` required (so you get a `cmd_ack`):

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "ovr-en-7f3a",
  "cmd": "override_enable"
}
```

**Effect (on success):** the hub starts the 24 h window, clears the valve's leak interlock, and opens the
valve. **Idempotent:** if a window is already active, the command refreshes it to a fresh 24 h and re-emits the
event (useful as a "extend / re-confirm" action). The 24 h window starts **only on successful execution**,
never on mere receipt.

New §5.3 command-table row:

| Command | Payload | Description | Error scenarios |
|---|---|---|---|
| `override_enable` | (none) | Open the valve during an active leak and start the 24 h water-access override window (remote equivalent of the physical valve button). | no active leak; water at the valve; valve unreachable; no valve set up |

---

## 3. `cmd_ack` — success and errors

`cmd_ack` uses the **existing** §5.2.7 schema unchanged — `error.code` is the command name, `error.detail` is
a human-readable string. **There is no new field.** `status:"ok"` means *the hub accepted and executed the
command*; the valve physically opening is confirmed by the subsequent `valve_state_changed` event / next
snapshot (see §5), exactly like `valve_open`.

**Success:**
```json
{ "schema":"eflostop.v2","ts":1770589510,
  "gateway":{"id":"GW-34B7DA6AAD54","fw":"1.4.0","uptime_s":1080},
  "type":"event",
  "data":{ "event":"cmd_ack","id":"ovr-en-7f3a","cmd":"override_enable","status":"ok" } }
```

**Error:**
```json
{ "schema":"eflostop.v2","ts":1770589511,
  "gateway":{"id":"GW-34B7DA6AAD54","fw":"1.4.0","uptime_s":1081},
  "type":"event",
  "data":{ "event":"cmd_ack","id":"ovr-en-7f3a","cmd":"override_enable","status":"error",
           "error":{ "code":"override_enable",
                     "detail":"Water detected at the valve. It can't be opened remotely until the valve area is dry." } } }
```

**Frozen `error.detail` strings** (render verbatim per APP-FR-054; these are contract — do not reword on the
backend). The app may key a richer affordance off the exact string (optional):

| Why it failed | `error.detail` (exact) | Suggested app handling |
|---|---|---|
| No active leak / interlock to override (pre-emptive use) | `No active leak to override. Use the normal Open Valve control.` | Toast; leave UI unchanged |
| Valve's own probe detects water at the valve (cannot be overridden — safety floor) | `Water detected at the valve. It can't be opened remotely until the valve area is dry.` | Show prominently; optional "Contact support" |
| Valve not reachable (after the hub's ~10 s reconnect attempt) | `The valve isn't responding. Check its power and connection, then try again.` | Toast; offer retry |
| Hub has no valve configured | `No valve is set up for this hub.` | Toast |
| Internal hub error | `Something went wrong applying the override. Your water state is unchanged. Try again.` | Toast; offer retry |

Standard timeouts apply: wait up to 20 s for the `cmd_ack`. If it times out, **reconcile against the next
snapshot/event before showing failure** — the override may have succeeded even if the ack was lost.

---

## 4. Telemetry event — `water_access_override_enabled`

When the window starts (by **either** trigger), the hub emits this `type:"event"`. It is the signal to show
the override banner. (Additive note for §5.2.5: this event now carries the fields below; per the forward-compat
rule, ignore any field you don't use.)

```json
{ "schema":"eflostop.v2","ts":1770589503,
  "gateway":{"id":"GW-34B7DA6AAD54","fw":"1.4.0","uptime_s":1073},
  "type":"event",
  "data":{
    "event":"water_access_override_enabled",
    "trigger":"c2d_command",
    "expires_ts":1770675903,
    "remaining_s":86400
  } }
```

| Field | Type | Meaning |
|---|---|---|
| `trigger` | string | `"c2d_command"` (app) or `"button"` (physical valve button). Use this to label history + decide notification copy. |
| `expires_ts` | int | Absolute UTC epoch seconds when the window ends. Use for absolute display / scheduling. |
| `remaining_s` | int | Seconds remaining at emit time (full 24 h = 86400 on start/refresh). **Drive the countdown off this (and snapshot `override_remaining_s`)** — it is clock-skew-proof; do **not** derive the countdown from `expires_ts` vs the phone clock. |

The window ends via the existing events (unchanged): `water_access_override_expired` (24 h elapsed) or
`auto_close_reenabled` (`override_cancel`). While the window is active, leaks continue to be reported and the
hub emits `auto_close_blocked_override` (rate-limited; for your event log/timeline — **do not** push-notify per
occurrence).

---

## 5. Snapshot — no changes needed

The snapshot already carries everything; a remotely-started window appears automatically:

| Field | Behaviour |
|---|---|
| `data.override_active` | `true` while the window is active (regardless of trigger) |
| `data.override_remaining_s` | seconds left (present only while active) — countdown source of truth |
| `data.valve.state` / `valve.connected` / `valve.rmleak` / `valve.leak_state` | the valve truth; after a successful override, `valve.state` becomes `open` and `valve.rmleak` becomes `false` |

**Two UI elements, two sources:** drive the **override banner** off the `water_access_override_enabled` event
(immediate); drive **"water is ON"** off `valve_state_changed{open}` / snapshot — never off the ack alone.

---

## 6. End-to-end flow (mirrors SRS §6.5 step-table format)

| # | Actor | Action |
|---|---|---|
| 1 | App | Valve shows leak-locked (`valve.rmleak=true`). User taps **"Open with 24 h Override"**. |
| 2 | App | Double confirmation with liability copy (§7). |
| 3 | App | Generates correlation `id`, shows spinner, sends `override_enable` to the backend. |
| 4 | Backend | Logs user + action + timestamp; sends the C2D command to the hub. |
| 5 | Hub | Validates (active leak? water at valve? valve reachable?), starts the 24 h window, clears the interlock, opens the valve. |
| 6 | Hub | Publishes `cmd_ack` (`ok`), then `water_access_override_enabled{trigger:"c2d_command"}`, then `valve_state_changed{valve_state:"open","rmleak":false}`. |
| 7 | App | On `cmd_ack ok` → show the override banner (from the event) + countdown (from `remaining_s`); valve card stays *pending* until `valve_state_changed{open}`. |
| 8 | App | Window persists for 24 h (survives hub reboot). Ends at expiry or via **Cancel Override** (`override_cancel`, unchanged). |

If `cmd_ack` is an error, show the verbatim `error.detail` (§3) and leave UI state unchanged.

---

## 7. App UX requirements (lean — proposed §4.4.3)

| Req ID | Pri | Requirement | Acceptance |
|---|---|---|---|
| **APP-FR-043** | P0 | When `valve.rmleak=true`, in addition to the existing **disabled** "Open Valve" button (APP-FR-057, unchanged), the app shall present a distinct **"Open with 24 h Override"** action. | Action visible only while `valve.rmleak=true`; sends `override_enable`. |
| **APP-FR-044** | P0 | The action shall require a **double confirmation** that names the still-active leak and warns the user, before sending. Step 1: *"A leak is still detected{ at &lt;location&gt;}. Turning water back on now can let it keep flooding. Auto-shutoff will be paused for 24 hours."* (primary button = **Keep water off**). Step 2: explicit acknowledgement *"You are turning water on while a leak is still detected. For 24 hours the system will not automatically shut off for sensor leaks. You can cancel anytime."* (confirm = **Open with 24 h Override**). | Two distinct dialogs; user can cancel at either; not auto-dismissed. |
| **APP-FR-045** | P0 | On success the app shall reuse the **existing** override banner, countdown, and Cancel-Override (APP-FR-040..042) unchanged; the banner is driven by the `water_access_override_enabled` event and the countdown by `remaining_s` / `override_remaining_s`. | Banner appears on event; countdown matches snapshot; Cancel sends `override_cancel`. |

The five `cmd_ack` `error.detail` strings (§3) are rendered per existing **APP-FR-054** (no new requirement).

---

## 8. Backend responsibilities

1. **Capability detection (primary signal = `gateway.fw`).** Treat the command as supported when the hub's
   latest envelope reports **`gateway.fw ≥ 1.4.0`**. Do **not** offer "Open with Override" for older hubs;
   show "update hub firmware" instead. An older hub that does receive the command replies with a `cmd_ack`
   error (it does not recognise the command) — but use `gateway.fw` as the gate, not the error, and **never**
   treat a bare ack-timeout as "unsupported" (timeout = possibly offline). *Do not auto-fall-back to
   `leak_reset → valve_open` on older hubs without an explicit extra user confirmation: that path re-closes
   the valve while the sensor is still wet.*
2. **Audit (existing APP-NF-003).** Log user + action + timestamp for every `override_enable`, as for any
   command. (Richer audit — leak snapshot, confirmation-copy version, household notification — is **not** in
   this scope; see "Deferred" below.)
3. **Notification (one rule, proposed §4.10):**

| Req ID | Pri | Requirement | Acceptance |
|---|---|---|---|
| **APP-FR-116** | P0 | On a `water_access_override_enabled` event, the backend shall push-notify the account. Copy by `trigger`: `c2d_command` → *"Water override enabled — auto-shutoff is paused for 24 hours."*; `button` → *"Someone opened the valve at the device during a leak — auto-shutoff is paused for 24 hours."* | Push within 10 s of the event; correct copy per trigger. |

---

## 9. Tests (proposed §10)

| Test ID | Steps | Expected |
|---|---|---|
| **TC-012** | With a sensor leaking and the valve leak-locked, send `override_enable`. | `cmd_ack ok`; `water_access_override_enabled{trigger:"c2d_command",remaining_s:86400}`; `valve_state_changed{valve_state:"open","rmleak":false}`; snapshot `override_active:true`, `valve.state:"open"`. Banner + countdown shown. |
| **TC-N07** | Send `override_enable` with **no** active leak; and (separately) with water present at the valve. | `cmd_ack error` with the exact §3 detail (`No active leak to override…` / `Water detected at the valve…`); UI unchanged. |

§11 traceability row: **Remote Override | APP-FR-043..045, APP-FR-116 | `override_enable` | `water_access_override_enabled` (+`trigger`), `cmd_ack`, snapshot(`override_active`) | TC-012, TC-N07**.

---

## 10. Out of scope for this iteration (deferred)
Recorded so expectations are clear; none of these block the feature and none change the firmware contract:
household-wide / non-suppressible notifications, cancel-and-expiry push branches, a 12 h mid-window reminder,
an enhanced audit record (leak snapshot + confirmation-copy versioning), and richer per-error dialogs. These
can be added later without firmware changes.

*This document contains no references to any unreleased hub capability.*
