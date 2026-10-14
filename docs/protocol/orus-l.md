# Gremsy Orus L protocol notes

How the Orus L is controlled over Ethernet. Facts here come from:

- Gremsy's public MAVLink command list:
  https://docs.gremsy.com/payloads/general/gremsy-payload-sdk/mavlink-command-list
- The behaviour of the official PayloadSdk, branch `payloadsdk_v3` (SDK 3.2.0, Aug 2026):
  https://github.com/Gremsy/PayloadSdk/tree/payloadsdk_v3

The SDK is GPLv3. **Do not copy its code into this repository.** This file describes the wire
protocol in our own words so the core can implement it independently.

Items marked *(unverified)* still need confirmation against a real Orus L or a Payload Plus capture.

## Transport

Two network layouts are in use:

- **Direct**: the app sends MAVLink to the Orus L's IP on UDP 14566 and plays its RTSP stream.
- **Through Herelink**: video comes from the Herelink ground unit's own RTSP server
  (`rtsp://192.168.43.1:8554/fpv_stream` on its Wi-Fi hotspot), which avoids sending a second
  stream over the radio. MAVLink can take two paths:
  - to the Orus L's IP over Herelink's Ethernet bridge (UDP 14566, what Payload Plus uses), or
  - Herelink's MAVLink forwarding: the ground unit sends to UDP 14550 on its Wi-Fi clients
    (confirmed 2026-10-13: payload components, gimbal attitude and DEBUG data streams arrive from
    192.168.43.1, and our commands reach the payload). The app listens and replies to the sender.
- macOS and iOS block local-network traffic until the user grants the Local Network permission;
  until then connections fail with "Network is unreachable" and the core keeps retrying.

- MAVLink v2 over UDP. The payload listens on **UDP 14566**. The SDK binds the same local port and
  sends to `<payload-ip>:14566`. Default payload IP in the SDK: `192.168.55.1`.
- Video over RTSP. The URL is announced in `VIDEO_STREAM_INFORMATION` (269), requested with
  `MAV_CMD_REQUEST_VIDEO_STREAM_INFORMATION` (2504). Codec and transport *(unverified)*.
- The ground side sends a `HEARTBEAT` about once per second (the SDK uses
  `MAV_TYPE_ONBOARD_CONTROLLER`, `MAV_AUTOPILOT_INVALID`, `MAV_STATE_ACTIVE`).
- SDK identity: system 1, component `MAV_COMP_ID_ONBOARD_COMPUTER3` (193). Ours is configurable.

## Components on the payload side

| Component | ID | Used for |
| --- | --- | --- |
| Payload manager | `MAV_COMP_ID_USER2` (26) | Custom commands (`MAV_CMD_USER_4`), data streams, parameter rates |
| Camera | `MAV_COMP_ID_CAMERA` .. `CAMERA6` (100..105), usually 101 | Standard camera commands, `PARAM_EXT_*` camera settings |
| Gimbal | `MAV_COMP_ID_GIMBAL` (154) or `GIMBAL2..6` | `GIMBAL_DEVICE_SET_ATTITUDE`, gimbal `PARAM_*` |

The system and component IDs are learned from whatever the payload sends: any message from a
component in the camera range sets the camera target, any message from a gimbal component sets the
gimbal target, and any message from component 26 sets the payload manager's system ID.

## Camera settings (`PARAM_EXT_SET`, 323, to the camera)

Values are written as a `uint32` in the first 4 bytes of `param_value`, `param_type` = 5
(`MAV_PARAM_EXT_TYPE_UINT32`). The payload answers with `PARAM_EXT_ACK` (324).

A single setting is read with `PARAM_EXT_REQUEST_READ` (320, `param_id` set, `param_index` = -1)
and comes back as `PARAM_EXT_VALUE` (322) with the same value layout; `PARAM_EXT_REQUEST_LIST`
(321) returns them all. The core reads `C_V_ZM_MODE` this way on link up and every 10 s, since
it sets the zoom range. *(Unverified on the Orus L: that it answers a read by name.)*

