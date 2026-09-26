#!/usr/bin/env python3
# ruff: noqa: E501
"""Validate a captured Azure IoT Hub monitor log against the firmware 2.1.4 wire contract.

Reads whatever `az iot hub monitor-events` printed (or a UART log containing `Pub event:`
lines), pulls every JSON message out of it, and checks each one against the rules the
2.1.4 firmware is supposed to follow. Reports per-message failures and a summary.

Events held before the first clock sync and replayed from the offline buffer never appear
as `Pub event:` lines on UART (the drain logs only "Replayed [ob_NN]"); validate them from
an `az iot hub monitor-events` capture.

This encodes the contract as ASSERTIONS, not as prose — so a passing run is evidence the
firmware and the catalogue agree, rather than an assumption that they do.

Standard library only. Run: python docs/telemetry/validate_capture.py "<path to capture>"
"""
import json
import re
import sys

# ---------------------------------------------------------------------------
# Contract — firmware 2.1.4
# ---------------------------------------------------------------------------
EXPECTED_FW = "2.1.4"
SCHEMA = "eflostop.v2"
DEVICE_TYPES = {"valve", "ble_leak_sensor", "lora"}
RATINGS = {"excellent", "good", "warning", "critical", "unknown"}
# Severity order of the ratings, for the system-health consistency checks.
RATING_RANK = {"excellent": 0, "good": 1, "warning": 2, "critical": 3}
SNAPSHOT_REASONS = {"heartbeat", "event", "commission", "boot", "fast", "decommission"}

# 2.1.4 snapshot valve object. {} when no valve is provisioned; otherwise these keys
# always, plus the live keys only while connected to the PROVISIONED valve.
VALVE_ALWAYS = ("valve_id", "state", "connected", "rating", "last_seen_age_s")
VALVE_LIVE = ("battery", "leak_state", "rmleak", "fw_version")

# system_health.reason texts that only an EXCELLENT rating can carry.
EMPTY_HUB_REASON = "No devices provisioned"
EXCELLENT_REASONS = {"All devices healthy", EMPTY_HUB_REASON}
SYNCING_RE = re.compile(r"^Syncing - waiting for (\d+) devices?$")

# Events the hub raises ABOUT a device, vs events a device reports about ITSELF.
# Since 2.1.0 BOTH families name the device the same way — valve_id for the valve,
# sensor_id for a leak sensor — so the distinction below no longer changes the
# identity key. It is kept because the two families differ in whether the key may
# legitimately be absent.
HUB_GENERATED = {"device_offline", "device_recovered", "auto_close",
                 "auto_close_blocked_override"}
LEAK_EVENTS = {"leak_detected", "leak_cleared"}

# Keys that must never appear anywhere on the wire again. `device_id` joined this
# set in 2.1.0: it was the last surviving third spelling of an identity, used by
# health alerts and the auto_close / RMLEAK families while every other message had
# already moved to valve_id / sensor_id.
RETIRED_KEYS = {"dev_type", "valve_device_id", "valve_flood_detected",
                "valve_flood_cleared", "device_id"}

# Minimum epoch the hub treats as a synced clock (2024-01-01T00:00:00Z). No ts or absolute
# expiry below it may reach the wire: since 2.1.4 an event raised before the first clock
# sync is held in the offline buffer and its ts rewritten from the hub uptime once the
# clock syncs (one left by a restart before the sync is dropped), and water_access_override_enabled
# omits expires_ts while the clock is unsynced instead of sending a 1970 instant.
EPOCH_VALID = 1704067200

MAC_RE = re.compile(r"^[0-9A-F]{2}(:[0-9A-F]{2}){5}$")
LORA_RE = re.compile(r"^0x[0-9A-F]{8}$")

# The one identity key a message about a device is allowed to carry.
IDENTITY_KEYS = ("valve_id", "sensor_id")


def is_id(v):
    return isinstance(v, str) and bool(MAC_RE.match(v) or LORA_RE.match(v))


def identity_keys_in(d):
    """Which identity keys this data object carries. Exactly one is expected."""
    return [k for k in IDENTITY_KEYS if k in d]


def expected_identity(source_type):
    """The key a message must use, given its source_type. None when undecidable."""
    if source_type == "valve":
        return "valve_id"
    if source_type in {"ble_leak_sensor", "lora"}:
        return "sensor_id"
    return None


