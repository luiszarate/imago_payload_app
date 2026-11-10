#!/usr/bin/env python3
"""Can the Orus L turn slower than its 2 deg/s floor? Drives imago-cli through a pipe.

The gimbal ignores rates below about 2 deg/s (ground test 2026-11-09: 1.5 deg/s did not move it,
2 deg/s gave 2), so a zoomed-in joystick has a dead stretch. Two ways around it are tried here,
on the pan axis, with the timing a person cannot type:

- PWM: 2 deg/s and 0 in turn, on for target/2 of each period (1 and 0.5 deg/s, periods
  0.2, 0.4 and 1 s). It works only if the gimbal smooths after its floor, not before.
- Sigma-delta: each 1/30 s tick turns at 2 deg/s while the angle owed is ahead, else stops.
- Angles: the target yaw steps 1/30 s at a time (1, 0.5 and 0.25 deg/s), no rate at all.

Each case runs 8 s and the direction alternates, so the gimbal stays near where it started.
Watch the video and note per case whether it moved smoothly, in jerks or not at all; the
.tlog says how fast it really went.

    python3 tools/gimbal-slow-test.py                      # Herelink, gimbal manager, 10x
    python3 tools/gimbal-slow-test.py --zoom 1 --only pwm
    python3 tools/gimbal-slow-test.py --proto 2 --payload 127.0.0.1 -- --local-port 14567  # orus-sim

Files: logs/lento-<time>.tlog and .txt (imago-cli), logs/lento-<time>-cases.csv (when each
case ran, wall clock, to line up with the .tlog).
"""
import argparse
import csv
import os
import re
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field

FLOOR_DPS = 2.0  # the slowest rate the Orus L obeys (2026-11-09)
TICK_S = 1 / 30  # the core streams gimbal rates at 30 Hz
CASE_S = 8.0
REST_S = 4.0


@dataclass
class State:
    """What imago-cli last printed, updated from its output."""
    link_up: bool = False
    control: str = 'unknown'
    zoom: float = 0.0
    pitch: float | None = None
    yaw: float | None = None
    lock: threading.Lock = field(default_factory=threading.Lock)


LINK = re.compile(r'link=(UP|down)')
GIMBAL = re.compile(r'gimbal p=(-?[\d.]+) y=(-?[\d.]+|-) .*\| zoom=([\d.]+)x')
CONTROL = re.compile(r'gimbal manager control=([a-z ]+) \(')


class Cli:
    """imago-cli as a child process: commands in, state lines out."""

    def __init__(self, argv: list[str]) -> None:
        self.state = State()
        self.proc = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, text=True, bufsize=1)
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self) -> None:
        for line in self.proc.stdout:
            with self.state.lock:
                if m := LINK.search(line):
                    self.state.link_up = m.group(1) == 'UP'
                if m := GIMBAL.search(line):
                    self.state.pitch = float(m.group(1))
                    self.state.yaw = None if m.group(2) == '-' else float(m.group(2))
                    self.state.zoom = float(m.group(3))
                if m := CONTROL.search(line):
                    self.state.control = m.group(1)
            if 'unknown command' in line or 'error' in line.lower():
                print(f'   imago-cli: {line.rstrip()}')

    def send(self, command: str) -> None:
        self.proc.stdin.write(command + '\n')
        self.proc.stdin.flush()

    def wait_for(self, what: str, test, timeout_s: float, retry: str | None = None) -> bool:
        print(f'-- waiting for {what}')
        deadline = time.monotonic() + timeout_s
        last_retry = time.monotonic()
        while time.monotonic() < deadline:
            with self.state.lock:
                if test(self.state):
                    return True
            if retry and time.monotonic() - last_retry > 3:
                self.send(retry)
                last_retry = time.monotonic()
            time.sleep(0.2)
        print(f'!! no {what} after {timeout_s:.0f} s')
        return False

    def close(self) -> None:
        try:
            self.send('quit')
            self.proc.wait(timeout=10)
        except (BrokenPipeError, subprocess.TimeoutExpired):
            self.proc.kill()


def run_rate_pattern(cli: Cli, on_at) -> None:
    """Streams 0 / FLOOR_DPS on the pan axis for CASE_S; on_at(t, dt) -> signed rate."""
    sent = None
    start = time.monotonic()
    tick = 0
    while (t := time.monotonic() - start) < CASE_S:
        rate = on_at(t, TICK_S)
        if rate != sent:
            cli.send(f'rate 0 {rate:g}')
            sent = rate
        tick += 1
        time.sleep(max(0.0, start + tick * TICK_S - time.monotonic()))
    cli.send('rate 0 0')


def pwm(target: float, period: float):
    on_s = period * abs(target) / FLOOR_DPS
    sign = 1 if target > 0 else -1
    return lambda t, _dt: sign * FLOOR_DPS if (t % period) < on_s else 0.0


def sigma_delta(target: float):
    owed = [0.0]  # angle the gimbal should have turned and has not been told to yet
    sign = 1 if target > 0 else -1

    def step(_t: float, dt: float) -> float:
        # Pulses only in the asked direction: once ahead, it waits rather than turns back.
        owed[0] += target * dt
        out = sign * FLOOR_DPS if sign * owed[0] >= FLOOR_DPS * dt / 2 else 0.0
        owed[0] -= out * dt
        return out
    return step


