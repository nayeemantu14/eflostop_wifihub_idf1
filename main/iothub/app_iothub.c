#include "app_iothub.h"
#include <stdio.h>
#include <stdlib.h>    // malloc/free — inbound fragment reassembly
#include <string.h>
#include <strings.h>   // strcasecmp — decommission targets match case-insensitively
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "esp_mac.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"

#include "app_lora/app_lora.h"
#include "ble_valve/app_ble_valve.h"
#include "ble_leak_scanner/app_ble_leak.h"
#include "provisioning_manager/provisioning_manager.h"
#include "rules_engine/rules_engine.h"
#include "health_engine/health_engine.h"
#include "sensor_meta/sensor_meta.h"
#include "telemetry/telemetry_v2.h"
#include "commands/c2d_commands.h"
#include "offline_buffer/offline_buffer.h"
#include "dps_client/dps_client.h"
#include "hub_identity/hub_identity.h"
#include "net_status/net_status.h"

// External Queue from LoRa app
extern QueueHandle_t lora_rx_queue;

TaskHandle_t iothub_task_handle = NULL;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool g_iot_hub_connected = false;
// True while the esp-mqtt client task is started; gates the stop/resume of the cloud admission.
static bool g_mqtt_running = false;

// DPS-assigned credentials (populated at boot)
static char g_hub_hostname[128] = {0};
static char g_device_id[64] = {0};
static char g_device_key[64] = {0};

// ---------------------------------------------------------------------------
// SAS token lifecycle
//
// The IoT Hub password is a SAS token whose expiry is baked in at mint time from
// the wall clock. Two things follow, and both used to be unhandled:
//   1. Minting before SNTP has synced yields se=<1970+ttl>, which IoT Hub rejects
//      with a 401 forever — esp-mqtt keeps retrying the same dead credential.
//      SNTP can legitimately still be unsynced here: nothing waits for it any more
//      (net_maintain() polls it from the loop), and on every boot after the first,
//      DPS returns straight from its NVS cache.
//   2. A token always expires eventually; when it does, the hub drops off and
//      reconnects with the same expired credential until someone power-cycles it.
// So: never start the client without a valid clock, and re-mint well before expiry.
// ---------------------------------------------------------------------------
#define SAS_TTL_SEC           (24 * 3600)  // token lifetime
#define SAS_RENEW_MARGIN_SEC  (6 * 3600)   // re-mint once less than this remains (~18 h)

static char   s_resource_uri[256]  = {0};
static char   s_mqtt_uri[192]      = {0};
static char   s_mqtt_username[256] = {0};
// Absolute expiry of the token currently held by the client. 0 = we have never
// minted one against a valid clock, so the client must not be started yet.
static volatile time_t s_sas_expiry     = 0;
// True while the cloud is not admitted (cloud_admission(), below): from boot until the
// first admission, and from every link loss or SoftAP start until the next. Nothing
// starts the client while it is set - not the admission's own resume, not cloud_bringup(),
// not sas_refresh() - and sas_maintain() does not even mint (2.1.4 WP2, plan I4).
static volatile bool   s_mqtt_suspended = true;
// Serialises client start/stop/reconfigure: mqtt_stop_for()/mqtt_resume() and
// sas_refresh() run on iothub_task, esp-mqtt's own events on its task; the mutex keeps a
// stop/set_config/start sequence whole so g_mqtt_running never lies.
// Statically allocated so creation cannot fail — the alternative (bailing out of
// iothub_task) would silently take leak evaluation and rules_engine_tick with it.
// Initialised before mqtt_client, so a non-NULL mqtt_client implies a live mutex.
static StaticSemaphore_t s_mqtt_ctl_mutex_buf;
static SemaphoreHandle_t s_mqtt_ctl_mutex = NULL;

// Cloud bring-up state. Neither Wi-Fi, SNTP nor DPS registration may block iothub_task
// before its event loop: this task is the sole caller of rules_engine_tick() and
// rules_engine_evaluate_leak(), so waiting on any of them disabled leak auto-close (N1).
// All three are driven from inside the loop (net_maintain(), dps_maintain()): the first
// DPS_BOOT_ATTEMPTS registrations back off 5..30 s, later ones every
// DPS_RETRY_INTERVAL_MS.
#define DPS_BOOT_ATTEMPTS     6
#define DPS_RETRY_INTERVAL_MS (5 * 60 * 1000)

static bool    g_cloud_ready      = false;   // DPS assigned + MQTT client built
static int64_t s_dps_next_try_ms  = 0;
static int     s_dps_attempts     = 0;       // registrations tried so far (dps_maintain)

// Wi-Fi STA has an IP. Set by iothub_on_wifi_connected() and cleared by
// iothub_on_wifi_lost() (both on the wifi_manager task, flags only); read by iothub_task.
static volatile bool s_wifi_up = false;
// Link losses since boot, counted by iothub_on_wifi_lost() (its only writer): iothub_task
// stops MQTT for a loss even when the link is back by its next pass.
static volatile uint32_t s_wifi_losses = 0;

// SNTP bring-up, iothub_task only (net_maintain()).
static bool    s_sntp_started   = false;   // esp_sntp_init() done (first Wi-Fi IP)
static bool    s_sntp_fallback  = false;   // initial sync timed out; 60 s re-poll timer armed
static bool    s_time_ok        = false;   // wall clock valid (>= SNTP_EPOCH_VALID)
static int64_t s_sntp_start_ms  = 0;       // monotonic ms of esp_sntp_init()

// Lifecycle flag: set in MQTT_EVENT_CONNECTED, consumed in event loop
static bool g_needs_lifecycle = false;

// The next replay of events kept in the offline buffer while connected (monotonic ms;
// iothub_task only; see REPLAY_RETRY_MS).
static int64_t s_replay_retry_ms = 0;

// The lifecycle of this connection has not reached esp-mqtt yet (refused for room): sent
// again while connected, every LIFECYCLE_RETRY_MS, until it does. iothub_task only.
#define LIFECYCLE_RETRY_MS 5000
static bool    s_lifecycle_owed     = false;
static int64_t s_lifecycle_retry_ms = 0;

// Full-decommission reboot: set by handle_c2d_command ('decommission all', which
// runs in the esp-mqtt event task) and consumed by iothub_task, which publishes a
// final snapshot of the cleared state and reboots. The publish MUST happen in
// iothub_task (the single snapshot-flush context) so it doesn't race the caches.
static volatile bool g_decommission_reboot = false;

// Device-set change (D0): set by the C2D provision/decommission handlers on the esp-mqtt
// event task, AFTER the provisioning change succeeded; consumed at the top of the
// iothub_task loop by apply_device_set_change(). One task then runs the health reconcile,
// the cache and rules purges, the commission-snapshot arming and the twin in a defined
// order, ahead of the command-ack snapshot — so the removal snapshot can never show the
// removed device, and no scheduler flag (incl. the int64 g_commission_until_ms) is
// written from two tasks. 2.1.3 did all of that on the esp-mqtt task.
static volatile bool g_devset_changed = false;

// iothub_apply_provisioned_mac() found provisioning busy (at boot, or for a `provision` on
// the esp-mqtt task), so the BLE target and the BLE start are still owed. iothub_task
// retries at the top of every pass until one succeeds: a busy mutex must never leave BLE
// unstarted or the valve target stale. The retry can never re-target a valve that a C2D
// change removed or replaced meanwhile: it reads and sets the target in one provisioning
// mutex hold (see iothub_apply_provisioned_mac()).
static volatile bool s_ble_apply_owed = false;

// True while the health table holds no device (nothing provisioned, or a rules-only
// provision). iothub_task ONLY: seeded after health_engine_init() and updated by
// apply_device_set_change(), whose false->true EDGE runs on_hub_emptied(). Also keeps
// the 2 s commission poll off on an empty hub, which has nothing to sync.
static bool s_hub_empty = false;

// An empty-hub rules-engine reset (boot, or the C2D change that emptied the hub) could not
// take the rules mutex, so the RAM latch/override may be stale. iothub_task retries it in
// on_hub_emptied() and on the next empty -> non-empty edge in apply_device_set_change().
// Written by the esp-mqtt task (reset_rules_state_hub_emptied()) and by iothub_task; each
// write is the outcome of a complete reset, so the last one written is never a lost reset.
static volatile bool s_rules_reset_owed = false;

// Sensor-meta rename: set by handle_c2d_command (esp-mqtt event task) on a
// successful standalone `sensor_meta` command; consumed by iothub_task, which
// converts it into an event snapshot so the app reflects the new label/location
// promptly instead of at the next heartbeat. Cross-task-safe: a flag + wake, with
// the snapshot request issued only from iothub_task (the single flush context).
// A successfully-handled C2D command asks iothub_task for a snapshot, labelled with
// the command name so the UART trace says WHICH command caused it. Written on the
// esp-mqtt event task, consumed by iothub_task — the tolerant cross-task flag pattern
// used elsewhere in this file. The label is filled before the flag is set; two
// commands arriving back-to-back are handled sequentially on the one task, so the
// worst case is a single snapshot carrying the newer command's label while covering
// both. That is accurate enough to be useful and never wrong about the state it
// reports. Buffer matches c2d_command_t.cmd[32], so the name is never truncated.
static volatile bool g_cmd_snap_pending = false;
static char          g_cmd_snap_label[32] = {0};

// Sync snapshot: published once after the health-engine sync window completes
// (all known devices heard, else the 2-min / 120 s timeout). Re-armed (set false)
// on MQTT (re)connect AND on a `provision` (commission) command, so the first
// post-commission snapshot lands within 120 s instead of waiting for the 5-min timer.
static bool g_boot_snapshot_sent = false;

// Incremental commission snapshot: after the initial sync snapshot, a far/slow
// sensor (dry WBA leak sensor advertises on a ~100 s cadence) may not be heard
// until after the window closed. To avoid "sensor data never arrives until the
// 5-min periodic", we publish a refreshed snapshot each time the count of heard
// devices increases, for a bounded grace after a commission. g_commission_until_ms
// is the grace deadline (esp_timer ms); g_commission_pub_seen is the seen-count
// already reflected in the last published commission snapshot.
#define COMMISSION_REFRESH_GRACE_MS (6 * 60 * 1000)   // 6 min after a provision
static int64_t g_commission_until_ms = 0;
static uint8_t g_commission_pub_seen = 0;

// ---------------------------------------------------------------------------
// Post-provision snapshot PULSE
//
// The incremental refresh above fires only on a device's FIRST contact (`seen >
// g_commission_pub_seen`), and its window is deliberately collapsed the moment every
// device has been heard. Good enough to assemble one complete snapshot; not good
// enough for an installer standing next to the hardware, who needs the battery/RSSI
// numbers to keep refreshing while they move a sensor around. A dry WBA leak sensor
// beacons about every 100 s, so its 2nd and later packets used to produce nothing at
// all until the 5-min heartbeat.
//
// So: for a bounded window after a `provision`, publish on a 30 s cadence AND every
// time a sensor is heard. Kept on its own time-based window rather than reusing
// g_commission_until_ms precisely because that one is zeroed at seen>=total — sharing
// it would end the pulse early and fight the incremental-refresh design.
//
// Deadlines are owned by iothub_task. arm_commission_snapshot() (iothub_task, via
// apply_device_set_change) only sets the g_prov_pulse_arm FLAG; the arithmetic happens in
// the consume block later in the same loop iteration.
// ---------------------------------------------------------------------------
#define PROV_PULSE_WINDOW_MS   (5 * 60 * 1000)   // elevated cadence lasts 5 min
#define PROV_PULSE_PERIOD_MS   (30 * 1000)       // periodic pulse inside the window
#define PROV_PULSE_MAX_SNAPS   40                // safety valve against a pathological
                                                 // flapping sensor; LOGS when it bites,
                                                 // never truncates silently
static volatile bool g_prov_pulse_arm      = false;
static int64_t       g_prov_pulse_until_ms = 0;   // 0 = window closed
static int64_t       g_prov_pulse_next_ms  = 0;   // next periodic pulse deadline
static uint32_t      g_prov_pulse_seq      = 0;   // check-in seq already reflected in a request
static uint16_t      g_prov_pulse_count    = 0;   // snapshots requested this window

// ---------------------------------------------------------------------------
// Unified snapshot scheduler (event-coupled snapshots + heartbeat suppression)
//
// All state below is owned by iothub_task ONLY (read/written inside the event
// loop) and is therefore lock-free. The deadline is a MONOTONIC esp_timer value
// (immune to SNTP wall-clock steps). EVERY snapshot — heartbeat, event-coupled,
// or commission/boot — flows through the single flush block at the end of the
// loop, which re-arms the heartbeat ONLY on a snapshot that actually reached
// esp-mqtt (so an offline/pre-SNTP/outbox drop never suppresses the heartbeat).
// ---------------------------------------------------------------------------
typedef enum {
    SNAP_HEARTBEAT = 0,   // periodic ~5-min heartbeat
    SNAP_EVENT,           // coupled to a state-changing event (tiered window)
    SNAP_COMMISSION,      // incremental commission/boot refresh (urgent, no clamp)
    SNAP_BOOT,            // complete commission-sync snapshot (all-heard or timeout)
    SNAP_FAST,            // EARLY boot/reconnect snapshot fired at valve-ready (UI ASAP)
} snap_reason_t;

typedef enum { SNAP_TIER_LOW = 0, SNAP_TIER_HIGH = 1 } snap_tier_t;

#define SNAP_HIGH_WINDOW_MS    300     // safety-critical burst coalescing window
#define SNAP_LOW_WINDOW_MS     2000    // low-priority coalescing window
#define SNAP_MIN_INTERVAL_MS   5000    // min spacing between EVENT/HEARTBEAT snapshots (<=12/min)
#define SNAP_OFFLINE_FLOOR_MS  30000   // loop idle cap while offline with a due deadline
#define SNAP_RETRY_FLOOR_MS    5000    // retry spacing when connected but a publish failed (no tight-spin)
#define SNAP_FAST_CEILING_MS   150000  // fire the fast boot snapshot by 150 s even if the valve never connects

static int64_t       s_snap_due_ms       = 0;                   // monotonic ms; next snapshot deadline
static int64_t       s_snap_last_pub_ms  = 0;                   // monotonic ms of last CONFIRMED publish
static int64_t       s_snap_retry_until_ms = 0;                 // monotonic ms; publish-fail backoff floor (0 = none)
static snap_reason_t s_snap_reason       = SNAP_HEARTBEAT;
static snap_tier_t   s_snap_tier         = SNAP_TIER_LOW;
static char          s_snap_evt[32]      = {0};                 // event name (log + observability)
static int64_t       s_hb_interval_ms    = SNAPSHOT_INTERVAL_MS;// latched from Twin each iteration
static bool          g_fast_snapshot_sent = false;             // one-shot fast boot/reconnect snapshot (reset only on lifecycle)
static int64_t       g_fast_arm_ms        = 0;                  // monotonic ms when the fast snapshot was (re)armed; ceiling is relative to THIS
static uint32_t      s_rating_seq_seen    = 0;                  // health_get_rating_seq() already requested/published (see Phase 3)

// Delta-gate for valve_state_changed (was emitted on every BLE_UPD_STATE notify).
// -2 sentinel = nothing published yet; valve states are 1=open / 0=closed / -1=unknown.
static int s_valve_pub_state = -2;
/* Last valve flood state published as a leak event, as the cloud understands it.
 *
 * Gates the at-link-up flood announcement from app_ble_valve.c, which now fires on every
 * link-up for BOTH wet and dry. Initialised to 0 (dry), NOT -1: a routine dry link-up is
 * the overwhelmingly common case and must publish nothing, while a wet one (1 != 0) is
 * reported. Deliberately NOT reset on reconnect — that is exactly what stops a valve
 * flapping while wet from republishing leak_detected each time. */
static int s_valve_pub_wet = 0;

// Delta-gate for the valve LINK edge, same sentinel convention (-1 = nothing
// published yet, 1 = linked, 0 = unlinked). Holds the last PUBLISHED link state,
// updated only when a snapshot is actually requested, so a valve flapping at
// supervision-timeout cadence produces one snapshot per real transition rather
// than one per GAP teardown.
//
// Why this exists at all: before it, the disconnect edge produced NO cloud
// message whatsoever. The valve-event block below is gated on vlk_mac_ok, and
// that flag is derived from ble_valve_get_mac(), which app_ble_valve.c zeroes on
// disconnect — so the gate is structurally false exactly when the link drops. The
// first the cloud heard of an unreachable valve was the CRITICAL health alert
// ~245 s later, or the next heartbeat. The link edge is therefore published from
// OUTSIDE that gate.
//
// Note the live MAC is deliberately NOT needed here: the snapshot builder always
// takes valve_id from the health table's (provisioned) dev_id (telemetry_v2.c, the
// valve block), so the identity on the wire is correct without it.
static int s_valve_pub_linked = -1;

// The provisioned valve MAC the three detectors above currently describe ("" = no
// valve). iothub_task only; see sync_valve_detectors().
static char s_det_valve_mac[18] = {0};

// Device Twin: request ID counter for twin GET/PATCH operations. Only ever advanced
// through next_twin_rid(): the esp-mqtt task (twin GET, reported echoes of C2D and
// desired patches) and iothub_task (connect, device-set reconcile) both take ids, and a
// plain ++ from the two could hand out the same $rid twice.
static int g_twin_rid = 0;

static int next_twin_rid(void)
{
    return __atomic_add_fetch(&g_twin_rid, 1, __ATOMIC_RELAXED);
}

// msg_id of the in-flight "$iothub/twin/res/#" SUBSCRIBE, or -1 when there is
// none. The twin GET is deferred until the matching SUBACK so the response cannot
// arrive before the broker has us on the response topic. Reset on every connect,
// cleared once the GET is sent, so exactly one GET is issued per connection.
static int g_twin_res_sub_id = -1;

// ---------------------------------------------------------------------------
// Telemetry v2 caches (shared with telemetry module for snapshot reads)
// ---------------------------------------------------------------------------

static telem_lora_cache_t     g_telem_lora_cache[TELEM_MAX_LORA_CACHE] = {0};
static telem_ble_leak_cache_t g_telem_ble_cache[TELEM_MAX_BLE_LEAK_CACHE] = {0};

// ---------------------------------------------------------------------------
// Legacy cache types (deprecated — kept for build_*_delta_json() below)
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t battery;
    bool leak_state;
    int valve_state;
    bool rmleak;
    bool valid;
} valve_cache_t;

static valve_cache_t g_last_valve = {0};

// ---------------------------------------------------------------------------
// SAS token helpers (unchanged)
// ---------------------------------------------------------------------------

void url_encode(const char *src, char *dst, size_t dst_len)
{
    char *end = dst + dst_len - 1;
    while (*src && dst < end)
    {
        if ((*src >= 'A' && *src <= 'Z') || (*src >= 'a' && *src <= 'z') ||
            (*src >= '0' && *src <= '9') || *src == '-' || *src == '.' || *src == '_' || *src == '~')
        {
            *dst++ = *src;
        }
        else
        {
            if (dst + 3 > end)
                break;
            dst += sprintf(dst, "%%%02X", (unsigned char)*src);
        }
        src++;
    }
    *dst = '\0';
}

char *generate_sas_token(const char *resource_uri, const char *key, long expiry_seconds)
{
    char expiry_str[24];
    time_t now;
    time(&now);
    // 64-bit: time_t/long are 32-bit here, so a 32-bit expiry goes negative after
    // 2038-01-18 and Azure rejects the token. The wire format is plain seconds.
    int64_t expiry = (int64_t)now + (int64_t)expiry_seconds;
    snprintf(expiry_str, sizeof(expiry_str), "%lld", (long long)expiry);

    char encoded_uri[128];
    url_encode(resource_uri, encoded_uri, sizeof(encoded_uri));
    char string_to_sign[256];
    snprintf(string_to_sign, sizeof(string_to_sign), "%s\n%s", encoded_uri, expiry_str);
    unsigned char decoded_key[64];
    size_t decoded_key_len = 0;
    mbedtls_base64_decode(decoded_key, sizeof(decoded_key), &decoded_key_len, (const unsigned char *)key, strlen(key));
    unsigned char hmac[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, decoded_key, decoded_key_len);
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)string_to_sign, strlen(string_to_sign));
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);
    unsigned char signature_b64[64];
    size_t signature_b64_len = 0;
    mbedtls_base64_encode(signature_b64, sizeof(signature_b64), &signature_b64_len, hmac, 32);
    signature_b64[signature_b64_len] = '\0';
    char encoded_signature[128];
    url_encode((char *)signature_b64, encoded_signature, sizeof(encoded_signature));
    char *sas_token = (char *)malloc(512);
    if (!sas_token) return NULL;   // callers check; without this we'd fault in snprintf
    snprintf(sas_token, 512, "SharedAccessSignature sr=%s&sig=%s&se=%s", encoded_uri, encoded_signature, expiry_str);
    return sas_token;
}

// ---------------------------------------------------------------------------
// v2 change detection helpers
// ---------------------------------------------------------------------------

/**
 * Update the LoRa telem cache and return true if leak_status changed.
 * Always updates all cached fields (battery, rssi, snr) for snapshot use.
 */
static bool update_lora_cache_check_leak(const lora_packet_t *pkt)
{
    // Search for existing entry
    for (int i = 0; i < TELEM_MAX_LORA_CACHE; i++) {
        if (g_telem_lora_cache[i].valid &&
            g_telem_lora_cache[i].sensor_id == pkt->sensorId) {
            bool leak_changed =
                (g_telem_lora_cache[i].leak_status != pkt->leakStatus);
            g_telem_lora_cache[i].battery     = pkt->batteryPercentage;
            g_telem_lora_cache[i].leak_status = pkt->leakStatus;
            g_telem_lora_cache[i].rssi        = pkt->rssi;
            g_telem_lora_cache[i].snr         = pkt->snr;
            return leak_changed;
        }
    }

    // Not in cache — find empty slot
    for (int i = 0; i < TELEM_MAX_LORA_CACHE; i++) {
        if (!g_telem_lora_cache[i].valid) {
            g_telem_lora_cache[i].sensor_id   = pkt->sensorId;
            g_telem_lora_cache[i].battery     = pkt->batteryPercentage;
            g_telem_lora_cache[i].leak_status = pkt->leakStatus;
            g_telem_lora_cache[i].rssi        = pkt->rssi;
            g_telem_lora_cache[i].snr         = pkt->snr;
            g_telem_lora_cache[i].valid       = true;
            return (pkt->leakStatus != 0);  // First time: only emit if actively leaking
        }
    }

    // Cache full — overwrite slot 0
    g_telem_lora_cache[0].sensor_id   = pkt->sensorId;
    g_telem_lora_cache[0].battery     = pkt->batteryPercentage;
    g_telem_lora_cache[0].leak_status = pkt->leakStatus;
    g_telem_lora_cache[0].rssi        = pkt->rssi;
    g_telem_lora_cache[0].snr         = pkt->snr;
    g_telem_lora_cache[0].valid       = true;
    return (pkt->leakStatus != 0);
}

/**
 * Update the BLE leak telem cache and return true if leak_state changed.
 * Always updates all cached fields for snapshot use.
 */
