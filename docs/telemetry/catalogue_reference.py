# ruff: noqa: E501
"""Part II reference content, plus findings, options and open questions.

Companion to catalogue_data.py and catalogue_content.py.

Every enum member below is taken from a `switch` or literal in firmware source and carries its
citation. Where a value can only arise from a `default:` branch that is stated, because it tells a
consumer the value is a fallback rather than a deliberate signal.
"""

# --------------------------------------------------------------------------------------
# All 19 enum-valued telemetry fields, every member, cited.
# --------------------------------------------------------------------------------------

ENUMS = [
    {
        "paths": ["type"], "title": "Envelope discriminator",
        "citation": "main/telemetry/telemetry_v2.c:89; values passed at :336, :374, :610, :637, :659, :679, :700",
        "note": "This string also has behaviour inside the firmware: publish_json compares it against \"event\" to decide whether an offline message is buffered or dropped (main/telemetry/telemetry_v2.c:115-122).",
        "members": [
            ("lifecycle", "Boot / online announcement."),
            ("snapshot", "Full state roll-up."),
            ("event", "A discrete occurrence."),
        ],
    },
    {
        "paths": ["data.event"], "title": "Event discriminator",
        "citation": "See per-member citations",
        "note": "Eighteen members. Two of them (rules_engine, health_engine) are fallback labels emitted only if an internally generated payload could not be re-parsed; they accompany data.raw and should never be seen in normal operation.",
        "members": [
            ("online", "Hub has an MQTT session. main/telemetry/telemetry_v2.c:340"),
            ("valve_state_changed", "The valve opened or closed. main/iothub/app_iothub.c:1789-1790"),
            ("valve_flood_detected", "The valve's own flood probe went wet. main/iothub/app_iothub.c:1778-1779"),
            ("valve_flood_cleared", "The valve's own flood probe went dry. main/iothub/app_iothub.c:1778-1779"),
            ("leak_detected", "A leak sensor went wet. main/iothub/app_iothub.c:1748, :1808"),
            ("leak_cleared", "A leak sensor went dry. main/iothub/app_iothub.c:1748, :1808"),
            ("cmd_ack", "Result of an inbound C2D command. main/telemetry/telemetry_v2.c:704"),
            ("water_access_override_enabled", "A 24-hour override window started. main/rules_engine/rules_engine.c:215"),
            ("auto_close", "The hub closed the valve in response to a leak. main/rules_engine/rules_engine.c:333, :934"),
            ("auto_close_blocked_override", "A leak occurred but auto-close was suppressed by an active override. main/rules_engine/rules_engine.c:469"),
            ("rmleak_cleared", "The leak interlock was cleared. main/rules_engine/rules_engine.c:678"),
            ("auto_close_reenabled", "An override window was cancelled by command. main/rules_engine/rules_engine.c:720"),
            ("water_access_override_expired", "The override window elapsed. main/rules_engine/rules_engine.c:1025"),
            ("rmleak_auto_cleared", "The interlock cleared itself on timeout. main/rules_engine/rules_engine.c:1082"),
            ("device_offline", "A peer crossed into the critical health rating. main/health_engine/health_engine.c:587-588"),
            ("device_recovered", "A peer crossed out of the critical health rating. main/health_engine/health_engine.c:587-588"),
            ("rules_engine", "Fallback label; accompanies data.raw when a rules payload could not be re-parsed. main/telemetry/telemetry_v2.c:668"),
            ("health_engine", "Fallback label; accompanies data.raw when a health payload could not be re-parsed. main/telemetry/telemetry_v2.c:687"),
        ],
    },
    {
        "paths": ["data.reset_reason"], "title": "Reset cause (lifecycle only) — LOSSY",
        "citation": "main/telemetry/telemetry_v2.c:341; mapping :128-141",
        "note": "Two distinct losses. The three ESP-IDF watchdog causes collapse into one string, so a hung task cannot be distinguished from a blocked interrupt. And the default: branch absorbs everything else including an operator pressing the reset button, which then looks identical to a genuinely unknown cause. [EXTERNAL] the full esp_reset_reason_t member list is ESP-IDF's, at $IDF_PATH/components/esp_system/include/esp_system.h.",
        "members": [
            ("power_on", "ESP_RST_POWERON. main/telemetry/telemetry_v2.c:131"),
            ("software", "ESP_RST_SW — a deliberate esp_restart(). main/telemetry/telemetry_v2.c:132"),
            ("panic", "ESP_RST_PANIC — a crash. main/telemetry/telemetry_v2.c:133"),
            ("watchdog", "ESP_RST_INT_WDT, ESP_RST_TASK_WDT and ESP_RST_WDT — three causes, one string. main/telemetry/telemetry_v2.c:134-136"),
            ("brownout", "ESP_RST_BROWNOUT — supply voltage collapsed. main/telemetry/telemetry_v2.c:137"),
            ("deep_sleep", "ESP_RST_DEEPSLEEP. main/telemetry/telemetry_v2.c:138"),
            ("unknown", "The default: branch — every other cause, including an external pin reset. main/telemetry/telemetry_v2.c:139"),
        ],
    },
    {
        "paths": ["data.reason"], "title": "Why this message was produced — two disjoint vocabularies on one key",
        "citation": "snapshot meaning main/telemetry/telemetry_v2.c:383 with values from main/iothub/app_iothub.c:581-591; rules meaning main/rules_engine/rules_engine.c:722",
        "note": "The same key carries two unrelated vocabularies. In a snapshot it names the trigger that caused the snapshot. In the auto_close_reenabled rules event it names why the override was cancelled. They are told apart only by type and data.event.",
        "members": [
            ("heartbeat", "The periodic timer; also the default: fallback. main/iothub/app_iothub.c:588-589"),
            ("event", "A telemetry event just fired and pulled a snapshot with it. main/iothub/app_iothub.c:584"),
            ("commission", "A provisioning change. main/iothub/app_iothub.c:585"),
            ("boot", "The first snapshot after boot. main/iothub/app_iothub.c:586"),
            ("fast", "An early snapshot taken as soon as the valve is ready; incomplete by design. main/iothub/app_iothub.c:587"),
            ("decommission", "Passed as a literal, not via the enum, immediately before the hub restarts. main/iothub/app_iothub.c:1570"),
            ("c2d_command", "Rules-event meaning only: the override was cancelled by a cloud command. main/rules_engine/rules_engine.c:722"),
        ],
    },
    {
        "paths": ["data.system_health.rating", "data.valve.rating", "data.lora_sensors[].rating", "data.ble_leak_sensors[].rating", "data.rating", "data.prev_rating"],
        "title": "Health rating — one vocabulary, six field paths",
        "citation": "health_rating_to_str, main/health_engine/health_engine.c:57-66",
        "note": "The most consistently reused enum in the contract: six different field names, one vocabulary. data.system_health.rating is the worst rating across all devices in the same snapshot.",
        "members": [
            ("excellent", "HEALTH_EXCELLENT. main/health_engine/health_engine.c:60"),
            ("good", "HEALTH_GOOD. main/health_engine/health_engine.c:61"),
            ("warning", "HEALTH_WARNING. main/health_engine/health_engine.c:62"),
            ("critical", "HEALTH_CRITICAL. main/health_engine/health_engine.c:63"),
            ("unknown", "The default: branch. main/health_engine/health_engine.c:64"),
        ],
    },
    {
        "paths": ["data.dev_type"], "title": "Device class on health events — CHANGED IN 1.8.0",
        "citation": "dev_type_to_str, main/health_engine/health_engine.c:72-80; emitted :590",
        "note": "The ble_leak_sensor value was ble_leak at git HEAD b62b25e and is ble_leak_sensor in the working tree, changed deliberately to match source_type on leak events (intent comment at main/health_engine/health_engine.c:68-71). Both spellings will exist in stored history and in the fleet simultaneously — see §18.",
        "members": [
            ("valve", "HEALTH_DEV_VALVE. main/health_engine/health_engine.c:75"),
            ("lora", "HEALTH_DEV_LORA. main/health_engine/health_engine.c:76"),
            ("ble_leak_sensor", "HEALTH_DEV_BLE_LEAK. Was ble_leak at HEAD. main/health_engine/health_engine.c:77"),
            ("unknown", "The default: branch. main/health_engine/health_engine.c:78"),
        ],
    },
    {
        "paths": ["data.source_type"], "title": "What kind of thing reported the leak",
        "citation": "source_to_str, main/rules_engine/rules_engine.c:259-267",
        "note": "The leak-event path does NOT use source_to_str — it passes string literals directly (main/iothub/app_iothub.c:1749, :1809), so this vocabulary is maintained in two places. The value is then re-parsed inside the firmware (strcmp against \"lora\") to select a metadata table at main/telemetry/telemetry_v2.c:648-649, so the wire string is load-bearing in the device. Note reconnect is a pseudo-source that is not a member of the underlying C enum.",
        "members": [
            ("lora", "A LoRa sensor. main/iothub/app_iothub.c:1749"),
            ("ble_leak_sensor", "A BLE leak sensor. main/iothub/app_iothub.c:1809; enum member main/rules_engine/rules_engine.c:262"),
            ("valve_flood", "The valve's own on-board flood probe. main/rules_engine/rules_engine.c:264"),
            ("reconnect", "Pseudo-source used only by the valve-reconnect auto_close variant; not a leak_source_t member. main/rules_engine/rules_engine.c:935"),
            ("unknown", "The default: branch. main/rules_engine/rules_engine.c:265"),
        ],
    },
    {
        "paths": ["data.location.code", "data.lora_sensors[].location.code", "data.ble_leak_sensors[].location.code"],
        "title": "Where the sensor is installed",
        "citation": "s_location_strings, main/sensor_meta/sensor_meta.c:23-27; range-guarded lookup :414-420; emitted main/telemetry/telemetry_v2.c:148 and main/rules_engine/rules_engine.c:344",
        "note": "The only enum in the contract with a compile-time guard: a _Static_assert binds the string table length to LOC_COUNT (main/sensor_meta/sensor_meta.c:28-29), so the table and the C enum cannot drift apart. An out-of-range code falls back to unknown.",
        "members": [
            ("unknown", "No location assigned, or an out-of-range code."),
            ("bathroom", "Bathroom."), ("kitchen", "Kitchen."), ("laundry", "Laundry."),
            ("garage", "Garage."), ("garden", "Garden."), ("basement", "Basement."),
            ("utility", "Utility room."), ("hallway", "Hallway."), ("bedroom", "Bedroom."),
            ("living_room", "Living room."), ("attic", "Attic."), ("outdoor", "Outdoor."),
        ],
    },
    {
        "paths": ["data.valve.state"], "title": "Valve position in a snapshot — 4 members",
        "citation": "main/telemetry/telemetry_v2.c:429-430 (open/closed/unknown ternary), :442 (disconnected)",
        "note": "This field merges two different facts: the valve's mechanical position AND whether there is a BLE link at all. unknown means the GATT characteristic byte was neither 0 nor 1; disconnected means there is no link. The link state is also carried separately as data.valve.connected, so it appears twice.",
        "members": [
            ("open", "Position byte == 1. main/telemetry/telemetry_v2.c:429-430"),
            ("closed", "Position byte == 0. main/telemetry/telemetry_v2.c:429-430"),
            ("unknown", "Position byte was neither 0 nor 1, including the -1 not-yet-read sentinel. main/telemetry/telemetry_v2.c:430"),
            ("disconnected", "No BLE link to the valve. Snapshot only. main/telemetry/telemetry_v2.c:442"),
        ],
    },
    {
        "paths": ["data.valve_state"], "title": "Valve position on a valve event — 3 members",
        "citation": "main/telemetry/telemetry_v2.c:617-618",
        "note": "The same ternary as data.valve.state but WITHOUT the disconnected member, because a valve event cannot be produced while the valve is disconnected (see §10, valve event).",
        "members": [
            ("open", "Position byte == 1. main/telemetry/telemetry_v2.c:617"),
            ("closed", "Position byte == 0. main/telemetry/telemetry_v2.c:617"),
            ("unknown", "Neither 0 nor 1. main/telemetry/telemetry_v2.c:618"),
        ],
    },
    {
        "paths": ["data.trigger"], "title": "What started the override window",
        "citation": "main/rules_engine/rules_engine.c:216; callers :846, :978, :1128",
        "note": "The ternary's default is button, so any path that does not explicitly say c2d_command reports button.",
        "members": [
            ("button", "A physical long-press on the valve, or the reconnect inference. main/rules_engine/rules_engine.c:978, :1128"),
            ("c2d_command", "A remote override_enable command. main/rules_engine/rules_engine.c:846"),
        ],
    },
    {
        "paths": ["data.status"], "title": "Command outcome",
        "citation": "main/telemetry/telemetry_v2.c:708",
        "note": "A plain success flag rendered as a string. The failure detail is free text in data.error.detail; there is no machine-readable failure class — see F-06.",
        "members": [
            ("ok", "The command succeeded."),
            ("error", "The command failed; data.error is then present."),
        ],
    },
]

