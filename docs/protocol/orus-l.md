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

Through Herelink (2026-10-16) imago-cli showed `cam=101` in two state lines and `cam=100` in
the one between: some other component in the camera range (possibly the Herelink air unit's
own camera) also sends, so the camera target may flip between them and camera commands go to
the wrong one for a while. *(Unverified who 100 is; a tlog of that link settles it.)* The same
session counted 695 sequence gaps in 7274 messages (≈10 %) against 3 lost video packets.

## Camera settings (`PARAM_EXT_SET`, 323, to the camera)

Values are written as a `uint32` in the first 4 bytes of `param_value`, `param_type` = 5
(`MAV_PARAM_EXT_TYPE_UINT32`). The payload answers with `PARAM_EXT_ACK` (324).

A single setting is read with `PARAM_EXT_REQUEST_READ` (320, `param_id` set, `param_index` = -1)
and comes back as `PARAM_EXT_VALUE` (322) with the same value layout; `PARAM_EXT_REQUEST_LIST`
(321) returns them all. The core reads `C_V_ZM_MODE` this way on link up and every 10 s, since
it sets the zoom range; the app's settings screen reads each setting it shows the same way
(the core sends them 40 ms apart). The Orus L answers a read by name (field test 2026-10-14:
the app showed the super resolution zoom range it read).

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
| `C_V_ZM_CB_LV` | Combine zoom level | index 0..23 → 1, 2, 4, 6, ..., 22, 25, 50, 75, ..., 300x. The app's zoom slider stops on these levels and sends this index |
| `C_V_ZM_SR_LV` | Super resolution level | index 0..12 → 1, 2, 4, ..., 22, 25x. Same, in super resolution mode |
| `C_V_Z_SPD` | EO zoom speed | 0..7 |
| `C_T_ZOOM` | IR digital zoom | 0..7 → 1x..8x |
| `C_T_PALETTE` | IR palette | 0..9, names per thermal core below |
| `C_T_G` | IR gain (F1 core; **not on our Orus L**, see below) | 0 high (−50..150 °C), 1 low (−50..550 °C) |
| `C_T_ISO_M` | Isotherm (F1 core; **not on our Orus L**) | 0 off, 1 on |
| `C_T_ISO_U` | Isotherm unit (F1 core; **not on our Orus L**) | 0 Kelvin, 1 Celsius |
| `C_T_RG<n>_TMP` | Isotherm threshold, n = 0..4 (**not on our Orus L**) | upper end of region n, lower end of region n+1, in the unit above |
| `C_T_RG<n>_CLR_M` | Isotherm region colour mode, n = 0..5 (**not on our Orus L**) | 0 off (palette shows), 2 linear RGB |
| `C_T_RG<n>_CLR_MN` / `_CLR_MX` | Region colours at its cold / hot end (**not on our Orus L**) | colour index: 0 black, 9 red, 10 magenta (others undocumented) |
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

IR palettes (`C_T_PALETTE`) as Gremsy's SDK names them for its two thermal cores. **Our Orus L
has the G1 core**: on 2026-10-16 the operator stepped through the ten indices and each picture
matched its G1 name (0 white hot, 1 red to yellow, 2 iron, 3 teal to red, 4 and 6 rainbows from
magenta, 5 blue-green-yellow-red-pink, 7 rainbow from dark blue, 8 grey with the hottest in red,
9 black hot), and none its F1 name. The payload reports its core as data value 21 (IR camera
type); which value is which core is still unknown, so the core uses the G1 names and colours
(sampled from those pictures) for every type until a payload with the F1 reports its value
(`isF1()` in `core/src/payload/orus_protocol.cpp`, and the HUD's "IR sensor type" row):

| Index | F1 | G1 |
| --- | --- | --- |
| 0 | White hot | White hot |
| 1 | Black hot | Fulgurite |
| 2 | Rainbow | Iron red |
| 3 | Rainbow HC | Hot iron |
| 4 | Ironbow | Medical |
| 5 | Lava | Arctic |
| 6 | Arctic | Rainbow 1 |
| 7 | Globow | Rainbow 2 |
| 8 | Graded fire | Tint |
| 9 | Hottest | Black hot |

Gain and isotherm come from Gremsy's example for the Vio F1 (camera definition version 12,
payload software 3.0.3 or later). There is no setting to fix the palette's temperature span
(manual min/max): the camera stretches the palette over the picture by itself, and the
isotherm is the way to single out a band. How the core sets a band (`imago_ir_set_isotherm`):
unit Celsius; regions 0 and 2..5 colour off; `C_T_RG0_TMP` = low, `C_T_RG1_TMP` = high;
region 1 linear from black (0) to red (9); `C_T_ISO_M` = 1 last. Above 150 °C it also sets
`C_T_G` = 1 (low gain). Each setting is written once, so retries cannot reorder them into a
wrong state. The thresholds are unsigned, so the band starts at 0 °C. Whether the Orus L's IR
core has these settings is what the app checks by reading `C_T_ISO_M`: no answer, no isotherm
controls.

