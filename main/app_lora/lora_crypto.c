/*
 * lora_crypto.c
 *
 * AES-128-CCM decryption/verification for eFloStop LoRa sensor packets.
 * ESP32 hub (receiver) side — uses mbedtls for all crypto operations.
 *
 * STM32 Compatibility Note:
 *   The STM32WL sender uses HAL_CRYP with CRYP_DATATYPE_8B, which byte-swaps
 *   the DATA path but NOT the KEY registers. Combined with LE ARM memcpy into
 *   uint32_t key arrays, the AES core sees keys that are byte-reversed within
 *   each 32-bit word. We replicate this on ESP32 via make_stm32_key().
 *
 * Created for: ESP32 WiFi Hub (receiver side)
 */

#include "lora_crypto.h"

#include <string.h>
#include "esp_log.h"
#include "mbedtls/aes.h"
#include "mbedtls/ccm.h"

static const char *TAG = "LORA_CRYPTO";

/* =========================================================================
 * MASTER SECRET (must match STM32WL sender firmware)
 * ========================================================================= */
static const uint8_t MASTER_SECRET[LORA_CRYPTO_KEY_LEN] = LORA_CRYPTO_MASTER_SECRET;

/* =========================================================================
 * REPLAY PROTECTION STATE
 * ========================================================================= */
typedef struct {
    uint32_t sensor_id;
    uint32_t boot_random;
    uint16_t last_frame_cnt;
    bool     active;
} sensor_replay_state_t;

static sensor_replay_state_t s_replay[LORA_CRYPTO_MAX_SENSORS];
static bool s_initialized = false;

/* =========================================================================
 * HELPERS
 * ========================================================================= */

static void put_be32(uint8_t *dst, uint32_t val)
{
    dst[0] = (uint8_t)(val >> 24);
    dst[1] = (uint8_t)(val >> 16);
    dst[2] = (uint8_t)(val >>  8);
    dst[3] = (uint8_t)(val);
}

static uint32_t get_be32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] <<  8) |
           ((uint32_t)src[3]);
}

static uint16_t get_be16(const uint8_t *src)
{
    return ((uint16_t)src[0] << 8) | (uint16_t)src[1];
}

/**
 * @brief Create a word-byte-swapped copy of a 16-byte key.
 *
 * Matches the STM32 HAL_CRYP key register loading behavior:
 * LE ARM memcpy(uint32_t[], byte_array) then write to BE KEYR registers.
 *   {EF 10 57 0A} -> uint32_t 0x0A5710EF -> KEYR sees {0A 57 10 EF}
 */
static void make_stm32_key(const uint8_t in[16], uint8_t out[16])
{
    for (int i = 0; i < 16; i += 4) {
        out[i]     = in[i + 3];
        out[i + 1] = in[i + 2];
        out[i + 2] = in[i + 1];
        out[i + 3] = in[i];
    }
}

/* =========================================================================
 * KEY DERIVATION
 * ========================================================================= */
static bool derive_sensor_key(uint32_t sensor_id, uint8_t key_out[LORA_CRYPTO_KEY_LEN])
{
    uint8_t kdf_block[16];

    put_be32(&kdf_block[0],  sensor_id);
    put_be32(&kdf_block[4],  ~sensor_id);
    put_be32(&kdf_block[8],  sensor_id ^ 0xDEADBEEFUL);
    put_be32(&kdf_block[12], (sensor_id << 16) | (sensor_id >> 16));

    uint8_t stm32_master[LORA_CRYPTO_KEY_LEN];
    make_stm32_key(MASTER_SECRET, stm32_master);

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);

    int ret = mbedtls_aes_setkey_enc(&aes, stm32_master, 128);
    if (ret != 0) {
        ESP_LOGE(TAG, "AES setkey failed: -0x%04X", (unsigned int)-ret);
        mbedtls_aes_free(&aes);
        return false;
    }

    ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, kdf_block, key_out);
    mbedtls_aes_free(&aes);

    if (ret != 0) {
        ESP_LOGE(TAG, "AES ECB failed: -0x%04X", (unsigned int)-ret);
        return false;
    }

    return true;
}

/* =========================================================================
 * NONCE CONSTRUCTION (13 bytes)
 * ========================================================================= */
static void build_nonce(uint32_t sensor_id, uint32_t boot_rnd, uint16_t frame_cnt,
                        uint8_t nonce[LORA_CRYPTO_NONCE_LEN])
{
    put_be32(&nonce[0], sensor_id);
    put_be32(&nonce[4], boot_rnd);
    nonce[8]  = (uint8_t)(frame_cnt >> 8);
    nonce[9]  = (uint8_t)(frame_cnt);
    nonce[10] = 0x00;
    nonce[11] = 0x00;
    nonce[12] = 0x00;
}

/* =========================================================================
 * REPLAY PROTECTION
 * ========================================================================= */

static sensor_replay_state_t* find_or_create_slot(uint32_t sensor_id)
{
    sensor_replay_state_t *empty_slot = NULL;

    for (int i = 0; i < LORA_CRYPTO_MAX_SENSORS; i++) {
        if (s_replay[i].active && s_replay[i].sensor_id == sensor_id) {
            return &s_replay[i];
        }
        if (!s_replay[i].active && empty_slot == NULL) {
            empty_slot = &s_replay[i];
        }
    }

    if (empty_slot != NULL) {
        empty_slot->sensor_id      = sensor_id;
        empty_slot->boot_random    = 0;
        empty_slot->last_frame_cnt = 0;
        empty_slot->active         = true;
        ESP_LOGI(TAG, "New sensor registered: 0x%08lX", (unsigned long)sensor_id);
    } else {
        ESP_LOGW(TAG, "Replay table full! Cannot track sensor 0x%08lX",
                 (unsigned long)sensor_id);
    }

    return empty_slot;
}