def is_num(v):
    """A JSON number, never a bool."""
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def is_battery(v):
    """A battery value: a number, or null for unknown (0xFF on the hub). Never a bool."""
    return v is None or (isinstance(v, (int, float)) and not isinstance(v, bool))


def check_valve(valve):
    """2.1.4 snapshot valve object: {} with no valve, never a stale or foreign valve."""
    f = []
    if not isinstance(valve, dict):
        return [f"data.valve is {type(valve).__name__}, expected an object ({{}} when no valve)"]
    if not valve:
        return f                     # no valve provisioned (BUG-5)
    for k in VALVE_ALWAYS:
        if k not in valve:
            f.append(f"data.valve.{k} missing (valve object has keys {list(valve)})")
    if valve.get("connected") is True:
        for k in VALVE_LIVE:
            if k not in valve:
                f.append(f"data.valve.{k} missing on a connected valve")
        if valve.get("state") not in {"open", "closed", "unknown"}:
            f.append(f"connected valve has state {valve.get('state')!r}")
    elif valve.get("connected") is False:
        if valve.get("state") != "disconnected":
            f.append(f"disconnected valve has state {valve.get('state')!r}, expected 'disconnected'")
        for k in VALVE_LIVE:
            if k in valve:
                f.append(f"data.valve.{k} present on a disconnected valve (live data only from the link)")
    if "battery" in valve and not is_battery(valve["battery"]):
        f.append(f"data.valve.battery = {valve['battery']!r}, expected a number or null")
    return f


def check_system_health(d):
    """system_health against the device arrays of the SAME snapshot (one lock on the hub)."""
    f = []
    sh = d.get("system_health") or {}
    rating, reason = sh.get("rating"), sh.get("reason")
    if rating not in RATING_RANK:
        f.append(f"system_health.rating = {rating!r}")
        return f
    if not isinstance(reason, str) or not reason:
        f.append(f"system_health.reason = {reason!r}")
        return f

    valve = d.get("valve") if isinstance(d.get("valve"), dict) else {}
    devices = ([valve] if valve else []) + list(d.get("lora_sensors", [])) \
        + list(d.get("ble_leak_sensors", []))

    # Empty hub (BUG-3/5/6): valve {}, both arrays [], its own reason, excellent.
    empty = not devices
    if empty != (reason == EMPTY_HUB_REASON):
        f.append(f"empty hub mismatch: {len(devices)} device(s) but reason {reason!r} "
                 f"(\"{EMPTY_HUB_REASON}\" exactly when valve is {{}} and both arrays are [])")
    if empty and rating != "excellent":
        f.append(f"empty hub rated {rating!r}, expected 'excellent'")

    m = SYNCING_RE.match(reason)
    if rating == "excellent":
        if reason not in EXCELLENT_REASONS and not m:
            f.append(f"rating excellent with reason {reason!r}")
    elif reason in EXCELLENT_REASONS or m:
        f.append(f"rating {rating!r} with reason {reason!r}")

    # "Syncing - waiting for N": N devices never heard (excused ones have no last_seen).
    unheard = sum(1 for x in devices if x.get("last_seen_age_s") is None)
    if m and int(m.group(1)) > unheard:
        f.append(f"reason says waiting for {m.group(1)} device(s) but only {unheard} "
                 f"have last_seen_age_s null")

    # A device that has been heard is never excused, so it counts in the roll-up.
    sys_rank = RATING_RANK[rating]
    for x in devices:
        r = RATING_RANK.get(x.get("rating"))
        if r is not None and x.get("last_seen_age_s") is not None and r > sys_rank:
            ident = x.get("valve_id") or x.get("sensor_id")
            f.append(f"{ident} is {x.get('rating')!r} but the system is only {rating!r}")
    # The cause list is ", "-joined, and the hub turns commas in a user label into
    # semicolons, so splitting is exact: a label such as "Valve closet" is one whole part.
    parts = reason.split(", ")

    # Some device carries the system rating, unless the interlock floor raised it.
    if sys_rank > 0 and "Leak interlock latched" not in parts \
            and not any(x.get("rating") == rating for x in devices):
        f.append(f"system {rating!r} but no device is {rating!r} and no interlock floor")

    # A wet sensor is critical and named (leak outranks every other cause).
    wet = [x for x in d.get("lora_sensors", []) + d.get("ble_leak_sensors", [])
           if x.get("leak_state") is True]
    leak_named = any(p.startswith("Leak detected: ") or re.match(r"^\d+ leaks detected$", p)
                     for p in parts)
    if wet and (rating != "critical" or not leak_named):
        f.append(f"{len(wet)} wet sensor(s) but system_health is {rating!r} / {reason!r}")

    # Valve cause parts name the valve's own rating (BUG-1: battery-critical is critical).
    vr = valve.get("rating")
    if "Valve battery critical" in parts and not (rating == "critical" and vr == "critical"):
        f.append(f"'Valve battery critical' with system {rating!r}, valve {vr!r}")
    if "Valve battery low" in parts and not (vr == rating and rating != "critical"):
        f.append(f"'Valve battery low' with system {rating!r}, valve {vr!r}")
    if "Valve offline" in parts and rating != "critical":
        f.append(f"'Valve offline' with system {rating!r}")
    valve_parts = {"Valve offline", "Valve disconnected", "Valve battery critical",
                   "Valve battery low"}
    if valve_parts.intersection(parts) and not valve:
        f.append(f"reason names the valve ({reason!r}) but data.valve is {{}}")
    return f


