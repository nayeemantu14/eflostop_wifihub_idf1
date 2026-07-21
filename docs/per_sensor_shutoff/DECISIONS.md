# DECISIONS — Per-Individual-Sensor Auto-Close (Shutoff vs Monitor-only)

> Companion to [PLAN.md](PLAN.md). Records the locked decisions and the verified findings from the
> 3-agent adversarial design review (run 2026-06-15) so a future session can implement without re-deriving
> the analysis. **Feature is DEFERRED — not implemented.**

## Locked decisions

- **D1 — New field, default ON.** Append `uint8_t auto_close_enabled` to `sensor_meta_entry_t`,
  default `1` (Shutoff). Opt-out model: a sensor only stops triggering shutoff when a user explicitly
  sets it to Monitor-only. Monitoring/reporting always continues regardless (user requirement).
- **D2 — NVS schema v1→v2 with migration.** Bump `CURRENT_META_VERSION` 1→2. Migration is **mandatory**
  (see Finding 1) — without it all sensor labels/locations are wiped on upgrade.
- **D3 — Promote mid-leak = close immediately.** Toggling an already-wet sensor Monitor→Shutoff closes
  the valve at once (re-evaluate with cached leak state in the command handler).
- **D4 — Demote mid-leak = release hold immediately.** Toggling an already-wet sensor Shutoff→Monitor
  drops it from the active-leak count at once → 30 s auto-clear → RMLEAK + incident cleared (valve stays
  physically closed). ⚠️ **Accepted protection trade-off:** a settings toggle can clear RMLEAK during a
  live leak. The app should present a confirm-dialog on demote. Chosen deliberately for a consistent
  "toggle takes effect now" model in both directions.
- **D5 — BLE + LoRa in firmware; Watts doc BLE-only.** Shared rules-engine path handles both identically;
  snapshot already exposes `lora_sensors[]`, so `auto_close` is added to both arrays internally. LoRa is
  CONFIDENTIAL — it must not appear in any Watts Digital deliverable.
- **D6 — Additive only.** Reuse the existing `sensor_meta` C2D command (new optional `auto_close` payload
  field) and the existing snapshot per-sensor object (new `auto_close` field). No new command/envelope,
  no contract break.
- **D7 — FW version bump** 1.4.0 → 1.5.0 via `PROJECT_VER` in the top-level `CMakeLists.txt` (single source of truth since 2026-06-17; `gateway.fw`/twin `fw_version` read it at runtime through `telemetry_v2_fw_version()`). Capability signal for the app/backend.