# --------------------------------------------------------------------------------------
# §13 Twin plane
# --------------------------------------------------------------------------------------

TWIN_REPORTED = [
    ("fw_version", "string", "always", "Hub firmware version, from the build-time PROJECT_VER.", "main/iothub/app_iothub.c:953"),
    ("gateway_id", "string", "always", "GW- prefixed hub identity. Same value as telemetry gateway.id, different key name.", "main/iothub/app_iothub.c:954"),
    ("short_id", "string", "always", "Last 4 hex of the gateway id.", "main/iothub/app_iothub.c:955"),
    ("hub_name", "string", "always — emitted even when empty", "User-assigned hub name. NOTE: telemetry omits gateway.name when empty; the twin always emits the key.", "main/iothub/app_iothub.c:956"),
    ("provisioned", "boolean", "always", "Whether the hub has completed commissioning.", "main/iothub/app_iothub.c:957"),
    ("valve_mac", "string", "omitted when no valve is provisioned", "The provisioned valve MAC.", "main/iothub/app_iothub.c:961"),
    ("lora_sensor_count", "number", "always", "How many LoRa sensors are commissioned.", "main/iothub/app_iothub.c:966"),
    ("ble_leak_sensor_count", "number", "always", "How many BLE leak sensors are commissioned.", "main/iothub/app_iothub.c:971"),
    ("auto_close_enabled", "boolean", "omitted if the rules config cannot be read", "Whether the hub will close the valve automatically on a leak.", "main/iothub/app_iothub.c:975"),
    ("trigger_mask", "number", "omitted if the rules config cannot be read", "Bitmask of which leak sources may trigger auto-close. bit0=BLE, bit1=LoRa, bit2=valve flood.", "main/iothub/app_iothub.c:976"),
    ("uptime_s", "number", "always", "Seconds since boot AT THE MOMENT THE TWIN WAS PATCHED. Only three triggers write the twin, so this can be arbitrarily stale — it is not a live value.", "main/iothub/app_iothub.c:979"),
    ("free_heap", "number", "always", "Free heap in bytes. The ONLY hub diagnostic that exists on the twin and nowhere in telemetry.", "main/iothub/app_iothub.c:981"),
]

