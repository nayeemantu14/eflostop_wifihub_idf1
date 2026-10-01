#include "monitoring.h"
#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "MONITOR";

/* ---- Internal-DMA heap and failed allocations (2.1.4 WP0, the G0 baseline) ----
 * The internal DMA-capable heap is what the Wi-Fi driver, lwIP and TLS draw on, and what ran
 * out beside the SoftAP in the E4 bench log (152 B left, 4 failed allocations). The monitor
 * task samples its free size and largest free block every IDMA_SAMPLE_MS and prints, next to
 * its heap line, the last sample and the lowest of each since the previous line, with the
 * number of failed allocations since boot. The largest block costs a walk of the heap under
 * its lock, once a second, on this core-1 task at priority 1. */
#define IDMA_CAPS       (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)
#define IDMA_SAMPLE_MS  1000
_Static_assert(MONITORING_INTERVAL_MS % IDMA_SAMPLE_MS == 0, "whole samples between two reports");

typedef struct {
    size_t free_now;       // the last sample
    size_t largest_now;
    size_t min_free;       // the lowest since the last report, that sample included
    size_t min_largest;
} idma_stats_t;

/* heap_caps calls alloc_failed_hook() for every allocation that fails, in the failing call's
 * context: any task, maybe inside a critical section or with interrupts off. So the hook only
 * stores: no logging, no allocation, no lock, no RTOS call. The free size it reads is a plain
 * sum of the matching heaps' counters. A report that races a second failure can mix the two in
 * its "last" details; the count is what the gates use.
 * The hook and heap_caps_get_free_size() are in flash (IRAM is full), but heap_caps calls the
 * hook from its IRAM failure path: an allocation that fails while the flash cache is disabled
 * (an IRAM-safe ISR during a flash write) would fault here instead of returning NULL. None was
 * found in this image: the one IRAM ISR (reset_button.c) does not allocate, and the BT
 * controller's malloc wrapper is in flash itself. Code that ever allocates there needs this
 * hook unregistered first. */
static volatile uint32_t s_alloc_fails = 0;             // failed allocations since boot
static volatile uint32_t s_alloc_fail_size = 0;         // the last one: bytes asked for,
static volatile uint32_t s_alloc_fail_caps = 0;         // its capabilities,
static volatile uint32_t s_alloc_fail_free = 0;         // the free bytes with those then,
static const char *volatile s_alloc_fail_func = NULL;   // and the heap function that failed

static void alloc_failed_hook(size_t size, uint32_t caps, const char *function_name)
{
    s_alloc_fail_size = (uint32_t)size;
    s_alloc_fail_caps = caps;
    s_alloc_fail_free = (uint32_t)heap_caps_get_free_size(caps);
    s_alloc_fail_func = function_name;
    s_alloc_fails++;
}

void monitoring_alloc_fail_hook_init(void)
{
    // Fails only for a NULL callback.
    heap_caps_register_failed_alloc_callback(alloc_failed_hook);
}

static void idma_sample(idma_stats_t *st)
{
    st->free_now    = heap_caps_get_free_size(IDMA_CAPS);
    st->largest_now = heap_caps_get_largest_free_block(IDMA_CAPS);
    if (st->free_now < st->min_free)
        st->min_free = st->free_now;
    if (st->largest_now < st->min_largest)
        st->min_largest = st->largest_now;
}

// The internal-DMA line; the next interval's lows then start from the last sample. The last
// failed allocation is shown when the count changed since the previous line.
// min_ever, last on the line (2.1.4 WP1): the all-time low of the internal-DMA free size, which
// the allocator records at every allocation. The 1 s samples miss short dips (by about 20 KB on
// the CP5 bench); this does not. It is the sum of each internal-DMA heap's own lowest free size,
// reached at different times, so a lower bound of the true low, like the heap line's min_ever.
static void idma_report(idma_stats_t *st, uint32_t *fails_reported)
{
    uint32_t fails = s_alloc_fails;
    unsigned long min_ever = (unsigned long)heap_caps_get_minimum_free_size(IDMA_CAPS);
    if (fails == *fails_reported) {
        ESP_LOGI(TAG, "idma: free=%lu min=%lu largest=%lu min_largest=%lu allocfail=%lu min_ever=%lu",
                 (unsigned long)st->free_now, (unsigned long)st->min_free,
                 (unsigned long)st->largest_now, (unsigned long)st->min_largest,
                 (unsigned long)fails, min_ever);
    } else {
        const char *func = s_alloc_fail_func;
        ESP_LOGI(TAG, "idma: free=%lu min=%lu largest=%lu min_largest=%lu allocfail=%lu "
                 "(last: %lu B, caps 0x%lx, %lu B free, %s) min_ever=%lu",
                 (unsigned long)st->free_now, (unsigned long)st->min_free,
                 (unsigned long)st->largest_now, (unsigned long)st->min_largest,
                 (unsigned long)fails, (unsigned long)s_alloc_fail_size,
                 (unsigned long)s_alloc_fail_caps, (unsigned long)s_alloc_fail_free,
                 func ? func : "?", min_ever);
        *fails_reported = fails;
    }
    st->min_free    = st->free_now;
    st->min_largest = st->largest_now;
}

static void monitoring_task(void *pvParameter)
{
    (void)pvParameter;

    size_t prev_free = 0;
    bool low_heap_warned = false;
    idma_stats_t idma = { .min_free = SIZE_MAX, .min_largest = SIZE_MAX };
    uint32_t fails_reported = 0;

    idma_sample(&idma);   // for the first report

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
        idma_report(&idma, &fails_reported);

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
        // The same MONITORING_INTERVAL_MS to the next report, with an internal-DMA sample every
        // second, the last one just before that report.
        for (int i = 0; i < MONITORING_INTERVAL_MS / IDMA_SAMPLE_MS; i++) {
            vTaskDelay(pdMS_TO_TICKS(IDMA_SAMPLE_MS));
            idma_sample(&idma);
        }
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
