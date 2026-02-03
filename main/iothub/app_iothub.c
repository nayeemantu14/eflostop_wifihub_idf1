#include "app_iothub.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "esp_mac.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"

#include "app_ble_valve.h"

// External Queue from LoRa app
extern QueueHandle_t lora_rx_queue;

TaskHandle_t iothub_task_handle = NULL;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool g_iot_hub_connected = false;
static char g_gateway_id[32] = {0};

void url_encode(const char *src, char *dst, size_t dst_len)
{
    char *end = dst + dst_len - 1;
    while (*src && dst < end)
    {
        if ((*src >= 'A' && *src <= 'Z') || (*src >= 'a' && *src <= 'z') ||
            (*src >= '0' && *src <= '9') || *src == '-' || *src == '.' || *src == '_' || *src == '~')
        {
            *dst++ = *src;
        }
        else
        {
            if (dst + 3 > end)
                break;
            dst += sprintf(dst, "%%%02X", (unsigned char)*src);
        }
        src++;
    }
    *dst = '\0';
}

char *generate_sas_token(const char *resource_uri, const char *key, long expiry_seconds)
{
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
    mbedtls_base64_decode(decoded_key, sizeof(decoded_key), &decoded_key_len, (const unsigned char *)key, strlen(key));
    unsigned char hmac[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, decoded_key, decoded_key_len);
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)string_to_sign, strlen(string_to_sign));
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);
    unsigned char signature_b64[64];
    size_t signature_b64_len = 0;
    mbedtls_base64_encode(signature_b64, sizeof(signature_b64), &signature_b64_len, hmac, 32);
    signature_b64[signature_b64_len] = '\0';
    char encoded_signature[128];
    url_encode((char *)signature_b64, encoded_signature, sizeof(encoded_signature));
    char *sas_token = (char *)malloc(512);
    snprintf(sas_token, 512, "SharedAccessSignature sr=%s&sig=%s&se=%s", encoded_uri, encoded_signature, expiry_str);
    return sas_token;
}

static char *generate_json_telemetry(lora_packet_t *pkt)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "gatewayID", g_gateway_id);

    cJSON *devicesArr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "devices", devicesArr);

    cJSON *deviceObj = cJSON_CreateObject();
    cJSON_AddItemToArray(devicesArr, deviceObj);

    cJSON *valveObj = cJSON_CreateObject();
    cJSON_AddItemToObject(deviceObj, "valve", valveObj);

    // --- VALVE DATA ---
    char valve_mac[18];
    if (ble_valve_get_mac(valve_mac))
    {
        cJSON_AddStringToObject(valveObj, "valve_mac", valve_mac);
        cJSON_AddNumberToObject(valveObj, "battery", ble_valve_get_battery());
        cJSON_AddBoolToObject(valveObj, "leak_state", ble_valve_get_leak());

        int state = ble_valve_get_state();
        // LOGIC: 1=Open, 0=Closed
        if (state == 1)
            cJSON_AddStringToObject(valveObj, "valve_state", "open");
        else if (state == 0)
            cJSON_AddStringToObject(valveObj, "valve_state", "closed");
        else
            cJSON_AddStringToObject(valveObj, "valve_state", "unknown");
    }
    else
    {
        cJSON_AddNullToObject(valveObj, "valve_mac");
        cJSON_AddStringToObject(valveObj, "valve_state", "disconnected");
    }

    // --- SENSOR DATA ---
    cJSON *sensorsObj = cJSON_CreateObject();
    cJSON_AddItemToObject(valveObj, "leak_sensors", sensorsObj);

    if (pkt != NULL)
    {
        char sensorKey[32];
        snprintf(sensorKey, sizeof(sensorKey), "sensor_0x%08lX", pkt->sensorId);

        cJSON *thisSensor = cJSON_CreateObject();
        cJSON_AddItemToObject(sensorsObj, sensorKey, thisSensor);

        cJSON_AddNumberToObject(thisSensor, "battery", pkt->batteryPercentage);
        // LOGIC: 1=Leak, 0=No Leak
        cJSON_AddBoolToObject(thisSensor, "leak_state", (pkt->leakStatus == 1));
        cJSON_AddNumberToObject(thisSensor, "rssi", pkt->rssi);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(IOTHUB_TAG, "Connected to Azure IoT Hub!");
        g_iot_hub_connected = true;
        char sub_topic[128];
        snprintf(sub_topic, sizeof(sub_topic), "devices/%s/messages/devicebound/#", AZURE_DEVICE_ID);
        esp_mqtt_client_subscribe(mqtt_client, sub_topic, 1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(IOTHUB_TAG, "Disconnected.");
        g_iot_hub_connected = false;
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(IOTHUB_TAG, "Received C2D Message!");
        if (strstr(event->data, "VALVE_OPEN"))
        {
            ble_valve_connect();
            ble_valve_open();
        }
        else if (strstr(event->data, "VALVE_CLOSE"))
        {
            ble_valve_connect();
            ble_valve_close();
        }
        break;
    default:
        break;
    }
}

