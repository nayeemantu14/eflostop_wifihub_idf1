#include "monitoring.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "MONITOR";

static void monitoring_task(void *pvParameter)
{
    (void)pvParameter;

    size_t prev_free = 0;
    bool low_heap_warned = false;

    for (;;) {
        size_t free_heap      = esp_get_free_heap_size();
        size_t min_free_ever  = esp_get_minimum_free_heap_size();
        size_t largest_block  = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
        uint32_t uptime_s     = (uint32_t)(esp_timer_get_time() / 1000000);

        ESP_LOGI(TAG, "heap: free=%lu min_ever=%lu largest_blk=%lu uptime=%lus",
                 (unsigned long)free_heap,
                 (unsigned long)min_free_ever,
                 (unsigned long)largest_block,
                 (unsigned long)uptime_s);

        // Warn if heap is getting low
        if (free_heap < HEAP_LOW_WATERMARK && !low_heap_warned) {
            ESP_LOGW(TAG, "LOW HEAP WARNING: %lu bytes free (watermark=%d)",
                     (unsigned long)free_heap, HEAP_LOW_WATERMARK);
            low_heap_warned = true;
        } else if (free_heap >= HEAP_LOW_WATERMARK) {
            low_heap_warned = false;
        }

        // Detect significant heap drops between intervals
        if (prev_free > 0 && free_heap + 4096 < prev_free) {
            ESP_LOGW(TAG, "Heap dropped %ld bytes since last check",
                     (long)(prev_free - free_heap));
        }

        prev_free = free_heap;
        vTaskDelay(pdMS_TO_TICKS(MONITORING_INTERVAL_MS));
    }
}

void monitoring_init(void)
{
#if CONFIG_SOC_CPU_CORES_NUM > 1
    // Pin to core 1 to avoid contending with the main app on core 0
    xTaskCreatePinnedToCore(monitoring_task, "monitor", 3072, NULL, 1, NULL, 1);
#else
    xTaskCreate(monitoring_task, "monitor", 3072, NULL, 1, NULL);
#endif
    ESP_LOGI(TAG, "System monitoring started (interval=%ds)", MONITORING_INTERVAL_MS / 1000);
}
