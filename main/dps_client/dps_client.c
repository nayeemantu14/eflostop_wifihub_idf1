#include "dps_client.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "app_iothub.h"  // url_encode(), generate_sas_token()

// ---------------------------------------------------------------------------
// NVS cache
// ---------------------------------------------------------------------------

#define NVS_NAMESPACE   "dps_cache"
#define NVS_KEY_HUB     "hub_host"
#define NVS_KEY_DEV_ID  "dev_id"
#define NVS_KEY_DEV_KEY "dev_key"
#define NVS_KEY_CACHED  "cached"

// ---------------------------------------------------------------------------
// DPS protocol constants
// ---------------------------------------------------------------------------

#define DPS_GLOBAL_ENDPOINT  "global.azure-devices-provisioning.net"
#define DPS_API_VERSION      "2019-03-31"
#define DPS_POLL_INTERVAL_MS 3000
#define DPS_TIMEOUT_MS       60000
#define DPS_SAS_EXPIRY_SEC   3600   // 1 hour

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------

typedef enum {
    DPS_STATE_IDLE,
    DPS_STATE_CONNECTING,
    DPS_STATE_SUBSCRIBING,
    DPS_STATE_REGISTERING,
    DPS_STATE_POLLING,
    DPS_STATE_DONE,
    DPS_STATE_FAILED
} dps_state_t;

// Context shared between caller and MQTT event handler
typedef struct {
    dps_state_t      state;
    SemaphoreHandle_t done_sem;
    char             operation_id[128];
    dps_assignment_t result;
    const char      *id_scope;
    const char      *registration_id;
    esp_mqtt_client_handle_t client;
    int              poll_rid;
} dps_ctx_t;

static dps_ctx_t s_ctx;

// ---------------------------------------------------------------------------
// NVS helpers
// ---------------------------------------------------------------------------

static esp_err_t nvs_load_cache(dps_assignment_t *out)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    uint8_t cached = 0;
    err = nvs_get_u8(h, NVS_KEY_CACHED, &cached);
    if (err != ESP_OK || cached != 1) {
        nvs_close(h);
        return ESP_ERR_NOT_FOUND;
    }

    size_t len;
    len = sizeof(out->hub_hostname);
    err = nvs_get_str(h, NVS_KEY_HUB, out->hub_hostname, &len);
    if (err != ESP_OK) { nvs_close(h); return err; }

    len = sizeof(out->device_id);
    err = nvs_get_str(h, NVS_KEY_DEV_ID, out->device_id, &len);
    if (err != ESP_OK) { nvs_close(h); return err; }

    len = sizeof(out->device_key);
    err = nvs_get_str(h, NVS_KEY_DEV_KEY, out->device_key, &len);
    if (err != ESP_OK) { nvs_close(h); return err; }

    nvs_close(h);
    return ESP_OK;
}

static esp_err_t nvs_save_cache(const dps_assignment_t *a)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    nvs_set_str(h, NVS_KEY_HUB, a->hub_hostname);
    nvs_set_str(h, NVS_KEY_DEV_ID, a->device_id);
    nvs_set_str(h, NVS_KEY_DEV_KEY, a->device_key);
    nvs_set_u8(h, NVS_KEY_CACHED, 1);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(DPS_TAG, "Cached assignment in NVS");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Key derivation: derive per-device key from group enrollment key
// ---------------------------------------------------------------------------

static esp_err_t derive_device_key(const char *group_key_b64,
                                   const char *registration_id,
                                   char *out_key_b64, size_t out_len)
{
    // Decode group key from base64
    unsigned char group_key_raw[64];
    size_t group_key_raw_len = 0;
    int ret = mbedtls_base64_decode(group_key_raw, sizeof(group_key_raw),
                                    &group_key_raw_len,
                                    (const unsigned char *)group_key_b64,
                                    strlen(group_key_b64));
    if (ret != 0) {
        ESP_LOGE(DPS_TAG, "Failed to decode group key (ret=%d)", ret);
        return ESP_FAIL;
    }

    // HMAC-SHA256(group_key, registration_id)
    unsigned char hmac[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, group_key_raw, group_key_raw_len);
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)registration_id,
                           strlen(registration_id));
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);

    // Base64 encode the derived key
    size_t b64_len = 0;
    ret = mbedtls_base64_encode((unsigned char *)out_key_b64, out_len,
                                &b64_len, hmac, 32);
    if (ret != 0) {
        ESP_LOGE(DPS_TAG, "Failed to encode derived key (ret=%d)", ret);
        return ESP_FAIL;
    }
    out_key_b64[b64_len] = '\0';

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Parse DPS response JSON
// ---------------------------------------------------------------------------

