# sdkconfig.wp9: WP9's memory set, staged and off by default (2.1.4)

Plan section 8's WP9 table (decision D14) and HANDOFF section 15n's `cloud_tx` stack trim, one file per line.
Nothing here is part of any image. No build reads a file in this folder unless its command names it,
`sdkconfig.defaults` is unchanged, and no source file is changed, so CP7's image (`52ef6a2`) is exactly as before.
Each line is built and benched on its own as a G-M variant (plan section 12). It ships only if it pays and its
gate passes, and then lands as described under "Landing a line".

The figures below come from ESP-IDF 5.5.1's own sources: kconfgen run on CP7's `sdkconfig` with each fragment,
scratch compiles of the affected IDF and project sources with the build's flags, CP5's map for the IRAM line, and
the Kconfig help for W2's buffer size. Nothing was built, linked or flashed; the bench measures each line.

| File | Line | Frees at rest (estimate) | Gate (G-M), in short | Status |
|---|---|---|---|---|
| `w1_wifi_iram_off.defaults` | `ESP_WIFI_IRAM_OPT` and `ESP_WIFI_RX_IRAM_OPT` off | **17,580 B** of IRAM code to flash: DIRAM `.text` 113,387 -> about 95,800 B, heap about +17.5 KB, flash about +17.6 KB | IRAM gate re-baselined; G-CNA p90 within +20 %; TLS and DPS; LS-1; adverts per burst +-5 % | bench |
| `w2_wifi_static_rx_6.defaults` | `ESP_WIFI_STATIC_RX_BUFFER_NUM` 10 -> 6 | about 6.4 KB of heap | G-CNA S1 and the tail; G3 (E4, E2) | bench |
| `w3_nimble_evt_12.defaults` | `BT_NIMBLE_TRANSPORT_EVT_COUNT` 30 -> 12 | 4,968 B (heap 4,680, `.bss` 288) | VAL-01, G6b, P11, P14; no `host_rcv_pkt` assert in a 24 h soak | bench |
| `w4_nimble_acl_8_msys2_12.defaults` | `BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT` 24 -> 8, `BT_NIMBLE_MSYS_2_BLOCK_COUNT` 24 -> 12 | 8,704 B of heap | G6b, VAL-01, P11, P14, LS-1; no ACL or msys shortage line | bench |
| `w5_nimble_one_connection.defaults` | `BT_NIMBLE_MAX_CONNECTIONS` 3 -> 1 | 528 B of `.bss` | G6b, the 100 valve power cycles with 0 `ble_gap_connect rc=6` | bench; small |
| `w6_nimble_central_observer_only.defaults` | `BT_NIMBLE_ROLE_PERIPHERAL` and `_BROADCASTER` off | 1,226 B static and the GATT server's tables; flash about -18 to -20 KB | **does not link without `w6_valve_gatt_server_guard.patch`**; then VAL-01 in full, G6b, P11, P14 | bench in a worktree; small |
| `w7_mbedtls_free_config_data.defaults` | `MBEDTLS_DYNAMIC_FREE_CONFIG_DATA` on | **0 B** on this hub (bundle CA, no client certificate, no DHM); flash at most about +160 B | TLS and DPS regression | **recommend: do not ship** |
| `w8_cloud_tx_stack_4096.patch` | `CLOUD_TX_STACK_BYTES` 5,120 -> 4,096 (a source constant: no Kconfig symbol, so no defaults file) | 1,024 B of `.bss` | only after T6-11 shows >= 1,536 B free on `cloud_tx`; then T6-11 again, >= 512 B free | waits for T6-11 |
| `gm_nimble_pool_diag.patch` | bench-only diagnostic for W3 and W4: NimBLE's pool lows on the `MONITOR` line | - | - | **never shipped** |

