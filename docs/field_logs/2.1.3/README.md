# Hub FW 2.1.4 bug-fix kit

| File | What it is |
|---|---|
| `CLAUDE_CODE_PROMPT.md` | The prompt to paste into Claude Code in VS Code |
| `ANALYSIS.md` | Architecture diagrams, log evidence, per-bug root-cause hypotheses with grep anchors, and the latent-bug register |

## Setup (one-time, ~5 min)

1. Open the hub repo in VS Code at `master` @ `ae4d59a` (2.1.3), the build that produced the field logs.
2. Commit or stash any uncommitted work so Claude starts from a clean tree.
3. Copy these files into the hub repo:
   ```
   docs/field_logs/2.1.3/UART_logs.txt          ← your UART log
   docs/field_logs/2.1.3/IoT_hub_monitor.txt    ← your IoT Hub monitor log
   docs/field_logs/2.1.3/ANALYSIS.md            ← from this folder
   ```
4. Optional but recommended: clone the valve repo next to the hub
   (`../elfostop_ble_valve`), so Claude can re-confirm the battery thresholds itself.
   It is read-only for this job.
5. Make sure `idf.py build` works from the VS Code ESP-IDF terminal (v5.5.1,
   target `esp32s3`). Claude will run it.

## Run

0. Switch Claude Code to **Plan mode** (Shift+Tab) before pasting. That keeps Phases A–C read-only.
1. In the Claude Code panel: `/effort xhigh`.
   (I read "xcode high" in your request as the `xhigh` effort level.)
2. Paste everything between `=== BEGIN PROMPT ===` and `=== END PROMPT ===`.
   The word `ultracode` on the first line is intentional. It is your explicit opt-in for
   multi-agent Workflows. The prompt restricts them to Phases D–F, after you approve the plan.

## What you will be asked to do

| Phase | Your action |
|---|---|
| A — Discovery | nothing. Claude reads all of `main/` with parallel read-only agents (no files are written) |
| B — Questions | answer the questions (valve threshold, event naming, `reason` strings, rules reset, `battery:null`, re-provision behaviour, **two P0 safety bugs**, latent-bug scope) |
| C — Plan | review the plan in plan mode and **approve or edit it**. No code is changed before this |
| D — Implement | nothing. Branch `fix/2.1.4`, `ROOT_CAUSE.md` committed first, then one commit per bug group, each built |
| E — Adversarial review | approve any fix that falls outside the plan (at most 2 E/F rounds) |
| F — Agent council | accept any residual risk the council cannot resolve |
| G — Deliver | flash 2.1.4 and run `docs/field_logs/2.1.4/BENCH_TEST.md` |

## Headline findings (details in ANALYSIS.md)

- **BUG-1:** valve Critical = battery ≤ 10 %. The hub has no battery→CRITICAL rule and reports battery `0` before the first read.
- **BUG-2:** every decommission `memset`s the whole health table and re-arms the sync/grace windows for everyone. The fix keeps the intended 600 s "Syncing" grace and the per-advertisement snapshots for not-yet-heard devices, and stops a removal from resetting the survivors.
- **BUG-3/6:** `if (!provisioned) { …; continue; }` in the iothub loop skips the snapshot flush.
- **BUG-5:** the snapshot's valve block is keyed on the live BLE link, not on provisioning.
- **P0 (Bug 5 state):** a sensors-only hub can close any nearby eFloStop valve, and BLE never starts without a provisioned valve. The two fixes must ship together.
