# eFloStop II — Full Leak / Override / Valve-Close Flow (all cases)

> App-team reference. Every case below was extracted from the hub firmware and **adversarially
> verified against the source** (25 cases, all confirmed). Devices in scope: **BLE leak sensors** and
> the **valve's own flood probe**. Event names are the literal `data.event` strings in `eflostop.v2`.

## The model (read this first)

Four pieces of state drive everything:

| State | What it is |
|---|---|
| **Incident latch** | Hub flag "a leak incident is in progress" (persisted across reboot). |
| **RMLEAK interlock** | A latch *on the valve*. While set, the valve **refuses to open** (even an `valve_open` command). Set by auto-close; cleared by leak-reset / the 30 s auto-clear / a successful override. |
| **Override window** | The 24 h "water access" state. While active, auto-close is **blocked** (leaks still reported). |
| **Active-leak count** | How many sources are currently wet *and* eligible to shut off. Drives the 30 s auto-clear and the leak-reset guard. |

Two **independent** things happen on every leak: it is always **reported** (`leak_detected` /
`valve_flood_detected`), and *separately* it may **shut off** the valve (only when eligible). Turning
the valve off and reporting a leak are different code paths.

### Lifecycle at a glance
```
        leak (eligible)            user/app or button            cancel / 24h expiry
  OPEN ───────────────► CLOSED+RMLEAK ──────────► OVERRIDE (open, ──────────────► (re-evaluate:
   ▲       auto_close      (locked)   override     auto-close paused)   end window    leak? → CLOSE
   │                                                                                  no leak? → stay)
   │   leak_reset (dry) / 30s auto-clear (RMLEAK lifted, valve stays closed) ─────────────┘
   └── valve_open (only allowed once RMLEAK is clear)
```

---

## A. Leak onset (a sensor goes wet)

| # | Scenario | Valve | Events |
|---|---|---|---|
| **A1** | Eligible (provisioned + auto-close ON + source in trigger mask + no override) | **Closes**, RMLEAK set | `leak_detected`/`valve_flood_detected`, `auto_close`, `valve_state_changed{closed,rmleak:true}` |
| **A2** | Override window **active** | **Stays open** (auto-close blocked) | `leak_detected`…, `auto_close_blocked_override` (max once/60 s) |
| **A3** | Auto-close **OFF** *or* source **not in trigger mask** | **Stays open** | `leak_detected`/`valve_flood_detected` **only** (rules engine emits nothing) |
| **A4** | Eligible but valve **already closed + RMLEAK already set** | Unchanged (idempotent) | `leak_detected`… only (no second `auto_close`) |

*Note (A2/A4): the incident is latched even when the close is blocked/redundant, so the valve can close the moment the override ends.*

---

## B. Starting the override (open during a leak, pause auto-close 24 h)

| # | Scenario | Valve | Events |
|---|---|---|---|
| **B1** | App `override_enable` **succeeds** (clears interlock → opens → starts 24 h) | **Opens**, RMLEAK cleared | `cmd_ack{ok}`, `water_access_override_enabled{trigger:"c2d_command"}`, `valve_state_changed{open,rmleak:false}` |
| **B2** | App `override_enable` **rejected** (one of 4 preconditions fails) | Unchanged | `cmd_ack{error}` with verbatim detail — *no leak* / *water at valve* / *valve unreachable* / *no valve set up* |
| **B3** | **Physical valve button** override detected live (hub online) | Unchanged (already opened by user) | `water_access_override_enabled{trigger:"button"}` |

*B1 preconditions: valve provisioned, valve reachable (≤10 s reconnect), something to override (incident/RMLEAK/window), and the valve's own flood probe is dry.*

---

## C. Another leak *during* an active override

| # | Scenario | Valve | Events |
|---|---|---|---|
| **C1** | Any further leak while window active | **Stays open** (blocked) | `leak_detected`/`valve_flood_detected` (per change), `auto_close_blocked_override` (rate-limited, ≤1/60 s) |

---

## D. Ending the override — *this is the family your sketch is in*

> Your sketch = **D1**: leak active → user cancels override → valve turns off.

| # | Scenario | Valve | Events |
|---|---|---|---|
| **D1** ★ | `override_cancel`, **leak still active**, auto-close **ON**, valve connected | **Closes** (RMLEAK re-asserted before close) | `auto_close_reenabled`, `valve_state_changed{closed,rmleak:true}`, `cmd_ack{ok}` |
| **D2** | Same as D1 but valve **disconnected** | **Closes on reconnect** (deferred) | `auto_close_reenabled`, then on reconnect `auto_close{source_type:"reconnect"}` + `valve_state_changed{closed}` |
| **D3** | `override_cancel`, leak active, auto-close **OFF** (monitor-only) | **Stays open** (nothing forces a close) | `auto_close_reenabled`, `cmd_ack{ok}` |
| **D4** | `override_cancel`, **no active leak** (leak already dried) | Unchanged (residual incident wiped) | `auto_close_reenabled`, `cmd_ack{ok}` |
| **D5** | `override_cancel`, **no window active** | Unchanged (no-op success) | `cmd_ack{ok}` only |
| **D6** | **24 h expiry**, leak still active, auto-close **ON** | **Closes** immediately | `water_access_override_expired{auto_close_resumed:true}`, `valve_state_changed{closed}` |
| **D7** | **24 h expiry**, leak active, auto-close **OFF** | **Stays open** | `water_access_override_expired{auto_close_resumed:true}` (informational; no close) |
| **D8** | **24 h expiry**, no active leak | Unchanged | `water_access_override_expired{auto_close_resumed:false}` |