| param_id | Meaning | Values |
| --- | --- | --- |
| `C_SOURCE` | View source | 0 EO/IR (PiP), 1 EO, 2 IR, 3 IR/EO (PiP), 4 Sync, 6 side by side |
| `C_V_REC` | Record source | 0 both, 1 EO, 2 IR, 5 OSD |
| `TRACK_MODE` | Tracker function | 0 object tracking, 1 AI object detection |
| `OSD_MODE` | On-screen display | 0 off, 1 debug, 2 status |
| `RC_MODE` | RC input mode | 0 Gremsy, 1 standard |
| `GB_MODE` | Gimbal mode | 0 off, 1 lock, 2 follow, 3 mapping, 4 reset |
| `LRF_MODE` | Laser rangefinder | 0 1 Hz, 1 4 Hz, 2 10 Hz, 3 off |
| `C_V_ZM_MODE` | EO zoom mode | 0 combine (optical + digital), 2 super resolution |
| `C_V_ZM_CB_LV` | Combine zoom level | index 0..23 → 1, 2, 4, 6, ..., 22, 25, 50, 75, ..., 300x |
| `C_V_ZM_SR_LV` | Super resolution level | index 0..12 → 1, 2, 4, ..., 22, 25x |
| `C_V_Z_SPD` | EO zoom speed | 0..7 |
| `C_T_ZOOM` | IR digital zoom | 0..7 → 1x..8x |
| `C_T_PALETTE` | IR palette | 0..9 |
| `C_V_FLIP` | Image flip | 2 on, 3 off |
| `C_V_FREEZE` | Freeze image | 2 on, 3 off |
| `C_V_DEFOG` | Defog | 2 on, 3 off |
| `C_V_DEFOG_LV` | Defog level | 0..3 |
| `C_V_HS` | EO high sensitivity | 2 on, 3 off |
| `C_V_AE` | Exposure mode | 0 auto, 3 manual, 10 shutter priority, 11 iris priority |
| `C_V_SP` | Shutter speed | 0..21 → 1/1 .. 1/10000 |
| `C_V_MinSP` | Shutter lower limit | same scale |
| `C_V_IrP` | Aperture | 5..16 → F14 .. F1.6 |
| `C_V_BrP_HS` / `C_V_BrP_LS` | Brightness (HS on / off) | 0..41 / 0..37 |
| `C_V_WB` | White balance | 0 auto, 1 indoor, 2 outdoor, 3 one push, 4 ATW, 5 manual |
| `C_V_RGAIN`, `C_V_BGAIN` | Manual WB gains | 0..255 |
| `C_V_FM` | Focus mode | 0 manual, 1 auto, 2 one-push auto |
| `C_V_FV` | Manual focus position | 0..61440 |
| `C_V_F_SPD` | Focus speed | 0..7 |
| `C_V_ICR`, `C_V_ICR_THR`, `C_V_ICR_MAN` | IR-cut filter | auto 2 / manual 3; threshold 0..255; manual on 2 / off 3 |
| `C_V_EIS`, `C_V_EIS_LV` | Electronic stabilisation | hold 0, on 2, off 3; level super 2, super+ 3 |
| `C_V_SPAVOID` | Spot light avoidance | 1 on, 0 off |
| `C_V_FLREDUCT` | Flicker reduction | 2 on, 3 off |
| `C_V_NSREDUCT` (+ `2D`, `3D`) | Noise reduction | 0 off, 1..5, 127 manual |
| `C_V_STZOOM` | Stable zoom | 1 on, 0 off |
| `C_F_DEFOG` | Lens defog fan | 0 auto, 1 off, 2 on |

Current values can be read with `PARAM_EXT_REQUEST_LIST` (321) / `PARAM_EXT_REQUEST_READ` (320);
the payload answers with `PARAM_EXT_VALUE` (322).

## Standard camera commands (`COMMAND_LONG` to the camera, `confirmation` starts at 1)

