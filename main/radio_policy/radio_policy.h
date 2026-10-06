#ifndef RADIO_POLICY_H
#define RADIO_POLICY_H
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "esp_wifi_types.h"
#include "http_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =================================================================================================
 * The radio policy (2.1.4 WP8 core; plan sections 3, 4.1-4.8, 5; HANDOFF section 15s, 15t)
 *
 * One 2.4 GHz radio is shared by Wi-Fi and BLE. This module is the single source of the radio
 * mode: it decides, from facts, which scan pattern the BLE scan executor runs (a "row" of the
 * profile table below) and when BLE stops for a Wi-Fi or valve "pulse". The executor (the
 * ble_leak_scan task, app_ble_leak.c) is the only code that starts or stops a BLE scan (I5); it
 * runs the rows slot by slot and asks this module before every pulse. Every fact has exactly one
 * writer (plan 4.1), and the mode is recomputed from them on every executor pass, at least twice a
 * second (I6). No task, timer or heap of its own (I10): it runs on its callers' tasks.
 *
 * The Wi-Fi side (app_wifi.c) gives the SoftAP, the STA's IP and attempts, the setup page's Connect,
 * the SoftAP's stations and the page's activity, and asks for its RETRY and LIST pulses. Nothing
 * else pauses BLE for Wi-Fi (2.1.4 WP8 deleted app_wifi.c's portal priority window and Wi-Fi radio
 * holds): the no-credential setup portal keeps scanning in the AP modes (D2).
 * ================================================================================================= */

/* ---- The sensor-firmware timings the profiles depend on (FW 1.1.0, unchanged in this release;
 * plan 4.8: documented hub assumptions) and the invariants checked against them -------------------- */
#define RP_ADV_SMAX_MS        448    // longest advert spacing: Ta max 437.5 ms + advDelay 10 ms.
                                     // PROVISIONAL until G0 measures Ta per sensor (the burst lines'
                                     // shortest dT): a longer Ta raises RP_L_MS, and the asserts then
                                     // say which row no longer covers a burst.
#define RP_JITTER_MS          100    // a slot's start: the NORMAL dither U(0, 100 ms), or start latency
#define RP_BURST_MIN_MS      2500    // a heartbeat burst
#define RP_BURST_EDGE_MS     4000    // a leak-edge burst
#define RP_L_MS              (RP_ADV_SMAX_MS + RP_JITTER_MS)       // 548
#define RP_W_MIN_MS          (RP_L_MS + 50)                        // 598: the shortest covering window
#define RP_GAP_MAX_MS        (RP_BURST_MIN_MS - 2 * RP_L_MS)       // 1404: the longest gap plus jitter
#define RP_BLIND_MAX_MS      2800    // I2: the longest span with no Coded scan
#define RP_RECOVERY_MS       1200    // I2: the Coded window after a pulse
#define RP_I8_BLE_MAX_MS      600    // I8: the longest BLE run in an AP row ...
#define RP_I8_WIFI_MIN_MS     300    // ... and the shortest Wi-Fi slot
#define RP_I2B_SPACING_MS    6000    // I2b: profile time between two pulses (SUBMIT exempt), at least ...
#define RP_I2B_JITTER_MS     1000    // ... plus U(0, this): every re-arm is jittered
#define RP_I2B_BLIND_MAX_MS 12000    // I2b: pulse time in any rolling ...
#define RP_I2B_WINDOW_MS    60000    // ... 60 s, every pulse counted (SUBMIT too)

/* ---- Pulses (plan 4.4): the only Coded stops longer than a row's own gap -------------------------- */
typedef enum {
    RP_PULSE_NONE = 0,
    RP_PULSE_JOIN,      // JOIN_ASSIST: a station joined the SoftAP (radio_policy_station_joined())
    RP_PULSE_SUBMIT,    // the setup page's Connect (radio_policy_note_submit())
    RP_PULSE_RETRY,     // the hub's router retry while the SoftAP is up (radio_policy_pulse_request())
    RP_PULSE_LIST,      // the hub's network-list scan (radio_policy_pulse_request())
    RP_PULSE_CONNECT,   // the valve claim's connect (the executor's own grant)
    RP_PULSE_COUNT
} rp_pulse_t;