**Our Orus L has neither** (imago-cli through Herelink at 192.168.144.240, 2026-10-16): reads
of `C_T_ISO_M`, `C_T_G`, `C_T_ISO_U` and `C_T_RG0_TMP` went unanswered while `C_V_ZM_MODE`
was answered, and the full parameter list the payload sent the same session (see "Recentre")
has no `C_T_G`, `C_T_ISO_*` or `C_T_RG*`. Its thermal settings are only `C_T_PALETTE` and
`C_T_ZOOM`. The app's bar therefore shows no ISO button on it, as designed; the isotherm code
stays for payloads whose thermal core has it.

The isotherm our Orus L does have lives on its web settings page (seen 2026-10-16), outside
MAVLink: "Isotherms Regions" with Status (Enable), one Threshold in °C (20) and a Measurement
Range (−20 °C~150 °C), so a single threshold rather than the F1's band. The same page holds
"Environmental Factors" (Atmos_Temp K, Distance m, Emissivity %, Humidity %, ModifyB, ModifyK,
Window Reflect Temp K, Temperature Unit) and "IR Camera" (Scale: Original; Overlay Roi; Roi
Measurement: Medium; FFC Mode; FFC Period in minutes). With Overlay Roi on, the camera draws a
measurement box and "Max/Min/Mean" in white at the top left of the IR picture. The page showed
FFC Mode "Manual" while the app showed AUTO from data value 23 *(which is current, or whether
value 23 is the inverse, unverified)*. How the page talks to the payload (its HTTP requests)
is not known yet.

The payload's parameter list on that day, 46 settings: `CAM_MODE`, `C_F_DEFOG`, `C_F_HSINK`,
`C_SOURCE`, `C_T_PALETTE`, `C_T_ZOOM`, `C_V_AE`, `C_V_BGAIN`, `C_V_BrP_HS`, `C_V_BrP_LS`,
`C_V_DEFOG`, `C_V_DEFOG_LV`, `C_V_EIS`, `C_V_EIS_LV`, `C_V_FA`, `C_V_FLIP`, `C_V_FLREDUCT`,
`C_V_FM`, `C_V_FREEZE`, `C_V_FV`, `C_V_GAIN_HS`, `C_V_GAIN_LS`, `C_V_HS`, `C_V_ICR`,
`C_V_ICR_MAN`, `C_V_ICR_THR`, `C_V_IrP`, `C_V_MinSP`, `C_V_NSREDUCT`, `C_V_NSREDUCT2D`,
`C_V_NSREDUCT3D`, `C_V_REC`, `C_V_RGAIN`, `C_V_SP`, `C_V_SPAVOID`, `C_V_STZOOM`, `C_V_WB`,
`C_V_ZM_CB_LV`, `C_V_ZM_MODE`, `C_V_ZM_SR_LV`, `GB_MODE`, `LRF_MODE`, `OSD_MODE`, `RC_MODE`,
`SETTING_TARGET`, `TRACK_MODE`. Not in the table above yet: `C_F_HSINK`, `C_V_FA`,
`C_V_GAIN_HS`, `C_V_GAIN_LS`, `SETTING_TARGET` *(meanings unknown)*. `C_V_Z_SPD` and
`C_V_F_SPD` are absent although the app offers them. `RC_MODE` read 1 (standard), `OSD_MODE`
2 (status), `C_T_PALETTE` 9.

Current values can be read with `PARAM_EXT_REQUEST_LIST` (321) / `PARAM_EXT_REQUEST_READ` (320);
the payload answers with `PARAM_EXT_VALUE` (322).

## Standard camera commands (`COMMAND_LONG` to the camera, `confirmation` starts at 1)

