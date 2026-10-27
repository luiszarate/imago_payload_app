#!/usr/bin/env python3
"""What the gimbal was told and what it did, from a .tlog recorded by the app or imago-cli.

Written for the recentre ground test (docs/pruebas/centrado-en-tierra.md): flight 2026-10-27
showed the autopilot passing an angle of 0/0 on to the Orus L, which did not move, while a
GB_MODE 4 sent by another client centred it. One line per event, in time order:

- every command the gimbal or the gimbal manager got: GIMBAL_MANAGER_SET_ATTITUDE (282) and
  GIMBAL_DEVICE_SET_ATTITUDE (284) by angle (rate setpoints only when they change), the
  gimbal commands of COMMAND_LONG (DO_MOUNT_CONTROL, DO_GIMBAL_MANAGER_CONFIGURE), GB_MODE and
  RC_MODE through PARAM_EXT_SET, and their acknowledgements;
- who holds the gimbal manager (GIMBAL_MANAGER_STATUS), when it changes;
- the gimbal's own attitude (GIMBAL_DEVICE_ATTITUDE_STATUS) twice a second, or at once when
  its flags change.

Then, for each angle command, where the gimbal was before it and 3 s later.

    tools/tlog-gimbal.py logs/*.tlog [--from SECONDS] [--to SECONDS] [--ours COMPID]

Times are seconds from the start of the file; --ours is our component when imago-cli ran
with --compid (default 193). Only the standard library is used.
"""

import math
import struct
import sys

from importlib import util as _util

_spec = _util.spec_from_file_location('tlog_bursts', __file__.replace('tlog-gimbal.py', 'tlog-bursts.py'))
_bursts = _util.module_from_spec(_spec)
_spec.loader.exec_module(_bursts)
parse = _bursts.parse

OUR_COMPONENT = 193  # imago_config.component_id default; --ours changes it
GIMBAL_COMMANDS = {205: 'DO_MOUNT_CONTROL', 1000: 'DO_GIMBAL_MANAGER_PITCHYAW',
                   1001: 'DO_GIMBAL_MANAGER_CONFIGURE'}
WATCHED_PARAMS = {'GB_MODE', 'RC_MODE'}
GB_MODES = {0: 'off', 1: 'lock', 2: 'follow', 3: 'mapping', 4: 'reset'}
FLAG_NAMES = ((1, 'RETRACT'), (2, 'NEUTRAL'), (4, 'ROLL_LOCK'), (8, 'PITCH_LOCK'), (16, 'YAW_LOCK'),
              (32, 'YAW_VEHICLE'), (64, 'YAW_EARTH'))
STATUS_PERIOD_US = 500_000
SETTLE_US = 3_000_000


def unpack(fmt, payload):
    """MAVLink 2 drops trailing zero bytes of a payload: put them back before unpacking."""
    size = struct.calcsize(fmt)
    return struct.unpack(fmt, payload[:size].ljust(size, b'\0'))


def euler(q):
    """-> (pitch, yaw) in degrees, or None for a NaN quaternion (a rate setpoint)."""
    w, x, y, z = q
    if math.isnan(w):
        return None
    pitch = math.degrees(math.asin(max(-1.0, min(1.0, 2 * (w * y - z * x)))))
    yaw = math.degrees(math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)))
    return pitch, yaw


def flags_text(flags):
    names = [name for bit, name in FLAG_NAMES if flags & bit]
    return f'{flags} ({"|".join(names) or "-"})'


def degrees(rad):
    return '-' if math.isnan(rad) else f'{math.degrees(rad):.1f}'


def text(raw):
    return raw.split(b'\0', 1)[0].decode('ascii', 'replace')


def describe(system, component, msgid, payload):
    """-> (kind, line) for the messages this tool reports, else None."""
    sender = f'{system}/{component}'
    if msgid == 282:  # GIMBAL_MANAGER_SET_ATTITUDE
        flags, *rest = unpack('<I4f3fBBB', payload)
        q, (rate_pitch, rate_yaw), target = rest[0:4], rest[5:7], rest[7:9]
        angle = euler(q)
        if angle is None:
            return 'rate282', f'{sender} -> manager {target[0]}/{target[1]} RATE pitch {degrees(rate_pitch)} yaw {degrees(rate_yaw)} deg/s flags {flags_text(flags)}'
        return 'angle', f'{sender} -> manager {target[0]}/{target[1]} ANGLE pitch {angle[0]:.1f} yaw {angle[1]:.1f} flags {flags_text(flags)}'
    if msgid == 284:  # GIMBAL_DEVICE_SET_ATTITUDE
        values = unpack('<4f3fHBB', payload)
        q, (rate_pitch, rate_yaw), flags, target = values[0:4], values[5:7], values[7], values[8:10]
        angle = euler(q)
        if angle is None:
            return 'rate284', f'{sender} -> gimbal {target[0]}/{target[1]} RATE pitch {degrees(rate_pitch)} yaw {degrees(rate_yaw)} deg/s flags {flags_text(flags)}'
        return 'angle', f'{sender} -> gimbal {target[0]}/{target[1]} ANGLE pitch {angle[0]:.1f} yaw {angle[1]:.1f} flags {flags_text(flags)}'
    if msgid == 76:  # COMMAND_LONG
        values = unpack('<7fHBBB', payload)
        command = values[7]
        if command not in GIMBAL_COMMANDS:
            return None
        params = ' '.join(f'{v:g}' for v in values[0:7])
        kind = 'angle' if command in (205, 1000) else 'command'
        return kind, f'{sender} -> {values[8]}/{values[9]} {GIMBAL_COMMANDS[command]} ({command}) params {params}'
    if msgid == 77:  # COMMAND_ACK
        command, result, _progress, _param2, target_system, target_component = unpack('<HBBiBB', payload)
        if command not in GIMBAL_COMMANDS:
            return None
        return 'ack', f'{sender} ACK {GIMBAL_COMMANDS[command]} result {result} to {target_system}/{target_component}'
    if msgid == 323:  # PARAM_EXT_SET
        target_system, target_component, pid, value, _type = unpack('<BB16s128sB', payload)
        name = text(pid)
        if name not in WATCHED_PARAMS:
            return None
        number = value[0]
        kind = 'angle' if name == 'GB_MODE' and number == 4 else 'command'
        label = f' ({GB_MODES.get(number, "?")})' if name == 'GB_MODE' else ''
        return kind, f'{sender} -> {target_system}/{target_component} PARAM_EXT_SET {name} = {number}{label}'
    if msgid == 324:  # PARAM_EXT_ACK
        pid, value, _type, result = unpack('<16s128sBB', payload)
        name = text(pid)
        if name not in WATCHED_PARAMS:
            return None
        return 'ack', f'{sender} PARAM_EXT_ACK {name} = {value[0]} result {result}'
    return None


