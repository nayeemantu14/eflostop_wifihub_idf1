# FEATURE TRACKER — Event-coupled snapshots + unified snapshot scheduler (FW 1.5.0)

Branch `feature/event-snapshots`. Target **1.5.0** (`PROJECT_VER`, CMakeLists.txt only).

## Goal
The app uses a two-channel model — **events** feed an append-only log; **snapshots** are the single
source of truth that refreshes UI. Today an event fires but no snapshot follows, so a user opening the
app before the next 5-min periodic sees stale state. Add an **event-coupled snapshot** + a **unified
scheduler** (heartbeat suppression + burst coalescing), and make snapshots **complete** (fold in
`auto_close_enabled`/`trigger_mask` and an absolute override expiry).

## Defects fixed
- **#1** heartbeat timer never reset by commission/boot/fast snapshots → redundant snapshots.
- **#2** `auto_close_enabled`/`trigger_mask` absent from the snapshot (only lifecycle/twin).

## Design provenance
Understand workflow (file:line map) → judge-panel Design (3 architectures → 4 dimension-judges →
synthesis) → Adversarial Review (5 skeptics → verdict **GO** + 10 corrections, all folded in).
Plan: `~/.claude/plans/hidden-toasting-storm.md`.

## Locked decisions
| Knob | Value |
|---|---|
| MIN_INTERVAL (event snapshot cap) | 5000 ms (≤12/min) |
| HIGH / LOW coalescing window | 300 ms / 2000 ms |
| Offline floor / retry floor | 30000 ms / 5000 ms |
| `data.reason` field | added |
| Reconnect wake | immediate (snap_q enqueue from MQTT_EVENT_CONNECTED) |
| `valve_state_changed` | delta-gated |
| Heartbeat default | 300 s (Twin-tunable via `snapshot_interval_s`) |

## Implementation status
| Step | Scope | Status |
|---|---|---|
| C0-1 | `publish_json`/`publish_snapshot` → bool (reached-esp-mqtt, msg_id≥0) | ✅ code |
| C0-2 | Twin interval → atomic int32 seconds; 5-min timer demoted to liveness backstop | ✅ code |
| C0-3 | `telemetry_v2_is_connected()` + `telemetry_v2_wake_snapshot()` + interval getter | ✅ code |
| C0-4 | `rules_engine_get_override_status()` (one mutex hold, fixes expires_ts TOCTOU) | ✅ code |
| C1 | snapshot adds `data.rules`, `data.expires_ts`, `data.reason` (additive) | ✅ code |
| C2 | scheduler core: monotonic deadline, `snap_request`/`snap_rearm`, evt_wait rewrite, single flush, unprovisioned reset | ✅ code |
| C3 | event coupling at LoRa/valve/BLE/rules sites + `valve_state_changed` delta-gate | ✅ code |
| C4 | immediate reconnect wake | ✅ code |
| C5 | `PROJECT_VER` → 1.5.0 + docs | ✅ code |
| — | **Adversarial review of the actual diff** | ⏳ pending |
| — | **Bench test (E0–E12 in TEST_PLAN.md)** | ⏳ pending hardware |

## Files touched
- `main/telemetry/telemetry_v2.c` / `.h`
- `main/rules_engine/rules_engine.c` / `.h`
- `main/iothub/app_iothub.c`
- `CMakeLists.txt`
- `docs/event_snapshots/{TEST_PLAN,FEATURE_TRACKER}.md`

## Schema note for the app team
All additions are **additive**; schema stays `eflostop.v2` (no version bump). New snapshot fields:
`data.reason` (string), `data.rules{auto_close_enabled,trigger_mask}`, `data.expires_ts`
(absolute epoch — **same field name as the override event**). Legacy `override_active` /
`override_remaining_s` unchanged. Future per-sensor `auto_close` + `last_seen_ts` will be purely additive.

## Merge / push
Merge to `master` with `--no-ff` only after a confirmed bench pass. **Do not push** until explicitly told.