static bool update_ble_leak_cache_check_leak(const ble_leak_event_t *evt)
{
    for (int i = 0; i < TELEM_MAX_BLE_LEAK_CACHE; i++) {
        if (g_telem_ble_cache[i].valid &&
            strcasecmp(g_telem_ble_cache[i].mac_str, evt->sensor_mac_str) == 0) {
            bool leak_changed =
                (g_telem_ble_cache[i].leak_state != evt->leak_detected);
            g_telem_ble_cache[i].battery    = evt->battery;
            g_telem_ble_cache[i].leak_state = evt->leak_detected;
            g_telem_ble_cache[i].rssi       = evt->rssi;
            strncpy(g_telem_ble_cache[i].fw_version, evt->fw_version,
                    sizeof(g_telem_ble_cache[i].fw_version) - 1);
            g_telem_ble_cache[i].fw_version[sizeof(g_telem_ble_cache[i].fw_version) - 1] = '\0';
            return leak_changed;
        }
    }

    // Not in cache — find empty slot
    for (int i = 0; i < TELEM_MAX_BLE_LEAK_CACHE; i++) {
        if (!g_telem_ble_cache[i].valid) {
            strncpy(g_telem_ble_cache[i].mac_str, evt->sensor_mac_str, 17);
            g_telem_ble_cache[i].mac_str[17] = '\0';
            g_telem_ble_cache[i].battery    = evt->battery;
            g_telem_ble_cache[i].leak_state = evt->leak_detected;
            g_telem_ble_cache[i].rssi       = evt->rssi;
            strncpy(g_telem_ble_cache[i].fw_version, evt->fw_version,
                    sizeof(g_telem_ble_cache[i].fw_version) - 1);
            g_telem_ble_cache[i].fw_version[sizeof(g_telem_ble_cache[i].fw_version) - 1] = '\0';
            g_telem_ble_cache[i].valid      = true;
            return evt->leak_detected;  // First time: only emit if actively leaking
        }
    }

    // Cache full — overwrite slot 0
    strncpy(g_telem_ble_cache[0].mac_str, evt->sensor_mac_str, 17);
    g_telem_ble_cache[0].mac_str[17] = '\0';
    g_telem_ble_cache[0].battery    = evt->battery;
    g_telem_ble_cache[0].leak_state = evt->leak_detected;
    g_telem_ble_cache[0].rssi       = evt->rssi;
    strncpy(g_telem_ble_cache[0].fw_version, evt->fw_version,
            sizeof(g_telem_ble_cache[0].fw_version) - 1);
    g_telem_ble_cache[0].fw_version[sizeof(g_telem_ble_cache[0].fw_version) - 1] = '\0';
    g_telem_ble_cache[0].valid      = true;
    return evt->leak_detected;
}

// ---------------------------------------------------------------------------
// Legacy helpers (deprecated — kept until v1 telemetry fully removed)
// ---------------------------------------------------------------------------

// (DEPRECATED) Check if LoRa packet is a duplicate
__attribute__((unused))
static bool is_lora_duplicate(const lora_packet_t *pkt)
{
    (void)pkt;
    return true; // Always suppress — v2 uses update_lora_cache_check_leak()
}

// (DEPRECATED) Check if valve data changed
__attribute__((unused))
static bool valve_data_changed(void)
{
    uint8_t batt = ble_valve_get_battery();
    bool leak = ble_valve_get_leak();
    int state = ble_valve_get_state();
    bool rmleak = ble_valve_get_rmleak_state();

    if (!g_last_valve.valid) {
        g_last_valve.battery = batt;
        g_last_valve.leak_state = leak;
        g_last_valve.valve_state = state;
        g_last_valve.rmleak = rmleak;
        g_last_valve.valid = true;
        return true;
    }

    if (g_last_valve.battery != batt ||
        g_last_valve.leak_state != leak ||
        g_last_valve.valve_state != state ||
        g_last_valve.rmleak != rmleak) {
        g_last_valve.battery = batt;
        g_last_valve.leak_state = leak;
        g_last_valve.valve_state = state;
        g_last_valve.rmleak = rmleak;
        return true;
    }

    return false;
}

// (DEPRECATED) Build valve delta JSON
__attribute__((unused))
static char *build_valve_delta_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "gatewayID", hub_identity_get_gateway_id());

    cJSON *devicesArr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "devices", devicesArr);

    cJSON *deviceObj = cJSON_CreateObject();
    cJSON_AddItemToArray(devicesArr, deviceObj);

    cJSON *valveObj = cJSON_CreateObject();
    cJSON_AddItemToObject(deviceObj, "valve", valveObj);

    char valve_mac[18];
    if (ble_valve_get_mac(valve_mac))
    {
        cJSON_AddStringToObject(valveObj, "valve_mac", valve_mac);
        cJSON_AddNumberToObject(valveObj, "battery", ble_valve_get_battery());
        cJSON_AddBoolToObject(valveObj, "leak_state", ble_valve_get_leak());
        cJSON_AddBoolToObject(valveObj, "rmleak", ble_valve_get_rmleak_state());

        int state = ble_valve_get_state();
        if (state == 1)
            cJSON_AddStringToObject(valveObj, "valve_state", "open");
        else if (state == 0)
            cJSON_AddStringToObject(valveObj, "valve_state", "closed");
        else
            cJSON_AddStringToObject(valveObj, "valve_state", "unknown");
    }
    else
    {
        cJSON_AddNullToObject(valveObj, "valve_mac");
        cJSON_AddStringToObject(valveObj, "valve_state", "disconnected");
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// (DEPRECATED) Build LoRa delta JSON
__attribute__((unused))
static char *build_lora_delta_json(const lora_packet_t *pkt)
{
    if (!pkt) return NULL;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "gatewayID", hub_identity_get_gateway_id());

    cJSON *devicesArr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "devices", devicesArr);

    cJSON *deviceObj = cJSON_CreateObject();
    cJSON_AddItemToArray(devicesArr, deviceObj);

    cJSON *sensorsObj = cJSON_CreateObject();
    cJSON_AddItemToObject(deviceObj, "leak_sensors", sensorsObj);

    char sensorKey[32];
    snprintf(sensorKey, sizeof(sensorKey), "sensor_0x%08lX", pkt->sensorId);

    cJSON *thisSensor = cJSON_CreateObject();
    cJSON_AddItemToObject(sensorsObj, sensorKey, thisSensor);

    cJSON_AddNumberToObject(thisSensor, "battery", pkt->batteryPercentage);
    cJSON_AddBoolToObject(thisSensor, "leak_state", (pkt->leakStatus != 0));
    cJSON_AddNumberToObject(thisSensor, "rssi", pkt->rssi);

    char lora_id[16];
    snprintf(lora_id, sizeof(lora_id), "0x%08lX", pkt->sensorId);
    sensor_meta_entry_t meta;   // a copy, never a pointer into the table (L16)
    bool have_meta = sensor_meta_get(SENSOR_TYPE_LORA, lora_id, &meta);
    cJSON *locObj = cJSON_CreateObject();
    cJSON_AddStringToObject(locObj, "code",
        sensor_meta_location_code_to_str(have_meta ? meta.location_code : LOC_UNKNOWN));
    cJSON_AddStringToObject(locObj, "label", have_meta ? meta.label : "");
    cJSON_AddItemToObject(thisSensor, "location", locObj);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// (DEPRECATED) Build BLE leak sensor delta JSON
__attribute__((unused))
static char *build_ble_leak_delta_json(const ble_leak_event_t *evt)
{
    if (!evt) return NULL;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "gatewayID", hub_identity_get_gateway_id());

    cJSON *devicesArr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "devices", devicesArr);

    cJSON *deviceObj = cJSON_CreateObject();
    cJSON_AddItemToArray(devicesArr, deviceObj);

    cJSON *sensorsObj = cJSON_CreateObject();
    cJSON_AddItemToObject(deviceObj, "ble_leak_sensors", sensorsObj);

    char sensorKey[32];
    snprintf(sensorKey, sizeof(sensorKey), "BLE_%s", evt->sensor_mac_str);

    cJSON *thisSensor = cJSON_CreateObject();
    cJSON_AddItemToObject(sensorsObj, sensorKey, thisSensor);

    cJSON_AddNumberToObject(thisSensor, "battery", evt->battery);
    cJSON_AddBoolToObject(thisSensor, "leak_state", evt->leak_detected);
    cJSON_AddNumberToObject(thisSensor, "rssi", evt->rssi);

    sensor_meta_entry_t meta;   // a copy, never a pointer into the table (L16)
    bool have_meta = sensor_meta_get(SENSOR_TYPE_BLE_LEAK, evt->sensor_mac_str, &meta);
    cJSON *locObj = cJSON_CreateObject();
    cJSON_AddStringToObject(locObj, "code",
        sensor_meta_location_code_to_str(have_meta ? meta.location_code : LOC_UNKNOWN));
    cJSON_AddStringToObject(locObj, "label", have_meta ? meta.label : "");
    cJSON_AddItemToObject(thisSensor, "location", locObj);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// ---------------------------------------------------------------------------
// C2D command dispatch (uses c2d_commands parser)
// ---------------------------------------------------------------------------

static void publish_twin_reported(void);   // forward declaration
static void mark_mqtt_disconnected(void);  // forward declaration (used by mqtt_stop_for)

// Reject an "open the valve" request while the valve's RMLEAK latch is asserted
// (valve locked after an auto-close). Forwarding the open anyway lets the valve
// briefly honor it before its own RMLEAK interlock re-closes it — a sub-second
// water-on transient during a live leak. Clear the latch first with leak_reset,
// or open during a leak via override_enable (which clears RMLEAK as part of the
// guarded 24h window and so does not go through this handler). Returns the
// cmd_ack error detail when the open must be refused, or NULL when it may proceed.
//
// A latched leak incident with no override window refuses it too, with the SAME message,
// whether or not the valve is linked (E-06): the disconnect clears the RMLEAK cache, so an
// open sent while the valve was out of range was accepted, pended, and written at the
// reconnect ahead of the close the leak was owed. The rules getters take only the rules
// mutex, and nothing else is held here (esp-mqtt task).
static const char *valve_open_reject_reason(void)
{
    // The latch is read through the health engine's lock-free mirror as well as the rules
    // getter: the getter returns false when the rules mutex is busy for 1 s, which would
    // let an open through during a latched incident. A busy override getter likewise
    // reads "no override", so a busy rules engine refuses rather than allows.
    bool latched = health_is_interlock_held() || rules_engine_is_leak_incident_active();
    if (ble_valve_get_rmleak_state() ||
        (latched && !rules_engine_is_override_window_active())) {
        return "Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, "
               "or use override to open the valve during a leak.";
    }
    // The valve (FW 2.2.0) refuses to open at <=10 % and GATT writes have no completion
    // callback, so forwarding the open got an `ok` ack for a valve that stayed shut. The
    // last REAL reading (kept across reconnects) comes from the health table.
    if (health_is_valve_battery_critical()) {
        return "Valve battery critical (≤10 %): the valve will not open. Replace the batteries.";
    }
    return NULL;
}

// valve_open / valve_close / valve_set_state. Returns the cmd_ack error, or NULL once the
// command is queued. No provisioned valve is refused outright (P0-a): the valve module
// would refuse it anyway, and "ok" for a command that went nowhere is what N9 removes.
// A queued command is still only QUEUED — the valve's own report confirms it.
static const char *c2d_valve_command(bool open)
{
    if (!ble_valve_has_target_mac()) {
        return "No valve is set up for this hub.";
    }
    if (open) {
        const char *why = valve_open_reject_reason();
        if (why) return why;
    }
    ble_valve_connect();
    bool queued = open ? ble_valve_open() : ble_valve_close();
    return queued ? NULL : "The valve command could not be queued. Try again.";
}

// Arm the commission snapshot after devices were ADDED: re-arm the one-shot initial
// snapshot, reset the published seen-count, and open the incremental-refresh grace window
// so a device heard after the initial snapshot still gets reported promptly (not only at
// the next 5-min periodic).
// `pulse` = also run the 30 s post-provision snapshot pulse.
// Removals never arm anything: the generic on-success command snapshot is their single
// owner, and an already-open window keeps running unchanged (BUG-2/3).
static void arm_commission_snapshot(bool pulse)
{
    // NOTE: runs on iothub_task ONLY (apply_device_set_change). It used to run on the
    // esp-mqtt event task and race this task on g_boot_snapshot_sent and the 64-bit
    // g_commission_until_ms; that cross-task write is gone. It still MUST NOT call
    // snap_request(): the BOOT/COMMISSION arming further down the loop derives the
    // deadline from these flags, and g_prov_pulse_arm is consumed by the pulse block
    // later in this same iteration.
    g_boot_snapshot_sent  = false;
    g_commission_pub_seen = 0;
    g_commission_until_ms = (esp_timer_get_time() / 1000) + COMMISSION_REFRESH_GRACE_MS;
    if (pulse) g_prov_pulse_arm = true;
}

// Invalidate every telemetry-cache entry whose device is no longer provisioned (L9).
// The leak-event delta gate lives in these caches, so a sensor removed while wet and
// re-added while still wet compared "wet == wet" against its stale entry and never
// emitted leak_detected again. iothub_task only (the caches' single writer).
static void purge_telemetry_caches(void)
{
    prov_device_set_t set;
    if (!provisioning_get_device_set(&set)) {
        // Unknown != "nothing provisioned". Owed like the rules purge and the detector sync:
        // a skipped purge would keep a removed wet sensor's entry, and its re-add would then
        // never emit leak_detected (S6). Re-running the whole change is harmless.
        ESP_LOGW(IOTHUB_TAG, "Telemetry-cache purge: provisioning busy, retrying");
        g_devset_changed = true;
        return;
    }

    int lora_purged = 0, ble_purged = 0;

    for (int i = 0; i < TELEM_MAX_LORA_CACHE; i++) {
        if (!g_telem_lora_cache[i].valid) continue;
        bool keep = false;
        for (int k = 0; k < set.lora_count; k++) {
            if (set.lora_ids[k] == g_telem_lora_cache[i].sensor_id) { keep = true; break; }
        }
        if (!keep) {
            memset(&g_telem_lora_cache[i], 0, sizeof(g_telem_lora_cache[i]));
            lora_purged++;
        }
    }

    for (int i = 0; i < TELEM_MAX_BLE_LEAK_CACHE; i++) {
        if (!g_telem_ble_cache[i].valid) continue;
        bool keep = false;
        for (int k = 0; k < set.ble_count; k++) {
            if (strcasecmp(set.ble_macs[k], g_telem_ble_cache[i].mac_str) == 0) { keep = true; break; }
        }
        if (!keep) {
            memset(&g_telem_ble_cache[i], 0, sizeof(g_telem_ble_cache[i]));
            ble_purged++;
        }
    }

    if (lora_purged > 0 || ble_purged > 0) {
        ESP_LOGI(IOTHUB_TAG, "Telemetry caches purged: %d LoRa, %d BLE (no longer provisioned)",
                 lora_purged, ble_purged);
    }
}

// The hub just became EMPTY (the last device was removed). iothub_task only.
//
// This is an EDGE, run once per transition — not the per-pass reset the old unprovisioned
// branch did, which cleared g_boot_snapshot_sent on every wake and so re-fired BOOT each
// time the loop ran. Marking boot/fast as sent here is what stops a BOOT/FAST storm on a
// hub with nothing to sync: the command-ack EVENT is the single owner of the transition
// snapshot. After a reboot or an MQTT (re)connect the lifecycle block clears
// g_boot_snapshot_sent again and, because an empty table is sync-complete, exactly one
// "boot" snapshot is published; heartbeats follow at the interval.
static void on_hub_emptied(void)
{
    ESP_LOGW(IOTHUB_TAG, "Hub is now EMPTY - no devices provisioned; heartbeat-only snapshots");

    // The rules are NOT reset here any more (E-05). This edge is decided from a read that is
    // not ordered against a C2D provision/rules_config landing on the esp-mqtt task right
    // after the removal, so a reset here could overwrite a just-acked opt-out, or be skipped
    // and let the old install's latch/override carry over. The removal (or the provision's
    // sensor arrays) that emptied the hub resets them itself: the config in its own
    // provisioning save, the rules-engine state synchronously on the esp-mqtt task
    // (reset_rules_state_hub_emptied()). Only a state reset that failed there, or the boot
    // fallback's, is retried here.
    if (s_rules_reset_owed) {
        s_rules_reset_owed = !rules_engine_reset_all();   // else retried when the hub next gains a device
        if (s_rules_reset_owed) {
            ESP_LOGE(IOTHUB_TAG, "Hub empty: rules-engine RAM reset failed - retry owed");
        }
    }

    // The transition to empty is an EVENT snapshot (Phase B). An urgent reason still pending
    // here (held by the retry floor or the settle gate) would otherwise publish it as
    // boot/commission/fast: marking boot/fast sent below does not touch s_snap_reason, and
    // the command-ack EVENT cannot displace it because snap_request() is pull-in-only. The
    // deadline is kept, so this changes the label, not the timing.
    if (s_snap_reason == SNAP_BOOT || s_snap_reason == SNAP_COMMISSION ||
        s_snap_reason == SNAP_FAST) {
        s_snap_reason = SNAP_EVENT;
        s_snap_tier   = SNAP_TIER_HIGH;
        snprintf(s_snap_evt, sizeof(s_snap_evt), "%s", "hub_emptied");
    }

    g_boot_snapshot_sent  = true;   // nothing to sync — see above
    g_fast_snapshot_sent  = true;
    g_commission_pub_seen = 0;
    g_commission_until_ms = 0;
    // An open post-provision pulse has nothing left to refresh.
    g_prov_pulse_arm      = false;
    g_prov_pulse_until_ms = 0;
    g_prov_pulse_next_ms  = 0;
    g_prov_pulse_count    = 0;
}

// A C2D removal or provision just emptied the hub (esp-mqtt task, after the provisioning
// change and the BLE target change). Nothing left to protect: drop the latch, the override,
// the active leaks and any pending close, so the next deployment starts clean (Q6; L12). The
// rules CONFIG was already put back to the defaults in that change's own save
// (provisioning_remove_*() or provisioning_handle_azure_payload_json()).
// Synchronous, as decommission-all does: esp-mqtt handles C2D one at a time, so this is done
// before any later provision or rules_config is handled (E-05). Takes only the rules mutex;
// no other lock is held here.
// Assigned both ways: a success also settles a reset still owed from an earlier failure,
// which would otherwise force an unneeded reset on the next empty -> non-empty edge.
static void reset_rules_state_hub_emptied(void)
{
    s_rules_reset_owed = !rules_engine_reset_all();   // retried by iothub_task
    if (s_rules_reset_owed) {
        ESP_LOGE(IOTHUB_TAG, "Hub empty: rules-engine RAM reset failed - retry owed");
    }
}

// Re-point the valve change detectors (s_valve_pub_linked / _wet / _state) when the
// provisioned valve changes or is removed (L10). iothub_task only.
//
// They hold the last value PUBLISHED for one particular valve, and nothing reset them:
//   - a removed valve's teardown DISCONNECTED then produced a second "valve_unlinked"
//     snapshot after the removal snapshot — presetting linked to 0 (no valve) absorbs it;
//   - a NEW valve inherited the old one's edges, so a new wet valve's leak_detected was
//     suppressed and a new dry one produced a phantom leak_cleared — presetting -1/0/-2
//     makes it publish its own first link, leak and state edges.
//
// Read through provisioning_get_device_set(), NOT provisioning_get_valve_mac(): the latter
// returns false both for "no valve" and for a busy provisioning mutex, and s_det_valve_mac
// also gates the valve link-edge snapshot, so one busy read used to blank it and silence
// every valve_linked/valve_unlinked until the next device-set change. Unknown now leaves
// the detectors alone and retries the whole (idempotent) change on the next pass.
//
// The same comparison tells the rules engine when a valve is REPLACED or REMOVED.
static void sync_valve_detectors(void)
{
    prov_device_set_t set;   // ~376 B on the iothub stack (10 KB)
    if (!provisioning_get_device_set(&set)) {
        ESP_LOGW(IOTHUB_TAG, "Valve detectors: provisioning busy, retrying");
        g_devset_changed = true;
        return;
    }

    bool have = set.has_valve;
    char mac[18];
    snprintf(mac, sizeof(mac), "%s", have ? set.valve_mac : "");

    if (strcasecmp(mac, s_det_valve_mac) == 0) return;   // same valve (or still none)

    // A real valve replaced by a DIFFERENT one, or removed (not a first valve): the rules
    // engine's MAC-less valve leak source, and a latch no other wet source holds, would
    // otherwise act on the next valve. On a swap the new, dry valve was auto-closed on its
    // first link (E-04). On a removal forget_unprovisioned() drops the source but leaves the
    // latch to the 10 s all-clear, and a valve provisioned within it links open with RMLEAK
    // clear, reads as a physical override and blocks auto-close for 24 h. A latch another
    // source still holds is kept. Decided before s_det_valve_mac takes the new MAC. Takes
    // only the rules mutex; no other lock is held here.
    if (s_det_valve_mac[0] != '\0') {
        rules_engine_on_valve_replaced();
    }

    s_valve_pub_linked = have ? -1 : 0;
    s_valve_pub_wet    = 0;
    s_valve_pub_state  = -2;
    snprintf(s_det_valve_mac, sizeof(s_det_valve_mac), "%s", mac);
    ESP_LOGI(IOTHUB_TAG, "Valve detectors reset for %s", have ? mac : "no valve");
}

// The single owner of a device-set change (D0). iothub_task only, consumed at the top of
// the loop so everything below — including the command-ack snapshot — sees the new set.
static void apply_device_set_change(void)
{
    health_reconcile_result_t r;
    if (!health_engine_reconcile_devices(HEALTH_COMMISSION_SYNC_TIMEOUT_MS, &r)) {
        g_devset_changed = true;   // provisioning or health busy: retry next pass
        ESP_LOGW(IOTHUB_TAG, "Device-set change: reconcile deferred, retrying");
        return;
    }

    // Only an ADDED valve can have missed its CONNECTED: the provision handler starts the
    // link on the esp-mqtt task before this reconcile appends the entry. The engine reads
    // the live link itself and ignores the request otherwise (e.g. sensors-only additions).
    if (r.added > 0 && !health_request_valve_resync()) {
        ESP_LOGW(IOTHUB_TAG, "Device-set change: valve link resync not queued (health queue full)");
    }

    purge_telemetry_caches();

    if (!rules_engine_forget_unprovisioned()) {
        // The reconcile is idempotent, so re-running the whole change is harmless.
        ESP_LOGW(IOTHUB_TAG, "Device-set change: rules purge deferred, retrying");
        g_devset_changed = true;
    }

    // The BLE scanner keeps its per-MAC delta state (seen / last leak) until its own 10 s
    // whitelist reload drops the MAC, so a sensor removed and re-added between two reloads
    // keeps it: a wet sensor's unchanged advertisement then produces no event, so no rules
    // evaluation and no leak_detected, while the removal's purge above already took it out
    // of the active-leak set and the interlock auto-clears (L8/L9, S6). On an ADD, forget
    // what every sensor reported, as at boot, so each re-emits once; the cache gate and the
    // idempotent rules evaluation absorb the survivors' repeats. Not on a removal alone: a
    // removed sensor still heard before the reload would simply re-commit its state.
    if (r.added > 0) {
        app_ble_leak_reset_tracking();
    }

    // Emptied -> reset once (edge). Otherwise ONLY additions arm the commission snapshot +
    // pulse; removals never do (BUG-2/3).
    bool now_empty = (r.total == 0);
    if (now_empty && !s_hub_empty) {
        on_hub_emptied();
    } else if (r.added > 0) {
        /* Empty -> non-empty edge (s_hub_empty still holds the OLD value): retry an
         * empty-hub rules reset that is known to have FAILED, so a stale latch/override
         * cannot turn the new valve's first open-with-RMLEAK-clear link into an inferred
         * physical override that blocks auto-close for 24 h.
         *
         * Deliberately NOT unconditional on every such edge: between the esp-mqtt
         * provisioning write and this loop-top reconcile, a newly provisioned sensor's
         * first leak can already have latched the incident in Phase 2, and a blanket reset
         * would drop a real leak. Only for an owed reset is that narrow race the lesser
         * risk. (The boot reset is race-free: the event loop has not started.) */
        if (s_hub_empty && s_rules_reset_owed) {
            s_rules_reset_owed = !rules_engine_reset_all();
        }
        arm_commission_snapshot(true);
        ESP_LOGI(IOTHUB_TAG,
                 "Commission: fast snapshot armed (all-devices-seen, else <=%ds; refreshes on late devices)",
                 HEALTH_COMMISSION_SYNC_TIMEOUT_MS / 1000);
    }
    s_hub_empty = now_empty;

    // A HEARD device removed inside the refresh grace lowers the seen-count below the one
    // the last commission snapshot recorded, and the refresh fires only on seen >
    // g_commission_pub_seen — so a late device's first contact would merely restore the old
    // count and publish nothing. Clamp only: an open window keeps running unchanged, and a
    // closed one stays closed. (snap_now_ms() is defined below this function.)
    if (r.removed > 0 && (esp_timer_get_time() / 1000) < g_commission_until_ms) {
        uint8_t seen = 0, total = 0;
        if (health_get_sync_counts(&seen, &total) && seen < g_commission_pub_seen) {
            g_commission_pub_seen = seen;
        }
    }

    sync_valve_detectors();

    // Every device-set change refreshes the twin (Q7; L11: decommission used to leave the
    // twin claiming the removed device until the next reconnect).
    publish_twin_reported();
}

