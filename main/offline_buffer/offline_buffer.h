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

/**
 * @brief Store a JSON telemetry string in NVS ring buffer.
 *        Overwrites oldest entry if buffer is full.
 *
 * @param json  Null-terminated JSON string
 * @param len   Length of json (excluding null terminator)
 * @return true on success, false on NVS error or json too large
 */
bool offline_buffer_store(const char *json, size_t len);

/**
 * @brief Drain all buffered events by publishing via MQTT.
 *        Publishes FIFO (oldest first), clears entries from NVS.
 *
 * @param client  MQTT client handle
 * @param topic   MQTT topic string
 * @return Number of events published
 */
int offline_buffer_drain(esp_mqtt_client_handle_t client, const char *topic);

/**
 * @brief Return number of events currently buffered.
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
