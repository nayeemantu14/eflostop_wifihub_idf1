# ruff: noqa: E501
"""Overlap analysis, the compatibility envelope, options, open questions, and the
delivery-semantics material for Part I and the back half of Part II.

STANCE: every option set opens with "Option 0 — no change; the cloud absorbs it", stated with its
ongoing cost. No option is marked as chosen. Where the author holds a view it is labelled
"[ONE INPUT]". The decision belongs to the joint meeting.
"""

# --------------------------------------------------------------------------------------
# §5 Overlap & derivation relationships
# --------------------------------------------------------------------------------------

OVERLAP_INTRO = """Overlap is presented here as a relationship, not as waste. A field carried in two places
sometimes costs bytes for no benefit, and sometimes buys something real: the twin can be queried without
replaying a message stream, a duplicated value survives a dropped message, and two independent decode paths
fail independently. Each item below states the relationship and what, if anything, the duplication buys.

The byte figures are the actual serialized `"key":value` length plus its separating comma, matching the
compact output of `cJSON_PrintUnformatted` (`main/telemetry/telemetry_v2.c:103`)."""

OVERLAP = [
    ("O-1", "data.valve.state == \"disconnected\" is equivalent to data.valve.connected == false",
     "Both are written from the same boolean. The valve position enum absorbs link state, and link state is then re-emitted as its own boolean.",
     "17 B per snapshot",
     "Buys nothing structurally, but connected is the field a consumer should actually gate on, because state conflates position with link.",
     "main/telemetry/telemetry_v2.c:427-434, :442-443"),
    ("O-2", "Per-sensor connected is derivable from last_seen_age_s",
     "connected == (last_seen_age_s is not null AND last_seen_age_s <= 600). The timeout is 600 s for both sensor classes.",
     "17-18 B per sensor per snapshot",
     "Buys a consumer not having to know the 600 s constant, which is a firmware-side decision that could change.",
     "main/health_engine/health_engine.c:640-648, :651-656; timeout main/health_engine/health_engine.h:16-17"),
    ("O-2a", "That derivation does NOT hold for the valve — a trap worth 60 seconds",
     "The three valve keys come from three different sources in the same object. connected reads the live BLE link; last_seen_age_s and rating read the health record; and the health engine's own valve connected predicate is different again and is never emitted. There is no timeout constant relating valve connected to valve last_seen_age_s, so a cloud rule written for the sensor arrays will misjudge the valve. Valve liveness is authoritative from connected only.",
     "n/a",
     "This is a correctness note rather than an overlap saving.",
     "main/telemetry/telemetry_v2.c:407, :434, :443, :447-455; health predicate main/health_engine/health_engine.c:637-638"),
    ("O-3", "Per-sensor rating == \"critical\" is equivalent to connected == false",
     "compute_sensor_rating returns critical on exactly the complement of the connected predicate, using the same constant. rating still adds information above critical, distinguishing excellent/good/warning from battery and RSSI.",
     "21 B per sensor per snapshot",
     "rating is cached by a 30 s health tick while connected is computed live at snapshot build time, so the two can disagree for up to about 30 seconds. That skew is observable in production data.",
     "main/health_engine/health_engine.c:121-124, :126-141; tick main/health_engine/health_engine.h:18"),
    ("O-4", "data.system_health.rating is the worst rating in the same snapshot",
     "It is a max() over the per-device ratings, every one of which is also emitted in the same message.",
     "21 B per snapshot",
     "Buys the consumer not having to implement the ordering of the rating enum.",
     "main/health_engine/health_engine.c:170-182"),
    ("O-5", "data.system_health.reason is a prose rendering of the same arrays",
     "The builder reads only rating, dev_type, connected and last_battery — all four already present per device in the same payload. Its only extra input is a battery threshold constant.",
     "31 B healthy, up to about 140 B degraded, capped at 127 characters",
     "Buys a human-readable summary for a UI without the consumer implementing the phrasing rules.",
     "main/telemetry/telemetry_v2.c:157-231, :386; threshold main/health_engine/health_engine.h:21"),
    ("O-6", "gateway.short_id is the last 4 characters of gateway.id",
     "Both are rendered from the same MAC bytes, and both are emitted on every telemetry message and again on the twin.",
     "18 B per message",
     "Buys nothing on the wire; it exists because the short id is used in the setup AP SSID.",
     "main/hub_identity/hub_identity.c:31-37; emitted main/telemetry/telemetry_v2.c:79-80"),
    ("O-7", "data.override_remaining_s is usually data.expires_ts minus ts — but not always",
     "Both derive from the same expiry moment. One reachable branch breaks the derivation: when the window has expired but the tick has not yet processed it, remaining is 0 and expires_ts is omitted. A second branch exists in the rules engine for an unsynced clock, but it cannot reach the wire, because build_envelope suppresses the whole message below the same epoch threshold - so a consumer will never observe it.",
     "26 B per snapshot when an override is active",
     "The exceptions are exactly why the field is not purely redundant. A consumer computing remaining from expires_ts alone will be wrong in both branches.",
     "main/rules_engine/rules_engine.c:1191-1198 (expired branch :1197-1198; unreachable unsynced branch :1195-1196); envelope suppression main/telemetry/telemetry_v2.c:69-74; emitted :594-599"),
    ("O-8", "data.override_active is NOT derivable — listed to close the lead",
     "The obvious derivation \"active if expires_ts is present\" fails in both O-7 exception branches, where override_active is true with no expires_ts.",
     "n/a",
     "It carries information no other field in the payload does, so it is listed here only to close the lead rather than as a candidate for removal.",
     "main/telemetry/telemetry_v2.c:594-599"),
    ("O-9", "cmd_ack: data.error.code is byte-identical to data.cmd",
     "The same C variable is written to both keys. See F-06.",
     "Varies with command name length",
     "Buys nothing. There is no failure taxonomy on this path.",
     "main/telemetry/telemetry_v2.c:707, :711"),
    ("O-10", "On leak and valve-flood events, data.leak_state is implied by data.event",
     "The event name is selected from the same leak state that is then emitted as the field.",
     "20 B per event",
     "For valve flood the duplication is also racy: the field is a second, later read of live state, so a fast toggle can in principle produce valve_flood_detected together with leak_state:false. For valve_state_changed the field is independent information and is not redundant.",
     "main/iothub/app_iothub.c:1748-1750, :1778-1780; emitted main/telemetry/telemetry_v2.c:644, :620"),
    ("O-11", "data.auto_close_resumed is (data.active_leak_count > 0)",
     "Both are derived from the same counter in adjacent statements in the same object.",
     "26 B per water_access_override_expired event",
     "Buys nothing.",
     "main/rules_engine/rules_engine.c:1026-1028"),
    ("O-12", "Eleven of the twelve twin-reported keys duplicate a telemetry field",
     "Only free_heap is unique to the twin. Full row-by-row analysis, including which plane is authoritative and where the two can diverge, is the Plane Collision Index in §4.8.",
     "n/a — different plane, not extra bytes per telemetry message",
     "Buys real queryability: the twin can be read at any time without replaying the telemetry stream, which is exactly what a device-management view needs.",
     "main/iothub/app_iothub.c:953-982"),
    ("O-13", "Four fields are constant on the wire",
     "data.category is always \"health\"; data.rmleak_asserted is hard-coded true at both emit sites; data.remaining_s is a compile-time constant; and data.provisioned cannot be false in practice because the publish path is gated on being provisioned. Constant does not mean removable: data.category is the key that identifies a health event, and data.rmleak_asserted and data.override_cancelled are presence markers whose information is in whether the key appears at all.",
     "About 75 B combined across the messages that carry them",
     "rmleak_asserted and override_cancelled are presence markers typed as booleans — their information is in whether the key appears, not in its value.",
     "main/health_engine/health_engine.c:584; main/rules_engine/rules_engine.c:336, :938, :218, :679-681; main/telemetry/telemetry_v2.c:342; gate main/iothub/app_iothub.c:1674-1688"),
    ("O-14", "Invariant identity is re-sent on every message",
     "schema, gateway.id, gateway.short_id, gateway.name and gateway.fw change either never or only at a firmware update, yet are present on every telemetry message.",
     "About 120 B per message",
     "Buys self-describing messages: any single message can be interpreted without joining to another source, which matters for an at-least-once stream where a consumer may see messages out of order.",
     "main/telemetry/telemetry_v2.c:64, :79-84"),
]

