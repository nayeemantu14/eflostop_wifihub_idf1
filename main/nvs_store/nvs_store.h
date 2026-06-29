#ifndef NVS_STORE_H
#define NVS_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Dedicated NVS partition for commissioning / identity data that MUST survive
 * a WiFi reset and any default-partition ("nvs") full erase.
 *
 * The physical reset button clears only the WiFi credentials (stored in the
 * default "nvs" partition by wifi_manager). Provisioning (valve MAC, LoRa
 * sensors, BLE leak sensors), hub identity, DPS cache, sensor metadata, and
 * rules-engine state live in THIS partition instead, so neither the button nor
 * the default-partition corruption-recovery erase in app_main() can wipe them.
 * Full factory reset (erasing this partition) is app-only via the C2D
 * "decommission" command.
 *
 * Modules that store commissioning data open their namespaces with
 *   nvs_open_from_partition(NVS_PROV_PARTITION, <ns>, mode, &h)
 * instead of nvs_open(<ns>, mode, &h).
 */
#define NVS_PROV_PARTITION "nvs_prov"

/*
 * Initialize the dedicated commissioning NVS partition. Call once at boot,
 * after nvs_flash_init() (default partition) and BEFORE any module that reads
 * or writes commissioning/identity data (hub_identity, provisioning_manager,
 * dps_client, sensor_meta, rules_engine).
 *
 * Corruption recovery here erases ONLY this partition — it never touches the
 * default partition (and vice-versa), which is the whole point of the split.
 */
void nvs_store_init(void);

#ifdef __cplusplus
}
#endif

#endif /* NVS_STORE_H */
