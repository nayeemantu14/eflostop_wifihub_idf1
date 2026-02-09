#include "app_uart.h"

QueueHandle_t uartQueueHandler = NULL;
TaskHandle_t uartTaskHandler = NULL;

static void uart_event_task(void *params);

void configureUART(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(UART_TAG, "Configured UART");
    uart_driver_install(UART_NUM_1, RXBUFSIZE, TXBUFSIZE, 20, &uartQueueHandler, 0);
    uart_enable_pattern_det_baud_intr(UART_NUM_1, '+', 3, 20000, 10, 10);
    uart_pattern_queue_reset(UART_NUM_1, 20);

    xTaskCreate(uart_event_task, "uart_event_task", 4096, NULL, 12, &uartTaskHandler);
}

static void uart_event_task(void *params)
{
    (void)params;

    uart_event_t uart_event;
    size_t buffered_size = 0;

    // Allocate once, keep for life of task (or make it a stack array)
    uint8_t *rx = malloc(RXBUFSIZE);
    if (!rx)
    {
        ESP_LOGE(UART_TAG, "malloc(RXBUFSIZE) failed");
        vTaskDelete(NULL);
        return;
    }

    while (true)
    {
        if (xQueueReceive(uartQueueHandler, &uart_event, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        ESP_LOGI(UART_TAG, "uart[%d] event:", UART_NUM_1);

        switch (uart_event.type)
        {
        case UART_DATA:
        {
            int to_read = (int)uart_event.size;
            if (to_read >= RXBUFSIZE)
            {
                to_read = RXBUFSIZE - 1; // keep space for '\0'
            }

            int len = uart_read_bytes(UART_NUM_1, rx, to_read, portMAX_DELAY);
            if (len < 0)
            {
                ESP_LOGE(UART_TAG, "uart_read_bytes failed");
                break;
            }

            // If you want to print as a string safely:
            rx[len] = '\0';
            ESP_LOGI(UART_TAG, "[DATA]: %.*s", len, (char *)rx);

            break;
        }

        case UART_FIFO_OVF:
            ESP_LOGI(UART_TAG, "hw fifo overflow");
            uart_flush_input(UART_NUM_1);
            xQueueReset(uartQueueHandler);
            break;

        case UART_BUFFER_FULL:
            ESP_LOGI(UART_TAG, "ring buffer full");
            uart_flush_input(UART_NUM_1);
            xQueueReset(uartQueueHandler);
            break;

        case UART_BREAK:
            ESP_LOGI(UART_TAG, "uart rx break");
            break;

        case UART_PARITY_ERR:
            ESP_LOGI(UART_TAG, "uart parity error");
            break;

        case UART_FRAME_ERR:
            ESP_LOGI(UART_TAG, "uart frame error");
            break;

        case UART_PATTERN_DET:
        {
            uart_get_buffered_data_len(UART_NUM_1, &buffered_size);
            int pos = uart_pattern_pop_pos(UART_NUM_1);

            ESP_LOGI(UART_TAG, "Detected pattern at pos: %d, buffered size: %d", pos, buffered_size);
            if (pos == -1)
            {
                uart_flush_input(UART_NUM_1);
            }
            else
            {
                int to_read = pos;
                if (to_read >= RXBUFSIZE)
                {
                    to_read = RXBUFSIZE - 1;
                }

                int len = uart_read_bytes(UART_NUM_1, rx, to_read, 100 / portTICK_PERIOD_MS);
                if (len < 0)
                    len = 0;
                rx[len] = '\0';

                uint8_t pat[3 + 1] = {0};
                uart_read_bytes(UART_NUM_1, pat, 3, 100 / portTICK_PERIOD_MS);

                ESP_LOGI(UART_TAG, "read data: %.*s", len, (char *)rx);
                ESP_LOGI(UART_TAG, "read pat: %s", (char *)pat);
            }
            break;
        }

        default:
            ESP_LOGI(UART_TAG, "uart event type: %d", uart_event.type);
            break;
        }
    }
    free(rx);
    vTaskDelete(NULL);
}