TWIN_DESIRED = [
    ("hub_name", "string", "Sets the user-assigned hub name. Written to NVS, then echoed into twin reported and into telemetry gateway.name. Max 31 characters; a longer value is rejected and logged, leaving the stored name unchanged.", "main/iothub/app_iothub.c:1030-1040; length limit main/hub_identity/hub_identity.h:10"),
    ("snapshot_interval_s", "number", "Sets the snapshot heartbeat period. Range-checked against 60..3600; a value outside that range is DISCARDED with a warning, not clamped, so the previous interval stays in force. This is the only validated numeric on any plane. Held in RAM only, so it reverts to the 300 s default on every reboot, and it is NOT echoed into twin reported, so the cloud gets no acknowledgement that it was applied.", "main/iothub/app_iothub.c:1019-1028; range check :1022; default main/telemetry/telemetry_v2.c:42"),
]

HUB_NAME_TRACE = """**Two inbound paths reach one stored value.** The hub name can be set either by writing the twin
desired property `hub_name`, or by sending the C2D command `set_hub_name`. Both write the same NVS value, and
that one value then surfaces on two outbound planes under two different key names, with two different presence
rules.

    INBOUND                                    STORED            OUTBOUND
    twin desired  hub_name  ──┐
                              ├──►  NVS "hub_ident"  ──┬──►  twin reported  hub_name   (always emitted)
    C2D  set_hub_name  ───────┘                        └──►  D2C  gateway.name         (OMITTED if empty)

Citations: twin desired path `main/iothub/app_iothub.c:1030-1040`; C2D command path `:908-935`; twin reported
emission `:956`; telemetry emission with its emptiness test `main/telemetry/telemetry_v2.c:82-83`.

Two consequences worth a moment of meeting time. A backend that implements only one inbound path will find the
other silently diverging in the field. And because telemetry omits the key when the name is empty while the twin
always emits it, "no name set" looks different depending on which plane you read."""

