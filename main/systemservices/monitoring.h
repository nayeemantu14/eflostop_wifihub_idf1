#ifndef MONITORING_H
#define MONITORING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MONITORING_INTERVAL_MS  10000   // 10 seconds between reports
#define HEAP_LOW_WATERMARK      8192    // Warn below this free heap level

/**
 * @brief Initialize and start the system monitoring task.
 *        Logs free heap, minimum ever free heap, and largest free block.
 *        Next to it, the internal DMA-capable heap (sampled every second: free size, largest
 *        free block and the lowest of each since the previous report) and the number of
 *        failed allocations.
 *        Warns when heap drops below HEAP_LOW_WATERMARK.
 *        Runs on core 1 if available, otherwise core 0.
 */
void monitoring_init(void);

/**
 * @brief Register the failed-allocation counter (heap_caps_register_failed_alloc_callback()).
 *        Call first thing in app_main(), so failures during start-up are counted too; the
 *        monitoring task reports the count.
 */
void monitoring_alloc_fail_hook_init(void);

#ifdef __cplusplus
}
#endif

#endif // MONITORING_H