# --------------------------------------------------------------------------------------
# §6 Compatibility envelope
# --------------------------------------------------------------------------------------

COMPAT = [
    ("6.1", "Updating hubs costs a reflash, not a compatibility break", "NOT FIXED — prototype phase",
     """The product is in prototype phase and every hub can be updated, so there is no fielded fleet to strand
and no wire format that must be preserved for devices already in service.

The firmware image contains no over-the-air update client: a repo-wide search of `main/` for `esp_https_ota`,
`esp_ota_begin`, `esp_ota_write` and `esp_ota_set_boot_partition` returns nothing, although the partition table
reserves `ota_0`, `ota_1` and `otadata`. Updates therefore reach hubs by reflashing rather than remotely.

Read that as a per-change logistics cost rather than a constraint on what the wire may look like. It argues for
batching wire changes into as few releases as possible; it does not argue against making them. If remote update
is wanted before the fleet grows, adding an OTA client is separate work that this document does not size.""",
     "partitions.csv:4-8; absence verified across main/"),
    ("6.2", "The application contract is not yet written", "NOT FIXED — greenfield",
     """The Watts Digital application will be written against the telemetry contract this document describes. No
parser exists yet that depends on a current field name, nesting or type.

This is the single most important input to §7. Every rename, reshape and removal option in this document is
available at ordinary engineering cost, because nothing downstream has to be migrated in step. The usual reason
to reject normalisation — an existing consumer that would break — does not apply here.

It also raises what is at stake in the other direction: whatever is agreed becomes the contract the application
is built to, so an irregularity left in place now is one the application will encode and carry forward.""",
     "No application source in this repository; stated by the product owner"),
    ("6.3", "Formats already consumed by tooling", "PARTIALLY FIXED — evidence in-repo",
     """Two couplings exist inside the product itself and would need changing in step rather than independently.

The production tool parses the hub's UART boot log for the gateway ID, firmware version and Wi-Fi MAC during
manufacture. That is a serial-console contract, not a cloud contract, so it does not constrain the telemetry
schema — but the `GW-` + 12-uppercase-hex rendering is consumed by tooling outside this repository.

The identifier format also spans two planes: the LoRa `0x%08lX` rendering is constructed as a C2D decommission
payload as well as being emitted in telemetry, so changing it on one plane requires changing it on the other in
the same release.""",
     "main/hub_identity/hub_identity.c:31-37; C2D coupling main/commands/c2d_commands.c:173"),
    ("6.4", "Historical data already stored cloud-side", "NOT FIXED — prototype data",
     """Whatever has been ingested so far is prototype data from development hubs rather than customer history, so
there is no long-lived record whose shape must be preserved and no backfill obligation implied by a rename.

One change is already in flight regardless: the health event `dev_type` value for a BLE leak sensor changed from
`ble_leak` to `ble_leak_sensor` between git HEAD and the working tree. Since all hubs can be updated, both
spellings need not coexist for long — but during the transition a hub on either build emits its own spelling,
so a cutover order is worth agreeing rather than discovering.""",
     "main/health_engine/health_engine.c:77; intent comment :68-71"),
    ("6.5", "What this leaves genuinely fixed", "ALMOST NOTHING",
     """Three things are worth stating explicitly so the meeting does not invent constraints that are not there.

The telemetry schema string `eflostop.v2` is a constant identifying the envelope shape, not the field set.
Fields have been added and an enum value renamed without it moving, so nothing currently depends on it staying
still, and bumping it would not by itself tell a consumer anything about capability.

The additive-only pattern is a habit observed in the git history, not a rule asserted anywhere in code. No
comment in the telemetry path marks any field as frozen.

The only real costs of change are the ones named above: a reflash per hub, and keeping the two identifier
couplings in §6.3 in step. Everything else — names, nesting, absence conventions, the device-shape asymmetry,
the twin/telemetry overlap — is open.""",
     "main/telemetry/telemetry_v2.c:64; main/telemetry/telemetry_v2.h:14"),
]

