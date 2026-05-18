#ifndef HUB_IDENTITY_H
#define HUB_IDENTITY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HUB_NAME_MAX_LEN  31   /* max chars (excluding null terminator) */

/**
 * @brief Initialise hub identity module.
 *        Reads WiFi STA MAC from eFuse to derive Gateway ID and Short ID,
 *        then loads the user-assigned hub name from NVS.
 *        Call after nvs_flash_init(), before app_wifi_start().
 */
bool hub_identity_init(void);

/** @return "GW-XXXXXXXXXXXX" — derived from WiFi STA MAC, never changes. */
const char *hub_identity_get_gateway_id(void);

/** @return "XXXX" — last 4 hex chars of Gateway ID. Used in AP SSID. */
const char *hub_identity_get_short_id(void);

/** @return User-assigned hub name, or "" if not set. */
const char *hub_identity_get_name(void);

/**
 * @brief Set/update the user-assigned hub name (persisted to NVS).
 * @param name  UTF-8 string, max HUB_NAME_MAX_LEN chars. NULL or "" clears.
 * @return true on success.
 */
bool hub_identity_set_name(const char *name);

/** @brief Erase hub name from NVS (called on decommission_all). */
void hub_identity_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* HUB_IDENTITY_H */