def analyze(path, start_s, end_s):
    records = parse(path)
    if not records:
        print(f'{path}: no packets')
        return
    t0 = records[0][0]
    print(f'\n=== {path}: {(records[-1][0] - t0) / 1e6:.0f} s')
    last_status = None
    last_status_us = -STATUS_PERIOD_US
    last_control = None
    last_rate = {}
    attitudes = []  # (us, pitch, yaw)
    angle_commands = []  # (us, line)
    for ts, system, component, msgid, _seq, payload in records:
        seconds = (ts - t0) / 1e6
        if msgid == 285:  # GIMBAL_DEVICE_ATTITUDE_STATUS
            values = unpack('<I4f3fIHBB', payload)
            angle = euler(values[1:5])
            flags = values[9]
            if angle is None:
                continue
            attitudes.append((ts, angle[0], angle[1]))
            if not start_s <= seconds <= end_s:
                continue
            if flags != last_status or ts - last_status_us >= STATUS_PERIOD_US:
                print(f'{seconds:8.2f}   gimbal {system}/{component} at pitch {angle[0]:6.1f} yaw {angle[1]:6.1f} flags {flags_text(flags)}')
                last_status, last_status_us = flags, ts
            continue
        if msgid == 281:  # GIMBAL_MANAGER_STATUS
            _time, flags, _device, sysid, compid, _sec_sys, _sec_comp = unpack('<IIBBBBB', payload)
            control = (sysid, compid)
            if control != last_control and start_s <= seconds <= end_s:
                holder = 'nobody' if sysid == 0 else f'{sysid}/{compid}'
                print(f'{seconds:8.2f}   manager primary control: {holder} flags {flags_text(flags)}')
            last_control = control
            continue
        found = describe(system, component, msgid, payload)
        if found is None or not start_s <= seconds <= end_s:
            continue
        kind, line = found
        if kind in ('rate282', 'rate284'):
            # Rate setpoints stream several times a second: report only a change.
            key = (system, component, kind)
            rate = line.split(' RATE ', 1)[1]
            if last_rate.get(key) == rate:
                continue
            last_rate[key] = rate
        marker = '>>' if component == OUR_COMPONENT else '  '
        print(f'{seconds:8.2f} {marker} {line}')
        # The summary is for what we sent; the autopilot echoes each angle several times.
        if kind == 'angle' and component == OUR_COMPONENT:
            angle_commands.append((ts, line))

    if not angle_commands:
        return
    print('\n  angle and reset commands: gimbal before -> 3 s later')
    for ts, line in angle_commands:
        before = [a for a in attitudes if a[0] <= ts]
        after = [a for a in attitudes if a[0] <= ts + SETTLE_US]
        if not before or not after:
            continue
        b, a = before[-1], after[-1]
        # Pitch is against the horizon in every mode; yaw against the nose also moves when the
        # aircraft turns, so pitch is the cleaner sign of a command obeyed.
        yaw_change = ((a[2] - b[2] + 180) % 360) - 180
        print(f'  {(ts - t0) / 1e6:8.2f}  {line}\n'
              f'            pitch {b[1]:6.1f} -> {a[1]:6.1f} ({a[1] - b[1]:+.1f}), '
              f'yaw {b[2]:6.1f} -> {a[2]:6.1f} ({yaw_change:+.1f})')


if __name__ == '__main__':
    args = sys.argv[1:]
    start, end = 0.0, float('inf')
    paths = []
    while args:
        arg = args.pop(0)
        if arg == '--from' and args:
            start = float(args.pop(0))
        elif arg == '--to' and args:
            end = float(args.pop(0))
        elif arg == '--ours' and args:
            OUR_COMPONENT = int(args.pop(0))
        else:
            paths.append(arg)
    if not paths:
        print(__doc__)
        sys.exit(1)
    for p in paths:
        analyze(p, start, end)
