# Android and desktop against the iOS app

The iOS app is the reference: what it does is what the other two must do. This document lists
every difference found by reading the three apps file by file (2026-10-15), so Android and
desktop can be brought up to it, and then the field tests neither has had yet. Paths: iOS
`apple/ImagoPayloadControl/`, Android `android/app/src/main/` (Kotlin under
`java/aero/imago/payloadcontrol/`), desktop `desktop/`.

Both ports were written against the core's C API as it stood that day, so profiles, the zoom
control, the settings catalog, controllers, tlog recording, the HUD, the rails, the overlay and
the toasts already match. The gaps are the iOS changes of 2026-10-15 and a few small
behaviours.

## 1. To bring from iOS

| # | iOS behaviour | iOS files | Android | Desktop |
| --- | --- | --- | --- | --- |
| 1 | **Gimbal pan in the state**: `GimbalStatus.pan` (from `gimbal_pan_valid` / `gimbal_pan_deg`, 0.1°), `panNearLimit`, `panLimit` = `IMAGO_GIMBAL_PAN_LIMIT_DEG` | `Core/PayloadStatus.swift` | `cpp/state_writer.cpp` + `core/PayloadStatus.kt` (the read order is a contract between the two, change both and regenerate the fixture) | `src/core/PayloadStatus.{h,cpp}` |
| 2 | **Attitude indicator**: YAW shows `pan ?? yaw` (±320), amber (`Theme.warning`) when `panNearLimit`; accessibility adds "near the pan limit" | `UI/AttitudeIndicator.swift` | `ui/AttitudeIndicator.kt` shows the wrapped yaw | `qml/Operator/AttitudeIndicator.qml` shows the wrapped yaw |
| 3 | **Gimbal rail**: "Y %+.1f°" from `pan ?? yaw`, amber near the limit | `UI/GimbalRail.swift` | `ui/GimbalRail.kt` | `qml/Operator/GimbalRail.qml` |
| 4 | **Pan near-limit toast**, once per approach, reset on disconnect: `"Gimbal pan %+.0f°: near its ±%.0f° limit"` (error) | `App/AppModel.swift` `warnNearPanLimit` | `app/AppModel.kt` | `src/app/AppModel.cpp` |
| 5 | **`IMAGO_EVENT_TRACKING_IGNORED` (14)** → `.trackingIgnored(command, state)` → error toast "Payload accepted Stop but is {still tracking / target lost / idle}. Try changing the view." or "Payload accepted the point but did not track it ({…}). Try changing the view." | `Core/PayloadSession.swift`, `App/AppModel.swift` | `core/NativeCore.kt` has no constant 14; `core/PayloadSession.kt` maps it to `StatusText`, so today it shows a stray info toast reading "track" or "stop" | `src/app/AppModel.cpp` has no case 14: same stray toast |
| 6 | **Recentre under the gimbal manager**: the core sends an angle setpoint through PX4 (not acknowledged), so `recenterGimbal()` registers the "Recenter" label only when the profile is not in manager mode | `App/AppModel.swift` | `app/AppModel.kt` always registers it | `src/app/AppModel.cpp` always registers it |
| 7 | Legacy shaper key `controls.gimbalMaxRateDps` migrated | `Control/GimbalInputShaper.swift` | not needed unless old installs exist | same |

Items 1–4 go together; 5 and 6 are a few lines each.

## 2. Deliberate platform differences (keep)

| Area | iOS | Android | Desktop |
| --- | --- | --- | --- |
| Delete a profile | swipe | button + "Delete X?" dialog | button + dialog |
| Clear a gamepad assignment | swipe | "Clear" button | "Clear" button |
| Learn an axis | no baseline, never auto-inverted | rest baseline + auto-invert (throttles, pedals) | same, plus D-pad as axis |
| Digital input detection | `isAnalog` flag | hat axes | `pad:dpad*`, `joy:hat*` |
| Zoom haptics | per detent and drag start | same | none |
| Zoom by wheel | — | — | over the video (stops 250 ms after the last step) and over the slider (steps through levels) |
| Gamepad zoom path | sent from the gamepad queue, bookkeeping (`zoomDirection`, label) hops to main | through `AppModel.zoom()` on the main thread | same as Android |
| Focus loss | zeroes the gimbal rate and zoom | `gamepad.stopAll()` on pause / focus loss; disconnect on stop, reconnect on start | none by design (SDL background events, polled every 4 ms) |
| tlog sharing | `ShareLink` | share intent through a FileProvider | opens the log folder in the file manager |
| HUD | — | identical to iOS | + "Resolution" and "Decoder" rows; "App: on screen" always shown, with the real swap time (iOS and Android show "App: shown (smoothing)" only when smoothing holds frames) |
| Tracking brackets | gradient | gradient | solid magenta |
| Wording | "Tap to release", "tablet's Wi-Fi", "a pinch" | "device's Wi-Fi" | "Click to release", "Click TAKE CONTROL", "Wi-Fi adapter", "the mouse wheel", "(disk full?)" |
| NAL format to the decoder | AVCC | Annex B | Annex B |
| Keep the system awake | `isIdleTimerDisabled` | `FLAG_KEEP_SCREEN_ON` | `SetThreadExecutionState` / freedesktop ScreenSaver |