static void parse_dps_response(const char *data, int data_len, int status_code)
{
    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (!root) {
        ESP_LOGE(DPS_TAG, "Failed to parse DPS response JSON");
        s_ctx.state = DPS_STATE_FAILED;
        xSemaphoreGive(s_ctx.done_sem);
        return;
    }

    if (status_code == 202 || status_code == 200) {
        // Check if we have an operationId (means still in progress or just started)
        cJSON *op_id = cJSON_GetObjectItem(root, "operationId");
        if (op_id && cJSON_IsString(op_id)) {
            strncpy(s_ctx.operation_id, op_id->valuestring,
                    sizeof(s_ctx.operation_id) - 1);
        }

        // Check for registration state (final assignment)
        cJSON *reg_state = cJSON_GetObjectItem(root, "registrationState");
        if (reg_state) {
            cJSON *assigned_hub = cJSON_GetObjectItem(reg_state, "assignedHub");
            cJSON *device_id = cJSON_GetObjectItem(reg_state, "deviceId");

            if (assigned_hub && cJSON_IsString(assigned_hub) &&
                device_id && cJSON_IsString(device_id) &&
                strlen(assigned_hub->valuestring) > 0) {
                // Got final assignment
                strncpy(s_ctx.result.hub_hostname, assigned_hub->valuestring,
                        sizeof(s_ctx.result.hub_hostname) - 1);
                strncpy(s_ctx.result.device_id, device_id->valuestring,
                        sizeof(s_ctx.result.device_id) - 1);
                ESP_LOGI(DPS_TAG, "Assigned hub=%s device=%s",
                         s_ctx.result.hub_hostname, s_ctx.result.device_id);
                s_ctx.state = DPS_STATE_DONE;
                xSemaphoreGive(s_ctx.done_sem);
                cJSON_Delete(root);
                return;
            }
        }

        // Not final yet — need to poll
        if (s_ctx.state == DPS_STATE_REGISTERING || s_ctx.state == DPS_STATE_POLLING) {
            s_ctx.state = DPS_STATE_POLLING;
            ESP_LOGI(DPS_TAG, "Registration in progress, polling (opId=%s)...",
                     s_ctx.operation_id);
        }
    } else {
        // Error response
        cJSON *msg = cJSON_GetObjectItem(root, "message");
        ESP_LOGE(DPS_TAG, "DPS error %d: %s", status_code,
                 (msg && cJSON_IsString(msg)) ? msg->valuestring : "unknown");
        s_ctx.state = DPS_STATE_FAILED;
        xSemaphoreGive(s_ctx.done_sem);
    }

    cJSON_Delete(root);
}

// ---------------------------------------------------------------------------
// Extract HTTP-like status code from DPS response topic
// e.g. "$dps/registrations/res/200/?$rid=2" → 200
// ---------------------------------------------------------------------------

static int extract_status_from_topic(const char *topic, int topic_len)
{
    const char *prefix = "$dps/registrations/res/";
    size_t prefix_len = strlen(prefix);
    if (topic_len < (int)(prefix_len + 3)) return -1;
    if (strncmp(topic, prefix, prefix_len) != 0) return -1;
    return atoi(topic + prefix_len);
}

// ---------------------------------------------------------------------------
// DPS MQTT event handler
// ---------------------------------------------------------------------------

