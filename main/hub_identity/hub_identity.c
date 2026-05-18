/****************************************************
 *  MODULE:   Hub Identity
 *  PURPOSE:  Gateway ID, Short ID and user-assigned hub name.
 *            Gateway ID + Short ID are derived from WiFi STA MAC (eFuse).
 *            Hub name is persisted in NVS and set via Device Twin desired.
 ****************************************************/

#include "hub_identity.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"

#define TAG          "HUB_IDENT"
#define NVS_NS       "hub_ident"
#define NVS_KEY_NAME "hub_name"

static char s_gateway_id[32];                    /* "GW-XXXXXXXXXXXX" */
static char s_short_id[8];                       /* "XXXX"            */
static char s_hub_name[HUB_NAME_MAX_LEN + 1];   /* user-friendly     */

/* ------------------------------------------------------------------ */
bool hub_identity_init(void)
{
    /* --- derive Gateway ID and Short ID from WiFi STA MAC (eFuse) --- */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    snprintf(s_gateway_id, sizeof(s_gateway_id),
             "GW-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    snprintf(s_short_id, sizeof(s_short_id), "%02X%02X", mac[4], mac[5]);

    ESP_LOGI(TAG, "Gateway ID : %s", s_gateway_id);
    ESP_LOGI(TAG, "Short ID   : %s", s_short_id);

    /* --- load hub name from NVS --- */
    s_hub_name[0] = '\0';

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_hub_name);
        if (nvs_get_str(h, NVS_KEY_NAME, s_hub_name, &len) != ESP_OK)
            s_hub_name[0] = '\0';
        nvs_close(h);
    }

    if (s_hub_name[0])
        ESP_LOGI(TAG, "Hub name   : %s", s_hub_name);
    else
        ESP_LOGI(TAG, "Hub name   : (not set)");

    return true;
}

/* ------------------------------------------------------------------ */
const char *hub_identity_get_gateway_id(void)
{
    return s_gateway_id;
}

const char *hub_identity_get_short_id(void)
{
    return s_short_id;
}

const char *hub_identity_get_name(void)
{
    return s_hub_name;
}

/* ------------------------------------------------------------------ */
bool hub_identity_set_name(const char *name)
{
    if (!name || name[0] == '\0') {
        /* treat empty string as "clear" */
        hub_identity_clear();
        return true;
    }

    if (strlen(name) > HUB_NAME_MAX_LEN) {
        ESP_LOGW(TAG, "Hub name too long (%d chars, max %d)",
                 (int)strlen(name), HUB_NAME_MAX_LEN);
        return false;
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK)
        return false;

    esp_err_t err = nvs_set_str(h, NVS_KEY_NAME, name);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) {
        strncpy(s_hub_name, name, HUB_NAME_MAX_LEN);
        s_hub_name[HUB_NAME_MAX_LEN] = '\0';
        ESP_LOGI(TAG, "Hub name set: %s", s_hub_name);
        return true;
    }

    ESP_LOGE(TAG, "NVS write failed: %s", esp_err_to_name(err));
    return false;
}

/* ------------------------------------------------------------------ */
void hub_identity_clear(void)
{
    s_hub_name[0] = '\0';

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY_NAME);
        nvs_commit(h);
        nvs_close(h);
    }

    ESP_LOGI(TAG, "Hub name cleared");
}
