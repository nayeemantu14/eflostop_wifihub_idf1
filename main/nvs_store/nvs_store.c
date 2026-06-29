#include "nvs_store/nvs_store.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "NVS_STORE";

void nvs_store_init(void)
{
    esp_err_t err = nvs_flash_init_partition(NVS_PROV_PARTITION);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* Full/changed: erase ONLY this partition (the default "nvs" partition is
         * never touched) and retry. */
        ESP_LOGW(TAG, "commissioning partition '%s' full/changed — erasing ONLY this partition",
                 NVS_PROV_PARTITION);
        err = nvs_flash_erase_partition(NVS_PROV_PARTITION);
        if (err == ESP_OK) {
            err = nvs_flash_init_partition(NVS_PROV_PARTITION);
        }
    }
    if (err != ESP_OK) {
        /* Partition missing (e.g. a 1.4.3 image delivered over an old layout) or
         * unrecoverable. Fail closed instead of panic-looping: the hub still boots
         * (WiFi/BLE work); commissioning reads/writes return errors so it comes up
         * unprovisioned and can be re-commissioned via the app. */
        ESP_LOGE(TAG, "commissioning NVS partition '%s' unavailable: %s — booting unprovisioned",
                 NVS_PROV_PARTITION, esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "commissioning NVS partition '%s' ready", NVS_PROV_PARTITION);
}
