#!/usr/bin/env python3
"""How bursty a MAVLink link was, from a .tlog recorded by the app or imago-cli.

A phone or tablet's Wi-Fi radio dozes while the app sends nothing, and the access point then
hands it everything it held back at once. This reads the arrival times of every datagram and
tells idle seconds (the app sent at most 2 messages) from active ones (at least 10, e.g. the
joystick moving), then reports for each:

- the gaps between datagrams, and how many were long holds (> 250 ms) with their durations
  (an iPad on a Herelink hotspot: a sharp peak at 410-430 ms, four beacon intervals);
- the holds' timing relative to the app's own heartbeat (flat = the radio wakes on the
  access point's beacon, not on our sends);
- lost messages by sequence number, per sender (loss that is the same idle and active is
  upstream of the Wi-Fi: a serial link, the datalink);
- for messages that carry the sender's clock (time_boot_ms), how late they arrived against
  the fastest of the last 200 (upstream pacing vs arrival pacing).

    tools/tlog-bursts.py logs/*.tlog

Only the standard library is used. The .tlog format is 8 bytes big-endian microseconds since
1970, then the raw packet, for every packet in and out.
"""

import collections
import struct
import sys

OUR_COMPONENT = 193  # imago_config.component_id default (ONBOARD_COMPUTER3)
TIME_BOOT_FIRST = {30, 32, 33, 65, 254, 281, 285}  # time_boot_ms at payload offset 0
HOLD_US = 250_000
BURST_US = 3_000
IDLE_MAX_OUT = 2
ACTIVE_MIN_OUT = 10


def parse(path):
    """-> [(ts_us, system, component, msgid, seq, payload)], in file order."""
    data = open(path, 'rb').read()
    i, n, ts = 0, len(data), None
    records = []
    while i < n:
        b = data[i]
        if b == 0xFD and i + 10 <= n:  # MAVLink v2
            length, incompat, seq = data[i + 1], data[i + 2], data[i + 4]
            system, component = data[i + 5], data[i + 6]
            msgid = data[i + 7] | data[i + 8] << 8 | data[i + 9] << 16
            records.append((ts, system, component, msgid, seq, data[i + 10:i + 10 + length]))
            i += 10 + length + 2 + (13 if incompat & 1 else 0)
        elif b == 0xFE and i + 6 <= n:  # MAVLink v1
            length, seq, system, component, msgid = data[i + 1], data[i + 2], data[i + 3], data[i + 4], data[i + 5]
            records.append((ts, system, component, msgid, seq, data[i + 6:i + 6 + length]))
            i += 6 + length + 2
        else:  # timestamp (first byte 0x00 until the year 2255)
            if i + 8 > n:
                break
            ts = struct.unpack('>Q', data[i:i + 8])[0]
            i += 8
    return records


def percentile(values, p):
    if not values:
        return float('nan')
    values = sorted(values)
    return values[min(len(values) - 1, int(p * len(values)))]