def check_override_times(d, where):
    """Override-window numbers, wherever they appear. A duration is never negative (the key
    is omitted instead) and an absolute expiry is never from an unsynced clock."""
    f = []
    for k in ("override_remaining_s", "remaining_s", "previous_remaining_s"):
        if k in d and (not is_num(d[k]) or d[k] < 0):
            f.append(f"{where}: {k} = {d[k]!r}, expected a number >= 0 (omitted when unknown)")
    if "expires_ts" in d and (not is_num(d["expires_ts"]) or d["expires_ts"] < EPOCH_VALID):
        f.append(f"{where}: expires_ts = {d['expires_ts']!r} is below {EPOCH_VALID} "
                 f"(omitted, never sent, while the clock is unsynced)")
    return f


def walk_keys(o, path="data"):
    """Yield (json_path, key) for every key in a nested structure."""
    if isinstance(o, dict):
        for k, v in o.items():
            yield f"{path}.{k}", k
            yield from walk_keys(v, f"{path}.{k}")
    elif isinstance(o, list):
        for v in o:
            yield from walk_keys(v, f"{path}[]")


def check(msg):
    """Return a list of failure strings for one message."""
    f = []
    ks = list(msg.keys())

    # ---- envelope ----
    if ks[:5] != ["schema", "ts", "gateway", "type", "data"]:
        f.append(f"envelope key order is {ks}, expected [schema, ts, gateway, type, data]")
    if msg.get("schema") != SCHEMA:
        f.append(f"schema is {msg.get('schema')!r}, expected {SCHEMA!r}")
    ts = msg.get("ts")
    if not is_num(ts) or ts < EPOCH_VALID:
        f.append(f"ts = {ts!r} is below {EPOCH_VALID}: an unsynced clock reached the wire "
                 f"(a pre-sync event must be time-stamped by the replay, or dropped)")

    gw = msg.get("gateway", {})
    gk = [k for k in gw if k != "name"]          # name is conditional
    if gk != ["id", "short_id", "fw", "uptime_s"]:
        f.append(f"gateway keys are {list(gw)}, expected id, short_id, [name], fw, uptime_s")
    if gw.get("fw") != EXPECTED_FW:
        f.append(f"gateway.fw is {gw.get('fw')!r}, expected {EXPECTED_FW!r}  <-- stale flash?")

    mtype = msg.get("type")
    if mtype not in {"snapshot", "event", "lifecycle"}:
        f.append(f"type is {mtype!r}")
    d = msg.get("data", {})

    # ---- retired keys, anywhere ----
    for path, k in walk_keys(d):
        if k in RETIRED_KEYS:
            f.append(f"RETIRED key {k!r} present at {path}")

    # ---- identifiers are uppercase ----
    for path, k in walk_keys(d):
        if k in IDENTITY_KEYS:
            v = _at(d, path)
            if isinstance(v, str) and not is_id(v):
                f.append(f"{path} = {v!r} is not an UPPERCASE MAC or 0xHEX id")

    ev = d.get("event")

    # ---- snapshot ----
    if mtype == "snapshot":
        if d.get("reason") not in SNAPSHOT_REASONS:
            f.append(f"data.reason = {d.get('reason')!r}, expected one of {sorted(SNAPSHOT_REASONS)}")
        if "valve" not in d:
            f.append("data.valve missing (must be present, {} when no valve)")
        f += check_valve(d.get("valve", {}))
        for arr in ("lora_sensors", "ble_leak_sensors"):
            if arr not in d:
                f.append(f"data.{arr} missing (must be present, [] when empty)")
            for i, s in enumerate(d.get(arr, [])):
                if "sensor_id" not in s:
                    f.append(f"data.{arr}[{i}] has no sensor_id (keys: {list(s)})")
                if "battery" not in s or not is_battery(s["battery"]):
                    f.append(f"data.{arr}[{i}].battery = {s.get('battery', '<missing>')!r}, "
                             f"expected a number or null (always present)")
        if "override_active" not in d:
            f.append("data.override_active missing (unconditional on snapshots)")
        f += check_override_times(d, "snapshot")
        f += check_system_health(d)

    # ---- lifecycle ----
    if mtype == "lifecycle" and "valve_device_id" in d:
        f.append("data.valve_device_id present — must be valve_id")

    # ---- events ----
    if mtype == "event" and ev:
        st = d.get("source_type")
        if st is not None and st not in DEVICE_TYPES:
            f.append(f"source_type {st!r} is not one of {sorted(DEVICE_TYPES)}")

        if ev in LEAK_EVENTS or ev == "valve_state_changed":
            keys = list(d)
            if keys[:2] != ["event", "source_type"]:
                f.append(f"{ev}: first two keys are {keys[:2]}, expected [event, source_type]")
            want = expected_identity(st) or "sensor_id"
            if len(keys) < 3 or keys[2] != want:
                f.append(f"{ev} (source_type={st!r}): 3rd key is "
                         f"{keys[2] if len(keys) > 2 else '<none>'!r}, expected {want!r}")

        # 2.1.4: an unknown battery is null (never 0) on every device-reported event.
        if ev in LEAK_EVENTS or ev == "valve_state_changed":
            if "battery" not in d:
                f.append(f"{ev}: battery missing (required; null when unknown)")
            elif not is_battery(d["battery"]):
                f.append(f"{ev}: battery = {d['battery']!r}, expected a number or null")
        if ev == "valve_state_changed" and (st != "valve" or "valve_id" not in d):
            f.append(f"valve_state_changed: source_type={st!r}, valve_id "
                     f"{'present' if 'valve_id' in d else 'missing'} (expected 'valve' + valve_id)")

        if ev in LEAK_EVENTS:
            want_state = ev == "leak_detected"
            if d.get("leak_state") is not want_state:
                f.append(f"{ev} carries leak_state={d.get('leak_state')!r}, expected {want_state}")
            if "location" not in d:
                f.append(f"{ev}: location missing (required core, every source)")
            if st == "valve" and "rssi" in d:
                f.append("valve leak event carries rssi (valve link has no cached RSSI)")
            if st in {"ble_leak_sensor", "lora"} and "rssi" not in d:
                f.append(f"{st} leak event has no rssi")

        if ev in HUB_GENERATED:
            # 2.1.0: hub-generated events name the device the SAME way a device
            # names itself — valve_id or sensor_id per source_type. The identity is
            # required EXCEPT on the auto_close family, where it is legitimately
            # omitted when the source is a valve with no resolvable MAC (better no
            # identity than the placeholder "valve").
            optional_id = ev in {"auto_close", "auto_close_blocked_override"}
            have = identity_keys_in(d)
            if len(have) > 1:
                f.append(f"{ev}: carries both {have} — a message names its device once")
            elif not have and not optional_id:
                f.append(f"{ev}: no identity key (expected valve_id or sensor_id)")
            want = expected_identity(st)
            if want and have and have[0] != want:
                f.append(f"{ev} (source_type={st!r}): identity key is {have[0]!r}, "
                         f"expected {want!r}")

        if ev in {"device_offline", "device_recovered"}:
            if d.get("category") != "health":
                f.append(f"{ev}: category is {d.get('category')!r}, expected 'health'")
            want = expected_identity(st) or "sensor_id"
            if list(d)[:4] != ["category", "event", "source_type", want]:
                f.append(f"{ev}: first four keys are {list(d)[:4]}, "
                         f"expected [category, event, source_type, {want}]")
            for k in ("rating", "prev_rating"):
                if d.get(k) not in RATINGS:
                    f.append(f"{ev}.{k} = {d.get(k)!r} not a valid rating")
            # 2.1.4: reachability only. device_offline is always critical; device_recovered
            # MAY be critical (back into a leak/battery critical), and prev_rating may equal
            # rating on a debounced trailing edge, so neither is flagged.
            if ev == "device_offline" and d.get("rating") != "critical":
                f.append(f"device_offline with rating {d.get('rating')!r}, expected 'critical'")
            if ev == "device_recovered" and "offline_duration_s" in d:
                f.append("device_recovered carries offline_duration_s (device_offline only)")
            if "battery" in d and (d["battery"] is None or not is_battery(d["battery"])):
                f.append(f"{ev}: battery = {d['battery']!r} (health alerts OMIT an unknown battery)")

        if ev == "cmd_ack":
            err = d.get("error")
            if d.get("status") == "error":
                if not isinstance(err, dict) or err.get("code") != d.get("cmd") \
                        or not isinstance(err.get("detail"), str):
                    f.append(f"cmd_ack error without error.code == cmd and a detail: {err!r}")
            elif err is not None:
                f.append("cmd_ack status ok carries an error object")
            if d.get("id") == "":
                f.append("cmd_ack id is \"\" (omitted when the command had none)")

        if ev == "water_access_override_enabled":
            # 2.1.4: expires_ts is omitted while the clock is unsynced; trigger and
            # remaining_s are always there.
            for k in ("trigger", "remaining_s"):
                if k not in d:
                    f.append(f"{ev}: {k} missing (always present)")
        if ev in {"water_access_override_enabled", "auto_close_blocked_override",
                  "auto_close_reenabled"}:
            f += check_override_times(d, ev)

        if ev == "auto_close" and "active_leak_count" in d:
            # the reconnect variant
            if d.get("cause") != "reconnect":
                f.append("reconnect auto_close: cause != 'reconnect'")
            if "source_type" in d:
                f.append("reconnect auto_close still carries source_type (should be cause only)")

        # The RMLEAK interlock events identify the valve by valve_id and carry NO
        # source_type. The incident they close may have been latched by a sensor, so
        # claiming source_type:"valve" would make that key mean something different
        # here than everywhere else — and since 2.1.0 the key itself names the type,
        # so nothing is lost. valve_id is omitted entirely on a hub with no valve
        # rather than naming a device that does not exist.
        if ev in {"rmleak_cleared", "rmleak_auto_cleared"}:
            if "source_type" in d:
                f.append(f"{ev}: carries source_type={d['source_type']!r} — this event "
                         "family must not claim a device type")
            if "sensor_id" in d:
                f.append(f"{ev}: carries sensor_id — the RMLEAK interlock is the "
                         "valve's, so the key must be valve_id")

        # No event may carry the placeholder id.
        for k in IDENTITY_KEYS:
            if d.get(k) == "valve":
                f.append(f"{ev}: {k} is the literal 'valve', not an identifier")

    return f


