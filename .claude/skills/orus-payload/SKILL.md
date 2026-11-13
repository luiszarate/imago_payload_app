---
name: orus-payload
description: How the Gremsy Orus L payload really behaves on the wire and in the field, and how the Imago Payload Control code is laid out to drive it (C++20 core with a C API, iOS reference app, Android, Qt desktop, Quest). Use for any work in this repository that touches the payload: MAVLink commands, PARAM_EXT camera settings, MAV_CMD_USER_4, gimbal control (rates, angles, recentre, fine aim, head tracking, PX4 gimbal manager), zoom, tracking and AI boxes, IR/thermal, RTSP/RTP video and lag, Herelink links, the payload's web and media pages, orus-sim, imago-cli, .tlog analysis, or adding a feature that crosses core and apps. Also use when the user reports a field finding or asks why the gimbal, video or a setting misbehaved.
---

# Orus L payload and the Imago architecture

The Orus L does not behave like the MAVLink spec or Gremsy's SDK suggest in several places.
Most of what is below was learned in field tests with a date; the code is shaped around it.
Before "fixing" something that looks odd in the core, check here and in
`docs/protocol/orus-l.md`: the odd thing is usually a workaround for real hardware.

**Source of truth:** `docs/protocol/orus-l.md` (protocol, with source and date of every fact)
and `docs/manual-programador.md` (architecture, in Spanish). This skill is a map and a
digest. If it disagrees with those files, they win; fix this skill in the same commit.

## Reference files (load on demand)

| File | Read it when |
| --- | --- |
| `references/protocol.md` | Building or decoding any message: ports, IDs, `PARAM_EXT_*` settings, `MAV_CMD_USER_4` groups, `DEBUG` data indexes, `V2_EXTENSION` boxes, web and media HTTP endpoints |
| `references/gimbal.md` | Anything that moves the gimbal: rates, angles, recentre, `GB_MODE`, rate floor, Auto Speed, the `yaw_absolute` frame, fine aim, head tracking, PX4's gimbal manager |
| `references/video.md` | Video reception, lag guard, stalls, UDP/TCP fallback, Herelink and Wi-Fi holds, smoothing, badges |
| `references/code-map.md` | Finding where something lives: core modules, threads, C API groups, events, `CommandManager` semantics, the per-app file correspondence |
| `references/field-workflow.md` | Reproducing on the bench (`orus-sim` flags), driving a payload from the terminal (`imago-cli`), reading `.tlog`s, and turning a field finding into doc + sim + test |

## Non-negotiable rules (from `CLAUDE.md`)

1. **Protocol logic lives in the core**, never in Swift, Kotlin, QML or the Quest's Kotlin.
   Builders in `core/src/payload/orus_protocol.*` (pure functions), behaviour in
   `core/src/session/`, the boundary in `core/include/imago/imago_core.h` + `capi.cpp`,
   a simulation in `tools/orus-sim/`, tests in `core/tests/`, facts in `docs/protocol/orus-l.md`.
2. **Never copy Gremsy's PayloadSdk** (GPLv3). Read it to learn the protocol, describe it in
   our own words, write our own code. No FFmpeg/live555 for RTSP either: the client is ours.
3. **Hot paths never wait for the main thread and never queue**: video frame → decoder →
   screen, and joystick/gamepad/head pose → UDP. Commands are encoded and sent on the
   caller's thread.
4. **Video reception policy is shared** in `core/src/video/video_receiver.cpp`; never add
   per-app stall/fallback logic.
5. **iOS is the reference.** Change iOS only (Mac rides along via Catalyst); record each
   difference in `docs/pending-android-desktop.md` §1 unless the user asks to port. Quest is
   outside this rule (deliberately small operator seat).
6. **Manuals in the same commit** for any change of behaviour, UI, API, build or tools:
   `docs/manual-usuario.md` and `docs/manual-programador.md`, in Spanish, UI strings quoted
   in English.
7. Unconfirmed-on-hardware facts are marked *(unverified)* in `orus-l.md`. Code comments
   explain *why*, citing the field test and its date.
8. Talk to the user in Spanish; UI text, code and comments in English.

## The payload in one screen

