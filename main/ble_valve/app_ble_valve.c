#include "app_ble_valve.h"
#include "ble_leak_scanner/app_ble_leak.h"
#include "health_engine/health_engine.h"
#include "radio_policy/radio_policy.h"
#include "rules_engine/rules_engine.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "host/ble_store.h"
#include "host/ble_sm.h"

#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "os/os_mbuf.h"

#define BLE_TAG "BLE_VALVE"
/* The valve's advertised Complete Local Name (app_ble.c on the STM32WB valve). No longer
 * used for matching (2.1.4, P0-a): EVERY eFloStop valve advertises this name and answers
 * the same fixed passkey, so a hub with no (or a different) provisioned valve linked a
 * neighbour's valve by name and then auto-close / C2D / the rules tick drove it. Discovery
 * now matches the provisioned valve's MAC only (ble_valve_note_adv). Kept for reference. */
#define VALVE_DEVICE_NAME "eFloStopV2"

// -----------------------------------------------------------------------------
// SECURITY CONFIGURATION
// -----------------------------------------------------------------------------
// Fixed passkey matching STM32WB valve (CFG_FIXED_PIN = 222900)
#define BLE_VALVE_FIXED_PASSKEY  222900

// Timeout for overall connection setup (discovery + pairing + reads)
#define SECURITY_TIMEOUT_MS 60000

// Discovery timeout
#define DISCOVERY_TIMEOUT_MS 30000

// Small delay after connection before starting security (allow link to stabilize)
#define POST_CONNECT_SECURITY_DELAY_MS 1000

// Retry delay if initial security attempt fails
#define SECURITY_RETRY_DELAY_MS 2000

// Maximum security initiation retries
#define MAX_SECURITY_RETRIES 3

// nimble_port_init() attempts at BLE start, and the pause between them
#define NIMBLE_INIT_ATTEMPTS  5
#define NIMBLE_INIT_RETRY_MS  5000

// -----------------------------------------------------------------------------
// UUIDS (128-bit Explicit)
// -----------------------------------------------------------------------------
static const ble_uuid128_t UUID_SVC_VALVE =
    BLE_UUID128_INIT(0x8f, 0xe5, 0xb3, 0xd5, 0x2e, 0x7f, 0x4a, 0x98, 0x2a, 0x48, 0x7a, 0xcc, 0x02, 0x00, 0x00, 0x00);
static const ble_uuid128_t UUID_CHR_VALVE =
    BLE_UUID128_INIT(0x19, 0xed, 0x82, 0xae, 0xed, 0x21, 0x4c, 0x9d, 0x41, 0x45, 0x22, 0x8e, 0x02, 0x00, 0x00, 0x00);

static const ble_uuid128_t UUID_SVC_FLOOD =
    BLE_UUID128_INIT(0x8f, 0xe5, 0xb3, 0xd5, 0x2e, 0x7f, 0x4a, 0x98, 0x2a, 0x48, 0x7a, 0xcc, 0x01, 0x00, 0x00, 0x00);
static const ble_uuid128_t UUID_CHR_FLOOD =
    BLE_UUID128_INIT(0x19, 0xed, 0x82, 0xae, 0xed, 0x21, 0x4c, 0x9d, 0x41, 0x45, 0x22, 0x8e, 0x01, 0x00, 0x00, 0x00);

// RMLEAK characteristic (in FLOOD service) — Remote Leak Interlock
static const ble_uuid128_t UUID_CHR_RMLEAK =
    BLE_UUID128_INIT(0x19, 0xed, 0x82, 0xae, 0xed, 0x21, 0x4c, 0x9d, 0x41, 0x45, 0x22, 0x8e, 0x00, 0x00, 0x00, 0x00);

static const ble_uuid128_t UUID_SVC_BATT =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x0f, 0x18, 0x00, 0x00);

static const ble_uuid128_t UUID_CHR_BATT =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x19, 0x2a, 0x00, 0x00);

// Device Information Service (0x180A) — optional, for firmware version reporting
static const ble_uuid128_t UUID_SVC_DIS =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x0a, 0x18, 0x00, 0x00);

// Firmware Revision String characteristic (0x2A26)
static const ble_uuid128_t UUID_CHR_FIRMWARE_REV =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x26, 0x2a, 0x00, 0x00);

// -----------------------------------------------------------------------------
// ATT ERROR CODES
// -----------------------------------------------------------------------------
#define ATT_ERR_INSUFFICIENT_AUTHEN  0x05
#define ATT_ERR_INSUFFICIENT_ENC     0x0F
#define BLE_HS_ATT_ERR(att_err)      (0x100 + (att_err))

// -----------------------------------------------------------------------------
// GLOBALS
// -----------------------------------------------------------------------------
static QueueHandle_t ble_cmd_queue = NULL;
QueueHandle_t ble_update_queue = NULL;

static TaskHandle_t ble_starter_task_handle = NULL;

// Event group for thread-safe state synchronization
static EventGroupHandle_t ble_state_event_group = NULL;

// Mutex for serializing GATT operations
static SemaphoreHandle_t gatt_mutex = NULL;

static uint16_t valve_conn_handle = BLE_HS_CONN_HANDLE_NONE;

// ---- The valve hunt and its claims (2.1.4 WP5) -----------------------------------------------
// The module runs no scan of its own any more: the leak scanner's task is the only BLE scan
// executor (app_ble_leak.c). The hunt is a demand the executor reads from the facts on every pass
// (ble_valve_hunt_wanted()); every advert of the executor's scans reaches ble_valve_note_adv() on
// the NimBLE host task, which stores a claim request when it is the provisioned valve and wakes
// the executor; the executor stops its scan and grants the claim (ble_valve_claim_start(), on its
// own task), which issues the connect. request_hunt() keeps the old start_scan()'s checks and
// lines and wakes the executor.
// True from a hunt's "[SCAN] Starting scan for provisioned valve" until its claim, its link, its
// end by BLE_CMD_DISCONNECT: the old is_scanning, for those lines only.
static volatile bool s_hunt_announced = false;
// The valve was heard while the hunt wants it: a claim is due (host task sets, executor clears).
static volatile bool s_claim_req = false;
// When the valve was last heard while the hunt wanted it, or last claimed (0: never): the radio
// policy's hunt slot (B2) is left out for VALVE_HEARD_HOLD_MS after it, the claim then following
// on what the plain profile hears (ble_valve_hunt_slot_wanted()). Host task and executor write a
// tick, the executor reads it.
#define VALVE_HEARD_HOLD_MS  7000   // one I2b spacing at its longest: a claim may follow in it
static volatile TickType_t s_valve_heard_at = 0;

// ---- The valve claim policy (2.1.4 WP6; plan §4.4 CONNECT, decision D5) -----------------------
// Every claim is a CONNECT pulse: no BLE scan runs from its grant until its CONNECT event, for the
// length the radio policy grants (RP_CONNECT_LR_MS while a leak response is pending, otherwise
// RP_CONNECT_MS to RP_CONNECT_LR_MS within the blind budget, B2), and the executor follows it with a
// Coded recovery window (I2). The radio policy also spaces claims by its pulse-rate limit (I2b: 6-7 s
// of scanning between pulses, at most 12 s of pulses in any 60 s).
// A claim fails when it gives no link: no link within its pulse, or a connect refused or not
// started. Failures in a row back the next claim off by k_claim_backoff_s[], from the SECOND in a
// row on (B2 of WP8's core stage: a powered valve whose few adverts in one pulse were all lost is
// claimed again after the pulse spacing alone, so a single empty pulse no longer costs a 10 s
// back-off on top of it); a link held CLAIM_HELD_MS resets the back-off. A link that reached
// CONNECT ends the run of empty claims, and otherwise, lost sooner (a 0x3E, a valve power-cycled, a
// failed pairing, or a link this module drops), neither counts nor resets: the next claim may follow
// at once, within I2b, as 2.1.3 relinked at once (plan §12 G6b and D6: relink within 10 s after a
// 0x3E, with no CLOSE pended).
// While a leak response is pending (s_lr_trigger) no back-off applies: claims are spaced by I2b
// only, so a pended RMLEAK / CLOSE reaches a valve within about 6-7 s of scanning once it is heard
// (D5: shutoff delayed, never dropped). A claim cancelled by this module (a DISCONNECT command, a
// target change, a connect it did not start) is no failure.
// Under s_mac_lock: written on the executor's task (grant), the host task (CONNECT, link loss)
// and the command task (link held).
#define CLAIM_HELD_MS  60000
#define CLAIM_EMPTY_FREE  1                 // empty claims in a row with no back-off (B2)
static const uint16_t k_claim_backoff_s[] = { 10, 30, 60, 300 };   // after 2, 3, 4, 5+ empty claims
#define CLAIM_FAILS_MAX  ((uint8_t)(sizeof(k_claim_backoff_s) / sizeof(k_claim_backoff_s[0])))
static uint8_t s_claim_empty = 0;           // claims in a row with no link at all, since the last CONNECT
static uint8_t s_claim_fails = 0;           // the back-off's step (0 .. CLAIM_FAILS_MAX): empty claims
                                            // in a row past CLAIM_EMPTY_FREE
static TickType_t s_claim_not_before = 0;   // the back-off: no claim before this tick
static bool s_claim_open = false;           // a claim was granted and has no link held yet
static TickType_t s_link_up_at = 0;         // the current link's CONNECT (s_claim_open only)

// ---- The leak-response trigger (2.1.4 WP6; plan §4.1, D5) -------------------------------------
// s_lr_trigger: a leak response is pending (ble_valve_lr_pending()). Its scanning overlay (NORMAL_LR,
// LR_AP) and the overlay's cap, RP_LR_OVERLAY_CAP_MS per episode, are the radio policy's (WP8:
// radio_policy_note_lr(), radio_policy_lr_overlay()): the incident latch stays set until LEAK_RESET,
// so without the cap a dead valve would keep the hub on the 1M-weighted profile, with its weaker
// sensor coverage, for good. An episode ends when no RMLEAK / CLOSE is pended and no incident is
// latched. Command task (lr_poll()) writes them.
static volatile bool s_lr_trigger = false;
static bool s_interlock_ok = false;         // the valve confirmed RMLEAK=1 + CLOSED in this incident

// True between issuing ble_gap_connect() and the BLE_GAP_EVENT_CONNECT that
// resolves it. Guards the claim (ble_valve_note_adv(), ble_valve_claim_start()) against
// duplicate advertisement reports — see the guard there for why this became load-bearing
// when filter_duplicates was turned off.
// Set BEFORE the connect is issued, so a connect in flight with it false is not this module's
// (link_poll()). Volatile: the command task reads it against ble_gap_conn_active().
static volatile bool g_connecting = false;
static bool g_ble_synced = false;
static bool g_connect_requested = false;

static uint8_t g_own_addr_type = BLE_OWN_ADDR_PUBLIC;

static char g_valve_mac[18] = {0};

// The valve's address: the advert a claim is for (ble_valve_note_adv(), host task; read by
// ble_valve_claim_start() on the executor's task), then the link's identity address (GAP
// CONNECT). Under s_mac_lock.
static ble_addr_t g_peer_addr;
static bool g_peer_addr_valid = false;

static uint16_t h_valve_char = 0, h_flood_char = 0, h_rmleak_char = 0, h_batt_char = 0;
static uint16_t h_valve_svc_end = 0, h_flood_svc_end = 0, h_batt_svc_end = 0;

static uint16_t h_dis_char = 0;
static uint16_t h_dis_svc_end = 0;
static char g_firmware_rev[32] = {0};

/* 0xFF = no real reading on this link: characteristic missing, read failed, or setup not
 * done yet. 0 is a REAL 0 %. It used to start (and reset) at 0, so an unknown battery was
 * published as battery:0 and rated as an empty one (BUG-1). */
static uint8_t g_val_battery = 0xFF;
static bool g_val_leak = false;
static int g_val_state = -1;
static bool g_val_rmleak = false;

// Commands pended for the provisioned valve's next setup completion (-1 = none, else the
// value). Every access is under s_mac_lock, and a pend checks s_cmd_gen in the same
// critical section (pending_update()), so it can never survive the valve-target flush.
// gatt_mutex does not cover a CMD_WR_RELINK pend: finish_cmd_write() makes it after
// write_cmd_with_retry() released the mutex, so a setup completion can run in between and
// miss it. finish_cmd_write() therefore re-checks the link after pending.
static int g_pending_valve_cmd = -1;
static int g_pending_rmleak_cmd = -1;
#define PEND_ANY (-2)   // pending_update(): match whatever the slot holds

// ---- Failed-write handling (G4c) --------------------------------------------
// A GATT write the host refuses (rc != 0) is never dropped. The command task retries it
// on the same link (CMD_WRITE_ATTEMPTS, 200 ms then 400 ms apart); when the link is gone,
// or every attempt failed, it is pended and the next setup completion replays it, and
// after failed attempts the link is dropped so that replay comes promptly, at most
// CMD_MAX_FORCED_RELINKS times in a row (drop_link_after_failed_write()). A replay that
// fails at setup completion goes to the command task through the replay token
// (post_replay_token()).
#define CMD_WRITE_ATTEMPTS   3
#define CMD_WR_NO_MUTEX     (-1)   // gatt_mutex not taken within 1 s
#define CMD_WR_STALE        (-2)   // issued before a valve target change: drop it
#define CMD_WR_RELINK       (-3)   // no set-up link to the provisioned valve: pend it
#define CMD_WR_MOVED        (-4)   // replay only: its pending slot no longer holds it

// BLE_HS_ENOMEM is not such a failure (B1). Every hub command takes two of NimBLE's GATT
// procedures (CONFIG_BT_NIMBLE_GATT_MAX_PROCS = 4), the write and its read-back, and the ATT
// requests go out one at a time per link, each procedure freed only when the valve answers
// (0.45-0.75 s on the bench). Setup completion's replay of both slots plus the rules engine's
// reconcile SET+CLOSE needs more than the pool holds: ENOMEM means it is busy with this hub's
// own commands, not that the link failed. The command task waits for a free procedure,
// CMD_BUSY_RETRY_MS apart for at most CMD_BUSY_MAX_MS, without using up an attempt, and does
// the same for the read-back (read_back_when_free()). Past that bound, or when setup
// completion's replay gets it, the command stays pended and the replay token replays it. The
// link is never dropped for it: the terminate would discard the commands queued on it.
#define CMD_BUSY_RETRY_MS    250
#define CMD_BUSY_MAX_MS      5000

// Forced reconnects after failed writes in a row, with no accepted live command that left
// nothing pended in between, before the hub stops forcing them (drop_link_after_failed_write()).
#define CMD_MAX_FORCED_RELINKS  3

// ble_cmd_queue items carry the command (bits 0-7) and the generation it was issued under
// (bits 8-31) in 4 bytes, the size of the one-enum message struct they replaced, so the
// queue costs no more RAM.
#define CMD_GEN_MASK        0x00FFFFFFu
#define CMD_ITEM(cmd, gen)  ((((uint32_t)(gen) & CMD_GEN_MASK) << 8) | ((uint32_t)(cmd) & 0xFFu))
#define CMD_ITEM_CMD(item)  ((ble_valve_cmd_t)((item) & 0xFFu))
#define CMD_ITEM_GEN(item)  (((item) >> 8) & CMD_GEN_MASK)

// Internal item, never a ble_valve_cmd_t: replay the commands still pended, RMLEAK first
// (post_replay_token(), replay_pending_cmds()).
#define CMD_REPLAY_TOKEN          0xFFu
#define CMD_ITEM_IS_REPLAY(item)  (((item) & 0xFFu) == CMD_REPLAY_TOKEN)

// Internal item, never a ble_valve_cmd_t: wake the command task for its per-pass checks
// (link_poll()) now rather than at its next CMD_POLL_MS timeout. Queued at the BACK, so a
// replay token keeps the front (replay_token_queued()); a full queue just leaves it to that timeout.
#define CMD_WAKE_TOKEN            0xFEu
#define CMD_ITEM_IS_WAKE(item)    (((item) & 0xFFu) == CMD_WAKE_TOKEN)

// ---- Hub-issued command settle barrier -------------------------------------
// ble_valve_open/close/set_rmleak only ENQUEUE onto ble_cmd_queue; the command
// task writes the command later, and the cached valve state (g_val_state /
// g_val_rmleak) changes only when the valve reports the new value back (its
// notify or the read-back, through on_notify()), never at the write. iothub_task
// runs at the same priority, so it is not preempted and can publish an event AND
// its coupled snapshot before the write goes out.
//
// The enqueue arms this barrier; the command task releases it once the write is
// issued, pended or dropped (finish_cmd_write()), NOT at the valve's report. A
// snapshot built in between can still show the PRE-command value: the report's
// own update (BLE_UPD_STATE / BLE_UPD_RMLEAK, posted only on a change) requests
// the snapshot that shows the new one (app_iothub.c). The snapshot flush block
// consults ble_valve_cmd_settling() and defers while it is set. The deadline is
// the backstop for commands that never reach GATT (link down, mutex timeout) so
// the snapshot is at worst late, never blocked.
#define VALVE_CMD_SETTLE_MS 1500

static atomic_int      g_cmd_inflight    = 0;
static _Atomic int64_t g_cmd_deadline_us = 0;

static void cmd_settle_arm(void)
{
    atomic_fetch_add(&g_cmd_inflight, 1);
    atomic_store(&g_cmd_deadline_us,
                 esp_timer_get_time() + (int64_t)VALVE_CMD_SETTLE_MS * 1000);
}

// Every arm is matched by exactly one release (a replay token from post_replay_token()
// arms too). The clamp at zero is a backstop: an unmatched release must not drive the
// count negative and wedge the barrier permanently open.
static void cmd_settle_release(void)
{
    int prev = atomic_fetch_sub(&g_cmd_inflight, 1);
    if (prev <= 0)
        atomic_store(&g_cmd_inflight, 0);
}

static TimerHandle_t sec_timeout_timer = NULL;
static TimerHandle_t discovery_timeout_timer = NULL;
static TimerHandle_t post_connect_timer = NULL;
static TimerHandle_t security_retry_timer = NULL;

static int g_security_retry_count = 0;

// Flag to suppress updates during initial setup (avoid publishing partial data)
static bool g_setup_in_progress = false;

// Provisioning support - target MAC filtering
static char g_target_valve_mac[18] = {0};
static bool g_has_target_mac = false;

// Guards g_target_valve_mac, g_has_target_mac and g_valve_mac: the target is written on
// the esp-mqtt / iothub tasks while the NimBLE host task matches against it, and a torn
// 18-byte read could match (or miss) the wrong valve. Held only around mem*/str* calls.
static portMUX_TYPE s_mac_lock = portMUX_INITIALIZER_UNLOCKED;

// True from rejecting a link to a valve that is not the provisioned one (GAP CONNECT)
// until its DISCONNECT, which then closes it silently. NimBLE host task only.
static bool s_rejecting_conn = false;

// True from drop_link_after_failed_write() dropping the link after failed writes (command
// task) until the next GAP CONNECT, DISCONNECT or TERM_FAILURE (host task). The link still
// reads as ready until its DISCONNECT lands, and a command written on it now could be lost
// with it: it is pended for the next link instead. Set BEFORE that link's terminate, and
// cleared again when the terminate fails other than with "already being dropped", so it
// never outlives the link it was set for.
static bool s_link_dropping = false;

// True from a GAP TERM_FAILURE on the current link until its DISCONNECT or the next CONNECT
// (host task). NimBLE keeps that link marked as terminating, so every later
// ble_gap_terminate() on it returns BLE_HS_EALREADY although no DISCONNECT is coming.
static bool s_term_failed = false;

// ---- A stale link handle (2.1.4 WP3; plan §4.7, decision D6, red team SR-1) ----------------
// NimBLE's connect re-attempt (CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT) handles a link that failed
// to be established (HCI 0x3E) by deleting it and issuing a new connect itself, with NO
// DISCONNECT event (ble_hs_hci_evt.c, ble_gap_master_connect_reattempt()). valve_conn_handle would
// then name a link that no longer exists: ble_valve_is_connected() stays true, every command pends
// on a link that still reads as ready, and every rescan stops at "Already connected", so a pended
// RMLEAK / CLOSE is never written. BLE_GAP_EVENT_REATTEMPT_COUNT closes that link on the host task
// (ble_gap_event()), and the command task checks the handle with ble_gap_conn_find() on every pass
// (link_poll()): a handle NimBLE no longer knows, and still not closed STALE_LINK_CONFIRM_MS later,
// is closed as if its DISCONNECT had come. The wait covers the legitimate gap: NimBLE deletes a
// link just before it delivers that link's DISCONNECT (ble_gap_conn_broken()). sdkconfig turns the
// re-attempt off (I5); this holds either way. Command task only.
#define STALE_LINK_CONFIRM_MS  1000
static uint16_t s_stale_handle = BLE_HS_CONN_HANDLE_NONE;   // handle seen with no NimBLE link
static TickType_t s_stale_since = 0;                        // ... first seen then