// ---- Snapshot scheduler helpers (iothub_task context ONLY) ----------------

static inline int64_t snap_now_ms(void) { return esp_timer_get_time() / 1000; }

static const char *snap_reason_str(snap_reason_t r)
{
    switch (r) {
        case SNAP_EVENT:      return "event";
        case SNAP_COMMISSION: return "commission";
        case SNAP_BOOT:       return "boot";
        case SNAP_FAST:       return "fast";
        case SNAP_HEARTBEAT:
        default:              return "heartbeat";
    }
}

// Would the boot/commission window gate currently SUPPRESS a snapshot whose reason is
// neither EVENT nor FAST? SINGLE DEFINITION of that rule, used by both the flush block's
// gate_ok and snap_request()'s yield check — they were inline duplicates, which is how a
// suppressed reason came to outrank an EVENT in the scheduler while the flush block
// believed EVENT always wins.
//
// True means: no complete snapshot has been published for this boot/commission cycle AND
// the sync window has not closed, so HEARTBEAT/BOOT/COMMISSION would emit a
// value-incomplete snapshot. EVENT and FAST are exempt by design (EVENT carries
// post-event state already in the cache; FAST is the deliberately-early valve-ready one).
//
// NOTE health_is_boot_sync_complete() evaluates the window deadlines on read and may
// re-roll the rating. That is idempotent and wanted — it is what makes the window close
// within a caller's poll cadence rather than at the next 30 s tick.
static inline bool snap_window_suppressing(void)
{
    return !g_boot_snapshot_sent && !health_is_boot_sync_complete();
}

// Re-arm the heartbeat deadline relative to the last CONFIRMED publish. Called at
// task start, after every successful snapshot publish, and when a Twin change re-aims
// a pending heartbeat.
static void snap_rearm_heartbeat(void)
{
    s_snap_due_ms = s_snap_last_pub_ms + s_hb_interval_ms;
    s_snap_reason = SNAP_HEARTBEAT;
    s_snap_tier   = SNAP_TIER_LOW;
    s_snap_evt[0] = '\0';
    s_snap_retry_until_ms = 0;   // a confirmed publish clears any publish-fail backoff
}

// Request a snapshot. PULL-IN-ONLY: the deadline only moves EARLIER (or upgrades
// LOW->HIGH tier within an EVENT burst), so a cascade collapses to ONE snapshot.
// COMMISSION/BOOT are urgent (due=now, no min-interval clamp) to keep incremental
// refresh prompt; EVENT/HEARTBEAT are clamped to last_pub+MIN_INTERVAL to bound
// message volume. Runs in iothub_task only -> lock-free.
static void snap_request(snap_reason_t reason, snap_tier_t tier, const char *evt)
{
    int64_t now = snap_now_ms();

    if (reason == SNAP_COMMISSION || reason == SNAP_BOOT || reason == SNAP_FAST) {
        int64_t want = now;   // urgent: fire ASAP, no min-interval clamp
        if (want < s_snap_retry_until_ms) want = s_snap_retry_until_ms;  // honor publish-fail backoff
        s_snap_due_ms = want;
        s_snap_reason = reason;
        s_snap_tier   = SNAP_TIER_HIGH;
        snprintf(s_snap_evt, sizeof(s_snap_evt), "%s", evt ? evt : "");
        return;
    }

    int64_t window = (reason == SNAP_EVENT)
        ? ((tier == SNAP_TIER_HIGH) ? SNAP_HIGH_WINDOW_MS : SNAP_LOW_WINDOW_MS)
        : 0;
    int64_t want  = now + window;
    int64_t floor = s_snap_last_pub_ms + SNAP_MIN_INTERVAL_MS;   // min-interval clamp
    if (want < floor) {
        // Log it. The settle barrier announces itself when it holds a snapshot
        // back, but this clamp — which fires far more often, whenever two events
        // land inside 5 s of the last publish — used to be silent. A 1.85 s
        // snapshot delay on the bench then looked like an unexplained stall and
        // cost a log-forensics session to attribute. Bounded by SNAP_MIN_INTERVAL_MS,
        // Emitted per REQUEST, not per interval: one loop iteration can raise
        // several (health, LoRa, valve, BLE, rules), so a burst may log a few
        // identical lines. Acceptable for the diagnostic value.
        ESP_LOGI(IOTHUB_TAG, "SNAP clamped by min-interval: +%lld ms",
                 (long long)(floor - want));
        want = floor;
    }
    if (want < s_snap_retry_until_ms) want = s_snap_retry_until_ms;  // honor publish-fail backoff

    bool upgrade = (reason == SNAP_EVENT && s_snap_reason == SNAP_EVENT &&
                    tier == SNAP_TIER_HIGH && s_snap_tier == SNAP_TIER_LOW);

    /* A pending reason that the window gate is CURRENTLY SUPPRESSING must not outrank an
     * EVENT, which that gate always lets through.
     *
     * Pull-in-only compares strictly-less, and the flush block's !gate_ok arm re-arms the
     * deadline to flush_now + 2000 on every pass. That makes a suppressed HEARTBEAT (or
     * BOOT/COMMISSION) an unbeatable moving baseline: it sits ~2 s ahead forever, so a
     * SNAP_TIER_LOW request (want = now + SNAP_LOW_WINDOW_MS, also 2000) can never be
     * strictly less, and even TIER_HIGH loses on the iterations where the deferral has
     * just fired. Every such request is discarded in silence.
     *
     * Reachable whenever the deadline is already PAST-DUE at the moment the window opens
     * — e.g. a `provision` on a hub that has been offline for a while (in 2.1.3 also an
     * unprovisioned one, whose since-removed !provisioned branch re-armed the heartbeat
     * from a stale last_pub). The provision's own EVENT request is dropped for the same
     * reason (a past-due deadline is already "earlier"), so the reason stays HEARTBEAT
     * and the deferral loop starts.
     *
     * This is PRE-EXISTING, not new in 2.1.3: it equally delays a leak_detected snapshot
     * by up to the whole boot/commission window. Safety is unaffected — the leak EVENT
     * message is a direct publish, and the valve close runs in Phase 2 — but the snapshot
     * the app renders could lag the leak by minutes. The 30 s pulse is simply the first
     * feature to make it visible, because it requests at TIER_LOW on exactly this path.
     *
     * Expressed via the same helper the flush block's gate uses, so the two can never
     * drift apart. */
    bool yields = (reason == SNAP_EVENT) &&
                  (s_snap_reason != SNAP_EVENT && s_snap_reason != SNAP_FAST) &&
                  snap_window_suppressing();

    if (want < s_snap_due_ms || upgrade || yields) {
        s_snap_due_ms = want;
        s_snap_reason = reason;
        s_snap_tier   = tier;
        snprintf(s_snap_evt, sizeof(s_snap_evt), "%s", evt ? evt : "");
    }
}

// Publish (or, offline, buffer) one rules-engine event taken from
// rules_engine_take_pending_telemetry() and couple its snapshot, then free it. NULL is a
// no-op. iothub_task only, like snap_request().
static void publish_rules_telemetry(char *rules_json)
{
    if (!rules_json) return;
    telemetry_v2_publish_rules_event(rules_json);
    // Couple a snapshot: auto_close_blocked_override is low-priority (it is
    // rate-limited and the override state is unchanged); all other rules
    // events (auto_close, rmleak_cleared, override enable/re-enable) are
    // safety-critical and flush at the HIGH cadence.
    bool low = (strstr(rules_json, "auto_close_blocked_override") != NULL);
    snap_request(SNAP_EVENT, low ? SNAP_TIER_LOW : SNAP_TIER_HIGH, "rules");
    free(rules_json);
}

static void handle_c2d_command(const char *data, size_t data_len)
{
    c2d_command_t cmd;
    if (!c2d_command_parse(data, data_len, &cmd)) {
        ESP_LOGW(IOTHUB_TAG, "Unrecognized C2D payload");
        return;
    }

    ESP_LOGI(IOTHUB_TAG, "C2D cmd='%s' ver=%d id='%s'", cmd.cmd, cmd.ver, cmd.id);

    bool success = true;
    const char *error_msg = NULL;

    // ---- Valve control ----
    if (strcmp(cmd.cmd, C2D_CMD_VALVE_OPEN) == 0) {
        ESP_LOGI(IOTHUB_TAG, "Command: VALVE_OPEN");
        error_msg = c2d_valve_command(true);
        if (error_msg) {
            success = false;
            ESP_LOGW(IOTHUB_TAG, "VALVE_OPEN refused — %s", error_msg);
        }
    }
    else if (strcmp(cmd.cmd, C2D_CMD_VALVE_CLOSE) == 0) {
        ESP_LOGI(IOTHUB_TAG, "Command: VALVE_CLOSE");
        error_msg = c2d_valve_command(false);
        if (error_msg) {
            success = false;
            ESP_LOGW(IOTHUB_TAG, "VALVE_CLOSE refused — %s", error_msg);
        }
    }
    // ---- Valve set state (unified open/close) ----
    else if (strcmp(cmd.cmd, C2D_CMD_VALVE_SET_STATE) == 0) {
        cJSON *pl = cmd.payload_json ? cJSON_Parse(cmd.payload_json) : NULL;
        const char *desired = pl ? cJSON_GetStringValue(cJSON_GetObjectItem(pl, "state")) : NULL;
        if (!desired) {
            success = false;
            error_msg = "missing 'state' field (expected \"open\" or \"closed\")";
        } else if (strcmp(desired, "open") == 0) {
            ESP_LOGI(IOTHUB_TAG, "Command: VALVE_SET_STATE -> open");
            error_msg = c2d_valve_command(true);
            if (error_msg) {
                success = false;
                ESP_LOGW(IOTHUB_TAG, "VALVE_SET_STATE open refused — %s", error_msg);
            }
        } else if (strcmp(desired, "closed") == 0) {
            ESP_LOGI(IOTHUB_TAG, "Command: VALVE_SET_STATE -> closed");
            error_msg = c2d_valve_command(false);
            if (error_msg) {
                success = false;
                ESP_LOGW(IOTHUB_TAG, "VALVE_SET_STATE closed refused — %s", error_msg);
            }
        } else {
            success = false;
            error_msg = "invalid state value (expected \"open\" or \"closed\")";
        }
        if (pl) cJSON_Delete(pl);
    }
    // ---- Leak reset ----
    else if (strcmp(cmd.cmd, C2D_CMD_LEAK_RESET) == 0) {
        ESP_LOGI(IOTHUB_TAG, "Command: LEAK_RESET");
        // Refused while a leak is still active — clearing the interlock then
        // would let valve_open restore water during a live leak with no
        // override window. During-leak water must go through override_enable.
        if (!rules_engine_reset_leak_incident()) {
            success = false;
            error_msg = "A leak is still active. Fix the leak first, or use override to open the valve during a leak.";
        } else {
            ESP_LOGI(IOTHUB_TAG, "Leak incident cleared, RMLEAK reset");
        }
    }
    // ---- Decommission ----
    else if (strcmp(cmd.cmd, C2D_CMD_DECOMMISSION) == 0) {
        cJSON *pl = cmd.payload_json ? cJSON_Parse(cmd.payload_json) : NULL;
        const char *target = pl ? cJSON_GetStringValue(cJSON_GetObjectItem(pl, "target")) : NULL;

        if (!target) {
            success = false;
            error_msg = "missing decommission target";
        }
        // Targets match case-insensitively, like the BLE family below (which has
        // always used strcasecmp via sensor_type_is_ble_leak). Before 2.0.0 these
        // three used strcmp, so "BLE" was accepted but "ALL" was rejected as an
        // unknown target — the destructive one being the strict one.
        else if (strcasecmp(target, "valve") == 0) {
            ESP_LOGW(IOTHUB_TAG, "!!! DECOMMISSION_VALVE !!!");
            bool emptied = false;
            if (provisioning_remove_valve(&emptied)) {
                // The BLE target change stays here, synchronous: it is the safety half. An
                // owed BLE-apply retry cannot undo it (see iothub_apply_provisioned_mac()).
                ble_valve_set_target_mac(NULL);
                ble_valve_disconnect();
                // The last device: reset the rules-engine state now, ordered before any
                // later C2D (E-05). After the target change, so a leak or a reconnect cannot
                // command the removed valve while this waits on the rules mutex.
                if (emptied) reset_rules_state_hub_emptied();
                // Health reconcile, purges and twin run on iothub_task (D0). No snapshot
                // arming: the on-success command snapshot below is the removal's only one.
                g_devset_changed = true;
                if (!provisioning_is_provisioned())
                    ESP_LOGI(IOTHUB_TAG, "Device is now UNPROVISIONED");
            } else {
                success = false;
                error_msg = "valve decommission failed";
            }
        }
        else if (strcasecmp(target, "lora") == 0) {
            const char *sid_str = cJSON_GetStringValue(
                cJSON_GetObjectItem(pl, "sensor_id"));
            uint32_t sid = sid_str ? (uint32_t)strtoul(sid_str, NULL, 16) : 0;
            ESP_LOGW(IOTHUB_TAG, "!!! DECOMMISSION_LORA: 0x%08lX !!!", (unsigned long)sid);
            bool emptied = false;
            if (provisioning_remove_lora_sensor(sid, &emptied)) {
                char lora_id_str[16];
                snprintf(lora_id_str, sizeof(lora_id_str), "0x%08lX",
                         (unsigned long)sid);
                sensor_meta_remove(SENSOR_TYPE_LORA, lora_id_str);
                if (emptied) reset_rules_state_hub_emptied();   // the last device (E-05)
                g_devset_changed = true;   // reconcile on iothub_task (D0); survivors keep their state
                if (!provisioning_is_provisioned())
                    ESP_LOGI(IOTHUB_TAG, "Device is now UNPROVISIONED");
            } else {
                success = false;
                error_msg = "lora sensor decommission failed";
            }
        }
        // Canonical target is "ble_leak_sensor"; "ble" / "ble_leak" stay accepted
        // forever (every deployed app sends "ble"). See sensor_type_is_ble_leak().
        else if (sensor_type_is_ble_leak(target)) {
            const char *mac = cJSON_GetStringValue(
                cJSON_GetObjectItem(pl, "sensor_id"));
            ESP_LOGW(IOTHUB_TAG, "!!! DECOMMISSION_BLE: %s !!!", mac ? mac : "?");
            bool emptied = false;
            if (mac && provisioning_remove_ble_sensor(mac, &emptied)) {
                sensor_meta_remove(SENSOR_TYPE_BLE_LEAK, mac);
                if (emptied) reset_rules_state_hub_emptied();   // the last device (E-05)
                g_devset_changed = true;   // reconcile on iothub_task (D0); survivors keep their state
                if (!provisioning_is_provisioned())
                    ESP_LOGI(IOTHUB_TAG, "Device is now UNPROVISIONED");
            } else {
                success = false;
                error_msg = "ble sensor decommission failed";
            }
        }
        else if (strcasecmp(target, "all") == 0) {
            ESP_LOGW(IOTHUB_TAG, "!!! DECOMMISSION_ALL !!!");
            if (provisioning_decommission()) {
                // The safety half first: nothing below needs a BLE target, and
                // rules_engine_reset_all() can wait on the rules mutex for seconds, during
                // which a leak or a valve reconnect must not command the removed valve.
                ble_valve_set_target_mac(NULL);
                ble_valve_disconnect();
                sensor_meta_clear_all();
                hub_identity_clear();
                dps_clear_cache();
                // RAM + NVS, incl. the override window. The result is ignored: the hub
                // reboots, and the boot-time empty-hub reset runs again on a clean mutex.
                (void)rules_engine_reset_all();
                telemetry_v2_clear_settings();   // heartbeat cadence back to default

                // The health table is emptied by iothub_task (apply_device_set_change in
                // the g_decommission_reboot block) before the final snapshot, so that
                // snapshot renders the cleared (no-device) state.

                // Ack now, then hand off to iothub_task: it publishes one last
                // "decommissioned" snapshot and reboots to re-register with DPS. The
                // snapshot must be published from iothub_task (this handler runs in
                // the esp-mqtt event task), so we flag + wake instead of publishing
                // and rebooting here.
                if (cmd.is_envelope || cmd.id[0]) {
                    telemetry_v2_publish_cmd_ack(cmd.id, cmd.cmd, true, NULL);
                }
                c2d_command_free(&cmd);
                if (pl) cJSON_Delete(pl);

                g_decommission_reboot = true;
                telemetry_v2_wake_snapshot();
                return;
            } else {
                success = false;
                error_msg = "full decommission failed";
            }
        }
        else {
            success = false;
            error_msg = "unknown decommission target";
        }

        if (pl) cJSON_Delete(pl);
    }
    // ---- Override cancel (re-enable auto-close, cancel 24h override window) ----
    else if (strcmp(cmd.cmd, C2D_CMD_OVERRIDE_CANCEL) == 0) {
        ESP_LOGI(IOTHUB_TAG, "Command: OVERRIDE_CANCEL");
        if (!rules_engine_cancel_override()) {
            success = false;
            error_msg = "override cancel failed";
        }
    }
    // ---- Override enable (remote equivalent of the physical valve button) ----
    // Opens the valve during an active leak and starts the 24h water-access
    // override window. Same end-state as a physical button press; see §4.4.3.
    else if (strcmp(cmd.cmd, C2D_CMD_OVERRIDE_ENABLE) == 0) {
        ESP_LOGI(IOTHUB_TAG, "Command: OVERRIDE_ENABLE");
        override_enable_result_t r = rules_engine_enable_override_remote();
        if (r != OVERRIDE_ENABLE_OK) {
            success = false;
            switch (r) {
            case OVERRIDE_ENABLE_ERR_NO_INCIDENT:
                error_msg = "No active leak to override. Use the normal Open Valve control.";
                break;
            case OVERRIDE_ENABLE_ERR_VALVE_FLOOD:
                error_msg = "Water detected at the valve. It can't be opened remotely until the valve area is dry.";
                break;
            case OVERRIDE_ENABLE_ERR_VALVE_DISCONNECTED:
                error_msg = "The valve isn't responding. Check its power and connection, then try again.";
                break;
            case OVERRIDE_ENABLE_ERR_NOT_PROVISIONED:
                error_msg = "No valve is set up for this hub.";
                break;
            default:
                error_msg = "Something went wrong applying the override. Your water state is unchanged. Try again.";
                break;
            }
        }
    }
    // ---- Rules config ----
    else if (strcmp(cmd.cmd, C2D_CMD_RULES_CONFIG) == 0) {
        ESP_LOGI(IOTHUB_TAG, "Command: RULES_CONFIG");
        if (!cmd.payload_json ||
            !rules_engine_handle_config_command(cmd.payload_json)) {
            success = false;
            error_msg = "rules config update failed";
        } else {
            // auto_close_enabled and trigger_mask are twin-reported properties, so
            // refresh them here too. Without this they stayed stale until the next
            // MQTT reconnect — an app polling the twin to confirm the write read the
            // OLD value and could not tell the command had worked. The snapshot
            // requested at the ack site covers data.rules; this covers the twin.
            publish_twin_reported();
        }
    }
    // ---- Sensor metadata ----
    else if (strcmp(cmd.cmd, C2D_CMD_SENSOR_META) == 0) {
        ESP_LOGI(IOTHUB_TAG, "Command: SENSOR_META");
        if (!cmd.payload_json ||
            !sensor_meta_handle_command(cmd.payload_json)) {
            success = false;
            error_msg = "sensor metadata update failed";
        }
        // WI-4's prompt snapshot (so a rename reaches the app without waiting for the
        // heartbeat) is now the generic on-success snapshot at the ack site, which
        // every command gets. The dedicated flag this branch used to set has been
        // removed — two mechanisms for one job is what let rules_config end up with
        // neither.
    }
    // ---- Provisioning ----
    else if (strcmp(cmd.cmd, C2D_CMD_PROVISION) == 0) {
        ESP_LOGI(IOTHUB_TAG, "Provisioning JSON detected");
        bool emptied = false;
        if (cmd.payload_json &&
            provisioning_handle_azure_payload_json(
                cmd.payload_json, strlen(cmd.payload_json), &emptied)) {
            // The BLE target stays synchronous here (the safety half). A busy provisioning
            // read applies nothing: the retry goes to iothub_task.
            if (!iothub_apply_provisioned_mac()) {
                s_ble_apply_owed = true;
            }
            // Sensor arrays that left no device empty the hub like the last removal: reset
            // the rules-engine state now, ordered before any later C2D (E-05). The rules
            // config went back to the defaults, under this payload's own rules, in its save.
            if (emptied) reset_rules_state_hub_emptied();
            // WI-3: apply any inline per-sensor metadata carried in the SAME
            // provision payload (optional "sensor_meta":[{sensor_type,sensor_id,
            // location_code,label},...]). Shares the standalone-command apply path;
            // a bare provision (no array) is a no-op. The commission snapshot armed
            // on iothub_task reflects the location/label (add_location_obj reads it live).
            int meta_n = sensor_meta_apply_array_from_payload(cmd.payload_json);
            if (meta_n > 0)
                ESP_LOGI(IOTHUB_TAG, "Provision: applied %d inline sensor_meta entry(ies)", meta_n);
            // Hand the device-set change to iothub_task (D0). apply_device_set_change()
            // reconciles the health table (survivors keep their state; new devices get the
            // commission sync window) and, if anything was ADDED, fast-tracks the first
            // post-commission snapshot and starts the 30 s x 5 min pulse: the event loop
            // publishes as soon as every commissioned device has been heard (or at the
            // deadline), then refreshes as any late device is first heard, and keeps
            // battery/RSSI refreshing while an installer places sensors. It also pushes the
            // new commissioning state (auto_close_enabled / trigger_mask, device lists,
            // valve_id) into twin reported, so an app confirming setup via the twin does
            // not have to wait for a reconnect.
            g_devset_changed = true;
        } else {
            success = false;
            error_msg = "provisioning failed";
        }
    }
    // ---- Hub Identity ----
    else if (strcmp(cmd.cmd, C2D_CMD_SET_HUB_NAME) == 0) {
        ESP_LOGI(IOTHUB_TAG, "Command: SET_HUB_NAME");
        cJSON *pl = cmd.payload_json ? cJSON_Parse(cmd.payload_json) : NULL;
        const char *new_name = pl ? cJSON_GetStringValue(cJSON_GetObjectItem(pl, "name")) : NULL;

        if (!new_name) {
            success = false;
            error_msg = "missing 'name' field";
        } else if (strlen(new_name) > HUB_NAME_MAX_LEN) {
            success = false;
            error_msg = "name too long (max 31 chars)";
        } else {
            hub_identity_set_name(new_name);
            ESP_LOGI(IOTHUB_TAG, "Hub name set to: '%s'", hub_identity_get_name());
            publish_twin_reported();
        }
        if (pl) cJSON_Delete(pl);
    }
    else {
        ESP_LOGW(IOTHUB_TAG, "Unknown command: %s", cmd.cmd);
        success = false;
        error_msg = "unknown command";
    }

    // Send ack for v1 commands or when correlation ID is present
    if (cmd.is_envelope || cmd.id[0]) {
        telemetry_v2_publish_cmd_ack(cmd.id, cmd.cmd, success, error_msg);
    }

    // Every command that succeeded is followed by a snapshot, so the cmd_ack is
    // never the app's only evidence of what changed.
    //
    // Done HERE rather than per-command on purpose. Handling it in each branch is
    // what left rules_config with no feedback at all — no snapshot and no twin
    // publish — while sensor_meta and provision beside it had both. One site means
    // a new command cannot be added without inheriting the behaviour.
    //
    // Requested unconditionally on success, including for commands that changed
    // nothing (an open on an already-open valve, a leak_reset with nothing latched).
    // The app should not have to distinguish "accepted and no-op" from "accepted and
    // applied" by inference; the snapshot states the resulting truth either way.
    //
    // NOTE this runs on the esp-mqtt event task, so it MUST NOT call snap_request()
    // — the deadline scheduler is iothub_task-only (see g_devset_changed).
    // Flag + wake, the pattern already used for sensor_meta. Requests coalesce:
    // snap_request() is pull-in-only and clamped to SNAP_MIN_INTERVAL_MS, so a burst
    // of commands still yields one snapshot, and provision's urgent COMMISSION
    // request already scheduled below still wins over this EVENT one.
    if (success) {
        snprintf(g_cmd_snap_label, sizeof(g_cmd_snap_label), "%s", cmd.cmd);
        g_cmd_snap_pending = true;
        telemetry_v2_wake_snapshot();
    }

    c2d_command_free(&cmd);
}

