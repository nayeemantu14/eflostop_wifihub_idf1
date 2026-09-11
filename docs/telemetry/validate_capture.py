#!/usr/bin/env python3
# ruff: noqa: E501
"""Validate a captured Azure IoT Hub monitor log against the firmware 2.1.0 wire contract.

Reads whatever `az iot hub monitor-events` printed (or a UART log containing `Pub event:`
lines), pulls every JSON message out of it, and checks each one against the rules the
2.1.0 firmware is supposed to follow. Reports per-message failures and a summary.

This encodes the contract as ASSERTIONS, not as prose — so a passing run is evidence the
firmware and the catalogue agree, rather than an assumption that they do.

Run: python docs/telemetry/validate_capture.py "<path to capture>"
"""
import json
import re
import sys

# ---------------------------------------------------------------------------
# Contract — firmware 2.1.0
# ---------------------------------------------------------------------------
EXPECTED_FW = "2.1.0"
SCHEMA = "eflostop.v2"
DEVICE_TYPES = {"valve", "ble_leak_sensor", "lora"}
RATINGS = {"excellent", "good", "warning", "critical", "unknown"}

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
        valve = d.get("valve", {})
        if valve.get("connected") and "valve_id" not in valve:
            f.append("data.valve.valve_id missing on a connected valve")
        for arr in ("lora_sensors", "ble_leak_sensors"):
            if arr not in d:
                f.append(f"data.{arr} missing (must be present, [] when empty)")
            for i, s in enumerate(d.get(arr, [])):
                if "sensor_id" not in s:
                    f.append(f"data.{arr}[{i}] has no sensor_id (keys: {list(s)})")
        if "override_active" not in d:
            f.append("data.override_active missing (unconditional on snapshots)")

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

    print(f"\n{'=' * 70}")
    print(f"{len(msgs)} messages checked, {len(msgs) - bad} pass, {bad} fail"
          f"{f', {len(order_fails)} ordering violation(s)' if order_fails else ''}")
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
    return 1 if (bad or order_fails) else 0


if __name__ == "__main__":
    sys.exit(main())