// ---- A claim's connect that NimBLE lost in flight (2.1.4, review of WP5/WP6) ------------------
// A scan's own end (LE Scan Timeout) that NimBLE processes after a claim's ble_gap_connect() began
// takes the connect with it: ble_gap_disc_complete() resets NimBLE's master state whatever it
// holds, and hands DISC_COMPLETE to the procedure's handler, this module's. No CONNECT follows:
// g_connecting would stay true for good, so the hunt would never be wanted again and a pended
// RMLEAK / CLOSE never written. The controller keeps initiating with no host timeout, and a link it
// makes is refused by the host (ble_gap_accept_master_conn(): ENOENT) and lives in the controller
// only, with the valve no longer advertising. Only a host reset clears that (ble_hs_sched_reset():
// HCI reset and resync, after which on_stack_sync() asks for the hunt again). The executor grants a
// claim only at a scan's own end, so no late end should meet a connect; this is the backstop:
// DISC_COMPLETE on this module's handler while its connect is in flight (host task), or that
// connect still not run by NimBLE ORPHAN_CONFIRM_MS after it was first seen so (link_poll()).
#define ORPHAN_CONFIRM_MS  1000
static volatile uint8_t s_connect_seq = 0;  // connects issued (ble_valve_claim_start())
static uint8_t s_orphan_seq = 0;            // link_poll(): the connect first seen lost ...
static TickType_t s_orphan_since = 0;       // ... and when (0: none)
static bool s_host_reset_asked = false;     // a host reset is on its way (under s_mac_lock)

// Copies the provisioned valve's MAC; returns false (out = "") when there is none.
static bool target_copy(char out[18])
{
    taskENTER_CRITICAL(&s_mac_lock);
    bool has = g_has_target_mac;
    memcpy(out, g_target_valve_mac, sizeof(g_target_valve_mac));
    taskEXIT_CRITICAL(&s_mac_lock);
    if (!has)
        out[0] = '\0';
    return has;
}

// True only while the current link is to the PROVISIONED valve. The single gate for
// everything that may act on, or report from, a link (P0-a).
// mac_out (18 bytes, or NULL): the link's MAC when true, else "". Copied in the gate's own
// critical section, so a health event stamped with it names the valve the gate passed.
static bool link_is_target_mac(char *mac_out)
{
    if (mac_out)
        mac_out[0] = '\0';
    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
        return false;
    taskENTER_CRITICAL(&s_mac_lock);
    bool match = g_has_target_mac && g_valve_mac[0] != '\0' &&
                 strcasecmp(g_valve_mac, g_target_valve_mac) == 0;
    if (match && mac_out)
        memcpy(mac_out, g_valve_mac, sizeof(g_valve_mac));
    taskEXIT_CRITICAL(&s_mac_lock);
    return match;
}

static bool link_is_target(void)
{
    return link_is_target_mac(NULL);
}

// Valve-command generation, under s_mac_lock. Bumped with every valve target change, in the
// same critical section that writes the new target and clears the pending slots. Each
// queued command carries the generation it was issued under (CMD_ITEM), and a pend or a
// retry re-checks it, so a command issued for the previous valve (or for none) is dropped
// wherever it is: queued, already dequeued, mid-retry or being replayed (P0-c).
static uint32_t s_cmd_gen = 0;

static uint32_t cmd_gen_now(void)
{
    taskENTER_CRITICAL(&s_mac_lock);
    uint32_t gen = s_cmd_gen;
    taskEXIT_CRITICAL(&s_mac_lock);
    return gen;
}

static bool cmd_gen_is_current(uint32_t gen)
{
    return gen == cmd_gen_now();
}

// Sets *slot (a pending slot) to `val` only while `gen` is still current and the slot holds
// `expect` (PEND_ANY = anything). False = the target changed, or the slot moved on.
static bool pending_update(int *slot, int expect, int val, uint32_t gen)
{
    taskENTER_CRITICAL(&s_mac_lock);
    bool ok = (gen == s_cmd_gen) && (expect == PEND_ANY || *slot == expect);
    if (ok)
        *slot = val;
    taskEXIT_CRITICAL(&s_mac_lock);
    return ok;
}

// The command pended in *slot (0/1), or -1: none, or `gen` is no longer current.
static int pending_get(const int *slot, uint32_t gen)
{
    taskENTER_CRITICAL(&s_mac_lock);
    int v = (gen == s_cmd_gen) ? *slot : -1;
    taskEXIT_CRITICAL(&s_mac_lock);
    return v;
}

// Links dropped after failed writes (drop_link_after_failed_write()) since the last live
// command the host accepted with nothing left pended. Under s_mac_lock. Counted for
// generation s_relink_gen only, so a valve target change starts a new count.
static uint8_t s_relink_count = 0;
static uint32_t s_relink_gen = 0;

// A live hub command (command task, not a replay) was accepted, after its slot was cleared:
// forced reconnects allowed again, once nothing is left pended. Not while a command of the
// other kind still waits: a valve that accepts one kind and keeps refusing the other would
// otherwise have every reconnect's accepted write re-arm the reconnects its failures used up.
// Never for a replay, at setup completion or from the replay token (B1): every forced
// reconnect ends in one, so a cycle of replay accepted -> live write fails -> forced reconnect
// would re-arm itself, and CMD_MAX_FORCED_RELINKS would never engage.
static void relink_count_reset(void)
{
    taskENTER_CRITICAL(&s_mac_lock);
    if (g_pending_valve_cmd < 0 && g_pending_rmleak_cmd < 0)
        s_relink_count = 0;
    taskEXIT_CRITICAL(&s_mac_lock);
}

// A leak response is pended for the provisioned valve: the RMLEAK interlock or a CLOSE. The
// rules engine pends both, RMLEAK first, when the valve is not linked, and
// ble_valve_cancel_pending_close() withdraws them when the leak is over. The slots are
// emptied at every valve target change, so they always belong to the valve provisioned now.
static bool leak_response_pending(void)
{
    taskENTER_CRITICAL(&s_mac_lock);
    bool pending = (g_pending_valve_cmd == 0 || g_pending_rmleak_cmd == 1);
    taskEXIT_CRITICAL(&s_mac_lock);
    return pending;
}

// Forward declarations
static int ble_gap_event(struct ble_gap_event *event, void *arg);
static void request_hunt(void);
static void start_discovery_chain(void);
static void sec_timeout_cb(TimerHandle_t xTimer);
static void discovery_timeout_cb(TimerHandle_t xTimer);
static void post_connect_timer_cb(TimerHandle_t xTimer);
static void security_retry_timer_cb(TimerHandle_t xTimer);
static void initiate_security(void);
static bool link_stale_check(void);
static void claim_end(bool failed, const char *why);

// -----------------------------------------------------------------------------
// DEBUG HELPER
// -----------------------------------------------------------------------------
static void print_hex_dump(const char *prefix, const uint8_t *data, uint16_t len)
{
    char buf[64];
    int offset = 0;
    for (uint16_t i = 0; i < len && offset < 60; i++)
    {
        offset += snprintf(buf + offset, sizeof(buf) - offset, "%02X ", data[i]);
    }
    ESP_LOGI(BLE_TAG, "%s [%u bytes]: %s", prefix, len, buf);
}

static const char* state_bits_to_str(EventBits_t bits)
{
    static char buf[128];
    snprintf(buf, sizeof(buf), "CONN=%d PAIR=%d ENC=%d AUTH=%d BOND=%d DISC=%d",
             (bits & BLE_STATE_BIT_CONNECTED) ? 1 : 0,
             (bits & BLE_STATE_BIT_PAIRING) ? 1 : 0,
             (bits & BLE_STATE_BIT_ENCRYPTED) ? 1 : 0,
             (bits & BLE_STATE_BIT_AUTHENTICATED) ? 1 : 0,
             (bits & BLE_STATE_BIT_BONDED) ? 1 : 0,
             (bits & BLE_STATE_BIT_DISCOVERY_DONE) ? 1 : 0);
    return buf;
}

// -----------------------------------------------------------------------------
// EVENT GROUP HELPERS
// -----------------------------------------------------------------------------
static void set_state_bit(EventBits_t bit)
{
    if (ble_state_event_group != NULL)
    {
        xEventGroupSetBits(ble_state_event_group, bit);
        ESP_LOGI(BLE_TAG, "[STATE] Set bit 0x%02X -> %s", (unsigned)bit, state_bits_to_str(xEventGroupGetBits(ble_state_event_group)));
    }
}

static void clear_state_bit(EventBits_t bit)
{
    if (ble_state_event_group != NULL)
    {
        xEventGroupClearBits(ble_state_event_group, bit);
        ESP_LOGI(BLE_TAG, "[STATE] Clear bit 0x%02X -> %s", (unsigned)bit, state_bits_to_str(xEventGroupGetBits(ble_state_event_group)));
    }
}

static void clear_all_state_bits(void)
{
    if (ble_state_event_group != NULL)
    {
        xEventGroupClearBits(ble_state_event_group,
            BLE_STATE_BIT_CONNECTED | BLE_STATE_BIT_PAIRING | BLE_STATE_BIT_ENCRYPTED |
            BLE_STATE_BIT_AUTHENTICATED | BLE_STATE_BIT_BONDED | BLE_STATE_BIT_DISCOVERY_DONE);
        ESP_LOGI(BLE_TAG, "[STATE] All bits cleared");
    }
}

static EventBits_t get_state_bits(void)
{
    if (ble_state_event_group != NULL)
    {
        return xEventGroupGetBits(ble_state_event_group);
    }
    return 0;
}

static bool is_link_encrypted(void)
{
    return (get_state_bits() & BLE_STATE_BIT_ENCRYPTED) != 0;
}

static bool is_ready_for_gatt(void)
{
    EventBits_t bits = get_state_bits();
    return (bits & BLE_STATE_BIT_READY_FOR_GATT) == BLE_STATE_BIT_READY_FOR_GATT;
}

// A hub command may be written now: the link is the provisioned valve's, fully set up, not
// being dropped, and `handle` (its characteristic) was discovered.
static bool cmd_link_ready(uint16_t handle)
{
    return handle != 0 && !s_link_dropping && valve_conn_handle != BLE_HS_CONN_HANDLE_NONE &&
           is_ready_for_gatt() && link_is_target();
}

// -----------------------------------------------------------------------------
// SECURITY INITIATION
// Called after connection to start the pairing/encryption process
// -----------------------------------------------------------------------------
static void initiate_security(void)
{
    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(BLE_TAG, "[SECURITY] Cannot initiate - no connection");
        return;
    }

    // Never pair with (or encrypt to) a valve that is not the provisioned one: the fixed
    // passkey would bond it. Drop the link instead of leaving it idle.
    if (!link_is_target())
    {
        ESP_LOGW(BLE_TAG, "[SECURITY] Not initiating - link is not the provisioned valve; disconnecting");
        ble_gap_terminate(valve_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }

    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(valve_conn_handle, &desc);
    if (rc != 0)
    {
        ESP_LOGE(BLE_TAG, "[SECURITY] Cannot find connection: rc=%d", rc);
        return;
    }

    ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(BLE_TAG, "║            SECURITY INITIATION (attempt %d/%d)                ║",
             g_security_retry_count + 1, MAX_SECURITY_RETRIES);
    ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");

    ESP_LOGI(BLE_TAG, "[SECURITY] Connection handle: %d", valve_conn_handle);
    ESP_LOGI(BLE_TAG, "[SECURITY] Role: %s", (desc.role == BLE_GAP_ROLE_MASTER) ? "MASTER/CENTRAL" : "SLAVE/PERIPHERAL");
    ESP_LOGI(BLE_TAG, "[SECURITY] Peer addr: %02X:%02X:%02X:%02X:%02X:%02X",
             desc.peer_id_addr.val[5], desc.peer_id_addr.val[4], desc.peer_id_addr.val[3],
             desc.peer_id_addr.val[2], desc.peer_id_addr.val[1], desc.peer_id_addr.val[0]);
    ESP_LOGI(BLE_TAG, "[SECURITY] Current state: encrypted=%d, authenticated=%d, bonded=%d, key_size=%d",
             desc.sec_state.encrypted, desc.sec_state.authenticated,
             desc.sec_state.bonded, desc.sec_state.key_size);

    // If already encrypted (reconnection with bonded device), proceed to discovery
    if (desc.sec_state.encrypted)
    {
        ESP_LOGI(BLE_TAG, "[SECURITY] Already encrypted (bonded reconnection)");

        set_state_bit(BLE_STATE_BIT_ENCRYPTED);

        if (desc.sec_state.authenticated)
            set_state_bit(BLE_STATE_BIT_AUTHENTICATED);
        if (desc.sec_state.bonded)
            set_state_bit(BLE_STATE_BIT_BONDED);

        if (sec_timeout_timer)
            xTimerStop(sec_timeout_timer, 0);

        ESP_LOGI(BLE_TAG, "[SECURITY] Proceeding to service discovery...");
        start_discovery_chain();
        return;
    }

    // Not encrypted - need to initiate pairing
    ESP_LOGI(BLE_TAG, "[SECURITY] Link not encrypted. Initiating pairing...");
    set_state_bit(BLE_STATE_BIT_PAIRING);

    if (sec_timeout_timer)
        xTimerReset(sec_timeout_timer, 0);

    ESP_LOGI(BLE_TAG, "[SECURITY] SM Config: io_cap=%d, bonding=%d, mitm=%d, sc=%d",
             ble_hs_cfg.sm_io_cap, ble_hs_cfg.sm_bonding,
             ble_hs_cfg.sm_mitm, ble_hs_cfg.sm_sc);

    ESP_LOGI(BLE_TAG, "[SECURITY] Calling ble_gap_security_initiate(handle=%d)...", valve_conn_handle);
    rc = ble_gap_security_initiate(valve_conn_handle);

    if (rc == 0)
    {
        ESP_LOGI(BLE_TAG, "[SECURITY] ble_gap_security_initiate() SUCCESS - pairing started");
        return;
    }

    const char *err_str = "UNKNOWN";
    switch (rc) {
        case BLE_HS_EAGAIN: err_str = "BLE_HS_EAGAIN (busy)"; break;
        case BLE_HS_EALREADY: err_str = "BLE_HS_EALREADY (in progress)"; break;
        case BLE_HS_ENOTCONN: err_str = "BLE_HS_ENOTCONN (not connected)"; break;
        case BLE_HS_ENOTSUP: err_str = "BLE_HS_ENOTSUP (not supported)"; break;
    }
    ESP_LOGE(BLE_TAG, "[SECURITY] ble_gap_security_initiate() FAILED: rc=%d (%s)", rc, err_str);

    // Try ble_gap_pair_initiate as fallback
    ESP_LOGI(BLE_TAG, "[SECURITY] Trying ble_gap_pair_initiate() as fallback...");
    rc = ble_gap_pair_initiate(valve_conn_handle);

    if (rc == 0)
    {
        ESP_LOGI(BLE_TAG, "[SECURITY] ble_gap_pair_initiate() SUCCESS - pairing started");
        return;
    }

    ESP_LOGE(BLE_TAG, "[SECURITY] ble_gap_pair_initiate() FAILED: rc=%d", rc);
    clear_state_bit(BLE_STATE_BIT_PAIRING);

    // Schedule retry if not exceeded max
    g_security_retry_count++;
    if (g_security_retry_count < MAX_SECURITY_RETRIES && security_retry_timer != NULL)
    {
        ESP_LOGW(BLE_TAG, "[SECURITY] Scheduling retry %d/%d in %d ms...",
                 g_security_retry_count, MAX_SECURITY_RETRIES, SECURITY_RETRY_DELAY_MS);
        xTimerStart(security_retry_timer, 0);
    }
    else
    {
        ESP_LOGE(BLE_TAG, "[SECURITY] Max retries exceeded. Starting discovery anyway...");
        start_discovery_chain();
    }
}

// -----------------------------------------------------------------------------
// TIMER CALLBACKS
// -----------------------------------------------------------------------------
static void security_retry_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;

    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(BLE_TAG, "[SECURITY_RETRY] Connection lost");
        return;
    }

    if (is_link_encrypted())
    {
        ESP_LOGI(BLE_TAG, "[SECURITY_RETRY] Already encrypted, skipping retry");
        return;
    }

    ESP_LOGI(BLE_TAG, "[SECURITY_RETRY] Retrying security initiation...");
    initiate_security();
}

static void post_connect_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;

    ESP_LOGI(BLE_TAG, "[TIMER] Post-connect delay complete.");

    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(BLE_TAG, "[TIMER] Connection lost");
        return;
    }

    g_security_retry_count = 0;
    ESP_LOGI(BLE_TAG, "[TIMER] Initiating security...");
    initiate_security();
}