// ---------------------------------------------------------------------------
// Device Twin — reported properties
// ---------------------------------------------------------------------------

static void publish_twin_reported(void)
{
    // mqtt_client is NULL until cloud_bringup() succeeds; every caller today runs
    // only after MQTT_EVENT_CONNECTED, but guard rather than rely on that.
    if (mqtt_client == NULL) return;

    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddStringToObject(root, "fw_version", telemetry_v2_fw_version());
    cJSON_AddStringToObject(root, "gateway_id", hub_identity_get_gateway_id());
    cJSON_AddStringToObject(root, "short_id", hub_identity_get_short_id());
    cJSON_AddStringToObject(root, "hub_name", hub_identity_get_name());
    cJSON_AddBoolToObject(root, "provisioned", provisioning_is_provisioned());

    // Same identity key as the lifecycle message and the snapshot valve object —
    // twin reported is hub->cloud like telemetry, so it uses the D2C vocabulary.
    // A twin reported PATCH is a MERGE: a key we simply stop writing is not
    // removed, it freezes at its last value forever. So the two older spellings
    // are explicitly nulled — in a reported patch, null DELETES the property —
    // otherwise a hub upgraded in place would report valve_id alongside a stale
    // valve_device_id, and a backend reading `valve_device_id ?? valve_id` would
    // silently prefer the frozen one. Drop these two lines one release after
    // every hub has upgraded.
    cJSON_AddNullToObject(root, "valve_mac");
    cJSON_AddNullToObject(root, "valve_device_id");

    // Null rather than omitted when no valve is provisioned, for the same reason:
    // omitting it after a decommission would leave the twin claiming a valve that
    // no longer exists.
    char valve_mac[18];
    if (provisioning_get_valve_mac(valve_mac))
        cJSON_AddStringToObject(root, "valve_id", valve_mac);
    else
        cJSON_AddNullToObject(root, "valve_id");

    uint32_t ids[MAX_LORA_SENSORS];
    uint8_t cnt = 0;
    provisioning_get_lora_sensors(ids, &cnt);
    cJSON_AddNumberToObject(root, "lora_sensor_count", cnt);

    char macs[MAX_BLE_LEAK_SENSORS][18];
    uint8_t bcnt = 0;
    provisioning_get_ble_leak_sensors(macs, &bcnt);
    cJSON_AddNumberToObject(root, "ble_leak_sensor_count", bcnt);

    rules_config_t rules;
    if (provisioning_get_rules_config(&rules)) {
        cJSON_AddBoolToObject(root, "auto_close_enabled", rules.auto_close_enabled);
        cJSON_AddNumberToObject(root, "trigger_mask", rules.trigger_mask);
    }

    cJSON_AddNumberToObject(root, "uptime_s",
                            (double)(esp_timer_get_time() / 1000000));
    // The heartbeat cadence actually IN FORCE, not the value the twin asked for.
    // Every other writable setting is echoed here (hub_name, auto_close_enabled,
    // trigger_mask); this one was not, so an app had no way to read back what the
    // hub applied — nor to notice that an out-of-range write had been rejected.
    cJSON_AddNumberToObject(root, "snapshot_interval_s",
                            (double)telemetry_v2_get_snapshot_interval_s());

    cJSON_AddNumberToObject(root, "free_heap",
                            (double)esp_get_free_heap_size());

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return;

    char topic[128];
    int rid = next_twin_rid();
    snprintf(topic, sizeof(topic),
             "$iothub/twin/PATCH/properties/reported/?$rid=%d", rid);

    ESP_LOGI(IOTHUB_TAG, "Twin reported (%d): %s", rid, json);
    esp_mqtt_client_publish(mqtt_client, topic, json, 0, 1, 0);
    free(json);
}

// ---------------------------------------------------------------------------
// Device Twin — handle desired property patches
// ---------------------------------------------------------------------------

// Apply a desired-properties OBJECT. Split out from the message handler because
// the same keys arrive in two different envelopes: a PATCH delivers them at the
// top level, while the response to a twin GET nests them under "desired". Both
// paths must behave identically — a setting that only takes effect when someone
// happens to edit it is the bug this split exists to prevent.
static void apply_twin_desired(cJSON *obj)
{
    if (!obj) return;

    // Handle snapshot_interval_s. Range validation lives in the setter, so the
    // Twin path, the NVS restore and any future C2D command share one rule and
    // cannot drift apart. The setter also persists, so this survives a reboot.
    cJSON *interval = cJSON_GetObjectItem(obj, "snapshot_interval_s");
    if (interval && cJSON_IsNumber(interval)) {
        int val = interval->valueint;
        ESP_LOGI(IOTHUB_TAG, "Twin: snapshot_interval_s = %d", val);
        if (!telemetry_v2_set_snapshot_interval(val)) {
            // Rejected. The reported echo below still carries the value actually
            // in force, so the app can see that its write did not take.
            ESP_LOGW(IOTHUB_TAG,
                     "Twin: snapshot_interval_s %d rejected — reported will show %d",
                     val, (int)telemetry_v2_get_snapshot_interval_s());
        }
    }

    // Handle hub_name
    cJSON *name = cJSON_GetObjectItem(obj, "hub_name");
    if (name && cJSON_IsString(name)) {
        if (strlen(name->valuestring) <= HUB_NAME_MAX_LEN) {
            hub_identity_set_name(name->valuestring);
            ESP_LOGI(IOTHUB_TAG, "Twin: hub_name = '%s'", hub_identity_get_name());
        } else {
            ESP_LOGW(IOTHUB_TAG, "Twin: hub_name too long (%d chars, max %d)",
                     (int)strlen(name->valuestring), HUB_NAME_MAX_LEN);
        }
    }

    // Acknowledge: publish reported back so twin stays in sync. Always — including
    // after a rejected value, which is how the app learns what is actually in force.
    publish_twin_reported();
}

static void handle_twin_desired(const char *data, int data_len)
{
    char *buf = malloc(data_len + 1);
    if (!buf) return;
    memcpy(buf, data, data_len);
    buf[data_len] = '\0';

    ESP_LOGI(IOTHUB_TAG, "Twin desired patch: %s", buf);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGW(IOTHUB_TAG, "Twin desired: invalid JSON");
        return;
    }

    apply_twin_desired(root);
    cJSON_Delete(root);
}

// Response to our twin GET: the FULL twin document, {"desired":{...},"reported":{...}}.
//
// This is what makes a desired property survive a reboot. IoT Hub sends a
// desired-properties PATCH only when the document CHANGES; it does not replay the
// current state to a device that just connected. Without an explicit GET the hub
// would come back from any reboot running compile-time defaults while the twin
// still advertised the operator's chosen values, and nothing would ever reconcile
// the two until somebody edited the twin again.
//
// The same topic also carries the (empty-bodied) acknowledgement of our own
// reported PATCH, so key off the presence of "desired" rather than the status code.
static void handle_twin_get_response(const char *data, int data_len)
{
    if (data_len <= 0) return;          // reported-PATCH ack: empty body, nothing to do

    char *buf = malloc(data_len + 1);
    if (!buf) return;
    memcpy(buf, data, data_len);
    buf[data_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return;                  // not JSON — not a twin document

    cJSON *desired = cJSON_GetObjectItem(root, "desired");
    if (cJSON_IsObject(desired)) {
        ESP_LOGI(IOTHUB_TAG, "Twin GET: applying desired properties from full document");
        apply_twin_desired(desired);
    }
    cJSON_Delete(root);
}

// ---------------------------------------------------------------------------
// MQTT stop / resume, on iothub_task only (cloud_admission())
//
// The esp-mqtt client auto-reconnects with TLS. If WiFi STA drops (e.g. a button
// WiFi reset that brings up the SoftAP captive portal without rebooting), leaving
// the client running makes it retry TLS handshakes into a dead network. Each
// handshake wants a large (~16 KB SSL_IN) buffer; with dynamic buffers disabled
// these fail (MBEDTLS_ERR_SSL_ALLOC_FAILED / -0x7F00) and fragment the heap the
// captive portal's http/dns servers need, making AP join slow and flaky. Stopping
// the client while STA is down frees that heap; the next admission restarts it.
// Until 2.1.4 WP2 the Wi-Fi manager's callbacks did both, on the wifi_manager task: the
// stop could hold that task on the MQTT control mutex and in esp_mqtt_client_stop() for
// as long as a TLS connect runs, and the resume at the IP started TLS beside the SoftAP
// of the AP tail (E4: internal heap down to 152 B, 4 failed allocations). Now the
// callbacks only set flags (iothub_on_wifi_connected() / _lost()), and iothub_task
// stops, admits and resumes.
// ---------------------------------------------------------------------------
static void mqtt_stop_for(const char *why)
{
    s_mqtt_suspended = true;   // also blocks sas_maintain() from restarting behind us
    if (mqtt_client == NULL) return;

    xSemaphoreTake(s_mqtt_ctl_mutex, portMAX_DELAY);
    if (g_mqtt_running) {
        // "WiFi down" prints the line it always printed, byte for byte.
        ESP_LOGW(IOTHUB_TAG, "%s — stopping MQTT client (free TLS heap for AP/captive portal)", why);
        esp_mqtt_client_stop(mqtt_client);
        g_mqtt_running = false;
        mark_mqtt_disconnected();   // stop() dispatches no event; clear the flags ourselves
    }
    xSemaphoreGive(s_mqtt_ctl_mutex);
}

static void mqtt_resume(void)
{
    s_mqtt_suspended = false;
    if (mqtt_client == NULL) return;

    xSemaphoreTake(s_mqtt_ctl_mutex, portMAX_DELAY);
    if (!g_mqtt_running) {
        // Starting with a token that is missing, or too close to expiry to be worth
        // a TLS handshake, would just 401. Leave it: sas_maintain() mints a fresh one
        // and starts the client within one loop iteration (<=30 s).
        if (s_sas_expiry == 0 || time(NULL) >= s_sas_expiry - SAS_RENEW_MARGIN_SEC) {
            ESP_LOGW(IOTHUB_TAG, "WiFi up — MQTT held pending SAS token refresh");
        } else {
            ESP_LOGI(IOTHUB_TAG, "WiFi up — restarting MQTT client");
            esp_mqtt_client_start(mqtt_client);
            g_mqtt_running = true;
        }
    }
    xSemaphoreGive(s_mqtt_ctl_mutex);
}

// Run on the wifi_manager task (its GOT_IP and STA_DISCONNECTED callbacks). A flag and a
// wake only: SNTP, the admission, MQTT and DPS are iothub_task's (net_maintain /
// dps_maintain), never here, so nothing here blocks. The wake is a no-op until
// telemetry_v2_init() has created the queue; the loop reads the flags on its first pass.
void iothub_on_wifi_connected(void)
{
    s_wifi_up = true;
    telemetry_v2_wake_snapshot();
}

void iothub_on_wifi_lost(void)
{
    s_wifi_up = false;   // before the count: cloud_admission() reads them the other way round
    s_wifi_losses++;     // this task is its only writer
    telemetry_v2_wake_snapshot();
}

// ---------------------------------------------------------------------------
// Inbound message reassembly
// ---------------------------------------------------------------------------
//
// esp-mqtt delivers a message larger than the RX buffer as a SEQUENCE of
// MQTT_EVENT_DATA events (deliver_publish(), mqtt_client.c). Before this, the
// handler treated every event as a whole message, so an oversized one was parsed
// from its first fragment, failed as truncated JSON, and was dropped — silently
// for C2D, because the ack is only published once a command has parsed. A
// `provision` carrying inline sensor_meta crosses 1024 bytes at about the sixth
// sensor, which is well inside the 16 the firmware supports.
//
// Three things the esp-mqtt contract guarantees, all of which this code relies on:
//
//   1. The FIRST fragment has current_data_offset == 0 and carries the topic.
//      Later fragments carry topic == NULL / topic_len == 0, because
//      CONFIG_MQTT_TOPIC_PRESENT_ALL_DATA_EVENTS is not enabled — so the routing
//      decision must be latched on the first fragment and remembered.
//   2. total_data_len is the FULL message length on every fragment, including
//      the first, so the final size is known before any of the body arrives.
//   3. Every fragment of one message is dispatched from a single loop inside
//      deliver_publish(), on the esp-mqtt task, with nothing else interleaved.
//      That is why this state can be plain statics with no lock: two messages
//      can never be in flight at once.
//
// The reassembly buffer is allocated per message at exactly total_data_len and
// freed on completion — nothing is held between messages.

#define MQTT_RX_BUFFER_BYTES   4096   // esp_mqtt_client_config_t buffer.size
#define MQTT_TX_BUFFER_BYTES   1024   // buffer.out_size — see build_mqtt_cfg()

// The outbox ceiling (esp_mqtt_client_config_t outbox.limit; 2.1.4 WP2, plan section 4.6).
// esp-mqtt keeps every QoS 1 message whole in its outbox from the publish to its PUBACK, or
// for 30 s (OUTBOX_EXPIRED_TIMEOUT_MS) when none comes - also one whose session broke (E4:
// the lifecycle sat there through two failed handshakes and expired 5 s before the next
// connect). Unbounded, a stalled session piles up a snapshot every few seconds in internal
// heap. With a limit, esp-mqtt refuses (-2) a publish that, with the queue, would pass it,
// and refuses every SUBSCRIBE while the queue is over it.
// The plan asked for about 4 KB. That alone would refuse a big hub's snapshot for ever: about
// 0.2-0.25 KB per device with its label (2.1.3 log: 1,436 B for a valve and 4 BLE sensors),
// so 7.5-8 KB for 16 BLE + 16 LoRa sensors and the valve. So the ceiling is about 4 KB of
// backlog over the largest message the hub sends. What a refusal costs: an event is kept in
// the offline buffer and replayed while connected (telemetry_v2.c), the rest of a drain
// waits likewise, a snapshot retries 5 s later (SNAP_RETRY_FLOOR_MS), and refused SUBSCRIBEs
// at a connect reconnect (MQTT_EVENT_CONNECTED).
#define MQTT_TX_MAX_MESSAGE        8192   // the largest message the hub publishes (a full hub's snapshot)
#define MQTT_OUTBOX_BACKLOG_BYTES  4096
#define MQTT_OUTBOX_LIMIT_BYTES    (MQTT_TX_MAX_MESSAGE + MQTT_OUTBOX_BACKLOG_BYTES)

// How often iothub_task replays events kept in the offline buffer while connected
// (telemetry_v2_replay_owed()).
#define REPLAY_RETRY_MS            10000

// Hard ceiling on a reassembled message. The largest legal `provision` (16 BLE +
// 16 LoRa sensors + 32 metadata entries at the full SENSOR_META_LABEL_MAX label)
// is about 5.1 KB, so this admits every payload the firmware can act on while
// still bounding what a malformed or hostile length field can make us allocate.
#define MQTT_RX_MAX_MESSAGE    8192

typedef enum {
    RX_KIND_NONE = 0,
    RX_KIND_TWIN_RES,
    RX_KIND_TWIN_DESIRED,
    RX_KIND_C2D,
} rx_kind_t;

static char     *s_rx_buf;            // NULL when not reassembling, or when discarding
static int       s_rx_len;            // bytes accumulated so far
static int       s_rx_total;          // 0 == no message in flight
static rx_kind_t s_rx_kind;
static char      s_rx_topic[96];      // fragment 1's topic, for logging after reassembly
static char      s_rx_corr[64];       // correlation id recovered from fragment 1 (C2D)
static char      s_rx_cmd[32];        // command name recovered from fragment 1 (C2D)

// Which handler owns this topic. Returns C2D for anything that is not a twin
// topic, matching the original routing: Azure delivers C2D on
// devices/<id>/messages/devicebound/... plus an optional property bag.
// Prefix lengths are DERIVED, never typed. The desired-properties prefix is 38
// characters and the old code compared 37 of them against a `topic_len > 30`
// guard: it happened to route correctly, because 37 characters are already
// unique, but it ignored the trailing '/' and would let a 31-to-36 character
// topic run strncmp past the end of a buffer esp-mqtt does not NUL-terminate.
// sizeof-1 makes both the guard and the comparison exactly right by construction.
static rx_kind_t classify_topic(const char *topic, int topic_len)
{
    static const char PFX_RES[]     = "$iothub/twin/res/";
    static const char PFX_DESIRED[] = "$iothub/twin/PATCH/properties/desired/";
    const int res_len = (int)sizeof(PFX_RES) - 1;
    const int des_len = (int)sizeof(PFX_DESIRED) - 1;

    if (!topic || topic_len <= 0) return RX_KIND_NONE;
    if (topic_len >= res_len && strncmp(topic, PFX_RES, (size_t)res_len) == 0)
        return RX_KIND_TWIN_RES;
    if (topic_len >= des_len && strncmp(topic, PFX_DESIRED, (size_t)des_len) == 0)
        return RX_KIND_TWIN_DESIRED;
    return RX_KIND_C2D;
}

// Pull a TOP-LEVEL string field out of a PARTIAL JSON envelope, so an oversized
// command can still be acked against its correlation id. cJSON is no use here —
// the document is truncated by definition and will not parse.
//
// This walks the document tracking brace/bracket depth and string state, and
// accepts a key only at depth 1. That is stricter than it looks and the
// strictness is the point:
//
//   - `"sensor_id"` / `"valve_id"` never match, because the literal `"id"`
//     requires a quote immediately before the 'i'.
//   - `"label":"id"` never matches, because a value is inside a string token,
//     which the scanner skips wholesale.
//   - `"payload":{"id":"..."}` never matches, because that key sits at depth 2.
//     Acking with an id lifted out of the payload would correlate the failure to
//     the wrong request, which is worse than acking with none.
//
// Every index is bounds-checked against len, so a fragment cut at any byte is
// safe to scan.
static void json_scan_top_string(const char *data, int len, const char *key,
                                 char *out, size_t out_sz)
{
    out[0] = '\0';
    int  klen   = (int)strlen(key);
    int  depth  = 0;
    bool in_str = false;

    for (int i = 0; i < len; i++) {
        char c = data[i];

        if (in_str) {                       // inside a string we care about nothing
            if (c == '\\')      i++;        // but an escape can hide a quote
            else if (c == '"')  in_str = false;
            continue;
        }
        if (c == '{' || c == '[') { depth++; continue; }
        if (c == '}' || c == ']') { depth--; continue; }
        if (c != '"') continue;

        // A string token starts here, outside any string. If it is our key at the
        // top level, take its value; otherwise fall through and skip the token.
        if (depth == 1 && i + klen + 1 < len &&
            strncmp(&data[i + 1], key, klen) == 0 && data[i + 1 + klen] == '"') {
            int j = i + klen + 2;
            while (j < len && (data[j] == ' ' || data[j] == '\t')) j++;
            if (j < len && data[j] == ':') {
                j++;
                while (j < len && (data[j] == ' ' || data[j] == '\t')) j++;
                if (j < len && data[j] == '"') {
                    j++;
                    size_t o = 0;
                    while (j < len && data[j] != '"' && o + 1 < out_sz) {
                        if (data[j] == '\\' && j + 1 < len) j++;
                        out[o++] = data[j++];
                    }
                    out[o] = '\0';
                    return;
                }
            }
        }
        in_str = true;
    }
}

static void rx_reset(void)
{
    free(s_rx_buf);
    s_rx_buf   = NULL;
    s_rx_len   = 0;
    s_rx_total = 0;
    s_rx_kind  = RX_KIND_NONE;
    s_rx_topic[0] = '\0';
    s_rx_corr[0]  = '\0';
    s_rx_cmd[0]   = '\0';
}

static void rx_dispatch(rx_kind_t kind, const char *topic, const char *data, int len)
{
    switch (kind) {
    case RX_KIND_TWIN_RES:
        // Logged BEFORE the length test: the ack of our own reported PATCH has an
        // EMPTY body, and this line is the observable proof the response topic is
        // live — and therefore that a twin GET will be answered.
        ESP_LOGI(IOTHUB_TAG, "Twin response: %s (%d bytes)", topic, len);
        if (len > 0) handle_twin_get_response(data, len);
        break;
    case RX_KIND_TWIN_DESIRED:
        if (len > 0) handle_twin_desired(data, len);
        break;
    case RX_KIND_C2D:
        ESP_LOGI(IOTHUB_TAG, "Received C2D Message! (%d bytes)", len);
        if (len > 0) handle_c2d_command(data, len);
        break;
    default:
        break;
    }
}

// A message we cannot accept. The point of this path is that the sender finds
// out: a dropped command used to look identical to a lost one from the cloud
// side, because the ack is only published once a command has parsed.
static void rx_reject(rx_kind_t kind, const char *why, int total)
{
    ESP_LOGE(IOTHUB_TAG, "Inbound %s message DROPPED: %s (%d bytes, limit %d)",
             kind == RX_KIND_C2D ? "C2D" : "twin", why, total, MQTT_RX_MAX_MESSAGE);

    if (kind == RX_KIND_C2D) {
        char detail[96];
        snprintf(detail, sizeof(detail), "%s: %d bytes exceeds the %d byte limit",
                 why, total, MQTT_RX_MAX_MESSAGE);
        telemetry_v2_publish_cmd_ack(s_rx_corr, s_rx_cmd[0] ? s_rx_cmd : "unknown",
                                     false, detail);
    }
}

// First (or only) fragment: classify, and either dispatch straight through or
// open a reassembly.
static void rx_begin(esp_mqtt_event_handle_t e)
{
    if (s_rx_total) {
        // Cannot happen given guarantee 3 above, but if a future IDF ever
        // interleaves, fail loudly rather than splicing two messages together.
        ESP_LOGW(IOTHUB_TAG, "Reassembly abandoned at %d/%d bytes — new message started",
                 s_rx_len, s_rx_total);
        rx_reset();
    }

    rx_kind_t kind = classify_topic(e->topic, e->topic_len);
    if (kind == RX_KIND_NONE) return;

    // Whole message in one event — the overwhelmingly common case, and the only
    // one that existed before. No allocation, no state.
    if (e->total_data_len <= e->data_len) {
        char topic[sizeof(s_rx_topic)];
        int n = e->topic_len < (int)sizeof(topic) - 1 ? e->topic_len : (int)sizeof(topic) - 1;
        memcpy(topic, e->topic, n);
        topic[n] = '\0';
        rx_dispatch(kind, topic, e->data, e->data_len);
        return;
    }

    ESP_LOGI(IOTHUB_TAG, "Inbound message spans fragments: %d of %d bytes — reassembling",
             e->data_len, e->total_data_len);

    s_rx_kind  = kind;
    s_rx_total = e->total_data_len;
    s_rx_len   = e->data_len;

    int n = e->topic_len < (int)sizeof(s_rx_topic) - 1 ? e->topic_len : (int)sizeof(s_rx_topic) - 1;
    memcpy(s_rx_topic, e->topic, n);
    s_rx_topic[n] = '\0';

    // Recover id/cmd now, while we still hold the head of the envelope — the
    // reject path below needs them and the tail may never be usable.
    if (kind == RX_KIND_C2D) {
        json_scan_top_string(e->data, e->data_len, "id", s_rx_corr, sizeof(s_rx_corr));
        json_scan_top_string(e->data, e->data_len, "cmd", s_rx_cmd, sizeof(s_rx_cmd));
    }

    if (e->total_data_len > MQTT_RX_MAX_MESSAGE) {
        rx_reject(kind, "exceeds the reassembly limit", e->total_data_len);
        return;                       // s_rx_buf stays NULL: swallow the remainder
    }

    s_rx_buf = malloc((size_t)e->total_data_len + 1);
    if (!s_rx_buf) {
        rx_reject(kind, "out of memory", e->total_data_len);
        return;
    }
    memcpy(s_rx_buf, e->data, e->data_len);
}

// Continuation fragment. Runs with no topic, so everything comes from the state
// latched by rx_begin(). A NULL s_rx_buf means the message is being swallowed
// (rejected above) — the byte count still has to be tracked so the end of it is
// recognised and the next message starts clean.
static void rx_continue(esp_mqtt_event_handle_t e)
{
    if (!s_rx_total) return;          // no message in flight; nothing to attach to

    if (s_rx_buf) {
        if (s_rx_len + e->data_len > s_rx_total) {
            ESP_LOGE(IOTHUB_TAG, "Reassembly overflow (%d + %d > %d) — message dropped",
                     s_rx_len, e->data_len, s_rx_total);
            rx_reset();
            return;
        }
        memcpy(s_rx_buf + s_rx_len, e->data, e->data_len);
    }
    s_rx_len += e->data_len;

    if (s_rx_len < s_rx_total) return;

    if (s_rx_buf) {
        s_rx_buf[s_rx_total] = '\0';
        ESP_LOGI(IOTHUB_TAG, "Reassembled %d bytes from fragments", s_rx_total);
        rx_dispatch(s_rx_kind, s_rx_topic, s_rx_buf, s_rx_total);
    }
    rx_reset();
}

// ---------------------------------------------------------------------------
// MQTT event handler
// ---------------------------------------------------------------------------

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(IOTHUB_TAG, "Connected to Azure IoT Hub!");
        g_iot_hub_connected = true;
        telemetry_v2_set_connected(true);
        net_status_set_mqtt(true);   // status LED -> fully connected (ramp blue)
        g_needs_lifecycle = true;  // Event loop will publish lifecycle + snapshot
        telemetry_v2_wake_snapshot();  // wake iothub_task now so the first post-reconnect snapshot is prompt
        {
            char sub_topic[128];
            // C2D messages
            snprintf(sub_topic, sizeof(sub_topic),
                     "devices/%s/messages/devicebound/#", g_device_id);
            int c2d_sub = esp_mqtt_client_subscribe(mqtt_client, sub_topic, 1);
            // Device Twin — response to GET/PATCH requests.
            // Remember the msg_id: the twin GET must not be published until this
            // subscription is CONFIRMED, or the response can arrive before the
            // broker has us on the topic and be dropped. See MQTT_EVENT_SUBSCRIBED.
            g_twin_res_sub_id = esp_mqtt_client_subscribe(mqtt_client,
                                                          "$iothub/twin/res/#", 1);
            // Device Twin — desired property change notifications
            int desired_sub = esp_mqtt_client_subscribe(mqtt_client,
                                      "$iothub/twin/PATCH/properties/desired/#", 1);
            // A refused SUBSCRIBE (-2: the outbox still over MQTT_OUTBOX_LIMIT_BYTES with the
            // last session's unacknowledged messages; -1: no memory) would leave this whole
            // session without C2D or the twin, maybe for days. Reconnect instead: esp-mqtt
            // expires those messages within 30 s, and the next CONNECTED subscribes again.
            if (c2d_sub < 0 || g_twin_res_sub_id < 0 || desired_sub < 0) {
                ESP_LOGE(IOTHUB_TAG, "Subscribe refused (%d %d %d, outbox %d B) - reconnecting",
                         c2d_sub, g_twin_res_sub_id, desired_sub,
                         esp_mqtt_client_get_outbox_size(mqtt_client));
                esp_mqtt_client_disconnect(mqtt_client);
            }
        }
        break;

    case MQTT_EVENT_SUBSCRIBED:
        // Fetch the full twin exactly once per connection, as soon as the response
        // topic is live. IoT Hub pushes a desired PATCH only on CHANGE, so without
        // this the hub runs compile-time defaults after every reboot while the twin
        // still advertises the operator's values.
        if (g_twin_res_sub_id > 0 && event->msg_id == g_twin_res_sub_id) {
            g_twin_res_sub_id = -1;             // one GET per connection
            char topic[64];
            int rid = next_twin_rid();
            snprintf(topic, sizeof(topic), "$iothub/twin/GET/?$rid=%d", rid);
            esp_mqtt_client_publish(mqtt_client, topic, "", 0, 1, 0);
            ESP_LOGI(IOTHUB_TAG, "Twin GET requested (rid=%d)", rid);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(IOTHUB_TAG, "Disconnected.");
        g_iot_hub_connected = false;
        telemetry_v2_set_connected(false);
        net_status_set_mqtt(false);  // status LED -> connecting (beat blue) if WiFi still up
        break;

    case MQTT_EVENT_DATA:
    {
        // Routing and the truncation guard both live in the reassembly layer
        // above, because neither can be decided from a single event: a message
        // too big for the RX buffer arrives as several of these, and only the
        // first one carries the topic. current_data_offset is what tells them
        // apart — 0 on the first (or only) fragment, rising on the rest.
        if (event->current_data_offset == 0) {
            rx_begin(event);
        } else {
            rx_continue(event);
        }
        break;
    }

    default:
        break;
    }
}

