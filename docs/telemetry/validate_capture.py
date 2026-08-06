#!/usr/bin/env python3
# ruff: noqa: E501
"""Validate a captured Azure IoT Hub monitor log against the firmware 2.0.2 wire contract.

Reads whatever `az iot hub monitor-events` printed (or a UART log containing `Pub event:`
lines), pulls every JSON message out of it, and checks each one against the rules the
2.0.2 firmware is supposed to follow. Reports per-message failures and a summary.

This encodes the contract as ASSERTIONS, not as prose — so a passing run is evidence the
firmware and the catalogue agree, rather than an assumption that they do.

Run: python docs/telemetry/validate_capture.py "<path to capture>"
"""
import json
import re
import sys

# ---------------------------------------------------------------------------
# Contract — firmware 2.0.2
# ---------------------------------------------------------------------------
EXPECTED_FW = "2.0.2"
SCHEMA = "eflostop.v2"
DEVICE_TYPES = {"valve", "ble_leak_sensor", "lora"}
RATINGS = {"excellent", "good", "warning", "critical", "unknown"}

# Events the hub raises ABOUT a device (generic device_id), vs events a device
# reports about ITSELF (type-named identity key).
HUB_GENERATED = {"device_offline", "device_recovered", "auto_close",
                 "auto_close_blocked_override"}
LEAK_EVENTS = {"leak_detected", "leak_cleared"}

# Keys that must never appear anywhere on the wire again.
RETIRED_KEYS = {"dev_type", "valve_device_id", "valve_flood_detected",
                "valve_flood_cleared"}

MAC_RE = re.compile(r"^[0-9A-F]{2}(:[0-9A-F]{2}){5}$")
LORA_RE = re.compile(r"^0x[0-9A-F]{8}$")


def is_id(v):
    return isinstance(v, str) and bool(MAC_RE.match(v) or LORA_RE.match(v))


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
        if k in {"valve_id", "sensor_id", "device_id"}:
            v = _at(d, path)
            if isinstance(v, str) and not is_id(v):
                f.append(f"{path} = {v!r} is not an UPPERCASE MAC or 0xHEX id")

    ev = d.get("event")

    # ---- snapshot ----
    if mtype == "snapshot":
        valve = d.get("valve", {})
        if valve and "device_id" in valve:
            f.append("data.valve.device_id present — must be valve_id")
        if valve.get("connected") and "valve_id" not in valve:
            f.append("data.valve.valve_id missing on a connected valve")
        for arr in ("lora_sensors", "ble_leak_sensors"):
            if arr not in d:
                f.append(f"data.{arr} missing (must be present, [] when empty)")
            for i, s in enumerate(d.get(arr, [])):
                if "sensor_id" not in s:
                    f.append(f"data.{arr}[{i}] has no sensor_id (keys: {list(s)})")
                if "device_id" in s:
                    f.append(f"data.{arr}[{i}].device_id present — must be sensor_id")
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
            want = "valve_id" if st == "valve" else "sensor_id"
            if len(keys) < 3 or keys[2] != want:
                f.append(f"{ev} (source_type={st!r}): 3rd key is "
                         f"{keys[2] if len(keys) > 2 else '<none>'!r}, expected {want!r}")
            if "device_id" in d:
                f.append(f"{ev}: device_id present — device-reported events use {want}")

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
            # device_id is required EXCEPT on the auto_close family, where it is
            # legitimately omitted when the source is a valve with no resolvable
            # MAC (2.0.2 — better no identity than the placeholder "valve").
            optional_id = ev in {"auto_close", "auto_close_blocked_override"}
            if "device_id" not in d and not optional_id:
                f.append(f"{ev}: device_id missing (hub-generated events use device_id)")
            for bad in ("valve_id", "sensor_id"):
                if bad in d:
                    f.append(f"{ev}: {bad} present — hub-generated events use device_id")

        if ev in {"device_offline", "device_recovered"}:
            if d.get("category") != "health":
                f.append(f"{ev}: category is {d.get('category')!r}, expected 'health'")
            if list(d)[:4] != ["category", "event", "source_type", "device_id"]:
                f.append(f"{ev}: first four keys are {list(d)[:4]}, "
                         "expected [category, event, source_type, device_id]")
            for k in ("rating", "prev_rating"):
                if d.get(k) not in RATINGS:
                    f.append(f"{ev}.{k} = {d.get(k)!r} not a valid rating")

        if ev == "auto_close" and "active_leak_count" in d:
            # the reconnect variant
            if d.get("cause") != "reconnect":
                f.append("reconnect auto_close: cause != 'reconnect'")
            if "source_type" in d:
                f.append("reconnect auto_close still carries source_type (should be cause only)")

        # 2.0.2: the RMLEAK interlock events identify the valve by device_id and
        # carry NO source_type. The incident they close may have been latched by a
        # sensor, so claiming source_type:"valve" would make that key mean something
        # different here than everywhere else. device_id is omitted entirely on a
        # hub with no valve rather than naming a device that does not exist.
        if ev in {"rmleak_cleared", "rmleak_auto_cleared"}:
            if "source_type" in d:
                f.append(f"{ev}: carries source_type={d['source_type']!r} — this event "
                         "family must not claim a device type (2.0.2)")
            if d.get("device_id") == "valve":
                f.append(f"{ev}: device_id is the literal 'valve' — a phantom device; "
                         "the key should have been omitted")

        # 2.0.2: no event may carry the placeholder id.
        for k in ("device_id", "valve_id", "sensor_id"):
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
            ident = d.get("device_id")
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
