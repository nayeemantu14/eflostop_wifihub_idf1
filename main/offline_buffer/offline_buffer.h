#ifndef OFFLINE_BUFFER_H
#define OFFLINE_BUFFER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OFFLINE_BUF_MAX_ENTRIES  16
#define OFFLINE_BUF_MAX_JSON_LEN 512

/**
 * @brief Initialize offline buffer from NVS.
 *        Loads head/tail/count; resets if corrupt.
 *        Call once from iothub_task before event loop.
 */
void offline_buffer_init(void);

/*
 * store / drain / count / clear may be called from any task: each holds the buffer's
 * mutex (1000 ms). On a timeout they fail safe - store returns false, drain publishes
 * nothing, count returns 0, clear does nothing - and log it. try_store does not wait.
 * Since 2.1.4 WP2c the waiting calls run on cloud_tx (app_iothub.c), never on iothub_task,
 * which evaluates the leaks; the esp-mqtt task uses try_store().
 */

/**
 * @brief Store a JSON telemetry string in NVS ring buffer.
 *        Overwrites oldest entry if buffer is full.
 *
 * @param json  Null-terminated JSON string
 * @param len   Length of json (excluding null terminator)
 * @return true on success; false on NVS error, lock timeout, or json longer than
 *         OFFLINE_BUF_MAX_JSON_LEN (refused whole, never truncated into invalid JSON)
 */
bool offline_buffer_store(const char *json, size_t len);

/**
 * @brief offline_buffer_store() that does not wait for the buffer's mutex: false at once
 *        (logged) while another task holds it. For the esp-mqtt task (2.1.4 WP2), which
 *        stores from inside its event handler with esp-mqtt's API lock held: the drain holds
 *        the mutex while it waits for that lock in esp_mqtt_client_publish(), so a wait here
 *        would stall both tasks for the whole timeout and store nothing.
 */
bool offline_buffer_try_store(const char *json, size_t len);

/**
 * @brief Store an event built before the first clock sync: its envelope "ts" is the
 *        unsynced time() value. Same rules and return value as offline_buffer_store();
 *        the slot is also marked, in RAM only, as stored this boot, so the drain can
 *        stamp the event with its real time once the clock has synced.
 */
bool offline_buffer_store_presync(const char *json, size_t len);

/**
 * What a drain's publish callback did with one entry (offline_buffer_drain()).
 */
typedef enum {
    OFFLINE_BUF_PUB_TAKEN,    // published (msg_id >= 0), the session still up after it: erased
    OFFLINE_BUF_PUB_FAILED,   // the session was up, but the publish was not taken (msg_id < 0):
                              // kept, with the rest; the drain stops
    OFFLINE_BUF_PUB_DOWN,     // the session was not up, before the publish or after it: kept,
                              // with the rest; the drain stops
} offline_buffer_pub_t;

/**
 * Publishes one replayed entry: `json` is NUL-terminated, `len` its length. The caller of
 * offline_buffer_drain() owns the MQTT client, its topic and the session's state, so this
 * module makes no MQTT call itself (2.1.4 WP2c).
 */
typedef offline_buffer_pub_t (*offline_buffer_publish_fn)(const char *json, size_t len);

/**
 * @brief Drain all buffered events through `publish`.
 *        Publishes FIFO (oldest first), clears entries from NVS.
 *
 *        An entry whose top-level "ts" is below the synced-clock threshold is a pre-sync
 *        event not yet stamped by offline_buffer_stamp_presync(). Stored this boot, it is
 *        published with ts rewritten to now - (uptime now - its gateway.uptime_s). Left by
 *        a restart that came before the clock synced, or with no usable uptime_s, it is
 *        dropped (logged): its real time can never be known. Should the clock still be
 *        unsynced, the drain stops there and keeps it.
 *
 *        An entry is published only while the MQTT session is up, and erased only if it is
 *        still up after the publish (`publish` decides both: OFFLINE_BUF_PUB_DOWN). A
 *        session that ended mid-drain (the link lost, a failed read on the esp-mqtt task)
 *        still takes a QoS 1 publish into its outbox and returns a msg_id, but the stop at a
 *        link loss deletes that outbox, and it expires after 30 s anyway: the drain stops
 *        there instead and keeps that entry and the rest for the next connect's drain. One
 *        kept although it did reach the broker is sent again then (a QoS 1 duplicate, never
 *        a loss).
 *
 * @param publish  publishes one entry and reports the session (telemetry_v2's replay)
 * @return Number of events published (0 on lock timeout)
 */
int offline_buffer_drain(offline_buffer_publish_fn publish);

/**
 * @brief The number of events buffered, read without the buffer's mutex (2.1.4 WP2c). A hint
 *        for "is anything waiting?", never waits: it can be one store or one drained entry
 *        behind another task's. 0 before offline_buffer_init().
 */
int offline_buffer_pending(void);

/**
 * @brief Stamp every pre-sync event stored this boot with its real time, in NVS, once the
 *        clock has synced (same rewrite as the drain). Call once, when the clock first
 *        syncs: the proof that an entry belongs to this boot is RAM only, so without this a
 *        restart between the sync and the next drain would lose those events. Entries it
 *        cannot stamp are left for the drain, and so is one that would then be longer than
 *        OFFLINE_BUF_MAX_JSON_LEN: no slot is written back longer than an older build can
 *        read after a rollback. No-op while the clock is unsynced.
 */
void offline_buffer_stamp_presync(void);

/**
 * @brief Return number of events currently buffered (0 on lock timeout).
 */
int offline_buffer_count(void);

/**
 * @brief Clear all buffered events from NVS.
 */
void offline_buffer_clear(void);

/**
 * @brief Erase the buffer's NVS namespace WITHOUT its mutex, and leave its RAM state alone:
 *        only for a path that restarts right after (2.1.4 WP2c: a decommission whose clear
 *        the sender, cloud_tx, did not finish in time, R1-7). A drain still running can only
 *        rewrite the ring's metadata, which then points at erased slots: the next boot reads
 *        them as missing ("Read ... failed, skipping") and replays nothing. Never waits.
 */
void offline_buffer_erase_for_restart(void);

#ifdef __cplusplus
}
#endif

#endif // OFFLINE_BUFFER_H