| Command | ID | Parameters |
| --- | --- | --- |
| `MAV_CMD_SET_CAMERA_MODE` | 530 | p2: 0 photo, 1 video. The SDK sets it before every capture or recording |
| `MAV_CMD_SET_CAMERA_ZOOM` | 531 | p1: 0 step, 1 continuous, 2 range; p2: value (continuous: -1 out, 0 stop, 1 in; range: 0..100 % of the current zoom mode). Range is how Gremsy's SDK zooms to a position. On a real Orus L (field test 2026-10-14) 0 % and 100 % land on the ends, but intermediate percents do not map linearly in x (10x in super resolution ended near 4x), so the slider uses `C_V_ZM_CB_LV` / `C_V_ZM_SR_LV` instead. `zoomrange` in `imago-cli` still sends it, to measure the mapping. |
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
| 2 | 0 | 4 | p4: level | Zoom to an exact level (1..25x in super resolution mode, up to 300x combined). Did not move a real Orus L in super resolution mode (field test 2026-10-14); the core uses the range zoom instead |
| 2 | 6 | mode | | IR flat-field correction mode: 0 manual, 1 auto (`imago_ir_set_ffc_mode`) |
| 2 | 7 | | | Trigger IR flat-field correction (`imago_ir_ffc`) |
| 2 | 8 | | | Trigger EO one-push white balance |
| 4 | 2 | cam | p4: 0 bitrate (p5 = bit/s), 1 resolution (p5 = level), 2 encoder profile (p5) | Stream settings |
| 4 | 3 | 0 | p4: 0/1 | Standby mode |
| 4 | 4 | app | | Restart a payload application |
| 4 | 5 | 0 / 1 / 2 | display / mode / x, y, w, h | EO spot auto-exposure |

