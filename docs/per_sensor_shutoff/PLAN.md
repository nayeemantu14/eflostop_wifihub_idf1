# Plan — Per-Individual-Sensor Auto-Close (Shutoff vs Monitor-only)

> **STATUS: DEFERRED / NOT IMPLEMENTED.** Design reviewed (3-agent adversarial workflow) and plan
> approved 2026-06-15. No firmware written yet. Pick this up in a future session — start from this
> file + [DECISIONS.md](DECISIONS.md). Source of truth for the approved plan; a copy also lives at
> `~/.claude/plans/agile-percolating-storm.md`.

## Context

Today the hub's automatic valve shutoff on a leak is controlled **per sensor TYPE**: a single
`trigger_mask` in the rules config turns auto-close on/off for *all* BLE leak sensors (bit 0), all
LoRa sensors (bit 1), and valve flood (bit 2). A user cannot say "this one sensor should only *alert*
me, not shut my water off" — e.g. a sensor near a known slow drip, a condensate tray, or an outdoor
spigot. They must either accept nuisance closures from that sensor or disable shutoff for the *entire*
type, losing protection on every other sensor of that type.

This feature adds **per-individual-sensor** control: each BLE leak sensor (and internally each LoRa
sensor) can be set to **Shutoff** (default — triggers auto-close) or **Monitor-only** (still detects
and *reports* leaks to the app/cloud, but never closes the valve). It is an **opt-out** model: every
sensor defaults to Shutoff so behavior is unchanged until a user deliberately changes one.

The control rides the **existing** `sensor_meta` C2D command and the **existing** snapshot per-sensor
object — both additive — so no new command, no contract break, and the app's existing per-sensor
cards are the natural home for the toggle.

### Confidentiality
LoRa remains **CONFIDENTIAL**. The firmware implements both BLE and LoRa identically (shared rules-engine
path), and the snapshot already exposes `lora_sensors[]` internally — so `auto_close` is added to both
arrays. **The Watts Digital handoff doc documents BLE only**, exactly as done for the override feature.

---

## Design overview

**Layered eligibility (logical AND).** A leak from a source triggers auto-close only if ALL hold:
```
eligible = provisioned
        && rules.auto_close_enabled          // master switch (existing)
        && (rules.trigger_mask & type_bit)   // per-TYPE switch (existing)
        && per_sensor_flag                   // per-SENSOR switch (NEW)
```
- `per_sensor_flag` = `sensor_meta_get_auto_close(type, id)`, which **defaults `true`** when the sensor
  has no metadata entry → untagged/legacy fleets keep full protection (backward compatible).
- **Valve flood** (`LEAK_SOURCE_VALVE_FLOOD`, source_id `"valve"`) is an absolute floor: `per_sensor_flag`
  is hard-wired `true` and the meta table is **never** queried for it.

**Default ON / opt-out** → zero behavior change until a user opts a sensor out.

