# WP9: the G-M bench procedure, one line at a time

> **In the repo since 2026-10-07 (WP10, `HANDOFF.md` 15w).** Copied from the WP9 stage's scratch procedure, with the
> corrections of the WP9 adversarial review (WP9-ADV-1 to -5) and CP7 moved to `b651701` (phase 3). The review's
> points override `sdkconfig.wp9/README.md` and the fragments' comments where they differ, until that folder is
> corrected (`HANDOFF.md` 15x). The corrections:
> - **W6 must never be benched or shipped (WP9-ADV-1, critical).** In ESP-IDF 5.5.1, `BLE_GATTS` = 0 (which the
>   peripheral role takes with it) compiles out NimBLE's receipt of Handle Value Notifications and Indications
>   (`nimble/host/src/ble_att.c` lines 62-80 dispatch them only under `#if MYNEWT_VAL(BLE_GATTS)`; `ble_att_svr.c`
>   is under it as a whole) and the ATT error response (`ble_att.c:511-515`). The hub would lose every valve
>   notification: the flood probe's wet report (`health_post_valve_leak`, a CRITICAL valve-probe leak), RMLEAK 1->0
>   from the valve's long press (SRS 4.4.2's 24 h grace), a manual open or close, a battery at 10 % or less. W6's
>   gate (VAL-01, G6b, P11, P14) cannot see it: the CCCD writes succeed and every RMLEAK or CLOSE read-back is a
>   `ble_gattc_read`. An ATT request from the valve gets no answer at all (the valve then waits out its 30 s ATT
>   timeout), not an error. Section 5.6 is replaced; W6 is out of G4b and the totals.
> - **Totals (WP9-ADV-2):** about **38.2 KB** on plan section 8's rows that can ship (W1-W5: 17,580 + about 6,400
>   + 4,968 + 8,704 + 528 B), plus W8's 1.0 KB if T6-11 allows it; W6 excluded; W7 0. The plan's "+40-50 KB" is met
>   only with W8, or not at all.
> - **Pin the commit (WP9-ADV-3):** a variant is "CP7 plus one line" only when built at CP7's commit, `b651701`, or a
>   later commit that changes no firmware (section 2's check), and a worktree is added at that commit.
> - **W4's risk (WP9-ADV-4):** an ACL shortage can block the controller's VHCI receive callback up to 29 ticks (about
>   290 ms at `CONFIG_FREERTOS_HZ` 100) and then drop the packet, which can be a valve notification lost with no
>   GATT timeout (notifications have no ATT response); the `Free ACL mbufs: N` line prints the msys count, not the
>   ACL pool's (section 5.4).
> - **Worktrees (WP9-ADV-5):** the managed components are tracked in git, so a fresh worktree fetches nothing
>   (section 4).


Source: the WP9 stage of 2026-10-07, on `fix/2.1.4` (started at `d0d2bd3`; commits in section 0). The package is `sdkconfig.wp9/` (seven
defaults fragments, two staged patches, one bench-only diagnostic patch, `README.md`); it changes no image: CP7
(now of `b651701`, `HANDOFF.md` 15t) is exactly as without it. Plan references: section 8 (the WP9 table, the floors), section 11 (WP9),
section 12 (G-M, G3, G4b, G6b), section 13 (D14); HANDOFF 15n (`cloud_tx`), 15t (CP7, VAL-01), 15u (B2's
statistical gate, B4), 15v (the lab image's build-folder pattern, which this follows).

Nothing in this procedure was run: no bench data exists for any line. Every expected figure below comes from
kconfgen 2.5.0 run on CP7's `sdkconfig`, scratch compiles of the affected ESP-IDF 5.5.1 and project sources with
`build\compile_commands.json`'s flags, CP5's map (the IRAM line), or the Kconfig help (W2's buffer size). The
scratch material is in `...\scratchpad\finish3\wp9\` (`sim.py`, `simfinal.py`, `kc\`, `out\`, `final\`,
`proj_*.txt`, `nimble_*.txt`, `tls_*.txt`, `wifi_*.txt`, `pools\`, `pt\`, `po\`).

## 0. The commits (on `fix/2.1.4`, after `509e6b6`; every file under `sdkconfig.wp9/`, nothing else)

| SHA | Subject | Files |
|---|---|---|
| `ca8ed64` | build(wp9): stage W1 and W2, the Wi-Fi memory lines, off by default | `w1_wifi_iram_off.defaults`, `w2_wifi_static_rx_6.defaults` |
| `f3ef9cc` | build(wp9): stage W3 to W6, the NimBLE memory lines, off by default | `w3_nimble_evt_12.defaults`, `w4_nimble_acl_8_msys2_12.defaults`, `w5_nimble_one_connection.defaults`, `w6_nimble_central_observer_only.defaults`, `w6_valve_gatt_server_guard.patch` |
| `1d64367` | build(wp9): stage W7 (mbedTLS) and W8 (cloud_tx stack), off by default | `w7_mbedtls_free_config_data.defaults`, `w8_cloud_tx_stack_4096.patch` |
| `575e725` | test(wp9): add a bench-only NimBLE pool diagnostic for the G-M gates | `gm_nimble_pool_diag.patch` |
| `e3cad30` | docs(wp9): explain the staged memory set, its builds and its landing | `README.md` |

The image gates for the package itself: **no change at all** (no source, no `sdkconfig.defaults`, no Kconfig file
touched; no build reads the folder): DIRAM `.text`, `.bss`, `.data`, flash, tasks, timers and heap are CP7's. The
fragments are CRLF in the working tree like `sdkconfig.defaults` (LF in the index), the patches and the README LF.
Two radio commits by another stage (`d6c8b69`, `509e6b6`, `radio_policy.{c,h}` only) landed during this stage; the
three patches still pass `git apply --check` at `e3cad30`.

## 1. What G-M decides, and the rule

| Line | File | Frees at rest (estimate) | Gate in short |
|---|---|---|---|
| W1 | `w1_wifi_iram_off.defaults` | 17,580 B DIRAM `.text` -> heap | IRAM re-baseline; G-CNA S1, S2 p90 within +20 %; TLS and DPS; LS-1; adverts per burst +-5 % |
| W2 | `w2_wifi_static_rx_6.defaults` | about 6.4 KB heap | G-CNA S1 and the tail; G3 (E4, E2) |
| W3 | `w3_nimble_evt_12.defaults` | 4,968 B (heap 4,680, `.bss` 288) | VAL-01, G6b, P11, P14, 24 h soak with no `host_rcv_pkt` assert; adverts +-5 % |
| W4 | `w4_nimble_acl_8_msys2_12.defaults` | 8,704 B heap | G6b, VAL-01, P11, P14, LS-1; no ACL or msys shortage; 24 h soak |
| W5 | `w5_nimble_one_connection.defaults` | 528 B `.bss` | G6b, 100 valve power cycles with 0 `rc=6`, VAL-01, P11, P14 |
| W6 | `w6_nimble_central_observer_only.defaults` + `w6_valve_gatt_server_guard.patch` | 1,226 B static + GATT tables; flash -18 to -20 KB | **never bench, never ship** (WP9-ADV-1: the hub would receive no valve notification; its gate cannot see it) |
| W7 | `w7_mbedtls_free_config_data.defaults` | 0 B (expected) | TLS and DPS; recommended: skip and do not ship |
| W8 | `w8_cloud_tx_stack_4096.patch` | 1,024 B `.bss` | only after T6-11 at 5,120 B shows >= 1,536 B free; then T6-11 at 4,096 B, >= 512 B free |

**A line ships only if it pays and its gate passes** (plan section 8). Proposed meaning of "pays" (the user's
call): the median `idma: free` at rest (section 3, M-heap) rises by at least half the estimate. W5 (under 1 KB)
ships only if the user wants that gain for its risk, or if G4b's floor needs it. W6 never (WP9-ADV-1). W7 is expected to
free nothing: recommended not to bench at all, or one TLS and DPS run to confirm the 0.

**Recommended order:** first the pool-diagnostic baseline on CP7's own configuration (section 5.0: it shows in one
run whether W3's 12 and W4's 8 and 12 have room), then W4 and W3 (BLE bench), W2 and W1 (phones), W5 (with the
100 power cycles B2 needs anyway), W8 (after T6-11). Not W6. Each line's 24 h soak may be shared by W3 and W4
(one combined variant) once each passed its short gate; if that soak fails, soak them apart.

## 2. Preconditions (all on CP7, before any variant)

- CP7 built and VAL-01 passed (HANDOFF 15t); the project's `sdkconfig` is CP7's, SHA256
  `9E13270C4A2D05B0781A88841318160C588683CE06D1574935C17308AAE0412D`. A different hash: stop, every expected
  figure below assumes CP7's. **A variant is the project folder's commit plus one line** (WP9-ADV-3): run
  `git diff --stat b651701 HEAD -- main components sdkconfig.defaults CMakeLists.txt partitions.csv dependencies.lock`
  before every variant build; it must print nothing. If it prints anything, the firmware is no longer CP7's: build
  and bench the production image of that commit first, take the baselines below from it, and give a worktree that
  same commit. The variant hashes below hold only while `sdkconfig.defaults` and the Kconfig files are as at
  `b651701` (unchanged since `52ef6a2`; record them either way).
- **CP7's baselines, each recorded in the state the variant will be measured in** (the variant is judged against
  them, never against the plan's estimates):
  - M-heap at rest (section 3) and the `cloud admitted ... (internal DMA free X B, largest Y B)` line;
  - `idf.py size` (DIRAM `.text` 113,387 B, `.bss`, `.data`, flash `.text`, `.rodata`, total image);
  - G-CNA S1 and S2, P-1 to P-6 (and P-8, P-9), 10 runs each on the iPhone and one Android phone: the p90 of each
    step's time, P-5 (page rendered after the sign-in UI opens) in particular;
  - TLS and DPS: boot `Connected! IP` -> `Connected to Azure IoT Hub!`, 10 router power cycles' reconnect times, one
    first commissioning with DPS (cache cleared), a SAS renewal, a full snapshot; no `esp-tls` or mbedTLS error;
  - LS-1 (15k-12 / T6-21): leak -> CLOSE within 200 ms during a WAN black-hole;
  - adverts per burst: 30 min of `RADIO: [SUMMARY]` `adverts <id>=<n>` per sensor, with the setup unchanged
    (sensors, positions, valve state) for the variant run;
  - for W8: T6-11's high-water marks on CP7 (`cloud_tx` at 5,120 B).

## 3. Common measurements (every line)

**M-build** (the recipe, section 4): `exit=0`; the same four known warnings as CP7 (`app_ble_valve.c:106:9`,
`app_lora.cpp:194:5` x2, `app_lora.cpp:160:13`) and nothing else; the filter prints the listed lines;
`Compare-Object` prints exactly the listed lines (**nothing printed = the filter did not take and the image is
CP7's: stop**); the configure prints nothing about a `sdkconfig.wp9` file beyond its `Loading defaults file`
line (no `was replaced with`, no `line was updated to`); the variant `sdkconfig`'s hash as listed; the project's `sdkconfig` hash and `git status --short`
unchanged before and after. A configure that re-solves dependencies or changes `dependencies.lock` is a finding.

**M-size:** `idf.py ... size` against CP7's; the expected deltas are in each line's section. DIRAM `.text` must stay
exactly CP7's (113,387 B) for every line but W1: if it moves, stop and compare the IRAM input sections of the two
maps (not their `*fill*`).

**M-boot:** VAL-01's boot lines (HANDOFF 15t) unchanged, plus each line's own differences; no new E or W line; no
`Profile self-test FAILED`, no stack-canary panic, no `assert failed`.

**M-heap (pays or not):** the state: Wi-Fi and the cloud connected, SoftAP down, valve linked, every sensor heard
(`PHY table loaded: N of N`), no portal session since boot, 10 min after boot. Then 5 min of `MONITOR:` lines (30
`heap:` / `idma:` pairs, every 10 s): record the medians of `heap: free`, `idma: free`, `idma: largest`, and the
last `min_ever` and `allocfail`. Gain = variant median minus CP7's median in the same state. Also record the boot's
`cloud admitted ... internal DMA free X B, largest Y B`. Every line here frees DMA-capable internal memory, so the
gains in `heap: free` and `idma: free` should agree within about 1 KB. `allocfail` must stay 0 throughout every run.

**Flash and back:** the recipe's last command flashes the variant. After the line's runs, flash production back
from the project's own build (`idf.py -p <port> flash`) and check that the boot is CP7's (no variant line, the
`MONITOR` pool line gone if the diagnostic was on).

## 4. The build recipe (PowerShell, ESP-IDF 5.5.1 environment, the project folder)

The variant's `sdkconfig` is a copy of the project's with the line's symbols deleted, so the fragment, read after
`sdkconfig.defaults`, supplies them (kconfgen loads the defaults first and an existing `sdkconfig` over them;
`SDKCONFIG_DEFAULTS` replaces the default list, so it names `sdkconfig.defaults` first; relative paths resolve
from the project folder). The build folder and `sdkconfig` are the variant's own: the project's `sdkconfig` and
`build\` are never written (the lab image's pattern, HANDOFF 15v).

```powershell
git log --oneline -1
Get-FileHash sdkconfig        # 9E13270C4A2D05B0781A88841318160C588683CE06D1574935C17308AAE0412D
$port = 'COM5'                # the hub's serial port
$line = 'w3_nimble_evt_12'    # the fragment's file name without .defaults
$re   = '^(# )?CONFIG_(BT_NIMBLE_TRANSPORT_EVT_COUNT)[ =]'   # the line's symbols (table below)
$dir  = "$env:TEMP\build_wp9_$line"
New-Item -ItemType Directory -Force $dir | Out-Null
(Get-Content sdkconfig) | Where-Object { $_ -match $re }
(Get-Content sdkconfig) | Where-Object { $_ -notmatch $re } | Set-Content "$dir\sdkconfig.wp9"
idf.py -B $dir -D SDKCONFIG="$dir\sdkconfig.wp9" -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.wp9/$line.defaults" build *> "$dir\build.log" ; "exit=$LASTEXITCODE"
Select-String -Path "$dir\build.log" -Pattern 'warning:|error:' | ForEach-Object Line
Select-String -Path "$dir\build.log" -Pattern 'Processing \d+ dependencies|Manifest files have changed' | ForEach-Object Line
Compare-Object (Get-Content sdkconfig) (Get-Content "$dir\sdkconfig.wp9")
Get-FileHash "$dir\sdkconfig.wp9"
idf.py -B $dir -D SDKCONFIG="$dir\sdkconfig.wp9" -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.wp9/$line.defaults" size
(Get-FileHash "$dir\eFloStop_WiFiHub_idf1.elf").Hash
Get-FileHash sdkconfig        # unchanged
git status --short            # unchanged
idf.py -B $dir -D SDKCONFIG="$dir\sdkconfig.wp9" -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.wp9/$line.defaults" -p $port flash monitor --timestamps
```

| Line | `$line` | `$re` | Filter prints | `Compare-Object` prints | Variant `sdkconfig` SHA256 |
|---|---|---|---|---|---|
| W1 | `w1_wifi_iram_off` | `'^(# )?CONFIG_(ESP_WIFI_IRAM_OPT\|ESP_WIFI_RX_IRAM_OPT)[ =]'` | `CONFIG_ESP_WIFI_IRAM_OPT=y`, `CONFIG_ESP_WIFI_RX_IRAM_OPT=y` | 8: `=>` `# CONFIG_ESP_WIFI_IRAM_OPT is not set`, `# CONFIG_ESP_WIFI_RX_IRAM_OPT is not set`, `# CONFIG_ESP32_WIFI_IRAM_OPT is not set`, `# CONFIG_ESP32_WIFI_RX_IRAM_OPT is not set`; `<=` the same four `=y` | `8350C6339FB0F8FD16C80C16AF7F3858D6A631FDF32147AF26A179E2C49DF380` |
| W1 fallback | `w1_wifi_iram_off` (same file; set `$dir = "$env:TEMP\build_wp9_w1_fallback"` after `$dir`'s line) | `'^(# )?CONFIG_(ESP_WIFI_IRAM_OPT)[ =]'` | `CONFIG_ESP_WIFI_IRAM_OPT=y` | 4: `=>` `# CONFIG_ESP_WIFI_IRAM_OPT is not set`, `# CONFIG_ESP32_WIFI_IRAM_OPT is not set`; `<=` both `=y` (the RX option stays `y`: the copy's line wins over the fragment's) | `26DE66589557ED33315E84DCD15E3DE44650905F9CCAF4DA7AD90BB0423A4D13` |
| W2 | `w2_wifi_static_rx_6` | `'^(# )?CONFIG_(ESP_WIFI_STATIC_RX_BUFFER_NUM)[ =]'` | `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=10` | 4: `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM` and `CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM`, `10` -> `6` | `D3F2D5DF4EEBDB6650C69BDF1E74EE7971CD27E04CDA77A13991FC6249F46B21` |
| W3 | `w3_nimble_evt_12` | `'^(# )?CONFIG_(BT_NIMBLE_TRANSPORT_EVT_COUNT)[ =]'` | `CONFIG_BT_NIMBLE_TRANSPORT_EVT_COUNT=30` | 4: `CONFIG_BT_NIMBLE_TRANSPORT_EVT_COUNT` and `CONFIG_BT_NIMBLE_HCI_EVT_HI_BUF_COUNT`, `30` -> `12` | `1A4D9256CE95852F29A473EF8EF06194A53BB3E62D5332C654353FEB7149C242` |
| W4 | `w4_nimble_acl_8_msys2_12` | `'^(# )?CONFIG_(BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT\|BT_NIMBLE_MSYS_2_BLOCK_COUNT)[ =]'` | `CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT=24`, `CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT=24` | 6: `CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT` `24` -> `12`; `CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT` and `CONFIG_BT_NIMBLE_ACL_BUF_COUNT` `24` -> `8` | `B1DE0ADF86C455C4040E2CF237EBBCDE8CE730EC72F18AC570BF6F1DE5C1FBFD` |
| W5 | `w5_nimble_one_connection` | `'^(# )?CONFIG_(BT_NIMBLE_MAX_CONNECTIONS)[ =]'` | `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3` | 4: `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` and `CONFIG_NIMBLE_MAX_CONNECTIONS`, `3` -> `1` | `F04C659DC25525DB533E6957CA956BB80AACF97CCD9E645FB22E574648BF90DD` |
| W6 (never built: section 5.6; for the record only) | `w6_nimble_central_observer_only` | `'^(# )?CONFIG_(BT_NIMBLE_ROLE_PERIPHERAL\|BT_NIMBLE_ROLE_BROADCASTER)[ =]'` | `CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y`, `CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y` | 75: `=>` 4 (the two roles and their `CONFIG_NIMBLE_ROLE_` aliases `is not set`); `<=` 71 (those four `=y`, `CONFIG_BT_NIMBLE_GATT_SERVER=y`, and the whole "BLE Services" menu with its blank and comment lines). The file has 2,773 lines (CP7's 2,840) | `FD79C7CA0828C6B052F0323AF8BD5EA22CB01AB08A3CD19C35E2C2153053E321` |
| W7 | `w7_mbedtls_free_config_data` | `'^(# )?CONFIG_(MBEDTLS_DYNAMIC_FREE_CONFIG_DATA)[ =]'` | `# CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA is not set` | 3: `=>` `CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=y`, `CONFIG_MBEDTLS_DYNAMIC_FREE_CA_CERT=y`; `<=` the `is not set` line | `372F493A3506A61CE8B059C7678441AE41BE7D84A6FF469D21F7841270FFC7D7` |

(The table escapes `|` as `\|`; in PowerShell write it without the backslash.) Every variant `sdkconfig` but W6's
and W7's has 2,840 lines, as CP7's; W7's 2,841. The hashes were computed by kconfgen 2.5.0 on CP7's `sdkconfig` put
through this exact filter, with the committed fragments (CRLF), the build's `config.env`, `Kconfig` and renames
and `IDF_INIT_VERSION` 5.5.1; the same runner reproduces CP7's `sdkconfig` and `sdkconfig.h` byte for byte. Record
the hash either way.

**Worktree variants** (W8, the pool diagnostic; W6 never): they need a source change, so they build from a worktree,
never from the project folder.

```powershell
git worktree add ..\hub_wp9 b651701               # CP7's commit (or the commit whose production build gave the baselines)
Copy-Item sdkconfig ..\hub_wp9\sdkconfig      # CP7's
cd ..\hub_wp9
git apply sdkconfig.wp9\gm_nimble_pool_diag.patch     # or w6_valve_gatt_server_guard.patch, w8_cloud_tx_stack_4096.patch
git status --short                                   # only the patched file(s)
# the recipe above, from this folder, with a build folder of its own (CMake refuses a build folder made from
# another source tree): $dir = "$env:TEMP\build_wp9_wt_$line". For no fragment at all (the CP7 configuration
# with the patch), drop the filter, copy sdkconfig as it is to "$dir\sdkconfig.wp9", and leave out
# -D SDKCONFIG_DEFAULTS
cd "C:\Work\Projects\EfloStop 2\Firmware\Production\eFloStop_WiFiHub_idf1"
git worktree remove --force ..\hub_wp9
```

A worktree's `managed_components\` come from git (they are tracked: `git ls-files managed_components` lists 451
files, and with `STRICT_CHECKSUM` off in idf-component-manager 2.4.2 a CRLF checkout does not upset them): expect
`Processing 4 dependencies:` with no download and `dependencies.lock` unchanged. A download or a lock change is a
finding. `git apply` was checked (`--check`) against the working tree at
`d0d2bd3` for all three patches (and again at `b651701` by the phase-3 build council member); if a later commit moved their context, `git apply --3way` or the change in words
(each file's header) applies.

## 5. Per line

### 5.0 The pool-diagnostic baseline (CP7's configuration; before W3 and W4)

Worktree + `gm_nimble_pool_diag.patch`, no fragment. The patch adds, after every `MONITOR: idma:` line once
NimBLE has synced:
`MONITOR: WP9 nimble pools, lowest free/blocks: cmd a/1 evt b/30 evt_lo c/8 acl d/24 msys_1 e/12 msys_2 f/24`,
each the pool's lowest free count since boot. Its own cost (scratch compile): `monitoring.c` +210 B code, +89 B
`.rodata`, +160 B `.bss`; it newly links `os_mempool_info_get_next` (flash); no IRAM. Never shipped.

Run VAL-01's boot, G6b (x20), P11 and P14 (valve unpowered, then linked), 10 valve power cycles, N_MIXED (a boot
with an unknown PHY) and 30 min of NORMAL with every sensor live; read the last pool line after each.
- `evt`: peak use = 30 - b. **W3 has room if the peak use is 8 or less** (12 leaves 4). A peak above 12 means W3
  would spill events into the advertising pool, and assert once that is full too: W3 fails without a variant run.
- `acl`: peak use = 24 - d; W4's 8 has room at a peak of 5 or less. `msys_2`: peak use = 24 - f; W4's 12 has room
  at 8 or less. `evt_lo` at 0 is normal (advertising reports fill it; one more report is then dropped); `cmd` 0/1
  is normal.
Then the same runs on the W3 and W4 variants built in the same worktree (with the fragment): the margins in their
sections below.

### 5.1 W1: Wi-Fi IRAM options off

- **M-size:** DIRAM `.text` = CP7's - 17,580 B, about 95,800 B (accept 95,300-96,300 B: alignment fill moves; a drop
  far smaller means a mapping did not take: send both maps). IRAM 16,384 B stays 100 %. Flash `.text` about
  +17.6 KB (+16.5 to +18.5 KB); `.bss` and `.data` unchanged (+-8 B); total image size about unchanged (the code
  moves from IRAM to flash; +-1 KB). Record DIRAM `.text`: after W1 lands it is I10's new baseline.
- **M-boot:** `wifi_init: WiFi IRAM OP enabled` and `wifi_init: WiFi RX IRAM OP enabled` are gone (CP7 prints both).
- **M-heap:** gain about +17.5 KB (pays at >= +8.8 KB); record `idma: largest` too (the freed DIRAM joins the
  DRAM heap region, so the largest block may grow with it).
- **Gate:**
  - G-CNA S1 and S2, P-1 to P-6, 10 runs each on the iPhone and the Android phone: each step's p90 within +20 % of
    CP7's, P-5 (page rendered) in particular, and every pass line of plan 7.5 held. **Fail: a p90 more than +20 %,
    or a P-step failure CP7 did not have.**
  - TLS and DPS: boot connect, 10 router power cycles, one first commissioning with DPS, a SAS renewal, a full
    snapshot: medians within +20 % of CP7's; 0 `esp-tls` or mbedTLS errors; `allocfail` 0.
  - LS-1: leak -> CLOSE within 200 ms during the WAN black-hole (3 runs).
  - Adverts per burst: 30 min of `[SUMMARY]` per-sensor adverts within +-5 % of CP7's same setup;
    `BLE scanning ... (Z %)` with Z >= 90 in NORMAL (the coexistence code's 99 B moves to flash too).
- **Fallback if only the page gate fails:** build the W1 fallback (`ESP_WIFI_IRAM_OPT` off alone, the receive path
  stays in IRAM; table above): DIRAM `.text` = CP7's - 7,275 B (about 106,100 B), heap about +7.2 KB; same gate.
  It lands as its own line (`# CONFIG_ESP_WIFI_IRAM_OPT is not set` only, the guard on that symbol only).

### 5.2 W2: 6 static Wi-Fi RX buffers

- **M-size:** every section within +-16 B of CP7's (only `wifi_manager.c` changes, -4 B code); DIRAM `.text`
  113,387 B.
- **M-boot:** unchanged (`wifi_init: rx ba win: 6` as before).
- **M-heap:** gain about +6.4 KB (pays at >= +3.2 KB), from the first sample after boot.
- **Gate:**
  - G-CNA S1: P-1 to P-6, and P-8 and P-9 in the tail (SoftAP and STA both receiving), 10 runs each on the iPhone
    and the Android phone: p90 within +20 % of CP7's, every pass line held.
  - G3: the E4 replay and the E2 laptop flood (10 min): 0 failed allocations, internal DMA >= 8 KB and largest
    block >= 4.5 KB at every sample (`idma: min`, `min_largest`), no reboot.
  - TLS: a full snapshot and 10 reconnects with no `esp-tls` error.

### 5.3 W3: 12 high-priority HCI event buffers

- **M-size:** `.bss` -288 B (+-8); everything else unchanged; DIRAM `.text` 113,387 B.
- **M-heap:** gain about +4.97 KB (pays at >= +2.5 KB).
- **Failure signature:** `assert failed: host_rcv_pkt ... esp_nimble_hci.c ... (evbuf != NULL)`, a backtrace and a
  reboot: any one fails the line.
- **Gate:** VAL-01; G6b (0x3E induced x20: relink within 10 s, 0 `Already connected`, a pended CLOSE written);
  P11 and P14 (valve unpowered, then linked: RMLEAK before CLOSE, timings as CP7's); adverts per burst within
  +-5 % of CP7's (a full event pool spills into the advertising pool); then a 24 h G4 NORMAL soak: 0 asserts, 0
  false offlines, flat heap. Margin (diagnostic worktree with this fragment): `evt` lowest free >= 4 of 12 after
  all of the above.

### 5.4 W4: 8 ACL buffers, 12 msys_2 blocks

- **M-size:** every section within +-8 B of CP7's (the pools are heap); DIRAM `.text` 113,387 B.
- **M-heap:** gain about +8.7 KB (pays at >= +4.4 KB).
- **Failure signatures** (`esp_rom_printf`, no log tag, no timestamp): `ACL buf alloc failed N times`,
  `Free ACL mbufs: N`, `MBUF alloc stuck` (an ACL packet dropped after up to 29 ticks, about 290 ms at
  `CONFIG_FREERTOS_HZ` 100, with the controller's VHCI receive blocked meanwhile; `esp_nimble_hci.c` 173-188); a
  GATT timeout or a valve write retried as a busy GATT pool (BLE_HS_ENOMEM; the valve module retries every 250 ms,
  at most 5 s). Any one fails the line. **The dropped packet can be a valve notification** (the flood probe, RMLEAK
  1->0 from a long press, a manual open, a battery report): a notification has no ATT response, so it is lost with
  no GATT timeout and no other line. **`Free ACL mbufs: N` prints the msys free count** (`os_msys_num_free()`), not
  the ACL pool's: judge the ACL margin by the pool diagnostic's `acl` value only.
- **Gate:** G6b (x20); VAL-01; P11 and P14 with leak -> CLOSE timed (LS-1: within 200 ms with the valve linked,
  3 runs); none of the signatures over the runs and a 24 h G4 NORMAL soak (W3's soak may be shared, section 1).
  Margin (diagnostic worktree): `acl` lowest free >= 3 of 8 and `msys_2` >= 4 of 12.

### 5.5 W5: one NimBLE connection

- **M-size:** `.bss` -528 B (+-8); DIRAM `.text` 113,387 B.
- **M-heap:** gain about +0.5 KB (small: see section 1).
- **Failure signature:** `[SCAN] ble_gap_connect rc=6` (BLE_HS_ENOMEM: NimBLE still holds a link the valve module
  does not; with one slot every claim then fails until it closes).
- **Gate:** G6b (x20) and the 100 valve power cycles judged by B2's statistical gate (as chosen by the user for
  CP7; the relink distribution no worse than CP7's), with **0** `rc=6`; VAL-01; P11 and P14.

### 5.6 W6: central and observer only: never bench, never ship (WP9-ADV-1)

Not built, not benched, not landed. In ESP-IDF 5.5.1 the peripheral role takes `BT_NIMBLE_GATT_SERVER` with it, and
`BLE_GATTS` = 0 compiles out NimBLE's receipt of every valve notification and indication and the ATT error
response (the header note above has the source lines). The hub depends on notifications for the valve's flood
probe, RMLEAK 1->0 from the long press, the valve state and its battery (`app_ble_valve.c`: the subscriptions at
about lines 1293-1310, `BLE_GAP_EVENT_NOTIFY_RX` -> `on_notify()` at about 2221, `on_notify()` at about 856-930). The
staged gate (VAL-01, G6b, P11, P14) would pass, because every CCCD write succeeds and every RMLEAK or CLOSE
read-back is a GATT read: the loss would first show in the field. If W6 is ever reconsidered, its gate must
include valve-originated notifications (the valve probe wet -> `[DATA] Leak=1`; a long press -> RMLEAK 1->0; a
manual open; a battery notification), which fail by construction today. Its fragment, README row and patch header
(which say the GATT client stays and a valve ATT request gets an ATT error) are wrong on both counts until corrected.

### 5.7 W7: mbedTLS frees its configuration data (recommended: skip)

- **M-size:** flash at most about +160 B (objects: code +214, `.rodata` -57); RAM sections unchanged.
- **M-heap:** expected gain 0 (+-1 KB of noise): does not pay. If benched: TLS and DPS as in 5.1 (boot, 10 router
  power cycles, a first commissioning with DPS with the router pulled mid-DPS, a SAS renewal, a full snapshot),
  0 errors.

### 5.8 W8: `cloud_tx` stack 4,096 B (after T6-11)

- **Precondition:** T6-11 (the `2.1.4-hwm` image, MANUAL_TEST_PLAN T6-11; CP6 plan 15k-16) at CP7's commit shows
  `cloud_tx` with >= 1,536 B free at 5,120 B after its full scenario set. Less: W8 is dropped (below 1 KB the
  release raises the stack to 6,144 B instead).
- **Build:** T6-11's high-water-mark worktree with `w8_cloud_tx_stack_4096.patch` applied on top (no fragment).
- **M-size:** `.bss` exactly -1,024 B (the static stack; scratch compile of `app_iothub.c`); code unchanged.
- **M-boot:** `IOTHUB: cloud_tx started (stack 4096 B, priority 3)`.
- **Gate:** T6-11's own scenario set again, as run on CP7 (it includes a full-hub provision and 15k-13(b)):
  `cloud_tx` >= 512 B free (T6-11's per-task floor); no stack-overflow panic; heap at rest +1.0 KB.

## 6. G4b: the shipped set together (only if the WP9 floor is claimed)

Every line that passed (never W6), in one variant: `SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.wp9/<a>.defaults;sdkconfig.wp9/<b>.defaults;..."`
and `$re` the union of their symbols; W8's patch in a worktree if it passed. Then G4b (plan 12): 24 h
with 10 outages, 10 portal sessions and a laptop: internal DMA >= 16 KB and largest block >= 8 KB at every
sample, 0 failed allocations, 0 asserts or panics, flat heap. The combined `sdkconfig`'s hash is recorded.

## 7. Landing a line (after its gate and the user's approval)

1. Its setting lines into `sdkconfig.defaults`, in the I14 block, each with a one-line comment.
2. Its guard into `main/main.c`'s I14 block, in the same commit (the table in `sdkconfig.wp9/README.md`); W8's
   patch in that commit too. Remove the landed fragment (or mark it landed) so it is not built again.
3. The project's `sdkconfig` regenerated as CP7's step 1 (HANDOFF 15t): delete the line's symbols (its `$re`) and
   `idf.py reconfigure`. On CP7's `sdkconfig` with only that line landed, the new hash is the variant's above (the
   `sdkconfig` holds values only, so moving the line from the fragment to `sdkconfig.defaults` changes nothing).
   A stale `sdkconfig` then stops at the new `#error`, by design.
4. The next build checkpoint re-baselines: DIRAM `.text` after W1 (I10's 113,387 B goes), `.bss`, `.data`, the
   heap at rest, the plan's section 8 floors and the WP9 rows of the CHANGELOG.

## 8. Send back, per line

`build.log`, the filter's lines, the `Compare-Object` output, both `sdkconfig` hashes (variant, project before and
after), the ELF SHA256, `idf.py size`, the boot log through the first two `RADIO: [SUMMARY]` pairs, the M-heap
medians next to CP7's, each gate's figures (G-CNA p90 per step and phone, TLS and DPS times, LS-1 times, adverts per
sensor, G6b relink times, the 100 cycles' distribution), the pool-diagnostic lines where they ran, and both maps
if DIRAM `.text` moved unexpectedly.

## 9. Findings of this stage for the docs (plan section 8 and HANDOFF)

1. **The IRAM row frees 17,580 B on this image, not "+27 KB or more".** The Kconfig help's "> 10 KB" and "> 17 KB"
   count every function the options place; this image links 7,275 B (IRAM_OPT: libpp 5,271, libnet80211 1,824,
   libcoexist 99, three esp_wifi functions 81) and 10,305 B (RX_IRAM_OPT: `.wifirxiram` 3,364, `.wifislprxiram`
   6,941), summed from CP5's `.iram0.text` input sections, whose total matches the section to the byte; DIRAM
   `.text` has not moved since CP5, so the set is the same at CP7.
2. **W4 frees 8,704 B, not 7.8 KB** (an ACL buffer is 304 B with its mbuf headers); **W3 4,968 B** (4,680 B of
   pool and 288 B of NimBLE port statics).
3. **The set's total** (corrected by WP9-ADV-2): about **38.2 KB** at rest on plan section 8's rows that can ship
   (W1-W5), plus W8's 1.0 KB if T6-11 allows; W6 excluded (WP9-ADV-1), W7 0. The plan's "+40-50 KB" is met only
   with W8, or not at all. (The stage's first figure, 40.4 KB, counted W6 and W8.)
4. **W6 does not link with today's sources** (the three `ble_svc_*` calls), and **with its patch it would remove the
   hub's receipt of every valve notification** (WP9-ADV-1): never bench, never ship.
5. **W7 frees nothing on this hub** (certificate bundle, no client certificate, DHM off): recommended not to ship.
6. **W3's failure mode is a reboot**, not a dropped event (`assert(evbuf != NULL)` in `host_rcv_pkt`), and **W4's
   is a dropped ACL packet or a delayed GATT write**: both are bench-gated by margin (the diagnostic) as well as by
   the absence of failures.
7. **W5 changes the failure of a claim** made while NimBLE holds a link the module does not track: refused at once
   with `rc=6` instead of reaching the controller.
8. **W8 is a source constant, not a Kconfig symbol**: no defaults fragment can carry it; its one-line patch is
   staged.
9. The NimBLE log-level half of the W7 row shipped in `29f400b` (B4).
10. **kconfgen reads comments in a defaults file:** its deprecated-name pass searches every line for
    `CONFIG_<name>` followed by `=` or a space (a deprecated name in a comment is "replaced", with a printed
    line), and any line ending in `=` gets an `n` appended (printed too). Harmless to the values, but noisy in a
    build log: the fragments' comments name symbols without the `CONFIG_` prefix and no comment line ends in `=`.
    `sdkconfig.defaults` itself prints nothing of the kind today. Keep both rules when a line lands there.
11. Not in WP9 and not staged (for the record only): `CONFIG_BT_CTRL_BLE_MAX_ACT` (6) sizes the controller's own
    per-activity memory and could follow W5; plan section 14's 16 KB DCache change stays out unless the floor fails.