#define RP_JOIN_LEASE_TAIL_MS   1500   // a join assist ends at max(lease + this, ...
#define RP_JOIN_PROBE_TAIL_MS    300   // ... first 302 or page served to that station + this), the budget,
                                       // or the station leaving
#define RP_RETRY_MS             1500   // the router retry's pulse
#define RP_LIST_MAX_MS          2500   // a list scan's pulse: its SCAN_DONE, at most this
#define RP_CONNECT_MS           1500   // a valve claim's connect outside a leak response: at least this ...
#define RP_CONNECT_LR_MS        2500   // ... and at most this, clipped to the blind budget (B2); exactly
                                       // this while a leak response is pending (plan 4.4)
#define RP_PULSE_MIN_MS          300   // a SUBMIT or JOIN with less budget left than this is not run
#define RP_GRANT_WAIT_MS        2000   // a request not granted this soon is refused: the requester goes
                                       // on without a pulse (plan 4.4's grant protocol)
#define RP_JOIN_SPACING_MS     30000   // per station: assists at least this ...
#define RP_JOIN_SPACING_JIT_MS 10000   // ... + U(0, this) apart
#define RP_LR_JOIN_HOLDOFF_MS  30000   // under a leak response: no assist in its first 30 s ...
#define RP_LR_JOIN_EVERY_MS    60000   // ... then at most one per 60 s
#define RP_JOIN_SETTLE_MS      10000   // I7: no hub list scan or router retry this soon after a join
                                       // that has no lease yet (radio_policy_join_settling())
#define RP_STA_PRUNE_MS         2000   // a station the driver no longer lists is marked left only
                                       // once its join is this old (radio_policy_stations_prune())
#define RP_LR_OVERLAY_CAP_MS   (10u * 60u * 1000u)   // the LR overlay: at most this per episode (D5)

/* ---- The profile table (plan 4.8): one row is one scan pattern, its slots run in turn --------------
 * Slot kinds: C Coded only (160/160, continuous), M 1M only (160/160), N N_CODED's combined scan
 * (1M 160/32 + Coded 160/128), W a Wi-Fi slot (no scan), _ unused. Times in ms. Flags:
 *   RP_F_DITHER  each scan starts after U(0, RP_JITTER_MS) (NORMAL's de-lock, plan 5.1), and that
 *                jitter counts once per slot boundary in the I1 gap;
 *   RP_F_I1      I1 for Coded: exactly one Coded window, >= RP_W_MIN_MS, its gap plus jitter
 *                <= RP_GAP_MAX_MS;
 *   RP_F_I1M     the same for 1M (rows that run while a sensor is known to be on 1M);
 *   RP_F_I8      I8: every BLE slot <= 600 ms and followed by a Wi-Fi slot, every Wi-Fi slot >= 300 ms;
 *   RP_F_DISC    a discovery row: it runs only in turn with its plain row, so the period rule is
 *                checked on that sequence (RP_SEQ_*), not on the row alone;
 *   RP_F_LAB     a lab row (APP_RADIO_LAB only): I1 and I2 hold, I8 and the period rule are waived.
 * N_HUNT is B2's (the valve hunted in NORMAL, see radio_policy.c); RECOVERY follows every pulse (I2).
 * SERVE_B and SERVE_B_DISC are the council's rung, for SERVE_RUNG (G1 decides). SERVE_C and
 * SERVE_C_DISC exist only in the G1 lab image (plan 4.3: its 2.0 s period is resonant with TCP's
 * 1 s and 3 s retransmits, both in its Wi-Fi slot, and divides 8 s and 100 s). */
#define RP_K__ 0
#define RP_K_C 1
#define RP_K_M 2
#define RP_K_N 3
#define RP_K_W 4

#define RP_F_DITHER 0x01
#define RP_F_I1     0x02
#define RP_F_I1M    0x04
#define RP_F_I8     0x08
#define RP_F_DISC   0x10

#if CONFIG_APP_RADIO_LAB
#define RP_F_LAB    0x20
#define RP_ROWS_LAB(X) \
    X(SERVE_C,      RP_F_I1 | RP_F_LAB,                          C,1000, W,1000, _,0,   _,0)   \
    X(SERVE_C_DISC, RP_F_I1 | RP_F_DISC | RP_F_LAB,              C,1000, W,400,  M,300, W,300)
