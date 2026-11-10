#!/usr/bin/env python3
"""Which way the gimbal's reported attitude moves for the rate it is asked, from .tlog files.

Written after the first Quest head tracking test (2026-11-09): a loop that assumed a positive
pitch / yaw rate raises the reported pitch / yaw drove the Orus L to its stops. Any log with
joystick moves answers the question: for every stretch in which the rate commands
(GIMBAL_DEVICE_SET_ATTITUDE 284 or GIMBAL_MANAGER_SET_ATTITUDE 282 with a NaN quaternion) keep
one direction on an axis for at least a second, it compares the attitude
(GIMBAL_DEVICE_ATTITUDE_STATUS 285) at its start and its end.

    tools/tlog-rate-sign.py logs/*.tlog [--min-seconds 1.0] [--min-rate 3] [--verbose]

Per file and in total: stretches where the attitude followed the command's sign (same),
went against it (opposite) or barely moved (still), and the measured-to-asked rate ratio.
Yaw is against the nose in follow and against North in lock, so a turning aircraft adds to it:
pitch is the cleaner axis. Only the standard library is used.
"""

import math
import statistics
import sys

from importlib import util as _util

_spec = _util.spec_from_file_location('tlog_bursts', __file__.replace('tlog-rate-sign.py', 'tlog-bursts.py'))
_bursts = _util.module_from_spec(_spec)
_spec.loader.exec_module(_bursts)
parse = _bursts.parse

STILL_DEG = 0.5      # less than this over a stretch: the gimbal did not move
GAP_US = 400_000     # a stretch ends when rate commands stop for this long


def unpack(fmt, payload):
    import struct
    size = struct.calcsize(fmt)
    return struct.unpack(fmt, payload[:size].ljust(size, b'\0'))


def euler(q):
    w, x, y, z = q
    if math.isnan(w):
        return None
    pitch = math.degrees(math.asin(max(-1.0, min(1.0, 2 * (w * y - z * x)))))
    yaw = math.degrees(math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)))
    return pitch, yaw


def wrap(deg):
    return (deg + 180) % 360 - 180


def rates_of(msgid, payload):
    """-> (pitch, yaw) deg/s of a rate setpoint, or None."""
    if msgid == 284:
        values = unpack('<4f3fHBB', payload)
        q, rate = values[0:4], values[5:7]
    elif msgid == 282:
        values = unpack('<I4f3fBBB', payload)
        q, rate = values[1:5], values[6:8]
    else:
        return None
    if not math.isnan(q[0]):
        return None
    return tuple(0.0 if math.isnan(r) else math.degrees(r) for r in rate)


def analyze(path, min_seconds, min_rate, verbose):
    records = parse(path)
    attitudes = []  # (us, pitch, yaw)
    commands = []   # (us, pitch rate, yaw rate)
    for ts, _system, _component, msgid, _seq, payload in records:
        if ts is None:
            continue
        if msgid == 285:
            angle = euler(unpack('<I4f3fIHBB', payload)[1:5])
            if angle is not None:
                attitudes.append((ts, angle[0], angle[1]))
        else:
            rate = rates_of(msgid, payload)
            if rate is not None:
                commands.append((ts, rate[0], rate[1]))
    results = {'pitch': [], 'yaw': []}  # (verdict, ratio)
    if not attitudes or not commands:
        return results
    times = [a[0] for a in attitudes]

    def attitude_at(us, axis):
        import bisect
        i = bisect.bisect_right(times, us) - 1
        return attitudes[max(i, 0)][axis]

    t0 = records[0][0] or commands[0][0]
    for axis, name in ((1, 'pitch'), (2, 'yaw')):
        start = None  # (us, sign, [rates])
        def close(end_us):
            nonlocal start
            if start is None:
                return
            begin_us, sign, rates = start
            start = None
            seconds = (end_us - begin_us) / 1e6
            if seconds < min_seconds:
                return
            asked = statistics.mean(rates)
            before, after = attitude_at(begin_us, axis), attitude_at(end_us, axis)
            moved = wrap(after - before) if name == 'yaw' else after - before
            if abs(moved) < STILL_DEG:
                verdict = 'still'
            else:
                verdict = 'same' if (moved > 0) == (asked > 0) else 'opposite'
            ratio = (moved / seconds) / asked
            results[name].append((verdict, ratio))
            if verbose:
                print(f'  {(begin_us - t0) / 1e6:8.1f} s  {name:5} asked {asked:+6.1f} deg/s for {seconds:4.1f} s: '
                      f'{before:7.1f} -> {after:7.1f} ({moved:+6.1f}, {moved / seconds:+5.1f} deg/s)  {verdict}')
        last_us = None
        for us, *rate in commands:
            r = rate[axis - 1]
            sign = 0 if abs(r) < min_rate else (1 if r > 0 else -1)
            if start is not None and (sign != start[1] or us - last_us > GAP_US):
                close(last_us)
            if sign != 0:
                if start is None:
                    start = (us, sign, [])
                start[2].append(r)
            last_us = us
        close(last_us)
    return results


def summary(label, results):
    for name in ('pitch', 'yaw'):
        rows = results[name]
        counts = {v: sum(1 for verdict, _ in rows if verdict == v) for v in ('same', 'opposite', 'still')}
        moving = [abs(ratio) for verdict, ratio in rows if verdict != 'still']
        ratio = f', |measured/asked| median {statistics.median(moving):.2f}' if moving else ''
        print(f'{label:>10} {name:5}: same {counts["same"]:3}  opposite {counts["opposite"]:3}  '
              f'still {counts["still"]:3}{ratio}')


if __name__ == '__main__':
    args = sys.argv[1:]
    min_seconds, min_rate, verbose = 1.0, 3.0, False
    paths = []
    while args:
        arg = args.pop(0)
        if arg == '--min-seconds' and args:
            min_seconds = float(args.pop(0))
        elif arg == '--min-rate' and args:
            min_rate = float(args.pop(0))
        elif arg == '--verbose':
            verbose = True
        else:
            paths.append(arg)
    if not paths:
        sys.exit(__doc__)
    total = {'pitch': [], 'yaw': []}
    for path in paths:
        if verbose:
            print(f'\n=== {path}')
        found = analyze(path, min_seconds, min_rate, verbose)
        if found['pitch'] or found['yaw']:
            print(path)
            summary('', found)
        for name in total:
            total[name] += found[name]
    print()
    summary('TOTAL', total)
    print('\nsame: a positive rate raises the reported angle (what the head tracking loop assumes first).'
          '\nopposite: it lowers it. Mostly "still": the commands did not reach the gimbal in those logs.')
