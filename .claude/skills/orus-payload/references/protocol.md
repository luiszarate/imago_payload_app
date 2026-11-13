# Orus L wire protocol — digest

Condensed from `docs/protocol/orus-l.md`, which has the source and date of each fact and the
*(unverified)* marks. Builders and decoders live in `core/src/payload/orus_protocol.{h,cpp}`.

## Transport and identity

| Item | Value |
| --- | --- |
| MAVLink | v2 over UDP; payload listens on 14566 |
| Our identity | sys 1 / comp 193 (`MAV_COMP_ID_ONBOARD_COMPUTER3`, the SDK's); configurable (`imago-cli --sysid/--compid`) but whether the payload accepts another is unverified |
| Heartbeat | ~1 Hz, `MAV_TYPE_ONBOARD_CONTROLLER`, `MAV_AUTOPILOT_INVALID`, `MAV_STATE_ACTIVE` |
| Direct route | Orus L IP, UDP 14566 (behind Herelink: `192.168.144.240`, RTT ~130 ms) |
| Herelink forwarding | ground unit `192.168.43.1` sends to UDP 14550 on its Wi-Fi clients; app listens and replies to the sender (RTT ~235 ms). Also carries the autopilot's telemetry |
| Video | RTSP; URL in `VIDEO_STREAM_INFORMATION` (269) asked with cmd 2504. Herelink: `rtsp://192.168.43.1:8554/fpv_stream` |
| HTTP | port 8000, Werkzeug dev server, no auth: settings pages and media |
| Apple | Local Network permission required; without it "Network is unreachable" and the core keeps retrying |

Target learning: a message from a camera-range component sets the camera target, from a
gimbal component the gimbal target, from comp 26 the payload system. Once 26 is heard, only
that system counts (`PayloadModel::isPayloadSender`); Herelink's 42/100 camera is ignored.

## Components

| Component | ID | Gets |
| --- | --- | --- |
| Payload manager | 26 (`MAV_COMP_ID_USER2`) | `MAV_CMD_USER_4`, `SET_MESSAGE_INTERVAL` for data streams, cmd 512 for `COMPONENT_INFORMATION_BASIC` |
| Camera | 100..105, usually 101 | Standard camera commands, `PARAM_EXT_*` |
| Gimbal | 154 (or `GIMBAL2..6`) | `GIMBAL_DEVICE_SET_ATTITUDE` (284) |

## Camera settings — `PARAM_EXT_SET` (323) to the camera

Value as `uint32` in the first 4 bytes of `param_value`, `param_type` 5. Ack: `PARAM_EXT_ACK`
(324). Read one: `PARAM_EXT_REQUEST_READ` (320, `param_index` −1) → `PARAM_EXT_VALUE` (322).
All: `PARAM_EXT_REQUEST_LIST` (321). The core spaces reads 40 ms apart and reads `C_V_ZM_MODE`
on link up and every 10 s.

Most used:

| param_id | Values |
| --- | --- |
| `C_SOURCE` (view) | 0 EO/IR PiP, 1 EO, 2 IR, 3 IR/EO PiP, 4 sync, 6 side by side |
| `C_V_REC` | 0 both, 1 EO, 2 IR, 5 OSD |
| `TRACK_MODE` | 0 object tracking, 1 AI detection |
| `OSD_MODE` | 0 off, 1 debug, 2 status |
| `RC_MODE` | 0 Gremsy, 1 standard |
| `GB_MODE` | 0 off, 1 lock, 2 follow, 3 mapping, 4 reset. Read is never answered; mode comes from gimbal flags |
| `LRF_MODE` | 0 1 Hz, 1 4 Hz, 2 10 Hz, 3 off |
| `C_V_ZM_MODE` | 0 combine (to 300x), 2 super resolution (to 25x) |
| `C_V_ZM_CB_LV` / `C_V_ZM_SR_LV` | zoom level index (0..23 / 0..12); tables in `orus_protocol.cpp` |
| `C_T_PALETTE` | 0..9, G1 names on our unit (`isF1()` decides) |
| `C_T_ZOOM` | 0..7 → 1x..8x |
| On/off style | many `C_V_*` use **2 on, 3 off** (flip, freeze, defog, HS, flicker); some use 1/0 (`C_V_SPAVOID`, `C_V_STZOOM`) |

Exposure, WB, focus, ICR, EIS, noise reduction, defog fan: full table in `orus-l.md` §"Camera
settings". Not on our Orus L: `C_T_G`, `C_T_ISO_*`, `C_T_RG*` (F1 core only); `C_V_Z_SPD` and
`C_V_F_SPD` are absent from its list although the app offers them. Meanings unknown:
`C_F_HSINK`, `C_V_FA`, `C_V_GAIN_HS/LS`, `SETTING_TARGET`, `CAM_MODE`.

## Standard camera commands (`COMMAND_LONG` to the camera, ack `COMMAND_ACK` 77)

| Command | ID | Notes |
| --- | --- | --- |
| `SET_CAMERA_MODE` | 530 | p2 0 photo / 1 video; set before every capture or recording |
| `SET_CAMERA_ZOOM` | 531 | p1 0 step, 1 continuous (p2 −1/0/1), 2 range (0..100 %, non-linear in x) |
| `SET_CAMERA_FOCUS` | 532 | p1 1 continuous, 4 auto; p2 −1/0/1 |
| `IMAGE_START/STOP_CAPTURE` | 2000/2001 | |
| `VIDEO_START/STOP_CAPTURE` | 2500/2501 | |
| `REQUEST_CAMERA_INFORMATION / SETTINGS / STORAGE_INFORMATION / CAPTURE_STATUS` | 521/522/525/527 | |
| `REQUEST_VIDEO_STREAM_INFORMATION` | 2504 | p1 stream id, 0 all |
| `REQUEST_MESSAGE` | 512 | p1 271 `CAMERA_FOV_STATUS`, p2 0 EO / 1 IR |

## Gremsy custom — `MAV_CMD_USER_4` (31013) to comp 26

| p1 | p2 | p3 | p4.. | Function |
| --- | --- | --- | --- | --- |
| 4 | 0 | 0 | p4 0 stop, 1 active, 2 Eagle Eye | Tracker mode |
| 4 | 0 | 1 | x, y, w, h (1920×1080 px) | Track at point; if tracker not active, only re-points |
| 2 | 0 | 4 | level | Zoom to level — did **not** move a real Orus L; unused |
| 2 | 6 | mode | | IR FFC mode 0 manual / 1 auto |
| 2 | 7 | | | Trigger IR FFC |
| 2 | 8 | | | EO one-push white balance |
| 4 | 2 | cam | p4 0 bitrate (p5 bit/s), 1 resolution, 2 encoder profile | Stream settings |
| 4 | 3 | 0 | 0/1 | Standby |
| 4 | 4 | app | | Restart a payload application |
| 4 | 5 | 0/1/2 | display / mode / x,y,w,h | EO spot auto-exposure |

## Data streams — `DEBUG` (254) from comp 26

Enable with `SET_MESSAGE_INTERVAL` (511) to comp 26: **p1 = index below**, p2 = interval µs
(0 stops), p7 = 1. Older firmware sends `PARAM_VALUE` with `param_index` instead.

| Index | Value |
| --- | --- |
| 0 / 1 | EO / IR zoom level |
| 2 | Laser range (m) |
| 3..6 | Tracked box x, y, w, h (1920×1080; likely top-left origin, unverified) |
| 7 | Tracker status `(mode << 8) \| state`; state 0 idle, 1 tracking, 2 lost |
| 8, 9 | LRF offset |
| 10..12 | Target lon, lat, alt (where the camera looks; 0,0 without GPS fix) |
| 13..15 | Payload GPS lon, lat, alt |
| 16..18 | Payload app version x.y.z |
| 19 | View mode |
| 20 | Record source |
| 21 | IR camera type (2 on our G1 unit) |
| 22 | IR palette |
| 23 | IR FFC mode |
| 24 | Gimbal mode |
| 25..27 | IR temperature max, min, mean |

## AI boxes — `V2_EXTENSION` (248), `message_type` 0x4001

Enable `TRACK_MODE` = 1; rate with `SET_MESSAGE_INTERVAL` to 26, **p1 = 248** (real ID).
Silence for 1.5 s = no boxes. Payload, little-endian packed: u16 magic `0x4744`, u8 version 1,
u8 count (≤ 20), u32 frame counter, then per box at `8 + 12·i`: u16 x, y, w, h (top-left,
1920×1080), u16 track id (0 none), u8 class (COCO-80 on EO), u8 confidence 0..255.

## Gimbal messages

- `GIMBAL_DEVICE_SET_ATTITUDE` (284) to the gimbal: by rate (q all NaN, angular velocity x/y/z
  = roll/pitch/yaw rad/s) or by angle (q from roll/pitch/yaw, velocities NaN). Flags copied
  from the latest status so the mode is kept.
- `GIMBAL_DEVICE_ATTITUDE_STATUS` (285): q, rates, flags, `delta_yaw` (extension, may be
  absent). Mode from flags: `YAW_LOCK` lock, `RETRACT` off, `NEUTRAL` reset, `0x4000` mapping,
  else follow. Frames: `YAW_IN_EARTH_FRAME` (64), `YAW_IN_VEHICLE_FRAME` (32).
- `MOUNT_ORIENTATION` (265, ~6 Hz): `yaw_absolute`, the frame the Orus L uses for angle yaw.
- `DO_MOUNT_CONTROL` (205): p1 pitch, p2 roll, p3 yaw, p6 input mode, p7 targeting. Orus L
  denies or ignores angles here.
- Through PX4: `DO_GIMBAL_MANAGER_CONFIGURE` (1001), `GIMBAL_MANAGER_SET_ATTITUDE` (282),
  `GIMBAL_MANAGER_STATUS` (281). See `gimbal.md`.

## Other telemetry

Payload: `CAMERA_CAPTURE_STATUS` (262), `STORAGE_INFORMATION` (261), `CAMERA_SETTINGS` (260),
`CAMERA_FOV_STATUS` (271), `DISTANCE_SENSOR` (132), `COMPONENT_INFORMATION_BASIC` (396),
`STATUSTEXT` (253, may be chunked).

Autopilot (comp 1, when sharing the link via Herelink forwarding; each value expires 3 s after
its last message, battery 10 s): `ATTITUDE` (30) yaw as heading when the gimbal gives no
`delta_yaw`; `GLOBAL_POSITION_INT` (33) relative alt, lat/lon (0,0 = no fix); `VFR_HUD` (74);
`SYS_STATUS` (1) battery (65535 / −1 = unknown). Seen at 5 / 1.7 / 1.4 / 0.3 Hz in flight.

## Web settings (HTTP 8000) — `core/src/web/orus_web.*`

Server-rendered forms, no JSON, no auth. Current values are the inputs' `value` and the
`selected` options. Each section's Apply posts only its own fields plus `submit=Apply`
(urlencoded), answer 302.

- `GET/POST /setup-ir-camera`: isotherm (`IsoTherms_Status`, `Threshold` °C,
  `Measurement_Range`), environmental factors, IR camera (`Scale`, `Draw_Overlay`,
  `Roi_Measurement`, `FFC_Mode`, `FFC_Period`). Isotherm changes show at once despite the
  "next reboot" message.
- `POST /setup-systems`: `Restart_Service=1` (VIO service), `Reboot_Service=1` (system).
  Same endpoint takes `Format_Trigger=1` and `Factory_Reset=1`: **never send**.
- `GET /setup-gimbal`: `<select name="Auto_Speed">` 1/0; `GET /get-gimbal-settings` JSON with
  gTune params (not read yet).

## Media (HTTP 8000) — `core/src/web/orus_media.*`, `core/src/session/media_manager.*`

- `GET /list-file`: one HTML page, IMAGES then VIDEOS, newest first; find files by the
  `/delete/<name>` links. No size, date or thumbnail.
- Names `IMG_EO_<n>.jpeg`, `IMG_IR_<n>.jpeg`, `VID_EO_<n>.mp4`, `VID_IR_<n>.mp4`; one shot =
  EO + IR with the same `n`. Numbers are not chronological.
- `GET /download/<name>`: `Content-Length`, `Last-Modified` (UTC capture time), `ETag`;
  `Range` and `If-Range` honoured (resume possible), 304 on conditional GETs.
- No embedded thumbnails; Exif ends before 4 KiB; `DateTimeOriginal` is ISO 8601 with offset.
  Core reads the first bytes for metadata, fetches IR photos (< 1.5 MiB) whole as thumbnails,
  shows an EO photo with its IR twin's picture.
- `GET /delete/<name>`: 302 whether or not the file existed; verify with
  `HEAD /download/<name>` → 404. Never `HEAD /delete/...`.
- MP4s have `moov` at the end; a power cut leaves them unplayable. ~1.2-1.6 MB/s via Herelink.
