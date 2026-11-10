# Imago Payload VR (Meta Quest, experimental)

An operator's seat for the Orus L on a Meta Quest 3 (also Quest 3S, Quest Pro, Quest 2): the
live video on a large virtual screen fixed in the room, the Touch controllers for the gimbal,
zoom, photo, recording, view and the tracker, and **head tracking**: the gimbal follows the
operator's head (switchable). Settings, IR palettes, media and the rest stay in the
iOS/Android/desktop apps.

The app is native: an OpenXR session (Khronos' loader, OpenGL ES 3) on its own thread in
`src/main/cpp/`, the core in C++, the Android app's `AMediaCodec` decoder, and a small Kotlin
activity that loads the profiles and paints the HUD overlay with a Canvas. No Unity or Unreal.

## Build and install

Requirements: JDK 17+, Android SDK with platform 37, NDK 29.0.14206865, CMake 3.31.6 (the
same as `android/`). Enable developer mode on the headset (Meta Horizon app) and connect it by
USB.

```sh
cd quest
./gradlew assembleDebug testDebugUnitTest lintDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

The app appears under **Library → Unknown sources**. CI builds it on every push (job "Quest
app (build, tests, lint)", artifact `imago-payload-quest-debug-apk`).

## Connection profiles

There is no settings screen in VR. The app writes `profiles.json` with its defaults on first
launch in its external files folder and reads it on every launch:

```
/sdcard/Android/data/aero.imago.payloadcontrol.quest/files/profiles.json
```

Pull, edit and push it with `adb pull` / `adb push` (or through MTP on a computer). The fields
mirror the Android app's profiles (`mavlinkMode` "direct" or "listen", `payloadIP`,
`mavlinkPort`, `localPort`, `videoURL`, `videoTransport`, `gimbalProtocol`, `maxVideoLagMs`,
`videoSmoothingMs`), plus:

| Block | Field | Meaning |
| --- | --- | --- |
| `headTracking` | `mode` | `rate` (closed loop on the gimbal's attitude, default) or `angle` (setpoints, for a gimbal manager) |
| | `onAtStart` | head tracking on as soon as the link is up |
| | `gainDpsPerDeg`, `maxRateDps`, `deadbandDeg`, `smoothingMs`, `yawRangeDeg` | the core's `imago_head_track_config`; 0 = the core's default (3, 90, 0.7, 80, 150) |
| | `invertPitch`, `invertYaw` | which way the first two degrees of each axis go; the core then learns the real direction from the gimbal's attitude and keeps it (field test 2026-11-09: both axes ran to the stops with a plain loop) |
| (top level) | `keepWifiAwake` | a tiny datagram ~30/s keeps the headset's Wi-Fi out of power save (default true) |
| `controls` | `maxYawDps`, `maxPitchDps`, `deadzone`, `expo`, `zoomCompensation` | the right stick as a gimbal joystick, as in the other apps |
| | `trimFactor` | while head tracking is on the stick trims the aim at this fraction of the speed |
| `panel` | `distanceM`, `widthM`, `pitchDownDeg` | the virtual screen's place in front of the operator |
| | `passthrough` | the room (the headset's cameras) around the screen, default true; dark when the runtime lacks `XR_FB_passthrough` |

The headset must be on the payload's network: Wi-Fi to the Herelink ground unit (profile
"Herelink (forwarded)": MAVLink forwarded to port 14550, video `rtsp://192.168.43.1:8554/fpv_stream`)
or to the network the Orus L is on (profile "Orus L direct"). The left **menu** button cycles
the profiles; the first one connects at launch.

## Interface

Around the picture, the other apps' layout, worked by pointing the right controller and
pulling the trigger: a top bar (the profile's name opens **Connections**: profiles,
DISCONNECT and the head tracking gain; link, video and gimbal status; SCREEN), the camera
rail on the left (PHOTO, RECORD, EO / IR / EO+IR / IR+EO, zoom + and − held, AUTOFOCUS), the
gimbal rail on the right (HEAD ON/OFF, RECENTRE, LOCK / FOLLOW / MAPPING, CENTER, STOP TRACK,
AI DETECT) and a bottom bar (head tracking and tracker status, IR palette and FFC while IR is
in view). The trigger on the picture tracks the object there.

## Controls (Touch)

| Input | Action |
| --- | --- |
| Right stick | gimbal (speed shaped and scaled by zoom); with head tracking on, trims the aim |
| Left stick up / down | zoom in / out |
| A | head tracking on / off |
| B | stop tracking |
| Right trigger | press the button under the pointer; on the picture, track the object there |
| Right grip | point the camera there (Eagle Eye) |
| X | photo |
| Y | start / stop recording |
| Left trigger | next view (EO+IR, EO, IR, IR+EO) |
| Left grip | CENTER (the payload's gimbal reset) |
| Left stick click | head tracking recentre: where the head looks now is the nose |
| Right stick click | put the screen in front of where the head looks now |
| Menu (left) | next connection profile |

## Known limits

- First run on a Quest 3 with the Orus L on 2026-11-09: passthrough, panel, HUD, controllers
  and haptics worked. Head tracking drove the gimbal to its stops (the attitude moved against
  the rate asked; the loop now learns each axis's sign and stops itself if the gimbal does
  not move or moves away, HUD "HEAD STOPPED …", recentre to retry). The video paused or
  slowed while the gimbal moved or commands went out: cause not confirmed; this build adds
  the Wi-Fi keep-awake and low-latency lock and paces the loop's rates at the gimbal period.
  The gain is still to be settled against the real Orus L (− / + in Connections). A ground
  session the same day showed the yaw rate sign is the usual one through PX4's gimbal
  manager, and that a stream of angles through the manager is followed well: with PX4 in
  between, try `"gimbalProtocol": "manager"` and `"headTracking": {"mode": "angle"}`.
- The interface has not been seen on a headset yet (built and linted only): sizes and
  overlaps may need a pass.
- No hand tracking (controllers only). Haptics: a short buzz on A, track, photo and recentre.
- Head tracking needs the gimbal's yaw relative to the nose (`gimbal_yaw_valid`): in lock
  mode with yaw relative to North and no vehicle heading, it waits.