static void initialize_sntp(void)
{
    ESP_LOGI(IOTHUB_TAG, "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;

    // Block until time is valid (Year > 2020)
    while (timeinfo.tm_year < (2020 - 1900))
    {
        ESP_LOGI(IOTHUB_TAG, "Waiting for time... (%d)", ++retry);
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now);
        localtime_r(&now, &timeinfo);
        if (retry > 60)
            break; // 2 minutes timeout
    }
    ESP_LOGI(IOTHUB_TAG, "Time synced: %s", asctime(&timeinfo));
}

static void init_gateway_id(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(g_gateway_id, sizeof(g_gateway_id), "GW-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(IOTHUB_TAG, "Gateway ID: %s", g_gateway_id);
}

void iothub_task(void *param)
{
    ESP_LOGI(IOTHUB_TAG, "Waiting for Wi-Fi...");
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ESP_LOGI(IOTHUB_TAG, "Starting IOT Hub Task...");

    init_gateway_id();
    initialize_sntp();

    char resource_uri[128];
    snprintf(resource_uri, sizeof(resource_uri), "%s.azure-devices.net/devices/%s", AZURE_HUB_NAME, AZURE_DEVICE_ID);
    char *sas_token = generate_sas_token(resource_uri, AZURE_PRIMARY_KEY, 31536000);

    char uri[128], username[256];
    snprintf(uri, sizeof(uri), "mqtts://%s.azure-devices.net", AZURE_HUB_NAME);
    snprintf(username, sizeof(username), "%s.azure-devices.net/%s/?api-version=2021-04-12", AZURE_HUB_NAME, AZURE_DEVICE_ID);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials = {
            .username = username,
            .client_id = AZURE_DEVICE_ID,
            .authentication = {.password = sas_token}},
        .session.keepalive = 60,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    // Drain queues
    lora_packet_t dummy_pkt;
    uint8_t dummy_byte;
    while (xQueueReceive(lora_rx_queue, &dummy_pkt, 0) == pdTRUE)
        ;
    while (xQueueReceive(ble_update_queue, &dummy_byte, 0) == pdTRUE)
        ;

    QueueSetHandle_t evt_queue_set = xQueueCreateSet(15);
    xQueueAddToSet(lora_rx_queue, evt_queue_set);
    xQueueAddToSet(ble_update_queue, evt_queue_set);

    lora_packet_t pkt;
    QueueSetMemberHandle_t active_queue;
    char topic[128];
    snprintf(topic, sizeof(topic), "devices/%s/messages/events/", AZURE_DEVICE_ID);

    ESP_LOGI(IOTHUB_TAG, "QueueSet Initialized.");

    while (1)
    {
        active_queue = xQueueSelectFromSet(evt_queue_set, portMAX_DELAY);

        if (g_iot_hub_connected)
        {
            char *json_payload = NULL;

            if (active_queue == lora_rx_queue)
            {
                if (xQueueReceive(lora_rx_queue, &pkt, 0))
                {
                    ESP_LOGI(IOTHUB_TAG, "Event: LoRa Packet");
                    json_payload = generate_json_telemetry(&pkt);
                }
            }
            else if (active_queue == ble_update_queue)
            {
                if (xQueueReceive(ble_update_queue, &dummy_byte, 0))
                {
                    ESP_LOGI(IOTHUB_TAG, "Event: BLE Change");
                    json_payload = generate_json_telemetry(NULL);
                }
            }

            if (json_payload)
            {
                ESP_LOGI(IOTHUB_TAG, "Uploading: %s", json_payload);
                esp_mqtt_client_publish(mqtt_client, topic, json_payload, 0, 1, 0);
                free(json_payload);
            }
        }
        else
        {
            // Must drain even if disconnected
            if (active_queue == lora_rx_queue)
                xQueueReceive(lora_rx_queue, &pkt, 0);
            if (active_queue == ble_update_queue)
                xQueueReceive(ble_update_queue, &dummy_byte, 0);
        }
    }
}

void initialize_iothub(void)
{
    xTaskCreate(iothub_task, "iothub_task", 8192, NULL, 5, &iothub_task_handle);
}