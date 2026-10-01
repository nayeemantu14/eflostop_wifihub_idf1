#include "offline_buffer.h"
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"

#define OB_TAG       "OFFLINE_BUF"
#define OB_NAMESPACE "offline_buf"
#define OB_KEY_HEAD  "head"
#define OB_KEY_TAIL  "tail"
#define OB_KEY_COUNT "count"

#define OB_LOCK_TIMEOUT_MS 1000

// Minimum epoch that counts as a synced clock (2024-01-01 00:00:00 UTC). Must match
// EPOCH_VALID_THRESHOLD_TELEM in telemetry_v2.c, which decides what is a pre-sync event.
#define OB_EPOCH_VALID_THRESHOLD 1704067200

// Room for a pre-sync event's "ts" to grow when the drain stamps it: a few digits of
// unsynced time() become a 10-digit epoch.
#define OB_STAMP_MARGIN 16

static uint8_t s_head  = 0;   // Next write index
static uint8_t s_tail  = 0;   // Next read index
static uint8_t s_count = 0;   // Number of valid entries
static bool    s_ready = false;

// One bit per ring slot, RAM only: set = the slot holds a pre-sync event stored THIS boot,
// whose gateway.uptime_s is on this boot's esp_timer, so the drain can work out its real
// time once the clock has synced. 0 at every boot, so a pre-sync event left in NVS by an
// earlier power cycle reads as clear: its real time can never be known and it is dropped.
// Indexed by slot, not by position from the tail, so it stays aligned when a drain stops
// part-way. Every update is made with s_lock held.
_Static_assert(OFFLINE_BUF_MAX_ENTRIES <= 16, "s_presync_mask has one bit per ring slot");
static uint16_t s_presync_mask = 0;

// store() runs on whichever task publishes an event while offline (iothub_task, or the
// esp-mqtt task for a cmd_ack; since 2.1.4 WP2 that task uses try_store(), for a cmd_ack
// the outbox refused for room); drain and clear run on iothub_task. head/tail/count and
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
static bool store_locked(const char *json, size_t len, bool presync)
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

    // The slot now holds THIS entry, whatever it held before - including the oldest entry
    // when the ring is full and is being overwritten below - so its bit follows the new one.
    uint16_t bit = (uint16_t)(1u << s_head);
    if (presync) s_presync_mask |= bit;
    else         s_presync_mask &= (uint16_t)~bit;

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

// wait = false: the lock is not waited for (offline_buffer_try_store()).
static bool store_common(const char *json, size_t len, bool presync, bool wait)
{
    if (!s_ready || !json || len == 0) return false;

    // Refused whole: cutting an event at the slot size stored invalid JSON, which the
    // replay then published as a message nothing downstream could parse (L17/N18).
    if (len > OFFLINE_BUF_MAX_JSON_LEN) {
        ESP_LOGW(OB_TAG, "Event too large (%u bytes, max %d) - not buffered",
                 (unsigned)len, OFFLINE_BUF_MAX_JSON_LEN);
        return false;
    }

    if (wait) {
        if (!ob_lock("store")) return false;
    } else if (xSemaphoreTake(s_lock, 0) != pdTRUE) {
        ESP_LOGW(OB_TAG, "store: buffer busy - skipped, not waited for");
        return false;
    }
    bool ok = store_locked(json, len, presync);
    ob_unlock();
    return ok;
}

bool offline_buffer_store(const char *json, size_t len)
{
    return store_common(json, len, false, true);
}

bool offline_buffer_try_store(const char *json, size_t len)
{
    return store_common(json, len, false, false);
}

bool offline_buffer_store_presync(const char *json, size_t len)
{
    return store_common(json, len, true, true);
}

// ---------------------------------------------------------------------------
// Pre-sync replay: text scanning only. No cJSON here - the drain runs straight after the
// MQTT/TLS connect, when the heap is at its tightest - and no heap at all.
// ---------------------------------------------------------------------------