**Key takeaway for the app:** ending an override (cancel **or** expiry) **re-evaluates the leak**. The valve
turns off *only if* a leak is still active **and** auto-close is enabled (D1/D2/D6). If the leak already
cleared, or auto-close is off, the valve stays as it is (D3/D4/D7/D8).

---

## E. Recovery (leak fixed / cleared)

| # | Scenario | Valve | Events |
|---|---|---|---|
| **E1** | All sources dry for **30 s** while an incident is active | **Stays closed**, RMLEAK lifted (does *not* reopen) | `leak_cleared`/`valve_flood_cleared`, then `rmleak_auto_cleared{clear_after_seconds:30}` |
| **E2** | `leak_reset` while a leak is **still wet** → **refused** (guard) | Unchanged (interlock held) | `cmd_ack{error: "A leak is still active. Fix the leak first, or use override to open the valve during a leak."}` |
| **E3** | `leak_reset` when **all dry** (incident/RMLEAK/window to clear) | Unchanged (interlock cleared, **valve not opened**) | `rmleak_cleared` (`override_cancelled:true` if a window was active), `cmd_ack{ok}` |

*After E1 or E3 the interlock is clear, so the user can then `valve_open` to restore water (two-step by design).*

---

## F. Manual valve control (distinct from auto-close)

| # | Scenario | Valve | Events |
|---|---|---|---|
| **F1** | `valve_close` / `valve_set_state:closed` (manual) | **Closes** — but **RMLEAK NOT set, no incident latched** | `valve_state_changed{closed, rmleak:false}`, `cmd_ack{ok}` |
| **F2** | `valve_open` / `valve_set_state:open` | **Opens if RMLEAK clear**; if RMLEAK set the **valve refuses** (interlock is enforced *valve-side*, the hub does not gate it) | `valve_state_changed` (optimistic, then a corrective notify to the true state if refused), `cmd_ack{ok}` |

*F1 is the important contrast: a manual close is **not** the protected leak flow — it has no interlock and no 30 s auto-clear. Only auto-close (A1/D1/D6/reconnect) sets RMLEAK.*

---

## G. Valve reconnect reconciliation (after BLE drop / reboot)

| # | Scenario | Valve | Events |
|---|---|---|---|
| **G1** | Active leak present at reconnect (no override) | **Closes** (single anti-spam evaluation) | `auto_close{source_type:"reconnect"}`, `valve_state_changed{closed}` |
| **G2** | Persisted incident + valve reports **open & RMLEAK clear** → hub infers the user pressed the valve button while the hub was offline | Unchanged (honors physical override) | `water_access_override_enabled{trigger:"button"}` |
| **G3** | RMLEAK state re-sync across reboot (valve lost RMLEAK → re-assert; hub lost incident but valve still RMLEAK → re-latch incident) | Unchanged | **none** (silent reconciliation) |

---

## H. Absolute floor — valve's own flood probe

| # | Scenario | Valve | Events |
|---|---|---|---|
| **H1** | The valve is standing in water (its own flood probe wet) | **No override (remote or physical) can keep it open**; it also drives normal auto-close like A1/A2 | `valve_flood_detected`; an attempted `override_enable` returns `cmd_ack{error:"Water detected at the valve…"}` |

---

## Quick reference — "when does the valve turn OFF?"
- A leak that is **eligible** (provisioned + auto-close ON + in trigger mask + no override) → **off** (A1).
- **Override ends** (cancel or 24 h expiry) **and** a leak is still active **and** auto-close ON → **off** (D1/D2/D6).
- **Valve reconnects** with a leak still active → **off** (G1).
- A user/app **manual close** → **off**, but *without* the interlock (F1).
- Valve's **own flood probe** wet → **off** and cannot be overridden open (H1).

## Quick reference — "when does the valve turn ON / become openable?"
- **Successful `override_enable`** during a leak → opens immediately (B1).
- **`leak_reset` when dry** or the **30 s auto-clear** → lifts the RMLEAK interlock (valve stays closed), then a **`valve_open`** opens it (E1/E3 → F2).
- A physical button press at the valve → opens it directly (B3/G2).

*Event-name glossary: `auto_close` (hub shut the valve), `auto_close_blocked_override` (leak ignored during
window), `water_access_override_enabled`/`_expired`, `auto_close_reenabled` (override cancelled),
`rmleak_cleared` (manual reset), `rmleak_auto_cleared` (30 s auto), `valve_state_changed`, `cmd_ack`,
`leak_detected`/`leak_cleared`, `valve_flood_detected`/`valve_flood_cleared`.*