static bool replay_check_and_update(uint32_t sensor_id, uint32_t boot_rnd,
                                    uint16_t frame_cnt)
{
    sensor_replay_state_t *slot = find_or_create_slot(sensor_id);
    if (slot == NULL) {
        return true;
    }

    if (boot_rnd != slot->boot_random) {
        ESP_LOGI(TAG, "Sensor 0x%08lX rebooted (boot_rnd: 0x%08lX -> 0x%08lX)",
                 (unsigned long)sensor_id,
                 (unsigned long)slot->boot_random,
                 (unsigned long)boot_rnd);
        slot->boot_random    = boot_rnd;
        slot->last_frame_cnt = frame_cnt;
        return true;
    }

    if (frame_cnt > slot->last_frame_cnt) {
        slot->last_frame_cnt = frame_cnt;
        return true;
    }

    ESP_LOGW(TAG, "REPLAY REJECTED: sensor=0x%08lX, cnt=%u, last=%u",
             (unsigned long)sensor_id, frame_cnt, slot->last_frame_cnt);
    return false;
}

/* =========================================================================
 * PUBLIC API
 * ========================================================================= */

bool lora_crypto_init(void)
{
    memset(s_replay, 0, sizeof(s_replay));
    s_initialized = true;
    ESP_LOGI(TAG, "Crypto module initialized (hub receiver)");
    return true;
}

bool lora_crypto_decrypt_packet(const uint8_t *raw_pkt, int raw_len,
                                lora_crypto_payload_t *out)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Crypto not initialized!");
        return false;
    }

    if (raw_pkt == NULL || out == NULL) {
        return false;
    }

    if (raw_len < LORA_CRYPTO_PKT_LEN) {
        ESP_LOGE(TAG, "Packet too short (%d < %d)", raw_len, LORA_CRYPTO_PKT_LEN);
        return false;
    }

    /* 1. Extract header fields */
    uint32_t sensor_id  = get_be32(&raw_pkt[LORA_CRYPTO_OFF_SENSOR_ID]);
    uint32_t boot_rnd   = get_be32(&raw_pkt[LORA_CRYPTO_OFF_BOOT_RND]);
    uint16_t frame_cnt  = get_be16(&raw_pkt[LORA_CRYPTO_OFF_FRAME_CNT]);

    const uint8_t *ciphertext = &raw_pkt[LORA_CRYPTO_OFF_CIPHER];
    const uint8_t *mic_tag    = &raw_pkt[LORA_CRYPTO_OFF_MIC];

    /* 2. Derive per-sensor key (word-swapped master for STM32 compat) */
    uint8_t sensor_key[LORA_CRYPTO_KEY_LEN];
    if (!derive_sensor_key(sensor_id, sensor_key)) {
        return false;
    }

    /* 3. Build nonce */
    uint8_t nonce[LORA_CRYPTO_NONCE_LEN];
    build_nonce(sensor_id, boot_rnd, frame_cnt, nonce);

    /* 4. Word-swap derived key for CCM (STM32 loads CCM key same way) */
    uint8_t ccm_key[LORA_CRYPTO_KEY_LEN];
    make_stm32_key(sensor_key, ccm_key);

    /* 5. AES-CCM auth-decrypt */
    uint8_t plaintext[LORA_CRYPTO_PLAIN_LEN];

    mbedtls_ccm_context ccm;
    mbedtls_ccm_init(&ccm);

    int ret = mbedtls_ccm_setkey(&ccm, MBEDTLS_CIPHER_ID_AES, ccm_key, 128);
    if (ret != 0) {
        ESP_LOGE(TAG, "CCM setkey failed: -0x%04X", (unsigned int)-ret);
        mbedtls_ccm_free(&ccm);
        return false;
    }

    ret = mbedtls_ccm_auth_decrypt(
        &ccm,
        LORA_CRYPTO_PLAIN_LEN,
        nonce, LORA_CRYPTO_NONCE_LEN,
        raw_pkt, LORA_CRYPTO_HDR_LEN,
        ciphertext,
        plaintext,
        mic_tag, LORA_CRYPTO_TAG_LEN
    );

    mbedtls_ccm_free(&ccm);

    if (ret != 0) {
        ESP_LOGW(TAG, "CCM auth FAILED for sensor 0x%08lX cnt=%u (ret=-0x%04X)",
                 (unsigned long)sensor_id, frame_cnt, (unsigned int)-ret);
        return false;
    }

    /* 6. Replay protection */
    if (!replay_check_and_update(sensor_id, boot_rnd, frame_cnt)) {
        return false;
    }

    /* 7. Populate output */
    out->sensor_id      = sensor_id;
    out->battery        = plaintext[0];
    out->leak_status    = plaintext[1];
    out->frame_sent_cnt = frame_cnt;
    out->frame_ack_cnt  = (uint16_t)(plaintext[2] << 8) | plaintext[3];

    return true;
}

uint16_t lora_crypto_get_last_counter(uint32_t sensor_id)
{
    for (int i = 0; i < LORA_CRYPTO_MAX_SENSORS; i++) {
        if (s_replay[i].active && s_replay[i].sensor_id == sensor_id) {
            return s_replay[i].last_frame_cnt;
        }
    }
    return 0;
}