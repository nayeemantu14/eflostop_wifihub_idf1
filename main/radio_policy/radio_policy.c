/****************************************************
 *  MODULE:   Radio policy (2.1.4 WP8 core)
 *  PURPOSE:  The single source of the radio mode: which BLE scan
 *            pattern runs, and when BLE stops for a Wi-Fi or valve
 *            pulse, decided from facts with one writer each. The
 *            BLE scan executor (app_ble_leak.c) runs what this says.
 *            See radio_policy.h and the 2.1.4 plan, sections 3-5.
 ****************************************************/

#include "radio_policy.h"
#include <string.h>
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "sdkconfig.h"
#include "ble_leak_scanner/app_ble_leak.h"
#include "radio_lab.h"

#define RP_TAG "RADIO"

/* ---------------------------------------------------------
 * The policy's own constants (plan 4.1-4.4). Each is one named constant; the provisional ones are
 * marked and listed in HANDOFF section 15 for the bench.
 * --------------------------------------------------------- */
#define RP_PAGE_ACTIVE_MS         60000   // a page or API request (not status.json) keeps SERVE this long
#define RP_PROV_SERVE_MS          60000   // provisional SERVE after a lease ...
#define RP_PROV_SERVE_EVERY_MS   600000   // ... at most once per station per 10 min
#define RP_HOT_MS                 10000   // SERVE-A-thin: SERVE-A this long after a hot event
#define RP_LR_AP_30_MS            30000   // LR_AP's first 30 s: [1M 1.2][C 0.6], Wi-Fi only for SUBMIT
#define RP_UNKNOWN_PHY_MS        600000   // B3: a 1M share while a listed PHY is unknown, at most this
                                          // long after boot or the sensor's provisioning
#define RP_DISC_BACKOFF_MS       600000   // discovery backs off once its reasons are this old
#define RP_DISC_EVERY_AP              2   // AP_IDLE: a discovery period every 2nd period ...
#define RP_DISC_EVERY_AP_BO           6   // ... every 6th once backed off
#define RP_DISC_EVERY_SERVE           3   // SERVE: every 3rd ...
#define RP_DISC_EVERY_SERVE_BO        7   // ... every 7th once backed off: 6 x 1.2 + 1.5 = 8.7 s; every
                                          // 6th (7.5 s) would divide 15 s (the period rule)
#define RP_STARVE_FOR_MS         110000   // the sensor-starvation guard: this long ...
#define RP_STARVE_EVERY_MS       600000   // ... at most once per 600 s (its 250 s is the executor's fact)
#define RP_I2B_BUCKET_MS           4000   // I2b's rolling window, in buckets of 4 s ...
#define RP_I2B_BUCKETS               16   // ... 16 of them: 60-64 s, never less than the window
#define RP_DUTY_WARN_PCT             80   // the duty watchdog: BLE scanning below this share of its
#define RP_DUTY_WARN_RUNS             2   // ... expected time in this many summaries in a row warns
#define RP_REFUSE_LOG_MS          10000   // a refused pulse's line at most this often (the summary counts all)
#define RP_STA_MAX   CONFIG_DEFAULT_AP_MAX_CONNECTIONS   // the SoftAP's stations (4, asserted in main.c)

#if CONFIG_APP_RADIO_LAB
/* The G1 lab image (radio_lab.h): the SERVE rung its console keys chose, a period rule that lets the
 * lab's SERVE-C rows through (I1 and I2 hold for them), and contingency K1 (plan 4.4), built only
 * here: G1 decides whether production needs it. */
#define RP_RUNG_NOW              (radio_lab_rung())
#define RP_PERIOD_EXEMPT         (RP_F_DITHER | RP_F_DISC | RP_F_LAB)
#define RP_LAB_K1_MS               1500   // K1: a join assist of at most this ...
#define RP_LAB_K1_EVERY_MS        60000   // ... at most once per station per this
#define RP_LAB_DISC_EVERY_AP06_BO     7   // AP_IDLE at Wi-Fi 0.6 s, discovery backed off: every 7th
                                          // period (8.7 s); every 6th (7.5 s) would divide 15 s
#else
#define RP_RUNG_NOW              RP_SERVE_RUNG
#define RP_PERIOD_EXEMPT         (RP_F_DITHER | RP_F_DISC)
#endif

/* ---------------------------------------------------------
 * The profile table and its invariants, checked at compile time (plan 4.8; I1, I2, I8, the period
 * rule). radio_policy_init() runs the same checks on the table as built (the boot self-test).
 * --------------------------------------------------------- */
#define RP_KC(k)   ((k) == RP_K_C || (k) == RP_K_N)                 // a Coded window (I1)
#define RP_KM(k)   ((k) == RP_K_M)
#define RP_KB(k)   ((k) != RP_K__ && (k) != RP_K_W)                 // BLE runs in it
#define RP_KS(k)   ((k) != RP_K__)                                  // a slot in use
#define RP_N4(k0, k1, k2, k3)               (RP_KS(k0) + RP_KS(k1) + RP_KS(k2) + RP_KS(k3))
#define RP_CNT4(P, k0, k1, k2, k3)          (P(k0) + P(k1) + P(k2) + P(k3))
#define RP_MS4(P, k0, m0, k1, m1, k2, m2, k3, m3) \
    (P(k0) * (m0) + P(k1) * (m1) + P(k2) * (m2) + P(k3) * (m3))
#define RP_JIT(fl, n)        (((fl) & RP_F_DITHER) ? (n) * RP_JITTER_MS : RP_JITTER_MS)
#define RP_NEXT_W(kn, k0)    (((kn) == RP_K__) ? ((k0) == RP_K_W) : ((kn) == RP_K_W))
#define RP_I8_BLE(k, m, kn, k0)  (!RP_KB(k) || ((m) <= RP_I8_BLE_MAX_MS && RP_NEXT_W(kn, k0)))
#define RP_I8_WIFI(k, m)     ((k) != RP_K_W || (m) >= RP_I8_WIFI_MIN_MS)
#define RP_PERIOD_OK(p)      ((p) > 0 && 8000 % (p) != 0 && 15000 % (p) != 0 && 100000 % (p) != 0)
#define RP_SLOT_OK(k, m)     (RP_KS(k) ? (m) > 0 : (m) == 0)
#define RP_COMPACT(k0, k1, k2, k3) \
    (RP_KS(k0) && (RP_KS(k1) || (!RP_KS(k2) && !RP_KS(k3))) && (RP_KS(k2) || !RP_KS(k3)))
#define RP_CODED_IDX(k0, k1, k2, k3) \
    (RP_KC(k0) ? 0 : RP_KC(k1) ? 1 : RP_KC(k2) ? 2 : RP_KC(k3) ? 3 : 0)