def check_ordering(msgs):
    """2.0.1: the cause must reach the cloud before the consequence.

    Every auto_close that names a device must be preceded by a leak_detected for
    that same device. Checked across the whole capture rather than per-message,
    because it is an ordering property, not a shape property.
    """
    fails = []
    seen_leak = {}          # device id -> index of its most recent leak_detected
    for i, m in enumerate(msgs):
        d = m.get("data", {})
        ev = d.get("event")
        if ev == "leak_detected":
            ident = d.get("valve_id") or d.get("sensor_id")
            if ident:
                seen_leak[ident] = i
        elif ev == "auto_close" and "active_leak_count" not in d:
            ident = d.get("valve_id") or d.get("sensor_id")
            if ident and ident not in seen_leak:
                fails.append(
                    f"message {i+1}: auto_close for {ident} has no preceding "
                    f"leak_detected — consequence published before its cause "
                    f"(the pre-2.0.1 inversion)")
    return fails


def check_valveless_auto_close(msgs):
    """2.1.4: a hub with no provisioned valve publishes no auto_close.

    Whether a hub has a valve is tracked per gateway from what the capture itself shows:
    a snapshot's data.valve ({} = none) and a lifecycle's valve_id. A lifecycle without
    valve_id is ambiguous (a busy provisioning read omits it too), and a successful
    provision or decommission changes the answer before the next snapshot, so both make
    it unknown and nothing is flagged until a snapshot settles it again. Checked across
    the capture, like the ordering, because it is a property of a sequence of messages.
    """
    fails = []
    has_valve = {}          # gateway id -> True / False / None (unknown)
    for i, m in enumerate(msgs):
        gw = (m.get("gateway") or {}).get("id")
        d = m.get("data") or {}
        ev = d.get("event")
        if m.get("type") == "snapshot":
            if isinstance(d.get("valve"), dict):
                has_valve[gw] = bool(d["valve"])
        elif m.get("type") == "lifecycle":
            has_valve[gw] = True if "valve_id" in d else None
        elif ev == "cmd_ack" and d.get("status") == "ok" \
                and d.get("cmd") in {"provision", "decommission"}:
            has_valve[gw] = None
        elif ev == "auto_close" and has_valve.get(gw) is False:
            fails.append(
                f"message {i+1}: auto_close from hub {gw}, whose last snapshot had no "
                f"valve (valve {{}}): a hub with no provisioned valve publishes none")
    return fails


