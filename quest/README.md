# Imago Payload VR (Meta Quest, experimental)

An operator's seat for the Orus L on a Meta Quest 3 (also Quest 3S, Quest Pro, Quest 2): the
live video on a large virtual screen fixed in the room, the Touch controllers for the gimbal,
zoom, photo, recording, view and the tracker, and **head tracking**: the gimbal follows the
operator's head (switchable). Settings, IR palettes, media and the rest stay in the
iOS/Android/desktop apps.

It is a hybrid app. It opens as a **2D panel** (a window, like any Android app on the
headset): the setup screen, in the tablet app's theme, where the connection profiles and the
head tracking settings are edited with the system keyboard. **ENTER VR** starts the immersive
mode on the selected profile; the ✕ in its top bar comes back to the panel. The link to the
payload exists only in VR.

The immersive part is native: an OpenXR session (Khronos' loader, OpenGL ES 3) on its own thread in
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

Profiles are edited on the 2D panel (add, delete, every field below except the two
`invert` switches). The panel asks for a wide window and stacks into one column in a narrow
one. They
live in `profiles.json`, written with the defaults on first launch in the app's external
files folder, so `adb` can still read or replace them:

```
/sdcard/Android/data/aero.imago.payloadcontrol.quest/files/profiles.json
```

Pull, edit and push it with `adb pull` / `adb push` (or through MTP on a computer). The fields
mirror the Android app's profiles (`mavlinkMode` "direct" or "listen", `payloadIP`,
`mavlinkPort`, `localPort`, `videoURL`, `videoTransport`, `gimbalProtocol`, `maxVideoLagMs`,
`videoSmoothingMs`), plus:

| Block | Field | Meaning |
| --- | --- | --- |
| `headTracking` | `mode` | `angle` (attitude setpoints 30 times a second: what the Orus L follows, default) or `rate` (a closed loop of rates, for gimbals that take no angles) |
| | `onAtStart` | head tracking on as soon as the link is up |
| | `gainDpsPerDeg`, `maxRateDps`, `deadbandDeg`, `smoothingMs`, `yawRangeDeg` | the core's `imago_head_track_config`; 0 = the core's default (0.6, 90, 0.7, 80, 150) |
| | `zoomCompensation` | head motion times zoom^-this: 1 (default) keeps the picture's move on screen the same for a head turn at any zoom, 0 turns the gimbal what the head turns |
| | `clutch` | the camera follows the head only while the left grip is held (default false; "Clutch" on the gimbal rail) |
| | `invertPitch`, `invertYaw` | which way the first two degrees of each axis go; the core then learns the real direction from the gimbal's attitude and keeps it (field test 2026-11-09: both axes ran to the stops with a plain loop) |
| (top level) | `recordLog` | each VR session records its MAVLink to `files/logs/quest-<time>.tlog` from the start (default true); the **LOG** button of the top bar stops and starts it |
| | `detailedLog` | every message plus the app's measurements (default true); false keeps the other apps' compact log |
| (top level) | `keepWifiAwake` | a tiny datagram ~30/s keeps the headset's Wi-Fi out of power save (default true) |
| `controls` | `maxYawDps`, `maxPitchDps`, `deadzone`, `expo`, `zoomCompensation` | the right stick as a gimbal joystick, as in the other apps |
| | `fineAim` | slow stick moves go as angles (the Orus L ignores rates under ~2 deg/s), default true |
| | `trimFactor` | while head tracking is on the stick trims the aim at this fraction of the speed |
| `panel` | `distanceM`, `widthM`, `pitchDownDeg` | the virtual screen's place in front of the operator |
| | `passthrough` | the room (the headset's cameras) around the screen, default true; dark when the runtime lacks `XR_FB_passthrough` |
| | `followHead` | with head tracking on, the screen goes with the head instead of staying in the room, default true (also "Screen follows" on the gimbal rail) |
| | `vignette` | the picture's edge darkens while the head turns fast (default true; "Vignette" on the gimbal rail) |
| | `roomDim` | how much the room is darkened around the screen with passthrough, 0..1, default 0.35; in VR the sun button of the top bar steps dimmed, dark, clear, and Connections sets the level |

The headset must be on the payload's network: Wi-Fi to the Herelink ground unit (profile
"Herelink (forwarded)": MAVLink forwarded to port 14550, video `rtsp://192.168.43.1:8554/fpv_stream`)
or to the network the Orus L is on (profile "Orus L direct"). VR connects to the profile
selected on the panel; in VR the profile pill opens **Connections** to
switch.

