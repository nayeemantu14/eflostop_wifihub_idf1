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
#include "app_wifi/portal_priority.h"
#include "ble_leak_scanner/app_ble_leak.h"

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

/* ---------------------------------------------------------
 * The profile table and its invariants, checked at compile time (plan 4.8; I1, I2, I8, the period
 * rule). radio_policy_init() runs the same checks on the table as built (the boot self-test).
 * --------------------------------------------------------- */
#define RP_KC(k)   ((k) == RP_K_C || (k) == RP_K_N)                 // a Coded window (I1)
#define RP_KCH(k)  (RP_KC(k) || (k) == RP_K_H)                      // ... or the legacy hunt's
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
    (RP_KCH(k0) ? 0 : RP_KCH(k1) ? 1 : RP_KCH(k2) ? 2 : RP_KCH(k3) ? 3 : 0)

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
    _Static_assert(((fl) & (RP_F_DITHER | RP_F_DISC)) || RP_PERIOD_OK((m0) + (m1) + (m2) + (m3)),  \
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

/* ---------------------------------------------------------
 * The executor's side: the ble_leak_scan task only, so nothing here locks.
 * --------------------------------------------------------- */
typedef struct {
    rp_ble_facts_t f;           // the facts of the last pass
    uint8_t mode;               // the mode now (rp_mode_t)
    uint8_t logged;             // the mode last printed (RP_MODE_COUNT: none; HOLD is never printed)
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
        if (RP_KCH(k) && !coded_seen) {
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
    if (!(r->flags & (RP_F_DITHER | RP_F_DISC)) && !RP_PERIOD_OK(period))
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
    if (why != NULL) {
        s_pinned = true;
        ESP_LOGE(RP_TAG, "Profile self-test FAILED (%s: %s) - pinned to NORMAL (N_CODED), no Wi-Fi pulses",
                 row, why);
        return;
    }
    ESP_LOGI(RP_TAG, "Profile self-test passed: %d rows hold I1, I2, I8 and the period rule (SERVE rung %s)",
             (int)RP_ROW_COUNT,
             RP_SERVE_RUNG == RP_RUNG_SERVE_B ? "SERVE-B" :
             RP_SERVE_RUNG == RP_RUNG_SERVE_A_THIN ? "SERVE-A-thin" : "SERVE-A");
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
 * TRANSITIONAL: the legacy window and holds (app_wifi.c). Lock-free reads, any task.
 * ========================================================= */
bool radio_policy_legacy_window(void)
{
    return app_wifi_portal_priority_active();
}

bool radio_policy_legacy_hold(void)
{
    return app_wifi_portal_priority_active() || app_wifi_radio_hold_active();
}

/* =========================================================
 * Requests (any task)
 * ========================================================= */
// Asks for a pulse of `kind` (for JOIN: station `sta`), unless one of that kind is pending or on.
// True when it asked.
static bool req_ask(rp_pulse_t kind, int8_t sta)
{
    TickType_t now = rp_nz(xTaskGetTickCount());
    taskENTER_CRITICAL(&s_req_lock);
    bool busy = (s_req[kind].state == RP_GRANT_PENDING || s_req[kind].state == RP_GRANT_ON);
    if (!busy) {
        s_req[kind].state = RP_GRANT_PENDING;
        s_req[kind].end = false;
        s_req[kind].sta = sta;
        s_req[kind].at = now;
    }
    taskEXIT_CRITICAL(&s_req_lock);
    if (!busy)
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
    if (kind == HTTP_APP_ACT_DNS || (unsigned)kind >= HTTP_APP_ACT_COUNT)
        return;   // the dns_server task: nothing here keys on DNS (contingency K1 is not built)
    TickType_t now = rp_nz(xTaskGetTickCount());
    if (kind == HTTP_APP_ACT_PAGE || kind == HTTP_APP_ACT_API_USER || kind == HTTP_APP_ACT_API_BG)
        s_page_tick = now;
    if (kind == HTTP_APP_ACT_PAGE || kind == HTTP_APP_ACT_API_USER || kind == HTTP_APP_ACT_PROBE_302)
        s_hot_tick = now;
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

// The mode's line (plan 4.7: one INFO line per mode change). HOLD is never printed (app_wifi.c
// prints the window and each hold), and the mode after a hold only if it changed.
static void mode_line(uint8_t m)
{
    if (m == RP_MODE_HOLD || m == s_x.logged)
        return;
    s_x.logged = m;
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
    else if (radio_policy_legacy_hold())
        m = RP_MODE_HOLD;
    else if (s_pinned)
        m = RP_MODE_NORMAL;
    else if (f->sensors == 0 && (!f->valve || f->valve_linked))
        m = RP_MODE_BLE_IDLE;
    else if (lr)
        m = windowed ? RP_MODE_LR_AP : RP_MODE_NORMAL_LR;
    else if (!windowed)
        m = RP_MODE_NORMAL;
    else if (serve_now(now))
        m = RP_MODE_SERVE;
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
    if (serve && RP_SERVE_RUNG == RP_RUNG_SERVE_A_THIN && !rp_within(s_hot_tick, now, RP_HOT_MS))
        serve = false;
    bool b = (RP_SERVE_RUNG == RP_RUNG_SERVE_B);
    uint8_t plain = serve ? (b ? RP_ROW_SERVE_B : RP_ROW_SERVE_A) : RP_ROW_APIDLE;
    uint8_t disc = serve ? (b ? RP_ROW_SERVE_B_DISC : RP_ROW_SERVE_A_DISC) : RP_ROW_APIDLE_DISC;
    bool bo = disc_backoff(now);
    if (bo != s_x.backoff) {
        s_x.backoff = bo;
        ESP_LOGI(RP_TAG, "Discovery %s", bo ? "backed off: its reasons are 10 min old (a PHY unknown, the valve unlinked)"
                                            : "at its full rate again");
    }
    unsigned every = serve ? (bo ? RP_DISC_EVERY_SERVE_BO : RP_DISC_EVERY_SERVE)
                           : (bo ? RP_DISC_EVERY_AP_BO : RP_DISC_EVERY_AP);
    bool want_disc = s_x.f.any_unknown || s_x.f.valve_hunt;
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
    case RP_MODE_HOLD:
        return f->valve_hunt ? RP_ROW_HOLD_HUNT : RP_ROW_NONE;
    case RP_MODE_NORMAL_LR:
        return RP_ROW_NORMAL_LR;
    case RP_MODE_LR_AP: {
        TickType_t start = s_lr_start;
        return (start != 0 && (now - start) < pdMS_TO_TICKS(RP_LR_AP_30_MS)) ? RP_ROW_LR_AP_30 : RP_ROW_LR_AP;
    }
    case RP_MODE_NORMAL:
        // B2: while the valve is wanted and unlinked outside the LR overlay, a 1M slot finds it in
        // about 1-3 s (N_MIXED's 1M slots do the same).
        if (s_pinned)
            return RP_ROW_N_CODED;
        return k1m ? RP_ROW_N_MIXED : (f->valve_hunt ? RP_ROW_N_HUNT : RP_ROW_N_CODED);
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
        taskEXIT_CRITICAL(&s_req_lock);
        if (st != RP_GRANT_PENDING)
            continue;

        // BLE is not scanning (a legacy hold, BLE_IDLE, not synced): no pulse is needed.
        if (s_x.mode == RP_MODE_PAUSED || s_x.mode == RP_MODE_HOLD || s_x.mode == RP_MODE_BLE_IDLE) {
            req_set(k, RP_GRANT_FREE);
            continue;
        }
        // RETRY and LIST are pulses only while the SoftAP is up (plan 4.4).
        if ((k == RP_PULSE_RETRY || k == RP_PULSE_LIST) && !(s_wifi & RP_W_AP_UP)) {
            req_set(k, RP_GRANT_FREE);
            continue;
        }
        if ((now - at) >= pdMS_TO_TICKS(RP_GRANT_WAIT_MS)) {
            req_refuse(k, "not granted within 2 s");
            continue;
        }
        if (s_pinned) {
            req_refuse(k, "the profile self-test failed");
            continue;
        }

        uint32_t len = 0;
        switch (k) {
        case RP_PULSE_SUBMIT:
            // Always honoured (plan 4.4), but within I2b's budget and I2 (decided for 2.1.4: an
            // open SoftAP must not let Connects blind BLE beyond the invariants).
            if (!s_submit) {
                req_set(k, RP_GRANT_IDLE);   // its attempt ended before the grant
                continue;
            }
            if (room < RP_PULSE_MIN_MS) {
                req_refuse(k, "12 s of pulses in the last 60 s (I2b)");
                continue;
            }
            if (!coded_young)
                len = rp_min(rp_min(RP_BLIND_MAX_MS, budget_ms), room);
            break;
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
            break;
        }
        case RP_PULSE_RETRY:
        case RP_PULSE_LIST: {
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
            req_set(k, RP_GRANT_ON);
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

    ESP_LOGI(RP_TAG, "[SUMMARY] modes NORMAL %lu s, NORMAL_LR %lu s, AP_IDLE %lu s, SERVE %lu s, LR_AP %lu s, BLE_IDLE %lu s, hold %lu s, paused %lu s; "
             "BLE scanning %lu.%lu of %lu.%lu s (%u %%); LR overlay %lu s; PHY changes %u; adverts %s",
             (unsigned long)(s_x.mode_ms[RP_MODE_NORMAL] / 1000), (unsigned long)(s_x.mode_ms[RP_MODE_NORMAL_LR] / 1000),
             (unsigned long)(s_x.mode_ms[RP_MODE_AP_IDLE] / 1000), (unsigned long)(s_x.mode_ms[RP_MODE_SERVE] / 1000),
             (unsigned long)(s_x.mode_ms[RP_MODE_LR_AP] / 1000), (unsigned long)(s_x.mode_ms[RP_MODE_BLE_IDLE] / 1000),
             (unsigned long)(s_x.mode_ms[RP_MODE_HOLD] / 1000), (unsigned long)(s_x.mode_ms[RP_MODE_PAUSED] / 1000),
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
