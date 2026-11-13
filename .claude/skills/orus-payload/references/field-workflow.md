# Bench and field workflow

Full tool docs: `docs/manual-programador.md` §9. Ground procedures: `docs/pruebas/`
(`centrado-en-tierra.md`, `tuning-gimbal.md`, `video-beryl-mediamtx.md`).

## orus-sim (`./build/tools/orus-sim/orus-sim`)

MAVLink on UDP 14566, RTSP on 8554, web on 8000. Each flag models a measured behaviour:

| Flag | Models |
| --- | --- |
| `--video clip.h264 [--fps N]` | Decodable video (without it, frames are synthetic) |
| `--push IP:PORT` | Herelink forwarding (sends first) |
| `--autopilot` | PX4-like autopilot + gimbal manager obeying only the primary controller |
| `--heading N`, `--turn-rate N` | Vehicle heading / turning (yaw frames, `AimFrame`) |
| `--rate-scale 0.25` | Auto Speed on (¼ of the rate) |
| `--rate-delay 700` | Auto Speed off: late start and coast |
| `--rate-floor 2` | Rates under 2°/s ignored |
| `--angle-yaw-bias X` | Angle yaw lands X° off and `yaw_absolute = yaw − X` |
| `--rate-reversed` | Inverted gimbal (head-tracking sign detection) |
| `--lag MS`, `--lag-every S`, `--lag-catch-up` | Video falling behind |
| `--burst N` | Dozing Wi-Fi |
| `--udp-hold MS` | UDP vanishes with the RTSP session alive (Herelink hold) |
| `--nat` | UDP video reaches the client only after it sends a datagram from its RTP port (a router doing NAT) |
| `--no-isotherm` | IR core without gain/isotherm (our G1) |
| `--web-port N`, `--media-rate KB` | Web server port, slow media link |
| `--no-ack`, `--verbose` | No acks; detailed log |

A new hardware behaviour → new sim flag, modelled on the measurement, before the core fix.

## imago-cli (`./build/tools/imago-cli/imago-cli`)

Uses only the public C API. Default IP `192.168.144.240` (Orus L behind Herelink).

```sh
imago-cli 192.168.144.240 --tlog logs/x.tlog --log logs/x.txt          # direct
imago-cli - --rtsp rtsp://192.168.43.1:8554/fpv_stream                  # listen (Herelink forwarding)
imago-cli 127.0.0.1 --local-port 14567 --dump out.h264                  # against orus-sim
printf 'get C_T_PALETTE\nquit\n' | imago-cli 127.0.0.1                  # scripted
```

Commands (`help`; full table `kCommands` in `tools/imago-cli/main.cpp`): `rate`, `angle`,
`mode`, `proto <1|2|3>` (3 = PX4 manager), `fine <dps>`, `take`/`release`, `photo`, `rec`,
`zoom`, `zoomto`, `zoomrange`, `zoomsweep`, `view`, `track`, `eagle`, `untrack`, `detect`,
`lrf`, `bitrate <cam> <bps>`, `resolution <cam> <level>` (the payload's own stream: cam 1 EO,
2 IR), `param <ID> <v>`, `get <ID>`, `ffc`, `ffcmode`, `web`, `iso`, `restart service|system`,
`media`. Flags: `--sysid/--compid`, `--every N`, `--plain`, `--compact`.

Never run it at the same time as an app on the same payload (shared 1/193). If you `take` the
manager, always `release` at the end (a forgotten take left the iPad without gimbal).

## Reading .tlog files (`logs/` and `*.tlog` are git-ignored: positions and telemetry)

| Tool | Answers |
| --- | --- |
| `tools/tlog-gimbal.py logs/*.tlog [--from S] [--to S] [--ours COMPID]` | What was sent to the gimbal (angles, rate changes, `DO_MOUNT_CONTROL`, manager configure, `GB_MODE`/`RC_MODE` + acks), who held the manager, attitude 2×/s; per angle/reset: before vs. 3 s after |
| `tools/tlog-rate-sign.py logs/*.tlog [--min-seconds 1] [--min-rate 3]` | Per axis: same/opposite/still and median measured/asked ratio |
| `tools/tlog-bursts.py logs/*.tlog` | Gaps, holds > 250 ms, loss per sender, lateness idle vs. active, the app's own `vid_*`, `cmd_rtt_ms`, `mav_gaps`, `gb_pan_deg` |
| `tools/gimbal-slow-test.py` | Drives `imago-cli` through slow-motion cases (PWM, sigma-delta, angle steps); leaves `.tlog`, `.txt`, `-cases.csv` |

Pitch is the clean signal for gimbal motion; yaw against the nose also moves when the
aircraft turns. Quest logs: `adb pull /sdcard/Android/data/aero.imago.payloadcontrol.quest/files/logs`.

## Turning a field finding into code

1. **Write the fact** in `docs/protocol/orus-l.md`, in the matching section: date, route
   (direct / Herelink / PX4 manager), log name, numbers measured. Mark inferences
   *(unverified)*. Keep it in our words.
2. **Reproduce it** in `orus-sim` (new flag if needed) so the bench behaves like the payload.
3. **Test** in `core/tests/` that fails against the reproduction.
4. **Fix in the core**, with a comment citing the field test and date.
5. Update the manuals (and `pending-android-desktop.md` if apps differ), and this skill if a
   "hardware truth" changed.

## Asking the operator for a test

Give a short procedure: route and profile, what to press and in which order, record a
`.tlog` (app HUD or `--tlog`), note the clock time of each step, and what to watch on screen.
Vary one thing at a time (view EO / IR / EO+IR, lock / follow, zoom 1x / 10x, Auto Speed
on / off, manager / direct). Compare with Payload Plus when the payload itself is suspect.
