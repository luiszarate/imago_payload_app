# Gimbal — how the Orus L moves and how the core drives it

Facts with logs and dates: `docs/protocol/orus-l.md` §"Recentre…", §"Gimbal travel",
§"Gimbal control", §"Gimbal through the autopilot". Implementation narrative:
`docs/manual-programador.md` §8.2 (gimbal, fine aim) and §8.2b (head tracking).

## Protocols (`imago_gimbal_protocol`, per connection profile)

| Value | Name | Wire | Notes |
| --- | --- | --- | --- |
| 0 | `IMAGO_GIMBAL_PROTOCOL_V2` | `GIMBAL_DEVICE_SET_ATTITUDE` (284) to the gimbal | What Gremsy's SDK sends; "Gimbal direct" in `imago-cli` is `proto 2` |
| 1 | `IMAGO_GIMBAL_PROTOCOL_V1` | `DO_MOUNT_CONTROL` (205) | Orus L denies/ignores angles: no fine aim, head tracking faults `NO_ANGLES` |
| 2 | `IMAGO_GIMBAL_PROTOCOL_MANAGER` | `GIMBAL_MANAGER_SET_ATTITUDE` (282) to PX4 | `imago-cli proto 3`; needs primary control |

Zoom, photo, video, `GB_MODE`, recentre and tracking always go to the Orus L directly, in
every protocol.

## Physical limits and frames