#else
#define RP_ROWS_LAB(X)
#endif

#define RP_ROWS(X) \
    /* name          flags                                       slots: kind,ms x 4 */ \
    X(N_CODED,      RP_F_DITHER | RP_F_I1,                       N,1000, _,0,    _,0,   _,0)   \
    X(N_HUNT,       RP_F_DITHER | RP_F_I1,                       N,1000, M,300,  _,0,   _,0)   \
    X(N_MIXED,      RP_F_DITHER | RP_F_I1 | RP_F_I1M,            M,1000, C,1000, _,0,   _,0)   \
    X(NORMAL_LR,    RP_F_DITHER | RP_F_I1 | RP_F_I1M,            M,1000, C,600,  _,0,   _,0)   \
    X(RECOVERY,     RP_F_DITHER,                                 C,1200, _,0,    _,0,   _,0)   \
    X(SERVE_A,      RP_F_I1 | RP_F_I8,                           C,600,  W,600,  _,0,   _,0)   \
    X(SERVE_A_DISC, RP_F_I1 | RP_F_I8 | RP_F_DISC,               C,600,  W,300,  M,300, W,300) \
    X(SERVE_B,      RP_F_I1 | RP_F_I8,                           C,600,  W,1200, _,0,   _,0)   \
    X(SERVE_B_DISC, RP_F_I1 | RP_F_I8 | RP_F_DISC,               C,600,  W,600,  M,300, W,300) \
    X(APIDLE,       RP_F_I1 | RP_F_I8,                           C,600,  W,300,  _,0,   _,0)   \
    X(APIDLE_DISC,  RP_F_I1 | RP_F_I8 | RP_F_DISC,               C,600,  W,300,  M,300, W,300) \
    X(AP_K1M,       RP_F_I1 | RP_F_I1M | RP_F_I8,                C,600,  W,300,  M,600, W,300) \
    X(LR_AP_30,     RP_F_I1 | RP_F_I1M,                          M,1200, C,600,  _,0,   _,0)   \
    X(LR_AP,        RP_F_I1 | RP_F_I1M | RP_F_I8,                M,600,  W,300,  C,600, W,300) \
    RP_ROWS_LAB(X)

#define RP_ROW_ENUM(name, fl, k0, m0, k1, m1, k2, m2, k3, m3) RP_ROW_##name,
typedef enum { RP_ROWS(RP_ROW_ENUM) RP_ROW_COUNT } rp_row_id_t;
#undef RP_ROW_ENUM
#define RP_ROW_NONE ((uint8_t)RP_ROW_COUNT)

typedef struct {
    uint8_t kind[4];
    uint16_t ms[4];
    uint8_t n;          // slots in use (the first n)
    uint8_t flags;      // RP_F_*
    uint8_t coded;      // the index of its Coded window (C or N), or 0
} rp_row_t;

/* The SERVE rung (plan 4.3, D8): the densest Coded geometry that passes the phone gates.
 * PROVISIONAL: SERVE-A until G1 benches the ladder. */
#define RP_RUNG_SERVE_A        0   // [C 0.6][W 0.6]
#define RP_RUNG_SERVE_A_THIN   1   // SERVE-A for RP_HOT_MS after a hot event, AP_IDLE density otherwise
#define RP_RUNG_SERVE_B        2   // [C 0.6][W 1.2] (the council's)
#define RP_SERVE_RUNG          RP_RUNG_SERVE_A
#if CONFIG_APP_RADIO_LAB
/* The lab image's two more rungs (radio_lab.h, G1): there the rung is chosen at run time. */
#define RP_RUNG_SERVE_C        3   // [C 1.0][W 1.0] (lab only: breaks I8 and the period rule)
#define RP_RUNG_APIDLE         4   // AP_IDLE density: SERVE runs AP_IDLE's rows (D8 (c))
#define RP_RUNG_COUNT          5
#endif

