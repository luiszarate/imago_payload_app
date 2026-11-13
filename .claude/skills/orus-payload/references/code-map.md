# Code map

Full version: `docs/manual-programador.md` §2-§7. ADRs: `docs/adr/0001-shared-cpp-core.md`
(core with C API, core never decodes), `0002-desktop-qt-ffmpeg.md` (Qt Quick LGPL + FFmpeg).

## Core modules (`core/src/`)

| Module | Responsibility |
| --- | --- |
| `platform/` | Sockets, threads, clock. The only place for OS code inside the core |
| `mavlink/mavlink_link` | UDP socket, direct/listen mode, peer learning, counters, keep-awake |
| `mavlink/tlog_writer`, `tlog_filter` | `.tlog` recording on its own thread (full or compact) |
| `payload/orus_protocol` | **Pure** builders/decoders: params, data indexes, detections, zoom and palette tables, `isF1()` |
| `payload/payload_model` | Payload state from received messages, expiry, `isPayloadSender`, `angleYawOffset` |
| `payload/command_manager` | Acked commands: match by command or param ID, retries with adaptive timeout (2 × RTT + 100 ms), per-command `minWaitNs`, closing by other evidence (`complete`) |
| `rtsp/`, `rtp/` | Our RTSP client (UDP with TCP fallback), SDP, H.264/H.265 depacketizer |
| `video/` | `video_receiver` (delivery, stall/restart, fallback), `lag_tracker`, smoothing |
| `web/` | Own HTTP/1.1 client (`Range`, `HEAD`, chunked), `orus_web` (settings forms), `orus_media` (list, names, dates, Exif thumbnail, flights) — pure parsers |
| `session/session` | Orchestrates everything; network thread services (heartbeat, intervals, param reads, retries, gimbal rate repeat, fine aim, head tracking, recentre check) |
| `session/fine_aim`, `aim_frame.h`, `head_tracking` | Pure controllers driven by `Session` |
| `session/media_manager` | Payload files: list/thumbnails/deletes on one thread, downloads on another |
| `session/capi.cpp` | The C boundary; never lets an exception escape |

## Threads

| Thread | Work |
| --- | --- |
| Network (`Session::networkThread_`) | MAVLink in, periodic services, gimbal rate repeat, keep-awake |
| Video (`VideoReceiver`) | RTSP/RTP; calls the video callback **on this thread** |
| tlog (`TlogWriter`) | Disk writes, never on the control path |
| Web (`imago-web`) | Settings page, one request at a time |
| Media (`imago-media`, `imago-download`) | Listing/thumbnails/deletes vs. downloads |
| Caller (apps) | Commands are encoded and sent on the thread that calls them |
| Main (apps) | Polls `imago_session_get_state` at 30 Hz, paints, receives events |

## C API groups (`core/include/imago/imago_core.h`)

Config (`imago_config_init`; `payload_ip` NULL = listen mode), session
(`create/start/stop/destroy/get_state`), recording (`start/stop_recording`,
`set_recording_detail`), video (`video_start/stop`, `report_frame_presented`, `now_ns`), gimbal
(`set_rate/set_angle/set_mode/set_protocol/set_fine_aim/take_control`), head tracking
(`head_track_*`), camera (`set_mode/take_photo/set_recording/zoom/zoom_to/zoom_range/zoom_target/focus/autofocus/set_view/set_param/read_param`, `imago_zoom_levels`),
IR (`ir_palette_name/colors`, `ir_ffc`, `ir_set_ffc_mode`, `ir_set_isotherm`), tracking
(`tracker_set_function`, `tracking_set_mode/start_at/point_at/stop`, coords normalised 0..1),
web (`web_read_ir_settings/set_isotherm/restart/get_ir_settings`), media (`media_*`), other
(`lrf_set_mode`, `stream_set_bitrate/resolution`, `stream_resolution_count/size`;
`imago_stream_camera` 1 EO, 2 IR).

Acked commands return `request_id > 0`, echoed in `IMAGO_EVENT_COMMAND_RESULT` /
`PARAM_RESULT` (`result` 0 accepted, −1 no answer after retries, else MAVLink code). Apps map
`request_id → label` and toast failures ("Palette was rejected (code 4)").