def run_angle_sweep(cli: Cli, pitch: float, yaw0: float, rate: float) -> float:
    """Steps the yaw target from yaw0 at rate deg/s for CASE_S; returns where it ended."""
    start = time.monotonic()
    tick = 0
    yaw = yaw0
    while (t := time.monotonic() - start) < CASE_S:
        yaw = yaw0 + rate * t
        # The Orus L ignores exactly 0/0 (2026-10-27).
        cli.send(f'angle {pitch:.3f} {yaw if abs(yaw) > 0.01 or abs(pitch) > 0.01 else 0.02:.3f}')
        tick += 1
        time.sleep(max(0.0, start + tick * TICK_S - time.monotonic()))
    return yaw


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cli', default='./build/tools/imago-cli/imago-cli')
    ap.add_argument('--payload', default='-', help="imago-cli's payload IP ('-' listens, Herelink)")
    ap.add_argument('--proto', type=int, default=3, help='3 through the gimbal manager (take), 2 direct')
    ap.add_argument('--zoom', type=float, default=10.0)
    ap.add_argument('--only', choices=['pwm', 'angle'])
    ap.add_argument('extra', nargs='*', help="more imago-cli arguments, after '--' (e.g. -- --local-port 14567)")
    args = ap.parse_args()

    os.makedirs('logs', exist_ok=True)
    stamp = time.strftime('%Y%m%d-%H%M')
    base = f'logs/lento-{stamp}'
    cli = Cli([args.cli, args.payload, '--tlog', f'{base}.tlog', '--log', f'{base}.txt', '--every', '1',
               *args.extra])
    cases = open(f'{base}-cases.csv', 'w', newline='')
    log = csv.writer(cases)
    log.writerow(['case', 'what', 'start_unix_s', 'end_unix_s'])

    def case(name: str, what: str, body) -> None:
        print(f'\n>> {name}: {what}  (8 s; watch the video)')
        t0 = time.time()
        body()
        log.writerow([name, what, f'{t0:.3f}', f'{time.time():.3f}'])
        cases.flush()
        time.sleep(REST_S)

    try:
        if not cli.wait_for('link=UP', lambda s: s.link_up, 30):
            return 1
        cli.send(f'proto {args.proto}')
        if args.proto == 3:
            cli.send('take')
            if not cli.wait_for('gimbal manager control', lambda s: s.control == 'this app', 20, 'take'):
                return 1
        cli.send('mode 1')
        time.sleep(3)
        cli.send(f'zoomto {args.zoom:g}')
        if not cli.wait_for(f'zoom {args.zoom:g}x', lambda s: abs(s.zoom - args.zoom) <= 0.1 * args.zoom, 15):
            print('!! carrying on at the zoom the camera reports; note it')
        input('\nPoint the camera at something with detail, then press Enter to start...')

        if args.only != 'angle':
            case('R1', f'reference: {FLOOR_DPS:g} deg/s held', lambda: run_rate_pattern(cli, lambda t, dt: FLOOR_DPS))
            case('W1', 'PWM 1 deg/s, period 0.2 s', lambda: run_rate_pattern(cli, pwm(-1.0, 0.2)))
            case('W2', 'PWM 1 deg/s, period 0.4 s', lambda: run_rate_pattern(cli, pwm(1.0, 0.4)))
            case('W3', 'PWM 1 deg/s, period 1 s', lambda: run_rate_pattern(cli, pwm(-1.0, 1.0)))
            case('W4', 'PWM 0.5 deg/s, period 0.4 s', lambda: run_rate_pattern(cli, pwm(0.5, 0.4)))
            case('W5', 'PWM 0.5 deg/s, period 1 s', lambda: run_rate_pattern(cli, pwm(-0.5, 1.0)))
            case('S1', 'sigma-delta 1 deg/s', lambda: run_rate_pattern(cli, sigma_delta(1.0)))
            case('S2', 'sigma-delta 0.5 deg/s', lambda: run_rate_pattern(cli, sigma_delta(-0.5)))

        if args.only != 'pwm':
            if not cli.wait_for('gimbal yaw', lambda s: s.yaw is not None and s.pitch is not None, 10):
                return 1
            with cli.state.lock:
                pitch, yaw = cli.state.pitch, cli.state.yaw
            # Hold the start first: the first angle may jump if the yaw frames differ.
            def hold() -> None:
                cli.send(f'angle {pitch:.3f} {yaw:.3f}')
                time.sleep(CASE_S)
            case('A0', f'angle hold at p {pitch:.1f} y {yaw:.1f}', hold)
            end = [yaw]
            case('A1', 'angle sweep 1 deg/s', lambda: end.__setitem__(0, run_angle_sweep(cli, pitch, end[0], 1.0)))
            case('A2', 'angle sweep 0.5 deg/s', lambda: end.__setitem__(0, run_angle_sweep(cli, pitch, end[0], -0.5)))
            case('A3', 'angle sweep 0.25 deg/s', lambda: end.__setitem__(0, run_angle_sweep(cli, pitch, end[0], -0.25)))

        print('\nDone.')
        cli.send('zoomto 1')
        print(f'Send {base}.tlog, {base}.txt and {base}-cases.csv')
        return 0
    except KeyboardInterrupt:
        print('\nStopped.')
        return 130
    finally:
        # Whatever happened, stop the gimbal and give the gimbal manager back: a leftover hold
        # kept the iPad app off the gimbal (2026-11-09).
        try:
            cli.send('rate 0 0')
            if args.proto == 3:
                print('Releasing the gimbal manager.')
                cli.send('release')
                cli.wait_for('the release', lambda s: s.control != 'this app', 5)
        except BrokenPipeError:
            pass
        cases.close()
        cli.close()


if __name__ == '__main__':
    sys.exit(main())
