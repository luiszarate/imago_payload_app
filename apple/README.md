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
  zoom (hold + or −, drag the slider between them, or pinch on the video). The slider is
  logarithmic and covers the range of the camera's zoom mode: 25× in super resolution, 300×
  combined with the digital part (above 25×) dashed in amber. Its thumb is the level asked
  for; the small marker beside the track is the lens until it gets there. On a phone the
  zoom sits in a second column of the rail.
- **Right rail**: gimbal mode, recenter, AI detection, Point mode, joystick.
- **Tap on the video**: track the object there. In Point mode the gimbal only re-points
  (Eagle Eye).
- **Gamepad**: left stick gimbal, right stick zoom, A shutter, B record, X stop tracking,
  Y AI detection, L1/R1 view source, Menu recenter.