Events: `LINK_UP/DOWN`, `VIDEO_STARTED/STOPPED/ERROR`, `VIDEO_RESYNC`, `COMMAND_RESULT`,
`PARAM_RESULT`, `STREAM_URI`, `STATUS_TEXT`, `CAMERA_PARAM` (value read or accepted),
`FOREIGN_COMMAND`, `ZOOM_CHANGED_EXTERNALLY`, `TRACKING_IGNORED`, `WEB_RESULT`,
`MEDIA_RESULT`. Callbacks run on core threads: return fast, never stop/destroy the session.

## The apps — same shape, matching files

| Piece | iOS `apple/ImagoPayloadControl/` | Android `android/app/src/main/` | Desktop `desktop/` |
| --- | --- | --- | --- |
| Session wrapper | `Core/PayloadSession.swift` | `java/.../core/PayloadSession.kt` + `cpp/jni_core.cpp` | `src/core/PayloadSession.cpp` |
| State by topic | `Core/PayloadStatus.swift` | `core/PayloadStatus.kt` + `cpp/state_writer.cpp` | `src/core/PayloadStatus.{h,cpp}` |
| Settings catalogue | `Core/CameraSettings.swift` | `core/CameraSettings.kt` | `src/core/CameraSettings.cpp` |
| Connection profiles | `Core/ConnectionProfile.swift` | `core/ConnectionProfile.kt` | `src/core/ConnectionProfile.cpp` |
| Model | `App/AppModel*.swift` | `app/AppModel.kt` | `src/app/AppModel.{h,cpp}` |
| Video | `Video/VideoPipeline.swift` | `cpp/video_decoder.cpp` | `src/video/VideoDecoder.cpp` |
| Controllers | `Control/` (GameController) | `control/` | `src/control/` (SDL2) |
| Screens | `UI/` | `ui/` | `qml/Operator`, `qml/Pages` |

Mac = the iOS target through Catalyst; Mac-only code in `App/MacSupport.swift` under
`#if targetEnvironment(macCatalyst)`. Quest (`quest/`): native OpenXR operator
(`cpp/operator.{h,cpp}` owns the session), Kotlin `HudPainter.kt` draws the HUD; snapshot
contract `cpp/hud_layout.h` ↔ `NativeBridge.kt`.

Shared app patterns: 30 Hz poll assigning each topic only if changed; pending labels per
`request_id`; `cameraValues` dictionary fed by `IMAGO_EVENT_CAMERA_PARAM`; gimbal rates go from
the controller thread straight to the session; toasts in three classes (`status`, `others`,
`diagnostic`).

## Thermal (IR) path

State `ir_temp_min/max/mean_c` (data indexes 25-27), `ir_camera_type` (21), `ir_palette` (22),
`ir_ffc_mode` (23). iOS `App/AppModel+Thermal.swift` reads `C_T_PALETTE`, `C_T_ZOOM`,
`C_T_ISO_M`, … on entering IR; `UI/ThermalBar.swift` draws the palette with min on the left,
max on the right and the mean marked (`TemperatureScale`). The picture is already coloured
H.264: the app has no per-pixel temperatures. The camera auto-stretches the palette over the
scene; there is no MAVLink or web setting to fix the span. Isotherm (F1 over MAVLink; G1 only
a single threshold on the web page) is the way to single out temperatures.

Do not confuse the app's bar with the camera's own OSD, burnt into the video (`OSD_MODE` 2
"status" on our unit; 0 hides it). With the web page's "Overlay Roi" on, the camera also
draws a measurement box and Max/Min/Mean of that box. Whether data indexes 25-27 are the
box's or the whole frame's values is *(unverified)*: compare with "Roi Measurement" set to
full screen (value 3).

## Tests and CI

`core/tests/` doctest: `test_orus.cpp` (protocol, model), `test_command_manager.cpp`,
`test_session.cpp` (integration `Fixture` against `orus-sim`), `test_video.cpp`,
`test_fine_aim.cpp`, `test_head_tracking.cpp` (own ctest entry, long), `test_rtp/rtsp/util`.
Android JVM tests (`PayloadSnapshotTest` with the state fixture), desktop `tests/test_logic.cpp`
+ screenshot smoke test, Quest `HudPainterTest` (Robolectric PNGs). CI by area.