# --------------------------------------------------------------------------------------
# §7 Options per issue.  Option 0 is always present and always first.
# --------------------------------------------------------------------------------------

OPTIONS = [
    {
        "issue_id": "F-01", "title": "leak_state cannot express \"unknown\"",
        "context": """When a sensor has no live cache entry the snapshot emits leak_state:false while emitting null for
every other unknown value in the same object. A sensor that was wet and went silent therefore reads dry, and the
leak_detected event already published for it is never retracted. This is the one item in this document where the
encoding can cause a consumer to under-report a leak.""",
        "options": [
            ("O-F01-0", "No change — the cloud gates on connected",
             "Zero device work. The rule is: treat leak_state as meaningful only when connected is true; otherwise treat leak state as unknown and carry forward the last known value.",
             "The rule must be applied in every consumer that touches leak_state, including dashboards and any alerting path. A single consumer that forgets it under-reports a live leak.",
             "Every current and future cloud consumer.", "Backend", "No", "Reversible"),
            ("O-F01-a", "Emit null instead of false when there is no cache entry",
             "A one-line change per array in the device, making leak_state consistent with its four sibling keys.",
             "Changes the JSON type of a field from boolean to nullable boolean. Any consumer with a strict boolean parser breaks. Hubs still on the older build keep emitting false until reflashed, so the cloud rule from Option 0 is worth keeping until the rollout completes.",
             "Device firmware plus every consumer's type handling.", "Firmware, then backend", "Yes — shipped units unaffected", "Reversible"),
            ("O-F01-b", "Omit the key entirely when there is no cache entry",
             "Aligns leak_state with the class-1 omission convention used elsewhere in the payload.",
             "Same breakage profile as O-F01-a, and additionally changes the object's key set, which affects consumers that assume a fixed shape for array elements. The sensor arrays are currently the one place with a guaranteed-fixed shape.",
             "Device firmware plus consumers assuming fixed array-element shape.", "Firmware, then backend", "Yes", "Reversible"),
            ("O-F01-c", "Add a separate freshness field rather than changing leak_state",
             "Leaves the existing field untouched and adds an explicit indicator such as a data-age or validity flag.",
             "Purely additive, so nothing breaks — but it grows the payload and leaves the misleading field in place for any consumer that does not adopt the new one.",
             "Additive only.", "Firmware", "Yes", "Reversible"),
        ],
    },
    {
        "issue_id": "F-03", "title": "Pre-clock-sync telemetry is destroyed rather than buffered",
        "context": """Every message built before SNTP completes is deleted inside build_envelope and never reaches the
offline-buffer branch. Leak events and command acks are included. For an ack this means the cloud sees a timeout
on a command that actually executed.""",
        "options": [
            ("O-F03-0", "No change — the cloud tolerates the gap",
             "Zero device work. Consumers treat the interval between a hub's boot and its first message as a known blind spot, and treat a missing ack as indeterminate rather than as failure.",
             "A non-idempotent command issued in that window may be retried against a device that already executed it. The blind spot's length is not observable from the cloud.",
             "Command-issuing services and any consumer reasoning about gaps.", "Backend", "No", "Reversible"),
            ("O-F03-a", "Buffer pre-sync messages and stamp them at drain time",
             "Messages survive the window and are published once the clock is known.",
             "The timestamp would then be the drain time rather than the observation time, which changes what ts means for those messages. Requires a way to distinguish stamped-late messages, otherwise event ordering silently degrades.",
             "Device firmware plus any consumer that reasons about ts precisely.", "Firmware", "Yes", "Reversible"),
            ("O-F03-b", "Buffer with a monotonic offset and resolve at drain time",
             "Preserves true relative ordering and true intervals by recording uptime at build time and converting once the wall clock is known.",
             "More device-side state and more complexity in the buffer path, which is currently deliberately simple — it stores bytes and replays them verbatim.",
             "Device firmware only; the wire format is unchanged if the resolved ts is written before publishing.", "Firmware", "Yes", "Reversible"),
            ("O-F03-c", "Delay the MQTT connection until the clock has synced",
             "Removes the window rather than handling it, since nothing is published before the clock is valid.",
             "Delays every hub's first contact, including its lifecycle message and its twin patch, and makes a hub with no working SNTP invisible rather than partially visible. Trades a data gap for a connectivity gap.",
             "Device firmware; changes observed connect timing for every hub.", "Firmware", "Yes", "One-way in effect — hubs that cannot sync stop reporting at all"),
        ],
    },
    {
        "issue_id": "F-02", "title": "LoRa leak state is derived two different ways",
        "context": """The event path tests leak_status != 0 and the snapshot path tests leak_status == 1, so a sensor
reporting 2 auto-closes the valve while every snapshot reports dry. Note this is latent while the LoRa radio is
not populated on the PCBA.""",
        "options": [
            ("O-F02-0", "No change — latent while LoRa hardware is DNP",
             "Zero work. The divergence cannot occur in the field today because no LoRa sensor is fitted.",
             "The inconsistency remains in the code and becomes live the moment LoRa hardware is populated. It is invisible in production data until then, so it will not be rediscovered by observation.",
             "None today; the LoRa product variant later.", "Neither — deferred", "No", "Reversible"),
            ("O-F02-a", "Make both paths use the same test",
             "One-line change; removes the divergence at source.",
             "Requires deciding which semantic is correct, which needs the sensor firmware's definition of leak_status — that firmware is not in this repository.",
             "Device firmware.", "Firmware", "Yes", "Reversible"),
        ],
    },
    {
        "issue_id": "4.6", "title": "Three inconsistent representations of \"a device\"",
        "context": """The valve, a LoRa sensor and a BLE leak sensor are described by three differently-shaped objects,
and the same sensor appears nested in a snapshot but flat in an event. A consumer needs several extraction paths
for what is conceptually one entity.""",
        "options": [
            ("O-46-0", "No change — the cloud normalises at ingest",
             "Zero device work. A mapping layer converts all three shapes plus the flat event form into one internal entity model.",
             "The mapping is permanent and must be maintained as fields are added. Note this is the only option that requires no reflash, which matters for how changes are batched rather than for whether they are possible (§6.1).",
             "Backend ingest layer.", "Backend", "No", "Reversible"),
            ("O-46-a", "Converge the device shapes on new firmware, keep both readable",
             "New units emit one consistent device object; the ingest mapping is retained for older units.",
             "Two shapes in flight during the rollout window. Because every hub can be reflashed the fleet does converge, so the mapping layer can eventually be retired rather than frozen indefinitely.",
             "Firmware plus backend, with a long dual-support tail.", "Both", "Yes", "Reversible"),
            ("O-46-b", "Additive convergence — emit the new shape alongside the old",
             "Nothing breaks, because the existing keys stay. Consumers migrate at their own pace.",
             "Materially increases payload size on a message that is already the largest, and leaves two representations of the same data in one message, which is its own consistency problem.",
             "Payload size; every consumer eventually.", "Firmware", "Yes", "Reversible"),
        ],
    },
    {
        "issue_id": "4.2", "title": "Identifier rendering is not guaranteed join-safe",
        "context": """A BLE sensor's identifier reaches the cloud from two different renderers depending on message type.
Both are uppercase colon-separated by convention, but firmware compares case-insensitively, so the case is not an
invariant the device enforces on itself.""",
        "options": [
            ("O-42-0", "No change — the cloud normalises identifiers at ingest",
             "Zero device work. Case-fold and strip separators on ingest, and join on the normal form.",
             "Must also be applied to historical rows, or joins will straddle two conventions. Whether that has already been done is Q-04.",
             "Backend ingest plus any historical backfill.", "Backend", "No", "Reversible"),
            ("O-42-a", "Guarantee a single rendering in firmware",
             "Makes the convention an invariant so a byte-equality join is safe.",
             "Does not help fielded units, and does not by itself fix historical rows.",
             "Firmware.", "Firmware", "Yes", "Reversible"),
        ],
    },
    {
        "issue_id": "O-12", "title": "Eleven twin keys duplicate telemetry fields",
        "context": """Only free_heap is twin-only. A reader searching for a name finds two answers with different
delivery semantics; two of the eleven can genuinely diverge.""",
        "options": [
            ("O-12-0", "No change — document which plane is authoritative",
             "Zero engineering. The Plane Collision Index in §4.8 already states, per key, whether the values can diverge and which plane answers which question.",
             "Relies on readers consulting it. The two divergent cases (hub_name presence, uptime_s staleness) will surprise anyone who does not.",
             "Documentation only.", "Neither", "No", "Reversible"),
            ("O-12-a", "Reduce the twin to what only the twin can answer",
             "Keeps identity and configuration on the twin and drops values that telemetry already carries live.",
             "Removes the ability to query current state without replaying telemetry, which is the twin's main advantage. Also breaks any consumer reading those twin keys today.",
             "Backend consumers of the twin.", "Both", "Yes", "Reversible"),
            ("O-12-b", "Keep both and make the overlap explicit in the contract",
             "Formalises the split rather than changing it: the twin is the queryable current-state document, telemetry is the historical record.",
             "No reduction in bytes or duplication; the benefit is conceptual clarity only.",
             "Documentation and convention.", "Both", "No", "Reversible"),
        ],
    },
]

