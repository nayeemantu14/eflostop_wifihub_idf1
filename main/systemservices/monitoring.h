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
 *        Warns when heap drops below HEAP_LOW_WATERMARK.
 *        Runs on core 1 if available, otherwise core 0.
 */
void monitoring_init(void);

#ifdef __cplusplus
}
#endif

#endif // MONITORING_H