# --------------------------------------------------------------------------------------
# §14 Command plane
# --------------------------------------------------------------------------------------

C2D_COMMANDS = [
    ("valve_open", "Open the valve.", "none", "Refused while the valve's RMLEAK interlock is latched — the hub returns an error rather than forwarding the request.", "main/iothub/app_iothub.c:657-667; refusal guard :539-541"),
    ("valve_close", "Close the valve.", "none", "", "main/iothub/app_iothub.c:668-673"),
    ("valve_set_state", "Set the valve to an explicit state.", "state", "", "main/iothub/app_iothub.c:674-700"),
    ("leak_reset", "Clear the leak interlock (RMLEAK) on the valve.", "none", "REFUSED while any leak source is still wet, to prevent restoring water during an active leak. No event is emitted on refusal.", "main/iothub/app_iothub.c:701-713; guard main/rules_engine/rules_engine.c:645-650"),
    ("decommission", "Remove a device, or everything, from the hub's commissioning.", "target, sensor_id", "target is one of valve, lora, ble or all. The legacy plain-text forms are normalised in-process into this payload shape.", "main/iothub/app_iothub.c:714-814; normalisation main/commands/c2d_commands.c:161, :172-173, :192-193, :203"),
    ("override_cancel", "Cancel an active 24-hour water-access override window.", "none", "Produces the auto_close_reenabled event. If leaks are still active the hub immediately re-closes the valve.", "main/iothub/app_iothub.c:815-824; main/rules_engine/rules_engine.c:718-726"),
    ("override_enable", "Start a 24-hour water-access override window remotely.", "none", "Has the largest set of rejection messages of any command.", "main/iothub/app_iothub.c:825-849; rejection set :832-845"),
    ("rules_config", "Set auto-close behaviour.", "auto_close_enabled, trigger_mask, trigger_ble_leak, trigger_lora, trigger_valve_flood", "trigger_mask is stored as a raw byte with no masking or range check, so any value 0..255 is accepted and echoed back on the wire. THREE convenience booleans also exist: trigger_ble_leak, trigger_lora and trigger_valve_flood each set or clear one bit of the mask. They are applied AFTER trigger_mask, so a payload carrying both has the individual booleans win on the bits they name.", "main/iothub/app_iothub.c:850-858; raw mask main/rules_engine/rules_engine.c:560-562; convenience booleans :566, :574, :582"),
    ("sensor_meta", "Set a sensor's location code and label.", "sensor_type, sensor_id, location_code, label", "Accepts three aliases for a BLE sensor: ble_leak_sensor, ble_leak and ble, case-insensitively.", "main/iothub/app_iothub.c:859-873; aliases main/sensor_meta/sensor_meta.h:51-56"),
    ("provision", "Commission the hub: valve, sensors and rules in one message.", "valve_mac, lora_sensors, ble_leak_sensors, rules, sensor_meta", "An optional inline sensor_meta ARRAY may be carried in the same payload, so location metadata can be set during commissioning without a second command. Each element is {sensor_type, sensor_id, location_code, label}.", "main/iothub/app_iothub.c:874-907; inline metadata applied :887, parsed main/sensor_meta/sensor_meta.c:374, element fields :277-278, :294, :324, :338"),
    ("set_hub_name", "Set the user-assigned hub name.", "name", "Max 31 characters. The same value is also settable via the twin desired property hub_name — see §13.3.", "main/iothub/app_iothub.c:908-935"),
]