- **Link:** MAVLink v2 over UDP. Payload listens on **14566**. Our identity is **1/193** (the
  SDK's); every copy of the app shares it, so two apps cannot be told apart.
- **Components:** payload manager **26** (custom commands, data streams), camera **100..105**
  (usually 101), gimbal **154**. Only components of the system that component 26 speaks from
  count (`PayloadModel::isPayloadSender`): the Herelink ground unit has its own camera at
  **42/100** that once stole zoom commands.
- **Routes:** direct to the Orus L IP (behind Herelink usually `192.168.144.240`; the SDK's
  default `192.168.55.1` is not ours), or listen mode on UDP **14550** for Herelink's
  forwarding (ground unit `192.168.43.1`). Video normally from the Herelink:
  `rtsp://192.168.43.1:8554/fpv_stream`. Web/media pages on HTTP **8000**.
- **Camera settings:** `PARAM_EXT_SET` (uint32 in the first 4 bytes, type 5) to the camera,
  answered with `PARAM_EXT_ACK`; reads by name with `PARAM_EXT_REQUEST_READ`.
- **Gremsy commands:** `COMMAND_LONG` `MAV_CMD_USER_4` (31013) to component 26, p1 = group,
  p2 = function.
- **Data streams:** `MAV_CMD_SET_MESSAGE_INTERVAL` to 26 where **p1 is a value index, not a
  message ID**; values arrive as `DEBUG(ind, value)`. Exception: AI boxes use p1 = 248, a real
  message ID.

## Hardware truths that shaped the code

Each has a date and a log in `orus-l.md`. Do not undo the workaround without new field evidence.

| The Orus L... | So the core... |
| --- | --- |
| Ignores exactly the identity quaternion (angle 0/0), from any route | Recentres with `GB_MODE` = 4 in every protocol, sent once; never sends 0/0 exactly |
| Acks `GB_MODE` 1.7-2.1 s late (after re-sending its whole parameter list), sometimes never | Waits 3 s before resending a `GB_MODE`, never resends a reset, accepts a reset on `NEUTRAL` flag or attitude within 3°/5° of the nose |
| Ignores gimbal rates under ~2°/s | Fine aim: below the floor the joystick moves an angle setpoint at 30 Hz instead |
| Takes the yaw of an angle setpoint in its own `yaw_absolute` frame (`MOUNT_ORIENTATION`), whatever the flags say | Adds `yaw_absolute − yaw`; in lock, `AimFrame` freezes that offset when aiming starts (it drifts with heading in flight) |
| With web "Auto Speed" on, turns at ~¼ of the rate asked; off, starts ~0.6 s late and coasts ~0.7 s | Apps drop zoom compensation when Auto Speed is on; rate loops (head tracking RATE mode) are unstable, so head tracking uses angles |
| Rejects or ignores angles via `DO_MOUNT_CONTROL` | Fine aim and head tracking need protocol V2 or the PX4 manager (`NO_ANGLES` fault on V1) |
| Range zoom (`SET_CAMERA_ZOOM` type 2) is not linear in x; zoom-to-level `USER_4` did not move it | Slider sends `C_V_ZM_CB_LV` / `C_V_ZM_SR_LV` indexes, paced one per 150 ms |
| Sometimes accepts tracker stop/point and does nothing | Checks 1.5 s later and raises `IMAGO_EVENT_TRACKING_IGNORED` |
| Settings service can stop answering while everything else runs | Apps say "no response from the payload"; the remedy is `Restart_Service` on the web page |
| Our unit has the **G1** thermal core: no `C_T_G`, no `C_T_ISO_*` over MAVLink | Palette names are G1's; ISO controls appear only if a `C_T_ISO_M` read is answered |
| Reports gimbal yaw wrapped to ±180 though pan travel is ±320 | Core unwraps it into `gimbal_pan_deg` and warns near the stop |

PX4's gimbal manager (when the app goes through the autopilot) keeps resending its last
setpoint forever, even after release, and an RC stick touch takes primary control away
silently. The core always leaves a **zero rate** as the manager's last setpoint (three stops)
and never mixes direct commands while PX4 is the manager. Details in `references/gimbal.md`.

## Where a change goes

| You are adding or fixing... | Touch, in this order |
| --- | --- |
| A new payload command or setting | `orus_protocol.{h,cpp}` builder/decoder (pure) → `Session` (+ `CommandManager` if acked) → `capi.cpp` + `imago_core.h` → `orus-sim` handles it → test in `core/tests/` → `orus-l.md` fact with source → iOS `PayloadSession.swift` / `AppModel*.swift` / UI → `pending-android-desktop.md` §1 → both manuals |
| New state the apps must show | `PayloadModel` → `imago_state` field in `imago_core.h` → iOS `PayloadStatus.swift`. If Android must read it: `state_writer.cpp` field order is a contract with `PayloadSnapshot`/`PayloadStatus.kt`; regenerate the fixture with `android/tools/make_state_fixture.sh` |
| Gimbal motion behaviour | `core/src/session/` (`session.cpp`, `fine_aim.*`, `aim_frame.h`, `head_tracking.*`), never the apps; reproduce with `orus-sim --rate-floor 2 --angle-yaw-bias -18 --rate-delay 700 --turn-rate N` |
| Video stall, lag, fallback | `core/src/video/video_receiver.cpp` / `lag_tracker.*`; bench with `orus-sim --lag/--burst/--udp-hold`; test in `core/tests/test_video.cpp`; field finding with date in `orus-l.md` |
| Web settings or media files | `core/src/web/` (pure parsers) + `Session` web thread / `MediaManager`; `orus-sim`'s `web_server.cpp` |
| UI only | `apple/ImagoPayloadControl/UI/` first; log the gap for Android/desktop |

## Workflow for a task

1. Read the relevant section of `docs/protocol/orus-l.md` and the matching reference file.
2. Write the failing test against `orus-sim` (integration `Fixture` in
   `core/tests/test_session.cpp`) or against the pure function. If the simulator lacks the
   behaviour, add it to `tools/orus-sim/` first, modelled on the measured hardware.
3. Implement in the core; keep pure logic pure (`orus_protocol`, `fine_aim`, `head_tracking`,
   `aim_frame`, `orus_web`, `orus_media` take no sockets or clocks).
4. Build and test: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ctest --test-dir build --output-on-failure`.
   One case: `build/core/tests/imago_core_tests -tc="*isotherm*"`. Concurrency changes:
   also `-DIMAGO_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug` (and TSan).
5. Wire iOS. It cannot be built on Linux; the CI job "iOS and Mac app (build, unsigned)" is
   the check. On a Mac: `cd apple && xcodegen` after adding/removing files.
6. Update `orus-l.md` (facts), both manuals, `pending-android-desktop.md`.
7. `git fetch` before pushing (the user pushes to `main` too). CI runs by area
   (`.github/scripts/changed-areas.sh`); `core/` changes rebuild everything. Status:
   `https://api.github.com/repos/luiszarate/imago_payload_app/actions/runs?head_sha=<sha>`.

## When the user brings a field problem

1. Ask for (or locate under `logs/`, git-ignored) the `.tlog`, the app's text log and the time.
2. Read it with the tools: `tools/tlog-gimbal.py` (what was sent to the gimbal vs. what it did),
   `tools/tlog-rate-sign.py` (rate sign and ratio per axis), `tools/tlog-bursts.py` (holds,
   loss, the app's own `vid_*` metrics recorded as `NAMED_VALUE_FLOAT`).
3. Check route and actors first: Herelink forwarding or direct? PX4 gimbal manager holding
   control? Another app or QGroundControl also commanding (shared 1/193 identity,
   `IMAGO_EVENT_FOREIGN_COMMAND`)? Herelink's 42/100 camera?
4. Separate what was measured from what is inferred; write the finding into `orus-l.md` with
   date and log name, *(unverified)* on the inference, then a bench reproduction in `orus-sim`
   and a test. See `references/field-workflow.md`.

## Pitfalls

- `PARAM_EXT_ACK` often never arrives through Herelink + PX4 even though the setting applied
  (7 of ~40 on 2026-11-13); confirm through the payload's data streams, not the ack alone.
- `V2_EXTENSION` (AI boxes) did not reach the app through Herelink + PX4 forwarding.
- Never send `HEAD` to `/delete/<name>` (Werkzeug runs the GET handler: it deletes). The
  web page also exposes `Format_Trigger` and `Factory_Reset` unprotected: the core never sends them.
- Do not run the app and `imago-cli` at once against the payload: same identity.
- Media file numbers are not chronological; capture time comes from the name, else Exif
  (ISO 8601 with offset on this camera), else `Last-Modified`.
- Event callbacks run on core threads: return fast, never stop or destroy the session there.
