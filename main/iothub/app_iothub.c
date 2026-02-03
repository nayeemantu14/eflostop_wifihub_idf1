#include "app_iothub.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "esp_mac.h" // Required for fetching unique MAC address

// Crypto
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"

// Definition of the global handle
TaskHandle_t iothub_task_handle = NULL;

static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool g_iot_hub_connected = false;

// Buffer to store the Unique Gateway ID (e.g., "GW-A1B2C3D4E5F6")
static char g_gateway_id[32] = {0};

// -----------------------------------------------------------------------------
// Helper: URL Encode
// -----------------------------------------------------------------------------
void url_encode(const char *src, char *dst, size_t dst_len) {
    char *end = dst + dst_len - 1;
    while (*src && dst < end) {
        if ((*src >= 'A' && *src <= 'Z') || (*src >= 'a' && *src <= 'z') || 
            (*src >= '0' && *src <= '9') || *src == '-' || *src == '.' || *src == '_' || *src == '~') {
            *dst++ = *src;
        } else {
            if (dst + 3 > end) break;
            dst += sprintf(dst, "%%%02X", (unsigned char)*src);
        }
        src++;
    }
    *dst = '\0';
}

// -----------------------------------------------------------------------------
// Helper: Generate SAS Token
// -----------------------------------------------------------------------------
char* generate_sas_token(const char* resource_uri, const char* key, long expiry_seconds) {
    char expiry_str[20];
    time_t now;
    time(&now);
    long expiry = now + expiry_seconds;
    snprintf(expiry_str, sizeof(expiry_str), "%ld", expiry);

    char encoded_uri[128];
    url_encode(resource_uri, encoded_uri, sizeof(encoded_uri));

    char string_to_sign[256];
    snprintf(string_to_sign, sizeof(string_to_sign), "%s\n%s", encoded_uri, expiry_str);

    unsigned char decoded_key[64];
    size_t decoded_key_len = 0;
    mbedtls_base64_decode(decoded_key, sizeof(decoded_key), &decoded_key_len, (const unsigned char*)key, strlen(key));

    unsigned char hmac[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, decoded_key, decoded_key_len);
    mbedtls_md_hmac_update(&ctx, (const unsigned char*)string_to_sign, strlen(string_to_sign));
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);

    unsigned char signature_b64[64];
    size_t signature_b64_len = 0;
    mbedtls_base64_encode(signature_b64, sizeof(signature_b64), &signature_b64_len, hmac, 32);
    signature_b64[signature_b64_len] = '\0';

    char encoded_signature[128];
    url_encode((char*)signature_b64, encoded_signature, sizeof(encoded_signature));

    char *sas_token = (char*)malloc(512);
    snprintf(sas_token, 512, "SharedAccessSignature sr=%s&sig=%s&se=%s", encoded_uri, encoded_signature, expiry_str);

    return sas_token;
}

// -----------------------------------------------------------------------------
// Helper: Convert LoRa Packet to JSON (With Gateway ID)
// -----------------------------------------------------------------------------
static char* generate_json_telemetry(lora_packet_t *pkt) {
    cJSON *root = cJSON_CreateObject();
    
    // 1. Identify the Gateway (The "Who Am I")
    cJSON_AddStringToObject(root, "gatewayId", g_gateway_id);

    // 2. Identify the Sensor (The "Who Sent This")
    cJSON_AddNumberToObject(root, "sensorId", pkt->sensorId);
    
    // 3. Sensor Data
    cJSON_AddBoolToObject(root, "leak", (pkt->leakStatus != 0));
    cJSON_AddNumberToObject(root, "battery", pkt->batteryPercentage);
    
    // 4. Metadata
    cJSON_AddNumberToObject(root, "rssi", pkt->rssi);
    cJSON_AddNumberToObject(root, "snr", pkt->snr);
    cJSON_AddNumberToObject(root, "msgId", pkt->frameSent);
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// -----------------------------------------------------------------------------
// MQTT Logic
// -----------------------------------------------------------------------------
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(IOTHUB_TAG, "Connected to Azure IoT Hub!");
            g_iot_hub_connected = true;
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(IOTHUB_TAG, "Disconnected. Retrying...");
            g_iot_hub_connected = false;
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(IOTHUB_TAG, "MQTT Error: %d", event->error_handle->error_type);
            break;
        default: break;
    }
}

static void initialize_sntp(void) {
    ESP_LOGI(IOTHUB_TAG, "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    while (timeinfo.tm_year < (2020 - 1900)) {
        ESP_LOGI(IOTHUB_TAG, "Waiting for time... (%d)", ++retry);
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    ESP_LOGI(IOTHUB_TAG, "Time set: %s", asctime(&timeinfo));
}

static void init_gateway_id(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    // Format: "GW-A1B2C3D4E5F6"
    snprintf(g_gateway_id, sizeof(g_gateway_id), "GW-%02X%02X%02X%02X%02X%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
             
    ESP_LOGI(IOTHUB_TAG, "Unique Gateway ID: %s", g_gateway_id);
}

// -----------------------------------------------------------------------------
// The Main Task
// -----------------------------------------------------------------------------
void iothub_task(void *param)
{
    // ============================================================
    // 1. BLOCK HERE UNTIL WI-FI NOTIFIES US
    // ============================================================
    ESP_LOGI(IOTHUB_TAG, "Task Started. Waiting for Wi-Fi Signal...");
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY); 
    ESP_LOGI(IOTHUB_TAG, "Signal Received! Starting Initialization...");

    // ============================================================
    // 2. Perform Initialization
    // ============================================================
    
    // A. Generate Gateway ID from MAC
    init_gateway_id();

    // B. Sync Time
    initialize_sntp();

    // C. Generate SAS Token
    char resource_uri[128];
    snprintf(resource_uri, sizeof(resource_uri), "%s.azure-devices.net/devices/%s", AZURE_HUB_NAME, AZURE_DEVICE_ID);
    char *sas_token = generate_sas_token(resource_uri, AZURE_PRIMARY_KEY, 31536000);
    
    // D. Start MQTT
    char uri[128], username[256];
    snprintf(uri, sizeof(uri), "mqtts://%s.azure-devices.net", AZURE_HUB_NAME);
    snprintf(username, sizeof(username), "%s.azure-devices.net/%s/?api-version=2021-04-12", AZURE_HUB_NAME, AZURE_DEVICE_ID);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials = {
            .username = username,
            .client_id = AZURE_DEVICE_ID,
            .authentication = { .password = sas_token }
        },
        .session.keepalive = 60,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    // ============================================================
    // 3. Enter Main Loop
    // ============================================================
    lora_packet_t pkt;
    char topic[128];
    snprintf(topic, sizeof(topic), "devices/%s/messages/events/", AZURE_DEVICE_ID);

    while (1)
    {
        if (xQueueReceive(lora_rx_queue, &pkt, portMAX_DELAY) == pdTRUE)
        {
            if (g_iot_hub_connected) {
                char *json_payload = generate_json_telemetry(&pkt);
                ESP_LOGI(IOTHUB_TAG, "Uploading: %s", json_payload);
                esp_mqtt_client_publish(mqtt_client, topic, json_payload, 0, 1, 0);
                free(json_payload);
            } else {
                ESP_LOGW(IOTHUB_TAG, "Skipping upload (Not Connected)");
            }
        }
    }
}

void initialize_iothub(void)
{
    xTaskCreate(iothub_task, "iothub_task", 8192, NULL, 5, &iothub_task_handle);
}