ACK_CONTRACT = """**An acknowledgement is not guaranteed.** A command produces a `cmd_ack` telemetry event only if
it arrived inside an envelope (`eflostop.cmd` or `eflostop.cmd.v1`) **or** carried a correlation id. The emit is
wrapped in `if (cmd.is_envelope || cmd.id[0])`, so a legacy plain-text command sent without a correlation id is
executed and then **silently produces nothing at all** — no success ack and no failure ack. A caller that treats
a missing ack as failure will be wrong for exactly that case.

The same gate wraps a second emit site: the decommission-all path acknowledges early, before the hub restarts,
rather than going through the normal end-of-dispatch ack.

There is no direct-method subscription anywhere in the firmware — a repo-wide search finds no
`$iothub/methods/POST` subscription. The IoT Hub connection makes exactly three subscriptions -
`main/iothub/app_iothub.c:1118` (C2D), `:1120` (twin responses) and `:1122-1123` (twin desired). A fourth
subscribe exists in the codebase, `main/dps_client/dps_client.c:269`, but it is on the DPS client and a
different broker, so it is not part of the hub connection. The transport therefore supplies no request/response correlation, and the
acknowledgement travels on the telemetry plane rather than the command plane.

Correlation is by the optional `data.id` field, echoed from the inbound command's correlation id. If the inbound
command carried no id, **the key is omitted entirely** — it is not an empty string. Note the interaction with the
gate above: a command with no id can only have been acked because it was enveloped, so an ack with no `data.id`
can only be matched by `data.cmd` and timing.

There is no machine-readable failure class. On failure, `data.error.code` is byte-identical to `data.cmd` — the
same C variable is written to both (`main/telemetry/telemetry_v2.c:707` and `:711`). The only discriminator is
the free-text `data.error.detail`. See F-06.

Two delivery caveats specific to acks, both consequences of riding the telemetry plane:

- An ack is published from the esp-mqtt event task rather than the main IoT Hub task, so it bypasses the
  provisioned gate and the snapshot scheduler and can interleave anywhere in the stream. The gate and emit are
  at `main/iothub/app_iothub.c:932-935`; the decommission-all early ack is at `:793-795`.
- **A command executed before the clock syncs has its ack destroyed** by the pre-epoch gate in `build_envelope`
  (`main/telemetry/telemetry_v2.c:69-74`). The command still ran. The cloud sees a timeout on an operation that
  actually took effect, which matters for any non-idempotent command — see F-03."""

# --------------------------------------------------------------------------------------
# Findings (F-nn).  Reported, never fixed — this is a read-only investigation.
# --------------------------------------------------------------------------------------

