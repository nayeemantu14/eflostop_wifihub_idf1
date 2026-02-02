#include "app_iothub.h"

TaskHandle_t iothub_task_handle = NULL;

void initialize_iothub(void)

{
    ESP_LOGI(IOTHUB_TAG, "IoT Hub initialized");
    xTaskCreate(iothub_task, "iothub_task", 8192, NULL, 5, &iothub_task_handle);

}

void iothub_task(void *param)

{

    lora_packet_t pkt;

    while (1)

    {
        // Main IoT Hub task loop
        if (xQueueReceive(lora_rx_queue, &pkt, portMAX_DELAY) == pdTRUE)

        {
            // Process received packet
            ESP_LOGI(IOTHUB_TAG, "Processing packet from sensor ID: 0x%lX", pkt.sensorId);
            //send_packet_to_iothub(pkt.data, pkt.length);
        }
    }
}
void send_packet_to_iothub(const uint8_t* data, size_t length)
{
    // Placeholder function to send data to IoT Hub
   ESP_LOGI(IOTHUB_TAG, "Sending packet to IoT Hub, length: %d", length);
}