| Command | ID | Parameters |
| --- | --- | --- |
| `MAV_CMD_SET_CAMERA_MODE` | 530 | p2: 0 photo, 1 video. The SDK sets it before every capture or recording |
| `MAV_CMD_SET_CAMERA_ZOOM` | 531 | p1: 0 step, 1 continuous, 2 range; p2: value (continuous: -1 out, 0 stop, 1 in) |
| `MAV_CMD_SET_CAMERA_FOCUS` | 532 | p1: 1 continuous, 4 auto; p2: -1 out, 0 stop, 1 in |
| `MAV_CMD_IMAGE_START_CAPTURE` | 2000 | p2: interval s |
| `MAV_CMD_IMAGE_STOP_CAPTURE` | 2001 | |
| `MAV_CMD_VIDEO_START_CAPTURE` | 2500 | |
| `MAV_CMD_VIDEO_STOP_CAPTURE` | 2501 | |
| `MAV_CMD_REQUEST_CAMERA_INFORMATION` | 521 | |
| `MAV_CMD_REQUEST_CAMERA_SETTINGS` | 522 | |
| `MAV_CMD_REQUEST_STORAGE_INFORMATION` | 525 | |
| `MAV_CMD_REQUEST_CAMERA_CAPTURE_STATUS` | 527 | |
| `MAV_CMD_REQUEST_VIDEO_STREAM_INFORMATION` | 2504 | p1: stream id (0 = all) |
| `MAV_CMD_REQUEST_MESSAGE` | 512 | p1: `CAMERA_FOV_STATUS` (271), p2: 0 EO / 1 IR |

The payload answers with `COMMAND_ACK` (77).

## Custom commands (`COMMAND_LONG` `MAV_CMD_USER_4` = 31013, to the payload manager, component 26)

`param1` selects a group and `param2` a function inside it.

| p1 | p2 | p3 | p4..p7 | Function |
| --- | --- | --- | --- | --- |
| 4 | 0 | 0 | p4: 0 stop, 1 active, 2 Eagle Eye | Tracker mode. *Active* waits for a target; *Eagle Eye* only re-points the gimbal |
| 4 | 0 | 1 | x, y, w, h | Track the object centred at (x, y) with a w × h box, in pixels of a 1920 × 1080 frame. Sent while the tracker is not active, the gimbal only re-points (Eagle Eye behaviour) |
| 2 | 0 | 4 | p4: level | Zoom to an exact level (1..25x in super resolution mode, up to 300x combined). The app's zoom slider streams it at most every 150 ms while dragged *(unverified on the Orus L: how fast the lens follows a new level that arrives mid-travel)* |
| 2 | 6 | mode | | IR flat-field correction mode: 0 manual, 1 auto |
| 2 | 7 | | | Trigger IR flat-field correction |
| 2 | 8 | | | Trigger EO one-push white balance |
| 4 | 2 | cam | p4: 0 bitrate (p5 = bit/s), 1 resolution (p5 = level), 2 encoder profile (p5) | Stream settings |
| 4 | 3 | 0 | p4: 0/1 | Standby mode |
| 4 | 4 | app | | Restart a payload application |
| 4 | 5 | 0 / 1 / 2 | display / mode / x, y, w, h | EO spot auto-exposure |

## Gimbal control (`GIMBAL_DEVICE_SET_ATTITUDE`, 284, to the gimbal)

- By rate: `q` all NaN, `angular_velocity_x/y/z` = roll / pitch / yaw rate in rad/s.
- By angle: `q` from roll / pitch / yaw (yaw −180..180, pitch −90..90), angular velocities NaN.
- `flags` are copied from the latest `GIMBAL_DEVICE_ATTITUDE_STATUS` so the mode is preserved.
- Fallback for angles beyond the quaternion range: `MAV_CMD_DO_MOUNT_CONTROL` (205) with
  p1 pitch, p2 roll, p3 yaw, p6 input mode (1 angle, 2 rate), p7 `MAV_MOUNT_MODE_MAVLINK_TARGETING`.