**Reporting is independent of shutoff.** Leak `leak_detected`/`leak_cleared` events and snapshot
`leak_state` are published by a *separate* code path
([app_iothub.c:1177-1184](../../main/iothub/app_iothub.c#L1177), telemetry caches) that never consults
the rules engine. A Monitor-only sensor reports leaks exactly as before; only the auto-close action is
suppressed. (Verified structurally by the design review.)

**Symmetric tracking rule (this is the crux).** In `rules_engine_evaluate_leak`, compute `eligible`
once up front, then:
```
if (leak_active && eligible)  track_leak_source(id, true);   // add
else                          track_leak_source(id, false);  // remove (idempotent)
// then, only for (leak_active && eligible): incident latch → override block → auto-close
// for everything else: return early (NO incident latch, NO auto-close)
```
This single rule:
1. **Fixes a confirmed real bug** (see Risks): today `track_leak_source` is called *unconditionally*
   before all gates, so Monitor-only / type-disabled wet sensors pollute `g_active_leak_count`,
   which (a) blocks the all-clear/auto-clear timer from ever arming and (b) causes spurious valve
   re-closes on override-cancel / override-expiry / valve-reconnect.
2. Gives the **immediate, symmetric toggle** behavior chosen: re-running `evaluate_leak` after a
   toggle adds (promote → close now) or removes (demote → release now) the source at once.

---

## Decisions locked

| # | Decision | Rationale |
|---|---|---|
| D1 | New field `uint8_t auto_close_enabled` appended to `sensor_meta_entry_t`; **default 1 (Shutoff)** | Opt-out model, backward compatible |
| D2 | Bump `CURRENT_META_VERSION` 1→2 with real v1→v2 NVS migration | Without it, the size-check at [sensor_meta.c:116](../../main/sensor_meta/sensor_meta.c#L116) **silently wipes all user labels/locations** on upgrade |
| D3 | **Promote mid-leak = close immediately** | On toggle, re-eval with cached leak state |
| D4 | **Demote mid-leak = release hold immediately** | Symmetric; toggle authoritative now. ⚠️ This deliberately lets a settings toggle clear RMLEAK during a live leak (accepted). Documented in DECISIONS + Watts doc behavior note. |
| D5 | Implement BLE + LoRa (shared code); snapshot exposes `auto_close` on both arrays; **Watts doc = BLE only** | LoRa confidential; matches override-feature handling |
| D6 | Additive only: reuse existing `sensor_meta` command + snapshot object; no new command | No contract break |
| D7 | Bump hub version 1.4.0 → **1.5.0** via `PROJECT_VER` in the top-level `CMakeLists.txt` (single source of truth; `gateway.fw`/twin read it at runtime) | Capability signal for the app/backend |

---

## File-by-file changes

### 1. `main/sensor_meta/sensor_meta.h`
- Add `uint8_t auto_close_enabled;` as the **last** member of `sensor_meta_entry_t` (52 → 53 bytes).
- Add prototype: `bool sensor_meta_get_auto_close(sensor_type_t type, const char *sensor_id);`
  (returns `true` when no entry exists — the backward-compat linchpin).
- Extend `sensor_meta_set` signature with an `int auto_close` param (`-1` = keep existing, `0`/`1` = set),
  mirroring the existing `location_code == -1 means keep` convention.
- (Optional) a small result struct so the command handler can report what changed (see #4); simplest is
  out-params on `sensor_meta_handle_command`.

### 2. `main/sensor_meta/sensor_meta.c` — the highest-risk file
- `#define CURRENT_META_VERSION 2`.
- `_Static_assert(sizeof(sensor_meta_entry_t) == 53, "v2 layout drift — add a migration branch");`
  near the top — forces any future struct change to add a migration instead of silently wiping data.
- **Create branch** (~L208): after the `memset(entry,0,...)` and field init, add
  `entry->auto_close_enabled = 1;` (the memset would otherwise default it to 0 = Monitor-only). **Mandatory.**
- `sensor_meta_set`: apply `auto_close` when `>= 0`.
- **`sensor_meta_get_auto_close`**: mutex-guarded find; return `entry->auto_close_enabled != 0`, or
  `true` when not found / not initialized.
- **Rewrite `load_table_from_nvs`** (replace L110-129) to branch on the stored `meta_ver`:
  - **Downgrade guard:** `if (ver > CURRENT_META_VERSION) → discard` (protects future rollbacks; current
    code only rejects `ver==0`).
  - **`ver == 1`:** parse with a frozen local `sensor_meta_entry_v1_t` (52-byte: `uint8 type; char
    id[18]; uint8 loc; char label[32]`), `_Static_assert(sizeof == 52)`. Use **exact** size check
    (`blob_size == 1 + count*52`, not `<`). Field-copy each record into the v2 RAM struct (NOT a bulk
    `memcpy` — strides differ), set `auto_close_enabled = 1` per migrated entry, then `save_table_to_nvs()`
    to re-persist as v2. Idempotent across power loss (single atomic `nvs_commit`; never split version+blob).
  - **`ver == 2`:** exact size check (`== 1 + count*53`), bulk `memcpy` as today.
  - else discard.
- `handle_command`: parse optional `"auto_close"` boolean from payload; pass through to `sensor_meta_set`;
  report (type, id, whether auto_close was present) to the caller for the immediate re-eval (#4).
- `sensor_meta_clear_all` / decommission: unchanged — already erases the namespace cleanly (verified safe).

### 3. `main/rules_engine/rules_engine.{c,h}`
- In `rules_engine_evaluate_leak`: implement the **symmetric tracking rule** above, with the eligibility
  check **moved ABOVE the incident latch** at ~L441. This is the single most important ordering
  requirement: a Monitor-only (`!eligible`) leak must `return` **without** latching `g_leak_incident_active`
  (which persists to NVS and would corrupt reconnect reconciliation and make `override_enable` spuriously
  applicable).
- Compute `per_sensor_flag`: `LEAK_SOURCE_BLE → sensor_meta_get_auto_close(BLE, id)`,
  `LEAK_SOURCE_LORA → sensor_meta_get_auto_close(LORA, id)`, `LEAK_SOURCE_VALVE_FLOOD → true` (hard
  branch; never query meta for `"valve"`). Compute `eligible` **once** (keeps the `g_mutex`→`s_mutex`
  nested critical section short; lock order is already exercised safely by
  `build_auto_close_telemetry` → `sensor_meta_find` under `g_mutex`).
- No change to `track_leak_source` itself — its add/remove and all-clear-timer arming are already correct;
  they just stop being fed polluting sources.

### 4. `main/iothub/app_iothub.c` — immediate bidirectional toggle (D3/D4)
- In the `C2D_CMD_SENSOR_META` dispatch branch (~L597): after a successful `sensor_meta_handle_command`,
  **if the command set `auto_close`**, look up the sensor's current cached leak state and re-run the
  rules evaluation:
  ```c
  bool wet = telemetry_v2_get_cached_leak_state(type, id);   // new getter, #5
  rules_engine_evaluate_leak(source_for_type(type), wet, id);
  ```
  - Promote while wet → eligible → `track add` → auto-close fires → valve closes now (D3).
  - Demote while wet → `track remove` → count may reach 0 → all-clear timer arms → 30s later RMLEAK +
    incident cleared (D4); valve stays physically closed (auto-clear never opens).
- Only triggered when `auto_close` was actually in the payload (pure label/location edits do nothing new).

### 5. `main/telemetry/telemetry_v2.{c,h}`
- Snapshot builder: add `cJSON_AddBoolToObject(s, "auto_close", sensor_meta_get_auto_close(type, dev_id))`
  next to `add_location_obj` in **both** the BLE loop (L491) and the LoRa loop (L437). The valve-flood
  object gets **no** such field.
- Add `bool telemetry_v2_get_cached_leak_state(sensor_type_t type, const char *sensor_id);` — read-only
  lookup over `s_ble_cache` / `s_lora_cache` (returns `false` if not found). Used by #4.
- Bump `set(PROJECT_VER "1.5.0")` in the top-level `CMakeLists.txt` (D7) — the hub version's single source of truth; `gateway.fw` and twin `fw_version` derive from it at runtime via `telemetry_v2_fw_version()`.

### 6. Docs (this dir `docs/per_sensor_shutoff/`)
- `WATTS_DIGITAL_SPEC.md` — **BLE only, LoRa-clean** (write during implementation): the additive
  `auto_close` field in the `sensor_meta` command payload, the additive `ble_leak_sensors[].auto_close`
  snapshot field, default-ON semantics, redundancy-with-monitoring framing, and the **mid-leak toggle
  behavior note** (immediate both ways; demote-while-wet clears protection — surface to product/app so
  the UI can confirm-dialog it).
- `DECISIONS.md` — records D1–D7 + the design-review findings (written now).
- `TEST_PLAN.md` — the matrix below (write during implementation, or lift from here).

---

## Backward compatibility & rollback
- **Upgrade (v1→v2):** migration preserves all labels/locations and defaults every sensor to Shutoff →
  identical behavior to before. Untagged sensors (no meta entry) also default Shutoff via the getter.
- **Rollback (v2→already-shipped v1):** the *old* firmware lacks the downgrade guard, so rolling back to
  a build older than this one will misread the 53-byte blob (garbled labels until the next `sensor_meta`
  write self-heals the version). Mitigation: ship the `ver > CURRENT` guard **now** so all *future*
  rollbacks are safe; treat "no rollback below this build" as a one-time release constraint.
- **Decommission:** `sensor_meta_clear_all` already wipes flags with labels/locations.

---

## Risks & mitigations (from the adversarial design review)
1. **NVS migration silently wipes metadata** (confirmed) → ver-branched loader + frozen v1 struct +
   exact size check + `_Static_assert`. **Bench-tested by M7.**
2. **Incident-latch ordering** (top risk) → eligibility check moved above the latch; a Monitor-only leak
   must never latch an incident or persist one to NVS. **Detected by M2 + M10.**
3. **`g_active_leak_count` pollution** (confirmed pre-existing bug, also affects the type-disabled path)
   → symmetric add-if-eligible-else-remove rule. **Detected by M4 + M9.** *Release note:* deployments
   that previously had a trigger type disabled will now correctly auto-clear instead of staying latched.
4. **memset default trap** in BOTH the create branch and the migration loop → explicit `=1` in both.
5. **D4 protection bypass** (accepted): a Shutoff→Monitor toggle on a wet sensor clears RMLEAK during a
   live leak after the 30s timer. Documented; app should confirm-dialog the demote.
6. **Lock order** `g_mutex → s_mutex` safe today; keep `sensor_meta` free of any rules-engine callback;
   compute `eligible` once.

---

## Verification — bench test matrix
Assistant cannot flash/run hardware; **user** builds, flashes, and runs. Tooling: `az iot device
c2d-message send` (v2 envelope `{"schema":"eflostop.cmd","ver":1,"id":..,"cmd":"sensor_meta","payload":
{"sensor_type":"ble","sensor_id":"<MAC>","auto_close":false}}`) + `az iot hub monitor-events` + serial.
Two BLE sensors: A = Shutoff, B = Monitor-only.

| ID | Test | Pass criteria |
|---|---|---|
| M0 | Field presence | Every `ble_leak_sensors[]`/`lora_sensors[]` object carries `auto_close`; default `true`; valve-flood has none; `gateway.fw` = `1.5.0` |
| M1 | Set B → monitor-only | `cmd_ack ok`; next snapshot B `auto_close:false`, A unchanged; labels/locations intact |
| M2 | **Monitor-only wet → reports, no shutoff** | B wet → `leak_detected` fires; **NO** `auto_close`, **NO** `valve_state_changed`, **NO** "LEAK INCIDENT latched" on serial; valve stays open, `rmleak:false` |
| M3 | Shutoff regression | A wet → `leak_detected` → `auto_close` → `valve_state_changed{closed,rmleak:true}`; dry+30s → `rmleak_auto_cleared` |
| M4 | **Mixed; shutoff dries first** | A+B wet → close (by A). Dry A only, B still wet → `rmleak_auto_cleared` fires after 30s (proves B not in count) |
| M5 | Re-enable (dry) → next leak shuts off | B set Shutoff while dry; wet B → `auto_close` fires for B |
| M6 | No-meta default ON | Sensor with no entry wets → normal `auto_close` (getter returns true) |
| M7 | **NVS v1→v2 migration** | Flash OLD fw, set label+location on A; flash NEW fw **without erase**; serial shows v1→v2 migrate; snapshot: A keeps label+location AND `auto_close:true`; reboot twice, diff snapshots (idempotent) |
| M8 | **Promote mid-leak = close now (D3)** | B monitor-only + wet (reporting, valve open); set B Shutoff → valve **closes immediately** (`auto_close` + `valve_state_changed`) |
| M9 | **Demote mid-leak = release now (D4)** | A wet → valve closed by A; set A Monitor-only while still wet → within 30s `rmleak_auto_cleared`, `rmleak:false`, valve stays closed |
| M10 | leak_reset / override interaction | With only B (monitor-only) wet: `leak_reset` → `ok` (not refused — B not in count); `override_enable` → error `No active leak...` (B latched nothing) |
| M11 | Decommission wipes flags | `decommission_all` → after re-provision all sensors default `auto_close:true` |

Acceptance = M2, M4, M7, M8, M9 pass (core: report-vs-shutoff separation, count isolation, migration,
both immediate toggles) with no regression in M3/M6.

---

## Out of scope
- No valve or sensor firmware change (hub-only).
- No SRS rewrite (lean) — a short Watts doc + DECISIONS + TEST_PLAN, mirroring the override feature.
- No new C2D command and no new envelope.