/* ---- Modes (plan 4.2; first match wins) ------------------------------------------------------------ */
typedef enum {
    RP_MODE_PAUSED = 0,   // NimBLE not synced: nothing scans
    RP_MODE_BLE_IDLE,     // no BLE leak sensor, and no valve or its link verified: nothing scans
    RP_MODE_LR_AP,        // a leak response (overlay), SoftAP up and STA not connected
    RP_MODE_NORMAL_LR,    // a leak response (overlay), otherwise
    RP_MODE_NORMAL,       // not windowed (SoftAP down, or STA connected: the TAIL runs NORMAL)
    RP_MODE_SERVE,        // windowed, and a setup page in use, a submit in flight or a fresh lease
    RP_MODE_AP_IDLE,      // windowed, otherwise
    RP_MODE_COUNT
} rp_mode_t;

/* =================================================================================================
 * Facts from the Wi-Fi side (app_wifi.c, 2.1.4 WP8). One writer each; every setter is a plain store
 * or a short spinlock section, never blocks, and wakes the executor when the mode may change.
 * ================================================================================================= */

/** The SoftAP is up / the STA has its IP. One writer: the wifi_manager task's START_AP, STOP_AP,
 *  GOT_IP and STA_DISCONNECTED callbacks. wifi_task cross-checks the SoftAP's against
 *  esp_wifi_get_mode() on every pass and only logs a disagreement. */
void radio_policy_note_wifi(bool ap_up, bool sta_ip);

/** A STA connect attempt is in flight (wifi_manager task: CONNECT_STA started, ended at its
 *  disconnect or IP). Used for the join assist's one extra (plan 4.4). */
void radio_policy_note_sta_attempt(bool in_flight);

/** The setup page's Connect (C8, kind USER) started (true) or ended (false: GOT_IP or failure).
 *  wifi_manager task. Starting requests the SUBMIT pulse: always honoured, also under a leak
 *  response, exempt from the 6 s spacing, but counted in I2b's 12 s per 60 s and in I2 (at most
 *  2.8 s, then 1.2 s of Coded), so no pattern of Connects blinds BLE beyond the invariants. It
 *  waits for its grant until the Connect ends (no 2 s limit, no refusal for room). While a leak
 *  response is pending the valve's claim goes first: every Wi-Fi pulse leaves it 2.5 s of I2b's
 *  room, and while it is due no Wi-Fi pulse goes before it unless the last pulse was a claim, so
 *  Connects cannot hold its RMLEAK / CLOSE off (radio_policy_exec_wifi_grant()). The caller does
 *  not wait: BLE stops within one executor wake when nothing else runs. */
void radio_policy_note_submit(bool in_flight);

/** A station joined / left the SoftAP (default event loop: AP_STACONNECTED, AP_STADISCONNECTED).
 *  A join requests a JOIN_ASSIST when that station's spacing allows (30 s + U(0, 10 s) per MAC;
 *  under a leak response none in its first 30 s, then one per 60 s); a leave ends its assist. */
void radio_policy_station_joined(const uint8_t mac[6]);
void radio_policy_station_left(const uint8_t mac[6]);

/** A station got its lease (default event loop: IP_EVENT_AP_STAIPASSIGNED, which carries the MAC
 *  in IDF 5.5.1). Starts provisional SERVE for 60 s, at most once per MAC per 10 min. */
void radio_policy_station_leased(const uint8_t mac[6], uint32_t ip);

/** The portal's activity hook (C10a): call it from app_wifi.c's portal_activity() for every kind.
 *  httpd task (PAGE, API_USER, API_BG, PROBE_302, STATUS) or dns_server task (DNS, ignored here).
 *  page in use: PAGE, API_USER, API_BG (not STATUS); hot: PAGE, API_USER, PROBE_302; a station's
 *  first 302 or page (by its address) ends its join assist 0.3 s later. */
void radio_policy_portal_activity(http_app_activity_t kind, uint32_t client_ip);

/** The SoftAP's stations as the driver lists them now (wifi_task, every pass; NULL: the SoftAP is
 *  down). A station of the table that is not in the list, and joined RP_STA_PRUNE_MS ago or more
 *  (a list read just before a join must not undo it), left with no event: it is marked left, which
 *  ends its join assist. */
void radio_policy_stations_prune(const wifi_sta_list_t *list);