# --------------------------------------------------------------------------------------
# §9 Open questions.  Three lists, one ID space, each tagged with what it unblocks.
# --------------------------------------------------------------------------------------

OPEN_QUESTIONS = {
    "backend": [
        ("Q-01", "Do you already normalise identifiers at ingest — and if so, what is the normal form, and has it been applied to historical rows?", "Unblocks O-42-0 and O-42-a."),
        ("Q-02", "Which fields does your ingest actually read today, and which are stored but unused? We cannot determine this from the firmware repository, and it decides which of the four constant-on-the-wire fields can simply be dropped.", "Unblocks O-13 decisions and the consent agenda."),
        ("Q-04", "Both spellings of the BLE device class, ble_leak and ble_leak_sensor, will exist in the fleet and in stored history. Do you want the cloud to accept both permanently, or should the firmware revert until a coordinated cutover?", "Unblocks the §18 rollout decision."),
        ("Q-05", "Is a nullable boolean acceptable in your parsers for leak_state, or would a separate freshness field be easier to adopt?", "Chooses between O-F01-a, O-F01-b and O-F01-c."),
        ("Q-06", "Do you treat a missing cmd_ack as failure today? If so, the pre-clock-sync window can make a successful non-idempotent command look failed.", "Unblocks O-F03-0."),
        ("Q-07", "Would you rather receive one converged device shape and do less mapping, or keep the current shapes and keep the mapping you have already built?", "Chooses between O-46-0, O-46-a and O-46-b."),
    ],
    "firmware": [
        ("Q-08", "Are the two auto_close payload shapes deliberate? No source comment reconciles them, and the valve-reconnect variant uses a source_type value that is not a member of the underlying enum.", "Resolves the open item in §4.6."),
        ("Q-09", "Should snapshot_interval_s persist across reboots, and should it be echoed into twin reported — or is the current RAM-only behaviour acceptable if it is simply documented?", "Resolves F-07 and F-08."),
        ("Q-10", "Should valve disconnection emit its own event, or is inferring it from the next snapshot's connected:false and the health device_offline event sufficient?", "Resolves F-04."),
        ("Q-11", "Should trigger_mask be constrained to its three defined bits at all — and if so, on the way in, on the way out, on both, or should the device keep echoing the raw byte and let the cloud clamp it?", "Resolves F-05."),
        ("Q-12", "Is a machine-readable failure class wanted on cmd_ack, given data.error.code currently repeats the command name?", "Resolves F-06."),
    ],
    "joint": [
        ("Q-03", "Given that every hub can be reflashed, how many wire-format releases are we willing to absorb — one combined cutover, or incremental changes as they are agreed? This decides whether options are batched or taken piecemeal.", "Sequences every device-side option in §7."),
        ("Q-13", "Since the application will be written against whatever is agreed here, which parts of the current shape should be treated as the starting point and which as open for redesign? Left unstated, the application will encode the current irregularities by default.", "Sets the scope of every reshape option."),
        ("Q-14", "Should an OTA client be added before the fleet grows beyond what reflashing can practically cover? Not required for anything in this document, but it changes the cost of every future wire change.", "Affects the cost of change after this round."),
        ("Q-15", "This product reports no flow or consumption data at all. Is that a permanent product decision, or a gap the backend is expected to fill later? It determines whether usage analytics and consumption billing are off the table.", "Scopes future work."),
        ("Q-16", "There is no audit surface: cmd_ack records that a command succeeded but carries no actor identity and no authentication result. Is a who-closed-my-valve trail required?", "Scopes future work."),
    ],
}