// What one apply read from provisioning, recorded under the provisioning mutex by
// apply_valve_target_locked() and acted on by iothub_apply_provisioned_mac() after it.
typedef struct {
    bool    has_valve;
    char    valve_mac[18];
    uint8_t ble_count;
} ble_apply_t;

// provisioning_with_valve_target() callback: runs with the provisioning mutex HELD, so it
// only records the read and sets the valve target (plus the log line that always preceded
// it). ble_valve_set_target_mac() takes nothing that can wait on provisioning: s_mac_lock
// sections, the command queue's reset and at most one 10 ms send, and logging.
static void apply_valve_target_locked(const char *valve_mac, uint8_t ble_count, void *ctx)
{
    ble_apply_t *a = (ble_apply_t *)ctx;
    a->has_valve = (valve_mac != NULL);
    a->ble_count = ble_count;
    if (valve_mac) {
        snprintf(a->valve_mac, sizeof(a->valve_mac), "%s", valve_mac);
        ESP_LOGI(IOTHUB_TAG, "Applying provisioned valve MAC: %s", a->valve_mac);
    }
    ble_valve_set_target_mac(valve_mac);
}

// Apply the provisioned device set to BLE (boot, every `provision`, and iothub_task's retry
// of an owed apply; runs on iothub_task or the esp-mqtt task). The valve module's target IS
// the provisioned valve, or none, so it can never link or command another one (P0-a). BLE
// starts for a valve OR a BLE sensor: the leak scanner starts with the BLE stack, and a
// sensors-only hub used to never scan (P0-b). False = provisioning busy, nothing applied.
//
// The target is read and set in ONE provisioning mutex hold. Every provisioning change takes
// that mutex, and the C2D valve/full decommissions clear the target only after their change,
// so an apply can never set a valve a concurrent change has already removed or replaced: it
// runs wholly before the change (whose own target update then overwrites it) or reads the
// result. BLE start and the connect request follow the release; a CONNECT always scans for
// the target current when it runs, and a later target change flushes it.
bool iothub_apply_provisioned_mac(void)
{
    ble_apply_t a = {0};
    if (!provisioning_with_valve_target(apply_valve_target_locked, &a)) {
        // Unknown is not "no valve": leave the current target alone. The caller owes a retry.
        ESP_LOGW(IOTHUB_TAG, "Apply provisioned devices: provisioning busy - BLE target unchanged");
        return false;
    }

    if (a.has_valve || a.ble_count > 0) {
        ESP_LOGI(IOTHUB_TAG, "Starting BLE (valve=%s, BLE sensors=%u)",
                 a.has_valve ? a.valve_mac : "none", (unsigned)a.ble_count);
        app_ble_valve_signal_start();
    }

    if (a.has_valve) {
        // A link to another valve is dropped by ble_valve_set_target_mac() itself; this
        // (re)starts the search for the provisioned one.
        //
        // ALWAYS requested, even when the live link already is this valve (the MAC check
        // only picks the log line). A decommission of this valve cleared the valve module's
        // connect request and queued its DISCONNECT; a re-provision landing before that
        // teardown completes (~0.5 s) still reads the old link as "connected", so without
        // this CONNECT the drop that follows would never be rescanned (E-09). It goes
        // through the command queue, after any DISCONNECT, and on a live link the scan only
        // logs "Already connected".
        char current_mac[18];
        if (ble_valve_get_mac(current_mac)) {
            if (strcasecmp(current_mac, a.valve_mac) != 0) {
                ESP_LOGW(IOTHUB_TAG, "Connected to wrong MAC, will reconnect to: %s", a.valve_mac);
            }
        } else {
            ESP_LOGI(IOTHUB_TAG, "Not connected, triggering connection to: %s", a.valve_mac);
        }
        ble_valve_connect();
    }
    return true;
}

// SNTP_EPOCH_VALID now lives in app_iothub.h — dps_client.c needs the same
// threshold to gate its own registration SAS token.

static TimerHandle_t s_sntp_retry_timer = NULL;

static void sntp_retry_cb(TimerHandle_t xTimer)
{
    time_t now;
    time(&now);
    if (now >= SNTP_EPOCH_VALID) {
        ESP_LOGI(IOTHUB_TAG, "SNTP now synced (ts=%ld) — stopping retry timer", (long)now);
        xTimerStop(xTimer, 0);
        return;
    }
    ESP_LOGW(IOTHUB_TAG, "SNTP still not synced — restarting NTP poll");
    esp_sntp_restart();
}