- Travel: tilt +40 / −120, pan ±320, roll ±40 (operator's figures).
- Yaw is reported wrapped to ±180; the core unwraps it into `imago_state.gimbal_pan_deg`
  (re-anchors when within ±30° of the nose or past ±335) and sets `gimbal_pan_near_limit`
  within 30° of the stop (cleared 10° lower).
- Roll and pitch are horizon-relative. Yaw: vehicle frame with flag 32, earth with 64, earth
  if neither but `YAW_LOCK`. `delta_yaw` links frames; without it the core uses the
  autopilot's `ATTITUDE.yaw` when shared on the link.
- **A positive yaw rate raises the reported yaw** (2026-11-09). Pitch sign unverified.

## Rates

- Repeated at `gimbal_rate_hz` (30 Hz) while non-zero; a stop is sent three times.
- **Floor:** rates under ~2°/s are ignored (0.25..1.5°/s did nothing in 5-9 s, both axes,
  zoom 1x and 10x). 2 and 3°/s are followed accurately. Zoom does not change the gimbal.
- **Pulsing under the floor does not work** (PWM, sigma-delta: 20-138 % of the target, jerky).
- **Auto Speed** (web GIMBAL page): on → ~¼ of the rate asked (pitch 26 %, pan 16 %); off →
  the rate asked, but starts ~0.5-0.7 s late and coasts ~0.7 s after the stop. The core reads
  it every 30 s into `imago_state.gimbal_auto_speed` (−1 unread); apps then feed zoom 1x to
  the joystick shaper and disable zoom compensation in Settings.
- Joystick shaping lives in each app's `GimbalInputShaper` (same JSON fields everywhere):
  speed, curve, deadzone, `zoomCompensation` (rate × zoom^−k, default 0.5).

## Angles

- Angle setpoints stepped every 1/30 s are followed **exactly and smoothly** at 0.25-1°/s.
  Single jumps of 30-45° take ~2 s.
- **Identity quaternion (0/0) is ignored**, from PX4 and directly. Never send exactly 0/0.
- **Yaw frame:** the Orus L takes angle yaw in its own `yaw_absolute` frame
  (`MOUNT_ORIENTATION`), whatever the flags. Offset = `yaw_absolute − yaw`
  (`PayloadModel::angleYawOffset`, fresh 2 s); measured −2..+18° depending on session.
- **In flight** `yaw_absolute` is fixed over the ground, so that offset follows the heading.
  With the gimbal in yaw lock, `AimFrame` (`core/src/session/aim_frame.h`, pure) freezes the
  offset when aiming starts so the target stays fixed over the ground. In follow, or with
  nothing aiming, the frame is the nose. `rebase` on head-tracking recentre/enable.
  (Flight 2026-11-13: the live offset dragged the gimbal 22° in 2 s.) Unverified in flight
  with the fix.
- Starting angles from the reported attitude right after a fast rate pulls the gimbal back
  (it coasts, the report lags): stop first and wait for the attitude to settle.

## Recentre and `GB_MODE`

- "Center" = `IMAGO_GIMBAL_MODE_RESET` = `GB_MODE` 4, every protocol, sent **once**.
  Accepted when the gimbal reports `NEUTRAL` (~0.3 s) or sits within 3° pitch / 5° yaw of the
  nose (`Session::checkRecentre`, `CommandManager::complete`), because the ack comes ~2 s
  late after the payload re-sends its whole parameter list.
- Other `GB_MODE` values wait 3 s before a resend (`kGimbalModeAckWaitNs`, `minWaitNs`) and
  are excluded from the measured RTT.
- After a reset the gimbal returns to its previous mode. In follow, pitch creeps back to
  ~−16° at 0.3°/s; in lock it stays at the nose.
- Motors-off hang seen once (pitch −88.7°, `YAW_LOCK` flags, ignoring rates): `GB_MODE` 4 →
  `RETRACT`, then `GB_MODE` 1 powered and recentred it.
- A recentre from pitch −76° in the manager once flipped yaw 180° and swung ±for 10 s;
  cause unknown, not reproduced.

## Fine aim (`core/src/session/fine_aim.{h,cpp}`, `imago_gimbal_set_fine_aim(floor_dps)`)

Below the floor the joystick rate is not sent as a rate: the network thread advances an
**open-loop** angle target from the gimbal's position at the requested rate and sends it each
gimbal period (30 Hz) via 284 or 282.

- Hysteresis: enters under the floor, leaves above floor × 1.25.
- After a real rate within 1.5 s: three stops, wait for the attitude to settle (0.5 s under
  0.3°, max 1.5 s), start from there plus what the stick asked meanwhile.
- On release: hold the angle ≥ 1.2 s (to 2.5 s if still moving), then three zero-rate stops.
- Clamped to tilt −90..+40, yaw ±179.5, never exactly 0/0.
- **Never bind the target to the reported attitude** (an earlier ±2° leash chased the 18°
  offset and spun the gimbal: `fino2-20261109-1658`).
- Apps enable it with the **Fine aim** setting (floor 2°/s) on connect: iOS
  `PayloadSession.setFineAim`, Android `NativeCore.gimbalSetFineAim`, desktop
  `PayloadSession::setFineAim`. Quest: right stick with fine aim at 2°/s.
- Tests: `core/tests/test_fine_aim.cpp`; `test_session.cpp` "under the gimbal's floor the
  joystick aims by angle", "fine aim allows for the gimbal's absolute yaw and never runs off".

## Head tracking (`core/src/session/head_tracking.{h,cpp}`, `imago_head_track_*`)

The app (Quest) only hands over the head pose (`imago_head_track_pose_quaternion`, OpenXR
axes) from its tracking thread. All control runs in the core's network thread.

- **ANGLE mode (the Orus L's):** open-loop angle setpoints, one per gimbal period while the
  head moves, one per 500 ms while still; filtered (`smoothing_ms` 80), deadband,
  `max_rate_dps` ramp, `yaw_absolute` offset as fine aim, never 0/0. Faults: `NO_ANGLES` (V1
  protocol), `NO_MOTION` (> 15° off for 6 s: no manager control, motors off).
- **RATE mode:** proportional loop on reported attitude. **Unstable on the Orus L** (delay ×
  gain must stay under ~1.5; default gain 0.6). Learns axis sign after 2° of motion; faults
  `NO_MOTION`, `RAN_AWAY`.
- Zoom scaling: target = scale × head + offset, scale = zoom^−`zoom_compensation` (0 core
  default, 1 in Quest); applies even with Auto Speed (these are angles).
- Clutch `imago_head_track_hold`; recentre makes the current head direction the nose; a
  joystick rate while active trims the offset instead of fighting.
- Pauses: pose timeout 500 ms → stop and hold; waits while the payload tracker has a target.
- Every stop/disable/fault ends with three zero rates.
- Tests: `core/tests/test_head_tracking.cpp` (pure controller with a fake gimbal, plus
  `orus-sim` integration with `--angle-yaw-bias -18`, `--rate-scale 0.25`,
  `--rate-reversed`, `--rate-delay 700`).

## PX4 gimbal manager (MAVLink gimbal protocol v2)

When PX4 sits between ground and the Orus L (TELEM2), PX4 is the manager and the Orus L the
device. PX4 needs `MNT_MODE_IN` / `MNT_MODE_OUT` = gimbal protocol v2.

- **Take control:** `DO_GIMBAL_MANAGER_CONFIGURE` (1001) to the autopilot, p1/p2 = **our own**
  sysid/compid (−2 means the autopilot itself to PX4), p3/p4 −1, p7 0. Release: p1/p2 = −3.
- **Status:** `GIMBAL_MANAGER_STATUS` (281), requested every 2 s at 500 ms interval.
- Apps take primary control as soon as status arrives and retake it if it falls to "nobody"
  (≤ every 5 s; `keepGimbalControl` in each `AppModel`), never from a GCS (255 or comp 190).
  On disconnect the core sends the release three times.
- **PX4 resends its last setpoint forever** (~every 0.5 s), even after release. So the core
  always leaves a zero rate as the last setpoint (end of hold, release, protocol change,
  session stop). Mixing direct (`proto 2`) commands with a live manager made the gimbal jump
  between both and once run on its own at ~5°/s for 17 s.
- **RC takes control back:** touching the Herelink sticks/wheel made PX4 give primary control
  to itself (1/1) within a second, with no MAVLink command.
- Several app copies share 1/193: one releasing (or closing) releases for all, and every copy
  thinks it holds control.
- PX4 with RC-mapped mount (`MNT_MAN_*` on an AUX dial) fights the app: nothing moves until
  the dial is turned.

## Bench reproduction (`orus-sim`)

`--rate-scale 0.25` (Auto Speed on), `--rate-delay 700` (Auto Speed off coast),
`--rate-floor 2`, `--angle-yaw-bias -18` (yaw_absolute offset), `--rate-reversed`,
`--turn-rate N` (vehicle turning, for `AimFrame`), `--heading N`, `--autopilot` (PX4-like
manager that obeys only the primary controller).

## Open questions (do not assume)

Exact floor (1.5-2°/s? whole degrees?), pitch rate sign, what `GB_MODE` 4 does beyond
centring, which setting gives the −16° follow tilt, how fast it follows angles at head speed,
what it reports at the pan stop, cause of the yaw-frame shift between sessions, whether
`RC_MODE` = 1 is required for rates.