# --------------------------------------------------------------------------------------
# §16 Trigger & cadence matrix (all planes)
# --------------------------------------------------------------------------------------

TRIGGER_MATRIX = [
    ("Timer, 300 s default", "Telemetry", "snapshot (reason: heartbeat)", "The whole snapshot field set", "Interval settable 60..3600 via twin desired, RAM only. main/telemetry/telemetry_v2.c:42; main/iothub/app_iothub.c:1022"),
    ("MQTT connect", "Telemetry", "offline-buffer drain, then lifecycle", "Buffered events replayed verbatim, then the lifecycle field set", "main/iothub/app_iothub.c:1693-1694"),
    ("MQTT connect", "Twin reported", "twin PATCH", "All 12 twin keys", "main/iothub/app_iothub.c:1695"),
    ("Boot", "Telemetry", "snapshot (reason: boot)", "Full snapshot", "main/iothub/app_iothub.c:586"),
    ("Valve ready", "Telemetry", "snapshot (reason: fast)", "Full snapshot, incomplete by design", "main/iothub/app_iothub.c:587"),
    ("Valve GATT notify, value changed", "Telemetry", "valve event", "data.valve_state, battery, leak_state, rmleak, fw_version", "Delta-gated; gate reset to -2 on reconnect. main/iothub/app_iothub.c:1782-1791, :1658"),
    ("BLE advertisement, leak state changed", "Telemetry", "leak event + coupled snapshot", "data.sensor_id, leak_state, battery, rssi, location", "main/iothub/app_iothub.c:1807-1811"),
    ("LoRa packet, leak state changed", "Telemetry", "leak event + coupled snapshot", "Same leak-event field set", "main/iothub/app_iothub.c:1747-1750"),
    ("Rules engine decision", "Telemetry", "rules event + coupled snapshot", "Varies by event; see §10", "main/rules_engine/rules_engine.c, seven emit sites"),
    ("Health tick, 30 s, rating crossed critical", "Telemetry", "health event + one coupled snapshot per burst", "data.category, event, dev_type, sensor_id, rating, prev_rating, battery, rssi, offline_duration_s", "main/health_engine/health_engine.c:577-608; drain main/iothub/app_iothub.c:1720-1730"),
    ("Inbound C2D command", "Telemetry", "cmd_ack", "data.id, cmd, status, error", "Published from the esp-mqtt event task. main/iothub/app_iothub.c:932-935"),
    ("Provisioning change", "Telemetry", "snapshot (reason: commission)", "Full snapshot", "main/iothub/app_iothub.c:585"),
    ("decommission_all command", "Telemetry", "snapshot (reason: decommission)", "Full snapshot, published out of band immediately before restart", "main/iothub/app_iothub.c:1570"),
    ("set_hub_name success", "Twin reported", "twin PATCH", "All 12 twin keys", "main/iothub/app_iothub.c:922"),
    ("Any parseable desired patch", "Twin reported", "twin PATCH", "All 12 twin keys", "main/iothub/app_iothub.c:1045"),
    ("Cloud writes a desired property", "Twin desired", "inbound only", "hub_name, snapshot_interval_s", "Delivered only while subscribed; never fetched on connect. main/iothub/app_iothub.c:1122-1123"),
    ("First boot, or NVS cache miss", "Provisioning", "DPS register, then poll every 3 s", "registrationId", "main/dps_client/dps_client.c:272-312"),
]

