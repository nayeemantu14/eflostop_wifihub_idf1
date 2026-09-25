#include "offline_buffer.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define OB_TAG       "OFFLINE_BUF"
#define OB_NAMESPACE "offline_buf"
#define OB_KEY_HEAD  "head"
#define OB_KEY_TAIL  "tail"
#define OB_KEY_COUNT "count"

#define OB_LOCK_TIMEOUT_MS 1000

static uint8_t s_head  = 0;   // Next write index
static uint8_t s_tail  = 0;   // Next read index
static uint8_t s_count = 0;   // Number of valid entries
static bool    s_ready = false;

// store() runs on whichever task publishes an event while offline (iothub_task, or the
// esp-mqtt task for a cmd_ack); drain and clear run on iothub_task. head/tail/count and
// the NVS slots are one ring, so every entry point holds this lock (N18). Static storage:
// no heap. On a timeout each call fails safe (nothing stored, nothing drained, count 0)
// rather than touching the ring unlocked.
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock = NULL;

static bool ob_lock(const char *what)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(OB_LOCK_TIMEOUT_MS)) == pdTRUE) {
        return true;
    }
    ESP_LOGW(OB_TAG, "%s: buffer busy for %d ms - skipped", what, OB_LOCK_TIMEOUT_MS);
    return false;
}

static void ob_unlock(void)
{
    xSemaphoreGive(s_lock);
}

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
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);   // static storage: cannot fail
    }
    // Loaded under the lock so a task that sees s_ready also sees the loaded ring. Nothing
    // else takes the lock before s_ready is set, so this cannot time out in practice; if
    // it did, the buffer simply stays disabled.
    if (!ob_lock("init")) return;

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

    ob_unlock();
}

// Call with s_lock held.
static bool store_locked(const char *json, size_t len)
{
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

bool offline_buffer_store(const char *json, size_t len)
{
    if (!s_ready || !json || len == 0) return false;

    // Refused whole: cutting an event at the slot size stored invalid JSON, which the
    // replay then published as a message nothing downstream could parse (L17/N18).
    if (len > OFFLINE_BUF_MAX_JSON_LEN) {
        ESP_LOGW(OB_TAG, "Event too large (%u bytes, max %d) - not buffered",
                 (unsigned)len, OFFLINE_BUF_MAX_JSON_LEN);
        return false;
    }

    if (!ob_lock("store")) return false;
    bool ok = store_locked(json, len);
    ob_unlock();
    return ok;
}

// Call with s_lock held and s_count > 0.
static int drain_locked(esp_mqtt_client_handle_t client, const char *topic)
{
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

int offline_buffer_drain(esp_mqtt_client_handle_t client, const char *topic)
{
    if (!s_ready || !client || !topic) return 0;

    // The lock is held across the replay publishes. A store() that arrives meanwhile
    // waits up to OB_LOCK_TIMEOUT_MS and is then dropped (logged), never interleaved.
    if (!ob_lock("drain")) return 0;
    int published = (s_count > 0) ? drain_locked(client, topic) : 0;
    ob_unlock();
    return published;
}

int offline_buffer_count(void)
{
    if (!s_ready) return 0;
    if (!ob_lock("count")) return 0;
    int n = s_count;
    ob_unlock();
    return n;
}

// Call with s_lock held.
static void clear_locked(void)
{
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

void offline_buffer_clear(void)
{
    if (!s_ready) return;
    if (!ob_lock("clear")) return;
    clear_locked();
    ob_unlock();
}