/** Plan I7: a station is joining the SoftAP: one joined less than RP_JOIN_SETTLE_MS ago and has
 *  no lease yet, or a JOIN_ASSIST is asked for or runs. The hub's own off-channel work (the router
 *  retry, a network-list scan) waits for it, so the SoftAP stays on its channel for the station's
 *  DHCP. Any task; a short spinlock. */
bool radio_policy_join_settling(void);

/* ---- The grant protocol (plan 4.4) for RETRY and LIST --------------------------------------------- */
typedef enum {
    RP_GRANT_IDLE = 0,    // nothing asked, or the pulse is over
    RP_GRANT_PENDING,     // asked, waiting for the executor
    RP_GRANT_ON,          // BLE is off for this pulse now, until radio_policy_pulse_end() or its deadline
    RP_GRANT_FREE,        // no pulse is needed: BLE is not scanning (BLE_IDLE, not synced, or the
                          // executor not started: no BLE device, NimBLE not up) or the SoftAP is
                          // down (RETRY/LIST are pulses only while it is up)
    RP_GRANT_REFUSED,     // not granted (limits, or RP_GRANT_WAIT_MS passed): go on without a pulse
} rp_grant_t;

/** Ask for a RETRY or LIST pulse (any task but the executor's). Non-blocking. Then either poll
 *  radio_policy_pulse_state() or call radio_policy_pulse_wait(). RETRY is granted only at the end
 *  of a Coded window (1.5 s), LIST at one with room for 2.5 s; both only with the 6 s spacing and
 *  I2b's budget, and only while the SoftAP is up. The requester must gate LIST itself on
 *  internal-DMA >= 24 KB (plan 4.4) and RETRY on its 30 s rule and the page deferral (app_wifi.c). */
void radio_policy_pulse_request(rp_pulse_t kind);

/** The state of the caller's pulse (any task). */
rp_grant_t radio_policy_pulse_state(rp_pulse_t kind);

/** Waits (polling every tick) until the pulse is ON, FREE or REFUSED, at most max_ms (use
 *  RP_GRANT_WAIT_MS); returns that state, or PENDING if max_ms ran out (the executor then refuses
 *  it). Go ahead on ON or FREE; on REFUSED or PENDING go ahead without a pulse and log a warning. */
rp_grant_t radio_policy_pulse_wait(rp_pulse_t kind, uint32_t max_ms);

/** The requester's own end of its pulse (LIST: SCAN_DONE; RETRY: its attempt's outcome, or leave
 *  it to the 1.5 s deadline). Withdraws a pulse still pending. Any task. */
void radio_policy_pulse_end(rp_pulse_t kind);

/* =================================================================================================
 * The leak-response overlay (plan 4.1, D5). Valve command task only (lr_poll()).
 * ================================================================================================= */
typedef struct {
    bool overlay;     // the LR overlay runs now: trigger, and less than 10 min since the episode began
    bool started;     // this call began the episode
    bool changed;     // overlay differs from the last call's
    uint32_t ran_s;   // seconds since the episode began (0: none)
} rp_lr_t;

/** trigger: the widened leak-response trigger (the valve provisioned and not linked, and an
 *  RMLEAK / CLOSE pended or a latched incident the valve has not confirmed). episode: something
 *  is pended or an incident is latched (the episode, and its 10 min cap, end when neither is).
 *  incident: a leak incident is latched (AP modes then keep discovery without back-off). */
rp_lr_t radio_policy_note_lr(bool trigger, bool episode, bool incident);

/** The overlay runs (NORMAL_LR / LR_AP). Lock-free; any task. */
bool radio_policy_lr_overlay(void);

/** A leak response is pending (the trigger, whatever the cap). Lock-free; any task. */
bool radio_policy_lr_pending(void);

/* =================================================================================================
 * The executor's interface: the ble_leak_scan task (app_ble_leak.c) only, so none of it locks.
 * ================================================================================================= */

/** The boot self-test of the table (plan 4.8): the same checks as the asserts, and the rows'
 *  sequences. On a failure every mode runs N_CODED and no Wi-Fi pulse is granted (pinned). */
void radio_policy_init(void);
bool radio_policy_pinned(void);

const rp_row_t *radio_policy_row(uint8_t row);
const char *radio_policy_row_name(uint8_t row);
uint32_t radio_policy_row_gap_ms(uint8_t row);   // its own Coded gap (period - Coded window), 0 for none