Field test 2026-10-15 (in flight, 24 min `.tlog`): for six minutes every tracker command got
`MAV_RESULT_ACCEPTED` within 20-150 ms while the tracker status (value 7) stayed "tracking":
some fifty stops and forty new points, none applied. It happened while the view was EO + IR
(value 19 = 0) and the stops before it, in EO and IR + EO, had worked; switching the view to IR
set the status back to idle, but the two points sent in IR right after did not start tracking
either, so the view may be a trigger rather than the whole cause. *(Cause unverified.)* The
core therefore watches the status after each stop or point: if the command was accepted and
1.5 s later the tracker is still tracking (stop), or not tracking the new point (the box did
not move), it raises `IMAGO_EVENT_TRACKING_IGNORED`; the apps show it as a warning. A point
counts as followed when the tracker just started, when the box sits near the point, or when
the gimbal turned 2° or more since the command: with a point far from the centre the gimbal
brings the new target in, so 1.5 s later the box is back near the centre (field report
2026-10-16: every far point raised a false warning until the gimbal's turn counted). To narrow
it down: start and stop tracking several times in EO alone, EO + IR and IR, with a `.tlog`, and
try Payload Plus when it happens.

## Recentre with the autopilot's gimbal manager

Flight 2026-10-15, 11:05: a recentre (`GB_MODE` = 4) sent with the gimbal at pitch −76° made
it flip its yaw by 180° (through the nadir), overshoot to +49° and swing between −89° and
+49° for ten seconds, with nobody sending rates; the app was in "Autopilot gimbal manager"
mode, in which PX4 re-sends its last setpoint ("hold", rate 0, pitch lock) to the gimbal five
times a second. The four other recentres of the flight, from pitch −6° to −22°, were clean.
Two controllers at once, or a firmware fault when resetting from near the nadir: not settled.
The core now recentres through the manager in that mode (angle 0/0 against the nose), so only
one controller commands the gimbal; if it still happens from near the nadir, it is the
gimbal. *(What `GB_MODE` = 4 does beyond centring, a motor reset or not, is unverified.)*

imago-cli, 2026-10-16, from pitch 15.5° yaw 24.4° in follow: `GB_MODE` = 4 was acknowledged,
the payload reported `GB_MODE` 4 and then its whole parameter list (unrequested, as after a
restart of its settings service) with `GB_MODE` back to 2, and the gimbal was at pitch −0.3°
yaw 0.2°: it centres and returns to the mode it was in.

## Gimbal travel

Orus L: tilt +40° to −120°, pan ±320°, roll ±40° (operator's figures, 2026-10-15). The gimbal
reports its yaw wrapped to ±180 in both `GIMBAL_DEVICE_ATTITUDE_STATUS` and
`MOUNT_ORIENTATION` (checked on a 24 min flight log: never beyond ±180), so nothing on the wire
says how far along the pan travel it is. The core follows the yaw against the nose across the
±180 wrap (`imago_state.gimbal_pan_deg`, "pan"), assuming the gimbal sat within ±180 when first
seen. Whenever the yaw is within ±30° of the nose the pan is the yaw itself (±360 is out of
reach with ±320 of travel), which heals a wrong start, a change of heading source or a slew
missed in a gap; a reading past ±335 also shifts it by 360. Within 30° of the stop,
`gimbal_pan_near_limit` is set (cleared 10° lower) and the apps warn. *(The limits themselves are unverified
against the gimbal: it is not known what it reports when it hits the stop.)*

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

## Gimbal through the autopilot (gimbal manager, MAVLink gimbal protocol v2)

When PX4 sits between the ground and the Orus L (TELEM2), it can act as the gimbal manager and
the Orus L as the gimbal device. Field test 2026-10-14: with PX4's mount on RC input
(`MNT_MAN_ROLL` on AUX2 driving the zoom), neither this app nor QGroundControl could move the
gimbal until the AUX2 dial was turned, and tracking found targets but did not turn to them:
PX4 and the app were both commanding the gimbal. The app's "Autopilot gimbal manager" option:

- PX4: `MNT_MODE_IN` and `MNT_MODE_OUT` = MAVLink gimbal protocol v2. The app must reach PX4,
  e.g. through Herelink MAVLink forwarding.
- Take primary control: `MAV_CMD_DO_GIMBAL_MANAGER_CONFIGURE` (1001) to the autopilot,
  p1/p2 = **our own** sysid/compid (PX4 reads -2 as the autopilot itself, not the sender),
  p3/p4 = -1, p7 = 0. Give it back: p1/p2 = -3 (release if the sender holds it). The app takes
  control once per connection, as soon as the manager's status arrives, unless a ground station
  holds it, and not again after a link blip. On disconnect the core sends the release three
  times whatever the last status said (it is a no-op if someone else holds it). The gimbal rail
  toggles it.
- Move: `GIMBAL_MANAGER_SET_ATTITUDE` (282) to the autopilot, same q / angular velocity layout
  as `GIMBAL_DEVICE_SET_ATTITUDE`, `gimbal_device_id` 0. PX4 ignores it from anyone but the
  primary controller.
- Who holds it: `GIMBAL_MANAGER_STATUS` (281) from the autopilot, asked every 2 s with
  `MAV_CMD_SET_MESSAGE_INTERVAL` at 500 ms.

Zoom, photo, video, gimbal mode, recenter and tracking still go to the Orus L directly.

## Wi-Fi bursts

Field logs 2026-10-14 (Herelink hotspot, iPad Pro, two `.tlog` of 4 min, analysed with
`tools/tlog-bursts.py`), while the app sent nothing but its heartbeat:

- 144 holds longer than 250 ms in 252 s, **124 of them 410-430 ms long**: four beacon
  intervals of the access point (4 x 102.4 ms). Their timing bears no relation to our
  heartbeat, so the radio wakes on the beacon, not on our sends.
- The autopilot's messages (serial into the Herelink air unit) and the payload's (Ethernet
  into it) never share a datagram yet stop and resume together: the hold is after the point
  where they join, on the Wi-Fi hop or in the iPad, not on the air link.
- The screen recording of the same minutes shows the video frozen 400-500 ms about twice a
  second, then ~80 ms of motion. The HUD meanwhile said lost 0, jitter 1.4 ms, "behind live"
  8 ms (that figure is the minimum over the last second); only "Frame gaps" shows it.
- While the joystick streamed commands (30 a second), the holds were gone: largest gap
  ~100 ms, p99 48 ms.
- Lost messages by sequence number (autopilot 9-10 %, gimbal 34-38 %) are the same idle and
  active: upstream limits (TELEM2, forwarding), unrelated to the bursts; the access point held
  everything back without loss.

So the iPad's Wi-Fi dozes while it transmits nothing (Apple's adaptive beacon skipping) and
gets the downlink in bursts, video included; steady outgoing traffic keeps it awake. The
remedy is that traffic: `wifi_keep_awake` ("Keep Wi-Fi awake (experimental)" in the profile,
off by default) sends one zero byte every 33 ms to the MAVLink peer's discard port (9),
whether or not video runs; with Herelink forwarding the peer is the ground unit on the Wi-Fi
hop, so nothing crosses the air link. A first version sent an empty RTCP receiver report to the
stream's RTCP port: with the Herelink the video then stalled again and again (field test
2026-10-14), so the RTSP server must never get these. Whether the discard-port version removes
the holds with the Herelink is still to be measured ("Frame gaps" and a `.tlog` idle, with the
option on, and with the joystick moving).