## Command + contract facts (verified in source)
- `sensor_meta` dispatches via the v2 envelope at [app_iothub.c:597](../../main/iothub/app_iothub.c#L597)
  and **does get a cmd_ack** (dispatcher success/error tail). Payload reaches `sensor_meta_handle_command`
  as pure JSON ([app_iothub.c:600](../../main/iothub/app_iothub.c#L600)). A legacy `SENSOR_META:{...}` text
  form also exists ([c2d_commands.c:239](../../main/commands/c2d_commands.c#L239)) but has no correlation
  id → use the v2 envelope. **No envelope ambiguity for the contract.**
- Snapshot insertion points confirmed: BLE loop [telemetry_v2.c:491](../../main/telemetry/telemetry_v2.c#L491),
  LoRa loop [telemetry_v2.c:437](../../main/telemetry/telemetry_v2.c#L437). `add_location_obj`
  ([:115](../../main/telemetry/telemetry_v2.c#L115)) already does a `sensor_meta_find`.

## Design-review findings (carry into implementation)

### NVS migration (Agent A)
1. **(Required) Naive version bump = silent data loss.** Current loader guard `blob_size < expected`
   ([sensor_meta.c:116](../../main/sensor_meta/sensor_meta.c#L116)) computes `expected` from the *new*
   `sizeof` (53). A v1 blob (52-byte stride) is smaller → returns false → table discarded → first
   subsequent write overwrites it → **all labels/locations lost.** Migration mandatory.
2. **(High) Downgrade v2→shipped-v1 corrupts silently.** Old loader only rejects `ver==0`, reads 52-byte
   strides over 53-byte records. Add a `ver > CURRENT_META_VERSION` reject guard **now** so future
   downgrades are safe; rollback to an already-shipped v1 stays unsafe (one-time release constraint).
3. **(High) memset trap** in BOTH the create branch (~L208) and the migration loop — must explicitly set
   `auto_close_enabled = 1` in both, else new/migrated sensors silently become Monitor-only.
4. **(Medium) Tighten size check `<` → `==`** (exact) per chosen version; the count clamp at L111 masks
   corruption.
5. **(OK) Re-save crash safety is sound** — single atomic `nvs_commit` writes version+blob together; do
   NOT split into two commits. Migration is idempotent across power loss.
6. **(OK) `clear_all`/decommission safe** — erases version key + blob; no stale-version hazard.
7. Sizes: v1 = 52, v2 = 53, no padding (all byte members). Add `_Static_assert(sizeof==53)` and a v1
   `_Static_assert(==52)` inside the migration branch.
8. Parse v1 with a **frozen local struct + field-copy**, never a bulk `memcpy` of the new struct size.

### Rules engine (Agent B)
- **KEY BUG confirmed REAL.** `track_leak_source` is called *unconditionally* at
  [rules_engine.c:400](../../main/rules_engine/rules_engine.c#L400), before provisioned/master/trigger_mask
  gates. So Monitor-only / type-disabled wet sensors pollute `g_active_leak_count`, which (a) prevents the
  all-clear timer from ever arming (auto-clear never fires) and (b) causes spurious re-closes on
  override-cancel / override-expiry / valve-reconnect. The symmetric add-if-eligible-else-remove rule
  fixes this and **also silently fixes the same latent bug in the existing per-TYPE (trigger_mask) path**
  → release-note it.
- **Top risk — incident-latch ordering.** The eligibility check / early `return` MUST move **above** the
  incident latch (~L441, which persists to NVS via `incident_save_to_nvs`). A Monitor-only leak that
  latches an incident corrupts `rules_engine_on_valve_connected` reconciliation and makes `override_enable`
  spuriously applicable. Detected by tests M2 + M10.
- **G1 (flood):** hard-branch `LEAK_SOURCE_VALVE_FLOOD → per_sensor=true`; never call
  `sensor_meta_get_auto_close(..., "valve")`. Mirrors the existing flood guard at
  [rules_engine.c:338](../../main/rules_engine/rules_engine.c#L338).
- **G2 (default-true):** `sensor_meta_get_auto_close` returns `true` on missing entry — the backward-compat
  linchpin (untagged fleets keep protection).
- **Lock order** `g_mutex → s_mutex` is already exercised safely (`build_auto_close_telemetry` →
  `sensor_meta_find` under `g_mutex`). Keep `sensor_meta` free of any rules-engine callback; compute
  `eligible` once to keep the critical section short.
- **Leak reporting is structurally independent** of `evaluate_leak` — confirmed; Monitor-only sensors
  report `leak_detected`/`leak_cleared` and snapshot `leak_state` unchanged.
- State consistency for D4: auto-clear tick at [rules_engine.c:1074](../../main/rules_engine/rules_engine.c#L1074)
  clears both `g_leak_incident_active` (with NVS persist) and RMLEAK, and never opens the valve — so the
  demote-immediate path lands cleanly.

### Test matrix (Agent C)
- Full matrix M0–M11 captured in [PLAN.md](PLAN.md). Highest-value: M7 (migration), M2/M10 (incident-latch
  ordering), M4/M9 (count isolation), M8/M9 (immediate toggles).
- Telemetry size: adding `auto_close` to every sensor object grows the snapshot; verify a full fleet
  (up to 16 BLE + 16 LoRa) still fits the MQTT/offline buffer (M0).
