# Android and desktop against the iOS app

The iOS app is the reference: what it does is what the other two must do. This document lists
what Android and desktop still lack from it, the deliberate differences between the three, and
the field tests the ports have not had. Paths: iOS `apple/ImagoPayloadControl/`, Android
`android/app/src/main/` (Kotlin under `java/aero/imago/payloadcontrol/`), desktop `desktop/`.

Rule (see `CLAUDE.md`): every iOS change adds a row to section 1 until the user asks for the
ports; porting removes the row.

## 1. To bring from iOS

The iOS changes of 2026-10-15 (gimbal pan and its warning, `TRACKING_IGNORED`, recentre under
the gimbal manager) and the thermal bar (bf5714f) were ported to both apps; what follows came
after.

| # | iOS behaviour | iOS files | Android | Desktop |
| --- | --- | --- | --- | --- |
| 1 | Thermal bar at the bottom right, from the attitude indicator's right edge to the screen edge (or to the gimbal rail when the rail reaches down beside the bar, as on a phone); the attitude indicator stays at the bottom centre in every view. With the indicator hidden the bar spans between the rails. The bar gives up detail as room shrinks (`ViewThatFits`): palette by name → palette as its swatch → zoom, FFC and isotherm folded into an "IR" menu (IR zoom picker 1–8×, Calibrate now, Automatic FFC, Isotherm…) → without the scale. Toasts stay above the indicator. `setIRZoom(_:)` sets a zoom level directly | `UI/ThermalBar.swift`, `UI/OperatorView.swift` (`thermalBarInsets`, `BottomReader`), `App/AppModel+Thermal.swift` | Bar between the rails, attitude moves right above it (`ui/OperatorScreen.kt`, `ui/ThermalBar.kt`); no compact variants or "IR" menu | Same as Android (`qml/Operator/ThermalBar.qml` and the operator screen) |
| 6 | Isotherm through the payload's web settings page: Settings → Thermal & stream → "Isotherm (payload web settings)" with Isotherm, Threshold, Measurement range, Save to payload and "Restart payload to apply…" behind a confirmation (`imago_web_*`; the web host is the profile's payload IP, also in listen mode) | `UI/SettingsView.swift` (`WebIsothermSection`), `App/AppModel+Thermal.swift` (`WebIsotherm`), `Core/PayloadSession.swift` (`.webResult`, `web_host`) | Missing; `IMAGO_EVENT_WEB_RESULT` would show as a status text | Missing |
| 5 | Target position chip in the top bar, before the laser range: "22.16984, -101.05302" (TGT when the bar runs short), menu with the altitude, Copy coordinates, Share (Maps link), Open in Maps, Mark point. Marked points list in Settings → Camera → Marked points (swipe to delete, share as KML and CSV), kept in Documents/marked-points.json. The core fields (`target_*`) are shared | `UI/TopBar.swift` (`TargetChip`), `App/AppModel+Target.swift`, `Core/PayloadStatus.swift` (`TargetPosition`), `UI/SettingsView.swift` (`MarkedPointsView`) | No target shown; `state_writer.cpp` does not carry `target_*` yet | No target shown |
| 4 | Compact MAVLink logs by default; Settings → Thermal & stream → MAVLink log → "Detailed log (link debugging)" switches to everything (`imago_session_set_recording_detail`, `UserDefaults` `log.detailed`) | `App/AppModel.swift` (`detailedLog`), `Core/PayloadSession.swift`, `UI/SettingsView.swift` (`logSection`) | Logs are full (core default) | Logs are full (core default) |
| 3 | No flat-field correction in the thermal bar (decided 2026-10-16: the camera does it on its own, the operator rarely needs it): the FFC button and the "IR" menu's FFC entries are gone; `ThermalStatus.ffcAuto`, `AppModel.triggerFFC/setFFCAuto` and `PayloadSession.irFFC/setIRFFCAuto` removed. The core keeps `imago_ir_ffc` / `imago_ir_set_ffc_mode` (imago-cli uses them) | `UI/ThermalBar.swift`, `App/AppModel+Thermal.swift`, `Core/PayloadSession.swift`, `Core/PayloadStatus.swift` | FFC button still in `ui/ThermalBar.kt` | FFC button still in `qml/Operator/ThermalBar.qml` |
| 2 | The picture fits right of the camera rail, so the rail never covers the payload's OSD (drawn down the picture's left edge): `OperatorView` measures the rail's right edge on screen (`EdgeReader`, `.global`) and pads `VideoView` and `VideoOverlay` by it before `ignoresSafeArea()`; taps, AI boxes and the reticle follow because both views fit the picture in the same frame. Setting "Keep the picture clear of the camera rail" (Settings → Camera → Display, `UserDefaults` `ui.videoClearOfCameraRail`, on by default) | `UI/OperatorView.swift` (`videoInset`), `App/AppModel.swift`, `UI/SettingsView.swift` (`displaySection`) | Video full width under the rail (`ui/OperatorScreen.kt`); no setting | Same (operator screen QML); no setting |

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
| Thermal bar: isotherm panel | popover | `DropdownMenu`; it takes the window focus, so the gamepad stops driving the gimbal while it is open (focus loss already stops everything) | popup |
| Thermal bar: FFC menu | hold | long press (Compose's own vibration) | right-click or press-and-hold |
| Thermal bar: stepper repeat | system timing | 400 ms, then 120 ms speeding up to 40 ms | Qt autorepeat |
| Thermal bar: long labels | shrink to fit | cut off | elided |
| Keep the system awake | `isIdleTimerDisabled` | `FLAG_KEEP_SCREEN_ON` | `SetThreadExecutionState` / freedesktop ScreenSaver |


## 3. What the ports have and iOS lacks (keep; port to iOS if wanted)

- Android: `app/NetworkBinder.kt` binds the process to the network that reaches the payload
  and holds `WIFI_MODE_FULL_LOW_LATENCY` (API 29+) while connected: Android's own answer to
  the Wi-Fi doze that bursts the iPad's video (see `docs/protocol/orus-l.md`, "Wi-Fi bursts").
  Validation "Ports go from 1 to 65535."; BackHandler; controls inset from the display cutout;
  `decoderCounters` also reports a `rendered` count.
- Desktop: F11 / Esc, window title with the profile, close = disconnect; command line
  `--profile --connect --open --delay --screenshot --view` (the CI smoke test; `--view ir` shows
  the thermal bar); `IMAGO_HWDEC=0`;
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
| Thermal bar | Palette names against the picture; FFC tap and hold; does `C_T_ISO_M` answer (ISO button); isotherm band and gain on a real Orus L; HUD "IR sensor type" value | Same checks pending on iOS |
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
