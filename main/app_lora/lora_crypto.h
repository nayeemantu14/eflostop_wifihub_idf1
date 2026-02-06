/*
 * lora_crypto.h
 *
 * AES-128-CCM decryption/verification for eFloStop LoRa sensor packets.
 * ESP32 hub (receiver) side â€” uses mbedtls for AES-CCM.
 *
 * Must match the STM32WL sender's lora_crypto.h wire format exactly.
 *
 * ============================================================================
 * WIRE FORMAT (18 bytes from sensor, inside 64-byte LoRa payload)
 * ============================================================================
 *
 *  Offset  Size  Field              Notes
 *  ------  ----  -----------------  ------------------------------------------
 *  0       4     sensorID  (BE)     Plaintext AAD - hub uses to derive key
 *  4       4     boot_random (BE)   Plaintext     - nonce component
 *  8       2     FrameSentCnt (BE)  Plaintext AAD - replay detection + nonce
 *  10      4     ciphertext         AES-CCM encrypted payload
 *  14      4     MIC                AES-CCM authentication tag
 *
 * AAD (Associated Data) = bytes 0..9  (10 bytes: sensorID + boot_rnd + frameCnt)
 *
 * Plaintext (4 bytes, encrypted at offset 10):
 *   [battery:1] [leakStatus:1] [FrameAckCnt_BE:2]
 *
 * ============================================================================
 * NONCE (13 bytes, must be identical on both sides)
 * ============================================================================
 *   Byte 0-3:   sensorID   (big-endian)
 *   Byte 4-7:   boot_random (big-endian)
 *   Byte 8-9:   FrameSentCnt (big-endian)
 *   Byte 10-12: 0x00 0x00 0x00
 *
 * ============================================================================
 * KEY DERIVATION
 * ============================================================================
 *   sensor_key = AES-128-ECB( MASTER_SECRET, KDF_block )
 *
 *   KDF_block (16 bytes):
 *     [0..3]   sensorID (BE)
 *     [4..7]   ~sensorID (BE)
 *     [8..11]  sensorID ^ 0xDEADBEEF (BE)
 *     [12..15] (sensorID << 16) | (sensorID >> 16) (BE)
 */

#ifndef LORA_CRYPTO_H
#define LORA_CRYPTO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* =========================================================================
 * MASTER SECRET â€” MUST match STM32WL sender firmware exactly
 * ========================================================================= */
#define LORA_CRYPTO_MASTER_SECRET  { \
    0xEF, 0x10, 0x57, 0x0A, 0x3C, 0x8B, 0x2D, 0x6F, \
    0x91, 0x4E, 0xA7, 0xD5, 0x38, 0xC2, 0x7B, 0x1F  \
}
/* =========================================================================
 * WIRE FORMAT CONSTANTS
 * ========================================================================= */
#define LORA_CRYPTO_KEY_LEN        16    /* AES-128 key size */
#define LORA_CRYPTO_NONCE_LEN      13    /* CCM nonce size */
#define LORA_CRYPTO_TAG_LEN         4    /* CCM MIC (authentication tag) */
#define LORA_CRYPTO_HDR_LEN        10    /* sensorID(4) + boot_rnd(4) + frameCnt(2) */
#define LORA_CRYPTO_PLAIN_LEN       4    /* battery(1) + leak(1) + frameAckCnt(2) */
#define LORA_CRYPTO_PKT_LEN        18    /* HDR + CIPHER + TAG */

/* Header field offsets */
#define LORA_CRYPTO_OFF_SENSOR_ID   0
#define LORA_CRYPTO_OFF_BOOT_RND    4
#define LORA_CRYPTO_OFF_FRAME_CNT   8
#define LORA_CRYPTO_OFF_CIPHER     10
#define LORA_CRYPTO_OFF_MIC        14

/* =========================================================================
 * REPLAY PROTECTION
 * ========================================================================= */
#define LORA_CRYPTO_MAX_SENSORS    16   /* matches MAX_LORA_SENSORS */

/* =========================================================================
 * DECRYPTED PAYLOAD STRUCTURE
 * ========================================================================= */
typedef struct {
    uint32_t sensor_id;
    uint8_t  battery;
    uint8_t  leak_status;
    uint16_t frame_sent_cnt;
    uint16_t frame_ack_cnt;
} lora_crypto_payload_t;

/* =========================================================================
 * PUBLIC API
 * ========================================================================= */

/**
 * @brief  Initialize the crypto module on the hub side.
 *         Call once at startup (e.g. in lora_task before main loop).
 *
 * @return true on success
 */
bool lora_crypto_init(void);

/**
 * @brief  Decrypt and verify an incoming LoRa packet.
 *
 * Steps performed:
 *   1. Extract sensorID from plaintext header (bytes 0-3)
 *   2. Derive per-sensor AES key from MASTER_SECRET + sensorID
 *   3. Reconstruct the 13-byte nonce
 *   4. AES-CCM auth-decrypt: verify MIC tag + decrypt ciphertext
 *   5. Replay check: reject if FrameSentCnt <= last seen for this sensor
 *   6. Populate output structure with decrypted values
 *
 * @param  raw_pkt    Pointer to received LoRa payload (>= LORA_CRYPTO_PKT_LEN bytes)
 * @param  raw_len    Length of received payload
 * @param  out        Output: decrypted and verified sensor data
 * @return true if packet is authentic, untampered, and not a replay
 */
bool lora_crypto_decrypt_packet(const uint8_t *raw_pkt, int raw_len,
                                lora_crypto_payload_t *out);

/**
 * @brief  Get the last frame counter seen for a given sensor.
 *         Useful for diagnostics.
 *
 * @param  sensor_id  Sensor ID to query
 * @return Last seen FrameSentCnt, or 0 if sensor not yet seen
 */
uint16_t lora_crypto_get_last_counter(uint32_t sensor_id);

#ifdef __cplusplus
}
#endif

#endif /* LORA_CRYPTO_H */