## Interface

The immersive interface uses the tablet app's look: the same colours, Space Grotesk and
Manrope, Material icons, the gradient on active controls, translucent panels and status
pills (`HudPainter.kt` draws them by hand on a Canvas; `HudPainterTest` renders it to
`app/build/outputs/hud/*.png` off the headset, and CI publishes those as
`quest-interface-screenshots`). The panels are glass: translucent, lit from above, floating
a few centimetres in front of the picture, the two rails turned towards the operator
(`layers.h`); the picture has rounded corners and a thin frame; controls light up under the
pointer and shrink while pressed; the rails fade when not pointed at. Around the picture, worked by pointing the right controller
and pulling the trigger: a top bar (the profile's name opens **Connections**: profiles,
DISCONNECT and the head tracking gain; link and video status; SCREEN), the camera
rail on the left (PHOTO, RECORD, EO / IR / EO+IR / IR+EO, zoom + and − held, AUTOFOCUS), the
gimbal rail on the right (HEAD ON/OFF, RECENTRE, LOCK / FOLLOW / MAPPING, CENTER, TAKE CONTROL
with the gimbal manager, STOP, AI); Connections also switches the gimbal commands (direct,
mount control, gimbal manager) and the head tracking mode (angle, rate) and a bottom bar (head tracking status, IR palette and FFC while IR is
in view). The trigger on the picture tracks the object there. Under the camera rail, outside
the picture, the other apps' attitude dial: the aircraft from above, the camera's cone, the
compass ring and the pitch arc, with pitch, pan and bearing below. At the right end of the
bottom bar, the aircraft's altitude, speeds and battery when the autopilot's telemetry shares
the link. A warning on the picture and a buzz on both controllers when the gimbal nears its
pan stop. The **menu** button (or the full-screen pill) leaves only the picture, the dial and
the warnings.

The screen is moved and resized like the headset's own windows: a corner at each bottom end
resizes it (trigger held, drag), the bar with the four arrows beside the right one moves it,
and the right stick pushes it away or pulls it in meanwhile.

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
| Menu (left) | clear view: hide or show the interface (picture, attitude dial and warnings stay) |

## Known limits

- Head tracking on the real Orus L. The first two tests (2026-11-09, 2026-11-10) used the
  rate loop and the gimbal turned the right way but past the head's angle, to its stops:
  the gimbal answers rates ~0.6 s late and coasts ~0.7 s, which that loop did not survive.
  Head tracking now sends angles (with the yaw offset the Orus L wants), which ground tests
  show it follows exactly; this has passed against the simulator, not yet on the headset.
  Use **Gimbal commands** = the one that works with your aircraft (the gimbal manager with
  PX4 in between, and **IN CONTROL**).
- The logs stay on the headset (`files/logs`, the last 12): pull them with `adb` or MTP; there
  is no share button.
- The depth of the layers, the rails' angle and the room dimming can only be judged on a
  headset; the pictures show the flat overlay.
- The 2D panel's wide window (`<layout>` in the manifest) and its one-column layout have not
  run on a headset yet. The immersive interface is checked by rendering only.
- The session ends when leaving VR and starts again on entering: the link is not kept
  across the switch.
- No hand tracking (controllers only). Haptics: a short buzz on A, track, photo and recentre.
- Head tracking needs the gimbal's yaw relative to the nose (`gimbal_yaw_valid`): in lock
  mode with yaw relative to North and no vehicle heading, it waits.