## 3. What the ports have and iOS lacks (keep; port to iOS if wanted)

- Android: `app/NetworkBinder.kt` binds the process to the network that reaches the payload
  and holds `WIFI_MODE_FULL_LOW_LATENCY` (API 29+) while connected: Android's own answer to
  the Wi-Fi doze that bursts the iPad's video (see `docs/protocol/orus-l.md`, "Wi-Fi bursts").
  Validation "Ports go from 1 to 65535."; BackHandler; controls inset from the display cutout;
  `decoderCounters` also reports a `rendered` count.
- Desktop: F11 / Esc, window title with the profile, close = disconnect; command line
  `--profile --connect --open --delay --screenshot` (the CI smoke test); `IMAGO_HWDEC=0`;
  1 ms timer resolution on Windows; validation "Ports go from 1 to 65535.".

## 4. Field tests to repeat on each app

Neither port has run against a real Orus L. Same checklist the iOS app went through; the core
is shared, so the point is the platform parts (decoder, display, controllers, networking).

| Item | What to check | iOS finding to compare with |
| --- | --- | --- |
| Bursty link while idle | HUD "Frame gaps p95 / max" idle and with the joystick moving; a `.tlog` of each, through `tools/tlog-bursts.py` | iPad: ~410 ms holds about twice a second on 2026-10-14, none on 2026-10-15; cause on the iPad side, not settled |
| Keep Wi-Fi awake | Does the discard-port datagram remove the holds without hurting the video | Not yet measured with the holds present |
| Smoothing | Does 50–200 ms remove the stutter the link shows, at that cost in latency | Cannot hide ~400 ms holds |
| Tracking | Start and stop several times in EO, EO + IR and IR; does `IMAGO_EVENT_TRACKING_IGNORED` fire | Accepted and ignored for six minutes on 2026-10-15, in EO + IR |
| Gimbal pan | Turn past ±180: does the pan keep counting; CENTER; does the warning show near ±320 | Implemented 2026-10-15, untested on the gimbal |
| Recentre under the gimbal manager | Pitch far down, then CENTER: a clean return, or the swing of 2026-10-15 | Swing of ±90 for ten seconds once, from pitch −76 |
| Zoom slider, combined mode | Levels above 25x | Table unverified past index 10; `zoomsweep` pending |
| Gimbal manager (PX4) | Take control, move, release | Worked in flight 2026-10-15 |
| AI tap-to-track | Tap a detection box | Sometimes did not follow the tapped box |
| Zoom back to 1x by itself | `.tlog` with the moment | Seen once after a reconnect |
| Controllers | The operator's HOTAS and gamepad through learn mode | Works on the iPad with a Bluetooth gamepad |
| End-to-end latency | Camera-to-screen with a stopwatch in the frame | Pending on the iPad as well |

### Android specifics

- Confirm with the frame-gap metric that an idle Android tablet does not show the ~410 ms
  holds (the low-latency Wi-Fi lock should prevent them); otherwise "Keep Wi-Fi awake".
- MediaCodec low-latency on the operator's tablet: "App: decode" near one frame period.
- Bind-to-network: mobile data must not steal the route to the RTSP host.

### Desktop specifics

- Wi-Fi power save is the OS's: Windows "Wireless Adapter Settings → Power Saving Mode",
  `iw dev <if> set power_save off` on Linux. Document for operators if an idle laptop bursts.
- Hardware decoding (D3D11VA / DXVA2 / VA-API) has only run in the CI smoke test; check the
  HUD's decoder line and decode time, with `IMAGO_HWDEC=0` as the comparison.
- Drawing the decoder's GPU surface directly (ADR 0002) once latency is measured.
- HOTAS through SDL2: the mapping must survive reconnecting the stick.

## 5. Core and protocol items that reach every app

- Combined-mode zoom table (`C_V_ZM_CB_LV`) above index 10, from the `zoomsweep` run.
- Wi-Fi keep-awake: if the field test confirms it removes the iPad's holds without hurting the
  video, turn it on by default in the Herelink profile of every app.
- Restarting a payload application in flight (`MAV_CMD_USER_4` 4/4, application index
  unknown): try on the bench with `imago-cli` before exposing it.
- The pending field items in `docs/protocol/orus-l.md` (yaw flags in lock mode, lens behaviour
  on a new level mid-travel, `RC_MODE` 1).