#define RP_STARVED_NONE  0
#define RP_STARVED_CODED 1   // a sensor on Coded is unheard >= 250 s
#define RP_STARVED_K1M   2   // a sensor known on 1M, or with its PHY unknown, is

typedef struct {
    bool synced;              // NimBLE synced
    uint8_t sensors;          // BLE leak sensors listed
    bool any_1m;              // one is known to advertise on 1M
    bool any_unknown;         // one's PHY is unknown (never heard since it was listed)
    uint32_t listed_ms;       // ms since the list last gained a sensor (or the first load)
    uint8_t starved;          // RP_STARVED_*
    bool valve;               // a valve is provisioned
    bool valve_linked;        // its link is up, verified by ble_gap_conn_find()
    bool valve_hunt;          // it is wanted, not linked, and no connect is in flight
    bool valve_slot;          // ... and B2's hunt slot can help (ble_valve_hunt_slot_wanted())
} rp_ble_facts_t;

/** Every pass (I6): the facts in, the mode out. */
rp_mode_t radio_policy_exec_mode(const rp_ble_facts_t *f, TickType_t now);

/** The row to run now (RP_ROW_NONE: nothing scans). new_period: a period just ended (its last
 *  slot): the mode's sequence advances (discovery every Nth period). Call after exec_mode(). */
uint8_t radio_policy_exec_row(TickType_t now, bool new_period);

/** A Wi-Fi pulse to grant now, or RP_PULSE_NONE. kinds: RP_PULSE_* bits (1u << kind) to consider.
 *  at_coded_end: right after a Coded window's own end (aligned kinds need it). budget_ms: I2's
 *  blind budget left now (RP_BLIND_MAX_MS minus the time since the last Coded window). coded_young:
 *  a Coded scan runs that has not yet covered RP_L_MS (unaligned kinds wait for it). claim_due: the
 *  valve's claim is due (ble_valve_claim_wanted()); under a leak response it goes first. */
rp_pulse_t radio_policy_exec_wifi_grant(TickType_t now, uint32_t kinds, bool at_coded_end,
                                        uint32_t budget_ms, bool coded_young, bool claim_due,
                                        uint32_t *len_ms);

/** The claim's connect length if a CONNECT may be granted now, else 0: the mode, I2b, and I2's
 *  budget (RP_CONNECT_LR_MS under a leak response; otherwise RP_CONNECT_MS to RP_CONNECT_LR_MS). */
uint32_t radio_policy_exec_connect_len(TickType_t now, uint32_t budget_ms);

void radio_policy_exec_pulse_begin(rp_pulse_t kind, TickType_t now, uint32_t len_ms);
void radio_policy_exec_pulse_retract(rp_pulse_t kind);      // a grant BLE could not honour: pending again
bool radio_policy_exec_pulse_over(TickType_t now);          // a Wi-Fi pulse's end is due
TickType_t radio_policy_exec_pulse_deadline(void);
TickType_t radio_policy_exec_pulse_wake(void);              // the deadline, or a join assist's own
                                                            // end when that is known and sooner
void radio_policy_exec_pulse_end(TickType_t now);

/** Time since the last call goes to the state the last pass left (the summary, I2b, the duty
 *  watchdog). profile: a row runs (its Wi-Fi slots included; I2b's spacing counts it); want: a
 *  scan slot is due or runs; scanning: our scan runs; recovery: the recovery row runs. */
void radio_policy_exec_account(TickType_t now, bool profile, bool want, bool scanning, rp_pulse_t pulse,
                               bool recovery);

/** The I2 monitor: a Coded window started blind_ms after the last one ended (no hold or pause in
 *  between). Counts spans over RP_BLIND_MAX_MS + RP_JITTER_MS. */
void radio_policy_exec_gap(uint32_t blind_ms);

/** The 60 s summary and the duty watchdog. adverts: the leak scanner's per-sensor counts. The
 *  executor calls it at a priority below iothub_task's (15t I-5). */
void radio_policy_exec_summary(const char *adverts, unsigned phy_flips);

#ifdef __cplusplus
}
#endif

#endif // RADIO_POLICY_H