static void sec_timeout_cb(TimerHandle_t xTimer)
{
    (void)xTimer;

    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
        return;

    if (!is_ready_for_gatt())
    {
        ESP_LOGE(BLE_TAG, "[TIMEOUT] Setup incomplete. State: %s", state_bits_to_str(get_state_bits()));
        ESP_LOGE(BLE_TAG, "[TIMEOUT] Disconnecting to retry...");
        ble_gap_terminate(valve_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static void discovery_timeout_cb(TimerHandle_t xTimer)
{
    (void)xTimer;

    if (valve_conn_handle != BLE_HS_CONN_HANDLE_NONE && !is_ready_for_gatt())
    {
        ESP_LOGE(BLE_TAG, "[TIMEOUT] Discovery timeout. Disconnecting...");
        ble_gap_terminate(valve_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

// Battery for the logs: "unknown" for 0xFF (see g_val_battery), else "NN%".
static const char *batt_to_str(uint8_t batt, char *buf, size_t len)
{
    if (batt == 0xFF) return "unknown";
    snprintf(buf, len, "%u%%", (unsigned)batt);
    return buf;
}

// -----------------------------------------------------------------------------
// UPDATE NOTIFY
// -----------------------------------------------------------------------------
// disc_mac: DISCONNECTED only, the MAC of the link that went down, which its handler sampled
// with its gate before clearing g_valve_mac. NULL for every other update.
static void notify_hub_update(ble_update_type_t update_type, const char *disc_mac)
{
    /* Only the provisioned valve may reach the hub and the health engine (P0-a). A link
     * can outlive its target (changed or removed while linked) until the queued DISCONNECT
     * lands. A DISCONNECTED is posted after the link is gone, so the DISCONNECT handler
     * makes that call itself. The health posts below carry the MAC the gate passed. */
    char mac[18] = {0};
    if (update_type != BLE_UPD_DISCONNECTED && !link_is_target_mac(mac))
    {
        ESP_LOGW(BLE_TAG, "[NOTIFY] update type=%d dropped - link is not the provisioned valve",
                 (int)update_type);
        return;
    }

    if (ble_update_queue != NULL)
    {
        /* Non-blocking, deliberately. An earlier version waited 50 ms for
         * CONNECTED/DISCONNECTED on the theory that losing one skips the
         * reconnect reconciliation — true, but the wait cannot help: the only
         * consumer is iothub_task, which frees a slot only by completing a whole
         * loop iteration, and that iteration contains the MQTT/DPS calls that can
         * block it for seconds. A slot never appears within 50 ms in any state
         * where the queue is actually full. All the wait bought was up to 50 ms
         * of stalled NimBLE host task — shared with the leak scanner — in the
         * disconnect path, precisely when scanning needs to restart.
         *
         * The real protection is queue depth (16) plus the correctly-sized
         * QueueSet in app_iothub.c. What matters here is that a drop is LOUD: it
         * used to be cast to void, so a full queue looked exactly like a healthy
         * one in every log. */
        bool critical = (update_type == BLE_UPD_CONNECTED ||
                         update_type == BLE_UPD_DISCONNECTED);
        if (xQueueSend(ble_update_queue, &update_type, 0) != pdTRUE) {
            ESP_LOGE(BLE_TAG, "[QUEUE] ble_update_queue FULL — dropped update type=%d%s",
                     (int)update_type,
                     critical ? " (CONNECTED/DISCONNECTED — reconnect reconciliation SKIPPED)"
                              : "");
        }
    }
    /* Health engine: a CONNECTED for every update that gets here, i.e. the link-up
     * and each valve value that CHANGED (on_notify() calls this only on a change).
     * It refreshes last_seen_ms and clears a stale disconnect. A steady valve's
     * values do not change for hours, so last_seen_ms does not track a live link:
     * the snapshot's last_seen_age_s for the valve comes from the link state
     * instead (0 while linked, see health_get_device_status_all()). */
    if (update_type == BLE_UPD_DISCONNECTED)
        health_post_valve_event(disc_mac, false);
    else if (update_type != BLE_UPD_NONE) {
        health_post_valve_event(mac, true);
        /* Battery resync at LINK-UP only. The battery itself reaches the health engine
         * from on_notify() on every read/notify; re-posting it on every STATE/LEAK/RMLEAK
         * update as well doubled the health-queue traffic (L19). This one covers a
         * setup-time post the 16-deep queue dropped. 0xFF (unknown) is ignored there. */
        if (update_type == BLE_UPD_CONNECTED)
            health_post_valve_battery(mac, g_val_battery);
    }
}

static int on_notify(uint16_t conn_handle, uint16_t attr_handle, struct os_mbuf *om, void *arg)
{
    (void)conn_handle;
    (void)arg;

    // Values from any other valve must not reach the cache, the health engine (flood probe,
    // battery) or the hub. Covers notifies and every read that lands here. The health posts
    // below carry the MAC this gate passed.
    char mac[18] = {0};
    if (!link_is_target_mac(mac))
    {
        ESP_LOGW(BLE_TAG, "[NOTIFY] attr_handle=%u ignored - link is not the provisioned valve",
                 attr_handle);
        return 0;
    }

    uint8_t data[16] = {0};
    uint16_t len = OS_MBUF_PKTLEN(om);
    if (len > sizeof(data))
        len = sizeof(data);

    os_mbuf_copydata(om, 0, len, data);

    ESP_LOGI(BLE_TAG, "[NOTIFY] attr_handle=%u, len=%u", attr_handle, len);
    print_hex_dump("Notify data", data, len);

    if (attr_handle == h_valve_char)
    {
        int old_state = g_val_state;
        g_val_state = data[0];
        ESP_LOGI(BLE_TAG, "[DATA] Valve State=%d (%s)", g_val_state, g_val_state ? "OPEN" : "CLOSED");
        if (old_state != g_val_state && !g_setup_in_progress)
            notify_hub_update(BLE_UPD_STATE, NULL);
    }
    else if (attr_handle == h_flood_char)
    {
        bool old_leak = g_val_leak;
        g_val_leak = (data[0] != 0);
        ESP_LOGI(BLE_TAG, "[DATA] Leak=%d (%s)", g_val_leak, g_val_leak ? "LEAK" : "OK");
        /* Posted unconditionally, NOT behind the delta gate below: this feeds the
         * health roll-up (a wet valve probe is CRITICAL), and the initial setup read
         * is exactly when the roll-up most needs the value. The health engine keys
         * off the value, not the edge, so a repeat post is a no-op. */
        health_post_valve_leak(mac, g_val_leak);
        if (old_leak != g_val_leak && !g_setup_in_progress)
            notify_hub_update(BLE_UPD_LEAK, NULL);
    }
    else if (attr_handle == h_rmleak_char)
    {
        bool old_rmleak = g_val_rmleak;
        g_val_rmleak = (data[0] != 0);
        ESP_LOGI(BLE_TAG, "[DATA] RMLEAK=%d (%s)", g_val_rmleak, g_val_rmleak ? "ACTIVE" : "CLEAR");
        if (old_rmleak != g_val_rmleak && !g_setup_in_progress)
            notify_hub_update(BLE_UPD_RMLEAK, NULL);
    }
    else if (attr_handle == h_batt_char)
    {
        // An empty payload is not a reading. data[] is zero-filled, so storing data[0]
        // here would invent a 0 % battery (BUG-1).
        if (len == 0)
        {
            ESP_LOGW(BLE_TAG, "[DATA] Battery notify/read with no payload - ignored");
            return 0;
        }
        uint8_t old_batt = g_val_battery;
        g_val_battery = data[0];
        char bstr[8];
        ESP_LOGI(BLE_TAG, "[DATA] Battery=%s", batt_to_str(g_val_battery, bstr, sizeof(bstr)));
        /* Posted unconditionally, NOT behind the delta or setup gates below — like the
         * flood probe above. The valve re-notifies every 20 s at <=10 %, and a steady
         * critical reading never passes the delta gate, so an earlier post the queue
         * dropped would otherwise never be corrected and the valve would never be rated
         * CRITICAL. The health engine keys off the value, so a repeat is a no-op. */
        health_post_valve_battery(mac, g_val_battery);
        if (old_batt != g_val_battery && !g_setup_in_progress)
            notify_hub_update(BLE_UPD_BATTERY, NULL);
    }
    else
    {
        ESP_LOGW(BLE_TAG, "[NOTIFY] Unknown attr_handle=%u", attr_handle);
    }

    return 0;
}

// -----------------------------------------------------------------------------
// SEQUENTIAL SETUP (subscribing to notifications and reading initial values)
// -----------------------------------------------------------------------------
static int setup_step = 0;
static void setup_next_step(void);

static int on_cccd_write_cb(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr,
                            void *arg)
{
    (void)attr;
    uint16_t chr_val_handle = (uint16_t)(uintptr_t)arg;

    if (error->status == 0)
    {
        ESP_LOGI(BLE_TAG, "[SETUP] CCCD enabled for chr val handle=%u", chr_val_handle);
    }
    else
    {
        ESP_LOGW(BLE_TAG, "[SETUP] CCCD enable failed for chr=%u status=0x%04X", chr_val_handle, error->status);

        if (error->status == BLE_HS_ATT_ERR(ATT_ERR_INSUFFICIENT_AUTHEN) ||
            error->status == BLE_HS_ATT_ERR(ATT_ERR_INSUFFICIENT_ENC))
        {
            if (!is_link_encrypted() && conn_handle != BLE_HS_CONN_HANDLE_NONE)
            {
                ESP_LOGI(BLE_TAG, "[SETUP] Auth error - triggering security...");
                g_security_retry_count = 0;
                initiate_security();
                return 0;
            }
        }
    }

    setup_next_step();
    return 0;
}

static int on_dsc_disc_cb(uint16_t conn_handle,
                          const struct ble_gatt_error *error,
                          uint16_t chr_val_handle,
                          const struct ble_gatt_dsc *dsc,
                          void *arg)
{
    (void)arg;

    if (error->status == 0)
    {
        uint16_t uuid16 = ble_uuid_u16(&dsc->uuid.u);
        ESP_LOGI(BLE_TAG, "[SETUP] Descriptor: handle=%u, uuid16=0x%04X", dsc->handle, uuid16);

        if (uuid16 == BLE_GATT_DSC_CLT_CFG_UUID16)
        {
            ESP_LOGI(BLE_TAG, "[SETUP] CCCD found at handle=%u, enabling notifications", dsc->handle);
            uint8_t cccd[2] = {0x01, 0x00};
            int rc = ble_gattc_write_flat(conn_handle, dsc->handle, cccd, sizeof(cccd),
                                          on_cccd_write_cb, (void *)(uintptr_t)chr_val_handle);
            if (rc != 0)
            {
                ESP_LOGE(BLE_TAG, "[SETUP] CCCD write start failed rc=%d", rc);
                setup_next_step();
            }
            return BLE_HS_EDONE;
        }
        return 0;
    }

    if (error->status == BLE_HS_EDONE)
    {
        ESP_LOGW(BLE_TAG, "[SETUP] No CCCD found for chr=%u", chr_val_handle);
    }
    else
    {
        ESP_LOGE(BLE_TAG, "[SETUP] Descriptor discovery error status=%d", error->status);
    }

    setup_next_step();
    return 0;
}

// Shared body for every GATT read completion: fold the value into the cache via
// on_notify(), or recover from an auth error. Returns true when the caller may
// advance the setup state machine, false when the read was consumed by a
// security re-initiation (which restarts the chain itself).
static bool handle_read_result(uint16_t conn_handle,
                               const struct ble_gatt_error *error,
                               struct ble_gatt_attr *attr)
{
    if (error->status == 0 && attr != NULL)
    {
        ESP_LOGI(BLE_TAG, "[READ] Read success: handle=%u", attr->handle);
        if (attr->om != NULL)
        {
            uint8_t data[16] = {0};
            uint16_t len = OS_MBUF_PKTLEN(attr->om);
            if (len > sizeof(data))
                len = sizeof(data);
            os_mbuf_copydata(attr->om, 0, len, data);
            print_hex_dump("Read data", data, len);
        }
        on_notify(conn_handle, attr->handle, attr->om, NULL);
    }
    else
    {
        ESP_LOGW(BLE_TAG, "[READ] Read failed status=0x%04X", error->status);

        if (error->status == BLE_HS_ATT_ERR(ATT_ERR_INSUFFICIENT_AUTHEN) ||
            error->status == BLE_HS_ATT_ERR(ATT_ERR_INSUFFICIENT_ENC))
        {
            if (!is_link_encrypted() && conn_handle != BLE_HS_CONN_HANDLE_NONE)
            {
                ESP_LOGI(BLE_TAG, "[READ] Auth error on read - triggering security...");
                g_security_retry_count = 0;
                initiate_security();
                return false;
            }
        }
    }

    return true;
}

// Read completion for the SETUP CHAIN ONLY (steps 5-8). Advances the state
// machine, which is what those steps rely on to walk to the next one.
static int on_read_cb(uint16_t conn_handle,
                      const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr,
                      void *arg)
{
    (void)arg;

    if (handle_read_result(conn_handle, error, attr))
        setup_next_step();
    return 0;
}

// Read completion for POST-WRITE COMMAND READ-BACKS. Updates the cache and
// stops — it must NEVER touch the setup state machine.
//
// This split exists because sharing on_read_cb() with the command read-backs
// caused an unbounded telemetry/GATT storm in 2.1.1. setup_step is a file-static
// that only discovery resets; after a completed session it rests at its terminal
// value. Every command read-back incremented it again (Step 11, 12, 13 ...), each
// landing in setup_next_step()'s default: arm, which re-ran the "SETUP COMPLETE"
// block and re-posted BLE_UPD_CONNECTED. iothub_task turned each of those into
// rules_engine_on_valve_connected(), whose Priority 1 re-issued RMLEAK + close
// while a sensor was wet — writes that read back and re-entered the same arm.
// Field capture: 35+ `auto_close {"cause":"reconnect"}` events in 40 s, one NVS
// incident commit each, with no intervening DISCONNECTED.
static int on_cmd_read_cb(uint16_t conn_handle,
                          const struct ble_gatt_error *error,
                          struct ble_gatt_attr *attr,
                          void *arg)
{
    (void)arg;

    (void)handle_read_result(conn_handle, error, attr);
    return 0;
}

static int on_read_dis_cb(uint16_t conn_handle,
                          const struct ble_gatt_error *error,
                          struct ble_gatt_attr *attr,
                          void *arg)
{
    (void)conn_handle;
    (void)arg;

    if (error->status == 0 && attr != NULL && attr->om != NULL)
    {
        uint16_t len = OS_MBUF_PKTLEN(attr->om);
        if (len > sizeof(g_firmware_rev) - 1)
            len = sizeof(g_firmware_rev) - 1;
        os_mbuf_copydata(attr->om, 0, len, g_firmware_rev);
        g_firmware_rev[len] = '\0';
        ESP_LOGI(BLE_TAG, "[SETUP] Valve Firmware Rev: \"%s\"", g_firmware_rev);
    }
    else
    {
        ESP_LOGW(BLE_TAG, "[SETUP] DIS firmware rev read failed status=0x%04X", error->status);
        g_firmware_rev[0] = '\0';
    }

    setup_next_step();
    return 0;
}

// Replays the command pended in *slot on a freshly set-up link (setup completion, NimBLE
// host task). Only on the provisioned valve's link: a command pended for it must never be
// written to whichever valve happens to finish setup (P0-c).
//
// The command stays in its slot until the host accepts the write, so
// ble_valve_cancel_pending_close() and a newer command of its kind keep acting on it. The
// slot is re-checked under gatt_mutex, where every accepted hub write clears the older
// command pended for its kind (write_cmd_with_retry()): a newer command the command task
// wrote first is never undone by this older one.
// Returns true when the slot still holds a command after a failure on the live link (mutex
// timeout, rc != 0, or a newer command of its kind pended meanwhile): the caller then has
// the command task replay it (post_replay_token()). False: written, nothing pended, or it
// waits for the next link. A busy GATT procedure pool (BLE_HS_ENOMEM, CMD_BUSY_RETRY_MS) is
// such an rc: this task never waits for a procedure, the command task's replay does.
static bool apply_pending_cmd(int *slot, uint16_t handle, bool is_rmleak)
{
    const char *what = is_rmleak ? "RMLEAK" : "valve";

    if (!cmd_link_ready(handle))
        return false;

    taskENTER_CRITICAL(&s_mac_lock);
    int v = *slot;
    uint32_t gen = s_cmd_gen;
    taskEXIT_CRITICAL(&s_mac_lock);
    if (v != 0 && v != 1)
        return false;

    ESP_LOGI(BLE_TAG, "[CMD] Applying pending %s command=%d", what, v);

    if (gatt_mutex == NULL || xSemaphoreTake(gatt_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGW(BLE_TAG, "[CMD] Pending %s command=%d: GATT mutex timeout", what, v);
        return true;
    }

    // Re-checked after the wait: the target may have changed (its flush already
    // cleared the slot) or the link dropped (the slot waits for the next one).
    if (!cmd_gen_is_current(gen) || !cmd_link_ready(handle))
    {
        xSemaphoreGive(gatt_mutex);
        ESP_LOGW(BLE_TAG, "[CMD] Pending %s command=%d not applied - valve target or link changed", what, v);
        return false;
    }
    // ...or the command was cancelled, or superseded by a newer one of its kind that the
    // command task wrote while this task waited. One it pended instead (its own write
    // failed) is left to the command task's replay, still ahead of the valve command.
    int now = pending_get(slot, gen);
    if (now != v)
    {
        xSemaphoreGive(gatt_mutex);
        ESP_LOGW(BLE_TAG, "[CMD] Pending %s command=%d cancelled or superseded meanwhile - not applied",
                 what, v);
        return now == 0 || now == 1;
    }

    uint8_t b = (uint8_t)v;
    int rc = ble_gattc_write_flat(valve_conn_handle, handle, &b, 1, NULL, NULL);
    ESP_LOGI(BLE_TAG, "[CMD] Pending %s write rc=%d (value awaits the valve's own report)", what, rc);
    // Deliberately does NOT cache `b`, and reads straight back: a backstop for the
    // position (no VALVESTATE subscription), mandatory for RMLEAK (the valve does
    // not echo REMOTE_LEAK on the write path). See write_valve_command() and
    // write_rmleak_command().
    bool retry = false;
    if (rc == 0)
    {
        // Out of the slot before gatt_mutex is released, like every accepted hub write. A
        // replay does not reset the forced-reconnect count (relink_count_reset()).
        (void)pending_update(slot, v, -1, gen);
        int rrc = ble_gattc_read(valve_conn_handle, handle, on_cmd_read_cb, NULL);
        if (rrc != 0)
            ESP_LOGW(BLE_TAG, "[CMD] %s read-back rc=%d - %s unconfirmed", what, rrc,
                     is_rmleak ? "interlock state" : "position");
    }
    else
    {
        // Stays pended. On a link that is still up the command task retries it; a lost link
        // replays it at the next setup completion.
        retry = (rc != BLE_HS_ENOTCONN && link_is_target());
    }
    xSemaphoreGive(gatt_mutex);
    return retry;
}

// A replay token of generation `gen` waits at the front of ble_cmd_queue. Only
// post_replay_token() queues at the front, and only the command task takes items, so it stays
// there until the command task takes it.
static bool replay_token_queued(uint32_t gen)
{
    uint32_t front = 0;
    return ble_cmd_queue != NULL && xQueuePeek(ble_cmd_queue, &front, 0) == pdTRUE &&
           CMD_ITEM_IS_REPLAY(front) && CMD_ITEM_GEN(front) == gen;
}

// Queues the replay token at the FRONT of ble_cmd_queue (setup completion, NimBLE host task)
// for the pended commands apply_pending_cmd() could not write, or that the command task
// pended just as a setup completed (finish_cmd_write()). The command task replays them
// (replay_pending_cmds()) ahead of every command queued meanwhile, all of them newer, which
// therefore still land last and win. At most one token is outstanding: it replays whatever
// is pended when it is taken. A full queue leaves the commands pended for the next link.
// Arms the settle barrier like a hub command; replay_pending_cmds() releases it.
static void post_replay_token(void)
{
    if (ble_cmd_queue == NULL)
        return;

    // A token not yet taken is always at the front, so the queue itself is the "one is
    // outstanding" flag: nothing to reset when a valve target change wipes the queue. One of
    // an older generation is dropped when taken, so it does not count.
    uint32_t gen = cmd_gen_now();
    if (replay_token_queued(gen))
    {
        ESP_LOGW(BLE_TAG, "[CMD] Pending valve commands not applied - left to the replay already queued");
        return;
    }

    uint32_t item = CMD_ITEM(CMD_REPLAY_TOKEN, gen);
    cmd_settle_arm();
    if (xQueueSendToFront(ble_cmd_queue, &item, 0) == pdTRUE)
    {
        ESP_LOGW(BLE_TAG, "[CMD] Pending valve commands not applied - replay queued ahead of newer commands");
        return;
    }

    cmd_settle_release();
    ESP_LOGE(BLE_TAG, "[CMD] Pending valve commands not applied, command queue full - kept for the next link");
}

static void setup_next_step(void)
{
    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(BLE_TAG, "[SETUP] Connection lost during setup");
        return;
    }

    setup_step++;
    ESP_LOGI(BLE_TAG, "[SETUP] Step %d", setup_step);

    switch (setup_step)
    {
    case 1:
        if (h_valve_char && h_valve_svc_end)
        {
            ESP_LOGI(BLE_TAG, "[SETUP] Subscribe VALVE (chr=%u, end=%u)", h_valve_char, h_valve_svc_end);
            int rc = ble_gattc_disc_all_dscs(valve_conn_handle, h_valve_char, h_valve_svc_end, on_dsc_disc_cb, NULL);
            if (rc == 0) return;
            ESP_LOGE(BLE_TAG, "[SETUP] disc dsc valve rc=%d", rc);
        }
        setup_next_step();
        break;

    case 2:
        if (h_flood_char && h_flood_svc_end)
        {
            ESP_LOGI(BLE_TAG, "[SETUP] Subscribe FLOOD (chr=%u, end=%u)", h_flood_char, h_flood_svc_end);
            int rc = ble_gattc_disc_all_dscs(valve_conn_handle, h_flood_char, h_flood_svc_end, on_dsc_disc_cb, NULL);
            if (rc == 0) return;
            ESP_LOGE(BLE_TAG, "[SETUP] disc dsc flood rc=%d", rc);
        }
        setup_next_step();
        break;

    case 3:
        if (h_rmleak_char && h_flood_svc_end)
        {
            ESP_LOGI(BLE_TAG, "[SETUP] Subscribe RMLEAK (chr=%u, end=%u)", h_rmleak_char, h_flood_svc_end);
            int rc = ble_gattc_disc_all_dscs(valve_conn_handle, h_rmleak_char, h_flood_svc_end, on_dsc_disc_cb, NULL);
            if (rc == 0) return;
            ESP_LOGE(BLE_TAG, "[SETUP] disc dsc rmleak rc=%d", rc);
        }
        setup_next_step();
        break;

    case 4:
        if (h_batt_char && h_batt_svc_end)
        {
            ESP_LOGI(BLE_TAG, "[SETUP] Subscribe BATT (chr=%u, end=%u)", h_batt_char, h_batt_svc_end);
            int rc = ble_gattc_disc_all_dscs(valve_conn_handle, h_batt_char, h_batt_svc_end, on_dsc_disc_cb, NULL);
            if (rc == 0) return;
            ESP_LOGE(BLE_TAG, "[SETUP] disc dsc batt rc=%d", rc);
        }
        setup_next_step();
        break;

    case 5:
        if (h_valve_char)
        {
            ESP_LOGI(BLE_TAG, "[SETUP] Read VALVE");
            int rc = ble_gattc_read(valve_conn_handle, h_valve_char, on_read_cb, NULL);
            if (rc == 0) return;
            ESP_LOGE(BLE_TAG, "[SETUP] read valve rc=%d", rc);
        }
        setup_next_step();
        break;

    case 6:
        if (h_flood_char)
        {
            ESP_LOGI(BLE_TAG, "[SETUP] Read FLOOD");
            int rc = ble_gattc_read(valve_conn_handle, h_flood_char, on_read_cb, NULL);
            if (rc == 0) return;
            ESP_LOGE(BLE_TAG, "[SETUP] read flood rc=%d", rc);
        }
        setup_next_step();
        break;

    case 7:
        if (h_rmleak_char)
        {
            ESP_LOGI(BLE_TAG, "[SETUP] Read RMLEAK");
            int rc = ble_gattc_read(valve_conn_handle, h_rmleak_char, on_read_cb, NULL);
            if (rc == 0) return;
            ESP_LOGE(BLE_TAG, "[SETUP] read rmleak rc=%d", rc);
        }
        setup_next_step();
        break;

    case 8:
        if (h_batt_char)
        {
            ESP_LOGI(BLE_TAG, "[SETUP] Read BATT");
            int rc = ble_gattc_read(valve_conn_handle, h_batt_char, on_read_cb, NULL);
            if (rc == 0) return;
            ESP_LOGE(BLE_TAG, "[SETUP] read batt rc=%d", rc);
        }
        setup_next_step();
        break;

    case 9:
        if (h_dis_char)
        {
            ESP_LOGI(BLE_TAG, "[SETUP] Read DIS Firmware Rev");
            int rc = ble_gattc_read(valve_conn_handle, h_dis_char, on_read_dis_cb, NULL);
            if (rc == 0) return;
            ESP_LOGE(BLE_TAG, "[SETUP] read DIS fw_rev rc=%d", rc);
        }
        setup_next_step();
        break;

    default:
        // Done with setup.
        //
        // IDEMPOTENCE GUARD: this arm announces a NEW link (BLE_UPD_CONNECTED),
        // which iothub_task turns into rules_engine_on_valve_connected() — a
        // reconciliation that may close the valve and write NVS. Re-announcing a
        // link that is already up is therefore never harmless, so refuse to do it.
        //
        // setup_step only ever resets in the discovery callbacks, so any stray
        // increment after a completed session lands here. The command read-backs
        // used to supply exactly that (see on_cmd_read_cb) and produced an
        // unbounded auto_close storm. That specific route is now cut at the source;
        // this is the backstop that keeps any future one from reaching the same arm.
        if (get_state_bits() & BLE_STATE_BIT_DISCOVERY_DONE)
        {
            ESP_LOGD(BLE_TAG, "[SETUP] Step %d ignored — link already set up", setup_step);
            break;
        }

        g_security_retry_count = 0;

        if (discovery_timeout_timer)
            xTimerStop(discovery_timeout_timer, 0);
        if (sec_timeout_timer)
            xTimerStop(sec_timeout_timer, 0);

        set_state_bit(BLE_STATE_BIT_DISCOVERY_DONE);

        ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
        ESP_LOGI(BLE_TAG, "║            SETUP COMPLETE - READY FOR GATT                   ║");
        ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");
        ESP_LOGI(BLE_TAG, "[READY] Valve=%u, Flood=%u, RMLEAK=%u, Batt=%u, DIS=%u",
                 h_valve_char, h_flood_char, h_rmleak_char, h_batt_char, h_dis_char);
        ESP_LOGI(BLE_TAG, "[READY] FW Rev: \"%s\"", g_firmware_rev[0] ? g_firmware_rev : "(not available)");
        char bstr[8];
        ESP_LOGI(BLE_TAG, "[READY] Battery=%s, Leak=%s, Valve=%s, RMLEAK=%s",
                 batt_to_str(g_val_battery, bstr, sizeof(bstr)),
                 g_val_leak ? "LEAK" : "OK",
                 g_val_state == 1 ? "OPEN" : (g_val_state == 0 ? "CLOSED" : "UNKNOWN"),
                 g_val_rmleak ? "ACTIVE" : "CLEAR");
        ESP_LOGI(BLE_TAG, "[READY] State: %s", state_bits_to_str(get_state_bits()));

        g_setup_in_progress = false;
        notify_hub_update(BLE_UPD_CONNECTED, NULL);

        /* ANNOUNCE THE FLOOD PROBE STATE AT LINK-UP, WET OR DRY.
         *
         * Setup step 6 reads the flood characteristic, so g_val_leak is populated by
         * now — but that read arrives while g_setup_in_progress is still true, which
         * suppresses BLE_UPD_LEAK in on_notify(). And BLE_UPD_LEAK is the ONLY thing
         * that makes iothub_task call rules_engine_evaluate_leak(LEAK_SOURCE_VALVE,...):
         * on_valve_connected()'s Priority 1 keys off g_active_leak_count, which this
         * leak was never entered into, so nothing downstream would ever act on it.
         *
         * Without this, a hub that boots (or a valve that reconnects) into a flood AT
         * THE VALVE reports system_health critical and drives the fleet LED red while
         * leaving the valve OPEN indefinitely — it only ever closed on a dry->wet EDGE
         * observed after setup. The setup-time suppression itself is correct (no events
         * mid-chain); the missing piece was the catch-up once the link is genuinely up.
         *
         * Announced for DRY as well as wet, deliberately. Sending it only when wet left
         * two holes: a valve that reconnects dry after a wet episode never published
         * leak_cleared (the CONNECT handler zeroes the cache, so on_notify sees no
         * edge), and it never told the rules engine to drop the valve from its
         * active-leak table. Reporting both edges makes the consumer's delta gate the
         * single arbiter — see s_valve_pub_wet in app_iothub.c, initialised to 0 so a
         * routine dry link-up publishes nothing.
         *
         * Posted AFTER BLE_UPD_CONNECTED so reconciliation runs first: iothub dequeues
         * one update per iteration, so this lands on the next pass and goes through the
         * normal, already-tested BLE_UPD_LEAK path, which samples the valve state
         * consistently before calling the rules engine. */
        if (g_val_leak)
            ESP_LOGW(BLE_TAG, "[READY] Flood probe already WET at link-up — announcing for evaluation");
        else
            ESP_LOGI(BLE_TAG, "[READY] Announcing flood probe state (dry) for reconciliation");
        notify_hub_update(BLE_UPD_LEAK, NULL);

        /* RMLEAK before the valve command, the order every live pair is issued in (rules
         * engine: set_rmleak(true) then close, set_rmleak(false) then open). A CLOSE must
         * not land ahead of its interlock, nor an OPEN ahead of the interlock's release.
         * A command that could not be written now stays pended, and the command task
         * replays it from a token queued AHEAD of every newer command (post_replay_token()),
         * RMLEAK first. If the RMLEAK command failed, the valve command is left to that
         * replay (or to the next link) instead of overtaking it. */
        {
            bool retry = apply_pending_cmd(&g_pending_rmleak_cmd, h_rmleak_char, true);
            if (!retry)
                retry = apply_pending_cmd(&g_pending_valve_cmd, h_valve_char, false);
            if (retry)
                post_replay_token();
        }
        break;
    }
}

// -----------------------------------------------------------------------------
// DISCOVERY CHAIN
// -----------------------------------------------------------------------------
static int on_disc_dis_chr(uint16_t conn, const struct ble_gatt_error *err,
                           const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;

    if (err->status == 0)
    {
        h_dis_char = chr->val_handle;
        ESP_LOGI(BLE_TAG, "[DISC] Found DIS Firmware Rev char: val_handle=%u", chr->val_handle);
        setup_step = 0;
        setup_next_step();
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGW(BLE_TAG, "[DISC] DIS Firmware Rev char not found");
        h_dis_char = 0;
        setup_step = 0;
        setup_next_step();
    }

    return 0;
}

static int on_disc_dis_svc(uint16_t conn, const struct ble_gatt_error *err,
                           const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;

    if (err->status == 0)
    {
        h_dis_svc_end = svc->end_handle;
        ESP_LOGI(BLE_TAG, "[DISC] Found DIS svc: start=%u, end=%u", svc->start_handle, svc->end_handle);
        ble_gattc_disc_chrs_by_uuid(conn, svc->start_handle, svc->end_handle, &UUID_CHR_FIRMWARE_REV.u, on_disc_dis_chr, NULL);
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGW(BLE_TAG, "[DISC] DIS svc not found (optional, continuing)");
        h_dis_char = 0;
        h_dis_svc_end = 0;
        setup_step = 0;
        setup_next_step();
    }

    return 0;
}

static int on_disc_batt_chr(uint16_t conn, const struct ble_gatt_error *err,
                            const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;

    if (err->status == 0)
    {
        h_batt_char = chr->val_handle;
        ESP_LOGI(BLE_TAG, "[DISC] Found BATT char: val_handle=%u, props=0x%02X", chr->val_handle, chr->properties);
        // Chain to DIS discovery (optional service)
        ble_gattc_disc_svc_by_uuid(conn, &UUID_SVC_DIS.u, on_disc_dis_svc, NULL);
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGW(BLE_TAG, "[DISC] Battery char not found");
        h_batt_char = 0;
        // Chain to DIS discovery (optional service)
        ble_gattc_disc_svc_by_uuid(conn, &UUID_SVC_DIS.u, on_disc_dis_svc, NULL);
    }

    return 0;
}

static int on_disc_batt_svc(uint16_t conn, const struct ble_gatt_error *err,
                            const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;

    if (err->status == 0)
    {
        h_batt_svc_end = svc->end_handle;
        ESP_LOGI(BLE_TAG, "[DISC] Found BATT svc: start=%u, end=%u", svc->start_handle, svc->end_handle);
        ble_gattc_disc_chrs_by_uuid(conn, svc->start_handle, svc->end_handle, &UUID_CHR_BATT.u, on_disc_batt_chr, NULL);
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGW(BLE_TAG, "[DISC] Battery svc not found");
        h_batt_char = 0;
        h_batt_svc_end = 0;
        setup_step = 0;
        setup_next_step();
    }

    return 0;
}

static int on_disc_rmleak_chr(uint16_t conn, const struct ble_gatt_error *err,
                               const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;

    if (err->status == 0)
    {
        h_rmleak_char = chr->val_handle;
        ESP_LOGI(BLE_TAG, "[DISC] Found RMLEAK char: val_handle=%u, props=0x%02X", chr->val_handle, chr->properties);
        ble_gattc_disc_svc_by_uuid(conn, &UUID_SVC_BATT.u, on_disc_batt_svc, NULL);
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGW(BLE_TAG, "[DISC] RMLEAK char not found (optional, continuing)");
        h_rmleak_char = 0;
        ble_gattc_disc_svc_by_uuid(conn, &UUID_SVC_BATT.u, on_disc_batt_svc, NULL);
    }

    return 0;
}

static int on_disc_flood_chr(uint16_t conn, const struct ble_gatt_error *err,
                             const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;

    if (err->status == 0)
    {
        h_flood_char = chr->val_handle;
        ESP_LOGI(BLE_TAG, "[DISC] Found FLOOD char: val_handle=%u, props=0x%02X", chr->val_handle, chr->properties);
        // Discover RMLEAK char in same FLOOD service (registered after FLOOD)
        int rc = ble_gattc_disc_chrs_by_uuid(conn, h_flood_char + 1, h_flood_svc_end,
                                              &UUID_CHR_RMLEAK.u, on_disc_rmleak_chr, NULL);
        if (rc != 0)
        {
            ESP_LOGW(BLE_TAG, "[DISC] RMLEAK char discovery failed rc=%d, skipping", rc);
            h_rmleak_char = 0;
            ble_gattc_disc_svc_by_uuid(conn, &UUID_SVC_BATT.u, on_disc_batt_svc, NULL);
        }
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGE(BLE_TAG, "[DISC] FLOOD char not found. Disconnecting.");
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }

    return 0;
}

static int on_disc_flood_svc(uint16_t conn, const struct ble_gatt_error *err,
                             const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;

    if (err->status == 0)
    {
        h_flood_svc_end = svc->end_handle;
        ESP_LOGI(BLE_TAG, "[DISC] Found FLOOD svc: start=%u, end=%u", svc->start_handle, svc->end_handle);
        ble_gattc_disc_chrs_by_uuid(conn, svc->start_handle, svc->end_handle, &UUID_CHR_FLOOD.u, on_disc_flood_chr, NULL);
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGE(BLE_TAG, "[DISC] FLOOD svc not found. Disconnecting.");
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }

    return 0;
}

static int on_disc_valve_chr(uint16_t conn, const struct ble_gatt_error *err,
                             const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;

    if (err->status == 0)
    {
        h_valve_char = chr->val_handle;
        ESP_LOGI(BLE_TAG, "[DISC] Found VALVE char: val_handle=%u, props=0x%02X", chr->val_handle, chr->properties);
        ble_gattc_disc_svc_by_uuid(conn, &UUID_SVC_FLOOD.u, on_disc_flood_svc, NULL);
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGE(BLE_TAG, "[DISC] VALVE char not found. Disconnecting.");
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }

    return 0;
}

static int on_disc_valve_svc(uint16_t conn, const struct ble_gatt_error *err,
                             const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;

    if (err->status == 0)
    {
        h_valve_svc_end = svc->end_handle;
        ESP_LOGI(BLE_TAG, "[DISC] Found VALVE svc: start=%u, end=%u", svc->start_handle, svc->end_handle);
        ble_gattc_disc_chrs_by_uuid(conn, svc->start_handle, svc->end_handle, &UUID_CHR_VALVE.u, on_disc_valve_chr, NULL);
        return BLE_HS_EDONE;
    }

    if (err->status == BLE_HS_EDONE)
    {
        ESP_LOGE(BLE_TAG, "[DISC] VALVE svc not found. Disconnecting.");
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }

    return 0;
}

static void start_discovery_chain(void)
{
    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(BLE_TAG, "[DISC] Cannot start discovery - no connection");
        return;
    }

    if (!link_is_target())
    {
        ESP_LOGW(BLE_TAG, "[DISC] Not discovering - link is not the provisioned valve; disconnecting");
        ble_gap_terminate(valve_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }

    ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(BLE_TAG, "║            STARTING SERVICE DISCOVERY                        ║");
    ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");

    g_setup_in_progress = true;
    setup_step = 0;

    /* A fresh discovery means the link is no longer "discovered": the handles below
     * are about to be zeroed, so DISCOVERY_DONE (and therefore is_ready_for_gatt())
     * must not keep claiming the session is usable.
     *
     * This also makes setup_next_step()'s idempotence guard sound. That guard skips
     * the default: arm when DISCOVERY_DONE is set; without this clear, a re-entered
     * discovery on an already-set-up link would run the whole chain and then be
     * silently abandoned at the end — leaving g_setup_in_progress stuck true (which
     * suppresses every notify_hub_update, so the hub goes deaf to valve state) and
     * stranding any pending CLOSE/RMLEAK that the default: arm would have replayed.
     * Reachable because command read-backs share handle_read_result(), so an
     * auth-error on a post-setup read can re-enter initiate_security(). */
    clear_state_bit(BLE_STATE_BIT_DISCOVERY_DONE);

    h_valve_char = 0;
    h_flood_char = 0;
    h_rmleak_char = 0;
    h_batt_char = 0;
    h_dis_char = 0;
    h_valve_svc_end = 0;
    h_flood_svc_end = 0;
    h_batt_svc_end = 0;
    h_dis_svc_end = 0;
    g_firmware_rev[0] = '\0';

    if (discovery_timeout_timer)
        xTimerReset(discovery_timeout_timer, 0);

    ble_gattc_disc_svc_by_uuid(valve_conn_handle, &UUID_SVC_VALVE.u, on_disc_valve_svc, NULL);
}

// -----------------------------------------------------------------------------
// GAP EVENTS
// -----------------------------------------------------------------------------

// Forward-declare so ble_valve_claim_start() can pass it as ble_gap_connect()'s callback
static int ble_gap_event(struct ble_gap_event *event, void *arg);

// Forget everything the last link told us. 0xFF battery = unknown (not 0 %), and RMLEAK
// resets like the other fields so a missing/failed read cannot carry a prior session's value.
static void reset_link_cache(void)
{
    h_valve_char = 0;
    h_flood_char = 0;
    h_rmleak_char = 0;
    h_batt_char = 0;
    h_dis_char = 0;
    h_valve_svc_end = 0;
    h_flood_svc_end = 0;
    h_batt_svc_end = 0;
    h_dis_svc_end = 0;
    g_val_battery = 0xFF;
    g_val_leak = false;
    g_val_state = -1;
    g_val_rmleak = false;
    g_firmware_rev[0] = '\0';
}

// The module's side of a link that is gone: its GAP DISCONNECT, a link NimBLE's connect
// re-attempt deleted without one (BLE_GAP_EVENT_REATTEMPT_COUNT), or a stale handle
// (link_poll()). NimBLE host task for the first two; the command task for the third, only once no
// event for that link can come any more. A link GAP CONNECT rejected (s_rejecting_conn) leaves as
// it came, with nothing to the hub or the health engine. Rescans when a link is wanted.
static void link_closed(void)
{
    s_link_dropping = false;   // the dropped link is gone (drop_link_after_failed_write())
    s_term_failed = false;

    if (s_rejecting_conn)
    {
        // The link GAP CONNECT rejected. It never reached the hub or the health
        // engine, so it leaves the same way: no notify, no health post.
        s_rejecting_conn = false;
        claim_end(false, NULL);
        valve_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        reset_link_cache();
        clear_all_state_bits();
        taskENTER_CRITICAL(&s_mac_lock);
        memset(g_valve_mac, 0, sizeof(g_valve_mac));
        taskEXIT_CRITICAL(&s_mac_lock);
        if (sec_timeout_timer) xTimerStop(sec_timeout_timer, 0);   // the terminate backstop
        ESP_LOGI(BLE_TAG, "[DISCONNECT] Rejected link closed");
        if (g_connect_requested && ble_valve_has_target_mac())
            request_hunt();
        return;
    }

    // Sampled before the link state is cleared. A link whose target was changed or
    // removed while it was up is no longer the provisioned valve's, and its teardown
    // must not reach the hub or the health engine either. Its MAC is sampled with it,
    // for the health engine's DISCONNECTED (g_valve_mac is cleared below).
    char disc_mac[18];
    bool was_target = link_is_target_mac(disc_mac);

    valve_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    reset_link_cache();

    /* DELIBERATELY does NOT clear the health engine's valve leak state here.
     *
     * An earlier revision posted health_post_valve_leak(false) on disconnect, on the
     * reasoning that the probe's state is unknowable once the link is down. That is
     * true but it is the wrong conclusion: losing the link does not dry the floor.
     * Because compute_valve_rating() checks `leaking` first, clearing it demoted a
     * KNOWN flood at the valve from CRITICAL to the WARNING disconnect grace for a
     * full 3 minutes — under-reporting the single worst state the system has (water
     * running, and the hub can no longer reach the thing that stops it).
     *
     * The two objections that motivated the clear are both already answered:
     *  - "masks CRITICAL-because-offline": the reason builder now falls through for a
     *    valve that is leaking AND disconnected, so it reports BOTH "Leak detected:
     *    valve" and "Valve offline" (telemetry_v2.c).
     *  - "unclearable": setup step 6 re-reads the flood characteristic on every
     *    reconnect and posts the truth, so it clears as soon as the link is back.
     *
     * Known cost, accepted: since the rating was already CRITICAL while wet, the
     * disconnect produces no rating transition and therefore no `device_offline`
     * alert. The state is still fully on the wire — valve.connected:false plus the
     * "Valve offline" clause in system_health.reason. Under-reporting an alert is a
     * far better failure than under-reporting the severity of an active flood. */

    clear_all_state_bits();
    taskENTER_CRITICAL(&s_mac_lock);
    memset(g_valve_mac, 0, sizeof(g_valve_mac));
    taskEXIT_CRITICAL(&s_mac_lock);
    if (was_target)
        notify_hub_update(BLE_UPD_DISCONNECTED, disc_mac);
    else
        ESP_LOGW(BLE_TAG, "[DISCONNECT] Link was not the provisioned valve - hub not notified");

    if (sec_timeout_timer) xTimerStop(sec_timeout_timer, 0);
    if (post_connect_timer) xTimerStop(post_connect_timer, 0);
    if (discovery_timeout_timer) xTimerStop(discovery_timeout_timer, 0);
    if (security_retry_timer) xTimerStop(security_retry_timer, 0);

    // The claim (WP6): a link that reached CONNECT is no failed claim, however soon it is lost (see
    // s_claim_fails), and resets no back-off either: only a link held CLAIM_HELD_MS does, and then
    // claim_held_poll() has closed the claim already.
    claim_end(false, NULL);

    if (g_connect_requested)
        request_hunt();
}

// ---- The claim policy's bookkeeping (WP6, see s_claim_open) ------------------------------------
// A granted claim ended without a link held CLAIM_HELD_MS: `failed` (no link at all) counts it as
// one more failure in a row and backs the next claim off; otherwise (cancelled by this module, or a
// link that reached CONNECT) it ends with no count. Nothing when no claim is open. Any task.
static void claim_end(bool failed, const char *why)
{
    TickType_t now = xTaskGetTickCount();
    taskENTER_CRITICAL(&s_mac_lock);
    bool counted = s_claim_open && failed;
    s_claim_open = false;
    uint8_t empty = s_claim_empty;
    uint8_t n = s_claim_fails;
    bool backoff = false;
    if (counted)
    {
        if (empty < UINT8_MAX)
            empty++;
        s_claim_empty = empty;
        if (empty > CLAIM_EMPTY_FREE)
        {
            if (n < CLAIM_FAILS_MAX)
                n++;
            s_claim_fails = n;
            s_claim_not_before = now + pdMS_TO_TICKS((uint32_t)k_claim_backoff_s[n - 1] * 1000u);
            backoff = true;
        }
    }
    taskEXIT_CRITICAL(&s_mac_lock);
    if (counted && backoff)
        ESP_LOGW(BLE_TAG, "[CLAIM] Valve claim failed (%s), %u in a row - next claim in %u s (no back-off while a leak response is pending)",
                 why, (unsigned)empty, (unsigned)k_claim_backoff_s[n - 1]);
    else if (counted)
        ESP_LOGW(BLE_TAG, "[CLAIM] Valve claim failed (%s), %u in a row - the next needs only the pulse spacing, no back-off",
                 why, (unsigned)empty);
}

// No claim before the back-off has run out, unless a leak response is pending. Any task.
static bool claim_backoff_over(void)
{
    if (s_lr_trigger)
        return true;
    TickType_t now = xTaskGetTickCount();
    taskENTER_CRITICAL(&s_mac_lock);
    bool over = (s_claim_fails == 0) || (int32_t)(now - s_claim_not_before) >= 0;
    taskEXIT_CRITICAL(&s_mac_lock);
    return over;
}

// A claim's connect is gone from NimBLE without its CONNECT event (see s_connect_seq): end it here
// and reset the BLE host, which clears the controller's initiator, or the link the host never took.
// One reset at a time. Host task (DISC_COMPLETE) or command task (link_poll()).
static void connect_lost_by_host(const char *how)
{
    taskENTER_CRITICAL(&s_mac_lock);
    bool was = g_connecting;
    g_connecting = false;
    bool reset = was && !s_host_reset_asked;
    if (reset)
        s_host_reset_asked = true;
    taskEXIT_CRITICAL(&s_mac_lock);
    if (!was)
        return;
    ESP_LOGE(BLE_TAG, "[CLAIM] NimBLE lost the valve connect in flight (%s) - %s", how,
             reset ? "resetting the BLE host" : "a BLE host reset is on its way");
    claim_end(false, NULL);   // not the valve's failure
    if (reset)
        ble_hs_sched_reset(BLE_HS_ECONTROLLER);
}

// ---- The valve hunt, as the BLE scan executor sees it (WP5) -----------------------------------
// The hunt is wanted while the provisioned valve is wanted (g_connect_requested), not linked and
// no connect is in flight. Recomputed from those facts on every call, so no lost edge can leave
// the valve unhunted. The executor (app_ble_leak.c) runs a scan that covers the valve's 1M adverts
// while this is true, in every mode the radio policy runs (the setup portal's AP modes too, since
// 2.1.4 WP8 deleted the portal priority window and the Wi-Fi radio holds); any task may call it.
bool ble_valve_hunt_wanted(void)
{
    return g_ble_synced && g_connect_requested && !g_connecting &&
           valve_conn_handle == BLE_HS_CONN_HANDLE_NONE &&
           ble_valve_has_target_mac();
}

// Every complete advertising report of the executor's scans, from its GAP handler (NimBLE host
// task): it only stores and notifies. Matches the PROVISIONED valve's MAC only; with no
// provisioned valve nothing is ever linked (P0-a). The advertised name is no longer consulted,
// so the payload is not parsed. The checks that need no lock come first: this runs for every
// advert in range, and while the valve is linked it returns at the first.
void ble_valve_note_adv(const void *adv_addr)
{
    const ble_addr_t *addr = (const ble_addr_t *)adv_addr;

    // Re-entrancy guard. The scan is stopped only when the executor grants the claim, and
    // advertisement reports already queued in the host still arrive after that, so further
    // reports for the same valve reach this function while the claim's ble_gap_connect() is in
    // flight. Before WP5 a second connect then failed, and the old code responded by rescanning,
    // cancelling the connection attempt that was about to succeed.
    //
    // This was survivable while the scan ran with filter_duplicates=1, which capped the valve at
    // one report per session. That filter is now off (it was making the hub deaf to leak
    // sensors, see the executor's scan start in app_ble_leak.c), so duplicate reports are the
    // normal case and this guard is load-bearing. A claim already requested waits for its grant.
    if (s_claim_req || g_connecting || valve_conn_handle != BLE_HS_CONN_HANDLE_NONE ||
        !g_connect_requested)
        return;

    char target[18];
    if (!target_copy(target))
        return;

    char discovered_mac[18];
    snprintf(discovered_mac, sizeof(discovered_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             addr->val[5], addr->val[4], addr->val[3],
             addr->val[2], addr->val[1], addr->val[0]);
    if (strcasecmp(discovered_mac, target) != 0)
        return;

    TickType_t heard = xTaskGetTickCount();
    s_valve_heard_at = heard ? heard : 1;   // the hunt slot has done its part (B2)

    // The claim back-off (WP6): the valve keeps advertising, and its first report after the
    // back-off asks for the claim. Not while a leak response is pending.
    if (!claim_backoff_over())
        return;

    ESP_LOGI(BLE_TAG, "[SCAN] Target MAC matched - connecting to provisioned valve: %s",
             discovered_mac);

    taskENTER_CRITICAL(&s_mac_lock);
    memcpy(&g_peer_addr, addr, sizeof(ble_addr_t));
    g_peer_addr_valid = true;
    taskEXIT_CRITICAL(&s_mac_lock);
    s_claim_req = true;
    app_ble_leak_kick();   // the executor grants the claim (ble_valve_claim_start())
}

// B2's hunt slot (the radio policy's N_HUNT row, and the AP modes' valve discovery) is worth its
// Coded time only while it can find the valve for a claim: the hunt wants it, no claim is due, the
// valve has not been heard (or claimed) in the last VALVE_HEARD_HOLD_MS, and the back-off lets a
// claim follow. A valve that is heard but does not link (marginal RF, empty pulses) is then claimed
// on what the plain profile hears, and its claims no longer run beside the slot: the council's model
// gave the other sensors p99.9 75.5 s with both, 32.5 s with the claims alone (p_loss 0.3, a leak
// response pending past the overlay's cap, 2.5 s claims every 7-8 s). Executor task.
bool ble_valve_hunt_slot_wanted(void)
{
    if (s_claim_req || !ble_valve_hunt_wanted())
        return false;
    TickType_t h = s_valve_heard_at;
    if (h != 0 && (xTaskGetTickCount() - h) < pdMS_TO_TICKS(VALVE_HEARD_HOLD_MS))
        return false;
    return claim_backoff_over();
}

// The executor's question before it grants a claim: the valve was heard and the hunt still
// wants it. A request the hunt no longer wants (linked meanwhile, the target gone, the
// connect request withdrawn) is dropped here. Executor task.
bool ble_valve_claim_wanted(void)
{
    if (!s_claim_req)
        return false;
    if (ble_valve_hunt_wanted())
        return true;
    s_claim_req = false;
    return false;
}

// The executor's grant (its task, its scan stopped): connect to the valve heard. g_connecting is
// set BEFORE the connect is issued (link_poll() reads it against ble_gap_conn_active()). The
// connect is the claim's CONNECT pulse (WP6): it ends at its CONNECT event or after pulse_ms, the
// length the radio policy granted (radio_policy_exec_connect_len()); the executor starts no scan
// meanwhile.
void ble_valve_claim_start(uint32_t pulse_ms)
{
    s_claim_req = false;
    TickType_t claimed = xTaskGetTickCount();
    s_valve_heard_at = claimed ? claimed : 1;   // its claim: the hunt slot waits VALVE_HEARD_HOLD_MS
    if (!ble_valve_hunt_wanted())
        return;   // re-checked: linked, unprovisioned or no longer wanted since the report

    ble_addr_t peer;
    taskENTER_CRITICAL(&s_mac_lock);
    memcpy(&peer, &g_peer_addr, sizeof(peer));
    s_claim_open = true;
    taskEXIT_CRITICAL(&s_mac_lock);

    bool lr = s_lr_trigger;
    ESP_LOGI(BLE_TAG, "[CLAIM] Connecting to the valve: pulse up to %lu ms%s", (unsigned long)pulse_ms,
             lr ? " (leak response pending)" : "");
    s_hunt_announced = false;   // the hunt ends with its claim
    s_connect_seq++;
    g_connecting = true;
    int rc = ble_gap_connect(g_own_addr_type, &peer, (int32_t)pulse_ms, NULL, ble_gap_event, NULL);
    if (rc != 0)
    {
        ESP_LOGE(BLE_TAG, "[SCAN] ble_gap_connect rc=%d", rc);
        g_connecting = false;
        claim_end(true, "connect not started");
        request_hunt();
    }
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    struct ble_gap_conn_desc desc;
    int rc;

    // No advertising report reaches this handler any more: the module runs no scan (WP5).
    // It handles its connections only; the executor's GAP handler (app_ble_leak.c) gets the
    // reports and passes each to ble_valve_note_adv().
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
        ESP_LOGI(BLE_TAG, "║            GAP CONNECT EVENT                                 ║");
        ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");
        ESP_LOGI(BLE_TAG, "[CONNECT] status=%d", event->connect.status);

        // The connect attempt is resolved either way — release the re-entrancy
        // guard before branching, so a failure path that asks for a hunt (request_hunt()) can
        // legitimately attempt the next connection. The executor, which starts no scan while a
        // connect is in flight, is woken to resume scanning.
        g_connecting = false;
        s_link_dropping = false;   // a new link (or none): see drop_link_after_failed_write()
        s_term_failed = false;
        app_ble_leak_kick();

        if (event->connect.status == 0)
        {
            valve_conn_handle = event->connect.conn_handle;
            s_hunt_announced = false;
            s_rejecting_conn = false;
            taskENTER_CRITICAL(&s_mac_lock);
            s_link_up_at = xTaskGetTickCount();   // the claim's link: held from now (claim_held_poll())
            s_claim_empty = 0;                    // the run of empty claims is over (B2)
            taskEXIT_CRITICAL(&s_mac_lock);

            clear_all_state_bits();
            reset_link_cache();

            // The IDENTITY address, so a bonded valve that connects with an RPA still
            // matches its provisioned MAC. Left "" if the lookup fails: an unverifiable
            // peer is rejected below like any other.
            char peer_mac[18] = {0};
            if (ble_gap_conn_find(valve_conn_handle, &desc) == 0)
            {
                snprintf(peer_mac, sizeof(peer_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                         desc.peer_id_addr.val[5], desc.peer_id_addr.val[4], desc.peer_id_addr.val[3],
                         desc.peer_id_addr.val[2], desc.peer_id_addr.val[1], desc.peer_id_addr.val[0]);
                ESP_LOGI(BLE_TAG, "[CONNECT] MAC=%s, handle=%u", peer_mac, valve_conn_handle);

                taskENTER_CRITICAL(&s_mac_lock);
                memcpy(&g_peer_addr, &desc.peer_id_addr, sizeof(ble_addr_t));
                g_peer_addr_valid = true;
                taskEXIT_CRITICAL(&s_mac_lock);
            }
            taskENTER_CRITICAL(&s_mac_lock);
            memcpy(g_valve_mac, peer_mac, sizeof(g_valve_mac));
            taskEXIT_CRITICAL(&s_mac_lock);

            // Not the provisioned valve (the target changed or was removed while this
            // connect was in flight): drop it before anything runs on it — no state bits,
            // no pairing, no timers, nothing to the hub or the health engine (P0-a).
            if (!link_is_target())
            {
                char target[18];
                bool has_target = target_copy(target);
                ESP_LOGW(BLE_TAG, "[CONNECT] Peer %s is not the provisioned valve (%s) - disconnecting",
                         peer_mac[0] ? peer_mac : "unknown", has_target ? target : "none");
                s_rejecting_conn = true;
                rc = ble_gap_terminate(valve_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                if (rc != 0)
                {
                    // Backstop so a failed terminate cannot hold the foreign link forever:
                    // sec_timeout_cb() terminates any link that never became ready.
                    ESP_LOGE(BLE_TAG, "[CONNECT] terminate rc=%d - retrying via the setup timeout", rc);
                    if (sec_timeout_timer)
                        xTimerReset(sec_timeout_timer, 0);
                }
                return 0;
            }

            set_state_bit(BLE_STATE_BIT_CONNECTED);

            if (post_connect_timer)
            {
                ESP_LOGI(BLE_TAG, "[CONNECT] Starting %dms delay before security...", POST_CONNECT_SECURITY_DELAY_MS);
                xTimerStart(post_connect_timer, 0);
            }
            else
            {
                start_discovery_chain();
            }
        }
        else
        {
            ESP_LOGW(BLE_TAG, "[CONNECT] Failed status=%d", event->connect.status);
            valve_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            clear_all_state_bits();
            // BLE_HS_EAPP: this module cancelled it (BLE_CMD_DISCONNECT, a connect it did not
            // start): no failure. BLE_HS_ETIMEOUT: the valve was not reached within the pulse.
            claim_end(event->connect.status != BLE_HS_EAPP,
                      event->connect.status == BLE_HS_ETIMEOUT ? "no link within the claim's pulse"
                                                               : "connect failed");
            // Not after BLE_CMD_DISCONNECT cancelled this connect (status BLE_HS_EAPP):
            // rescanning would undo the disconnect (N8).
            if (g_connect_requested)
                request_hunt();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
        ESP_LOGI(BLE_TAG, "║            GAP DISCONNECT EVENT                              ║");
        ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");
        ESP_LOGW(BLE_TAG, "[DISCONNECT] reason=0x%02x", event->disconnect.reason);
        // Only the tracked link's own DISCONNECT closes it (a link GAP CONNECT rejected is tracked
        // too). Since WP3 this module also closes a link itself, a stale handle (link_stale_check())
        // or a REATTEMPT_COUNT, so a DISCONNECT for that old handle that comes late must not close
        // the link or the claim that replaced it.
        if (event->disconnect.conn.conn_handle != valve_conn_handle)
        {
            ESP_LOGW(BLE_TAG, "[DISCONNECT] Handle %u is not the tracked link (%u) - ignored",
                     event->disconnect.conn.conn_handle, valve_conn_handle);
            return 0;
        }
        link_closed();
        return 0;

#if MYNEWT_VAL(BLE_ENABLE_CONN_REATTEMPT)
    case BLE_GAP_EVENT_REATTEMPT_COUNT:
        // Defensive (WP3): sdkconfig turns NimBLE's connect re-attempt off (I5), but a build that
        // has it on gets here when a link fails to be established (0x3E). Right after this event
        // NimBLE deletes the link WITHOUT a DISCONNECT and issues a connect of its own (see the
        // stale-handle note at s_stale_handle). Close the link here as its DISCONNECT would, and
        // wake the command task, which cancels that connect (link_poll()): every link is then made
        // by this module's own hunt, and a pended RMLEAK / CLOSE is replayed at its setup.
        ESP_LOGW(BLE_TAG, "[DISCONNECT] Link failed to be established (0x3E), NimBLE re-attempt %u (handle=%u) - closing it here",
                 (unsigned)event->reattempt_cnt.count, event->reattempt_cnt.conn_handle);
        if (event->reattempt_cnt.conn_handle == valve_conn_handle)
            link_closed();
        if (ble_cmd_queue != NULL)
        {
            uint32_t wake = CMD_ITEM(CMD_WAKE_TOKEN, 0);
            (void)xQueueSend(ble_cmd_queue, &wake, 0);
        }
        return 0;
#endif

    case BLE_GAP_EVENT_DISC_COMPLETE:
        // A scan's end reaches this handler only when this module's connect was the GAP procedure
        // NimBLE ran as it processed that end, which reset the connect (see s_connect_seq). With
        // no connect of ours in flight it is harmless.
        if (g_connecting)
            connect_lost_by_host("a scan's late end reset it");
        return 0;

    case BLE_GAP_EVENT_TERM_FAILURE:
        // The controller refused a terminate: the link stays up and no DISCONNECT is coming to
        // clear s_link_dropping, which would otherwise pend every command on this link for a
        // next link that never comes (drop_link_after_failed_write()).
        ESP_LOGE(BLE_TAG, "[DISCONNECT] terminate failed status=%d - link stays up (handle=%u)",
                 event->term_failure.status, event->term_failure.conn_handle);
        if (event->term_failure.conn_handle == valve_conn_handle)
        {
            s_link_dropping = false;
            s_term_failed = true;
        }
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        return on_notify(event->notify_rx.conn_handle, event->notify_rx.attr_handle, event->notify_rx.om, NULL);

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(BLE_TAG, "[GAP] MTU updated: %u", event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(BLE_TAG, "[GAP] Connection params updated: status=%d", event->conn_update.status);
        return 0;

    case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
        ESP_LOGI(BLE_TAG, "[GAP] L2CAP update request");
        return 0;

    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        ESP_LOGI(BLE_TAG, "[GAP] PHY update complete");
        return 0;

#ifdef BLE_GAP_EVENT_DATA_LEN_CHG
    case BLE_GAP_EVENT_DATA_LEN_CHG:
        ESP_LOGI(BLE_TAG, "[GAP] Data length changed: tx=%d rx=%d",
                 event->data_len_chg.max_tx_octets, event->data_len_chg.max_rx_octets);
        return 0;
#endif

    // -------------------------------------------------------------------------
    // SECURITY EVENTS
    // -------------------------------------------------------------------------
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
        ESP_LOGI(BLE_TAG, "║            ENCRYPTION CHANGE EVENT                           ║");
        ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");
        ESP_LOGI(BLE_TAG, "[ENC_CHANGE] status=%d", event->enc_change.status);

        // A peer-initiated encryption on a link that is not (or no longer) the provisioned
        // valve's: no discovery, no bond housekeeping. That link is already being dropped.
        if (!link_is_target())
        {
            ESP_LOGW(BLE_TAG, "[ENC_CHANGE] Ignored - link is not the provisioned valve");
            return 0;
        }

        if (event->enc_change.status == 0)
        {
            if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0)
            {
                ESP_LOGI(BLE_TAG, "[ENC_CHANGE] encrypted=%d, authenticated=%d, bonded=%d, key_size=%d",
                         desc.sec_state.encrypted, desc.sec_state.authenticated,
                         desc.sec_state.bonded, desc.sec_state.key_size);

                if (desc.sec_state.encrypted)
                {
                    ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
                    ESP_LOGI(BLE_TAG, "║            LINK ENCRYPTED SUCCESSFULLY                       ║");
                    ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");

                    clear_state_bit(BLE_STATE_BIT_PAIRING);
                    set_state_bit(BLE_STATE_BIT_ENCRYPTED);

                    if (desc.sec_state.authenticated)
                    {
                        ESP_LOGI(BLE_TAG, "[ENC_CHANGE] MITM authentication achieved");
                        set_state_bit(BLE_STATE_BIT_AUTHENTICATED);
                    }

                    if (desc.sec_state.bonded)
                    {
                        ESP_LOGI(BLE_TAG, "[ENC_CHANGE] Device is bonded (keys stored)");
                        set_state_bit(BLE_STATE_BIT_BONDED);
                    }

                    if (sec_timeout_timer)
                        xTimerStop(sec_timeout_timer, 0);

                    EventBits_t bits = get_state_bits();
                    if (!(bits & BLE_STATE_BIT_DISCOVERY_DONE))
                    {
                        ESP_LOGI(BLE_TAG, "[ENC_CHANGE] Link secured. Starting discovery...");
                        start_discovery_chain();
                    }
                }
            }
        }
        else
        {
            ESP_LOGE(BLE_TAG, "[ENC_CHANGE] Encryption failed: status=%d", event->enc_change.status);
            clear_state_bit(BLE_STATE_BIT_PAIRING);

            // A valve that has been reflashed or factory-reset no longer holds the
            // LTK we bonded with, and answers every encryption attempt with
            // "PIN or Key Missing". Without this the hub re-offers that dead key
            // forever in a ~5 s connect/fail/rescan loop.
            //
            // This matters more than it looks: that loop's CLOUD signature is
            // indistinguishable from a marginal-RF flapper, so the disconnect
            // suppression this release adds would hide a PERMANENT failure
            // indefinitely. Dropping the stale bond lets the next attempt re-pair.
            //
            // Deliberately narrow — only this status. Deleting the bond on any
            // encryption failure would turn a transient error into a needless
            // re-pair, and re-pairing is the expensive, user-visible path.
            if (event->enc_change.status == BLE_HS_HCI_ERR(BLE_ERR_PINKEY_MISSING) &&
                ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0)
            {
                ESP_LOGW(BLE_TAG, "[ENC_CHANGE] Peer no longer holds our bond — deleting stale LTK");
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }

            if (valve_conn_handle != BLE_HS_CONN_HANDLE_NONE)
                ble_gap_terminate(valve_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
        ESP_LOGI(BLE_TAG, "║            PASSKEY ACTION EVENT                              ║");
        ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");
        ESP_LOGI(BLE_TAG, "[PASSKEY] action=%d", event->passkey.params.action);

        // The passkey is fixed and shared by every eFloStop valve: answering it would bond
        // whichever valve asked. Only the provisioned valve gets it.
        if (!link_is_target())
        {
            ESP_LOGW(BLE_TAG, "[PASSKEY] Not answered - link is not the provisioned valve");
            return 0;
        }

        if (event->passkey.params.action == BLE_SM_IOACT_INPUT)
        {
            ESP_LOGI(BLE_TAG, "[PASSKEY] INPUT required. Responding with the fixed passkey");

            struct ble_sm_io pk;
            pk.action = BLE_SM_IOACT_INPUT;
            pk.passkey = BLE_VALVE_FIXED_PASSKEY;

            rc = ble_sm_inject_io(event->passkey.conn_handle, &pk);
            if (rc == 0)
                ESP_LOGI(BLE_TAG, "[PASSKEY] Passkey injected successfully");
            else
                ESP_LOGE(BLE_TAG, "[PASSKEY] ble_sm_inject_io failed: rc=%d", rc);
        }
        else if (event->passkey.params.action == BLE_SM_IOACT_DISP)
        {
            ESP_LOGI(BLE_TAG, "[PASSKEY] DISPLAY action. Responding with the fixed passkey");

            struct ble_sm_io pk;
            pk.action = BLE_SM_IOACT_DISP;
            pk.passkey = BLE_VALVE_FIXED_PASSKEY;

            ble_sm_inject_io(event->passkey.conn_handle, &pk);
        }
        else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP)
        {
            ESP_LOGI(BLE_TAG, "[PASSKEY] Numeric comparison: %lu", (unsigned long)event->passkey.params.numcmp);

            struct ble_sm_io pk;
            pk.action = BLE_SM_IOACT_NUMCMP;
            pk.numcmp_accept = 1;

            rc = ble_sm_inject_io(event->passkey.conn_handle, &pk);
            if (rc == 0)
                ESP_LOGI(BLE_TAG, "[PASSKEY] Numeric comparison accepted");
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
        ESP_LOGI(BLE_TAG, "║            REPEAT PAIRING EVENT                              ║");
        ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");

        if (!link_is_target())
        {
            ESP_LOGW(BLE_TAG, "[REPEAT_PAIR] Ignored - link is not the provisioned valve");
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        }

        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc == 0)
        {
            ESP_LOGI(BLE_TAG, "[REPEAT_PAIR] Deleting old bond for peer...");
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    default:
        ESP_LOGW(BLE_TAG, "[GAP] Unhandled event: %d (0x%02X)", event->type, event->type);
        return 0;
    }
}

// -----------------------------------------------------------------------------
// THE VALVE HUNT
// -----------------------------------------------------------------------------
// Asks the BLE scan executor (app_ble_leak.c) for the valve hunt: the scan itself is the
// executor's, which runs one that covers the valve while ble_valve_hunt_wanted() holds, and its
// adverts reach ble_valve_note_adv(). Every caller has set g_connect_requested; this keeps the
// checks and lines of the old start_scan() and wakes the executor. The one choke point for every
// hunt: CONNECT, a failed connect, a disconnect, the stack sync and a pended command's link
// request. Any task.
static void request_hunt(void)
{
    if (!g_ble_synced)
    {
        ESP_LOGW(BLE_TAG, "[SCAN] Not synced");
        return;
    }

    // Nothing to look for: the hunt exists to find the provisioned valve (P0-a).
    char target[18];
    if (!target_copy(target))
    {
        ESP_LOGI(BLE_TAG, "[SCAN] No provisioned valve - not scanning for valves");
        return;
    }

    if (valve_conn_handle != BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(BLE_TAG, "[SCAN] Already connected");
        return;
    }

    if (s_hunt_announced)
        return;   // this hunt was asked for already; the executor runs it

    ESP_LOGI(BLE_TAG, "[SCAN] Starting scan for provisioned valve %s...", target);
    s_hunt_announced = true;
    app_ble_leak_kick();
}

// -----------------------------------------------------------------------------
// WRITE COMMAND
// -----------------------------------------------------------------------------
// The read-back of an accepted hub write found the GATT procedure pool busy (BLE_HS_ENOMEM,
// see CMD_BUSY_RETRY_MS): issue it once a procedure frees, polled like the write, for at most
// CMD_BUSY_MAX_MS. It is mandatory for RMLEAK, which the valve does not echo: without it the
// cache keeps the value from before the write, and a stale RMLEAK=0 while the hub holds the
// interlock reads as a valve-button override. ATT is sequential on a link, so a later read
// still returns the value after this write. Same link and target only: a new link reads every
// value at setup. Command task only, without gatt_mutex held.
static void read_back_when_free(uint16_t conn, const uint16_t *handle, uint32_t gen,
                                const char *what, const char *unconfirmed)
{
    ESP_LOGW(BLE_TAG, "[CMD] %s read-back: GATT busy - waiting", what);
    int rrc = BLE_HS_ENOMEM;
    for (int waited_ms = 0; rrc == BLE_HS_ENOMEM && waited_ms < CMD_BUSY_MAX_MS;
         waited_ms += CMD_BUSY_RETRY_MS)
    {
        vTaskDelay(pdMS_TO_TICKS(CMD_BUSY_RETRY_MS));
        if (gatt_mutex == NULL || xSemaphoreTake(gatt_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
        {
            rrc = CMD_WR_NO_MUTEX;
            break;
        }
        bool same_link = cmd_gen_is_current(gen) && valve_conn_handle == conn &&
                         cmd_link_ready(*handle);
        if (same_link)
            rrc = ble_gattc_read(conn, *handle, on_cmd_read_cb, NULL);
        xSemaphoreGive(gatt_mutex);
        if (!same_link)
            return;
    }
    if (rrc != 0)
        ESP_LOGW(BLE_TAG, "[CMD] %s read-back rc=%d - %s unconfirmed", what, rrc, unconfirmed);
}

// Writes a hub command to the provisioned valve's characteristic *handle (re-read on every
// attempt: a rediscovery zeroes it) and issues the read-back. Up to CMD_WRITE_ATTEMPTS
// attempts, 200 ms then 400 ms apart, with gatt_mutex released in between so the host
// task's replays are not held off. Command task only: it sleeps.
// A busy GATT procedure pool (BLE_HS_ENOMEM) is not a failed attempt: the write is retried
// every CMD_BUSY_RETRY_MS for at most CMD_BUSY_MAX_MS, with the same checks before each, and a
// read-back that finds the pool busy is issued once a procedure frees (read_back_when_free()).
// `slot` is the command's pending slot. An accepted write clears it under gatt_mutex, where
// setup completion's replay re-checks it (apply_pending_cmd()); a CMD_WR_RELINK pend is made
// after the mutex is released, by finish_cmd_write(). A live command clears whatever older
// command of its kind is still pended once the host accepts it: replayed later, that one
// would undo it.
// A `replay` (of the command pended in *slot) is written only while the slot still holds it,
// re-checked before every attempt, and leaves the slot once the host accepts it. A live valve
// command finding an RMLEAK command pended on its ready link returns CMD_WR_RELINK, so it is
// pended and replayed after that RMLEAK command.
// *conn_out = the link the last write went to (NONE when no attempt got that far).
// Returns 0 once the host accepted the write, CMD_WR_STALE, CMD_WR_RELINK, CMD_WR_MOVED
// (replay cancelled or superseded meanwhile), or the last failure (a NimBLE rc, or
// CMD_WR_NO_MUTEX) when every attempt failed on a live link: BLE_HS_ENOMEM when the pool
// stayed busy for CMD_BUSY_MAX_MS.
static int write_cmd_with_retry(const uint16_t *handle, uint8_t val, uint32_t gen, int *slot,
                                bool replay, uint16_t *conn_out, const char *what,
                                const char *unconfirmed)
{
    int rc = CMD_WR_RELINK;
    int busy_ms = 0;   // waited so far for a free GATT procedure (BLE_HS_ENOMEM)
    *conn_out = BLE_HS_CONN_HANDLE_NONE;
    for (int attempt = 1; attempt <= CMD_WRITE_ATTEMPTS; attempt++)
    {
        // Not after a busy pool: that wait is below, and it did not use up this attempt.
        if (attempt > 1 && rc != BLE_HS_ENOMEM)
        {
            int delay_ms = (attempt == 2) ? 200 : 400;
            ESP_LOGW(BLE_TAG, "[CMD] %s write attempt %d/%d failed (rc=%d) - retrying in %d ms",
                     what, attempt - 1, CMD_WRITE_ATTEMPTS, rc, delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }

        if (gatt_mutex == NULL || xSemaphoreTake(gatt_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
        {
            rc = CMD_WR_NO_MUTEX;
            continue;
        }

        bool read_back_busy = false;
        // Checked right before each write: while this task waited or slept, the target may
        // have changed (its flush must win), the link dropped, or a replayed command been
        // cancelled, superseded or written on a new link.
        if (!cmd_gen_is_current(gen))
        {
            rc = CMD_WR_STALE;
        }
        else if (replay && pending_get(slot, gen) != (int)val)
        {
            rc = CMD_WR_MOVED;
        }
        else if (!cmd_link_ready(*handle))
        {
            rc = CMD_WR_RELINK;
        }
        else if (!replay && slot == &g_pending_valve_cmd && cmd_link_ready(h_rmleak_char) &&
                 pending_get(&g_pending_rmleak_cmd, gen) >= 0)
        {
            // RMLEAK before the valve command (Phase F, F4): an RMLEAK command is still pended
            // on this ready link. Typically it was pended while the link was in setup, and this
            // command passed write_valve_command()'s RMLEAK check before setup completed, so
            // written now it would land ahead of the RMLEAK command. Pend it too:
            // finish_cmd_write() queues the replay token, and replay_pending_cmds() writes the
            // RMLEAK command first. That replay writes this one as a replay, never held here
            // again: a CLOSE still goes if the RMLEAK write fails there (also when that check
            // had just tried it and failed: it is then tried once more first), an OPEN waits,
            // and both wait while that RMLEAK write waits for a busy GATT pool.
            ESP_LOGW(BLE_TAG, "[CMD] %s=%u held behind the pending RMLEAK command", what, val);
            rc = CMD_WR_RELINK;
        }
        else
        {
            uint16_t conn = valve_conn_handle;
            *conn_out = conn;
            // While polling a busy pool, only a write that got a procedure is logged. rc is the
            // previous attempt's: a genuine retry after the wait is logged in full.
            bool polling = (rc == BLE_HS_ENOMEM);
            if (!polling)
                ESP_LOGI(BLE_TAG, "[CMD] Writing %s=%u", what, val);
            rc = ble_gattc_write_flat(conn, *handle, &val, 1, NULL, NULL);
            if (!polling || rc != BLE_HS_ENOMEM)
                ESP_LOGI(BLE_TAG, "[CMD] %s write rc=%d (value awaits the valve's own report)", what, rc);
            if (rc == 0)
            {
                // Before gatt_mutex is released (see above).
                (void)pending_update(slot, replay ? (int)val : PEND_ANY, -1, gen);
                if (!replay)
                    relink_count_reset();
                // The read-back write_valve_command() / write_rmleak_command() explain.
                int rrc = ble_gattc_read(conn, *handle, on_cmd_read_cb, NULL);
                if (rrc == BLE_HS_ENOMEM)
                    read_back_busy = true;   // issued below, once a procedure frees
                else if (rrc != 0)
                    ESP_LOGW(BLE_TAG, "[CMD] %s read-back rc=%d - %s unconfirmed", what, rrc, unconfirmed);
            }
            else if (rc == BLE_HS_ENOTCONN || !link_is_target())
            {
                // Retrying on this link is pointless: pend for the provisioned valve's next one.
                ESP_LOGW(BLE_TAG, "[CMD] %s write rc=%d - link to the provisioned valve lost", what, rc);
                rc = CMD_WR_RELINK;
            }
        }
        xSemaphoreGive(gatt_mutex);

        if (read_back_busy)
            read_back_when_free(*conn_out, handle, gen, what, unconfirmed);
        if (rc == 0 || rc == CMD_WR_STALE || rc == CMD_WR_RELINK || rc == CMD_WR_MOVED)
            return rc;
        if (rc == BLE_HS_ENOMEM)
        {
            // The pool is busy with this hub's own commands: wait for a free procedure, without
            // using up an attempt. Past the bound the caller keeps the command pended and has it
            // replayed (drop_link_after_failed_write()).
            if (busy_ms >= CMD_BUSY_MAX_MS)
                return rc;
            if (busy_ms == 0)
                ESP_LOGW(BLE_TAG, "[CMD] %s write: GATT busy - waiting", what);
            vTaskDelay(pdMS_TO_TICKS(CMD_BUSY_RETRY_MS));
            busy_ms += CMD_BUSY_RETRY_MS;
            attempt--;   // not one of the CMD_WRITE_ATTEMPTS
        }
    }
    return rc;
}

// A command waits in its pending slot for the provisioned valve's next setup completion:
// make sure a link comes. With a foreign link still up, its DISCONNECT rescans
// (g_connect_requested). Command task only. A write answered BLE_HS_ENOTCONN on a handle that
// NimBLE no longer knows starts the stale-handle check here (link_stale_check()): once it is
// confirmed, closing the link rescans.
static void request_valve_link(void)
{
    g_connect_requested = true;
    (void)link_stale_check();
    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
        request_hunt();
}

// Every attempt at a hub write failed on a live link, and the command is pended: drop that
// link (`conn`, the one the writes went to; NONE = the current one), and the setup completion
// of the next one replays the command (apply_pending_cmd()). Command task only.
// At most CMD_MAX_FORCED_RELINKS drops in a row: a command the valve keeps refusing must not
// cycle replay -> failed attempts -> reconnect for ever (valve battery, and every cycle is a
// CONNECTED/DISCONNECTED pair and a rules reconcile). After that it waits for the next
// natural reconnect. A live command the host accepts that leaves nothing pended
// (relink_count_reset()), or a valve target change, starts a new count.
// Never for BLE_HS_ENOMEM: the GATT procedure pool stayed busy with this hub's own commands
// (CMD_BUSY_RETRY_MS). The link works, and dropping it would discard the commands queued on
// it, a replayed CLOSE among them (B1). The command task replays the command instead.
static void drop_link_after_failed_write(int rc, uint16_t conn, const char *what, uint8_t val)
{
    if (rc == BLE_HS_ENOMEM)
    {
        ESP_LOGW(BLE_TAG, "[CMD] %s=%u kept pending - GATT still busy, replaying it", what, val);
        post_replay_token();
        return;
    }

    g_connect_requested = true;
    if (conn == BLE_HS_CONN_HANDLE_NONE)
        conn = valve_conn_handle;
    if (conn == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGE(BLE_TAG, "[CMD] valve write failed %d times (rc=%d) - reconnecting to re-apply (%s=%u)",
                 CMD_WRITE_ATTEMPTS, rc, what, val);
        request_hunt();
        return;
    }

    taskENTER_CRITICAL(&s_mac_lock);
    if (s_relink_gen != s_cmd_gen)
    {
        s_relink_gen = s_cmd_gen;
        s_relink_count = 0;
    }
    uint8_t forced = s_relink_count;
    if (forced <= CMD_MAX_FORCED_RELINKS)
        s_relink_count = forced + 1;   // stops at MAX + 1: the refusal below is logged once
    taskEXIT_CRITICAL(&s_mac_lock);

    if (forced >= CMD_MAX_FORCED_RELINKS)
    {
        if (forced == CMD_MAX_FORCED_RELINKS)
            ESP_LOGE(BLE_TAG, "[CMD] valve writes keep failing - no more forced reconnects until a write succeeds");
        ESP_LOGE(BLE_TAG, "[CMD] valve write failed %d times (rc=%d) - kept for the next link, "
                 "no forced reconnect (%s=%u)", CMD_WRITE_ATTEMPTS, rc, what, val);
        return;
    }

    ESP_LOGE(BLE_TAG, "[CMD] valve write failed %d times (rc=%d) - reconnecting to re-apply (%s=%u)",
             CMD_WRITE_ATTEMPTS, rc, what, val);
    // Set BEFORE the terminate: set after it, a DISCONNECT and the next CONNECT the host task
    // handled first would leave it set for the whole new link, pending every command there.
    s_link_dropping = true;   // later commands pend behind this one
    int trc = ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    if (trc != 0 && (trc != BLE_HS_EALREADY || s_term_failed))
    {
        // The link is already gone (its DISCONNECT, which clears the flag, has run), or it
        // stays up. BLE_HS_EALREADY: it is being dropped already, and its DISCONNECT (or
        // TERM_FAILURE) clears the flag; after a TERM_FAILURE it is a refusal like any other.
        s_link_dropping = false;
        ESP_LOGE(BLE_TAG, "[CMD] terminate rc=%d - %s=%u stays pending for the next link",
                 trc, what, val);
    }
}

// Settles a live hub command after write_cmd_with_retry(). Releases the settle barrier armed
// at enqueue on every path: once we know whether the write went out (or that it will not now),
// the snapshot has nothing left to wait for. `slot` is the command's pending slot, `conn` the
// link the writes went to, `handle` its characteristic (NULL: a valve command deliberately
// held behind a failed RMLEAK, which must not be replayed from here).
static void finish_cmd_write(int rc, uint8_t val, uint32_t gen, int *slot, uint16_t conn,
                             const uint16_t *handle, const char *what)
{
    if (rc == CMD_WR_STALE || (rc != 0 && !pending_update(slot, PEND_ANY, (int)val, gen)))
    {
        // Issued for the previous valve (or for none): pending it would replay it on the
        // valve provisioned now (P0-c).
        ESP_LOGW(BLE_TAG, "[CMD] %s write val=%u dropped - the valve target changed since it was issued",
                 what, val);
    }
    else if (rc == CMD_WR_RELINK)
    {
        // Pended above, for the provisioned valve's next setup completion.
        ESP_LOGW(BLE_TAG, "[CMD] %s write not ready. Queuing val=%u", what, val);
        request_valve_link();
        // The link was found not ready under gatt_mutex, but the pend above lands after its
        // release: a setup completion in between read the slot empty, and the command would
        // sit pended on a ready link. Re-checked AFTER the pend, a link that is not ready yet
        // sees the pend at its own completion; one that is ready now may have missed it, so
        // replay it. If that completion did write it, the replay finds the slot empty.
        if (handle && cmd_link_ready(*handle))
        {
            ESP_LOGW(BLE_TAG, "[CMD] %s val=%u pended as the link became ready - replaying it",
                     what, val);
            post_replay_token();
        }
    }
    else if (rc != 0)
    {
        // Pended above. Every attempt failed on a live link, or the GATT procedure pool stayed
        // busy (BLE_HS_ENOMEM: replayed, never a forced reconnect).
        drop_link_after_failed_write(rc, conn, what, val);
    }
    cmd_settle_release();
}

// Replays the command pended in *slot (command task: replay_pending_cmds(), and the
// interlock ahead of a live valve command in write_valve_command()). Returns 0, or the last
// failure (write_cmd_with_retry()) when it failed on a live link: it stays pended, and a valve
// command must not overtake it. BLE_HS_ENOMEM: the GATT pool stayed busy, and the replay token
// is queued to try it again (drop_link_after_failed_write()).
static int replay_pending_slot(int *slot, const uint16_t *handle, uint32_t gen,
                               const char *what, const char *unconfirmed)
{
    int v = pending_get(slot, gen);
    if (v != 0 && v != 1)
        return 0;   // nothing pended any more: written, cancelled or superseded meanwhile

    ESP_LOGI(BLE_TAG, "[CMD] Replaying pending %s command=%d", what, v);
    uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
    int rc = write_cmd_with_retry(handle, (uint8_t)v, gen, slot, true, &conn, what, unconfirmed);
    if (rc == 0)
        return 0;
    if (rc == CMD_WR_STALE || rc == CMD_WR_MOVED)
    {
        ESP_LOGW(BLE_TAG, "[CMD] Pending %s command=%d cancelled, superseded or flushed meanwhile - not replayed",
                 what, v);
        return 0;
    }
    if (rc == CMD_WR_RELINK)
    {
        // Still pended, for the next setup completion. The valve command is not held back: it
        // checks its own link, so it goes out only on a live link that merely lacks the RMLEAK
        // characteristic, as at setup completion.
        ESP_LOGW(BLE_TAG, "[CMD] Pending %s command=%d kept for the next link", what, v);
        request_valve_link();
        return 0;
    }
    drop_link_after_failed_write(rc, conn, what, (uint8_t)v);
    return rc;
}

static void write_valve_command(uint8_t val, uint32_t gen)
{
    // The target was removed after this was queued: drop it. Pending it would replay it
    // on the next valve provisioned (P0-c).
    if (!ble_valve_has_target_mac())
    {
        ESP_LOGW(BLE_TAG, "[CMD] Valve write val=%u dropped - no provisioned valve", val);
        cmd_settle_release();
        return;
    }

    // RMLEAK before the valve command, as at setup completion: an interlock command still
    // pended on this live link goes first. Its write failed and no reconnect followed (the
    // CMD_MAX_FORCED_RELINKS cap, or a failed terminate), so nothing else would write it
    // before this command. If it fails again, an OPEN stays pended behind it; a CLOSE still
    // goes, since the interlock was tried first and holding a CLOSE back during a leak is
    // worse. When that failure drops the link, the command pends behind it (CMD_WR_RELINK).
    if (cmd_link_ready(h_rmleak_char) &&
        replay_pending_slot(&g_pending_rmleak_cmd, &h_rmleak_char, gen, "RMLEAK", "interlock state") != 0 &&
        val == 1)
    {
        ESP_LOGW(BLE_TAG, "[CMD] Valve=%u not written - kept behind the pending RMLEAK command", val);
        finish_cmd_write(CMD_WR_RELINK, val, gen, &g_pending_valve_cmd, BLE_HS_CONN_HANDLE_NONE,
                         NULL, "Valve");
        return;
    }

    // A link that is not ready, or not the provisioned valve's, pends the command for the
    // real one (CMD_WR_RELINK). write_cmd_with_retry() issues the read-back below after
    // every accepted write.
    //
    // The cache is DELIBERATELY not written here.
    //
    // This used to do `g_val_state = val; notify_hub_update(BLE_UPD_STATE);`
    // on rc == 0. But rc == 0 is local NimBLE host acceptance — the write has
    // been queued, not delivered, not acknowledged, and certainly not acted
    // on. The hub was therefore publishing its own INTENT as fact: both wire
    // legs (valve_state_changed and the snapshot's data.valve.state) read
    // g_val_state. A valve that never received the command, or received it
    // and failed to move, was reported as closed.
    //
    // Worse, it was self-concealing: when the valve's real value did arrive,
    // on_notify()'s delta gate (old_state == g_val_state) saw no change and
    // suppressed the correcting event.
    //
    // Leaving the cache alone makes the valve the source of truth. The valve
    // notifies CUSTOM_STM_VALVESTATE from its own BLE-write handler
    // (DK-Servo_Motor app_main.c, the `echo` push), so a command that lands
    // produces a genuine old != new transition and emits
    // valve_state_changed sourced from the device. A command that does not
    // land produces no event, and the snapshot keeps honestly reporting the
    // last state the valve actually reported. Setup step 5 re-reads the
    // characteristic on every reconnect, so a lost notify self-corrects.
    //
    // Honest limitation: the valve reports its COMMANDED state, not a sensed
    // position — it has no position feedback. This upgrades the hub from
    // "reports what it asked for" to "reports what the valve says it did",
    // which proves delivery and actuation intent. It does not prove the gate
    // physically moved.
    //
    // BACKSTOP: read the characteristic straight back, because the notify is
    // not guaranteed. on_dsc_disc_cb() logs a missing CCCD and then calls
    // setup_next_step() anyway, so a session can reach DISCOVERY_DONE with no
    // VALVESTATE subscription at all. Before this commit the optimistic cache
    // write accidentally masked that; without a backstop it would surface as
    // a repeating auto_close — rules_engine's idempotence guard
    // (valve_state == 0 && rmleak_already) could never become true, so every
    // cooldown would re-issue the close while a sensor stayed wet.
    //
    // ATT is sequential on one connection, so this read is serviced after the
    // write and returns the value the valve's write handler just published.
    // It lands via on_cmd_read_cb() -> on_notify(), i.e. through the SAME delta
    // gate as an unsolicited notify, so it cannot double-report. One extra
    // round trip per valve command, and commands are rare.
    uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
    int rc = write_cmd_with_retry(&h_valve_char, val, gen, &g_pending_valve_cmd, false, &conn,
                                  "Valve", "position");
    finish_cmd_write(rc, val, gen, &g_pending_valve_cmd, conn, &h_valve_char, "Valve");
}

// -----------------------------------------------------------------------------
// RMLEAK WRITE COMMAND
// -----------------------------------------------------------------------------
static void write_rmleak_command(uint8_t val, uint32_t gen)
{
    // Same target rules as write_valve_command().
    if (!ble_valve_has_target_mac())
    {
        ESP_LOGW(BLE_TAG, "[CMD] RMLEAK write val=%u dropped - no provisioned valve", val);
        cmd_settle_release();
        return;
    }

    // Like write_valve_command(), this deliberately does NOT cache the
    // value it just asked for. rc == 0 is local host acceptance, and the
    // same false-assurance argument applies verbatim: reporting
    // rmleak:true for a write that never reached the valve is exactly the
    // failure P0-7 removed from the position path. The valve pushes
    // CUSTOM_STM_REMOTE_LEAK back itself, so on_notify() supplies the
    // real value; setup re-reads it on every reconnect.
    //
    // Leaving this asymmetric would have been worse than either choice
    // consistently: rules_engine_on_valve_connected() disambiguates a
    // physical override by comparing valve state against rmleak, and
    // feeding it one optimistic and one honest input is how that
    // inference goes wrong.
    //
    // The valve's report posts BLE_UPD_RMLEAK (on_notify(), on a change).
    // It refreshes valve liveness in the health engine and emits no D2C
    // event of its own; iothub_task couples a snapshot to it, so the cloud
    // gets the interlock state the valve reports within the 5 s clamp,
    // not at the next heartbeat.
    //
    // READ-BACK IS MANDATORY HERE, unlike the position path where it is
    // only a backstop. The valve echoes CUSTOM_STM_VALVESTATE from its own
    // BLE-write handler, but it does NOT do the same for REMOTE_LEAK — it
    // pushes that characteristic only on the physical-override path and at
    // init (DK-Servo_Motor app_main.c:227, :72). So a hub-written RMLEAK
    // produces NO notify at all, and without this read the cache would
    // stay stale until the next reconnect re-read it.
    //
    // Observed in the field on 2.1.1 before this line existed: nine
    // auto_close events reporting rmleak_asserted:true, valve connected
    // throughout, and valve.rmleak false in every snapshot — which also
    // kept the fleet LED green through an active leak, since its override
    // reads ble_valve_get_rmleak_state().
    uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
    int rc = write_cmd_with_retry(&h_rmleak_char, val, gen, &g_pending_rmleak_cmd, false, &conn,
                                  "RMLEAK", "interlock state");
    finish_cmd_write(rc, val, gen, &g_pending_rmleak_cmd, conn, &h_rmleak_char, "RMLEAK");
}

// -----------------------------------------------------------------------------
// REPLAY OF PENDING COMMANDS (command task)
// -----------------------------------------------------------------------------
// Takes the replay token (post_replay_token()): replays what is still pended, RMLEAK first,
// each through write_cmd_with_retry() with the usual generation and link checks, and only
// while its slot still holds it (a cancel, or a newer command of its kind, wins). Every
// command queued after the token is newer and runs after this.
// After a failed RMLEAK write only an OPEN is held back; a CLOSE still goes, for the reason
// write_valve_command() gives. Not after BLE_HS_ENOMEM: that RMLEAK write has not failed, it
// waits for a GATT procedure, and a CLOSE replayed now would poll the same pool and could take
// the first one that frees, ahead of it. The CLOSE stays pended behind the replay token
// drop_link_after_failed_write() just queued, which writes the RMLEAK command first. If the
// pool never frees, NimBLE's GATT timeout drops the link, and setup completion writes both,
// RMLEAK first. Only while that token is queued: when the command queue was full, the CLOSE
// goes now rather than wait for the next link.
static void replay_pending_cmds(uint32_t gen)
{
    if (!cmd_gen_is_current(gen))
    {
        ESP_LOGW(BLE_TAG, "[CMD] Replay of pending valve commands dropped - the valve target changed");
    }
    else
    {
        int rc = replay_pending_slot(&g_pending_rmleak_cmd, &h_rmleak_char, gen, "RMLEAK", "interlock state");
        int v = pending_get(&g_pending_valve_cmd, gen);
        if (rc != 0 && v == 1)
            ESP_LOGW(BLE_TAG, "[CMD] Pending valve command=1 kept behind the RMLEAK command");
        else if (rc == BLE_HS_ENOMEM && v == 0 && replay_token_queued(gen))
            ESP_LOGW(BLE_TAG, "[CMD] Pending valve command=0 kept behind the RMLEAK command (GATT busy)");
        else
            (void)replay_pending_slot(&g_pending_valve_cmd, &h_valve_char, gen, "Valve", "position");
    }
    cmd_settle_release();   // armed by post_replay_token()
}

// -----------------------------------------------------------------------------
// NIMBLE HOST TASK
// -----------------------------------------------------------------------------
static void nimble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(BLE_TAG, "[HOST] NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void on_stack_reset(int reason)
{
    ESP_LOGE(BLE_TAG, "[HOST] NimBLE stack reset: reason=%d", reason);
    g_ble_synced = false;
    g_connecting = false;   // a reset ends every connect: none of ours can be in flight now
    clear_all_state_bits();
}

static void on_stack_sync(void)
{
    ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(BLE_TAG, "║            NIMBLE STACK SYNCED                               ║");
    ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");

    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        ESP_LOGE(BLE_TAG, "[HOST] ble_hs_util_ensure_addr rc=%d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(BLE_TAG, "[HOST] ble_hs_id_infer_auto rc=%d", rc);
        g_own_addr_type = BLE_OWN_ADDR_PUBLIC;
    }

    uint8_t addr[6];
    ble_hs_id_copy_addr(g_own_addr_type, addr, NULL);
    ESP_LOGI(BLE_TAG, "[HOST] Own address: %02X:%02X:%02X:%02X:%02X:%02X (type=%u)",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], g_own_addr_type);

    ESP_LOGI(BLE_TAG, "[HOST] Security Manager ready");
    ESP_LOGI(BLE_TAG, "[HOST] SM Config: io_cap=%d, bonding=%d, mitm=%d, sc=%d",
             ble_hs_cfg.sm_io_cap, ble_hs_cfg.sm_bonding,
             ble_hs_cfg.sm_mitm, ble_hs_cfg.sm_sc);

    taskENTER_CRITICAL(&s_mac_lock);
    s_host_reset_asked = false;   // a reset connect_lost_by_host() asked for is done
    taskEXIT_CRITICAL(&s_mac_lock);
    g_ble_synced = true;

    if (g_connect_requested)
        request_hunt();
}

// -----------------------------------------------------------------------------
// LINK CHECKS (command task)
// -----------------------------------------------------------------------------
// The stale-handle check (WP3, see s_stale_handle). The first time valve_conn_handle names no
// NimBLE link only starts the clock: that link's DISCONNECT may be on its way. Still so
// STALE_LINK_CONFIRM_MS later, with no connect in flight (whose CONNECT would replace the handle),
// no event for that link can come any more, and it is closed here. Called on every pass
// (link_poll()) and where a write or a terminate found no link (BLE_HS_ENOTCONN), before a rescan
// that would otherwise stop at "Already connected". True when it closed a stale link.
static bool link_stale_check(void)
{
    uint16_t h = valve_conn_handle;
    if (h == BLE_HS_CONN_HANDLE_NONE || ble_gap_conn_find(h, NULL) == 0)
    {
        s_stale_handle = BLE_HS_CONN_HANDLE_NONE;
        return false;
    }
    TickType_t now = xTaskGetTickCount();
    if (s_stale_handle != h)
    {
        s_stale_handle = h;
        s_stale_since = now;
        return false;
    }
    if ((now - s_stale_since) < pdMS_TO_TICKS(STALE_LINK_CONFIRM_MS) || ble_gap_conn_active())
        return false;
    s_stale_handle = BLE_HS_CONN_HANDLE_NONE;
    ESP_LOGE(BLE_TAG, "[DISCONNECT] Link handle %u is stale (no such NimBLE link for %d ms, no DISCONNECT) - closing it here",
             h, STALE_LINK_CONFIRM_MS);
    link_closed();
    return true;
}

// Every pass of the command task. The stale-handle check, and a connect in flight that this
// module did not start (every one of its own sets g_connecting first): NimBLE's connect
// re-attempt (BLE_GAP_EVENT_REATTEMPT_COUNT). It is cancelled, so no link is made behind the
// module's back (I5); its CONNECT (status BLE_HS_EAPP) rescans when a link is wanted.
// ble_gap_conn_active() is read before g_connecting: a connect seen in flight was issued after
// its own g_connecting was set.
// And the reverse: a connect of ours that NimBLE does not run (connect_lost_by_host()), still so
// ORPHAN_CONFIRM_MS later for the same connect. Between ble_valve_claim_start()'s g_connecting and
// its ble_gap_connect(), and between NimBLE's end of a connect and its CONNECT event, the two
// disagree for microseconds only.
static void link_poll(void)
{
    (void)link_stale_check();
    if (ble_gap_conn_active() && !g_connecting)
    {
        int crc = ble_gap_conn_cancel();
        if (crc == 0)
            ESP_LOGW(BLE_TAG, "[CONNECT] Connect not started by this module (NimBLE re-attempt) cancelled");
    }
    if (g_connecting && !ble_gap_conn_active())
    {
        TickType_t now = xTaskGetTickCount();
        uint8_t seq = s_connect_seq;
        if (s_orphan_since == 0 || s_orphan_seq != seq)
        {
            s_orphan_seq = seq;
            s_orphan_since = now ? now : 1;
        }
        else if ((now - s_orphan_since) >= pdMS_TO_TICKS(ORPHAN_CONFIRM_MS))
        {
            s_orphan_since = 0;
            connect_lost_by_host("NimBLE has run no connect for 1 s");
        }
    }
    else
    {
        s_orphan_since = 0;
    }
}

// The claim policy (WP6, see s_claim_open), every pass: a set-up link held CLAIM_HELD_MS closes its
// claim and resets the back-off.
static void claim_held_poll(void)
{
    if (valve_conn_handle == BLE_HS_CONN_HANDLE_NONE || !is_ready_for_gatt())
        return;
    TickType_t now = xTaskGetTickCount();
    uint8_t had = 0;
    bool held = false;
    taskENTER_CRITICAL(&s_mac_lock);
    if (s_claim_open && (now - s_link_up_at) >= pdMS_TO_TICKS(CLAIM_HELD_MS))
    {
        s_claim_open = false;
        had = s_claim_fails;
        s_claim_fails = 0;
        held = true;
    }
    taskEXIT_CRITICAL(&s_mac_lock);
    if (held && had > 0)
        ESP_LOGI(BLE_TAG, "[CLAIM] Valve link held %d s - claim back-off reset (it was at step %u of %u)",
                 CLAIM_HELD_MS / 1000, (unsigned)had, (unsigned)CLAIM_FAILS_MAX);
}

// The leak-response trigger and its overlay (WP6; plan §4.1, D5), every pass:
//   trigger = valve provisioned AND not linked AND (an RMLEAK / CLOSE pended, OR a leak incident
//             latched AND the valve has not confirmed the interlock in it);
//   overlay = trigger AND less than RP_LR_OVERLAY_CAP_MS since the episode began (the radio
//             policy's, radio_policy_note_lr(), which this pass feeds).
// The incident latch is read lock-free, through the rules engine's own mirror of it,
// health_is_interlock_held(): rules_engine.c stores it with every change of the latch, under its
// mutex (incident_save_to_nvs(), and rules_engine_reset_all()'s release). NOT
// rules_engine_is_leak_incident_active(), which takes the rules mutex for up to 1 s: this task
// writes RMLEAK and CLOSE, and would wait behind a rules hold (provisioning reads under it take up
// to 1 s each) on every pass; and that read answers "no incident" when it times out, which dropped
// the interlock confirmation and restarted the 10 min overlay for a valve already confirmed.
// The interlock is confirmed, for the rest of the incident, when the provisioned valve's own
// reports on a set-up link read RMLEAK=1 and CLOSED. The episode ends when nothing is pended and
// no incident is latched. Pended commands stay pended after the cap: the next claim writes them.
static void lr_poll(void)
{
    bool provisioned = ble_valve_has_target_mac();
    bool incident = provisioned && health_is_interlock_held();
    bool pended = leak_response_pending();
    bool linked = (valve_conn_handle != BLE_HS_CONN_HANDLE_NONE);

    if (!incident)
    {
        s_interlock_ok = false;
    }
    else if (!s_interlock_ok && ble_valve_is_ready() && g_val_rmleak && g_val_state == 0)
    {
        s_interlock_ok = true;
        ESP_LOGI(BLE_TAG, "[LR] The valve confirms the interlock for this incident (RMLEAK=1, CLOSED)");
    }

    bool trigger = provisioned && !linked && (pended || (incident && !s_interlock_ok));
    s_lr_trigger = trigger;
    rp_lr_t lr = radio_policy_note_lr(trigger, pended || incident, incident);   // wakes the executor on a change
    if (lr.changed)
    {
        if (lr.overlay && lr.started)
            ESP_LOGW(BLE_TAG, "[LR] Leak response pending, valve not linked (%s) - leak-response scanning, at most %u s for this incident",
                     pended ? "RMLEAK/CLOSE pended" : "incident latched, interlock not confirmed",
                     (unsigned)(RP_LR_OVERLAY_CAP_MS / 1000));
        else if (lr.overlay)
            ESP_LOGW(BLE_TAG, "[LR] Leak-response scanning resumed (valve not linked), %lu s into this incident's %u s",
                     (unsigned long)lr.ran_s, (unsigned)(RP_LR_OVERLAY_CAP_MS / 1000));
        else
            ESP_LOGW(BLE_TAG, "[LR] Leak-response scanning ended after %lu s (%s)", (unsigned long)lr.ran_s,
                     linked ? "valve linked"
                            : (trigger ? "the cap per incident - normal scanning, claims go on"
                                       : "leak response written or withdrawn"));
    }
}

// -----------------------------------------------------------------------------
// BLE COMMAND TASK
// -----------------------------------------------------------------------------
// How long the command task waits for a command before its per-pass checks run again.
#define CMD_POLL_MS 1000

static void ble_valve_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t item = 0;

    ESP_LOGI(BLE_TAG, "[TASK] BLE command task started");

    while (1)
    {
        link_poll();
        claim_held_poll();
        lr_poll();
        if (xQueueReceive(ble_cmd_queue, &item, pdMS_TO_TICKS(CMD_POLL_MS)) != pdTRUE)
            continue;

        // Not a command: run the checks above now (BLE_GAP_EVENT_REATTEMPT_COUNT).
        if (CMD_ITEM_IS_WAKE(item))
            continue;

        // The generation the command was issued under (see s_cmd_gen): a valve write from
        // before a target change is dropped by write_cmd_with_retry().
        uint32_t gen = CMD_ITEM_GEN(item);

        // Not a ble_valve_cmd_t: setup completion could not write a pended command.
        if (CMD_ITEM_IS_REPLAY(item))
        {
            ESP_LOGI(BLE_TAG, "[TASK] CMD: REPLAY_PENDING");
            replay_pending_cmds(gen);
            continue;
        }

        switch (CMD_ITEM_CMD(item))
        {
        case BLE_CMD_CONNECT:
            ESP_LOGI(BLE_TAG, "[TASK] CMD: CONNECT");
            // Queued before the target was removed: there is nothing to connect to.
            if (!ble_valve_has_target_mac())
            {
                ESP_LOGW(BLE_TAG, "[TASK] CONNECT ignored - no provisioned valve");
                g_connect_requested = false;
                break;
            }
            g_connect_requested = true;
            (void)link_stale_check();   // a stale handle would stop the rescan at "Already connected"
            request_hunt();
            break;

        case BLE_CMD_OPEN_VALVE:
            ESP_LOGI(BLE_TAG, "[TASK] CMD: OPEN_VALVE");
            write_valve_command(1, gen);
            break;

        case BLE_CMD_CLOSE_VALVE:
            ESP_LOGI(BLE_TAG, "[TASK] CMD: CLOSE_VALVE");
            write_valve_command(0, gen);
            break;

        case BLE_CMD_DISCONNECT:
            ESP_LOGI(BLE_TAG, "[TASK] CMD: DISCONNECT");
            g_connect_requested = false;
            // Stop every stage, not only an established link (N8): a connect still in
            // flight would otherwise complete and link the valve anyway, and a valve hunt
            // would keep running. The cancelled connect reports status BLE_HS_EAPP, which
            // the CONNECT handler does not rescan on. With g_connect_requested false the hunt
            // is no longer wanted, and the executor, woken here, drops it.
            if (g_connecting)
            {
                int crc = ble_gap_conn_cancel();
                ESP_LOGI(BLE_TAG, "[TASK] Connect in flight cancelled (rc=%d)", crc);
                g_connecting = false;
            }
            s_hunt_announced = false;
            s_claim_req = false;
            app_ble_leak_kick();
            if (valve_conn_handle != BLE_HS_CONN_HANDLE_NONE &&
                ble_gap_terminate(valve_conn_handle, BLE_ERR_REM_USER_CONN_TERM) == BLE_HS_ENOTCONN)
                (void)link_stale_check();   // no such link: closed here once confirmed
            break;

        case BLE_CMD_SECURE:
            ESP_LOGI(BLE_TAG, "[TASK] CMD: SECURE");
            if (valve_conn_handle != BLE_HS_CONN_HANDLE_NONE && !is_link_encrypted())
                initiate_security();
            break;

        case BLE_CMD_SET_RMLEAK:
            ESP_LOGI(BLE_TAG, "[TASK] CMD: SET_RMLEAK");
            write_rmleak_command(1, gen);
            break;

        case BLE_CMD_CLEAR_RMLEAK:
            ESP_LOGI(BLE_TAG, "[TASK] CMD: CLEAR_RMLEAK");
            write_rmleak_command(0, gen);
            break;

        default:
            break;
        }
    }
}

// -----------------------------------------------------------------------------
// STARTER TASK
// -----------------------------------------------------------------------------
static void ble_starter_task(void *param)
{
    (void)param;

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ESP_LOGI(BLE_TAG, "[INIT] Signal received. Starting BLE stack...");

    // Retried: the likely failure is a heap shortfall for the controller, which can pass,
    // and giving up leaves the hub with no valve link and no BLE leak sensors until a
    // reboot. nimble_port_init() disables/deinits the controller again when it fails, but a
    // failed esp_nimble_init() returns without freeing the NPL function table and mempools
    // it had just allocated (ESP-IDF 5.5.1 nimble_port.c:97-105), and the next attempt
    // allocates them again. They are released after every failure: nothing uses them yet
    // (the first NPL object is created after that point), and both calls only free what is
    // allocated, so they are no-ops when the controller stage failed.
    int rc = ESP_FAIL;
    for (int attempt = 1; attempt <= NIMBLE_INIT_ATTEMPTS; attempt++)
    {
        rc = nimble_port_init();
        if (rc == ESP_OK)
            break;
        npl_freertos_mempool_deinit();
        npl_freertos_funcs_deinit();
        if (attempt < NIMBLE_INIT_ATTEMPTS)
        {
            ESP_LOGW(BLE_TAG, "[INIT] nimble_port_init failed (rc=%d), attempt %d/%d - retrying in %d s",
                     rc, attempt, NIMBLE_INIT_ATTEMPTS, NIMBLE_INIT_RETRY_MS / 1000);
            vTaskDelay(pdMS_TO_TICKS(NIMBLE_INIT_RETRY_MS));
        }
    }
    if (rc != ESP_OK) {
        ESP_LOGE(BLE_TAG, "[INIT] nimble_port_init failed (rc=%d) after %d attempts - BLE is not available",
                 rc, NIMBLE_INIT_ATTEMPTS);
        ble_starter_task_handle = NULL;   // gone: see app_ble_valve_signal_start()
        vTaskDelete(NULL);
        return;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("eFloStopHub");

    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.reset_cb = on_stack_reset;

    // -------------------------------------------------------------------------
    // SECURITY MANAGER CONFIGURATION
    // Must match STM32WB valve settings:
    //   - MITM required (passkey entry)
    //   - Bonding enabled
    //   - Secure Connections mandatory
    //   - Fixed passkey: 222900
    // -------------------------------------------------------------------------
    ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(BLE_TAG, "║            SECURITY MANAGER CONFIGURATION                    ║");
    ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_KEYBOARD_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    extern void ble_store_config_init(void);
    ble_store_config_init();
    ESP_LOGI(BLE_TAG, "[SM] Store config initialized");

    ESP_LOGI(BLE_TAG, "[SM] IO Capability: KEYBOARD_ONLY");
    ESP_LOGI(BLE_TAG, "[SM] Bonding: ENABLED");
    ESP_LOGI(BLE_TAG, "[SM] MITM: REQUIRED");
    ESP_LOGI(BLE_TAG, "[SM] Secure Connections: ENABLED");
    ESP_LOGI(BLE_TAG, "[SM] Fixed Passkey: configured (not logged)");

    // Create timers
    sec_timeout_timer = xTimerCreate("ble_sec_to", pdMS_TO_TICKS(SECURITY_TIMEOUT_MS),
                                     pdFALSE, NULL, sec_timeout_cb);
    post_connect_timer = xTimerCreate("ble_post_conn", pdMS_TO_TICKS(POST_CONNECT_SECURITY_DELAY_MS),
                                      pdFALSE, NULL, post_connect_timer_cb);
    discovery_timeout_timer = xTimerCreate("ble_disc_to", pdMS_TO_TICKS(DISCOVERY_TIMEOUT_MS),
                                           pdFALSE, NULL, discovery_timeout_cb);
    security_retry_timer = xTimerCreate("ble_sec_retry", pdMS_TO_TICKS(SECURITY_RETRY_DELAY_MS),
                                        pdFALSE, NULL, security_retry_timer_cb);

    nimble_port_freertos_init(nimble_host_task);
    xTaskCreate(ble_valve_task, "ble_valve", 4096, NULL, 5, NULL);
    ble_valve_connect();   // refused (logged) on a hub with no provisioned valve

    // Signal BLE leak scanner that NimBLE stack is ready
    app_ble_leak_signal_start();

    ble_starter_task_handle = NULL;   // gone: see app_ble_valve_signal_start()
    vTaskDelete(NULL);
}

// -----------------------------------------------------------------------------
// PUBLIC API
// -----------------------------------------------------------------------------
void app_ble_valve_init(void)
{
    ESP_LOGI(BLE_TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(BLE_TAG, "║            BLE VALVE MODULE INIT                             ║");
    ESP_LOGI(BLE_TAG, "║            Event-Driven Security Model                       ║");
    ESP_LOGI(BLE_TAG, "╚══════════════════════════════════════════════════════════════╝");

    ble_state_event_group = xEventGroupCreate();
    if (ble_state_event_group == NULL)
    {
        ESP_LOGE(BLE_TAG, "[INIT] Failed to create state event group");
        return;
    }

    gatt_mutex = xSemaphoreCreateMutex();
    if (gatt_mutex == NULL)
    {
        ESP_LOGE(BLE_TAG, "[INIT] Failed to create GATT mutex");
        return;
    }

    ble_cmd_queue = xQueueCreate(10, sizeof(uint32_t));   // CMD_ITEM: command + generation
    if (ble_cmd_queue == NULL)
    {
        ESP_LOGE(BLE_TAG, "[INIT] Failed to create command queue");
        return;
    }

    /* Depth 16, not 5. One auto-close alone posts RMLEAK + STATE + BATTERY, and
     * iothub_task dequeues only ONE item per loop iteration — so a burst during
     * a leak incident could previously overrun a 5-slot queue and drop a
     * CONNECTED, skipping reconnect reconciliation. 16 * sizeof(enum) is a few
     * dozen bytes against ~37 KB free heap. */
    ble_update_queue = xQueueCreate(16, sizeof(ble_update_type_t));
    if (ble_update_queue == NULL)
    {
        ESP_LOGE(BLE_TAG, "[INIT] Failed to create update queue");
        return;
    }

    xTaskCreate(ble_starter_task, "ble_starter", 3072, NULL, 5, &ble_starter_task_handle);
}

// Called from iothub_task (boot, the owed BLE-apply retry) and from esp-mqtt (`provision`),
// possibly at once: the start is claimed atomically, so exactly one caller notifies. Only that
// notify lets ble_starter_task run, and it clears its handle before deleting itself, so the
// caller that claimed the start notifies a live task. No handle yet: nothing is claimed, and a
// later call can still start BLE.
void app_ble_valve_signal_start(void)
{
    static atomic_int is_started = 0;
    TaskHandle_t starter = ble_starter_task_handle;
    if (starter == NULL)
        return;
    if (atomic_exchange(&is_started, 1) != 0)
        return;
    xTaskNotifyGive(starter);
}

// Tagged with the current generation: if the valve target changes before the command task
// writes it, the write is dropped rather than sent to the next valve (see s_cmd_gen).
static bool enqueue_cmd(ble_valve_cmd_t cmd)
{
    uint32_t item = CMD_ITEM(cmd, cmd_gen_now());
    return ble_cmd_queue != NULL &&
           xQueueSend(ble_cmd_queue, &item, pdMS_TO_TICKS(10)) == pdTRUE;
}

// With no provisioned valve every command is refused up front: nothing is queued, the
// settle barrier is not armed, and nothing is left to replay on whichever valve is
// provisioned next (P0-a/c).
static bool refuse_without_target(const char *what)
{
    if (ble_valve_has_target_mac())
        return false;
    ESP_LOGW(BLE_TAG, "[CMD] %s refused - no provisioned valve", what);
    return true;
}

// Logs what a valve target change flushed (ble_valve_set_target_mac() does the flushing).
// WARN only when something was actually dropped: the first target at boot counts as a
// change, so an unconditional WARN used to print on every boot with a valve.
static void report_valve_cmd_flush(const char *reason, unsigned queued, int inflight,
                                   int pend_valve, int pend_rmleak)
{
    if (queued == 0 && inflight <= 0 && pend_valve < 0 && pend_rmleak < 0)
    {
        ESP_LOGI(BLE_TAG, "[CMD] No valve commands to flush (%s)", reason);
        return;
    }
    ESP_LOGW(BLE_TAG, "[CMD] Flushed queued/pending valve commands (%s): queued=%u, in flight=%d, "
             "pending valve=%d rmleak=%d", reason, queued, inflight, pend_valve, pend_rmleak);
}

// Arming here rather than at the call sites means every caller — rules engine
// auto-close, C2D valve_open/valve_close, the override paths — gets the snapshot
// settle barrier without having to remember it.
bool ble_valve_open(void)
{
    if (refuse_without_target("OPEN"))
        return false;
    bool queued = enqueue_cmd(BLE_CMD_OPEN_VALVE);
    if (queued) cmd_settle_arm();
    else ESP_LOGE(BLE_TAG, "[CMD] OPEN ENQUEUE FAILED — command queue full, valve NOT commanded");
    return queued;
}

bool ble_valve_close(void)
{
    if (refuse_without_target("CLOSE"))
        return false;
    bool queued = enqueue_cmd(BLE_CMD_CLOSE_VALVE);
    if (queued) cmd_settle_arm();
    else ESP_LOGE(BLE_TAG, "[CMD] CLOSE ENQUEUE FAILED — command queue full, valve NOT commanded");
    return queued;
}

bool ble_valve_connect(void)
{
    if (refuse_without_target("CONNECT"))
        return false;
    return enqueue_cmd(BLE_CMD_CONNECT);
}

// Deliberately NOT gated on a target: it is how a removed valve's link is torn down.
bool ble_valve_disconnect(void)
{
    return enqueue_cmd(BLE_CMD_DISCONNECT);
}

bool ble_valve_get_mac(char *b)
{
    if (b == NULL || valve_conn_handle == BLE_HS_CONN_HANDLE_NONE)
        return false;

    taskENTER_CRITICAL(&s_mac_lock);
    bool have = (g_valve_mac[0] != '\0');
    if (have)
        memcpy(b, g_valve_mac, sizeof(g_valve_mac));
    taskEXIT_CRITICAL(&s_mac_lock);
    return have;
}

uint8_t ble_valve_get_battery(void)
{
    return g_val_battery;
}

bool ble_valve_get_leak(void)
{
    return g_val_leak;
}

int ble_valve_get_state(void)
{
    return g_val_state;
}

// Ready AND linked to the provisioned valve: every caller (health resync, rules tick,
// override, fast snapshot) means "our valve is usable", never "some valve is".
bool ble_valve_is_ready(void)
{
    return is_ready_for_gatt() && link_is_target();
}

bool ble_valve_is_secured(void)
{
    return is_link_encrypted();
}

bool ble_valve_is_authenticated(void)
{
    return (get_state_bits() & BLE_STATE_BIT_AUTHENTICATED) != 0;
}

// A CHANGE (or removal) flushes every valve command issued before it, so none reaches the
// next valve (P0-c, N7): the queue is wiped, then the generation moves with the target in
// one critical section that also clears the pending slots. A command queued between the
// two still carries the old generation and is dropped when dequeued, as is one already
// dequeued, mid-retry or being replayed; one queued after carries the new generation and
// is never wiped.
void ble_valve_set_target_mac(const char *mac_str)
{
    char now_target[18];
    bool changed = false;
    bool had_target = false;
    int pend_valve = -1;
    int pend_rmleak = -1;

    taskENTER_CRITICAL(&s_mac_lock);
    bool will_change = mac_str ? (!g_has_target_mac || strcasecmp(g_target_valve_mac, mac_str) != 0)
                               : g_has_target_mac;
    taskEXIT_CRITICAL(&s_mac_lock);

    unsigned queued = 0;
    int inflight = 0;
    if (will_change && ble_cmd_queue != NULL)
    {
        queued = (unsigned)uxQueueMessagesWaiting(ble_cmd_queue);
        xQueueReset(ble_cmd_queue);
        // The wiped commands' releases will never run; left armed, the barrier would hold
        // snapshots back until its deadline.
        inflight = atomic_exchange(&g_cmd_inflight, 0);
    }

    taskENTER_CRITICAL(&s_mac_lock);
    had_target = g_has_target_mac;
    if (!mac_str)
    {
        changed = g_has_target_mac;
        g_has_target_mac = false;
        g_target_valve_mac[0] = '\0';
    }
    else
    {
        changed = !g_has_target_mac || strcasecmp(g_target_valve_mac, mac_str) != 0;
        strncpy(g_target_valve_mac, mac_str, sizeof(g_target_valve_mac) - 1);
        g_target_valve_mac[sizeof(g_target_valve_mac) - 1] = '\0';
        g_has_target_mac = true;
    }
    if (changed)
    {
        s_cmd_gen = (s_cmd_gen + 1) & CMD_GEN_MASK;
        pend_valve = g_pending_valve_cmd;
        pend_rmleak = g_pending_rmleak_cmd;
        g_pending_valve_cmd = -1;
        g_pending_rmleak_cmd = -1;
    }
    memcpy(now_target, g_target_valve_mac, sizeof(now_target));
    taskEXIT_CRITICAL(&s_mac_lock);

    if (!mac_str)
    {
        g_connect_requested = false;
        s_hunt_announced = false;   // no hunt without a valve (ble_valve_hunt_wanted())
        ESP_LOGI(BLE_TAG, "[API] Target MAC cleared");
    }
    else
    {
        ESP_LOGI(BLE_TAG, "[API] Target MAC set to: %s", now_target);
    }

    // Same valve again (every provision re-applies it): keep its queued commands.
    if (!changed)
        return;

    // A claim requested for the previous valve's advert is not granted: the executor would
    // connect to that valve's address (WP5). The new valve starts with no claim back-off (WP6).
    s_claim_req = false;
    taskENTER_CRITICAL(&s_mac_lock);
    s_claim_fails = 0;
    s_claim_empty = 0;
    s_claim_open = false;
    taskEXIT_CRITICAL(&s_mac_lock);
    app_ble_leak_kick();

    report_valve_cmd_flush(!mac_str ? "valve decommissioned"
                                    : (had_target ? "valve target changed" : "valve target set"),
                           queued, inflight, pend_valve, pend_rmleak);

    // Still linked to the previous valve: drop it so the new one can be found (N6). Queued
    // after the flush above, so it survives it.
    if (mac_str && valve_conn_handle != BLE_HS_CONN_HANDLE_NONE && !link_is_target())
    {
        if (enqueue_cmd(BLE_CMD_DISCONNECT))
            ESP_LOGW(BLE_TAG, "[API] Linked to a valve that is no longer the target - disconnecting");
        else
            ESP_LOGE(BLE_TAG, "[API] DISCONNECT of the previous valve could not be queued");
    }
}

bool ble_valve_has_target_mac(void)
{
    taskENTER_CRITICAL(&s_mac_lock);
    bool has = g_has_target_mac;
    taskEXIT_CRITICAL(&s_mac_lock);
    return has;
}

EventGroupHandle_t ble_valve_get_state_event_group(void)
{
    return ble_state_event_group;
}

void ble_valve_clear_bonds(void)
{
    ESP_LOGI(BLE_TAG, "[API] Clearing all BLE bonds...");
    int rc = ble_store_clear();
    ESP_LOGI(BLE_TAG, "[API] ble_store_clear() rc=%d", rc);
}

bool ble_valve_set_rmleak(bool enabled)
{
    if (refuse_without_target(enabled ? "RMLEAK SET" : "RMLEAK CLEAR"))
        return false;
    bool queued = enqueue_cmd(enabled ? BLE_CMD_SET_RMLEAK : BLE_CMD_CLEAR_RMLEAK);
    if (queued) cmd_settle_arm();
    else ESP_LOGE(BLE_TAG, "[CMD] RMLEAK ENQUEUE FAILED — command queue full, valve NOT commanded");
    return queued;
}

bool ble_valve_cmd_settling(void)
{
    return atomic_load(&g_cmd_inflight) > 0 &&
           esp_timer_get_time() < atomic_load(&g_cmd_deadline_us);
}

bool ble_valve_get_rmleak_state(void)
{
    return g_val_rmleak;
}

bool ble_valve_is_connected(void)
{
    return valve_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

bool ble_valve_link_verified(void)
{
    uint16_t h = valve_conn_handle;
    return h != BLE_HS_CONN_HANDLE_NONE && ble_gap_conn_find(h, NULL) == 0;
}

// The executor (its task): a claim's connect still in flight 2 s past its pulse, so NimBLE neither
// completed nor cancelled it (no answer to its Create Connection Cancel). As for a connect NimBLE
// lost (connect_lost_by_host()): end the claim, no failure, and reset the BLE host, the only public
// way to clear the controller's initiator. Should never print (HANDOFF 15s residual 3).
void ble_valve_claim_overrun(void)
{
    connect_lost_by_host("still in flight 2 s past its pulse");
}

bool ble_valve_lr_pending(void)
{
    return s_lr_trigger;
}

void ble_valve_cancel_pending_close(void)
{
    // Under s_mac_lock like every other pending-slot update. A replay re-checks the slot
    // right before each write (apply_pending_cmd(), write_cmd_with_retry()), so it sees the
    // cancel and does not write the close.
    bool valve = false;
    bool rmleak = false;
    taskENTER_CRITICAL(&s_mac_lock);
    if (g_pending_valve_cmd == 0) {
        g_pending_valve_cmd = -1;
        valve = true;
    }
    if (g_pending_rmleak_cmd == 1) {
        g_pending_rmleak_cmd = -1;
        rmleak = true;
    }
    taskEXIT_CRITICAL(&s_mac_lock);
    if (valve)
        ESP_LOGI(BLE_TAG, "[CMD] Pending valve CLOSE cancelled (leak resolved)");
    if (rmleak)
        ESP_LOGI(BLE_TAG, "[CMD] Pending RMLEAK SET cancelled (leak resolved)");
}

bool ble_valve_get_firmware_rev(char *buffer, size_t len)
{
    if (buffer == NULL || len == 0)
        return false;
    if (g_firmware_rev[0] == '\0')
    {
        buffer[0] = '\0';
        return false;
    }
    strncpy(buffer, g_firmware_rev, len - 1);
    buffer[len - 1] = '\0';
    return true;
}