FINDINGS = [
    ("F-01", "Defect", "High",
     "A sensor that was wet and then goes silent reports leak_state:false in the next snapshot",
     "When the cache merge is skipped the snapshot writes the literal false into leak_state, while writing null into battery, rssi and fw_version in the same object. The merge is gated on the health engine's `connected` flag, which is itself a freshness test: connected is true only while the device has been heard within its 600 s timeout (main/health_engine/health_engine.c:640-648, timeout :16-17 of the header). So a sensor silent for more than ten minutes has its cached leak state dropped, and the field reads dry rather than unknown. A sensor that reported a leak and then went offline therefore flips to leak_state:false, while the leak_detected event already published for it is never retracted. A consumer that treats snapshots as authoritative will silently clear a live leak. Note that last_seen_age_s is emitted before the cache lookup and so keeps its real numeric value — a large last_seen_age_s next to leak_state:false is the signature of this case, and is the only in-payload contradiction available to a consumer.",
     "main/telemetry/telemetry_v2.c:506, :562; merge gate :485-496, :541-552; last_seen_age_s emitted earlier at :473-478, :530-535"),
    ("F-02", "Defect", "High",
     "LoRa leak state is derived two different ways, so a value of 2 auto-closes the valve while every snapshot reports dry",
     "The event path tests `pkt.leakStatus != 0`; the snapshot path tests `cached->leak_status == 1`. A LoRa sensor reporting leak_status == 2 triggers leak_detected and an auto-close, yet reads leak_state:false in every subsequent snapshot. The BLE path does not have this mismatch — both sides derive from a bool set with != 0.",
     "main/iothub/app_iothub.c:1750 versus main/telemetry/telemetry_v2.c:501; BLE comparison main/ble_leak_scanner/app_ble_leak.c:176-178"),
    ("F-03", "Defect", "High",
     "All telemetry produced before the clock syncs is destroyed rather than buffered, including leak events and command acks",
     "build_envelope() returns NULL when the wall clock is below epoch 1704067200, and publish_json() returns immediately on a NULL root — before reaching the branch that would have buffered the message for replay. Every message type is affected. For a command ack this means the cloud sees a timeout on a command that actually executed, which is a hazard for any non-idempotent command. The twin plane has no such gate, so a hub can be patching its twin while its telemetry is being discarded.",
     "main/telemetry/telemetry_v2.c:69-74, :99-122"),
    ("F-04", "Defect", "Medium",
     "Valve disconnection produces no D2C event",
     "g_valve_mac is zeroed before notify_hub_update(BLE_UPD_DISCONNECTED) is called, so the subsequent MAC lookup fails, mac_ok is false, and the entire valve-event block is skipped. Valve loss is observable only through the next snapshot's connected:false and a health device_offline event.",
     "main/ble_valve/app_ble_valve.c:1271-1272; skipped block main/iothub/app_iothub.c:1761-1796"),
    ("F-05", "Defect", "Medium",
     "trigger_mask is accepted and echoed as an unvalidated raw byte",
     "The inbound path stores (uint8_t)valueint with no masking and no range check, and the outbound path echoes it verbatim, so the wire range is 0..255 even though only bits 0-2 are ever interpreted. A cloud consumer cannot assume the value is within the documented three-bit domain.",
     "main/provisioning_manager/provisioning_manager.c:484-486; emitted main/telemetry/telemetry_v2.c:362, :581; interpreted main/rules_engine/rules_engine.c:432"),
    ("F-06", "Observation", "Medium",
     "data.error.code is not an error code — it is the command name repeated",
     "The same C variable is written to data.cmd and to data.error.code. There is no numeric or symbolic failure class anywhere on this path; the only discriminator is free-text detail. A consumer cannot switch on error.code to classify a failure.",
     "main/telemetry/telemetry_v2.c:707, :711"),
    ("F-07", "Defect", "Medium",
     "A desired property written while the hub is offline is never fetched",
     "The hub never publishes $iothub/twin/GET — no such string exists in main/. It only receives desired-property patches pushed while it is subscribed. Combined with snapshot_interval_s being RAM-only, a configured interval is lost on every reboot and is never re-read.",
     "verified absent in main/; desired-property subscribe main/iothub/app_iothub.c:1122-1123; interval default main/telemetry/telemetry_v2.c:42"),
    ("F-08", "Observation", "Medium",
     "snapshot_interval_s is accepted but never acknowledged",
     "The value is validated and applied but is not echoed into twin reported, so a cloud caller has no confirmation that the setting took effect, and no way to read back the value currently in force.",
     "main/iothub/app_iothub.c:1019-1028 versus the reported set at :953-982"),
    ("F-09", "Observation", "Low",
     "Three watchdog reset causes collapse into one wire value",
     "ESP_RST_INT_WDT, ESP_RST_TASK_WDT and ESP_RST_WDT all map to the string watchdog, so a hung task cannot be distinguished from a blocked interrupt handler. The default: branch additionally absorbs an external pin reset, which then reports as unknown.",
     "main/telemetry/telemetry_v2.c:134-136, :139"),
    ("F-10", "Observation", "Low",
     "g_twin_rid is a non-atomic int shared across twin patches",
     "The request id for twin PATCH topics is incremented without synchronisation. Twin patches are published from more than one context, so concurrent patches could in principle collide on a request id.",
     "main/iothub/app_iothub.c:171, :990-991"),
    ("F-11", "Observation", "Low",
     "nvs_save_cache does not check individual nvs_set_* return values",
     "The DPS cache write does not test the result of each nvs_set_* call before marking the cache valid, so a partial write could be recorded as complete.",
     "main/dps_client/dps_client.c:116-129 (nvs_save_cache); unchecked writes :122-126"),
    ("F-12", "Observation", "Low",
     "Valve battery reads 0 between BLE connect and GATT setup completion",
     "The cached value is initialised to 0 and reset on disconnect, and that 0 is emitted verbatim. It is indistinguishable on the wire from a genuinely flat battery. The health-alert path converts a 0xFF sentinel to an omitted key, but this 0 is not converted.",
     "main/ble_valve/app_ble_valve.c:134, :1209; emitted main/telemetry/telemetry_v2.c:431, :619"),
    ("F-13", "Observation", "Low",
     "A positive rssi on a LoRa sensor is a wrapped value, not a strong signal",
     "The LoRa path narrows an int to int8_t, so a true RSSI below -128 dBm wraps into the positive 0..127 range.",
     "main/app_lora/app_lora.cpp:274"),
    ("F-17", "Defect", "High",
     "A legacy plain-text command with no correlation id is executed but never acknowledged",
     "Both cmd_ack emit sites are wrapped in `if (cmd.is_envelope || cmd.id[0])`. A command that arrives in the legacy plain-text form AND carries no correlation id is therefore dispatched and acted on, but produces no acknowledgement of any kind — not a success ack and not a failure ack. A cloud caller that treats a missing ack as failure will conclude the command failed when it actually succeeded, and may retry a non-idempotent operation such as decommission.",
     "main/iothub/app_iothub.c:932-935 (end-of-dispatch ack), :793-795 (decommission-all early ack); legacy text parsing main/commands/c2d_commands.c:161, :172-173, :192-193, :203"),
    ("F-15", "Defect", "Medium",
     "An offline event larger than 512 bytes is truncated and then replayed as invalid JSON",
     "offline_buffer_store silently truncates any payload above OFFLINE_BUF_MAX_JSON_LEN to exactly 512 bytes, and the drain republishes the stored bytes verbatim. A truncated JSON object cannot parse, so such a replayed message will be rejected by any cloud-side parser rather than arriving in a degraded form. Typical leak and valve events are well under the limit — roughly 320 B and 260 B — so this is reachable only for the largest events, such as a cmd_ack carrying one of the longer override_enable rejection messages, or an event with a full-length location label. Only buffered (offline) messages are affected; an online publish is not truncated.",
     "main/offline_buffer/offline_buffer.c:77-81, :148; limit main/offline_buffer/offline_buffer.h:13"),
    ("F-16", "Observation", "Low",
     "The offline buffer holds 16 events; older events are lost in a longer outage",
     "OFFLINE_BUF_MAX_ENTRIES is 16. An outage producing more than 16 events does not preserve them all, so a cloud consumer cannot assume the replayed sequence after a reconnect is complete. Snapshots and lifecycle messages are not buffered at all — only type==\"event\" is stored.",
     "main/offline_buffer/offline_buffer.h:12; buffering condition main/telemetry/telemetry_v2.c:115-122"),
    ("F-14", "Observation", "Low",
     "Three deprecated serializers for a previous wire shape remain in the image",
     "build_valve_delta_json, build_lora_delta_json and build_ble_leak_delta_json build a flat gatewayID + devices[] shape. All three are marked (DEPRECATED) and __attribute__((unused)) and have no callers, so the old shape cannot reach the wire. They are listed because a backend may still hold parsers for that shape.",
     "main/iothub/app_iothub.c:397-399, :441-443, :483-485"),
]