Another RTSP viewer on the same iPad looks smooth because it buffers: it shows each frame by
its timestamp, typically 0.5-1.5 s behind, which swallows a 430 ms hold. This app shows each
frame the moment it arrives, so a hold shows as a freeze. Optional playout smoothing
(`video_smoothing_ms`, "Smoothing" in the profile, off by default) delays each frame by the
smoothing minus how late it already is against the fastest frame of the last 2 s: frames of a
burst go back to the stream's own pace, a frame later than the smoothing is shown at once, and
the delay never grows. With orus-sim `--burst 3` and 100 ms, no screen update gap above 51 ms
remained (without it, most gaps were 85-102 ms). It cannot hide the 410 ms holds above (that
would take ~500 ms of delay); it is for the 30-100 ms clumping that remains with the radio
awake. The HUD's "Frame gaps p95 / max" shows how bursty the link is (about one frame period
when smooth).

## Other controllers

The Orus L takes commands from more than one place: over Ethernet (this app, Payload Plus) and on
its serial port from the autopilot (TELEM2 on a Cube), which also relays a GCS such as
QGroundControl. Field test 2026-10-14: the zoom sometimes went back to 1x on its own, in this
app and in QGroundControl alike. The autopilot is PX4, which routes MAVLink between its links:
a zoom command from QGroundControl or from a Herelink controller button mapped to the camera
reaches the payload through PX4 on the serial port, invisible on Ethernet. *(Unverified which.)*
PX4's flight log (`vehicle_command` topic) and a passive tap on the TELEM2 wires show it; with
`MAV_<n>_FORWARD` on for TELEM2, the payload's acknowledgements also reach the Herelink side.

The payload acknowledges each command to its sender, so a `COMMAND_ACK` from the payload whose
target is another system/component is a command someone else sent; the core reports it as
`IMAGO_EVENT_FOREIGN_COMMAND`. Whether those acknowledgements reach this app depends on the
route: with Herelink MAVLink forwarding (listen mode) it sees what the ground unit routes,
direct to the payload's IP it may not. A zoom move this session did not ask for is reported as
`IMAGO_EVENT_ZOOM_CHANGED_EXTERNALLY` either way. Recording a `.tlog` (app HUD, or `imago-cli
--tlog`) keeps everything, including the autopilot's `RC_CHANNELS` when it is forwarded, plus
once a second the app's own measurements as `NAMED_VALUE_FLOAT` from its component (written,
never sent): `vid_kbps`, `vid_fps`, `vid_gap95`, `vid_gapmax`, `vid_lag_ms`, `vid_jitter`,
`vid_lost`, `vid_decode`, `vid_shown`, `cmd_rtt_ms`, `mav_gaps`, `gb_pan_deg`.
`tools/tlog-bursts.py` summarises them.

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
| 3..6 | Tracked box x, y, w, h (1920 × 1080 pixels; corner or centre *(unverified; 2026-10-16 a box last seen as 0.47, 0.44, 0.07 × 0.12 normalised, presumably from a track at the centre, points to the top-left corner)*) |
| 7 | Tracker status: `(mode << 8) \| state`, state 0 idle, 1 tracking, 2 lost; mode 0 stopped, 1 active |
| 8, 9 | LRF offset x, y |
| 10..12 | Target longitude, latitude, altitude: the point the camera looks at, worked out by the payload (its OSD's TARGET line; requested every 500 ms; 0, 0 without a GPS fix *(the payload's behaviour without a laser return is unverified)*) |
| 13..15 | Payload GPS longitude, latitude, altitude |
| 16..18 | Payload app version x.y.z |
| 19 | View mode |
| 20 | Record source |
| 21 | IR camera type (`ir_camera_type`; requested every 5 s; 2 on our Orus L, its G1 core, 2026-10-16) |
| 22 | IR palette ID (`ir_palette`, 1 s) |
| 23 | IR FFC mode (`ir_ffc_mode`, 0 manual, 1 auto, 1 s) |
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