If every line but W7 shipped, the set would free about 40 KB at rest (17.6 + 6.4 + 5.0 + 8.7 + 0.5 + 1.2 + 1.0 KB, plus
W6's tables). Plan section 8's "+40-50 KB" holds, but not its split: the two IRAM lines free 17.6 KB on this image, not
"more than 27 KB" (that is the Kconfig help's figure for every such function; only the ones this image links move),
and W4 frees 8.7 KB, not 7.8 KB. The row's other half, `CONFIG_BT_NIMBLE_LOG_LEVEL_WARNING=y`, shipped in `29f400b`
(decision B4).

## Rules

- Never put a line from here into the project's `sdkconfig`, and never build a variant into the project's `build\`:
  kconfgen loads an existing `sdkconfig` over the defaults, so a value left there stays in every later build.
- One line per variant, each in its own build folder with its own `sdkconfig` (the recipe below). A combined variant
  (several fragments in one `SDKCONFIG_DEFAULTS`) is for G4b only, after each line passed alone.
- A variant is a bench image: never on a field hub. Flash the production build back afterwards
  (`idf.py -p <port> flash` from the project's own `build\`).

## Building a variant (PowerShell, ESP-IDF 5.5.1 environment, the project folder)

The variant's `sdkconfig` starts as a copy of the project's (CP7's) with the line's symbols deleted, so that the
fragment, read after `sdkconfig.defaults`, supplies them: kconfgen loads the defaults first and an existing
`sdkconfig` over them.

```powershell
git log --oneline -1
Get-FileHash sdkconfig        # CP7's: 9E13270C4A2D05B0781A88841318160C588683CE06D1574935C17308AAE0412D
$port = 'COM5'                # the hub's serial port
$line = 'w3_nimble_evt_12'    # the file name without .defaults
$re   = '^(# )?CONFIG_(BT_NIMBLE_TRANSPORT_EVT_COUNT)[ =]'   # the line's symbols: the table below
$dir  = "$env:TEMP\build_wp9_$line"
New-Item -ItemType Directory -Force $dir | Out-Null
(Get-Content sdkconfig) | Where-Object { $_ -match $re }
(Get-Content sdkconfig) | Where-Object { $_ -notmatch $re } | Set-Content "$dir\sdkconfig.wp9"
idf.py -B $dir -D SDKCONFIG="$dir\sdkconfig.wp9" -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.wp9/$line.defaults" build *> "$dir\build.log" ; "exit=$LASTEXITCODE"
Select-String -Path "$dir\build.log" -Pattern 'warning:|error:' | ForEach-Object Line
Compare-Object (Get-Content sdkconfig) (Get-Content "$dir\sdkconfig.wp9")
Get-FileHash "$dir\sdkconfig.wp9"
idf.py -B $dir -D SDKCONFIG="$dir\sdkconfig.wp9" -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.wp9/$line.defaults" size
Get-FileHash sdkconfig        # unchanged
git status --short            # unchanged
idf.py -B $dir -D SDKCONFIG="$dir\sdkconfig.wp9" -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.wp9/$line.defaults" -p $port flash monitor --timestamps
```

`Compare-Object` printing nothing means the filter did not take and the image is CP7's: stop. The expected lines and
hashes, from kconfgen 2.5.0 run the same way on CP7's `sdkconfig`:

| Line | `$re` symbols (inside `CONFIG_(...)`) | Filter prints | `Compare-Object` | Variant `sdkconfig` SHA256 |
|---|---|---|---|---|
| W1 | `ESP_WIFI_IRAM_OPT\|ESP_WIFI_RX_IRAM_OPT` | 2 | 8 (the two and their `ESP32_` aliases, `=y` -> `is not set`) | `8350C6339FB0F8FD16C80C16AF7F3858D6A631FDF32147AF26A179E2C49DF380` |
| W1 fallback (`ESP_WIFI_IRAM_OPT` alone, W1's file, a `$dir` of its own) | `ESP_WIFI_IRAM_OPT` | 1 | 4 | `26DE66589557ED33315E84DCD15E3DE44650905F9CCAF4DA7AD90BB0423A4D13` |
| W2 | `ESP_WIFI_STATIC_RX_BUFFER_NUM` | 1 | 4 (and `ESP32_WIFI_STATIC_RX_BUFFER_NUM`) | `D3F2D5DF4EEBDB6650C69BDF1E74EE7971CD27E04CDA77A13991FC6249F46B21` |
| W3 | `BT_NIMBLE_TRANSPORT_EVT_COUNT` | 1 | 4 (and `BT_NIMBLE_HCI_EVT_HI_BUF_COUNT`) | `1A4D9256CE95852F29A473EF8EF06194A53BB3E62D5332C654353FEB7149C242` |
| W4 | `BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT\|BT_NIMBLE_MSYS_2_BLOCK_COUNT` | 2 | 6 (and `BT_NIMBLE_ACL_BUF_COUNT`) | `B1DE0ADF86C455C4040E2CF237EBBCDE8CE730EC72F18AC570BF6F1DE5C1FBFD` |
| W5 | `BT_NIMBLE_MAX_CONNECTIONS` | 1 | 4 (and `NIMBLE_MAX_CONNECTIONS`) | `F04C659DC25525DB533E6957CA956BB80AACF97CCD9E645FB22E574648BF90DD` |
| W6 | `BT_NIMBLE_ROLE_PERIPHERAL\|BT_NIMBLE_ROLE_BROADCASTER` | 2 | 75 (the roles, `GATT_SERVER`, the services menu) | `FD79C7CA0828C6B052F0323AF8BD5EA22CB01AB08A3CD19C35E2C2153053E321` |
| W7 | `MBEDTLS_DYNAMIC_FREE_CONFIG_DATA` | 1 | 3 (and `MBEDTLS_DYNAMIC_FREE_CA_CERT=y`) | `372F493A3506A61CE8B059C7678441AE41BE7D84A6FF469D21F7841270FFC7D7` |

(In PowerShell a `|` inside `$re` is a plain alternation: write it without the table's backslash.)

## Worktree variants: W6, W8 and the pool diagnostic

These need a source change, so they are built from a worktree, never from the project folder:

```powershell
git worktree add ..\hub_wp9 HEAD             # the commit the project folder is at
Copy-Item sdkconfig ..\hub_wp9\sdkconfig      # CP7's
cd ..\hub_wp9
git apply sdkconfig.wp9\w6_valve_gatt_server_guard.patch    # or w8_..., gm_nimble_pool_diag.patch
git status --short                                         # only the patched file
```

Then the recipe above from that folder, with a build folder of its own (CMake refuses a build folder made from
another source tree): `$dir = "$env:TEMP\build_wp9_wt_$line"`. W8, and the diagnostic on CP7's configuration, take
no fragment: drop `$re`, the filter and the `-D SDKCONFIG_DEFAULTS` argument, and copy `sdkconfig` as it is. Afterwards `cd` back and
`git worktree remove --force ..\hub_wp9`. With CP7's configuration, `w6_valve_gatt_server_guard.patch` leaves
`app_ble_valve.c`'s object byte-identical. `gm_nimble_pool_diag.patch` adds, after each `MONITOR: idma:` line once
NimBLE has synced, `MONITOR: WP9 nimble pools, lowest free/blocks: cmd a/1 evt b/30 evt_lo c/8 acl d/24 msys_1 e/12
msys_2 f/24` (the lowest free count of each pool since boot); built with W3's or W4's fragment it shows their margins.

## Landing a line (only after its gate passed and the user approved it)

1. Its setting lines go into `sdkconfig.defaults`, in the I14 block, each with a one-line comment. kconfgen reads
   comments too: name symbols there without the `CONFIG_` prefix, and end no comment line with `=` (a deprecated
   name or a trailing `=` in a comment makes the configure print a "replaced" or "updated" line).
2. Its guard goes into `main/main.c`'s I14 block (below), in the same commit; W6's and W8's patches too.
3. The project's `sdkconfig` is regenerated as in CP7's step 1 (HANDOFF section 15t): delete the line's symbols
   (the `$re` above) and run `idf.py reconfigure`. On CP7's `sdkconfig`, with only that line landed, the result's
   hash is the variant's in the table.
4. The gates are re-baselined at the next build checkpoint (W1: DIRAM `.text`, I10's 113,387 B no longer applies).

| Line | Guard for `main/main.c` (I14) |
|---|---|
| W1 | `#if defined(CONFIG_ESP_WIFI_IRAM_OPT) \|\| defined(CONFIG_ESP_WIFI_RX_IRAM_OPT)` / `#error "I14: CONFIG_ESP_WIFI_IRAM_OPT and CONFIG_ESP_WIFI_RX_IRAM_OPT must be off (WP9 W1): about 17.6 KB of Wi-Fi code in flash, not IRAM"` |
| W2 | `#if CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM != 6` / `#error "I14: CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM must be 6 (WP9 W2): 4 static RX buffers fewer"` |
| W3 | `#if CONFIG_BT_NIMBLE_TRANSPORT_EVT_COUNT != 12` / `#error "I14: CONFIG_BT_NIMBLE_TRANSPORT_EVT_COUNT must be 12 (WP9 W3)"` |
| W4 | `#if CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT != 8 \|\| CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT != 12` / `#error "I14: NimBLE ACL buffers 8 and msys_2 blocks 12 (WP9 W4)"` |
| W5 | `#if CONFIG_BT_NIMBLE_MAX_CONNECTIONS != 1` / `#error "I14: CONFIG_BT_NIMBLE_MAX_CONNECTIONS must be 1 (WP9 W5): the valve is the hub's only link"` |
| W6 | `#if defined(CONFIG_BT_NIMBLE_ROLE_PERIPHERAL) \|\| defined(CONFIG_BT_NIMBLE_ROLE_BROADCASTER)` / `#error "I14: NimBLE central and observer only (WP9 W6): no GATT server"` |
| W7 | not recommended; if it lands: `#if !defined(CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA)` / `#error "I14: ... (WP9 W7)"` |
| W8 | none: the constant is in `app_iothub.c`, and its boot line prints it (`cloud_tx started (stack 4096 B, ...)`) |

(In C the guards' `||` has no backslash: the table escapes it.)
