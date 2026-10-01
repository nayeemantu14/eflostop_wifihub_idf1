#ifndef DPS_CLIENT_H
#define DPS_CLIENT_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DPS_TAG "DPS"

typedef struct {
    char hub_hostname[128];   // e.g. "wd-core-iothub-poc.azure-devices.net"
    char device_id[64];       // e.g. "GW-34B7DA6AAD54"
    char device_key[64];      // base64-encoded derived key for SAS token generation
} dps_assignment_t;

/**
 * Try NVS cache first; if not cached, perform DPS registration over MQTT.
 * Blocks until complete or fails, for up to 60 s.
 *
 * @param id_scope        DPS ID scope (e.g. "0neXXXXXXXX")
 * @param group_key       Base64-encoded group enrollment primary key
 * @param registration_id Device registration ID (e.g. "GW-34B7DA6AAD54")
 * @param out             Filled on success with assigned hub, device ID, and derived key
 * @param keep_going      Abort hook, or NULL: asked about every second while a live
 *                        registration waits (on the caller's task). false stops and
 *                        destroys the registration's MQTT client and returns
 *                        ESP_ERR_INVALID_STATE. Asked once more after a registration that
 *                        ended without an assignment: false then makes it an abort too (a
 *                        link loss that ended the session itself). iothub_task passes its
 *                        cloud admission (2.1.4 WP2: no DPS TLS session while the SoftAP is up).
 * @return ESP_OK on success; ESP_ERR_INVALID_STATE when the registration did not run or
 *         was aborted (no valid clock, or keep_going said stop): not a failed attempt;
 *         ESP_ERR_NO_MEM or ESP_FAIL on registration failure
 */
esp_err_t dps_register(const char *id_scope, const char *group_key,
                       const char *registration_id, dps_assignment_t *out,
                       bool (*keep_going)(void));

/**
 * Clear the NVS DPS cache (forces re-registration on next boot).
 */
esp_err_t dps_clear_cache(void);

#ifdef __cplusplus
}
#endif

#endif // DPS_CLIENT_H