# --------------------------------------------------------------------------------------
# §17 Payload volume.  Arithmetic shown so the meeting can check it.
# --------------------------------------------------------------------------------------

BANDWIDTH = {
    "scope": """This analysis covers the telemetry plane. The twin plane is negligible by comparison — twelve flat
keys written on only three triggers — and the command plane is inbound. Figures are compact JSON as produced by
cJSON_PrintUnformatted (main/telemetry/telemetry_v2.c:103), excluding MQTT and TLS framing.""",
    "reference_config": "One valve, four BLE leak sensors, four LoRa entries. Note that the LoRa array is empty in practice because the radio is not populated, so a real installation's snapshot is smaller than the reference figure below.",
    "rows": [
        ("Envelope (schema, ts, gateway object, type)", "About 150 B", "Present on every message."),
        ("Typical snapshot, reference configuration", "2,162 B", "The largest routine message."),
        ("Lifecycle", "About 300 B", "Once per MQTT connect."),
        ("Leak event", "About 320 B", "Event-driven."),
        ("Valve event", "About 260 B", "Event-driven."),
        ("cmd_ack, success", "About 230 B", "One per inbound command."),
    ],
    "arithmetic": """Snapshots at the 300 s default heartbeat:

    86,400 s/day / 300 s = 288 snapshots/day
    288 x 2,162 B        = 622,656 B/day  (about 608 KiB/day)

Of that, the invariant and derivable content identified in §5 accounts for:

    288 x 902 B          = 259,776 B/day  (about 254 KiB/day, roughly 42%)

Events are additional and depend entirely on installation activity. A quiet installation adds only the
per-connect lifecycle message; an installation with an intermittent sensor can add far more, because each
health transition emits an event and pulls a coupled snapshot with it.""",
    "citation": "Byte figures reproduce the method in the overlap analysis; interval main/telemetry/telemetry_v2.c:42.",
}