Before rate control, the SDK example sets `RC_MODE` = 1 (standard) with `PARAM_EXT_SET`
*(unverified whether this is required; it may also change how the pilot's RC drives the gimbal)*.

Telemetry: `GIMBAL_DEVICE_ATTITUDE_STATUS` (285) gives the quaternion, rates and flags.
Mode from flags: `YAW_LOCK` → lock, `RETRACT` → off, `NEUTRAL` → reset, bit `0x4000` → mapping,
otherwise follow. Some firmware also sends `MOUNT_ORIENTATION` (265).

Frames, as MAVLink defines them: roll and pitch are always relative to the horizon. Yaw is
relative to North when `YAW_IN_EARTH_FRAME` (64) is set, relative to the vehicle's nose when
`YAW_IN_VEHICLE_FRAME` (32) is set, and, with neither, relative to North only if `YAW_LOCK` is
set. `delta_yaw` (an extension field, rad) is the vehicle heading that relates both frames; it
is missing from older senders. Without it the core uses the autopilot's `ATTITUDE.yaw` when
the autopilot's telemetry shares the link (Herelink forwarding). *(Unverified on the Orus L:
which frame flags it sets in lock mode and whether it fills `delta_yaw`. Check by turning the
aircraft in lock mode and watching yaw, bearing and heading in `imago-cli`.)*

## Payload data streams

The payload manager exposes numbered values. Their rate is set with `MAV_CMD_SET_MESSAGE_INTERVAL`
(511) sent to component 26, where **p1 is the value index below, not a message ID**, p2 is the
interval in µs (0 stops it) and p7 = 1. The values arrive as `DEBUG` (254) with `ind` = index and
`value` = value (older firmware: `PARAM_VALUE` with `param_index`).

| Index | Value |
| --- | --- |
| 0 | EO zoom level |
| 1 | IR zoom level |
| 2 | Laser range (m) |
| 3..6 | Tracked box x, y, w, h (1920 × 1080 pixels; corner or centre *(unverified)*) |
| 7 | Tracker status: `(mode << 8) \| state`, state 0 idle, 1 tracking, 2 lost; mode 0 stopped, 1 active |
| 8, 9 | LRF offset x, y |
| 10..12 | Target longitude, latitude, altitude |
| 13..15 | Payload GPS longitude, latitude, altitude |
| 16..18 | Payload app version x.y.z |
| 19 | View mode |
| 20 | Record source |
| 21 | IR camera type |
| 22 | IR palette ID |
| 23 | IR FFC mode |
| 24 | Gimbal mode |
| 25..27 | IR temperature max, min, mean |

## AI detection boxes (`V2_EXTENSION`, 248)

Enabled with `TRACK_MODE` = 1. The stream rate is set with `MAV_CMD_SET_MESSAGE_INTERVAL` to
component 26 with p1 = 248 (a real message ID this time), e.g. 100 ms. The payload sends nothing
when detection is off, so silence for 1.5 s means "no boxes".

`message_type` = `0x4001`. Payload layout, little-endian, packed:

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u16 | magic `0x4744` |
| 2 | u8 | version (1) |
| 3 | u8 | box count (≤ 20) |
| 4 | u32 | frame counter, increments per packet |
| 8 + 12·i | u16 × 4 | box x, y, w, h (top-left origin, 1920 × 1080 pixels) |
| +8 | u16 | track ID (0 = none yet) |
| +10 | u8 | class ID (COCO-80 on EO; a different model on IR) |
| +11 | u8 | confidence, 0..255 → 0..1 |

## Other telemetry

| Message | Content |
| --- | --- |
| `CAMERA_CAPTURE_STATUS` (262) | Image and video status, image count, recording time |
| `STORAGE_INFORMATION` (261) | Total, used, available capacity, status |
| `CAMERA_SETTINGS` (260) | Mode, zoom level, focus level |
| `CAMERA_FOV_STATUS` (271) | `camera_device_id`, hfov, vfov |
| `DISTANCE_SENSOR` (132) | Laser range |
| `COMPONENT_INFORMATION_BASIC` (396) | Model, software version, serial number (request with 512 to component 26) |
| `STATUSTEXT` (253) | Recording status text, possibly chunked (`id`, `chunk_seq`) |