# The previous wire shape, still compiled in but unreachable. Enumerated here so the claim that
# these are excluded from the 91 can actually be checked, rather than asserted as a bare count.
DEAD_PATHS = {
    "intro": """Three serializers for a previous wire shape remain in the image. All three are marked
`(DEPRECATED)` and `__attribute__((unused))`, and a search for their names finds only their definitions —
no caller anywhere. They cannot reach the wire, so none of the paths below is part of the 91-field census.

They are listed because a backend may still hold parsers for this shape from an earlier firmware
generation. Note how different it is from the current contract: a flat `gatewayID` rather than a nested
`gateway` object, a `devices` array wrapping everything, and no envelope at all — no `schema`, no `ts`,
no `type`.""",
    "builders": [
        ("build_valve_delta_json", "main/iothub/app_iothub.c:397-435"),
        ("build_lora_delta_json", "main/iothub/app_iothub.c:441-479"),
        ("build_ble_leak_delta_json", "main/iothub/app_iothub.c:483-521"),
    ],
    "paths": [
        ("gatewayID", "main/iothub/app_iothub.c:402, :447, :489"),
        ("devices", "main/iothub/app_iothub.c:405, :450, :492"),
        ("devices[].valve", "main/iothub/app_iothub.c:411"),
        ("devices[].valve.valve_mac", "main/iothub/app_iothub.c:416, :431 (null variant)"),
        ("devices[].valve.battery", "main/iothub/app_iothub.c:417"),
        ("devices[].valve.leak_state", "main/iothub/app_iothub.c:418"),
        ("devices[].valve.rmleak", "main/iothub/app_iothub.c:419"),
        ("devices[].valve.valve_state", "main/iothub/app_iothub.c:423, :425, :427, :432"),
        ("devices[].leak_sensors", "main/iothub/app_iothub.c:456"),
        ("devices[].leak_sensors.<sensor_0xNNNNNNNN>.battery", "main/iothub/app_iothub.c:464"),
        ("devices[].leak_sensors.<sensor_0xNNNNNNNN>.leak_state", "main/iothub/app_iothub.c:465"),
        ("devices[].leak_sensors.<sensor_0xNNNNNNNN>.rssi", "main/iothub/app_iothub.c:466"),
        ("devices[].leak_sensors.<sensor_0xNNNNNNNN>.location", "main/iothub/app_iothub.c:475"),
        ("devices[].leak_sensors.<sensor_0xNNNNNNNN>.location.code", "main/iothub/app_iothub.c:472"),
        ("devices[].leak_sensors.<sensor_0xNNNNNNNN>.location.label", "main/iothub/app_iothub.c:474"),
        ("devices[].ble_leak_sensors", "main/iothub/app_iothub.c:498"),
        ("devices[].ble_leak_sensors.<MAC>.battery", "main/iothub/app_iothub.c:506"),
        ("devices[].ble_leak_sensors.<MAC>.leak_state", "main/iothub/app_iothub.c:507"),
        ("devices[].ble_leak_sensors.<MAC>.rssi", "main/iothub/app_iothub.c:508"),
        ("devices[].ble_leak_sensors.<MAC>.location", "main/iothub/app_iothub.c:515"),
        ("devices[].ble_leak_sensors.<MAC>.location.code", "main/iothub/app_iothub.c:512"),
        ("devices[].ble_leak_sensors.<MAC>.location.label", "main/iothub/app_iothub.c:514"),
    ],
}