static void dps_mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                   int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(DPS_TAG, "Connected to DPS endpoint");
        s_ctx.state = DPS_STATE_SUBSCRIBING;
        esp_mqtt_client_subscribe(s_ctx.client, "$dps/registrations/res/#", 1);
        break;

    case MQTT_EVENT_SUBSCRIBED:
    {
        if (s_ctx.state != DPS_STATE_SUBSCRIBING) break;
        s_ctx.state = DPS_STATE_REGISTERING;

        // Publish registration request
        char topic[128];
        snprintf(topic, sizeof(topic),
                 "$dps/registrations/PUT/iotdps-register/?$rid=1");

        char payload[128];
        snprintf(payload, sizeof(payload),
                 "{\"registrationId\":\"%s\"}", s_ctx.registration_id);

        ESP_LOGI(DPS_TAG, "Submitting registration for '%s'...",
                 s_ctx.registration_id);
        esp_mqtt_client_publish(s_ctx.client, topic, payload, 0, 1, 0);
        break;
    }

    case MQTT_EVENT_DATA:
    {
        if (!event->topic || event->topic_len == 0) break;

        int status = extract_status_from_topic(event->topic, event->topic_len);
        if (status < 0) break;

        ESP_LOGI(DPS_TAG, "DPS response status=%d", status);
        parse_dps_response(event->data, event->data_len, status);

        // If still polling, schedule next poll after delay
        if (s_ctx.state == DPS_STATE_POLLING && s_ctx.operation_id[0] != '\0') {
            vTaskDelay(pdMS_TO_TICKS(DPS_POLL_INTERVAL_MS));

            s_ctx.poll_rid++;
            char poll_topic[256];
            snprintf(poll_topic, sizeof(poll_topic),
                     "$dps/registrations/GET/iotdps-get-operationstatus/"
                     "?$rid=%d&operationId=%s",
                     s_ctx.poll_rid, s_ctx.operation_id);
            esp_mqtt_client_publish(s_ctx.client, poll_topic, NULL, 0, 1, 0);
        }
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(DPS_TAG, "Disconnected from DPS endpoint");
        if (s_ctx.state != DPS_STATE_DONE) {
            s_ctx.state = DPS_STATE_FAILED;
            xSemaphoreGive(s_ctx.done_sem);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(DPS_TAG, "MQTT error during DPS registration");
        if (s_ctx.state != DPS_STATE_DONE) {
            s_ctx.state = DPS_STATE_FAILED;
            xSemaphoreGive(s_ctx.done_sem);
        }
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t dps_register(const char *id_scope, const char *group_key,
                       const char *registration_id, dps_assignment_t *out)
{
    // 1. Try NVS cache first
    if (nvs_load_cache(out) == ESP_OK) {
        ESP_LOGI(DPS_TAG, "Loaded cached assignment from NVS");
        ESP_LOGI(DPS_TAG, "hub=%s device=%s", out->hub_hostname, out->device_id);
        return ESP_OK;
    }

    ESP_LOGI(DPS_TAG, "No cached assignment, performing DPS registration...");

    // 2. Derive per-device key
    char derived_key[64];
    if (derive_device_key(group_key, registration_id,
                          derived_key, sizeof(derived_key)) != ESP_OK) {
        return ESP_FAIL;
    }
    ESP_LOGI(DPS_TAG, "Derived device key for '%s'", registration_id);

    // 3. Generate DPS SAS token
    char resource_uri[256];
    snprintf(resource_uri, sizeof(resource_uri),
             "%s/registrations/%s", id_scope, registration_id);
    char *sas_token = generate_sas_token(resource_uri, derived_key, DPS_SAS_EXPIRY_SEC);
    if (!sas_token) {
        ESP_LOGE(DPS_TAG, "Failed to generate DPS SAS token");
        return ESP_FAIL;
    }

    // 4. Prepare context
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.state = DPS_STATE_CONNECTING;
    s_ctx.done_sem = xSemaphoreCreateBinary();
    s_ctx.id_scope = id_scope;
    s_ctx.registration_id = registration_id;
    s_ctx.poll_rid = 2;  // rid=1 used for initial register

    // 5. Configure MQTT for DPS
    char uri[128];
    snprintf(uri, sizeof(uri), "mqtts://%s", DPS_GLOBAL_ENDPOINT);

    char username[256];
    snprintf(username, sizeof(username),
             "%s/registrations/%s/api-version=%s",
             id_scope, registration_id, DPS_API_VERSION);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials = {
            .username = username,
            .client_id = registration_id,
            .authentication = {.password = sas_token},
        },
        .session.keepalive = 30,
    };

    ESP_LOGI(DPS_TAG, "Connecting to %s...", DPS_GLOBAL_ENDPOINT);
    s_ctx.client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_ctx.client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                   dps_mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_ctx.client);

    // 6. Block until done or timeout
    bool got_sem = xSemaphoreTake(s_ctx.done_sem, pdMS_TO_TICKS(DPS_TIMEOUT_MS));

    // 7. Cleanup MQTT client
    esp_mqtt_client_stop(s_ctx.client);
    esp_mqtt_client_destroy(s_ctx.client);
    s_ctx.client = NULL;
    vSemaphoreDelete(s_ctx.done_sem);
    s_ctx.done_sem = NULL;
    free(sas_token);

    if (!got_sem || s_ctx.state != DPS_STATE_DONE) {
        ESP_LOGE(DPS_TAG, "DPS registration failed (state=%d, timeout=%s)",
                 s_ctx.state, got_sem ? "no" : "yes");
        return ESP_FAIL;
    }

    // 8. Store derived key in result and save to NVS
    strncpy(s_ctx.result.device_key, derived_key, sizeof(s_ctx.result.device_key) - 1);
    memcpy(out, &s_ctx.result, sizeof(dps_assignment_t));
    nvs_save_cache(out);

    return ESP_OK;
}

esp_err_t dps_clear_cache(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(DPS_TAG, "DPS cache cleared");
    return ESP_OK;
}