def _at(d, path):
    cur = d
    for part in path.split(".")[1:]:
        if part.endswith("[]"):
            return None            # array walk — value checked per element elsewhere
        if not isinstance(cur, dict):
            return None
        cur = cur.get(part)
    return cur


def extract(text):
    """Pull every top-level JSON object containing '"schema":"eflostop.v2"' out of a log."""
    out, i = [], 0
    while True:
        i = text.find('{', i)
        if i < 0:
            return out
        depth, j, instr, esc = 0, i, False, False
        while j < len(text):
            c = text[j]
            if instr:
                if esc:
                    esc = False
                elif c == '\\':
                    esc = True
                elif c == '"':
                    instr = False
            elif c == '"':
                instr = True
            elif c == '{':
                depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0:
                    break
            j += 1
        blob = text[i:j + 1]
        try:
            o = json.loads(blob)
            if isinstance(o, dict) and o.get("schema") == SCHEMA:
                out.append(o)
                i = j + 1
                continue
        except Exception:
            pass
        i += 1


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: validate_capture.py <capture-file>")
    text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    msgs = extract(text)
    if not msgs:
        raise SystemExit("no eflostop.v2 messages found in that file")

    bad = 0
    seen = {}
    types = set()
    for n, m in enumerate(msgs, 1):
        label = m.get("data", {}).get("event") or m.get("type")
        types.add(m.get("type"))
        seen[label] = seen.get(label, 0) + 1
        fails = check(m)
        if fails:
            bad += 1
            print(f"\n--- message {n}  ({m.get('type')} / {label})")
            for x in fails:
                print(f"    FAIL  {x}")

    order_fails = check_ordering(msgs)
    if order_fails:
        print("\n--- ORDERING (cause before consequence) ---")
        for x in order_fails:
            print(f"    FAIL  {x}")

    valveless_fails = check_valveless_auto_close(msgs)
    if valveless_fails:
        print("\n--- AUTO_CLOSE FROM A HUB WITH NO VALVE ---")
        for x in valveless_fails:
            print(f"    FAIL  {x}")

    print(f"\n{'=' * 70}")
    print(f"{len(msgs)} messages checked, {len(msgs) - bad} pass, {bad} fail"
          f"{f', {len(order_fails)} ordering violation(s)' if order_fails else ''}"
          f"{f', {len(valveless_fails)} auto_close with no valve' if valveless_fails else ''}")
    print("\nmessage mix:")
    for k in sorted(seen):
        print(f"    {seen[k]:4d}  {k}")

    # Coverage is by envelope type OR data.event — a lifecycle message is labelled
    # "online" by its event, so checking the event names alone would report it missing.
    covered = set(seen) | types
    missing = [w for w in ["snapshot", "lifecycle", "leak_detected", "leak_cleared",
                           "valve_state_changed", "device_offline", "auto_close", "cmd_ack"]
               if w not in covered]
    if missing:
        print("\nnot exercised by this capture (cannot be validated from it):")
        for w in missing:
            print(f"    {w}")
    return 1 if (bad or order_fails or valveless_fails) else 0


if __name__ == "__main__":
    sys.exit(main())