#define RP_ASSERT_ROW_(name, fl, k0, m0, k1, m1, k2, m2, k3, m3)                                   \
    _Static_assert(RP_COMPACT(k0, k1, k2, k3) && RP_SLOT_OK(k0, m0) && RP_SLOT_OK(k1, m1) &&      \
                   RP_SLOT_OK(k2, m2) && RP_SLOT_OK(k3, m3), "table: " #name "'s slots in order"); \
    _Static_assert(!((fl) & RP_F_I1) || RP_CNT4(RP_KC, k0, k1, k2, k3) == 1,                       \
                   "I1: " #name " has one Coded window");                                          \
    _Static_assert(!((fl) & RP_F_I1) || RP_MS4(RP_KC, k0, m0, k1, m1, k2, m2, k3, m3) >= RP_W_MIN_MS, \
                   "I1: " #name "'s Coded window covers an advert interval");                      \
    _Static_assert(!((fl) & RP_F_I1) || (m0) + (m1) + (m2) + (m3) -                               \
                   RP_MS4(RP_KC, k0, m0, k1, m1, k2, m2, k3, m3) + RP_JIT(fl, RP_N4(k0, k1, k2, k3)) \
                   <= RP_GAP_MAX_MS, "I1: " #name "'s Coded gap, with jitter");                    \
    _Static_assert(!((fl) & RP_F_I1M) || RP_CNT4(RP_KM, k0, k1, k2, k3) == 1,                      \
                   "I1: " #name " has one 1M window");                                             \
    _Static_assert(!((fl) & RP_F_I1M) || RP_MS4(RP_KM, k0, m0, k1, m1, k2, m2, k3, m3) >= RP_W_MIN_MS, \
                   "I1: " #name "'s 1M window covers an advert interval");                         \
    _Static_assert(!((fl) & RP_F_I1M) || (m0) + (m1) + (m2) + (m3) -                              \
                   RP_MS4(RP_KM, k0, m0, k1, m1, k2, m2, k3, m3) + RP_JIT(fl, RP_N4(k0, k1, k2, k3)) \
                   <= RP_GAP_MAX_MS, "I1: " #name "'s 1M gap, with jitter");                       \
    _Static_assert(!((fl) & RP_F_I8) || (RP_I8_BLE(k0, m0, k1, k0) && RP_I8_BLE(k1, m1, k2, k0) && \
                   RP_I8_BLE(k2, m2, k3, k0) && RP_I8_BLE(k3, m3, RP_K__, k0)),                    \
                   "I8: " #name "'s BLE runs are 600 ms at most, each followed by Wi-Fi");         \
    _Static_assert(!((fl) & RP_F_I8) || (RP_I8_WIFI(k0, m0) && RP_I8_WIFI(k1, m1) &&               \
                   RP_I8_WIFI(k2, m2) && RP_I8_WIFI(k3, m3)), "I8: " #name "'s Wi-Fi slots");      \
    _Static_assert(((fl) & RP_PERIOD_EXEMPT) || RP_PERIOD_OK((m0) + (m1) + (m2) + (m3)),           \
                   "period rule: " #name "'s period divides none of 8, 15 and 100 s");             \
    _Static_assert(RP_MS4(RP_KC, k0, m0, k1, m1, k2, m2, k3, m3) == 0 || (m0) + (m1) + (m2) + (m3) - \
                   RP_MS4(RP_KC, k0, m0, k1, m1, k2, m2, k3, m3) + RP_JIT(fl, RP_N4(k0, k1, k2, k3)) \
                   < RP_BLIND_MAX_MS, "I2: " #name "'s own Coded gap is shorter than a pulse");
#define RP_ASSERT_ROW(name, fl, k0, m0, k1, m1, k2, m2, k3, m3) \
    RP_ASSERT_ROW_(name, fl, RP_K_##k0, m0, RP_K_##k1, m1, RP_K_##k2, m2, RP_K_##k3, m3)
RP_ROWS(RP_ASSERT_ROW)

// Each row's period (ms), for the sequences' period rule.
#define RP_PERIOD_ENUM(name, fl, k0, m0, k1, m1, k2, m2, k3, m3) RP_PERIOD_##name = (m0) + (m1) + (m2) + (m3),
enum { RP_ROWS(RP_PERIOD_ENUM) };
#define RP_SEQ_MS(plain, disc, every) (((every) - 1) * RP_PERIOD_##plain + RP_PERIOD_##disc)
_Static_assert(RP_PERIOD_OK(RP_SEQ_MS(APIDLE, APIDLE_DISC, RP_DISC_EVERY_AP)), "period rule: AP_IDLE with discovery");
_Static_assert(RP_PERIOD_OK(RP_SEQ_MS(APIDLE, APIDLE_DISC, RP_DISC_EVERY_AP_BO)), "period rule: AP_IDLE backed off");
_Static_assert(RP_PERIOD_OK(RP_SEQ_MS(SERVE_A, SERVE_A_DISC, RP_DISC_EVERY_SERVE)), "period rule: SERVE-A with discovery");
_Static_assert(RP_PERIOD_OK(RP_SEQ_MS(SERVE_A, SERVE_A_DISC, RP_DISC_EVERY_SERVE_BO)), "period rule: SERVE-A backed off");
_Static_assert(RP_PERIOD_OK(RP_SEQ_MS(SERVE_B, SERVE_B_DISC, RP_DISC_EVERY_SERVE)), "period rule: SERVE-B with discovery");
_Static_assert(RP_PERIOD_OK(RP_SEQ_MS(SERVE_B, SERVE_B_DISC, RP_DISC_EVERY_SERVE_BO)), "period rule: SERVE-B backed off");
#if CONFIG_APP_RADIO_LAB
_Static_assert(RP_PERIOD_OK(RP_SEQ_MS(SERVE_A, APIDLE_DISC, RP_DISC_EVERY_AP)), "period rule: the lab's AP_IDLE at Wi-Fi 0.6 s");
_Static_assert(RP_PERIOD_OK(RP_SEQ_MS(SERVE_A, APIDLE_DISC, RP_LAB_DISC_EVERY_AP06_BO)), "period rule: the lab's AP_IDLE at Wi-Fi 0.6 s backed off");
_Static_assert(RP_PERIOD_OK(RP_SEQ_MS(SERVE_C, SERVE_C_DISC, RP_DISC_EVERY_SERVE)), "period rule: the lab's SERVE-C with discovery");
_Static_assert(RP_PERIOD_OK(RP_SEQ_MS(SERVE_C, SERVE_C_DISC, RP_DISC_EVERY_SERVE_BO)), "period rule: the lab's SERVE-C backed off");
_Static_assert(RP_LAB_K1_MS >= RP_PULSE_MIN_MS && RP_LAB_K1_MS <= RP_BLIND_MAX_MS, "I2: K1's assist fits the blind budget");
#endif

// I2 and I2b on the pulses (plan 3).
_Static_assert(RP_BLIND_MAX_MS + RP_RECOVERY_MS <= RP_BURST_EDGE_MS, "I2: a pulse and its recovery fit in an edge burst");
_Static_assert(RP_RECOVERY_MS >= 2 * RP_L_MS, "I2: the recovery spans two advert intervals");
_Static_assert(RP_PERIOD_RECOVERY >= RP_RECOVERY_MS, "I2: the recovery row is the recovery window");
_Static_assert(RP_RETRY_MS <= RP_BLIND_MAX_MS && RP_LIST_MAX_MS <= RP_BLIND_MAX_MS &&
               RP_CONNECT_MS <= RP_CONNECT_LR_MS && RP_CONNECT_LR_MS <= RP_BLIND_MAX_MS &&
               RP_PULSE_MIN_MS <= RP_CONNECT_MS, "I2: every pulse fits the blind budget");
_Static_assert(RP_BLIND_MAX_MS <= RP_I2B_BLIND_MAX_MS, "I2b: a pulse fits the per-minute budget");
_Static_assert(RP_I2B_BUCKET_MS * (RP_I2B_BUCKETS - 1) >= RP_I2B_WINDOW_MS, "I2b: the buckets span the rolling window");
_Static_assert(RP_JOIN_LEASE_TAIL_MS < RP_BLIND_MAX_MS, "a join assist can outlast its lease");
_Static_assert(RP_STA_MAX >= 1 && RP_STA_MAX <= 10, "the SoftAP's station table");

#define RP_ROW_INIT(name, fl, k0, m0, k1, m1, k2, m2, k3, m3)                                    \
    [RP_ROW_##name] = { { RP_K_##k0, RP_K_##k1, RP_K_##k2, RP_K_##k3 }, { m0, m1, m2, m3 },        \
                        (uint8_t)RP_N4(RP_K_##k0, RP_K_##k1, RP_K_##k2, RP_K_##k3), (fl),          \
                        (uint8_t)RP_CODED_IDX(RP_K_##k0, RP_K_##k1, RP_K_##k2, RP_K_##k3) },
static const rp_row_t k_rows[RP_ROW_COUNT] = { RP_ROWS(RP_ROW_INIT) };
#define RP_ROW_NAME(name, fl, k0, m0, k1, m1, k2, m2, k3, m3) [RP_ROW_##name] = #name,
static const char *const k_row_names[RP_ROW_COUNT] = { RP_ROWS(RP_ROW_NAME) };

static const char *const k_pulse_names[RP_PULSE_COUNT] = {
    [RP_PULSE_NONE] = "none", [RP_PULSE_JOIN] = "JOIN", [RP_PULSE_SUBMIT] = "SUBMIT",
    [RP_PULSE_RETRY] = "RETRY", [RP_PULSE_LIST] = "LIST", [RP_PULSE_CONNECT] = "CONNECT",
};

/* ---------------------------------------------------------
 * Small helpers. Ticks are FreeRTOS ticks (10 ms at 100 Hz); 0 means "none" wherever a tick is a
 * stamp, so stamps are forced non-zero.
 * --------------------------------------------------------- */
static inline uint32_t rp_ms(TickType_t t)
{
    return (uint32_t)t * portTICK_PERIOD_MS;
}

static inline TickType_t rp_nz(TickType_t t)
{
    return t ? t : 1;
}

// The stamp is set and less than ms ago. A stamp left for 2^31 ticks reads as recent again, for
// at most ms, once every 2^32 ticks (497 days at 100 Hz).
static inline bool rp_within(TickType_t stamp, TickType_t now, uint32_t ms)
{
    return stamp != 0 && (now - stamp) < pdMS_TO_TICKS(ms);
}

static inline uint32_t rp_min(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

/* ---------------------------------------------------------
 * Facts from the Wi-Fi side (one writer each, see radio_policy.h). Plain stores: each is a byte or
 * an aligned tick, read lock-free by the executor, which re-reads them on every pass.
 * --------------------------------------------------------- */
#define RP_W_AP_UP  0x01
#define RP_W_STA_IP 0x02
static volatile uint8_t s_wifi = 0;              // RP_W_* (the Wi-Fi writer)
static volatile bool s_sta_attempt = false;      // wifi_manager task
static volatile bool s_submit = false;           // wifi_manager task
static volatile TickType_t s_page_tick = 0;      // httpd task: the page in use
static volatile TickType_t s_hot_tick = 0;       // httpd task: a hot event (SERVE-A-thin)
static volatile TickType_t s_prov_until = 0;     // default event loop: provisional SERVE ends then

/* The SoftAP's stations (plan 4.1's station table), for the join assist and provisional SERVE.
 * Written by the default event loop (join, leave, lease) and the httpd task (a station's first page
 * or 302), read by the executor: every access is under s_sta_lock, a spinlock held for one table
 * scan, never while logging. Per-MAC memory (assist spacing, provisional SERVE) is best effort: a
 * phone that rotates its MAC is a new station, and I2b bounds the radio whatever the MACs. */
#define RP_STA_FREE   0
#define RP_STA_JOINED 1
#define RP_STA_LEFT   2
typedef struct {
    uint8_t mac[6];
    uint8_t state;            // RP_STA_*
    bool extra;               // this join's one extra assist was asked
    uint32_t ip;              // its lease, network byte order (0: none)
    TickType_t join_at;       // this join (0: not seen joining)
    TickType_t lease_at;      // its lease since this join (0: none)
    TickType_t probe_at;      // its first 302 or page since this join (0: none)
    TickType_t assist_at;     // this MAC's last assist (kept across joins; 0: none)
    TickType_t assist_gap;    // ... and the spacing the next one needs
    TickType_t prov_at;       // this MAC's last provisional SERVE (0: none)
} rp_sta_t;
static rp_sta_t s_sta[RP_STA_MAX];
static portMUX_TYPE s_sta_lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t s_joins = 0;           // joins since the summary (under s_sta_lock)
static uint32_t s_lease_ms_max = 0;    // the longest join -> lease since the summary (under s_sta_lock)

/* The pulse requests (the grant protocol). A requester sets PENDING; the executor sets ON, FREE,
 * REFUSED or, at a pulse's end, IDLE; the requester ends its own pulse (end) or withdraws a pending
 * one. Under s_req_lock (any task). */
typedef struct {
    uint8_t state;        // rp_grant_t
    bool end;             // the requester ended its pulse
    int8_t sta;           // JOIN: the station's index
#if CONFIG_APP_RADIO_LAB
    bool k1;              // JOIN: asked by the lab's K1 (a station's first DNS query): RP_LAB_K1_MS at most
#endif
    TickType_t at;        // when it was asked
} rp_req_t;
static rp_req_t s_req[RP_PULSE_COUNT];
static portMUX_TYPE s_req_lock = portMUX_INITIALIZER_UNLOCKED;

/* The leak-response overlay (plan 4.1, D5): written by the valve command task only
 * (radio_policy_note_lr()), read lock-free by the executor. */
static volatile bool s_lr_on = false;           // the overlay runs
static volatile bool s_lr_trigger = false;      // a leak response is pending
static volatile bool s_lr_incident = false;     // an incident is latched
static volatile TickType_t s_lr_start = 0;      // the episode's start (0: none)

static bool s_pinned = false;                   // the self-test failed: N_CODED only (init, then read-only)
static volatile bool s_live = false;            // the executor runs (radio_policy_init() done): until then
                                                // no BLE scan runs, and every request is answered FREE

/* ---------------------------------------------------------
 * The executor's side: the ble_leak_scan task only, so nothing here locks.
 * --------------------------------------------------------- */
typedef struct {
    rp_ble_facts_t f;           // the facts of the last pass
    uint8_t mode;               // the mode now (rp_mode_t)
    uint8_t logged;             // the mode last printed (RP_MODE_COUNT: none)
    uint8_t period_row;         // AP modes: the row of the current period
    uint16_t period_n;          // ... and the periods run with that row set (discovery cadence)
    bool backoff;               // discovery is backed off (for its line)
    bool i2b_noted;             // a claim waits for I2b: printed once per wait
    uint8_t pulse;              // the pulse running (rp_pulse_t)
    uint8_t starve_kind;        // the guard's kind (RP_STARVED_*)
    int8_t join_sta;            // the running JOIN's station
    int8_t extra_sta;           // a station owed its one extra assist (-1: none)
    bool join_attempt;          // a STA attempt was in flight when that JOIN began
    TickType_t hunt_since;      // the valve unlinked and hunted since (0: not)
    TickType_t starve_until;    // the sensor-starvation guard runs until (0: not)
    TickType_t starve_next;     // ... and may start again from (0: any time)
    TickType_t pulse_at;        // the running pulse's start ...
    TickType_t pulse_deadline;  // ... and deadline
    const char *pulse_why;      // ... and why it ended (for its line)
    TickType_t lr_join_at;      // the last assist under a leak response
    uint32_t prof_ms;           // I2b: profile time since the last pulse, at most a window ...
    uint32_t space_ms;          // ... and the spacing the next pulse needs
    uint16_t bucket[RP_I2B_BUCKETS];   // I2b: pulse ms per RP_I2B_BUCKET_MS ...
    uint8_t bucket_i;           // ... the current bucket ...
    TickType_t bucket_at;       // ... and its start
    // The summary's counters, reset every 60 s.
    TickType_t acct_at;
    uint32_t mode_ms[RP_MODE_COUNT];
    uint32_t want_ms, on_ms, recovery_ms, lr_ms;
    uint32_t blind_ms;          // pulse time
    uint16_t pulse_n[RP_PULSE_COUNT];
    uint16_t refused;
    TickType_t refuse_logged;   // the last refusal line (one per RP_REFUSE_LOG_MS at most)
    uint32_t i2b_peak;          // the most pulse time seen in a window
    uint32_t gap_max;           // the longest Coded gap (I2 monitor) ...
    uint16_t gap_over;          // ... and the gaps over RP_BLIND_MAX_MS + RP_JITTER_MS
    uint8_t low_duty;           // summaries in a row below RP_DUTY_WARN_PCT
} rp_exec_t;
static rp_exec_t s_x;   // zeroed (.bss); radio_policy_init() sets the fields whose "none" is not 0

#if CONFIG_APP_RADIO_LAB
/* The lab's K1: per entry of the station table, the MAC it last ran for and when (once per station
 * per RP_LAB_K1_EVERY_MS), and the assists asked since the summary. Under s_sta_lock. The lab's
 * functions are at the end of this file. */
static struct {
    uint8_t mac[6];
    TickType_t at;
} s_lab_k1[RP_STA_MAX];
static uint16_t s_lab_k1_n = 0;
static const char *lab_seq_check(void);
static bool lab_mode_line(uint8_t m);
static void lab_k1_dns(uint32_t ip);
#endif

/* =========================================================
 * The boot self-test (plan 4.8): the asserts' checks on the table as built, and the sequences
 * ========================================================= */
// NULL when the row holds, else what fails.
static const char *row_check(const rp_row_t *r)
{
    if (r->n < 1 || r->n > 4)
        return "slot count";
    uint32_t period = 0, coded = 0, m1 = 0;
    unsigned n_coded = 0, n_m = 0, coded_at = 0;
    bool coded_seen = false;
    for (int i = 0; i < 4; i++) {
        uint8_t k = r->kind[i];
        if ((i < r->n) != RP_KS(k) || (RP_KS(k) ? r->ms[i] == 0 : r->ms[i] != 0) || k > RP_K_W)
            return "slots out of order";
        period += r->ms[i];
        if (RP_KC(k)) {
            n_coded++;
            coded += r->ms[i];
        }
        if (RP_KM(k)) {
            n_m++;
            m1 += r->ms[i];
        }
        if (RP_KC(k) && !coded_seen) {
            coded_seen = true;
            coded_at = (unsigned)i;
        }
    }
    if (r->coded != coded_at)
        return "Coded window index";
    uint32_t jit = (r->flags & RP_F_DITHER) ? (uint32_t)r->n * RP_JITTER_MS : RP_JITTER_MS;
    if ((r->flags & RP_F_I1) &&
        (n_coded != 1 || coded < RP_W_MIN_MS || period - coded + jit > RP_GAP_MAX_MS))
        return "I1 (Coded)";
    if ((r->flags & RP_F_I1M) && (n_m != 1 || m1 < RP_W_MIN_MS || period - m1 + jit > RP_GAP_MAX_MS))
        return "I1 (1M)";
    if (r->flags & RP_F_I8) {
        for (int i = 0; i < r->n; i++) {
            uint8_t k = r->kind[i];
            if (RP_KB(k) && (r->ms[i] > RP_I8_BLE_MAX_MS || r->kind[(i + 1) % r->n] != RP_K_W))
                return "I8 (a BLE run)";
            if (k == RP_K_W && r->ms[i] < RP_I8_WIFI_MIN_MS)
                return "I8 (a Wi-Fi slot)";
        }
    }
    if (!(r->flags & RP_PERIOD_EXEMPT) && !RP_PERIOD_OK(period))
        return "the period rule";
    if (coded > 0 && period - coded + jit >= RP_BLIND_MAX_MS)
        return "I2 (its own gap)";
    return NULL;
}

static bool seq_ok(uint8_t plain, uint8_t disc, unsigned every)
{
    uint32_t p = 0, d = 0;
    for (int i = 0; i < 4; i++) {
        p += k_rows[plain].ms[i];
        d += k_rows[disc].ms[i];
    }
    return RP_PERIOD_OK((every - 1) * p + d);
}

void radio_policy_init(void)
{
    s_x.mode = RP_MODE_PAUSED;
    s_x.logged = RP_MODE_COUNT;
    s_x.period_row = RP_ROW_NONE;
    s_x.join_sta = -1;
    s_x.extra_sta = -1;

    const char *why = NULL;
    const char *row = NULL;
    for (int i = 0; i < RP_ROW_COUNT && why == NULL; i++) {
        why = row_check(&k_rows[i]);
        row = k_row_names[i];
    }
    if (why == NULL) {
        row = "RECOVERY";
        const rp_row_t *r = &k_rows[RP_ROW_RECOVERY];
        if (r->n != 1 || r->kind[0] != RP_K_C || r->ms[0] < RP_RECOVERY_MS)
            why = "not one Coded window of the recovery's length";
    }
    if (why == NULL) {
        row = "a discovery sequence";
        bool b = (RP_SERVE_RUNG == RP_RUNG_SERVE_B);
        uint8_t sp = b ? RP_ROW_SERVE_B : RP_ROW_SERVE_A, sd = b ? RP_ROW_SERVE_B_DISC : RP_ROW_SERVE_A_DISC;
        if (!seq_ok(RP_ROW_APIDLE, RP_ROW_APIDLE_DISC, RP_DISC_EVERY_AP) ||
            !seq_ok(RP_ROW_APIDLE, RP_ROW_APIDLE_DISC, RP_DISC_EVERY_AP_BO) ||
            !seq_ok(sp, sd, RP_DISC_EVERY_SERVE) || !seq_ok(sp, sd, RP_DISC_EVERY_SERVE_BO))
            why = "the period rule";
    }
#if CONFIG_APP_RADIO_LAB
    if (why == NULL) {
        row = "the lab's row sets";
        why = lab_seq_check();
    }
#endif
    if (why != NULL) {
        s_pinned = true;
        ESP_LOGE(RP_TAG, "Profile self-test FAILED (%s: %s) - pinned to NORMAL (N_CODED), no Wi-Fi pulses",
                 row, why);
    } else {
#if CONFIG_APP_RADIO_LAB
        ESP_LOGI(RP_TAG, "Profile self-test passed: %d rows hold I1 and I2, all but the lab's two SERVE-C rows I8 and the period rule (SERVE rung %s, the lab's)",
                 (int)RP_ROW_COUNT, radio_lab_rung_name(radio_lab_rung()));
#else
        ESP_LOGI(RP_TAG, "Profile self-test passed: %d rows hold I1, I2, I8 and the period rule (SERVE rung %s)",
                 (int)RP_ROW_COUNT,
                 RP_SERVE_RUNG == RP_RUNG_SERVE_B ? "SERVE-B" :
                 RP_SERVE_RUNG == RP_RUNG_SERVE_A_THIN ? "SERVE-A-thin" : "SERVE-A");
#endif
    }
    s_live = true;   // last: requests from now on wait for the executor's answer
}

bool radio_policy_pinned(void)
{
    return s_pinned;
}

const rp_row_t *radio_policy_row(uint8_t row)
{
    return &k_rows[row < RP_ROW_COUNT ? row : RP_ROW_N_CODED];
}

const char *radio_policy_row_name(uint8_t row)
{
    return row < RP_ROW_COUNT ? k_row_names[row] : "none";
}

uint32_t radio_policy_row_gap_ms(uint8_t row)
{
    if (row >= RP_ROW_COUNT)
        return 0;
    const rp_row_t *r = &k_rows[row];
    uint32_t period = 0, coded = 0;
    for (int i = 0; i < r->n; i++) {
        period += r->ms[i];
        if (RP_KC(r->kind[i]))
            coded += r->ms[i];
    }
    return coded ? period - coded : 0;
}

/* =========================================================
 * Requests (any task)
 * ========================================================= */
// Asks for a pulse of `kind` (for JOIN: station `sta`), unless one of that kind is pending or on.
// True when it asked. Before the executor runs (no BLE device, NimBLE not started or failed) no
// BLE scan runs and nobody would answer: FREE at once, so a requester never waits for nothing.
static bool req_ask(rp_pulse_t kind, int8_t sta)
{
    TickType_t now = rp_nz(xTaskGetTickCount());
    bool live = s_live;
    taskENTER_CRITICAL(&s_req_lock);
    bool busy = (s_req[kind].state == RP_GRANT_PENDING || s_req[kind].state == RP_GRANT_ON);
    if (!busy) {
        s_req[kind].state = live ? RP_GRANT_PENDING : RP_GRANT_FREE;
        s_req[kind].end = false;
        s_req[kind].sta = sta;
        s_req[kind].at = now;
#if CONFIG_APP_RADIO_LAB
        s_req[kind].k1 = false;
#endif
    }
    taskEXIT_CRITICAL(&s_req_lock);
    if (!busy && live)
        app_ble_leak_kick();
    return !busy;
}

// The executor settles a request.
static void req_set(rp_pulse_t kind, rp_grant_t state)
{
    taskENTER_CRITICAL(&s_req_lock);
    s_req[kind].state = state;
    s_req[kind].end = false;
    taskEXIT_CRITICAL(&s_req_lock);
}

// The executor grants a request only if it is still pending: its requester may have withdrawn it
// (radio_policy_pulse_end()) since the executor read it. True when granted.
static bool req_grant(rp_pulse_t kind)
{
    taskENTER_CRITICAL(&s_req_lock);
    bool ok = (s_req[kind].state == RP_GRANT_PENDING);
    if (ok) {
        s_req[kind].state = RP_GRANT_ON;
        s_req[kind].end = false;
    }
    taskEXIT_CRITICAL(&s_req_lock);
    return ok;
}

void radio_policy_pulse_request(rp_pulse_t kind)
{
    if (kind != RP_PULSE_RETRY && kind != RP_PULSE_LIST)
        return;   // JOIN and SUBMIT come from their facts; CONNECT is the executor's own
    (void)req_ask(kind, -1);
}

rp_grant_t radio_policy_pulse_state(rp_pulse_t kind)
{
    if (kind <= RP_PULSE_NONE || kind >= RP_PULSE_COUNT)
        return RP_GRANT_IDLE;
    taskENTER_CRITICAL(&s_req_lock);
    rp_grant_t st = (rp_grant_t)s_req[kind].state;
    taskEXIT_CRITICAL(&s_req_lock);
    return st;
}

rp_grant_t radio_policy_pulse_wait(rp_pulse_t kind, uint32_t max_ms)
{
    TickType_t t0 = xTaskGetTickCount();
    for (;;) {
        rp_grant_t st = radio_policy_pulse_state(kind);
        if (st != RP_GRANT_PENDING || (xTaskGetTickCount() - t0) >= pdMS_TO_TICKS(max_ms))
            return st;
        vTaskDelay(1);
    }
}

void radio_policy_pulse_end(rp_pulse_t kind)
{
    if (kind <= RP_PULSE_NONE || kind >= RP_PULSE_CONNECT)
        return;
    taskENTER_CRITICAL(&s_req_lock);
    if (s_req[kind].state == RP_GRANT_ON)
        s_req[kind].end = true;           // the executor ends it on its next pass
    else if (s_req[kind].state == RP_GRANT_PENDING || s_req[kind].state == RP_GRANT_FREE ||
             s_req[kind].state == RP_GRANT_REFUSED)
        s_req[kind].state = RP_GRANT_IDLE;
    taskEXIT_CRITICAL(&s_req_lock);
    app_ble_leak_kick();
}

/* =========================================================
 * Wi-Fi facts (see radio_policy.h for the writers)
 * ========================================================= */
void radio_policy_note_wifi(bool ap_up, bool sta_ip)
{
    uint8_t w = (uint8_t)((ap_up ? RP_W_AP_UP : 0) | (sta_ip ? RP_W_STA_IP : 0));
    if (w != s_wifi) {
        s_wifi = w;
        app_ble_leak_kick();
    }
}

void radio_policy_note_sta_attempt(bool in_flight)
{
    s_sta_attempt = in_flight;
}

void radio_policy_note_submit(bool in_flight)
{
    s_submit = in_flight;
    if (in_flight) {
        (void)req_ask(RP_PULSE_SUBMIT, -1);
    } else {
        radio_policy_pulse_end(RP_PULSE_SUBMIT);
    }
}

// Under s_sta_lock: the entry holding this MAC, else (claim) a free one, else the left station
// that joined first, else (a leave not seen yet) the joined one that joined first. -1: none.
static int sta_find_locked(const uint8_t *mac, bool claim)
{
    int spare = -1;
    for (int i = 0; i < RP_STA_MAX; i++) {
        if (s_sta[i].state != RP_STA_FREE && memcmp(s_sta[i].mac, mac, 6) == 0)
            return i;
    }
    if (!claim)
        return -1;
    for (int i = 0; i < RP_STA_MAX; i++) {
        if (s_sta[i].state == RP_STA_FREE)
            return i;
        if (spare < 0 || (s_sta[i].state == RP_STA_LEFT && s_sta[spare].state != RP_STA_LEFT) ||
            (s_sta[i].state == s_sta[spare].state &&
             (int32_t)(s_sta[i].join_at - s_sta[spare].join_at) < 0))
            spare = i;
    }
    return spare;
}

void radio_policy_station_joined(const uint8_t mac[6])
{
    TickType_t now = rp_nz(xTaskGetTickCount());
    bool ask = false;
    taskENTER_CRITICAL(&s_sta_lock);
    int i = sta_find_locked(mac, true);
    if (i >= 0) {
        rp_sta_t *s = &s_sta[i];
        if (s->state == RP_STA_FREE || memcmp(s->mac, mac, 6) != 0) {
            memset(s, 0, sizeof(*s));
            memcpy(s->mac, mac, 6);
        }
        s->state = RP_STA_JOINED;
        s->join_at = now;
        s->lease_at = 0;
        s->probe_at = 0;
        s->ip = 0;
        s->extra = false;
        ask = (s->assist_at == 0 || (now - s->assist_at) >= s->assist_gap);
    }
    if (s_joins < UINT16_MAX)
        s_joins++;
    taskEXIT_CRITICAL(&s_sta_lock);
#if CONFIG_APP_RADIO_LAB
    ask = ask && radio_lab_join_on();   // the lab's join-assist switch (G1)
#endif
    if (ask)
        (void)req_ask(RP_PULSE_JOIN, (int8_t)i);   // one at a time: a second station's join waits its turn
    else
        app_ble_leak_kick();
}

void radio_policy_station_left(const uint8_t mac[6])
{
    taskENTER_CRITICAL(&s_sta_lock);
    int i = sta_find_locked(mac, false);
    if (i >= 0)
        s_sta[i].state = RP_STA_LEFT;
    taskEXIT_CRITICAL(&s_sta_lock);
    app_ble_leak_kick();
}

void radio_policy_station_leased(const uint8_t mac[6], uint32_t ip)
{
    TickType_t now = rp_nz(xTaskGetTickCount());
    taskENTER_CRITICAL(&s_sta_lock);
    for (int k = 0; k < RP_STA_MAX; k++) {
        if (s_sta[k].ip == ip && memcmp(s_sta[k].mac, mac, 6) != 0)
            s_sta[k].ip = 0;   // an earlier holder of the address
    }
    int i = sta_find_locked(mac, true);
    if (i >= 0) {
        rp_sta_t *s = &s_sta[i];
        if (s->state == RP_STA_FREE || memcmp(s->mac, mac, 6) != 0) {
            memset(s, 0, sizeof(*s));   // its join was not seen
            memcpy(s->mac, mac, 6);
        }
        s->state = RP_STA_JOINED;
        s->ip = ip;
        s->lease_at = now;
        if (s->join_at != 0 && rp_ms(now - s->join_at) > s_lease_ms_max)
            s_lease_ms_max = rp_ms(now - s->join_at);
        if (s->prov_at == 0 || (now - s->prov_at) >= pdMS_TO_TICKS(RP_PROV_SERVE_EVERY_MS)) {
            s->prov_at = now;
            s_prov_until = rp_nz(now + pdMS_TO_TICKS(RP_PROV_SERVE_MS));
        }
    }
    taskEXIT_CRITICAL(&s_sta_lock);
    app_ble_leak_kick();
}

void radio_policy_portal_activity(http_app_activity_t kind, uint32_t client_ip)
{
    if (kind == HTTP_APP_ACT_DNS || (unsigned)kind >= HTTP_APP_ACT_COUNT) {
#if CONFIG_APP_RADIO_LAB
        if (kind == HTTP_APP_ACT_DNS)
            lab_k1_dns(client_ip);   // the lab's contingency K1 (G1 decides whether production needs it)
#endif
        return;   // the dns_server task: nothing here keys on DNS (contingency K1 is not built)
    }
    TickType_t now = rp_nz(xTaskGetTickCount());
    // The executor is woken when the mode or the SERVE rung's row may change (the page in use
    // again: AP_IDLE -> SERVE; a hot event for SERVE-A-thin), not at every request.
    bool wake = false;
    if (kind == HTTP_APP_ACT_PAGE || kind == HTTP_APP_ACT_API_USER || kind == HTTP_APP_ACT_API_BG) {
        wake = !rp_within(s_page_tick, now, RP_PAGE_ACTIVE_MS);
        s_page_tick = now;
    }
    if (kind == HTTP_APP_ACT_PAGE || kind == HTTP_APP_ACT_API_USER || kind == HTTP_APP_ACT_PROBE_302) {
        wake = wake || (RP_RUNG_NOW == RP_RUNG_SERVE_A_THIN && !rp_within(s_hot_tick, now, RP_HOT_MS));
        s_hot_tick = now;
    }
    if (wake)
        app_ble_leak_kick();
    if ((kind == HTTP_APP_ACT_PAGE || kind == HTTP_APP_ACT_PROBE_302) && client_ip != 0) {
        bool first = false;
        taskENTER_CRITICAL(&s_sta_lock);
        for (int i = 0; i < RP_STA_MAX; i++) {
            if (s_sta[i].state == RP_STA_JOINED && s_sta[i].ip == client_ip && s_sta[i].probe_at == 0) {
                s_sta[i].probe_at = now;
                first = true;
            }
        }
        taskEXIT_CRITICAL(&s_sta_lock);
        if (first)
            app_ble_leak_kick();
    }
}

void radio_policy_stations_prune(const wifi_sta_list_t *list)
{
    TickType_t now = xTaskGetTickCount();
    bool gone = false;
    taskENTER_CRITICAL(&s_sta_lock);
    for (int i = 0; i < RP_STA_MAX; i++) {
        rp_sta_t *s = &s_sta[i];
        if (s->state != RP_STA_JOINED)
            continue;
        TickType_t seen = s->join_at ? s->join_at : s->lease_at;   // a station whose join was not seen
        if (seen != 0 && (now - seen) < pdMS_TO_TICKS(RP_STA_PRUNE_MS))
            continue;
        bool listed = false;
        for (int k = 0; list != NULL && k < list->num && k < ESP_WIFI_MAX_CONN_NUM && !listed; k++)
            listed = (memcmp(list->sta[k].mac, s->mac, 6) == 0);
        if (!listed) {
            s->state = RP_STA_LEFT;
            gone = true;
        }
    }
    taskEXIT_CRITICAL(&s_sta_lock);
    if (gone)
        app_ble_leak_kick();
}

bool radio_policy_join_settling(void)
{
    TickType_t now = xTaskGetTickCount();
    bool joining = false;
    taskENTER_CRITICAL(&s_sta_lock);
    for (int i = 0; i < RP_STA_MAX && !joining; i++) {
        joining = (s_sta[i].state == RP_STA_JOINED && s_sta[i].lease_at == 0 &&
                   rp_within(s_sta[i].join_at, now, RP_JOIN_SETTLE_MS));
    }
    taskEXIT_CRITICAL(&s_sta_lock);
    if (!joining) {
        taskENTER_CRITICAL(&s_req_lock);
        joining = (s_req[RP_PULSE_JOIN].state == RP_GRANT_PENDING || s_req[RP_PULSE_JOIN].state == RP_GRANT_ON);
        taskEXIT_CRITICAL(&s_req_lock);
    }
    return joining;
}

/* =========================================================
 * The leak-response overlay (valve command task)
 * ========================================================= */
rp_lr_t radio_policy_note_lr(bool trigger, bool episode, bool incident)
{
    TickType_t now = xTaskGetTickCount();
    rp_lr_t r = { 0 };
    if (trigger && s_lr_start == 0) {
        s_lr_start = rp_nz(now);
        r.started = true;
    }
    TickType_t start = s_lr_start;
    r.ran_s = start ? (uint32_t)((now - start) / configTICK_RATE_HZ) : 0;
    r.overlay = trigger && start != 0 && (now - start) < pdMS_TO_TICKS(RP_LR_OVERLAY_CAP_MS);
    r.changed = (r.overlay != s_lr_on);
    s_lr_trigger = trigger;
    s_lr_incident = incident;
    s_lr_on = r.overlay;
    if (!episode)
        s_lr_start = 0;   // the episode is over: the next trigger starts a new cap
    if (r.changed)
        app_ble_leak_kick();
    return r;
}

bool radio_policy_lr_overlay(void)
{
    return s_lr_on;
}

bool radio_policy_lr_pending(void)
{
    return s_lr_trigger;
}

/* =========================================================
 * The executor's side (ble_leak_scan task)
 * ========================================================= */

// I2b's rolling window: the buckets advanced to now.
static void i2b_advance(TickType_t now)
{
    if (s_x.bucket_at == 0 || (now - s_x.bucket_at) >= pdMS_TO_TICKS(RP_I2B_BUCKET_MS * RP_I2B_BUCKETS)) {
        memset(s_x.bucket, 0, sizeof(s_x.bucket));
        s_x.bucket_i = 0;
        s_x.bucket_at = rp_nz(now);
        return;
    }
    while ((now - s_x.bucket_at) >= pdMS_TO_TICKS(RP_I2B_BUCKET_MS)) {
        s_x.bucket_i = (uint8_t)((s_x.bucket_i + 1) % RP_I2B_BUCKETS);
        s_x.bucket[s_x.bucket_i] = 0;
        s_x.bucket_at += pdMS_TO_TICKS(RP_I2B_BUCKET_MS);
    }
}

// Pulse time in the rolling window (60-64 s: never less than the last 60 s).
static uint32_t i2b_used(void)
{
    uint32_t sum = 0;
    for (int i = 0; i < RP_I2B_BUCKETS; i++)
        sum += s_x.bucket[i];
    return sum;
}

static bool serve_now(TickType_t now)
{
    return rp_within(s_page_tick, now, RP_PAGE_ACTIVE_MS) || s_submit ||
           (s_prov_until != 0 && (int32_t)(s_prov_until - now) > 0);
}

// The mode's line (plan 4.7: one INFO line per mode change).
static void mode_line(uint8_t m)
{
    if (m == s_x.logged)
        return;
    s_x.logged = m;
#if CONFIG_APP_RADIO_LAB
    if (lab_mode_line(m))
        return;   // the lab's SERVE and AP_IDLE lines (its rung, its AP_IDLE slot)
#endif
    const char *serve_rung = RP_SERVE_RUNG == RP_RUNG_SERVE_B ? "Coded 0.6 s / Wi-Fi 1.2 s (rung SERVE-B)" :
                             RP_SERVE_RUNG == RP_RUNG_SERVE_A_THIN ? "Coded 0.6 s / Wi-Fi 0.6 s for 10 s after a page or user request, else as AP_IDLE (rung SERVE-A-thin)" :
                             "Coded 0.6 s / Wi-Fi 0.6 s (rung SERVE-A)";
    switch (m) {
    case RP_MODE_PAUSED:
        ESP_LOGI(RP_TAG, "Mode PAUSED (NimBLE not synced) - no BLE scan");
        break;
    case RP_MODE_BLE_IDLE:
        ESP_LOGI(RP_TAG, "Mode BLE_IDLE (no BLE leak sensor listed; no valve, or its link is up) - no BLE scan");
        break;
    case RP_MODE_LR_AP:
        ESP_LOGW(RP_TAG, "Mode LR_AP (a leak response, the valve not linked; SoftAP up, STA not connected): 1M 1.2 s / Coded 0.6 s in the first 30 s, then 1M 0.6 / Wi-Fi 0.3 / Coded 0.6 / Wi-Fi 0.3 s");
        break;
    case RP_MODE_NORMAL_LR:
        ESP_LOGW(RP_TAG, "Mode NORMAL_LR (a leak response, the valve not linked)");
        break;
    case RP_MODE_NORMAL:
        ESP_LOGI(RP_TAG, "Mode NORMAL (SoftAP down, or STA connected)%s", s_pinned ? " - pinned" : "");
        break;
    case RP_MODE_SERVE:
        ESP_LOGI(RP_TAG, "Mode SERVE (SoftAP up, STA not connected; a setup page in use, a Connect or a new lease): %s, discovery every %d periods",
                 serve_rung, RP_DISC_EVERY_SERVE);
        break;
    case RP_MODE_AP_IDLE:
        ESP_LOGI(RP_TAG, "Mode AP_IDLE (SoftAP up, STA not connected): Coded 0.6 s / Wi-Fi 0.3 s, discovery every %d periods",
                 RP_DISC_EVERY_AP);
        break;
    default:
        break;
    }
}

// The sensor-starvation guard (plan 4.2): a sensor unheard >= 250 s gets AP_IDLE density with
// discovery suspended (AP_K1M for a 1M or unknown-PHY sensor) for 110 s, at most once per 600 s.
// AP modes only: NORMAL scans Coded at 80 % anyway.
static void guard_update(TickType_t now, uint8_t m)
{
    if (s_x.starve_until != 0 && (int32_t)(now - s_x.starve_until) >= 0) {
        s_x.starve_until = 0;
        ESP_LOGI(RP_TAG, "Sensor-starvation guard over");
    }
    if ((m == RP_MODE_SERVE || m == RP_MODE_AP_IDLE) && s_x.f.starved != RP_STARVED_NONE &&
        s_x.starve_until == 0 && (s_x.starve_next == 0 || (int32_t)(now - s_x.starve_next) >= 0)) {
        s_x.starve_until = rp_nz(now + pdMS_TO_TICKS(RP_STARVE_FOR_MS));
        s_x.starve_next = rp_nz(now + pdMS_TO_TICKS(RP_STARVE_EVERY_MS));
        s_x.starve_kind = s_x.f.starved;
        ESP_LOGW(RP_TAG, "Sensor-starvation guard: a sensor unheard 250 s or more (%s) - %s for %d s",
                 s_x.starve_kind == RP_STARVED_K1M ? "on 1M, or its PHY unknown" : "on Coded",
                 s_x.starve_kind == RP_STARVED_K1M ? "AP_K1M" : "AP_IDLE density, no discovery",
                 RP_STARVE_FOR_MS / 1000);
    }
}

rp_mode_t radio_policy_exec_mode(const rp_ble_facts_t *f, TickType_t now)
{
    s_x.f = *f;
    uint8_t w = s_wifi;
    bool windowed = (w & RP_W_AP_UP) && !(w & RP_W_STA_IP);
    bool lr = s_lr_on;
    rp_mode_t m;
    if (!f->synced)
        m = RP_MODE_PAUSED;
    else if (s_pinned)
        m = RP_MODE_NORMAL;
    else if (f->sensors == 0 && (!f->valve || f->valve_linked))
        m = RP_MODE_BLE_IDLE;
    else if (lr)
        m = windowed ? RP_MODE_LR_AP : RP_MODE_NORMAL_LR;
    else if (!windowed)
        m = RP_MODE_NORMAL;
#if CONFIG_APP_RADIO_LAB
    else if (serve_now(now) && RP_RUNG_NOW != RP_RUNG_APIDLE)
        m = RP_MODE_SERVE;   // the lab's AP_IDLE-density rung has no SERVE mode (D8 (c))
#else
    else if (serve_now(now))
        m = RP_MODE_SERVE;
#endif
    else
        m = RP_MODE_AP_IDLE;

    // How long the valve has been hunted (discovery's back-off): from the first pass it is wanted
    // unlinked until it links or is unprovisioned, whatever its claims do meanwhile.
    if (!f->valve || f->valve_linked)
        s_x.hunt_since = 0;
    else if (f->valve_hunt && s_x.hunt_since == 0)
        s_x.hunt_since = rp_nz(now);

    guard_update(now, m);
    if (m != s_x.mode) {
        s_x.mode = (uint8_t)m;
        s_x.period_row = RP_ROW_NONE;
        s_x.period_n = 0;
        mode_line(m);
    }
    return m;
}

// Discovery backs off (every 6th / 7th period) once each of its reasons is 10 min old: a sensor's
// PHY unknown since it was listed, the valve hunted unlinked. Never while an incident is latched.
static bool disc_backoff(TickType_t now)
{
    const rp_ble_facts_t *f = &s_x.f;
    if (s_lr_incident || (!f->any_unknown && !f->valve_hunt))
        return false;
    bool unknown_old = !f->any_unknown || f->listed_ms >= RP_DISC_BACKOFF_MS;
    bool valve_old = !f->valve_hunt ||
                     (s_x.hunt_since != 0 && (now - s_x.hunt_since) >= pdMS_TO_TICKS(RP_DISC_BACKOFF_MS));
    return unknown_old && valve_old;
}

// An AP mode's row: the guard's, AP_K1M with a 1M share, or the plain row with its discovery row
// every Nth period.
static uint8_t ap_row(TickType_t now, bool new_period, bool k1m)
{
    if (s_x.starve_until != 0)
        return (s_x.starve_kind == RP_STARVED_K1M) ? RP_ROW_AP_K1M : RP_ROW_APIDLE;
    if (k1m)
        return RP_ROW_AP_K1M;
    bool serve = (s_x.mode == RP_MODE_SERVE);
    if (serve && RP_RUNG_NOW == RP_RUNG_SERVE_A_THIN && !rp_within(s_hot_tick, now, RP_HOT_MS))
        serve = false;
#if CONFIG_APP_RADIO_LAB
    if (serve && RP_RUNG_NOW == RP_RUNG_APIDLE)
        serve = false;   // a key just chose the lab's AP_IDLE-density rung: the mode follows next pass
#endif
    bool b = (RP_RUNG_NOW == RP_RUNG_SERVE_B);
    uint8_t plain = serve ? (b ? RP_ROW_SERVE_B : RP_ROW_SERVE_A) : RP_ROW_APIDLE;
    uint8_t disc = serve ? (b ? RP_ROW_SERVE_B_DISC : RP_ROW_SERVE_A_DISC) : RP_ROW_APIDLE_DISC;
#if CONFIG_APP_RADIO_LAB
    if (serve && RP_RUNG_NOW == RP_RUNG_SERVE_C) {
        plain = RP_ROW_SERVE_C;   // the lab's SERVE-C: [C 1.0][W 1.0]
        disc = RP_ROW_SERVE_C_DISC;
    } else if (!serve && radio_lab_apidle_w06()) {
        plain = RP_ROW_SERVE_A;   // the lab's AP_IDLE at [C 0.6][W 0.6]; its discovery row is the same
    }
#endif
    bool bo = disc_backoff(now);
    if (bo != s_x.backoff) {
        s_x.backoff = bo;
        ESP_LOGI(RP_TAG, "Discovery %s", bo ? "backed off: its reasons are 10 min old (a PHY unknown, the valve unlinked)"
                                            : "at its full rate again");
    }
    unsigned every = serve ? (bo ? RP_DISC_EVERY_SERVE_BO : RP_DISC_EVERY_SERVE)
                           : (bo ? RP_DISC_EVERY_AP_BO : RP_DISC_EVERY_AP);
#if CONFIG_APP_RADIO_LAB
    if (!serve && bo && radio_lab_apidle_w06())
        every = RP_LAB_DISC_EVERY_AP06_BO;   // the period rule with the lab's 1.2 s plain period
#endif
    bool want_disc = s_x.f.any_unknown || s_x.f.valve_slot;
    if (s_x.period_row != plain && s_x.period_row != disc) {
        s_x.period_row = plain;   // a new row set: its plain row first
        s_x.period_n = 0;
    } else if (new_period) {
        s_x.period_n++;
        s_x.period_row = (want_disc && (s_x.period_n % every) == 0) ? disc : plain;
    }
    return s_x.period_row;
}

uint8_t radio_policy_exec_row(TickType_t now, bool new_period)
{
    const rp_ble_facts_t *f = &s_x.f;
    // B3: a 1M share while a listed sensor's PHY is unknown, for at most RP_UNKNOWN_PHY_MS after
    // boot or its provisioning (D1: some field sensors are on 1M).
    bool k1m = f->any_1m || (f->any_unknown && f->listed_ms < RP_UNKNOWN_PHY_MS);
    switch (s_x.mode) {
    case RP_MODE_NORMAL_LR:
        return RP_ROW_NORMAL_LR;
    case RP_MODE_LR_AP: {
        TickType_t start = s_lr_start;
        return (start != 0 && (now - start) < pdMS_TO_TICKS(RP_LR_AP_30_MS)) ? RP_ROW_LR_AP_30 : RP_ROW_LR_AP;
    }
    case RP_MODE_NORMAL:
        // B2: while the valve is wanted and unlinked outside the LR overlay, a 1M slot finds it in
        // about 1-3 s (N_MIXED's 1M slots do the same); not once it was heard, while its claim is
        // due or backed off (ble_valve_hunt_slot_wanted()).
        if (s_pinned)
            return RP_ROW_N_CODED;
        return k1m ? RP_ROW_N_MIXED : (f->valve_slot ? RP_ROW_N_HUNT : RP_ROW_N_CODED);
    case RP_MODE_SERVE:
    case RP_MODE_AP_IDLE:
        return ap_row(now, new_period, k1m);
    default:
        return RP_ROW_NONE;   // PAUSED, BLE_IDLE
    }
}

// A request the executor settles at once: its line and its count.
static void req_refuse(rp_pulse_t k, const char *why)
{
    req_set(k, RP_GRANT_REFUSED);
    if (s_x.refused < UINT16_MAX)
        s_x.refused++;
    // Rate-limited: an open SoftAP lets anyone ask for pulses (Connects, joins).
    TickType_t now = xTaskGetTickCount();
    if (!rp_within(s_x.refuse_logged, now, RP_REFUSE_LOG_MS)) {
        s_x.refuse_logged = rp_nz(now);
        ESP_LOGW(RP_TAG, "%s pulse not granted (%s) - Wi-Fi goes on beside BLE", k_pulse_names[k], why);
    }
}

rp_pulse_t radio_policy_exec_wifi_grant(TickType_t now, uint32_t kinds, bool at_coded_end,
                                        uint32_t budget_ms, bool coded_young, uint32_t *len_ms)
{
    static const uint8_t k_order[] = { RP_PULSE_SUBMIT, RP_PULSE_JOIN, RP_PULSE_RETRY, RP_PULSE_LIST };
    *len_ms = 0;

    // A station owed its one extra assist (plan 4.4: the first overlapped a STA attempt and gave no
    // lease) asks once the spacing allows; it bypasses the per-station spacing, not I2b's.
    if (s_x.extra_sta >= 0 && s_x.prof_ms >= s_x.space_ms) {
        int8_t i = s_x.extra_sta;
        s_x.extra_sta = -1;
        taskENTER_CRITICAL(&s_sta_lock);
        bool still = (s_sta[i].state == RP_STA_JOINED && s_sta[i].lease_at == 0);
        taskEXIT_CRITICAL(&s_sta_lock);
        if (still)
            (void)req_ask(RP_PULSE_JOIN, i);
    }

    uint32_t used = i2b_used();
    uint32_t room = (used < RP_I2B_BLIND_MAX_MS) ? RP_I2B_BLIND_MAX_MS - used : 0;
    for (size_t o = 0; o < sizeof(k_order); o++) {
        rp_pulse_t k = (rp_pulse_t)k_order[o];
        if (!(kinds & (1u << k)))
            continue;
        taskENTER_CRITICAL(&s_req_lock);
        uint8_t st = s_req[k].state;
        TickType_t at = s_req[k].at;
        int8_t sta = s_req[k].sta;
#if CONFIG_APP_RADIO_LAB
        bool lab_k1 = s_req[k].k1;
#endif
        taskEXIT_CRITICAL(&s_req_lock);
        if (st != RP_GRANT_PENDING)
            continue;

        // BLE is not scanning (BLE_IDLE, not synced): no pulse is needed.
        if (s_x.mode == RP_MODE_PAUSED || s_x.mode == RP_MODE_BLE_IDLE) {
            req_set(k, RP_GRANT_FREE);
            continue;
        }
        // RETRY and LIST are pulses only while the SoftAP is up (plan 4.4).
        if ((k == RP_PULSE_RETRY || k == RP_PULSE_LIST) && !(s_wifi & RP_W_AP_UP)) {
            req_set(k, RP_GRANT_FREE);
            continue;
        }
        // The grant protocol's 2 s, for every kind but SUBMIT: always honoured, it waits until it is
        // granted or its Connect ends (radio_policy_note_submit(false) withdraws it).
        if (k != RP_PULSE_SUBMIT && (now - at) >= pdMS_TO_TICKS(RP_GRANT_WAIT_MS)) {
            req_refuse(k, "not granted within 2 s");
            continue;
        }
        if (s_pinned) {
            req_refuse(k, "the profile self-test failed");
            continue;
        }

        uint32_t len = 0;
        switch (k) {
        case RP_PULSE_SUBMIT: {
            // Always honoured (plan 4.4), but within I2b's budget and I2 (decided for 2.1.4: an
            // open SoftAP must not let Connects blind BLE beyond the invariants). It waits for room
            // rather than being refused. While a leak response is pending it leaves the claim's
            // RP_CONNECT_LR_MS of that room (the executor also tries the claim first): Connects on
            // the open SoftAP, a stranger's included, can delay the valve's RMLEAK / CLOSE by at most
            // about one I2b window, never hold it off for good.
            if (!s_submit) {
                req_set(k, RP_GRANT_IDLE);   // its attempt ended before the grant
                continue;
            }
            uint32_t sroom = room;
            if (s_lr_trigger)
                sroom = (room > RP_CONNECT_LR_MS) ? room - RP_CONNECT_LR_MS : 0;
            if (!coded_young)
                len = rp_min(rp_min(RP_BLIND_MAX_MS, budget_ms), sroom);
            break;
        }
        case RP_PULSE_JOIN: {
            bool joined = false;
            if (sta >= 0 && sta < RP_STA_MAX) {
                taskENTER_CRITICAL(&s_sta_lock);
                joined = (s_sta[sta].state == RP_STA_JOINED);
                taskEXIT_CRITICAL(&s_sta_lock);
            }
            if (!joined) {
                req_set(k, RP_GRANT_IDLE);   // the station left before its assist
                continue;
            }
#if CONFIG_APP_RADIO_LAB
            if (lab_k1) {
                taskENTER_CRITICAL(&s_sta_lock);
                bool served = (s_sta[sta].probe_at != 0);
                taskEXIT_CRITICAL(&s_sta_lock);
                if (served) {
                    req_set(k, RP_GRANT_IDLE);   // the lab's K1: its station had its 302 or page meanwhile
                    continue;
                }
            }
#endif
            if (s_lr_on) {
                TickType_t start = s_lr_start;
                if (start != 0 && (now - start) < pdMS_TO_TICKS(RP_LR_JOIN_HOLDOFF_MS)) {
                    req_refuse(k, "a leak response's first 30 s");
                    continue;
                }
                if (rp_within(s_x.lr_join_at, now, RP_LR_JOIN_EVERY_MS)) {
                    req_refuse(k, "a leak response: one assist per 60 s");
                    continue;
                }
            }
            if (s_x.prof_ms >= s_x.space_ms && !coded_young)
                len = rp_min(rp_min(RP_BLIND_MAX_MS, budget_ms), room);   // clipped to I2b's room
#if CONFIG_APP_RADIO_LAB
            if (lab_k1)
                len = rp_min(len, RP_LAB_K1_MS);   // the lab's K1: 1.5 s at most (plan 4.4)
#endif
            break;
        }
        case RP_PULSE_RETRY:
        case RP_PULSE_LIST: {
            // LR_AP's first 30 s give Wi-Fi nothing but a SUBMIT (plan 4.2): the 1M and Coded scans
            // look for the valve. Refused at once, so the retry or the scan goes on beside BLE now
            // (the router's return carries the cloud alert) rather than after a 2 s wait.
            TickType_t lr_start = s_lr_start;
            if (s_x.mode == RP_MODE_LR_AP && lr_start != 0 &&
                (now - lr_start) < pdMS_TO_TICKS(RP_LR_AP_30_MS)) {
                req_refuse(k, "a leak response's first 30 s");
                continue;
            }
            uint32_t need = (k == RP_PULSE_RETRY) ? RP_RETRY_MS : RP_LIST_MAX_MS;
            if (at_coded_end && budget_ms >= need && s_x.prof_ms >= s_x.space_ms &&
                used + need <= RP_I2B_BLIND_MAX_MS)
                len = need;
            break;
        }
        default:
            break;
        }
        if (len >= RP_PULSE_MIN_MS) {
            if (!req_grant(k))
                continue;   // withdrawn meanwhile
            *len_ms = len;
            return k;
        }
        // It waits; nothing of a lower priority goes ahead of it.
        return RP_PULSE_NONE;
    }
    return RP_PULSE_NONE;
}

uint32_t radio_policy_exec_connect_len(TickType_t now, uint32_t budget_ms)
{
    (void)now;
    uint8_t m = s_x.mode;
    bool lr = s_lr_trigger;
    if (m == RP_MODE_PAUSED || m == RP_MODE_BLE_IDLE)
        return 0;
    if (m == RP_MODE_SERVE && !lr)
        return 0;   // claims wait while a setup page is served, unless a leak response is pending
    uint32_t len;
    if (lr)
        len = (budget_ms >= RP_CONNECT_LR_MS) ? RP_CONNECT_LR_MS : 0;
    else
        len = (budget_ms >= RP_CONNECT_MS) ? rp_min(RP_CONNECT_LR_MS, budget_ms) : 0;
    if (len == 0)
        return 0;
    uint32_t used = i2b_used();
    if (s_x.prof_ms >= s_x.space_ms && used + len <= RP_I2B_BLIND_MAX_MS) {
        s_x.i2b_noted = false;
        return len;
    }
    if (!s_x.i2b_noted) {
        s_x.i2b_noted = true;
        ESP_LOGI(RP_TAG, "[CLAIM] Valve claim waits for the pulse-rate limit (I2b): %lu.%lu of %lu.%lu s scanned since the last pulse, %lu.%lu s of pulses in the last 60 s",
                 (unsigned long)(s_x.prof_ms / 1000), (unsigned long)(s_x.prof_ms % 1000 / 100),
                 (unsigned long)(s_x.space_ms / 1000), (unsigned long)(s_x.space_ms % 1000 / 100),
                 (unsigned long)(used / 1000), (unsigned long)(used % 1000 / 100));
    }
    return 0;
}

void radio_policy_exec_pulse_begin(rp_pulse_t kind, TickType_t now, uint32_t len_ms)
{
    s_x.pulse = (uint8_t)kind;
    s_x.pulse_at = now;
    s_x.pulse_deadline = rp_nz(now + pdMS_TO_TICKS(len_ms));
    s_x.pulse_why = "its deadline";
    s_x.prof_ms = 0;
    s_x.space_ms = RP_I2B_SPACING_MS + esp_random() % (RP_I2B_JITTER_MS + 1);
    s_x.i2b_noted = false;
    if (s_x.pulse_n[kind] < UINT16_MAX)
        s_x.pulse_n[kind]++;
    if (kind == RP_PULSE_CONNECT)
        return;   // the valve module prints its claim
    if (kind == RP_PULSE_JOIN) {
        taskENTER_CRITICAL(&s_req_lock);
        int8_t i = s_req[RP_PULSE_JOIN].sta;
        taskEXIT_CRITICAL(&s_req_lock);
        uint8_t mac[6] = { 0 };
        if (i >= 0 && i < RP_STA_MAX) {
            taskENTER_CRITICAL(&s_sta_lock);
            s_sta[i].assist_at = rp_nz(now);
            s_sta[i].assist_gap = pdMS_TO_TICKS(RP_JOIN_SPACING_MS + esp_random() % (RP_JOIN_SPACING_JIT_MS + 1));
            memcpy(mac, s_sta[i].mac, 6);
            taskEXIT_CRITICAL(&s_sta_lock);
        }
        s_x.join_sta = i;
        s_x.join_attempt = s_sta_attempt;
        if (s_lr_on)
            s_x.lr_join_at = rp_nz(now);
        ESP_LOGI(RP_TAG, "JOIN pulse for station %02X:%02X:%02X:%02X:%02X:%02X: BLE off for up to %lu ms",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], (unsigned long)len_ms);
        return;
    }
    ESP_LOGI(RP_TAG, "%s pulse: BLE off for up to %lu ms", k_pulse_names[kind], (unsigned long)len_ms);
}

void radio_policy_exec_pulse_retract(rp_pulse_t kind)
{
    if (kind > RP_PULSE_NONE && kind < RP_PULSE_CONNECT)
        req_set(kind, RP_GRANT_PENDING);   // its asked time is kept: RP_GRANT_WAIT_MS still runs
}

TickType_t radio_policy_exec_pulse_deadline(void)
{
    return s_x.pulse_deadline;
}

// When the executor must look at the running pulse again: its deadline, or for a join assist whose
// station has its lease and its first page or 302, max(lease + 1.5 s, first 302 or page + 0.3 s)
// when that is sooner (plan 4.4's end rule, radio_policy_exec_pulse_over()). Without it the assist
// ran on to the executor's next poll, up to EXEC_POLL_MS (0.5 s) of blind time past its end.
TickType_t radio_policy_exec_pulse_wake(void)
{
    TickType_t dl = s_x.pulse_deadline;
    int8_t i = s_x.join_sta;
    if (s_x.pulse != RP_PULSE_JOIN || i < 0 || i >= RP_STA_MAX)
        return dl;
    taskENTER_CRITICAL(&s_sta_lock);
    TickType_t lease = s_sta[i].lease_at;
    TickType_t probe = s_sta[i].probe_at;
    taskEXIT_CRITICAL(&s_sta_lock);
    if (lease == 0 || probe == 0)
        return dl;
    TickType_t a = lease + pdMS_TO_TICKS(RP_JOIN_LEASE_TAIL_MS);
    TickType_t b = probe + pdMS_TO_TICKS(RP_JOIN_PROBE_TAIL_MS);
    TickType_t end = ((int32_t)(a - b) > 0) ? a : b;
    return ((int32_t)(end - dl) < 0) ? end : dl;
}

bool radio_policy_exec_pulse_over(TickType_t now)
{
    rp_pulse_t k = (rp_pulse_t)s_x.pulse;
    if (k == RP_PULSE_NONE || k == RP_PULSE_CONNECT)
        return false;   // CONNECT ends at its CONNECT event (the executor reads NimBLE)
    if ((int32_t)(now - s_x.pulse_deadline) >= 0) {
        s_x.pulse_why = "its deadline";
        return true;   // I3: every pause is a deadline the executor expires itself
    }
    taskENTER_CRITICAL(&s_req_lock);
    bool ended = s_req[k].end || s_req[k].state != RP_GRANT_ON;
    taskEXIT_CRITICAL(&s_req_lock);
    if (ended) {
        s_x.pulse_why = "its requester ended it";
        return true;
    }
    if (k == RP_PULSE_SUBMIT && !s_submit) {
        s_x.pulse_why = "the Connect's outcome";
        return true;
    }
    if (k == RP_PULSE_JOIN) {
        s_x.join_attempt = s_x.join_attempt || s_sta_attempt;   // the assist overlapped a STA attempt
        int8_t i = s_x.join_sta;
        if (i < 0 || i >= RP_STA_MAX) {
            s_x.pulse_why = "its station is gone";
            return true;
        }
        taskENTER_CRITICAL(&s_sta_lock);
        rp_sta_t s = s_sta[i];
        taskEXIT_CRITICAL(&s_sta_lock);
        if (s.state != RP_STA_JOINED) {
            s_x.pulse_why = "the station left";
            return true;
        }
        // max(lease + 1.5 s, first 302 or page + 0.3 s)
        if (s.lease_at != 0 && s.probe_at != 0 &&
            (now - s.lease_at) >= pdMS_TO_TICKS(RP_JOIN_LEASE_TAIL_MS) &&
            (now - s.probe_at) >= pdMS_TO_TICKS(RP_JOIN_PROBE_TAIL_MS)) {
            s_x.pulse_why = "its lease and first page or 302";
            return true;
        }
    }
    return false;
}

void radio_policy_exec_pulse_end(TickType_t now)
{
    rp_pulse_t k = (rp_pulse_t)s_x.pulse;
    if (k == RP_PULSE_NONE)
        return;
    s_x.pulse = RP_PULSE_NONE;
    s_x.pulse_deadline = 0;
    uint32_t used = i2b_used();
    if (used > s_x.i2b_peak)
        s_x.i2b_peak = used;
    if (k == RP_PULSE_CONNECT)
        return;
    req_set(k, RP_GRANT_IDLE);
    uint32_t ms = rp_ms(now - s_x.pulse_at);
    if (k == RP_PULSE_JOIN) {
        int8_t i = s_x.join_sta;
        s_x.join_sta = -1;
        bool extra = false;
        if (i >= 0 && i < RP_STA_MAX) {
            taskENTER_CRITICAL(&s_sta_lock);
            if (s_sta[i].state == RP_STA_JOINED && s_sta[i].lease_at == 0 && s_x.join_attempt && !s_sta[i].extra) {
                s_sta[i].extra = true;
                extra = true;
            }
            taskEXIT_CRITICAL(&s_sta_lock);
        }
        if (extra) {
            s_x.extra_sta = i;
            ESP_LOGI(RP_TAG, "JOIN pulse over after %lu ms with no lease, beside a STA attempt - one more assist after the recovery and the pulse spacing",
                     (unsigned long)ms);
            return;
        }
    }
    ESP_LOGI(RP_TAG, "%s pulse over after %lu ms (%s)", k_pulse_names[k], (unsigned long)ms, s_x.pulse_why);
}

void radio_policy_exec_account(TickType_t now, bool profile, bool want, bool scanning, rp_pulse_t pulse,
                               bool recovery)
{
    i2b_advance(now);
    if (s_x.acct_at == 0) {
        s_x.acct_at = rp_nz(now);
        return;
    }
    uint32_t dt = rp_ms(now - s_x.acct_at);
    s_x.acct_at = rp_nz(now);
    s_x.mode_ms[s_x.mode] += dt;
    if (pulse != RP_PULSE_NONE) {
        s_x.blind_ms += dt;
        uint32_t b = s_x.bucket[s_x.bucket_i] + dt;
        s_x.bucket[s_x.bucket_i] = (uint16_t)(b > UINT16_MAX ? UINT16_MAX : b);
        // A pulse past its deadline (a connect NimBLE has not ended) is time BLE should have scanned:
        // the duty watchdog sees it.
        if (s_x.pulse_deadline != 0 && (int32_t)(now - s_x.pulse_deadline) > 0)
            s_x.want_ms += dt;
    } else if (profile) {
        // I2b's spacing counts profile time: a row running, its Wi-Fi slots and the recovery included.
        s_x.prof_ms = rp_min(s_x.prof_ms + dt, RP_I2B_WINDOW_MS);
    }
    if (want) {
        s_x.want_ms += dt;
        if (scanning)
            s_x.on_ms += dt;
    }
    if (recovery)
        s_x.recovery_ms += dt;
    if (s_lr_on)
        s_x.lr_ms += dt;
}

void radio_policy_exec_gap(uint32_t blind_ms)
{
    if (blind_ms > s_x.gap_max)
        s_x.gap_max = blind_ms;
    if (blind_ms > RP_BLIND_MAX_MS + RP_JITTER_MS) {
        if (s_x.gap_over == 0)
            ESP_LOGE(RP_TAG, "I2: BLE went %lu ms with no Coded scan (limit %d ms) - send this log",
                     (unsigned long)blind_ms, RP_BLIND_MAX_MS);
        if (s_x.gap_over < UINT16_MAX)
            s_x.gap_over++;
    }
}

// The executor calls it in a frame of its own, at a priority below iothub_task's (15t I-5): its two
// lines keep UART0 busy for tens of milliseconds, and a leak on this core then never waits behind
// them. The executor's scan runs on in the controller meanwhile.
void radio_policy_exec_summary(const char *adverts, unsigned phy_flips)
{
    unsigned duty = s_x.want_ms ? (unsigned)((uint64_t)s_x.on_ms * 100u / s_x.want_ms) : 100u;
    uint32_t blind = s_x.blind_ms;
    taskENTER_CRITICAL(&s_sta_lock);
    unsigned joins = s_joins;
    uint32_t lease_max = s_lease_ms_max;
    s_joins = 0;
    s_lease_ms_max = 0;
    taskEXIT_CRITICAL(&s_sta_lock);

    ESP_LOGI(RP_TAG, "[SUMMARY] modes NORMAL %lu s, NORMAL_LR %lu s, AP_IDLE %lu s, SERVE %lu s, LR_AP %lu s, BLE_IDLE %lu s, paused %lu s; "
             "BLE scanning %lu.%lu of %lu.%lu s (%u %%); LR overlay %lu s; PHY changes %u; adverts %s",
             (unsigned long)(s_x.mode_ms[RP_MODE_NORMAL] / 1000), (unsigned long)(s_x.mode_ms[RP_MODE_NORMAL_LR] / 1000),
             (unsigned long)(s_x.mode_ms[RP_MODE_AP_IDLE] / 1000), (unsigned long)(s_x.mode_ms[RP_MODE_SERVE] / 1000),
             (unsigned long)(s_x.mode_ms[RP_MODE_LR_AP] / 1000), (unsigned long)(s_x.mode_ms[RP_MODE_BLE_IDLE] / 1000),
             (unsigned long)(s_x.mode_ms[RP_MODE_PAUSED] / 1000),
             (unsigned long)(s_x.on_ms / 1000), (unsigned long)(s_x.on_ms % 1000 / 100),
             (unsigned long)(s_x.want_ms / 1000), (unsigned long)(s_x.want_ms % 1000 / 100), duty,
             (unsigned long)(s_x.lr_ms / 1000), phy_flips, adverts);
    ESP_LOGI(RP_TAG, "[SUMMARY] pulses JOIN %u, SUBMIT %u, RETRY %u, LIST %u, CONNECT %u, refused %u: %lu.%lu s blind (at most %lu.%lu s in 60 s), recovery %lu.%lu s; "
             "longest Coded gap %lu ms (%u over %d ms); joins %u, join to lease %lu ms at most",
             (unsigned)s_x.pulse_n[RP_PULSE_JOIN], (unsigned)s_x.pulse_n[RP_PULSE_SUBMIT],
             (unsigned)s_x.pulse_n[RP_PULSE_RETRY], (unsigned)s_x.pulse_n[RP_PULSE_LIST],
             (unsigned)s_x.pulse_n[RP_PULSE_CONNECT], (unsigned)s_x.refused,
             (unsigned long)(blind / 1000), (unsigned long)(blind % 1000 / 100),
             (unsigned long)(s_x.i2b_peak / 1000), (unsigned long)(s_x.i2b_peak % 1000 / 100),
             (unsigned long)(s_x.recovery_ms / 1000), (unsigned long)(s_x.recovery_ms % 1000 / 100),
             (unsigned long)s_x.gap_max, (unsigned)s_x.gap_over, RP_BLIND_MAX_MS + RP_JITTER_MS,
             joins, (unsigned long)lease_max);

    // The duty watchdog: judged on minutes with at least half of them meant for scanning.
    if (s_x.want_ms >= 30000 && duty < RP_DUTY_WARN_PCT) {
        if (s_x.low_duty < UINT8_MAX)
            s_x.low_duty++;
        if (s_x.low_duty >= RP_DUTY_WARN_RUNS && (s_x.low_duty - RP_DUTY_WARN_RUNS) % 5 == 0)
            ESP_LOGW(RP_TAG, "Duty watchdog: BLE scanning ran %u %% of its expected time for %u min (below %d %%)",
                     duty, (unsigned)s_x.low_duty, RP_DUTY_WARN_PCT);
    } else {
        s_x.low_duty = 0;
    }
#if CONFIG_APP_RADIO_LAB
    // The lab's settings with the summary, so every minute of a G1 log names its ladder step.
    taskENTER_CRITICAL(&s_sta_lock);
    int k1_asked = s_lab_k1_n;
    s_lab_k1_n = 0;
    taskEXIT_CRITICAL(&s_sta_lock);
    radio_lab_log("60 s", k1_asked);
#endif

    memset(s_x.mode_ms, 0, sizeof(s_x.mode_ms));
    s_x.blind_ms = 0;
    memset(s_x.pulse_n, 0, sizeof(s_x.pulse_n));
    s_x.want_ms = 0;
    s_x.on_ms = 0;
    s_x.recovery_ms = 0;
    s_x.lr_ms = 0;
    s_x.refused = 0;
    s_x.i2b_peak = 0;
    s_x.gap_max = 0;
    s_x.gap_over = 0;
}

#if CONFIG_APP_RADIO_LAB
/* =========================================================
 * The G1 lab image (radio_lab.h): its row sets' self-test, its mode lines and contingency K1
 * ========================================================= */

// Every row set a lab setting can select holds the period rule (as the asserts say, on the table as
// built), and only the two SERVE-C rows are lab rows. NULL when it holds, else what fails.
static const char *lab_seq_check(void)
{
    if (!seq_ok(RP_ROW_SERVE_A, RP_ROW_APIDLE_DISC, RP_DISC_EVERY_AP) ||
        !seq_ok(RP_ROW_SERVE_A, RP_ROW_APIDLE_DISC, RP_LAB_DISC_EVERY_AP06_BO) ||
        !seq_ok(RP_ROW_SERVE_A, RP_ROW_SERVE_A_DISC, RP_DISC_EVERY_SERVE) ||
        !seq_ok(RP_ROW_SERVE_A, RP_ROW_SERVE_A_DISC, RP_DISC_EVERY_SERVE_BO) ||
        !seq_ok(RP_ROW_SERVE_B, RP_ROW_SERVE_B_DISC, RP_DISC_EVERY_SERVE) ||
        !seq_ok(RP_ROW_SERVE_B, RP_ROW_SERVE_B_DISC, RP_DISC_EVERY_SERVE_BO) ||
        !seq_ok(RP_ROW_SERVE_C, RP_ROW_SERVE_C_DISC, RP_DISC_EVERY_SERVE) ||
        !seq_ok(RP_ROW_SERVE_C, RP_ROW_SERVE_C_DISC, RP_DISC_EVERY_SERVE_BO))
        return "the period rule";
    for (int i = 0; i < RP_ROW_COUNT; i++) {
        bool lab = (i == RP_ROW_SERVE_C || i == RP_ROW_SERVE_C_DISC);
        if (lab != ((k_rows[i].flags & RP_F_LAB) != 0))
            return "a row's lab mark";
    }
    return NULL;
}

// The lab's SERVE and AP_IDLE mode lines: its rung and its AP_IDLE slot. True when printed (the
// other modes print as in production).
static bool lab_mode_line(uint8_t m)
{
    if (m == RP_MODE_AP_IDLE) {
        ESP_LOGI(RP_TAG, "Mode AP_IDLE (SoftAP up, STA not connected): Coded 0.6 s / Wi-Fi %s s, discovery every %d periods (lab)",
                 radio_lab_apidle_w06() ? "0.6" : "0.3", RP_DISC_EVERY_AP);
        return true;
    }
    if (m == RP_MODE_SERVE) {
        uint8_t r = radio_lab_rung();
        ESP_LOGI(RP_TAG, "Mode SERVE (SoftAP up, STA not connected; a setup page in use, a Connect or a new lease): rung %s (lab): %s",
                 radio_lab_rung_name(r), radio_lab_rung_text(r));
        return true;
    }
    return false;
}

// Asks for station sta's JOIN pulse as K1's: req_ask() with the K1 mark set under the same lock, so
// the executor never grants it as a full join assist. True when it asked.
static bool lab_k1_ask(int8_t sta)
{
    TickType_t now = rp_nz(xTaskGetTickCount());
    taskENTER_CRITICAL(&s_req_lock);
    bool busy = (s_req[RP_PULSE_JOIN].state == RP_GRANT_PENDING || s_req[RP_PULSE_JOIN].state == RP_GRANT_ON);
    if (!busy) {
        s_req[RP_PULSE_JOIN].state = RP_GRANT_PENDING;
        s_req[RP_PULSE_JOIN].end = false;
        s_req[RP_PULSE_JOIN].sta = sta;
        s_req[RP_PULSE_JOIN].at = now;
        s_req[RP_PULSE_JOIN].k1 = true;
    }
    taskEXIT_CRITICAL(&s_req_lock);
    if (!busy)
        app_ble_leak_kick();
    return !busy;
}

// Contingency K1 (plan 4.4), on the dns_server task: the first DNS query from a leased station
// that has not yet been sent a 302 or a page asks for a join assist of at most RP_LAB_K1_MS, at
// most once per station per RP_LAB_K1_EVERY_MS. In all else it is a JOIN pulse: within I2 and I2b
// (the 6 s spacing included), held off under a leak response as a join assist is, ended by the
// station's first 302 or page + 0.3 s, and it counts as that station's assist for the join
// spacing. Not asked while another JOIN is asked or runs (a later query asks again).
static void lab_k1_dns(uint32_t ip)
{
    if (!radio_lab_k1_on() || ip == 0 || !s_live)
        return;
    TickType_t now = rp_nz(xTaskGetTickCount());
    int i = -1;
    taskENTER_CRITICAL(&s_sta_lock);
    for (int k = 0; k < RP_STA_MAX && i < 0; k++) {
        if (s_sta[k].state == RP_STA_JOINED && s_sta[k].ip == ip && s_sta[k].probe_at == 0)
            i = k;
    }
    if (i >= 0) {
        if (memcmp(s_lab_k1[i].mac, s_sta[i].mac, 6) != 0) {
            memcpy(s_lab_k1[i].mac, s_sta[i].mac, 6);   // another station in this entry
            s_lab_k1[i].at = 0;
        }
        if (rp_within(s_lab_k1[i].at, now, RP_LAB_K1_EVERY_MS))
            i = -1;
    }
    taskEXIT_CRITICAL(&s_sta_lock);
    if (i < 0 || !lab_k1_ask((int8_t)i))
        return;
    taskENTER_CRITICAL(&s_sta_lock);
    s_lab_k1[i].at = now;
    if (s_lab_k1_n < UINT16_MAX)
        s_lab_k1_n++;
    taskEXIT_CRITICAL(&s_sta_lock);
    ESP_LOGI(RP_TAG, "[LAB] K1: %u.%u.%u.%u's first DNS query before a 302 or a page - a JOIN pulse of at most %d ms asked",
             (unsigned)(ip & 0xff), (unsigned)((ip >> 8) & 0xff), (unsigned)((ip >> 16) & 0xff),
             (unsigned)(ip >> 24), RP_LAB_K1_MS);
}
#endif // CONFIG_APP_RADIO_LAB
