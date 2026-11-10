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

Profiles are edited on the 2D panel (add, delete, every field below except `controls`). They
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
| `headTracking` | `mode` | `rate` (closed loop on the gimbal's attitude, default) or `angle` (setpoints, for a gimbal manager) |
| | `onAtStart` | head tracking on as soon as the link is up |
| | `gainDpsPerDeg`, `maxRateDps`, `deadbandDeg`, `smoothingMs`, `yawRangeDeg` | the core's `imago_head_track_config`; 0 = the core's default (3, 90, 0.7, 80, 150) |
| | `invertPitch`, `invertYaw` | which way the first two degrees of each axis go; the core then learns the real direction from the gimbal's attitude and keeps it (field test 2026-11-09: both axes ran to the stops with a plain loop) |
| (top level) | `keepWifiAwake` | a tiny datagram ~30/s keeps the headset's Wi-Fi out of power save (default true) |
| `controls` | `maxYawDps`, `maxPitchDps`, `deadzone`, `expo`, `zoomCompensation` | the right stick as a gimbal joystick, as in the other apps |
| | `trimFactor` | while head tracking is on the stick trims the aim at this fraction of the speed |
| `panel` | `distanceM`, `widthM`, `pitchDownDeg` | the virtual screen's place in front of the operator |
| | `passthrough` | the room (the headset's cameras) around the screen, default true; dark when the runtime lacks `XR_FB_passthrough` |
| | `roomDim` | how much the room is darkened around the screen with passthrough, 0..1, default 0.35 |

The headset must be on the payload's network: Wi-Fi to the Herelink ground unit (profile
"Herelink (forwarded)": MAVLink forwarded to port 14550, video `rtsp://192.168.43.1:8554/fpv_stream`)
or to the network the Orus L is on (profile "Orus L direct"). VR connects to the profile
selected on the panel; in VR the profile pill (or the tune icon) opens **Connections** to
switch, and the left **menu** button cycles the profiles.

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
- The depth of the layers, the rails' angle and the room dimming can only be judged on a
  headset; the pictures show the flat overlay.
- The 2D panel and the switch between panel and VR have not run on a headset yet (the
  manifest follows Meta's hybrid app pattern: a `2D` launcher activity and a `VR` activity in
  its own task). The immersive interface is checked by rendering only.
- The session ends when leaving VR and starts again on entering: the link is not kept
  across the switch.
- No hand tracking (controllers only). Haptics: a short buzz on A, track, photo and recentre.
- Head tracking needs the gimbal's yaw relative to the nose (`gimbal_yaw_valid`): in lock
  mode with yaw relative to North and no vehicle heading, it waits.