GAPS = [
    ("Deployed fleet composition", "How many hubs are fielded, how many are in inventory, and whether a physical reflash path exists via the production tool. This determines how much the no-OTA constraint actually costs. Not determinable from this repository.", "[UNVERIFIED]"),
    ("Backend storage contents", "Which fields are already persisted cloud-side and for how long. Determines what a rename breaks in history.", "[UNVERIFIED]"),
    ("Actual app consumption", "Which fields the Watts Digital app reads today. No app source is in this repository, so no field can be declared safe to change on the evidence available here.", "[UNVERIFIED]"),
    ("auto_close dual shape intent", "Whether the two structurally different auto_close payloads are deliberate. No source comment reconciles them and no version marker distinguishes them.", "[UNVERIFIED]"),
    ("LoRa NaN reachability", "Whether the LoRa driver can produce a NaN SNR, which cJSON would print as the bare token null. snr is the only float on the path. Not traced.", "[UNVERIFIED]"),
    ("Production tool source", "A separate PyQt6 project. Only its UART boot-log contract is visible from this repository.", "[UNVERIFIED]"),
]

SECRETS_NOTE = """No credential value is reproduced in this document. Two pieces of security material exist as
literals in firmware source and are referenced here by kind and location only:

- `<redacted: DPS ID scope>` — `main/iothub/app_iothub.h:30`, the AZURE_DPS_ID_SCOPE definition.
- `<redacted: DPS group symmetric key>` — `main/iothub/app_iothub.h:31`, the AZURE_DPS_GROUP_KEY definition.

The per-device key is derived at runtime by HMAC-SHA256 over the registration id using the group key
(`main/dps_client/dps_client.c:139-177`) and cached in NVS. A SAS token is constructed from it for MQTT
authentication (`main/iothub/app_iothub.c:219-254`). None of this material appears in any telemetry, twin or
command payload. That a group key is embedded in firmware is noted here as a security observation for the
team's own consideration; it is out of scope for the telemetry consolidation itself."""