// The integer after the FIRST occurrence of `key` (a quoted key with its colon, e.g.
// "\"ts\":") in the NUL-terminated json. The first occurrence is the envelope's own:
// build_envelope() writes "schema", then "ts", then "gateway" (with "uptime_s") before
// "data", and JSON string escaping means a quoted key can never appear inside a string
// value. Only an optional '-' and up to 18 digits are accepted - how cJSON prints the
// whole seconds these keys carry. Returns the start of the number and sets *num_end just
// past it, or NULL when the key is absent or no plain integer follows it.
static char *ob_find_int(char *json, const char *key, int64_t *out, char **num_end)
{
    char *p = strstr(json, key);
    if (!p) return NULL;

    char *num = p + strlen(key);
    char *q = num;
    bool neg = (*q == '-');
    if (neg) q++;

    int64_t v = 0;
    int digits = 0;
    while (*q >= '0' && *q <= '9') {
        if (++digits > 18) return NULL;
        v = v * 10 + (*q - '0');
        q++;
    }
    if (digits == 0 || *q == '.' || *q == 'e' || *q == 'E') return NULL;

    *out = neg ? -v : v;
    if (num_end) *num_end = q;
    return num;
}

// Replaces the number text [num, num_end) inside buf (NUL-terminated, length *len, `cap`
// bytes including the NUL) with `value`, moving the tail. false = the result would not
// fit; buf is then untouched.
static bool ob_rewrite_int(char *buf, size_t *len, size_t cap,
                           char *num, char *num_end, int64_t value)
{
    char digits[24];
    int n = snprintf(digits, sizeof(digits), "%lld", (long long)value);
    if (n <= 0 || (size_t)n >= sizeof(digits)) return false;

    size_t new_len = *len - (size_t)(num_end - num) + (size_t)n;
    if (new_len + 1 > cap) return false;

    memmove(num + n, num_end, (size_t)(buf + *len - num_end) + 1);   // tail + its NUL
    memcpy(num, digits, (size_t)n);
    *len = new_len;
    return true;
}

typedef enum {
    OB_STAMP_OK,         // "ts" rewritten in buf
    OB_STAMP_UNFIXABLE,  // no usable uptime_s, or the result is not a synced time
    OB_STAMP_TOO_LONG,   // the stamped text would not fit in buf
} ob_stamp_t;

// Stamps a pre-sync event of THIS boot (the caller has checked its mask bit): rewrites its
// "ts" number [ts_num, ts_end) in buf to now - (uptime now - its gateway.uptime_s), the
// same uptime basis build_envelope() uses. `now` must be a synced time. buf is untouched
// unless OB_STAMP_OK.
static ob_stamp_t ob_stamp(char *buf, size_t *len, size_t cap, char *ts_num, char *ts_end,
                           time_t now, int64_t *stamped_out, int64_t *age_out)
{
    int64_t uptime_now_s = esp_timer_get_time() / 1000000;
    int64_t uptime_s = 0;
    if (!ob_find_int(buf, "\"uptime_s\":", &uptime_s, NULL) ||
        uptime_s < 0 || uptime_s > uptime_now_s) {
        return OB_STAMP_UNFIXABLE;
    }
    int64_t stamped = (int64_t)now - (uptime_now_s - uptime_s);
    if (stamped < OB_EPOCH_VALID_THRESHOLD) return OB_STAMP_UNFIXABLE;
    if (!ob_rewrite_int(buf, len, cap, ts_num, ts_end, stamped)) return OB_STAMP_TOO_LONG;

    *stamped_out = stamped;
    *age_out = uptime_now_s - uptime_s;
    return OB_STAMP_OK;
}

typedef enum {
    OB_REPLAY_PUBLISH,   // publish buf as it now stands (stamped, if it was pre-sync)
    OB_REPLAY_DROP,      // erase without publishing (logged); advances like a publish
    OB_REPLAY_HOLD,      // stop the drain here, keeping this entry and the rest
} ob_replay_t;