# --------------------------------------------------------------------------------------
# §18 Version delta
# --------------------------------------------------------------------------------------

VERSION_DELTA = {
    "intro": """This document describes the working tree, which is FW 1.8.0 and uncommitted. Git HEAD is b62b25e,
which is FW 1.7.0. The two differ on the telemetry plane in exactly two ways. Everything else in the working-tree
diff is internal: no cJSON_Add* call was added or removed anywhere in main/, so no field appeared or disappeared.""",
    "rows": [
        ("data.dev_type value for a BLE leak sensor", "ble_leak", "ble_leak_sensor",
         "Breaking for any consumer matching the exact string. Deliberate, to match source_type on leak events; the intent is stated in an in-source comment.",
         "main/health_engine/health_engine.c:77; comment :68-71"),
        ("Accepted sensor_type aliases on the sensor_meta command", "\"ble\" only, case-insensitively", "ble_leak_sensor, ble_leak and ble all accepted case-insensitively",
         "Additive and inbound only. Widens what the command plane accepts; changes nothing outbound.",
         "main/sensor_meta/sensor_meta.h:51-56; sensor_meta handler main/iothub/app_iothub.c:859-873; the same helper is also used by the decommission target test at :758"),
    ],
    "mixed_fleet": """During a rollout a hub on 1.7.0 emits `ble_leak` while a hub on 1.8.0 emits `ble_leak_sensor`,
so both spellings can appear at once. Because every hub can be reflashed this is a transitional state rather
than a permanent one, and it ends when the rollout completes. What is worth agreeing is the order: whether the
cloud accepts both spellings first and the hubs are reflashed after, or the reverse. Q-04.""",
}

