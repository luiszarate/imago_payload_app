# Imago Payload Control — iOS / iPadOS app

SwiftUI app on top of the shared C++ core (`ImagoCore` Swift package at the repository root).

## First run on your Mac

```sh
brew install xcodegen          # once
cd apple
xcodegen                       # writes ImagoPayloadControl.xcodeproj (not committed)
open ImagoPayloadControl.xcodeproj
```

In Xcode: select the **ImagoPayloadControl** target → *Signing & Capabilities* → choose your
team. Then pick your iPad or iPhone and press Run. Run `xcodegen` again after pulling changes
that add or remove files.

The first connection asks for the **Local Network** permission; allow it, or nothing will connect.

## The Mac app (Mac Catalyst)

The same target builds for the Mac: in Xcode pick **My Mac (Mac Catalyst)** and press Run.
macOS 14 or later, Apple silicon or Intel. What only the Mac needs (window size, keeping the
Mac awake, mouse wheel zoom over the video, Finder) is in `App/MacSupport.swift` and behind
`#if targetEnvironment(macCatalyst)`; everything else is the iOS code, so iOS changes reach
the Mac with nothing to port.

The Mac build is sandboxed (`Mac.entitlements`: network client and server, USB and Bluetooth
for controllers) with the hardened runtime, as notarization requires. A build phase removes
`UIRequiresFullScreen` and the landscape-only orientations from the Mac's Info.plist, or the
window could not be resized.

Distribution is internal, outside the App Store: `scripts/build-mac.sh` archives, signs with
Developer ID, notarizes and staples a `.dmg` (see the script's header for the one-time setup):

```sh
TEAM_ID=ABCDE12345 NOTARY_PROFILE=imago-notary apple/scripts/build-mac.sh
```

## Trying it without the payload

Run the simulator on the Mac and the app in the iOS Simulator:

```sh
./build/tools/orus-sim/orus-sim --video clip.h264   # see the root README for clip.h264
```

In the app, choose the **orus-sim on this Mac** connection and press Connect. Without `--video`
the stream is synthetic and the picture stays black, but controls and telemetry work.

## Layout

| Folder | Contents |
| --- | --- |
| `App/` | App entry and `AppModel` (connection, state polling, actions) |
| `Core/` | Swift wrapper of the C API, connection profiles, state types |
| `Video/` | VideoToolbox decoding and the display layer |
| `Control/` | Joystick/gamepad rate shaping, gamepad support |
| `UI/` | Screens and components, Imago theme |
| `Resources/` | Asset catalog, Space Grotesk and Manrope (SIL Open Font License) |

## Controls

- **Left rail**: photo / video mode, shutter (photo or start/stop recording), view source,
  zoom (hold + or −, a tap zooms for 300 ms; drag the slider between them, or pinch on the
  video). The slider is
  logarithmic, covers the range of the camera's zoom mode (25× in super resolution, 300×
  combined, with the digital part above 25× dashed in amber) and stops only on the camera's own
  levels (1, 2, 4 … 22, 25, 50 … 300×), with a tick at each. Its thumb is the level asked for;
  the small marker beside the track is the lens until it gets there. On a phone the
  zoom sits in a second column of the rail.
- **Right rail**: gimbal mode, recenter, AI detection, Point mode, joystick. With the
  "Autopilot gimbal manager" profile option (PX4 in front of the gimbal), a button on top shows
  who holds the gimbal and takes or gives back control.
- **Tap on the video**: track the object there; a tap inside an AI detection tracks that box.
  In Point mode the gimbal only re-points (Eagle Eye).
- **Gamepad**: Xbox, PlayStation, MFi, or a HOTAS / flight stick that presents itself as one
  (iPadOS exposes no other HID joysticks). By default: left stick gimbal, right stick zoom,
  A shutter, B record, X stop tracking, Y AI detection, L1/R1 view source, Menu recenter.
  Any button, axis, hat or trigger can be reassigned in Settings › Controller.
- **Settings** (gear): camera (zoom mode and speed, focus, defog, EIS, noise, IR-cut, OSD),
  thermal palette and zoom, stream bitrate, gimbal mode and RC input; pan / tilt speed, curve
  and dead zone for sticks, and a separate fixed speed for D-pads and buttons; and the
  controller mapping. Payload settings are read when a page opens.
- **Network HUD** (gauge): link and latency figures. "Corrupt / failed / skipped" tells where a
  frozen picture comes from: lost packets, the decoder, or the display. "Frame gaps p95 / max"
  shows how bursty the video arrives (about 33 ms when smooth); the profile's Smoothing
  (50-200 ms) evens out bursts at the cost of that much delay. "Record log" writes all
  MAVLink to a `.tlog` (Files app › Imago Payload › Logs, or the share button), which
  QGroundControl, MAVExplorer and pymavlink open. "Others' commands" counts commands the payload
  acknowledged to someone else (autopilot, GCS); camera ones and zoom moves this app did not
  ask for also raise a warning.
