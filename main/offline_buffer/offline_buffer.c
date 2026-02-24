#include "offline_buffer.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define OB_TAG       "OFFLINE_BUF"
#define OB_NAMESPACE "offline_buf"
#define OB_KEY_HEAD  "head"
#define OB_KEY_TAIL  "tail"
#define OB_KEY_COUNT "count"

static uint8_t s_head  = 0;   // Next write index
static uint8_t s_tail  = 0;   // Next read index
static uint8_t s_count = 0;   // Number of valid entries
static bool    s_ready = false;

// ---------------------------------------------------------------------------
// NVS helpers
// ---------------------------------------------------------------------------

static void save_metadata(void)
{
    nvs_handle_t h;
    if (nvs_open(OB_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, OB_KEY_HEAD,  s_head);
    nvs_set_u8(h, OB_KEY_TAIL,  s_tail);
    nvs_set_u8(h, OB_KEY_COUNT, s_count);
    nvs_commit(h);
    nvs_close(h);
}

static void make_key(uint8_t index, char *buf, size_t buf_len)
{
    snprintf(buf, buf_len, "ob_%02u", (unsigned)index);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void offline_buffer_init(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(OB_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_OK) {
        nvs_get_u8(h, OB_KEY_HEAD,  &s_head);
        nvs_get_u8(h, OB_KEY_TAIL,  &s_tail);
        nvs_get_u8(h, OB_KEY_COUNT, &s_count);
        nvs_close(h);
    }

    // Sanity check: reset if corrupt
    if (s_head >= OFFLINE_BUF_MAX_ENTRIES ||
        s_tail >= OFFLINE_BUF_MAX_ENTRIES ||
        s_count > OFFLINE_BUF_MAX_ENTRIES) {
        ESP_LOGW(OB_TAG, "Corrupt metadata (h=%u t=%u c=%u), resetting",
                 s_head, s_tail, s_count);
        s_head = s_tail = s_count = 0;
        save_metadata();
    }

    s_ready = true;

    if (s_count > 0) {
        ESP_LOGI(OB_TAG, "Init: %d buffered event(s) pending from before reboot",
                 s_count);
    } else {
        ESP_LOGI(OB_TAG, "Init: buffer empty");
    }
}

bool offline_buffer_store(const char *json, size_t len)
{
    if (!s_ready || !json || len == 0) return false;

    if (len > OFFLINE_BUF_MAX_JSON_LEN) {
        ESP_LOGW(OB_TAG, "Event too large (%u bytes, max %d), truncating",
                 (unsigned)len, OFFLINE_BUF_MAX_JSON_LEN);
        len = OFFLINE_BUF_MAX_JSON_LEN;
    }

    nvs_handle_t h;
    if (nvs_open(OB_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(OB_TAG, "NVS open failed");
        return false;
    }

    char key[8];
    make_key(s_head, key, sizeof(key));

    esp_err_t err = nvs_set_blob(h, key, json, len);
    if (err != ESP_OK) {
        ESP_LOGE(OB_TAG, "NVS write '%s' failed: %s", key, esp_err_to_name(err));
        nvs_close(h);
        return false;
    }

    // Advance head
    s_head = (s_head + 1) % OFFLINE_BUF_MAX_ENTRIES;

    // If buffer was full, advance tail (overwrite oldest)
    if (s_count == OFFLINE_BUF_MAX_ENTRIES) {
        s_tail = (s_tail + 1) % OFFLINE_BUF_MAX_ENTRIES;
        ESP_LOGW(OB_TAG, "Buffer full, oldest event overwritten");
    } else {
        s_count++;
    }

    // Persist metadata
    nvs_set_u8(h, OB_KEY_HEAD,  s_head);
    nvs_set_u8(h, OB_KEY_TAIL,  s_tail);
    nvs_set_u8(h, OB_KEY_COUNT, s_count);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(OB_TAG, "Stored event [%s] (%u bytes), %d buffered",
             key, (unsigned)len, s_count);
    return true;
}

int offline_buffer_drain(esp_mqtt_client_handle_t client, const char *topic)
{
    if (!s_ready || s_count == 0 || !client || !topic) return 0;

    ESP_LOGI(OB_TAG, "Draining %d buffered event(s)...", s_count);

    nvs_handle_t h;
    if (nvs_open(OB_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(OB_TAG, "NVS open failed for drain");
        return 0;
    }

    int published = 0;
    char buf[OFFLINE_BUF_MAX_JSON_LEN + 1];

    while (s_count > 0) {
        char key[8];
        make_key(s_tail, key, sizeof(key));

        size_t len = OFFLINE_BUF_MAX_JSON_LEN;
        esp_err_t err = nvs_get_blob(h, key, buf, &len);
        if (err != ESP_OK) {
            ESP_LOGW(OB_TAG, "Read '%s' failed: %s, skipping",
                     key, esp_err_to_name(err));
        } else {
            buf[len] = '\0';
            int msg_id = esp_mqtt_client_publish(client, topic, buf, (int)len, 1, 0);
            if (msg_id >= 0) {
                published++;
                ESP_LOGI(OB_TAG, "Replayed [%s] (%u bytes)", key, (unsigned)len);
            } else {
                ESP_LOGW(OB_TAG, "MQTT publish failed for [%s], stopping drain", key);
                break;
            }
        }

        // Erase this slot and advance tail
        nvs_erase_key(h, key);
        s_tail = (s_tail + 1) % OFFLINE_BUF_MAX_ENTRIES;
        s_count--;
    }

    // Persist updated metadata
    nvs_set_u8(h, OB_KEY_HEAD,  s_head);
    nvs_set_u8(h, OB_KEY_TAIL,  s_tail);
    nvs_set_u8(h, OB_KEY_COUNT, s_count);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(OB_TAG, "Drain complete: %d event(s) published, %d remaining",
             published, s_count);
    return published;
}

int offline_buffer_count(void)
{
    return s_count;
}

void offline_buffer_clear(void)
{
    if (!s_ready) return;

    nvs_handle_t h;
    if (nvs_open(OB_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;

    // Erase all slot keys
    for (uint8_t i = 0; i < OFFLINE_BUF_MAX_ENTRIES; i++) {
        char key[8];
        make_key(i, key, sizeof(key));
        nvs_erase_key(h, key);
    }

    s_head = s_tail = s_count = 0;
    nvs_set_u8(h, OB_KEY_HEAD,  0);
    nvs_set_u8(h, OB_KEY_TAIL,  0);
    nvs_set_u8(h, OB_KEY_COUNT, 0);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(OB_TAG, "Buffer cleared");
}