def histogram(values, bin_width, limit=None):
    counts = collections.Counter(int(v // bin_width) * bin_width for v in values if limit is None or v <= limit)
    return ' '.join(f'{k}:{v}' for k, v in sorted(counts.items()))


def analyze(path):
    records = parse(path)
    if not records:
        print(f'{path}: no packets')
        return
    t0 = records[0][0]
    incoming = [r for r in records if r[2] != OUR_COMPONENT]
    # The app's own measurements (NAMED_VALUE_FLOAT, once a second) are written, not sent.
    stats = [r for r in records if r[2] == OUR_COMPONENT and r[3] == 251]
    outgoing = [r for r in records if r[2] == OUR_COMPONENT and r[3] != 251]
    seconds = (records[-1][0] - t0) / 1e6
    out_per_second = collections.Counter(int((r[0] - t0) / 1e6) for r in outgoing)

    def activity(ts):
        n = out_per_second.get(int((ts - t0) / 1e6), 0)
        return 'active' if n >= ACTIVE_MIN_OUT else ('idle' if n <= IDLE_MAX_OUT else None)

    print(f'\n=== {path}: {seconds:.0f} s, {len(incoming)} messages in, {len(outgoing)} out')
    senders = collections.Counter((r[1], r[2]) for r in incoming)
    print('  senders (system/component: messages): ' + ', '.join(f'{s}/{c}: {n}' for (s, c), n in senders.most_common(8)))

    arrivals = sorted(set(r[0] for r in incoming))
    heartbeats = sorted(r[0] for r in outgoing if r[3] == 0)
    for name in ('idle', 'active'):
        # Each gap belongs to the second its first datagram arrived in.
        pairs = [(a, b) for a, b in zip(arrivals, arrivals[1:]) if b - a < 2_000_000 and activity(a) == name]
        secs = sum(1 for s in range(int(seconds) + 1) if activity(t0 + s * 1_000_000) == name)
        if len(pairs) < 50:
            print(f'  {name}: too few datagrams ({len(pairs)})')
            continue
        gaps = [(b - a) / 1e3 for a, b in pairs]
        holds = [(a, b) for a, b in pairs if b - a > HOLD_US]
        print(f'  {name}: {secs} s, {len(pairs)} datagrams; gap ms p50 {percentile(gaps, .5):.1f} '
              f'p90 {percentile(gaps, .9):.1f} p99 {percentile(gaps, .99):.1f} max {max(gaps):.0f}; '
              f'{100 * sum(g <= BURST_US / 1e3 for g in gaps) / len(gaps):.0f}% within {BURST_US // 1000} ms of the previous')
        print(f'    holds > {HOLD_US // 1000} ms: {len(holds)} ({len(holds) / max(secs, 1):.2f} per s); '
              f'duration ms, 10 ms bins: {histogram([(b - a) / 1e3 for a, b in holds], 10)}')
        if heartbeats and holds:
            phase = []
            for a, _ in holds:
                prev = max((h for h in heartbeats if h <= a), default=None)
                if prev is not None and a - prev < 1_500_000:
                    phase.append((a - prev) / 1e3)
            print(f'    hold start after our last heartbeat, ms, 100 ms bins: {histogram(phase, 100)}')

    print('  lost messages by sequence number (idle / active):')
    by_sender = collections.defaultdict(list)
    for ts, system, component, _, seq, _ in incoming:
        by_sender[(system, component)].append((ts, seq))
    for key, items in sorted(by_sender.items(), key=lambda kv: -len(kv[1]))[:8]:
        lost = collections.Counter()
        seen = collections.Counter()
        for (_, a), (tb, b) in zip(items, items[1:]):
            d = (b - a) & 0xFF
            kind = activity(tb)
            seen[kind] += 1
            if 1 < d < 128:
                lost[kind] += d - 1
        def share(kind):
            return f'{lost[kind]}/{seen[kind]} ({100 * lost[kind] / max(1, seen[kind]):.1f}%)'
        print(f'    {key[0]}/{key[1]}: {share("idle")} / {share("active")}')

    if stats:
        # NAMED_VALUE_FLOAT: time_boot_ms u32, value f32, name char[10]
        series = collections.defaultdict(list)
        for _, _, _, _, _, payload in stats:
            if len(payload) >= 8:
                value = struct.unpack('<f', payload[4:8])[0]
                name = payload[8:18].split(b'\0')[0].decode(errors='replace')
                if value == value:  # not NaN
                    series[name].append(value)
        print('  app measurements, once a second (median / p90 / max; counters: last; pan: min / max):')
        for name, values in sorted(series.items()):
            if name in ('vid_lost', 'mav_gaps'):
                print(f'    {name:<10} last {values[-1]:.0f}')
            elif name == 'gb_pan_deg':
                print(f'    {name:<10} {min(values):8.1f} .. {max(values):8.1f}')
            else:
                print(f'    {name:<10} {percentile(values, .5):8.1f} {percentile(values, .9):8.1f} {max(values):8.1f}')

    print('  sender clock vs arrival (lateness ms = arrival - time_boot_ms, against the fastest of the last 200):')
    streams = collections.defaultdict(list)
    for ts, system, component, msgid, _, payload in incoming:
        if msgid in TIME_BOOT_FIRST and len(payload) >= 4:
            streams[(system, component, msgid)].append((ts, struct.unpack('<I', payload[:4])[0]))
    for key, items in sorted(streams.items(), key=lambda kv: -len(kv[1]))[:6]:
        if len(items) < 100:
            continue
        gen = [b[1] - a[1] for a, b in zip(items, items[1:]) if 0 < b[1] - a[1] < 2000]
        if not gen:
            continue  # this sender's clock is not usable (the Orus's own messages)
        offsets = [t / 1e3 - tb for t, tb in items]
        late = {'idle': [], 'active': []}
        for i, (t, _) in enumerate(items):
            kind = activity(t)
            if kind:
                late[kind].append(offsets[i] - min(offsets[max(0, i - 200):i + 1]))
        print(f'    {key[0]}/{key[1]} msg {key[2]} n={len(items)}: sent every {percentile(gen, .5):.0f} ms; '
              f'lateness idle p50 {percentile(late["idle"], .5):.0f} p99 {percentile(late["idle"], .99):.0f}, '
              f'active p50 {percentile(late["active"], .5):.0f} p99 {percentile(late["active"], .99):.0f}')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    for p in sys.argv[1:]:
        analyze(p)
