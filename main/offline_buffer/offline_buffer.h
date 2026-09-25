#ifndef OFFLINE_BUFFER_H
#define OFFLINE_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include "mqtt_client.h"

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
 * nothing, count returns 0, clear does nothing - and log it.
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
 * @brief Store an event built before the first clock sync: its envelope "ts" is the
 *        unsynced time() value. Same rules and return value as offline_buffer_store();
 *        the slot is also marked, in RAM only, as stored this boot, so the drain can
 *        stamp the event with its real time once the clock has synced.
 */
bool offline_buffer_store_presync(const char *json, size_t len);

/**
 * @brief Drain all buffered events by publishing via MQTT.
 *        Publishes FIFO (oldest first), clears entries from NVS.
 *
 *        An entry whose top-level "ts" is below the synced-clock threshold is a pre-sync
 *        event not yet stamped by offline_buffer_stamp_presync(). Stored this boot, it is
 *        published with ts rewritten to now - (uptime now - its gateway.uptime_s). Left by
 *        a restart that came before the clock synced, or with no usable uptime_s, it is
 *        dropped (logged): its real time can never be known. Should the clock still be
 *        unsynced, the drain stops there and keeps it.
 *
 * @param client  MQTT client handle
 * @param topic   MQTT topic string
 * @return Number of events published (0 on lock timeout)
 */
int offline_buffer_drain(esp_mqtt_client_handle_t client, const char *topic);

/**
 * @brief Stamp every pre-sync event stored this boot with its real time, in NVS, once the
 *        clock has synced (same rewrite as the drain). Call once, when the clock first
 *        syncs: the proof that an entry belongs to this boot is RAM only, so without this a
 *        restart between the sync and the next drain would lose those events. Entries it
 *        cannot stamp are left for the drain. No-op while the clock is unsynced.
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

#ifdef __cplusplus
}
#endif

#endif // OFFLINE_BUFFER_H