// Decides what the drain does with the entry just read from `slot` into buf, stamping a
// pre-sync event's "ts" in place. Call with s_lock held (reads s_presync_mask).
static ob_replay_t ob_prepare_replay_locked(uint8_t slot, const char *key,
                                            char *buf, size_t *len, size_t cap)
{
    int64_t ts = 0;
    char *ts_end = NULL;
    char *ts_num = ob_find_int(buf, "\"ts\":", &ts, &ts_end);
    if (!ts_num || ts >= OB_EPOCH_VALID_THRESHOLD) {
        return OB_REPLAY_PUBLISH;   // stamped when it was built: unchanged, as always
    }

    if ((s_presync_mask & (1u << slot)) == 0) {
        ESP_LOGW(OB_TAG, "Dropped a buffered event from an earlier power cycle that was never time-stamped [%s]",
                 key);
        return OB_REPLAY_DROP;
    }

    time_t now = time(NULL);
    if (now < OB_EPOCH_VALID_THRESHOLD) {
        // Should not happen: the drain runs after the MQTT connect, which needs the clock.
        ESP_LOGW(OB_TAG, "Clock not synced - pre-sync event [%s] and %d after it kept for the next drain",
                 key, s_count - 1);
        return OB_REPLAY_HOLD;
    }

    int64_t stamped = 0;
    int64_t age_s = 0;
    switch (ob_stamp(buf, len, cap, ts_num, ts_end, now, &stamped, &age_s)) {
    case OB_STAMP_OK:
        break;
    case OB_STAMP_TOO_LONG:
        ESP_LOGW(OB_TAG, "Dropped a buffered pre-sync event [%s] - too long once time-stamped",
                 key);
        return OB_REPLAY_DROP;
    default:
        ESP_LOGW(OB_TAG, "Dropped a buffered pre-sync event [%s] - cannot be time-stamped from its uptime_s",
                 key);
        return OB_REPLAY_DROP;
    }

    ESP_LOGI(OB_TAG, "Stamped pre-sync event [%s]: ts=%lld (%lld s before this replay)",
             key, (long long)stamped, (long long)age_s);
    return OB_REPLAY_PUBLISH;
}