// Start SNTP only. The first sync is awaited by net_maintain() from the event loop: the
// 120 s blocking wait that used to be here ran before the loop and so held off leak
// auto-close (N1).
static void initialize_sntp(void)
{
    ESP_LOGI(IOTHUB_TAG, "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    setenv("TZ", "UTC0", 1); tzset();
    esp_sntp_init();
}

// How long the first sync may take before the 60 s re-poll timer takes over (the old
// blocking wait's 60 x 2 s).
#define SNTP_INITIAL_SYNC_MS  (120 * 1000)

// ---------------------------------------------------------------------------
// Cloud admission (2.1.4 WP2; plan section 4.6, invariant I4: no TLS or DPS while the
// SoftAP is up)
//
// The cloud - MQTT's start, DPS, the SAS restart - is admitted only when all three hold:
//   - the STA has its IP (s_wifi_up);
//   - the SoftAP is down: esp_wifi_get_mode() reads WIFI_MODE_STA. A fact read on every
//     pass, never a flag a missed callback could leave stale. A SoftAP seen up for this IP
//     must have been seen down for ADMIT_AP_SETTLE_MS (its servers' teardown);
//   - internal DMA-capable heap: ADMIT_IDMA_FREE_MIN free, with a block of
//     ADMIT_IDMA_LARGEST_MIN. Bringing the cloud up takes about 20 KB of it (G0 run B:
//     36,904 -> 16,800 B), and the long-lived blocks of a session started in a fragmented
//     heap stay where they land (G0: the largest block 18,432 -> 6,400 B for good after a
//     TLS start in the AP tail).
// The heap gate never keeps the cloud off for good: ADMIT_ESCAPE_MS after it first held this
// IP's admission back (the SoftAP down) the cloud is admitted with a block of
// ADMIT_ESCAPE_LARGEST_MIN (W), and after ADMIT_FORCE_MS whatever the heap (E); both count in
// s_admit_escapes (G3b expects none). The SoftAP rule has no escape: the AP tail ends by the
// IP + 75 s (wifi_task's backstop).
// Once admitted, the gate is not checked again (the session's own heap would fail it):
// esp-mqtt's reconnects and the SAS renewal run as before, with the SoftAP down. A link loss,
// or a SoftAP start with the STA still connected, withdraws the admission: MQTT stops at once
// (mqtt_stop_for()), and s_mqtt_suspended keeps every start off until the next admission.
// A normal boot with Wi-Fi saved has no SoftAP and about 44 KB of internal DMA-capable heap
// free before TLS (G0 run B), so it is admitted on the pass that sees the IP: its cloud comes
// up as fast as before.
// iothub_task only: net_maintain() runs it every pass, every second while an IP waits.
// ---------------------------------------------------------------------------
#define ADMIT_IDMA_CAPS           (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)
#define ADMIT_IDMA_FREE_MIN       (36 * 1024)    // plan 4.6; G0 re-derives it
#define ADMIT_IDMA_LARGEST_MIN    (12 * 1024)    // plan 4.6; G0 re-derives it
#define ADMIT_ESCAPE_MS           (60 * 1000)    // then a block of ADMIT_ESCAPE_LARGEST_MIN will do
#define ADMIT_ESCAPE_LARGEST_MIN  (8 * 1024)
#define ADMIT_FORCE_MS            (180 * 1000)   // then admitted whatever the heap
// After a SoftAP seen up for this IP, its stop is let finish first: wifi_manager's STOP_AP
// switches the mode to STA, then waits up to 1 s for the DNS task, stops the HTTP server
// and frees the network list (the whole AP stop gave back 6.7 KB on the CP5 bench). TLS's
// long-lived blocks are allocated once that memory is back, not around it.
#define ADMIT_AP_SETTLE_MS        1000

typedef enum {
    ADMIT_NO_IP = 0,   // no STA IP, nothing to admit
    ADMIT_IP,          // an IP this task has not decided on yet
    ADMIT_WAIT_AP,     // deferred: the SoftAP is up
    ADMIT_AP_SETTLE,   // deferred: the SoftAP has just stopped (ADMIT_AP_SETTLE_MS)
    ADMIT_WAIT_HEAP,   // deferred: the heap gate
    ADMIT_DONE,        // admitted
} admit_state_t;

static admit_state_t s_admit_state      = ADMIT_NO_IP;
static uint32_t      s_wifi_losses_seen = 0;   // s_wifi_losses as last acted on
static int64_t       s_admit_ip_ms      = 0;   // when this task first saw the IP
static int64_t       s_admit_settle_ms  = 0;   // when it first saw that IP's SoftAP down
static int64_t       s_admit_heap_ms    = 0;   // when the heap gate first held it back; 0 = not
static uint32_t      s_admit_escapes    = 0;   // admissions past the heap gate since boot

// The SoftAP is down: the Wi-Fi mode is STA only. False when the driver cannot say.
static bool softap_down(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    return esp_wifi_get_mode(&mode) == ESP_OK && mode == WIFI_MODE_STA;
}

static void admit_withdraw(const char *why)
{
    if (s_admit_state == ADMIT_DONE)
        ESP_LOGI(IOTHUB_TAG, "cloud admission withdrawn (%s)", why);
    mqtt_stop_for(why);
}

// how: 0 = through the gate, 1 = the 60 s escape (W), 2 = the 180 s one (E).
static void admit_now(int64_t now, size_t free_b, size_t largest, int how)
{
    unsigned long ms = (unsigned long)(now - s_admit_ip_ms);
    if (how == 0) {
        ESP_LOGI(IOTHUB_TAG, "cloud admitted %lu.%lu s after the IP (internal DMA free %u B, largest %u B)",
                 ms / 1000, (ms % 1000) / 100, (unsigned)free_b, (unsigned)largest);
    } else {
        s_admit_escapes++;
        if (how == 1)
            ESP_LOGW(IOTHUB_TAG, "cloud admitted %lu.%lu s after the IP below the heap gate (internal DMA free %u B, largest %u B) - escape %lu since boot",
                     ms / 1000, (ms % 1000) / 100, (unsigned)free_b, (unsigned)largest,
                     (unsigned long)s_admit_escapes);
        else
            ESP_LOGE(IOTHUB_TAG, "cloud admitted %lu.%lu s after the IP whatever the heap (internal DMA free %u B, largest %u B) - escape %lu since boot",
                     ms / 1000, (ms % 1000) / 100, (unsigned)free_b, (unsigned)largest,
                     (unsigned long)s_admit_escapes);
    }
    s_admit_state   = ADMIT_DONE;
    s_admit_heap_ms = 0;
    mqtt_resume();   // DPS (dps_maintain()) and the SAS mint follow from the same flag
}

static void cloud_admission(void)
{
    // A loss clears the flag before it counts (iothub_on_wifi_lost()), so the count is read
    // first: a new count always comes with the flag clear, or set again by a later IP. Read
    // the other way round, a stale "up" beside a new count would admit a dead link for a
    // pass. A loss whose count is not visible yet is caught by the flag (the second test).
    uint32_t losses = s_wifi_losses;
    bool up = s_wifi_up;
    if (losses != s_wifi_losses_seen || (!up && s_admit_state != ADMIT_NO_IP)) {
        // A link loss since the last pass, even one the link is already back from: MQTT
        // stops at once, and the next IP is admitted afresh.
        s_wifi_losses_seen = losses;
        admit_withdraw("WiFi down");   // its stop line as it always read
        s_admit_state   = ADMIT_NO_IP;
        s_admit_heap_ms = 0;
    }
    if (!up)
        return;

    int64_t now = snap_now_ms();
    if (s_admit_state == ADMIT_NO_IP) {
        s_admit_state = ADMIT_IP;
        s_admit_ip_ms = now;
    }
    if (!softap_down()) {
        // The AP tail after a setup or a rejoin, or a SoftAP started with the STA connected.
        if (s_admit_state == ADMIT_DONE)
            admit_withdraw("SoftAP up");
        s_admit_heap_ms = 0;
        if (s_admit_state != ADMIT_WAIT_AP)
            ESP_LOGI(IOTHUB_TAG, "cloud admission deferred: SoftAP up - no TLS or DPS until it stops");
        s_admit_state = ADMIT_WAIT_AP;
        return;
    }
    if (s_admit_state == ADMIT_DONE)
        return;
    if (s_admit_state == ADMIT_WAIT_AP) {
        s_admit_state     = ADMIT_AP_SETTLE;
        s_admit_settle_ms = now;
    }
    if (s_admit_state == ADMIT_AP_SETTLE && now - s_admit_settle_ms < ADMIT_AP_SETTLE_MS)
        return;   // the SoftAP's stop is still freeing its servers (no line: a second at most)

    size_t free_b  = heap_caps_get_free_size(ADMIT_IDMA_CAPS);
    size_t largest = heap_caps_get_largest_free_block(ADMIT_IDMA_CAPS);
    if (free_b >= ADMIT_IDMA_FREE_MIN && largest >= ADMIT_IDMA_LARGEST_MIN) {
        admit_now(now, free_b, largest, 0);
        return;
    }
    if (s_admit_heap_ms == 0)
        s_admit_heap_ms = now;
    int64_t held = now - s_admit_heap_ms;
    if (held >= ADMIT_FORCE_MS) {
        admit_now(now, free_b, largest, 2);
    } else if (held >= ADMIT_ESCAPE_MS && largest >= ADMIT_ESCAPE_LARGEST_MIN) {
        admit_now(now, free_b, largest, 1);
    } else if (s_admit_state != ADMIT_WAIT_HEAP) {
        ESP_LOGI(IOTHUB_TAG, "cloud admission deferred: internal DMA free %u B, largest %u B (needs %u / %u)",
                 (unsigned)free_b, (unsigned)largest,
                 (unsigned)ADMIT_IDMA_FREE_MIN, (unsigned)ADMIT_IDMA_LARGEST_MIN);
        s_admit_state = ADMIT_WAIT_HEAP;
    }
}

// The admission still holds: no link loss since the pass that admitted, the IP, the SoftAP
// down. Read-only, so a pass can ask it from inside a blocking step: dps_register()'s abort
// hook asks it every second (plan I4: DPS's private MQTT client runs TLS too). The next
// pass's cloud_admission() then withdraws the admission itself.
static bool cloud_admission_holds(void)
{
    uint32_t losses = s_wifi_losses;   // read first, as in cloud_admission()
    return s_admit_state == ADMIT_DONE && losses == s_wifi_losses_seen && s_wifi_up &&
           softap_down();
}

// Network bring-up, one non-blocking step per loop pass. iothub_task only.
// The admission first (a link loss stops MQTT on the pass that sees it). SNTP starts at the
// first Wi-Fi IP, SoftAP up or not: a few small UDP packets, and the clock is then ready for
// the TLS the admission lets in. The clock is checked on every pass until it is valid -
// possibly before Wi-Fi, since a software reset keeps the RTC time.
static void net_maintain(void)
{
    cloud_admission();
    if (!s_sntp_started && s_wifi_up) {
        initialize_sntp();
        s_sntp_started  = true;
        s_sntp_start_ms = snap_now_ms();
    }
    if (s_time_ok) return;

    time_t now = time(NULL);
    if (now >= SNTP_EPOCH_VALID) {
        s_time_ok = true;
        struct tm timeinfo = {0};
        localtime_r(&now, &timeinfo);
        ESP_LOGI(IOTHUB_TAG, "Time synced: %s", asctime(&timeinfo));
        // Give events held from before the sync their real time now, in NVS, so a
        // restart before the next connect's drain cannot lose them.
        offline_buffer_stamp_presync();
        return;
    }

    if (s_sntp_started && !s_sntp_fallback &&
        snap_now_ms() - s_sntp_start_ms >= SNTP_INITIAL_SYNC_MS) {
        s_sntp_fallback = true;   // once, even if the timer cannot be created
        ESP_LOGW(IOTHUB_TAG, "SNTP initial sync failed — starting 60s retry timer");
        s_sntp_retry_timer = xTimerCreate("sntp_retry", pdMS_TO_TICKS(60000),
                                           pdTRUE, NULL, sntp_retry_cb);
        if (s_sntp_retry_timer) {
            xTimerStart(s_sntp_retry_timer, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// SAS token maintenance
// ---------------------------------------------------------------------------

// Fill a COMPLETE client config. esp_mqtt_set_config() applies its own defaults to
// every field left unset, so a partial config would silently drop the certificate
// bundle and the keepalive. Build it here and nowhere else.
static void build_mqtt_cfg(esp_mqtt_client_config_t *cfg, const char *password)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->broker.address.uri                    = s_mqtt_uri;
    cfg->broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    cfg->credentials.username                  = s_mqtt_username;
    cfg->credentials.client_id                 = g_device_id;
    cfg->credentials.authentication.password   = password;
    cfg->session.keepalive                     = 60;

    // RX buffer. Left unset this defaults to MQTT_BUFFER_SIZE_BYTE (1024, since
    // CONFIG_MQTT_USE_CUSTOM_CONFIG is off), and that 1024 covers the WHOLE
    // packet — fixed header plus topic plus payload — so the usable payload was
    // nearer 970. A `provision` with inline sensor_meta costs ~135 bytes per
    // sensor, which put the ceiling at about five. 4096 admits all 16 sensors
    // the firmware supports even at the maximum label length, and does it in one
    // event, so reassembly stays an exception rather than the normal path.
    cfg->buffer.size = MQTT_RX_BUFFER_BYTES;

    // TX buffer, pinned explicitly. out_size DEFAULTS TO buffer.size, so raising
    // the line above on its own would silently allocate a second 4096 buffer for
    // publishing — and buy nothing, because esp_mqtt_client_publish() already
    // fragments outbound messages that do not fit (mqtt_client.c, "Provide
    // support for sending fragmented message if it doesn't fit buffer"). That is
    // why snapshots have always gone out intact at ~978 bytes. Keeping this at
    // the historical 1024 holds the net cost of the change to +3 KB.
    cfg->buffer.out_size = MQTT_TX_BUFFER_BYTES;

    // Outbox ceiling: see MQTT_OUTBOX_LIMIT_BYTES. Set here, so sas_refresh()'s
    // esp_mqtt_set_config() keeps it.
    cfg->outbox.limit = MQTT_OUTBOX_LIMIT_BYTES;
}

// esp_mqtt_client_stop() does NOT dispatch MQTT_EVENT_DISCONNECTED — the client task
// simply exits. Without this, s_connected stays true across a deliberate stop, so
// publish_json() takes the online branch, hands a leak event to an outbox that is
// about to be discarded, and skips the NVS offline buffer entirely. Call after every
// explicit stop.
static void mark_mqtt_disconnected(void)
{
    g_iot_hub_connected = false;
    telemetry_v2_set_connected(false);
    net_status_set_mqtt(false);
}

// Mint a fresh token and hand it to the client. Caller guarantees a valid clock.
static bool sas_refresh(void)
{
    time_t now = time(NULL);

    char *tok = generate_sas_token(s_resource_uri, g_device_key, SAS_TTL_SEC);
    if (!tok) {
        ESP_LOGE(IOTHUB_TAG, "SAS: mint failed (out of memory)");
        return false;
    }

    esp_mqtt_client_config_t cfg;
    build_mqtt_cfg(&cfg, tok);

    xSemaphoreTake(s_mqtt_ctl_mutex, portMAX_DELAY);

    // set_config reallocates the client's RX/TX buffers, so it must not run under a
    // live connection: stop, swap, start — the sequence stop/resume already uses.
    if (g_mqtt_running) {
        esp_mqtt_client_stop(mqtt_client);
        g_mqtt_running = false;
        mark_mqtt_disconnected();
    }

    esp_err_t err = esp_mqtt_set_config(mqtt_client, &cfg);
    free(tok);   // esp-mqtt strdup'd it into its own storage
    if (err != ESP_OK) {
        // esp_mqtt_set_config() is NOT transactional. Every failure path inside it
        // runs esp_mqtt_destroy_config(), which frees the buffers and credentials,
        // deletes the client's event loop, and leaves client->config == NULL.
        // Restarting the client here would dereference that NULL (panic), and even
        // if it didn't, the deleted event loop means mqtt_event_handler never fires
        // again — the hub would go permanently mute while looking healthy.
        // The client is unusable; a clean reboot is the only recovery. NVS-persisted
        // commissioning, incident latch and override window all survive it.
        ESP_LOGE(IOTHUB_TAG,
                 "SAS: esp_mqtt_set_config failed (%s) — client destroyed, rebooting",
                 esp_err_to_name(err));
        xSemaphoreGive(s_mqtt_ctl_mutex);
        vTaskDelay(pdMS_TO_TICKS(200));   // let the log drain
        esp_restart();
    }

    s_sas_expiry = now + SAS_TTL_SEC;
    if (!s_mqtt_suspended) {
        esp_mqtt_client_start(mqtt_client);
        g_mqtt_running = true;
    }
    xSemaphoreGive(s_mqtt_ctl_mutex);

    ESP_LOGI(IOTHUB_TAG, "SAS: token renewed (valid %d h, expires ts=%ld)",
             SAS_TTL_SEC / 3600, (long)s_sas_expiry);
    return true;
}

// Called every iteration of the iothub_task loop (which wakes at least every 30 s).
// Covers both "the clock finally arrived, mint the first real token and connect"
// and periodic renewal well before expiry.
static void sas_maintain(void)
{
    if (mqtt_client == NULL) return;
    // Nothing to maintain while the cloud is not admitted (Wi-Fi down, the SoftAP up, the
    // heap gate): re-minting every 30 s would churn heap the portal needs, and the restart
    // would be TLS beside the SoftAP. mqtt_resume() defers to us, so a stale token is
    // refreshed on the pass after the admission.
    if (s_mqtt_suspended) return;

    time_t now = time(NULL);
    if (now < SNTP_EPOCH_VALID) return;   // no trustworthy clock: can't mint a usable token

    if (s_sas_expiry != 0 && now < s_sas_expiry - SAS_RENEW_MARGIN_SEC) {
        return;                            // still comfortably valid
    }

    if (s_sas_expiry == 0) {
        ESP_LOGW(IOTHUB_TAG, "SAS: clock valid (ts=%ld) — minting first token, starting MQTT",
                 (long)now);
    } else {
        ESP_LOGI(IOTHUB_TAG, "SAS: within %d h of expiry — renewing",
                 SAS_RENEW_MARGIN_SEC / 3600);
    }
    sas_refresh();
}

// ---------------------------------------------------------------------------
// Cloud bring-up (DPS registration + MQTT client construction)
// ---------------------------------------------------------------------------

// One attempt at the whole chain: DPS assignment -> client config -> client.
// Safe to call repeatedly; it does nothing lasting until every step has succeeded,
// and mqtt_client is only published once the client is fully built and its event
// handler registered. Returns ESP_OK when the cloud path is ready, ESP_ERR_INVALID_STATE
// when DPS did not run its attempt (its registration gave up because the cloud admission
// dropped, or it had no valid clock), another error for a failed attempt.
static esp_err_t cloud_bringup(void)
{
    dps_assignment_t dps = {0};
    esp_err_t err = dps_register(AZURE_DPS_ID_SCOPE, AZURE_DPS_GROUP_KEY,
                                 hub_identity_get_gateway_id(), &dps, cloud_admission_holds);
    if (err != ESP_OK) {
        return err;
    }

    strncpy(g_hub_hostname, dps.hub_hostname, sizeof(g_hub_hostname) - 1);
    strncpy(g_device_id,    dps.device_id,    sizeof(g_device_id) - 1);
    strncpy(g_device_key,   dps.device_key,   sizeof(g_device_key) - 1);
    ESP_LOGI(IOTHUB_TAG, "DPS: hub=%s device=%s", g_hub_hostname, g_device_id);

    // These back the client config for the life of the process — sas_refresh()
    // rebuilds the same config on every renewal, so they must outlive this scope.
    snprintf(s_resource_uri, sizeof(s_resource_uri), "%s/devices/%s",
             g_hub_hostname, g_device_id);
    snprintf(s_mqtt_uri, sizeof(s_mqtt_uri), "mqtts://%s", g_hub_hostname);
    snprintf(s_mqtt_username, sizeof(s_mqtt_username),
             "%s/%s/?api-version=2021-04-12", g_hub_hostname, g_device_id);

    // Mint the initial token only against a trustworthy clock. If SNTP hasn't landed
    // the client is still built (so the handle exists) but is NOT started — a token
    // minted at ts~0 expires in 1971 and IoT Hub 401s it forever. sas_maintain()
    // mints the real one and starts the client within one loop iteration.
    time_t now       = time(NULL);
    char  *sas_token = (now >= SNTP_EPOCH_VALID)
                     ? generate_sas_token(s_resource_uri, g_device_key, SAS_TTL_SEC)
                     : NULL;
    s_sas_expiry = sas_token ? now + SAS_TTL_SEC : 0;

    esp_mqtt_client_config_t mqtt_cfg;
    build_mqtt_cfg(&mqtt_cfg, sas_token);

    // Must be live before mqtt_client is published: stop/resume gate on a non-NULL
    // mqtt_client and then take this mutex unconditionally.
    if (s_mqtt_ctl_mutex == NULL) {
        s_mqtt_ctl_mutex = xSemaphoreCreateMutexStatic(&s_mqtt_ctl_mutex_buf);
    }

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    free(sas_token);   // esp-mqtt copied it into its own storage
    if (client == NULL) {
        ESP_LOGE(IOTHUB_TAG, "esp_mqtt_client_init failed — will retry");
        s_sas_expiry = 0;
        return ESP_FAIL;
    }
    esp_mqtt_client_register_event(client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);

    mqtt_client = client;   // publish only once fully constructed
    telemetry_v2_attach_client(client, g_device_id);
    g_cloud_ready = true;

    if (s_sas_expiry == 0) {
        ESP_LOGW(IOTHUB_TAG, "Clock not synced (ts=%ld) — holding MQTT until SNTP lands",
                 (long)now);
    } else if (!s_mqtt_suspended && cloud_admission_holds()) {
        esp_mqtt_client_start(client);
        g_mqtt_running = true;
    }
    // Otherwise the admission dropped while DPS ran (an assignment that arrived as the link
    // went): the next admission starts the client (mqtt_resume()).
    return ESP_OK;
}

// Cloud bring-up from inside the event loop, so neither Wi-Fi nor a DPS outage ever stops
// leak evaluation (N1). Gated on the cloud admission (Wi-Fi with its IP and the SoftAP down,
// cloud_admission(): a CACHED assignment would otherwise build and start MQTT into a dead
// network, the TLS thrash mqtt_stop_for() exists to prevent, and a live registration would
// run its own TLS session beside the SoftAP) and on a valid clock (DPS registration stamps
// its own SAS token). The first attempt runs as soon as both hold; failures back off like
// the boot loop this replaces, then settle to DPS_RETRY_INTERVAL_MS. NOTE a live (uncached)
// registration still blocks this pass for up to the DPS client's own timeout, or until the
// admission drops (its abort hook, cloud_admission_holds(), asked every second): then it is
// not counted as an attempt and runs again on the first pass after the next admission.
static void dps_maintain(void)
{
    if (g_cloud_ready) return;
    if (s_admit_state != ADMIT_DONE) return;
    if (time(NULL) < SNTP_EPOCH_VALID) return;

    if (s_dps_next_try_ms != 0 && snap_now_ms() < s_dps_next_try_ms) return;

    s_dps_attempts++;
    bool boot_phase = (s_dps_attempts <= DPS_BOOT_ATTEMPTS);
    if (!boot_phase) {
        ESP_LOGI(IOTHUB_TAG, "DPS: retrying registration...");
    }
    esp_err_t err = cloud_bringup();
    if (err == ESP_OK) {
        if (!boot_phase) {
            ESP_LOGI(IOTHUB_TAG, "DPS: registration recovered — cloud path up");
        }
        return;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        s_dps_attempts--;   // no attempt ran to its end (dps_register() logged why): no back-off
        return;
    }

    // Measured from AFTER the attempt, which can itself block for a while.
    if (s_dps_attempts < DPS_BOOT_ATTEMPTS) {
        int backoff = s_dps_attempts * 5;
        if (backoff > 30) backoff = 30;
        ESP_LOGW(IOTHUB_TAG, "DPS failed (attempt %d/%d), retry in %ds",
                 s_dps_attempts, DPS_BOOT_ATTEMPTS, backoff);
        s_dps_next_try_ms = snap_now_ms() + (int64_t)backoff * 1000;
    } else {
        if (s_dps_attempts == DPS_BOOT_ATTEMPTS) {
            ESP_LOGE(IOTHUB_TAG,
                     "DPS unavailable after %d attempts — continuing without cloud. "
                     "Leak detection and valve auto-close run normally; DPS retried every %d min.",
                     DPS_BOOT_ATTEMPTS, DPS_RETRY_INTERVAL_MS / 60000);
        }
        s_dps_next_try_ms = snap_now_ms() + DPS_RETRY_INTERVAL_MS;
    }
}

// ---------------------------------------------------------------------------
// Event QueueSet membership
// ---------------------------------------------------------------------------

// Largest backlog one member add carries over: all 10 LoRa packets (10 x 24 B).
#define QSET_STASH_BYTES  256

// Add one member queue to the event QueueSet without losing what it already holds (N3).
//
// FreeRTOS refuses to add a queue that is not empty, and 2.1.3 ignored the result: an item
// landing between its drain and the add kept that queue out of the set until reboot. The
// producers are already running here, and the LoRa task queues a packet only AFTER it has
// ACKed it, so a discarded packet (a leak included) is never sent again. So the backlog is
// taken out, the add is retried (bounded), and the backlog is put back IN ORDER once the
// queue is a member, which posts each item to the set. Only what does not fit the stash or
// the queue is discarded, and that is logged.
//
// `required`: a data queue that cannot join the set would be ignored until reboot, so its
// leak events would never be evaluated: reboot instead. Stack buffer kept out of
// iothub_task's frame (noinline): this runs once, at boot.
static __attribute__((noinline)) void qset_add_member(QueueSetHandle_t set, QueueHandle_t q,
                                                      size_t item_size, const char *name,
                                                      bool required)
{
    if (q == NULL) {
        ESP_LOGE(IOTHUB_TAG, "QueueSet: %s queue missing - its events will not be handled", name);
        return;
    }

    uint8_t stash[QSET_STASH_BYTES] = {0};
    const int cap = (int)(sizeof(stash) / item_size);
    int held = 0, discarded = 0;
    bool added = false;

    for (int tries = 0; tries < 10 && !added; tries++) {
        while (held < cap && xQueueReceive(q, &stash[held * item_size], 0) == pdTRUE) {
            held++;
        }
        UBaseType_t extra = uxQueueMessagesWaiting(q);
        if (held >= cap && extra > 0) {   // stash full: the rest cannot be kept
            discarded += (int)extra;
            xQueueReset(q);
        }
        added = (xQueueAddToSet(q, set) == pdPASS);
    }

    if (added) {
        // Oldest ends up at the FRONT, ahead of anything that arrived during the add.
        for (int i = held - 1; i >= 0; i--) {
            if (xQueueSendToFront(q, &stash[i * item_size], 0) != pdTRUE) discarded++;
        }
    } else {
        discarded += held;
    }

    if (held > 0 || discarded > 0) {
        ESP_LOGW(IOTHUB_TAG, "QueueSet: %s queue held %d item(s) at boot - %d discarded",
                 name, held, discarded);
    }
    if (!added) {
        ESP_LOGE(IOTHUB_TAG, "QueueSet: %s queue could not be added%s", name,
                 required ? " - rebooting" : "");
        if (required) {
            vTaskDelay(pdMS_TO_TICKS(1000));   // let the log drain
            esp_restart();
        }
    }
}

// ---------------------------------------------------------------------------
// Main IoT Hub task
// ---------------------------------------------------------------------------

void iothub_task(void *param)
{
    // No Wi-Fi wait (N1). 2.1.3 blocked here until the first IP, then up to 120 s on SNTP
    // and through six DPS attempts, all before provisioning, the rules and health engines
    // and BLE were initialised: a hub without Wi-Fi had no leak protection at all. SNTP and
    // DPS now run from the loop (net_maintain / dps_maintain) without blocking it.
    ESP_LOGI(IOTHUB_TAG, "Starting IOT Hub Task...");

    // Initialize provisioning manager
    if (!provisioning_init()) {
        ESP_LOGE(IOTHUB_TAG, "Failed to initialize provisioning manager");
    }

    // Initialize sensor metadata and rules engine
    sensor_meta_init();
    rules_engine_init();
    health_engine_init();

    /* Seed the empty-hub state from a DEFINITE provisioning read (false = unknown, never
     * "empty"), and clear persisted rules-engine STATE on a hub that boots empty.
     *
     * rules_engine_init() above always restores the incident latch and the override window
     * from NVS, and a hub emptied by per-device removals under 2.1.3 or earlier still has
     * both keys (only decommission-all cleared them there); the runtime empty-hub reset runs
     * only in the C2D change that empties a hub, which such a hub never takes. Left alone,
     * the next valve provisioned and linked open with RMLEAK clear would be read as a
     * physical override and block auto-close for 24 h on the new installation. Race-free
     * here: the event loop has not started, so no leak can have latched since the load.
     *
     * The rules CONFIG is not touched: a rules_config set on an empty hub must survive a
     * reboot.
     *
     * The same read seeds the valve change detectors with the valve provisioned at boot
     * (their initial values are already the "nothing published yet" sentinels).
     *
     * If provisioning cannot be read, s_hub_empty stays false and the whole device-set
     * change is handed to the loop's first pass, with this state reset marked owed. There,
     * before any event is processed, a hub that really is empty takes the non-empty -> empty
     * edge and on_hub_emptied() runs the owed reset (still race-free; the rules config is
     * left alone, as above); a hub with devices keeps its latch, and the flag only waits for
     * a later empty edge, where a reset is wanted anyway; and sync_valve_detectors() seeds
     * the detectors. A first ADD alone would never reset. */
    {
        prov_device_set_t set;   // ~376 B on the iothub stack (10 KB)
        if (provisioning_get_device_set(&set)) {
            snprintf(s_det_valve_mac, sizeof(s_det_valve_mac), "%s",
                     set.has_valve ? set.valve_mac : "");
            int n = (set.has_valve ? 1 : 0) + set.lora_count + set.ble_count;
            s_hub_empty = (n == 0);
            if (s_hub_empty) {
                ESP_LOGI(IOTHUB_TAG, "Boot: hub is empty - clearing any persisted rules-engine state");
                if (!rules_engine_reset_all()) s_rules_reset_owed = true;
            } else {
                /* The boot reconcile inside health_engine_init() failed (provisioning or
                 * the health mutex busy) and nothing else would retry it: the table would
                 * stay empty until the next C2D change. Hand it to the loop's device-set
                 * path. That reconciles with the commission window and arms the
                 * post-provision pulse — more snapshots than a normal boot, acceptable on a
                 * failure-only path. */
                uint8_t total = 0;
                if (health_get_sync_counts(NULL, &total) && total == 0) {
                    ESP_LOGW(IOTHUB_TAG,
                             "Boot: %d device(s) provisioned but the health table is empty - retrying the reconcile",
                             n);
                    g_devset_changed = true;
                }
            }
        } else {
            ESP_LOGW(IOTHUB_TAG, "Boot: provisioning unavailable - empty-hub state unknown, retrying in the loop");
            g_devset_changed = true;
            s_rules_reset_owed = true;   // run by on_hub_emptied() if the hub is empty
        }
    }

    // Log only. BLE is decided further down by iothub_apply_provisioned_mac(), whose read is
    // definite: this answer is also false for a busy mutex, which used to leave BLE unstarted.
    if (provisioning_is_provisioned()) {
        ESP_LOGI(IOTHUB_TAG, "Hub is PROVISIONED");
    } else {
        ESP_LOGI(IOTHUB_TAG, "Hub is UNPROVISIONED - waiting for provisioning JSON from Azure");
    }

    // Initialize offline event buffer (loads pending events from NVS)
    offline_buffer_init();

    // Restore the persisted heartbeat cadence BEFORE telemetry_v2_init(), so the
    // scheduler starts on the operator's value rather than running one default-length
    // heartbeat first and correcting itself only once the twin GET response lands.
    telemetry_v2_load_settings();

    // Initialize telemetry v2 (creates snapshot timer + queue) BEFORE DPS, with no
    // client yet. publish_json() gates on a non-NULL client, so until the cloud path
    // is up every event — including a leak — takes the offline-buffer branch instead
    // of being dropped. cloud_bringup() attaches the client and corrects the topic.
    // (Before the FIRST clock sync, snapshot and lifecycle are suppressed; events are
    // built with the unsynced ts, held in the offline buffer as pre-sync entries and
    // time-stamped when the clock syncs - see offline_buffer_stamp_presync().)
    telemetry_v2_init(NULL, hub_identity_get_gateway_id(),
                      hub_identity_get_gateway_id(),
                      g_telem_lora_cache, g_telem_ble_cache);

    // QueueSet length MUST be >= the SUM of every member queue's depth. FreeRTOS
    // pushes one handle into the set per successful member send and asserts
    // (queue.c: uxMessagesWaiting < uxLength) if the set overflows — with
    // assertions enabled that is a panic reboot, not a dropped event.
    //
    //   lora_rx_queue     10   (app_lora.cpp)
    //   ble_update_queue  16   (app_ble_valve.c)
    //   ble_leak_rx_queue 10   (app_ble_leak.c)
    //   snap_q             1   (telemetry_v2.c)
    //                    ---
    //                     37
    //
    // Keep this in step with those four depths. It was 26 (= 10+5+10+1) and the
    // valve queue then went 5 -> 16 without this being updated, which would have
    // aborted the firmware on the 27th pending item — reachable exactly when
    // iothub_task stalls in dps_maintain()/sas_maintain() while sensors are
    // filling their queues, i.e. during a leak incident with the cloud down.
    #define EVT_QUEUE_SET_LEN  (10 + 16 + 10 + 1)
    QueueSetHandle_t evt_queue_set = xQueueCreateSet(EVT_QUEUE_SET_LEN);
    if (evt_queue_set == NULL) {
        // Without the set no leak event is ever dequeued; a clean reboot is the only recovery.
        ESP_LOGE(IOTHUB_TAG, "QueueSet: creation failed (out of memory) - rebooting");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    // Every member is added checked, keeping its backlog (qset_add_member()). BLE has not
    // started yet, so only a LoRa packet or a snapshot wake token (MQTT connect, an alert,
    // a rating change on any task - the fleet LED's 250 ms poll included) can be waiting.
    QueueHandle_t snap_q = telemetry_v2_get_snapshot_queue();
    qset_add_member(evt_queue_set, lora_rx_queue, sizeof(lora_packet_t), "LoRa", true);
    qset_add_member(evt_queue_set, ble_update_queue, sizeof(ble_update_type_t), "valve", true);
    qset_add_member(evt_queue_set, ble_leak_rx_queue, sizeof(ble_leak_event_t), "BLE leak", true);
    // Not required: a missing wake only leaves snapshots to the 30 s idle cap.
    qset_add_member(evt_queue_set, snap_q, sizeof(uint8_t), "snapshot wake", false);

    // Reset BLE leak sensor tracking so next advertisement triggers a fresh event
    app_ble_leak_reset_tracking();

    // BLE starts only NOW, with every member already in the set (N2): 2.1.3 started it
    // before its pre-loop drain, which could discard the valve's CONNECTED and so skip the
    // reconnect reconciliation. Same path as a `provision`; a busy provisioning read is
    // retried from the loop's first pass on.
    if (!iothub_apply_provisioned_mac()) {
        s_ble_apply_owed = true;
    }

    // Start the periodic snapshot timer (fixed liveness backstop that only wakes
    // the loop; the actual heartbeat cadence is driven by the deadline scheduler).
    telemetry_v2_start_snapshot_timer();

    // Arm the snapshot scheduler's first heartbeat deadline (now + interval).
    s_hb_interval_ms   = (int64_t)telemetry_v2_get_snapshot_interval_s() * 1000;
    s_snap_last_pub_ms = snap_now_ms();
    g_fast_arm_ms      = snap_now_ms();   // fast-snapshot ceiling is measured from here (boot)
    snap_rearm_heartbeat();
    // Boot-time rating changes (the initial reconcile) are the boot snapshot's to report.
    s_rating_seq_seen  = health_get_rating_seq();

    lora_packet_t pkt;
    ble_update_type_t ble_upd_type;
    ble_leak_event_t ble_leak_evt;
    QueueSetMemberHandle_t active_queue;

    ESP_LOGI(IOTHUB_TAG, "QueueSet Initialized. Event loop starting...");

    while (1)
    {
        // A C2D 'decommission all' cleared the device set and asked us to reboot.
        // Publish one final snapshot of the now-empty state — here, in iothub_task,
        // so it doesn't race the snapshot caches — then restart to re-register with
        // DPS. (The 3 s delay lets esp-mqtt flush the snapshot + cmd_ack, as the
        // original inline-restart path did.)
        if (g_decommission_reboot) {
            // The valve BLE disconnect (requested in the C2D handler) is async;
            // wait up to ~1 s for it to complete so the snapshot shows the valve
            // gone rather than lingering as connected.
            for (int i = 0; i < 20 && ble_valve_is_connected(); i++) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            // Reconcile against the now-empty set FIRST, so the final snapshot is built
            // from an empty table (and the caches, the rules sources and the twin agree).
            apply_device_set_change();
            telemetry_v2_publish_snapshot("decommission");
            // Events buffered while offline belong to the deployment that just ended;
            // replaying them after the reboot would report its leaks under the next one
            // (L17).
            offline_buffer_clear();
            ESP_LOGI(IOTHUB_TAG, "Decommissioned — restarting in 3s...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        }

        // A BLE apply that found provisioning busy (boot, or a `provision`). Before the
        // device-set change below, so its valve resync already sees the right target.
        // Clear-then-apply, like g_devset_changed: a failure raised while this runs is
        // never lost. A C2D provision/decommission racing it cannot be undone by it: the
        // apply reads and sets the target in one provisioning hold.
        if (s_ble_apply_owed) {
            s_ble_apply_owed = false;
            if (!iothub_apply_provisioned_mac()) {
                s_ble_apply_owed = true;   // still busy
            }
        }

        // A provision/decommission changed the device set (D0). Consumed BEFORE the
        // command-ack snapshot below, so that snapshot is built from the reconciled table
        // and never shows a removed device. Clear-then-apply: a change landing while this
        // runs sets the flag again and is re-applied next pass (the reconcile is idempotent).
        if (g_devset_changed) {
            g_devset_changed = false;
            apply_device_set_change();
        }
        // Set now only if that apply failed and re-raised the flag (or a change landed this
        // instant). The flush below defers for a change this loop top has NOT seen, never for
        // a retry, so a provisioning mutex that stays busy cannot starve snapshots (E-10).
        bool devset_retry = g_devset_changed;

        // A standalone sensor_meta rename (from the C2D task) asked for a prompt
        // snapshot so the app reflects the new label/location without waiting for
        // the heartbeat. snap_request runs only here in iothub_task (the single
        // flush context); the earlier wake made this loop iterate promptly.
        // Snapshot owed to a command that succeeded (see handle_c2d_command).
        if (g_cmd_snap_pending) {
            g_cmd_snap_pending = false;
            char label[sizeof(g_cmd_snap_label)];
            snprintf(label, sizeof(label), "%s", g_cmd_snap_label);
            snap_request(SNAP_EVENT, SNAP_TIER_HIGH, label[0] ? label : "c2d_command");
        }

        // A `provision` that added devices asked for the post-commission snapshot pulse.
        // arm_commission_snapshot() (apply_device_set_change, above) only ever sets the
        // bool; the deadlines are computed here. A second adding provision inside an open
        // window simply re-arms it, extending the pulse — which is correct.
        if (g_prov_pulse_arm) {
            g_prov_pulse_arm = false;
            int64_t pnow = snap_now_ms();
            g_prov_pulse_until_ms = pnow + PROV_PULSE_WINDOW_MS;
            g_prov_pulse_next_ms  = pnow + PROV_PULSE_PERIOD_MS;
            // Seed from the CURRENT counter, not 0: we only want packets that arrive
            // from now on to trigger a refresh. Seeding 0 would fire a spurious
            // packet-pulse on the very first loop pass after a provision.
            g_prov_pulse_seq      = health_get_checkin_seq();
            g_prov_pulse_count    = 0;
            ESP_LOGI(IOTHUB_TAG,
                     "PROV pulse armed: every %d s for %d s, plus on every sensor packet",
                     PROV_PULSE_PERIOD_MS / 1000, PROV_PULSE_WINDOW_MS / 1000);
        }

        // Latch the (Twin-tunable) heartbeat interval for this iteration.
        //
        // A CHANGE must also re-aim the deadline that is already pending, not just
        // the ones after it. snap_rearm_heartbeat() runs only after a successful
        // publish, so without this a hub asked to go from 3600 s down to 60 s would
        // sit on the old hour-long deadline before the new cadence ever started —
        // the operator asks for faster telemetry and waits up to an hour for it.
        //
        // Only re-aim when the pending snapshot IS the heartbeat. s_snap_due_ms may
        // currently hold an EVENT deadline pulled in by snap_request(), and pushing
        // that back would delay a leak snapshot to serve a cadence change.
        {
            int64_t new_hb_ms = (int64_t)telemetry_v2_get_snapshot_interval_s() * 1000;
            // s_snap_retry_until_ms != 0 means a publish failed and we are backing
            // off; snap_rearm_heartbeat() would clear that backoff, so leave it be
            // and let the next confirmed publish pick the new interval up.
            if (new_hb_ms != s_hb_interval_ms && s_snap_reason == SNAP_HEARTBEAT &&
                s_snap_retry_until_ms == 0) {
                s_hb_interval_ms = new_hb_ms;
                snap_rearm_heartbeat();          // re-aims from the last CONFIRMED publish
                ESP_LOGI(IOTHUB_TAG,
                         "SNAP heartbeat=re-aimed interval_ms=%lld next_in_ms=%lld",
                         (long long)s_hb_interval_ms,
                         (long long)(s_snap_due_ms - snap_now_ms()));
            } else {
                s_hb_interval_ms = new_hb_ms;
            }
        }

        bool mqtt_up = telemetry_v2_is_connected();

        // While a boot/commission snapshot is pending OR the post-commission
        // refresh grace is open, poll briefly so the snapshot publishes within ~2 s
        // of the window completing and the incremental refresh fires promptly.
        // The pulse window is included: its packet arm is POLLED (a check-in counter, not
        // a queue), so without the 2 s base a sensor advertisement could wait up to 30 s
        // for its snapshot — and "within a couple of seconds of hearing the sensor" is the
        // whole user-visible promise of the feature.
        // Not while offline (nothing could be published; reconnect wakes us) and not on an
        // empty hub (nothing to sync; its one boot snapshot is due immediately anyway).
        bool commission_pending = !s_hub_empty && mqtt_up &&
            (!g_boot_snapshot_sent ||
             (snap_now_ms() < g_commission_until_ms) ||
             (g_prov_pulse_until_ms != 0));

        // Cloud bring-up in progress, which used to be a blocking boot sequence: poll at 2 s
        // so the first clock sync, the boot DPS backoff and the first SAS mint are acted on
        // promptly. The clock wait is bounded to the initial sync window; after it the 60 s
        // re-poll timer owns SNTP and the normal cadence is plenty. The DPS clause counts
        // only with a valid clock, dps_maintain()'s own gate: without one it neither tries
        // nor counts an attempt, so with NTP blocked that clause alone held the 2 s poll
        // for good.
        bool cloud_pending = s_wifi_up &&
            ((!s_time_ok && !s_sntp_fallback) ||
             (!g_cloud_ready && s_dps_attempts < DPS_BOOT_ATTEMPTS &&
              time(NULL) >= SNTP_EPOCH_VALID) ||
             (g_cloud_ready && s_sas_expiry == 0));
        // An IP the cloud admission has not let in yet (the AP tail, the heap gate): every
        // second, so the admission follows the SoftAP's stop within a second and the
        // facts are read at least once a second (plan I6). Nothing here wakes the loop at
        // the AP stop itself.
        bool admit_pending = s_wifi_up && s_admit_state != ADMIT_DONE;

        // Flush trigger = derive the select timeout from the snapshot deadline so
        // the loop wakes in time to flush a pending snapshot (defeats the 30 s idle
        // block). If a snapshot is due but we can't publish (offline), idle at the
        // offline floor instead of tight-spinning; reconnect wakes us immediately via
        // telemetry_v2_wake_snapshot(). An empty hub publishes like any other.
        // A device-set change still set here was re-raised by a failed apply (the loop top
        // consumed it): retry at the 2 s poll like an owed BLE apply, not the 30 s idle cap.
        int64_t now_ms = snap_now_ms();
        int64_t delta  = s_snap_due_ms - now_ms;
        if (delta < 0) delta = 0;
        bool can_pub = mqtt_up;
        if (delta <= 0 && !can_pub) delta = SNAP_OFFLINE_FLOOR_MS;
        // A pending RMLEAK auto-clear also polls at 2 s: the rules tick runs once per pass,
        // so at the 30 s idle cap the 10 s all-clear would land 10-40 s after the last dry
        // report instead of 10-12 s.
        // A replay or a lifecycle owed while connected (REPLAY_RETRY_MS, LIFECYCLE_RETRY_MS)
        // also polls at 2 s.
        bool replay_pending = mqtt_up && (telemetry_v2_replay_owed() || s_lifecycle_owed);
        int64_t base = admit_pending ? 1000 :
                       (commission_pending || cloud_pending || s_ble_apply_owed ||
                        g_devset_changed || replay_pending ||
                        rules_engine_auto_clear_pending()) ? 2000 : 30000;
        int64_t wake = (delta < base) ? delta : base;
        TickType_t evt_wait = pdMS_TO_TICKS((uint32_t)wake) + 1;  // +1 tick: deadline strictly past on wake
        active_queue = xQueueSelectFromSet(evt_queue_set, evt_wait);

        // Periodic rules engine tick (auto-clear timeout, valve override detection)
        rules_engine_tick();

        // Publish what the tick raised NOW, before Phase 2 evaluates this pass's item. The
        // rules engine holds ONE pending event, so a wet report evaluated below replaced a
        // tick's rmleak_auto_cleared with its auto_close, and the release never reached the
        // cloud (F-08). Not on the (re)connect pass: its offline replay and lifecycle go
        // first, so there the event is left for the Phase 3 take as before.
        if (!g_needs_lifecycle) {
            publish_rules_telemetry(rules_engine_take_pending_telemetry());
        }

        // =================================================================
        // Phase 1: RECEIVE (always -- regardless of connection state)
        // =================================================================
        bool has_lora = false, has_valve = false, has_ble_leak = false;

        // Valve flood-probe reading, sampled ONCE per iteration in Phase 2 and
        // reused by the publish in Phase 3. See the capture site below for why
        // re-reading the getters at publish time is a race.
        bool  vlk_wet     = false;
        int   vlk_state   = -1;
        bool  vlk_rmleak  = false;
        int   vlk_batt    = 0xFF;   // 0xFF = unknown (published as null), never 0
        char  vlk_fw[32]  = {0};
        bool  vlk_have_fw = false;
        bool  vlk_mac_ok  = false;
        char  vlk_mac[18] = {0};

        if (active_queue == NULL) {
            // Idle timeout. Checked first: a member queue that failed to be created is NULL
            // too, and must not be read.
        } else if (active_queue == lora_rx_queue) {
            has_lora = xQueueReceive(lora_rx_queue, &pkt, 0);
        } else if (active_queue == ble_update_queue) {
            has_valve = xQueueReceive(ble_update_queue, &ble_upd_type, 0);
        } else if (active_queue == ble_leak_rx_queue) {
            has_ble_leak = xQueueReceive(ble_leak_rx_queue, &ble_leak_evt, 0);
        } else if (snap_q && active_queue == snap_q) {
            // Liveness/reconnect wake only — the heartbeat is driven by the
            // deadline scheduler + single flush block, not by this trigger.
            uint8_t trig;
            xQueueReceive(snap_q, &trig, 0);
        }

        // =================================================================
        // Phase 2: RULES (always -- works offline, no MQTT needed)
        // =================================================================
        // Sensor packets drive the rules (here) and are published (Phase 3) only for a
        // sensor provisioned to THIS hub (N4): a neighbour's LoRa sensor must not close our
        // valve, and the BLE scanner re-reads its whitelist only every 10 s, so a
        // just-removed sensor can still be heard and would re-add a ghost to the rules
        // engine's active-leak set right after the removal purged it. Membership is sampled
        // ONCE per packet so the rules decision and the publish decision cannot disagree.
        // UNKNOWN (provisioning busy) counts as provisioned: fail toward protection — a busy
        // mutex must never drop a real leak.
        prov_member_t lora_member = PROV_MEMBER_NO;
        if (has_lora) {
            lora_member = provisioning_lora_sensor_membership(pkt.sensorId);
            if (lora_member != PROV_MEMBER_NO) {
                char lora_id_str[16];
                snprintf(lora_id_str, sizeof(lora_id_str), "0x%08lX",
                         (unsigned long)pkt.sensorId);
                rules_engine_evaluate_leak(LEAK_SOURCE_LORA,
                                           (pkt.leakStatus != 0), lora_id_str);
            }
        }
        bool ble_leak_prov = has_ble_leak &&
            provisioning_ble_sensor_membership(ble_leak_evt.sensor_mac_str) != PROV_MEMBER_NO;
        if (ble_leak_prov) {
            rules_engine_evaluate_leak(LEAK_SOURCE_BLE,
                                       ble_leak_evt.leak_detected,
                                       ble_leak_evt.sensor_mac_str);
        }
        if (has_valve) {
            // Validate the valve's identity HERE, in the same breath as the leak
            // sample below — not later at publish time. The publish used to
            // re-derive it, so a GAP disconnect on the NimBLE host task (which
            // preempts this one) between the two points made ble_valve_get_mac()
            // fail, silently dropping the leak event while the auto_close it
            // caused still published. That is the exact "auto-close with no
            // stated cause" this release set out to remove, arriving in the
            // window where a disconnect is MOST likely — the hub has just queued
            // RMLEAK and close writes to that very valve. Sampling here NARROWS
            // that window to a single iteration; it cannot close it entirely,
            // since the link can still drop before the update is dequeued.
            char prov_mac[18];
            bool have_live = ble_valve_get_mac(vlk_mac);
            bool have_prov = provisioning_get_valve_mac(prov_mac);
            // The getter answers false for "no valve" AND for a busy mutex. vlk_mac_ok now
            // also gates the valve's own leak evaluation and link-up reconciliation, so busy
            // must not read as "no valve" (a steady flood at the valve does not re-notify).
            // While the valve module still has a target a valve IS provisioned: check the
            // live link against the one the detectors were last synced to.
            if (!have_prov && s_det_valve_mac[0] != '\0' && ble_valve_has_target_mac()) {
                snprintf(prov_mac, sizeof(prov_mac), "%s", s_det_valve_mac);
                have_prov = true;
                ESP_LOGW(IOTHUB_TAG, "Provisioned valve unreadable - checking against last known %s",
                         prov_mac);
            }
            if (have_live && have_prov) {
                vlk_mac_ok = (strcasecmp(vlk_mac, prov_mac) == 0);
                if (!vlk_mac_ok) {
                    ESP_LOGW(IOTHUB_TAG,
                        "Connected valve MAC %s != provisioned %s, skipping",
                        vlk_mac, prov_mac);
                }
            } else {
                // Say WHY, don't just drop. Failing this check suppresses the whole
                // valve event family for this iteration, and the old code logged
                // only on MISMATCH — so "valve unreachable" and "valve fine" looked
                // identical in the log. Dropping is still correct: the cached valve
                // state is zeroed on disconnect, so there is nothing truthful to
                // publish. Only the silence was the defect.
                ESP_LOGW(IOTHUB_TAG,
                    "Valve identity unavailable (live=%d provisioned=%d) — "
                    "valve events suppressed this iteration",
                    (int)have_live, (int)have_prov);
            }
        }

        if (has_valve && ble_upd_type == BLE_UPD_LEAK) {
            // Sample the valve BEFORE the rules engine runs. rules_engine_evaluate_leak()
            // queues an RMLEAK write, and the cached rmleak flips only when the valve
            // reports the value back (the read-back or its notify, through on_notify()),
            // never at the write — so reading the getters at publish time yields false
            // or true depending purely on read-back timing (observed on the bench:
            // leak_detected shipped rmleak:false 40 ms after RMLEAK=1 was issued,
            // contradicting the auto_close beside it).
            //
            // These values are the valve's state as of the moment this update was
            // DEQUEUED — the closest the hub can get to "at detection". If the GAP
            // link dropped between the notify and this point, app_ble_valve.c zeroes
            // the cache first, and the vlk_mac_ok check above then suppresses the
            // publish entirely rather than shipping the zeros. That is what a leak
            // event should report: water seen, interlock not yet applied. The auto_close
            // that follows carries rmleak_asserted for the interlock itself — true only
            // when the valve was actually reachable to receive it.
            //
            // Sampling leak ONCE also removes a second race: the rules engine and the
            // publish used to call ble_valve_get_leak() independently, so a fast toggle
            // between the two calls could evaluate one state and report the other.
            vlk_wet     = ble_valve_get_leak();
            vlk_state   = ble_valve_get_state();
            vlk_rmleak  = ble_valve_get_rmleak_state();
            vlk_batt    = ble_valve_get_battery();
            vlk_have_fw = ble_valve_get_firmware_rev(vlk_fw, sizeof(vlk_fw));

            // VALVE_SOURCE_ID, not the MAC: this is the rules engine's internal
            // tracking key and must stay stable across a BLE dropout. The wire
            // valve_id is resolved to the real MAC inside the rules engine.
            //
            // Only for the provisioned valve's live link (vlk_mac_ok). Otherwise the
            // sample is another valve's, or the cache the disconnect just zeroed — which
            // would read "dry" and drop a real valve flood from the active-leak set.
            if (vlk_mac_ok)
                rules_engine_evaluate_leak(LEAK_SOURCE_VALVE, vlk_wet, VALVE_SOURCE_ID);
        }

        // Valve reconnect reconciliation: re-evaluate active leaks and hub/valve sync.
        // Provisioned valve only, as above: it may close the valve and write NVS.
        if (has_valve && ble_upd_type == BLE_UPD_CONNECTED) {
            if (vlk_mac_ok)
                rules_engine_on_valve_connected();
            // Reset the valve_state_changed delta-gate so the first state notify
            // after a (re)connect / re-provision always emits the event + snapshot.
            s_valve_pub_state = -2;
        }

        // Check for pending rules engine telemetry (auto-close, rmleak events)
        char *auto_close_json = rules_engine_take_pending_telemetry();

        // ---- Connection maintenance ----
        // Deliberately AFTER Phase 2: both can block this task (esp_mqtt_client_stop()
        // waits on the mqtt task, and DPS registration runs a whole MQTT session), and
        // leak evaluation must never queue behind that within an iteration.
        net_maintain();   // SNTP start on the first Wi-Fi IP; first-sync watch
        dps_maintain();   // no cloud yet? keep trying, without stalling the loop
        sas_maintain();   // mint on first valid clock, then renew before expiry

        // =================================================================
        // Phase 3: PUBLISH (every pass; the flush below needs a connection)
        // =================================================================
        // There is deliberately NO "provisioned" gate here any more. 2.1.3 `continue`d
        // past this whole phase while unprovisioned, which skipped the lifecycle, the
        // twin, the offline drain, alert popping, the rules events and the ONLY snapshot
        // flush — an emptied hub went silent for ~9 min (BUG-6), and its one stale
        // snapshot came from a `provisioned` value sampled before the wait (BUG-3).
        // Every publisher below gates itself on its own device instead: LoRa and BLE via the
        // membership sampled in Phase 2 (only a definite NO drops), the valve events via
        // vlk_mac_ok, and
        // the valve link edge via s_det_valve_mac (a provisioned valve exists; the
        // detectors sync_valve_detectors() re-points only absorb the first teardown
        // DISCONNECTED after a removal). The empty-hub scheduler state is set once, on the
        // transition, by on_hub_emptied().

        // ---- Lifecycle on first connect / reconnect ----
        // Once per CONNECTED: every (re)connect, esp-mqtt's own reconnects included, raises
        // g_needs_lifecycle, so a lifecycle lost with its session is sent again by the next
        // one (E4: the 234 s lifecycle expired from the outbox across two failed handshakes,
        // and the 269 s connect's reached IoT Hub). One that esp-mqtt did not take at all
        // (refused for room, MQTT_OUTBOX_LIMIT_BYTES) is owed until it does, while connected:
        // never a second copy on a connect whose first was taken.
        if (g_needs_lifecycle) {
            g_needs_lifecycle = false;
            telemetry_v2_drain_offline();   // Replay buffered events before lifecycle
            s_lifecycle_owed = !telemetry_v2_publish_lifecycle();
            s_lifecycle_retry_ms = snap_now_ms() + LIFECYCLE_RETRY_MS;
            if (s_lifecycle_owed)
                ESP_LOGW(IOTHUB_TAG, "Lifecycle not taken by MQTT - sent again every %d s while connected",
                         LIFECYCLE_RETRY_MS / 1000);
            publish_twin_reported();        // Update Device Twin reported properties
            g_boot_snapshot_sent = false;   // Wait for boot sync before first snapshot
            g_fast_snapshot_sent = false;   // Re-arm the fast valve-ready snapshot for this (re)connect
            g_fast_arm_ms = snap_now_ms();  // restart the ceiling clock from THIS (re)connect (not absolute uptime)
        } else if (telemetry_v2_is_connected()) {
            if (telemetry_v2_replay_owed() && snap_now_ms() >= s_replay_retry_ms) {
                // Events the outbox refused for room while connected, or the rest of a drain
                // cut short, wait in the offline buffer: replayed now, at most every
                // REPLAY_RETRY_MS, not only at the next connect (MQTT_OUTBOX_LIMIT_BYTES).
                s_replay_retry_ms = snap_now_ms() + REPLAY_RETRY_MS;
                telemetry_v2_drain_offline();
            }
            if (s_lifecycle_owed && snap_now_ms() >= s_lifecycle_retry_ms) {
                s_lifecycle_retry_ms = snap_now_ms() + LIFECYCLE_RETRY_MS;
                s_lifecycle_owed = !telemetry_v2_publish_lifecycle();
            }
        }

        // NOTE: the rules-engine events (auto_close, rmleak_*) are held in
        // auto_close_json and published FURTHER DOWN, after the device events.
        // Publishing them here put the CONSEQUENCE on the wire before its CAUSE:
        // rules evaluation runs in Phase 2, so a valve/sensor leak produced
        // auto_close ~40 ms ahead of the leak_detected that triggered it, and a
        // cloud consumer reading in order saw an unexplained auto-close.

        // ---- Health alerts (offline / recovered) and rating changes ----
        {
            health_alert_t alert;
            bool any_alert = false;
            while (health_pop_alert(&alert)) {
                char *json = health_alert_to_json(&alert);
                if (json) {
                    telemetry_v2_publish_health_event(json);
                    free(json);
                    any_alert = true;
                }
            }
            // A device crossing offline/Critical (or recovering) is a real state
            // change carried in the snapshot (rating/connected) — couple a snapshot
            // like every other event producer so the app doesn't wait for the next
            // heartbeat. Health alerts are already debounced, so this stays low-volume.
            if (any_alert)
                snap_request(SNAP_EVENT, SNAP_TIER_HIGH, "health");

            // A rating change nobody alerts on. Valve battery-critical raises no alert
            // (Phase B: suppressed like a leak), and a roll-up grace expiry has no device
            // edge at all; without this the cloud learned both only at the next heartbeat
            // (field log: RED at 678 s, published at 969 s). EVENT is 5 s-clamped and
            // coalesced, so a flapping reading costs at most one snapshot per clamp.
            uint32_t rs = health_get_rating_seq();
            if (rs != s_rating_seq_seen) {
                s_rating_seq_seen = rs;
                snap_request(SNAP_EVENT, SNAP_TIER_HIGH, "health");
            }
        }

        // ---- LoRa sensor events ----
        if (has_lora) {
            ESP_LOGI(IOTHUB_TAG, "Event: LoRa Packet from 0x%08lX",
                     (unsigned long)pkt.sensorId);

            if (lora_member == PROV_MEMBER_NO) {   // sampled in Phase 2
                ESP_LOGW(IOTHUB_TAG, "Sensor 0x%08lX not provisioned, skipping",
                         (unsigned long)pkt.sensorId);
            } else {
                bool leak_changed = update_lora_cache_check_leak(&pkt);
                if (leak_changed) {
                    char lora_id[16];
                    snprintf(lora_id, sizeof(lora_id), "0x%08lX",
                             (unsigned long)pkt.sensorId);
                    bool wet = (pkt.leakStatus != 0);
                    const char *ev = wet ? "leak_detected" : "leak_cleared";
                    telemetry_v2_publish_leak_event(&(telem_leak_event_t){
                        .event      = ev,
                        .source     = LEAK_SOURCE_LORA,
                        .device_id  = lora_id,
                        .leak_state = wet,
                        .battery    = pkt.batteryPercentage,
                        .has_rssi   = true,
                        .rssi       = pkt.rssi,
                    });
                    snap_request(SNAP_EVENT, SNAP_TIER_HIGH, ev);
                }
            }
        }

        // ---- Valve events ----
        if (has_valve) {
            ESP_LOGI(IOTHUB_TAG, "Event: BLE Update type=%d", ble_upd_type);

            // Identity was validated in Phase 2, in the same iteration and BEFORE
            // the rules engine ran — see the capture site. Re-deriving it here
            // would reopen the disconnect window it closes.
            const char *connected_mac = vlk_mac;

            // ---- Link edge: published OUTSIDE the vlk_mac_ok gate ----
            // This must not sit inside that gate: the gate reads the LIVE valve
            // MAC, which is zeroed on disconnect, so it is always false on the
            // very edge we need to report. The snapshot itself carries the valve
            // identity from the health table, so nothing here depends on the live
            // MAC. Delta-gated so a flapping link produces one snapshot per real
            // transition, and the tier only sets the coalescing window — the 5 s
            // SNAP_MIN_INTERVAL_MS rate cap still applies.
            //
            // Only with a PROVISIONED valve (s_det_valve_mac). An empty or valve-less hub
            // has no valve to report. Since 2.1.4 the valve module links and reports only
            // the provisioned valve, and a link whose target was removed tears down without
            // a DISCONNECTED; this gate and sync_valve_detectors()'s preset of 0 remain the
            // hub-side backstop for an update queued just before a removal.
            int linked = (ble_upd_type == BLE_UPD_CONNECTED)    ? 1
                       : (ble_upd_type == BLE_UPD_DISCONNECTED) ? 0
                       : -1;
            if (linked >= 0 && s_det_valve_mac[0] != '\0' && linked != s_valve_pub_linked) {
                s_valve_pub_linked = linked;
                snap_request(SNAP_EVENT, SNAP_TIER_HIGH,
                             linked ? "valve_linked" : "valve_unlinked");
            }

            if (vlk_mac_ok) {
                if (ble_upd_type == BLE_UPD_LEAK) {
                    // Water at the valve's own flood probe. Since 1.9.0 this is
                    // reported through the SAME leak_detected/leak_cleared family
                    // as the BLE and LoRa sensors, discriminated by
                    // source_type:"valve" — one cloud handler for "water was
                    // detected somewhere". (It was valve_flood_detected /
                    // valve_flood_cleared with an unrelated payload shape.)
                    //
                    // EVERY field here — including the identity — comes from the
                    // single sample taken in Phase 2, before the rules engine ran.
                    // Nothing is re-read: the same value that selected the event
                    // name populates leak_state (so leak_detected can never carry
                    // leak_state:false), and rmleak is the pre-interlock state
                    // rather than a coin-flip on GATT write timing.
                    const char *ev = vlk_wet ? "leak_detected" : "leak_cleared";

                    /* DELTA-GATE, like BLE_UPD_STATE below.
                     *
                     * app_ble_valve.c now announces BLE_UPD_LEAK at link-up when the flood
                     * probe is ALREADY wet (so a flood that predates the link still gets
                     * evaluated and closes the valve — it produces no dry->wet edge of its
                     * own). That announcement repeats on every reconnect, so without a gate
                     * a valve that keeps dropping while wet would republish leak_detected
                     * each time, with no intervening leak_cleared: a consumer counting leak
                     * events would see several incidents where there is one.
                     *
                     * The rules engine is NOT gated — rules_engine_evaluate_leak() already
                     * ran in Phase 2 and is idempotent, which is what makes the valve
                     * action safe to repeat while the D2C event is suppressed. */
                    if ((int)vlk_wet != s_valve_pub_wet) {
                        s_valve_pub_wet = (int)vlk_wet;

                        telemetry_v2_publish_leak_event(&(telem_leak_event_t){
                            .event         = ev,
                            .source        = LEAK_SOURCE_VALVE,
                            .device_id     = connected_mac,
                            .leak_state    = vlk_wet,
                            .battery       = vlk_batt,
                            .has_valve_ext = true,
                            .valve_state   = vlk_state == 1 ? "open"
                                           : vlk_state == 0 ? "closed" : "unknown",
                            .rmleak        = vlk_rmleak,
                            .fw_version    = vlk_have_fw ? vlk_fw : NULL,
                        });
                        snap_request(SNAP_EVENT, SNAP_TIER_HIGH, ev);
                    } else {
                        ESP_LOGD(IOTHUB_TAG,
                                 "valve leak event suppressed — wet=%d already reported",
                                 (int)vlk_wet);
                    }
                } else if (ble_upd_type == BLE_UPD_STATE) {
                    // Delta-gate: only emit on a REAL state change (was emitted on
                    // every BLE_UPD_STATE notify). Caps the event stream and the
                    // coupled snapshot rate.
                    int vstate = ble_valve_get_state();
                    if (vstate != s_valve_pub_state) {
                        s_valve_pub_state = vstate;
                        telemetry_v2_publish_valve_event("valve_state_changed",
                                                         connected_mac);
                        snap_request(SNAP_EVENT, SNAP_TIER_HIGH, "valve_state_changed");
                    }
                } else if (ble_upd_type == BLE_UPD_RMLEAK) {
                    // The valve reported a new RMLEAK value (its notify, or the read-back
                    // after a hub write). No event: the rules engine publishes its own
                    // (auto_close, rmleak_auto_cleared, override, ...). But the snapshot
                    // reads the cache, which changes only now, and the rules event's
                    // snapshot usually goes first (bench, auto-clear: RMLEAK=0 written at
                    // 219.871 s, snapshot at 220.181 s still rmleak:true, read-back at
                    // 221.061 s), so the cloud showed the valve locked until the next
                    // heartbeat. app_ble_valve.c posts this only on a change and never
                    // during GATT setup, and a request still pending coalesces with it.
                    snap_request(SNAP_EVENT, SNAP_TIER_HIGH, "rmleak");
                }
                // BLE_UPD_BATTERY: no event — included in snapshot
                // BLE_UPD_CONNECTED: lifecycle/snapshot handles this
            }
        }

        // ---- BLE leak sensor events ----
        if (has_ble_leak && !ble_leak_prov) {
            ESP_LOGW(IOTHUB_TAG, "BLE leak event from unprovisioned %s dropped",
                     ble_leak_evt.sensor_mac_str);
        } else if (has_ble_leak) {
            ESP_LOGI(IOTHUB_TAG, "Event: BLE Leak %s leak=%d batt=%d",
                     ble_leak_evt.sensor_mac_str,
                     ble_leak_evt.leak_detected, ble_leak_evt.battery);

            bool leak_changed = update_ble_leak_cache_check_leak(&ble_leak_evt);
            if (leak_changed) {
                const char *ev = ble_leak_evt.leak_detected ? "leak_detected"
                                                            : "leak_cleared";
                telemetry_v2_publish_leak_event(&(telem_leak_event_t){
                    .event      = ev,
                    .source     = LEAK_SOURCE_BLE,
                    .device_id  = ble_leak_evt.sensor_mac_str,
                    .leak_state = ble_leak_evt.leak_detected,
                    .battery    = ble_leak_evt.battery,
                    .has_rssi   = true,
                    .rssi       = ble_leak_evt.rssi,
                });
                snap_request(SNAP_EVENT, SNAP_TIER_HIGH, ev);
            }
        }

        // ---- Rules engine events (auto-close, rmleak changes) ----
        // DELIBERATELY LAST among the event publishers, so the cause reaches the
        // cloud before the consequence: leak_detected (above) then auto_close
        // (here). The rules engine still EVALUATES in Phase 2 and the valve close
        // is issued there — only the telemetry is held back, so this reorders the
        // wire, never the safety action.
        publish_rules_telemetry(auto_close_json);

        // ---- Fast boot/reconnect snapshot ARMING (valve-READY, no publish here) ----
        // Publish a snapshot as soon as the valve GATT setup completes (~20-30 s)
        // instead of waiting the full boot-sync timeout (~120 s) for an offline/slow
        // sensor. Gate on ble_valve_is_ready() (CONNECTED|ENCRYPTED|DISCOVERY_DONE),
        // NOT ble_valve_is_connected() (mere GAP link, ~11 s): "ready" means each
        // valve characteristic (state/flood/rmleak/battery/FW) has been read and the
        // valve struct is FILLED, so the fast snapshot carries real valve data, not
        // defaults. Ceiling: fire by 150 s even if the valve never becomes ready.
        // One-shot per (re)connect; armed ONLY on the boot/reconnect path
        // (g_fast_snapshot_sent is reset only in the lifecycle block, never by
        // arm_commission_snapshot), so the provision/commission path keeps its
        // complete-wait behavior. On the successful publish the flush opens the refresh
        // grace window so the remaining sensors fill in via incremental refresh.
        if (!g_fast_snapshot_sent && !g_boot_snapshot_sent &&
            (ble_valve_is_ready() ||
             (snap_now_ms() - g_fast_arm_ms) >= SNAP_FAST_CEILING_MS)) {
            snap_request(SNAP_FAST, SNAP_TIER_HIGH, "fast");
        }

        // ---- Boot/commission snapshot ARMING (no publish here) ----
        // Arm the deadline when the boot-sync window completes (boot, reconnect,
        // or a `provision` command) or when a late device is heard within the
        // refresh grace. The actual publish + bookkeeping happen in the single
        // flush block below, AFTER all cache updates and only on a confirmed send
        // — so the snapshot reflects the advertisement that just completed the
        // window and a failed publish never marks the snapshot "sent" (the
        // guaranteed boot snapshot can't be lost).
        if (!g_boot_snapshot_sent && health_is_boot_sync_complete()) {
            snap_request(SNAP_BOOT, SNAP_TIER_HIGH, "boot");
        } else if (g_boot_snapshot_sent &&
                   (snap_now_ms() < g_commission_until_ms)) {
            uint8_t seen = 0, total = 0;
            if (health_get_sync_counts(&seen, &total) && seen > g_commission_pub_seen) {
                snap_request(SNAP_COMMISSION, SNAP_TIER_HIGH, "commission-refresh");
            }
        }

        // ---- Post-provision snapshot PULSE (30 s cadence + every sensor packet) ----
        // Both arms request SNAP_EVENT, and each property of that is load-bearing:
        //   * EVENT is rate-CLAMPED to SNAP_MIN_INTERVAL_MS (5 s, <=12/min). COMMISSION /
        //     BOOT / FAST deliberately bypass the clamp, so using one of those here would
        //     let a multi-sensor burst publish back-to-back.
        //   * EVENT passes the incomplete-window `gate_ok` below, so pulses publish DURING
        //     the open sync window. That is the point — progressive refinement — and it is
        //     the same licence SNAP_FAST already takes.
        //   * EVENT does NOT set g_boot_snapshot_sent and does NOT touch
        //     g_commission_until_ms, so the guaranteed COMPLETE boot/commission snapshot
        //     still fires when the sync window closes. No existing guarantee is weakened.
        //   * On the wire data.reason stays "event" — no new schema enum value, nothing for
        //     a cloud consumer to learn. The distinction lives in the UART trace.
        // TIER_LOW (2 s coalescing) so four sensors bursting together collapse into one
        // snapshot; TIER_HIGH's 300 ms window would emit up to four.
        if (g_prov_pulse_until_ms) {
            int64_t pnow = snap_now_ms();
            if (pnow < g_prov_pulse_until_ms) {
                if (g_prov_pulse_count < PROV_PULSE_MAX_SNAPS) {
                    // Packet arm. The counter is the ONLY way this task can learn that a
                    // sensor was heard: an unchanged repeat packet is dropped by the
                    // scanner's telemetry delta gate and never reaches ble_leak_rx_queue,
                    // while the health engine sees every burst. Compared with != because
                    // the counter wraps.
                    uint32_t seq = health_get_checkin_seq();
                    if (seq != g_prov_pulse_seq) {
                        g_prov_pulse_seq = seq;
                        g_prov_pulse_count++;
                        snap_request(SNAP_EVENT, SNAP_TIER_LOW, "prov_pkt");
                    }
                    // Periodic arm. Deadline advances from the deadline, not from `pnow`,
                    // so the cadence cannot drift late across a busy iteration.
                    if (pnow >= g_prov_pulse_next_ms) {
                        g_prov_pulse_next_ms += PROV_PULSE_PERIOD_MS;
                        // A long stall (offline, or the loop blocked in dps_maintain)
                        // could leave the deadline several periods in the past; skip
                        // forward rather than firing a burst of catch-up pulses.
                        if (g_prov_pulse_next_ms <= pnow)
                            g_prov_pulse_next_ms = pnow + PROV_PULSE_PERIOD_MS;
                        g_prov_pulse_count++;
                        snap_request(SNAP_EVENT, SNAP_TIER_LOW, "prov_pulse");
                    }
                } else if (g_prov_pulse_count == PROV_PULSE_MAX_SNAPS ||
                           g_prov_pulse_count == PROV_PULSE_MAX_SNAPS + 1) {
                    // Announce the cap once, then stay quiet. Never truncate silently: a
                    // capped window must not read as a completed one.
                    //
                    // BOTH equality values are needed. The two arms above can each
                    // increment in the SAME iteration, so a count of MAX-1 becomes MAX+1
                    // in one pass and steps straight over a bare `== MAX` — the warning
                    // would never fire and the cap would suppress the rest of the window
                    // in exactly the silence this branch exists to prevent. The sentinel
                    // below then parks the count above both values so it still logs once.
                    g_prov_pulse_count = PROV_PULSE_MAX_SNAPS + 2;
                    ESP_LOGW(IOTHUB_TAG,
                             "PROV pulse capped at %d snapshots — suppressing the rest of the window",
                             PROV_PULSE_MAX_SNAPS);
                }
            } else {
                ESP_LOGI(IOTHUB_TAG, "PROV pulse window closed (%u snapshot(s) requested)",
                         (unsigned)g_prov_pulse_count);
                g_prov_pulse_until_ms = 0;
                g_prov_pulse_next_ms  = 0;
                g_prov_pulse_count    = 0;
            }
        }

        // =================================================================
        // SINGLE FLUSH BLOCK — the ONLY telemetry_v2_publish_snapshot() site.
        // Runs after all event/cache updates and the boot/commission arming, so
        // the snapshot always reflects post-burst state (ordering invariant).
        // Re-arms the heartbeat ONLY on a snapshot that actually reached esp-mqtt.
        //
        // Skipped for ONE pass while a C2D provision/decommission that landed on the esp-mqtt
        // task during this pass is still unreconciled (E-10): its ok ack precedes this
        // snapshot, and the health table would still show the pre-change set (e.g. a removed
        // valve). The next loop top reconciles it; the deadline stays due, so that pass's
        // select waits one tick and this block then publishes the reconciled table. Not for
        // a failed apply's retry (devset_retry above).
        // =================================================================
        bool devset_unseen = g_devset_changed && !devset_retry;
        if (!devset_unseen && telemetry_v2_is_connected() && snap_now_ms() >= s_snap_due_ms) {
            int64_t flush_now = snap_now_ms();
            snap_reason_t reason = s_snap_reason;
            // Gate: while a boot/commission (re)sync window is OPEN (g_boot_snapshot_sent
            // false), suppress HEARTBEAT/BOOT/COMMISSION until the window closes so we
            // never emit a value-incomplete snapshot. EVENT and FAST always pass:
            // EVENT carries post-event state already in the cache; FAST is the
            // deliberately-early valve-ready boot snapshot (incomplete-by-design,
            // refined afterward by incremental refresh).
            // Expressed through snap_window_suppressing() so this gate and the yield
            // check in snap_request() are literally the same rule. They used to be
            // separate inline expressions, and the scheduler consequently let a reason
            // this gate was suppressing keep ownership of the deadline — see the `yields`
            // comment in snap_request().
            bool gate_ok = (reason == SNAP_EVENT) || (reason == SNAP_FAST)
                           || !snap_window_suppressing();

            // Settle gate: a hub-issued valve command (auto-close, C2D
            // valve_open/close, override) is only QUEUED by ble_valve_*. Both
            // tasks are priority 5, so without this we can publish the event and
            // its coupled snapshot in the same loop iteration, before the command
            // is even written. The barrier ends when the write is issued, not at
            // the valve's report of the new value, which alone changes the cached
            // valve.state / rmleak (app_ble_valve.c): that report's BLE_UPD_STATE /
            // BLE_UPD_RMLEAK requests the snapshot that shows it (above). Applies
            // to every reason: a heartbeat landing mid-command is equally stale.
            //
            // Bounded by ble_valve_cmd_settling()'s own deadline; the +100 ms
            // deferral feeds the select timeout at the top of the loop. That re-poll
            // is what ends the deferral once the write is issued: the release posts
            // nothing to ble_update_queue. The valve's later BLE_UPD_STATE /
            // BLE_UPD_RMLEAK requests the follow-up snapshot.
            bool settling = gate_ok && ble_valve_cmd_settling();

            if (settling) {
                // INFO, not DEBUG: this fires only while a hub-issued valve command
                // is genuinely in flight, so it is rare — and it is the only direct
                // evidence that the settle barrier held a snapshot back. The build
                // compiles out ESP_LOGD (CONFIG_LOG_MAXIMUM_LEVEL=3), so at DEBUG it
                // would be invisible on the bench.
                ESP_LOGI(IOTHUB_TAG, "SNAP deferred — valve command settling");
                s_snap_due_ms = flush_now + 100;
            } else if (!gate_ok) {
                // Due but the (re)sync window is still open — defer ~one commission
                // poll instead of busy-spinning at 1 tick (the boot arming above will
                // pull the deadline in the instant the window closes).
                s_snap_due_ms = flush_now + 2000;
            } else {
                const char *rstr = snap_reason_str(reason);
                if (reason == SNAP_EVENT)
                    ESP_LOGI(IOTHUB_TAG, "SNAP trigger=event:%s", s_snap_evt);
                else
                    ESP_LOGI(IOTHUB_TAG, "SNAP trigger=%s", rstr);

                // Sampled BEFORE the build, so the snapshot reflects at least this much;
                // a change after the sample is re-requested on the next pass.
                uint32_t rs_pub = health_get_rating_seq();
                bool ok = telemetry_v2_publish_snapshot(rstr);
                if (ok) {
                    s_snap_last_pub_ms = flush_now;
                    // This snapshot already carries those rating changes: no duplicate.
                    s_rating_seq_seen  = rs_pub;
                    if (reason == SNAP_BOOT || reason == SNAP_COMMISSION) {
                        g_boot_snapshot_sent = true;
                        g_fast_snapshot_sent = true;   // flag hygiene: a boot/commission snapshot also satisfies the fast one-shot
                        uint8_t seen = 0, total = 0;
                        if (health_get_sync_counts(&seen, &total)) {
                            g_commission_pub_seen = seen;
                            if (seen >= total) g_commission_until_ms = 0;
                        }
                    } else if (reason == SNAP_FAST) {
                        // The fast snapshot IS the boot snapshot, fired early at
                        // valve-ready. Mark boot sent so the slow all-heard boot path
                        // does not double-publish, and OPEN the refresh grace window so
                        // the still-unheard sensors fill in via incremental refresh as
                        // each first beacons (capped by COMMISSION_REFRESH_GRACE_MS).
                        g_fast_snapshot_sent = true;
                        g_boot_snapshot_sent = true;
                        uint8_t seen = 0, total = 0;
                        if (health_get_sync_counts(&seen, &total)) {
                            g_commission_pub_seen = seen;
                            // Only open the grace window if devices are still unheard;
                            // if everything was already heard there is nothing to fill.
                            g_commission_until_ms = (seen >= total) ? 0
                                : (flush_now + COMMISSION_REFRESH_GRACE_MS);
                        } else {
                            g_commission_until_ms = flush_now + COMMISSION_REFRESH_GRACE_MS;
                        }
                    }
                    snap_rearm_heartbeat();   // also clears the retry backoff
                    ESP_LOGI(IOTHUB_TAG, "SNAP heartbeat=reset interval_ms=%lld",
                             (long long)s_hb_interval_ms);
                } else {
                    // Connected but publish failed (e.g. QoS-1 outbox full): back off
                    // RETRY_FLOOR for ALL reasons (the retry floor is honored by
                    // snap_request so BOOT/COMMISSION re-arms can't hammer the outbox).
                    s_snap_retry_until_ms = flush_now + SNAP_RETRY_FLOOR_MS;
                    s_snap_due_ms = s_snap_retry_until_ms;
                    ESP_LOGW(IOTHUB_TAG, "SNAP heartbeat=suppressed (publish-failed)");
                }
            }
        }
    }
}

void initialize_iothub(void)
{
    xTaskCreate(iothub_task, "iothub_task", 10240, NULL, 5, &iothub_task_handle);
}