GLOSSARY = [
    ("adv", "Advertisement — a BLE broadcast packet. Leak sensors advertise rather than connecting."),
    ("BAS", "Battery Service — a standard BLE service defining a 0-100 percent battery level characteristic."),
    ("C2D", "Cloud to device — a message sent from IoT Hub to the device."),
    ("cmd", "Command."),
    ("D2C", "Device to cloud — a telemetry message sent from the device to IoT Hub."),
    ("dev_type", "Device type — which class of peer a health event refers to."),
    ("DIS", "Device Information Service — a standard BLE service. The hub reads the valve's firmware revision string from it."),
    ("DNP", "Do not populate — a component present in the design but deliberately not fitted to the board. The LoRa radio is DNP on the current PCBA."),
    ("DPS", "Device Provisioning Service — the Azure service a device contacts once to discover which IoT Hub it belongs to."),
    ("fw", "Firmware version."),
    ("GATT", "Generic Attribute Profile — the BLE mechanism for reading, writing and subscribing to values on a connected peer. Used for the valve."),
    ("gw", "Gateway — this hub."),
    ("loc", "Location."),
    ("MQTT", "The publish/subscribe protocol used for all cloud communication here, over TLS."),
    ("NVS", "Non-volatile storage — the ESP32's key/value store in flash. Survives reboot."),
    ("ovr", "Override — the 24-hour water-access override window."),
    ("plane", "One of the four independent communication channels between the hub and the cloud: telemetry, twin, command and provisioning. Each has its own topic, direction and delivery semantics."),
    ("prov", "Provisioning."),
    ("RMLEAK", "Remote leak interlock — a latch on the valve. While it is set the valve refuses to open, which is how an automatic leak shut-off is prevented from being immediately undone."),
    ("RSSI", "Received signal strength indication, in dBm. Always negative in normal operation."),
    ("SAS", "Shared access signature — the time-limited token used to authenticate the MQTT connection."),
    ("SNR", "Signal-to-noise ratio, in dB. LoRa only."),
    ("SNTP", "Simple Network Time Protocol — how the hub learns the wall-clock time. Until it completes, telemetry is discarded."),
    ("snapshot", "The message type carrying the full current state of the hub and every device it knows about."),
    ("ts", "Timestamp — Unix epoch seconds, UTC."),
    ("twin", "The device twin — a JSON document held by IoT Hub, with a reported half written by the device and a desired half written by the cloud."),
]