// Call with s_lock held and s_count > 0.
static int drain_locked(esp_mqtt_client_handle_t client, const char *topic,
                        bool (*still_up)(void))
{
    ESP_LOGI(OB_TAG, "Draining %d buffered event(s)...", s_count);

    nvs_handle_t h;
    if (nvs_open(OB_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(OB_TAG, "NVS open failed for drain");
        return 0;
    }

    int published = 0;
    char buf[OFFLINE_BUF_MAX_JSON_LEN + OB_STAMP_MARGIN + 1];

    while (s_count > 0) {
        char key[8];
        make_key(s_tail, key, sizeof(key));

        // buf has OB_STAMP_MARGIN beyond the store limit, to stamp a pre-sync event here.
        // The whole of it is offered to the read: an earlier 2.1.4 build could leave a
        // slot stamped at the clock sync up to that much longer than the limit.
        size_t len = sizeof(buf) - 1;
        esp_err_t err = nvs_get_blob(h, key, buf, &len);
        if (err != ESP_OK) {
            ESP_LOGW(OB_TAG, "Read '%s' failed: %s, skipping",
                     key, esp_err_to_name(err));
        } else {
            buf[len] = '\0';
            ob_replay_t action = ob_prepare_replay_locked(s_tail, key, buf, &len, sizeof(buf));
            if (action == OB_REPLAY_HOLD) break;
            if (action == OB_REPLAY_PUBLISH) {
                // Only into a session still up, and erased only if it is still up after the
                // publish (offline_buffer_drain() in the header): else this entry and the rest
                // wait for the next connect's drain. A publish whose own write failed ended
                // the session too, and keeps its line below.
                bool up = still_up();
                int msg_id = up ? esp_mqtt_client_publish(client, topic, buf, (int)len, 1, 0) : -1;
                if (up && msg_id >= 0)
                    up = still_up();
                if (!up) {
                    ESP_LOGW(OB_TAG, "MQTT session ended - drain stopped at [%s], kept for the next connect",
                             key);
                    break;
                }
                if (msg_id >= 0) {
                    published++;
                    ESP_LOGI(OB_TAG, "Replayed [%s] (%u bytes)", key, (unsigned)len);
                } else {
                    // A stamped entry is not written back: its bit stays set, so the next
                    // drain stamps it again from the same uptime_s.
                    ESP_LOGW(OB_TAG, "MQTT publish failed for [%s], stopping drain", key);
                    break;
                }
            }
        }

        // Erase this slot and advance tail
        nvs_erase_key(h, key);
        s_presync_mask &= (uint16_t)~(1u << s_tail);
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

int offline_buffer_drain(esp_mqtt_client_handle_t client, const char *topic,
                         bool (*still_up)(void))
{
    if (!s_ready || !client || !topic || !still_up) return 0;

    // The lock is held across the replay publishes. A store() that arrives meanwhile
    // waits up to OB_LOCK_TIMEOUT_MS and is then dropped (logged), never interleaved.
    // Each publish takes esp-mqtt's API lock, so the esp-mqtt task, which holds that lock
    // in its event handler, stores only through offline_buffer_try_store().
    if (!ob_lock("drain")) return 0;
    int published = (s_count > 0) ? drain_locked(client, topic, still_up) : 0;
    ob_unlock();
    return published;
}

void offline_buffer_stamp_presync(void)
{
    if (!s_ready) return;
    time_t now = time(NULL);
    if (now < OB_EPOCH_VALID_THRESHOLD) return;
    if (!ob_lock("stamp")) return;   // the drain stamps them instead
    if (s_presync_mask == 0 || s_count == 0) {
        ob_unlock();
        return;
    }

    nvs_handle_t h;
    if (nvs_open(OB_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(OB_TAG, "NVS open failed for pre-sync stamping");
        ob_unlock();
        return;
    }

    int stamped_n = 0;
    // Only an entry stored this boot has its bit set, and store refuses anything longer
    // than OFFLINE_BUF_MAX_JSON_LEN, so this reads every one. The same limit caps what is
    // written back: an older build reads a slot with it, and a rollback to 2.1.3 skips and
    // erases a longer one, losing the event.
    char buf[OFFLINE_BUF_MAX_JSON_LEN + 1];
    uint8_t slot = s_tail;
    for (uint8_t i = 0; i < s_count; i++, slot = (uint8_t)((slot + 1) % OFFLINE_BUF_MAX_ENTRIES)) {
        uint16_t bit = (uint16_t)(1u << slot);
        if ((s_presync_mask & bit) == 0) continue;

        char key[8];
        make_key(slot, key, sizeof(key));
        size_t len = sizeof(buf) - 1;
        if (nvs_get_blob(h, key, buf, &len) != ESP_OK) continue;   // the drain retries
        buf[len] = '\0';

        int64_t ts = 0;
        char *ts_end = NULL;
        char *ts_num = ob_find_int(buf, "\"ts\":", &ts, &ts_end);
        if (!ts_num || ts >= OB_EPOCH_VALID_THRESHOLD) {
            s_presync_mask &= (uint16_t)~bit;   // nothing to stamp: replayed as it is
            continue;
        }

        int64_t stamped = 0;
        int64_t age_s = 0;
        // Not stampable: the bit stays, and the drain drops it with its own log line. Too
        // long for buf once stamped: the bit stays, and the drain stamps it in RAM, where
        // it has OB_STAMP_MARGIN to spare.
        if (ob_stamp(buf, &len, sizeof(buf), ts_num, ts_end, now, &stamped, &age_s) != OB_STAMP_OK)
            continue;
        if (nvs_set_blob(h, key, buf, len) != ESP_OK) continue;

        s_presync_mask &= (uint16_t)~bit;
        stamped_n++;
        ESP_LOGI(OB_TAG, "Stamped pre-sync event [%s] at clock sync: ts=%lld (%lld s ago)",
                 key, (long long)stamped, (long long)age_s);
    }

    if (stamped_n > 0) nvs_commit(h);
    nvs_close(h);
    ob_unlock();
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
    s_presync_mask = 0;
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
