# Android and desktop against the iOS app

The iOS app is the reference: what it does is what the other two must do. This document lists
what Android and desktop still lack from it, the deliberate differences between the three, and
the field tests the ports have not had. Paths: iOS `apple/ImagoPayloadControl/`, Android
`android/app/src/main/` (Kotlin under `java/aero/imago/payloadcontrol/`), desktop `desktop/`.

Rule (see `CLAUDE.md`): every iOS change adds a row to section 1 until the user asks for the
ports; porting removes the row. The experimental Quest app (`quest/`) is outside this rule: it
is an operator's seat with a small feature set of its own, documented in `quest/README.md`.

## 1. To bring from iOS

Every iOS change up to 3740a80 was ported to both apps on 2026-10-19; the gimbal manager rule
and the CENTER label of 8f11289 on 2026-10-27; the notices (kinds, switches, top-right
placement) and the video badges ("VIDEO RECONNECTING…", "VIDEO FROZEN x.x s") of 2026-11-06 to
Android the same day and to desktop on 2026-11-06 as well. Fine aim (the switch and the core's floor) and Auto Speed (no zoom compensation while it is
on) were brought to both on 2026-11-10.

| # | iOS behaviour | iOS files | Android | Desktop |
| --- | --- | --- | --- | --- |
| 1 | Media: the payload's photos and videos (core `imago_media_*`) grouped into flights with a capture timeline strip, one cell per EO+IR shot (EO cells show the IR twin's picture, "IR view"; preview with an EO | IR switch; download and delete act on both files), selection, download per shot / selection / flight into a folder per flight, delete with confirmation, photo preview, "Live video is on" question (pause or download alongside), states (starting, login, empty, failed) | `App/AppModel+Media.swift`, `Core/MediaFiles.swift`, `UI/MediaView.swift`, `UI/MediaPreview.swift`, `UI/TopBar.swift`, `UI/OperatorView.swift`, `Core/PayloadSession.swift` | Missing entirely: JNI for `imago_media_*` and `imago_video_stop/start`, Media screen (tablet first) | Done 2026-10-20 at the same time as iOS (desktop first by user priority); differences in section 2 |
| 2 | Radio link: the Herelink's RSSI and SNR from `RADIO_STATUS` (core `imago_state.radio_*`, level worked out by the core): a chip in the top bar ("-72 dBm · SNR 28", "SNR 28" when narrow, amber weak, red critical, "No radio" lost) and one notice when it turns critical or is lost | `Core/PayloadStatus.swift` (`RadioStatus`), `App/AppModel.swift` (`radio`, `warnRadio`), `UI/TopBar.swift` (`RadioChip`) | Missing: `radio_*` through `state_writer.cpp` ↔ `PayloadSnapshot`/`PayloadStatus.kt` (fixture regenerated), chip in `ui/TopBar.kt`, notice | Missing: chip in `qml/Operator/TopBar.qml`, notice |
| 3 | Isotherm in the thermal bar on a payload without the MAVLink isotherm (our Orus L): ISO is always there; it reads the payload's web page itself on entering IR (every 10 s until it answers; one notice per connection if it fails or there is no payload IP, ISO grey with "—"), and opens a panel with on/off, presets Warm 20 / People 35 / Hot 100 / Fire 150 °C sent at once, and a threshold of one's own with Apply. Over 150 °C the core's `imago_web_isotherm_range_for` picks the high range. The scale outlines threshold..550 | `App/AppModel+Thermal.swift` (`setWebIsotherm`, `scaleIsothermBand`, `.autoRead`), `UI/WebIsothermPanel.swift`, `UI/ThermalBar.swift` | Missing: the bar's ISO keeps the MAVLink condition; the web isotherm is only in Settings | Missing: same |
| 4 | Payload panel from the range finder's chip (always there while connected: distance, "LRF off", "LRF —"): range finder rate, payload OSD, and the payload's own stream (camera, bitrate, picture size), each sent at once | `UI/PayloadPanel.swift`, `UI/TopBar.swift` (`RangeFinderChip`) | Missing: the LRF chip only shows a distance; these settings are in Settings | Missing: same |
| 5 | Map placement on a tablet: the map and the aircraft's figures sit left of the attitude indicator, as wide as it and on its baseline, 10 pt from it (one row of instruments: map, attitude, thermal bar), so they no longer climb over the thermal bar in IR; without the indicator, left of the centre and above the bar; phones as before | `UI/OperatorView.swift` (`mapTrailing`, `mapBottom`, `mapSide`) | Missing: the map sits beside the gimbal rail on a tablet | Missing: same in a large window |

Brought to all three on 2026-11-18, from the Quest app where they came first: the stream's
camera (EO 1 / IR 2 instead of camera 0) and picture size, the range finder's rate
(`imago_lrf_set_mode`, read back after 1.5 s), the profile's **Aircraft telemetry (UDP 14550)**
switch (`imago_config.telemetry_port`) with the aircraft's figures, the AI button that shows what
was asked (a second press within 1 s ignored, a notice when no box comes in 6 s), and the map with
its offline tile store. Their differences are in section 2.

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
| Thermal bar: stepper repeat | system timing | 400 ms, then 120 ms speeding up to 40 ms | Qt autorepeat |
| Thermal bar: long labels | shrink to fit | cut off | elided |
| Thermal bar: narrowest variant (no scale) | centred in its room | kept at the right end | kept at the right end |
| Thermal bar: compact variants | reached on a phone | reached on a phone | the window's minimum size (900×480) still fits the swatch variant; the "IR" menu is rarely reached |
| Temperature scale when short | labels shrink | as iOS | drops "MEAN", then the mean value, so the figures never overlap |
| Target: Share | `ShareLink` with an Apple Maps link | share intent with the coordinates and a Google Maps link | "Copy map link" (Google Maps link to the clipboard, toast "Map link copied") |
| Target: Open in Maps | Apple Maps | `geo:` link in the installed maps app, Google Maps in the browser otherwise | Google Maps in the browser |
| Marked points: page | own page | sub-page inside Settings with a "Settings" back button | list inside Settings → Camera |
| Marked points: delete | swipe | button + "Delete Point N?" dialog | button + dialog |
| Marked points: export | `ShareLink` (KML, CSV) | share intent through the FileProvider (`cache/Exports/`) | "Save as KML (maps) and CSV (spreadsheet)": written to the app data folder, which opens |
| Marked points: time | local | stored in UTC, shown in local time | stored in UTC, shown in local time |
| Restart camera service: confirmation | `confirmationDialog` | `AlertDialog`; it takes the focus, so the gamepad stops while it is open | dialog |
| Aircraft telemetry switch: default | on | on | off: a ground station on the same computer listens on 14550, and each packet reaches only one of the two |
| Aircraft figures | a panel above the map, one figure per line (the iPhone's top bar is full) | a pill above the map | a pill above the map |
| Map: settings | Settings › Camera › "Map" ("Show the map", link "Offline map") | a fifth Settings tab, "Map" | a fifth Settings tab, "Map" |
| Map: a tile missing in flight | fetched at most once a minute per tile, nothing for 60 s after a failure (as Quest) | never fetched: only what was downloaded (the app is bound to the payload's network while connected) | as iOS, at most 2 at a time |
| Map: size | 190 pt iPad, 132 pt iPhone (down to 96 pt beside the attitude indicator) | 196 dp tablet, 136 dp phone | 240 px, 190 px compact, down to 140 px; the mouse wheel zooms it |
| Map: tile store | `Application Support/tiles`, excluded from backups | `filesDir/tiles` | `AppDataLocation/tiles` |
| Map: deleting stored tiles | confirmation | confirmation | confirmation (Quest: at once) |
| Range finder row | "Measurements per second" | "Measurements per second" | "Measurements" |
| Map: tile sources | a draft, applied with "Save"; https or http | a draft, "Apply" / "Cancel"; https only (the app allows no cleartext traffic) | a draft, "Save"; https or http |
| Map: tiles kept | PNG or JPEG up to 2 MB | PNG or JPEG up to 2 MB | PNG, JPEG or WebP up to 2 MB |
| Stream settings | always shown | shown only while connected | always shown; the section is titled "The Orus L's own stream" |
| Web isotherm threshold | `Stepper` | the thermal bar's repeat button | `SpinBox` |
| Media: where it opens | full screen cover, Done; Mac menu Payload → Media… (⇧⌘M) | — | own window (Ctrl+M), so the live video stays visible |
| Media: selection | Select mode on touch; on iPad ⌘/⇧-tap; Mac click model, no Select button | — | click, Ctrl-click, Shift-click, arrows, Space |
| Media: download folder | `Documents/Media` (Files app; Mac: "Show downloads in Finder"), not changeable | — | `<Pictures>/Imago Payload Control`, "Change folder…", "Open folder" |
| Media: TIFF preview | image preview (ImageIO reads TIFF) | — | none (Qt's TIFF plug-in is not shipped): tile with Download/Open |
| Media: selection bar | "6 shots · 120.4 MB" | — | "6 selected · 120.4 MB" (size of both files of each shot) |
| Media: videos after download | Share (Mac also Open) | — | Open in the system player |
| Media: main window while video is paused | — | — | connect panel with "Live video paused while Media downloads." |
| Keep the system awake | `isIdleTimerDisabled` | `FLAG_KEEP_SCREEN_ON` | `SetThreadExecutionState` / freedesktop ScreenSaver |
| Notice without the HUD | right under the top bar (a line lower on phones) | same | a line lower always (top 80/92): in a window under ~1400 px a long notice reached the badge at the top centre |
| Media pauses the live video | — | — | the connect panel comes back with "Live video paused while Media downloads." (`videoHadPicture` cleared), not "VIDEO RECONNECTING…" over the last picture |


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
| Recentre under the gimbal manager | Pitch far down, then CENTER: a clean return | `GB_MODE` 4 clean from −56 to −87 with and without the manager's control (ground, 2026-10-27); the swing of 2026-10-15 not repeated |
| Zoom slider, combined mode | Levels above 25x | Table unverified past index 10; `zoomsweep` pending |
| Gimbal manager (PX4) | Take control, move, release | Worked in flight 2026-10-15 |
| Thermal bar | Palette names against the picture; does `C_T_ISO_M` answer (ISO button); isotherm band and gain on a real Orus L; HUD "IR sensor type" value | Same checks pending on iOS |
| Web settings | "Isotherm (payload web settings)": read, save, does the picture show it at once; "Restart camera service…" and the reconnection | Isotherm applied at once on 2026-10-19; restart not yet tried in the field |
| Target position | Chip against the payload's OSD TARGET line; Copy, Share / map link, Open in Maps, Mark point; KML in Google Earth | Pending on iOS as well |
| AI tap-to-track | Tap a detection box | Sometimes did not follow the tapped box |
| Zoom back to 1x by itself | `.tlog` with the moment | Seen once after a reconnect |
| Controllers | The operator's HOTAS and gamepad through learn mode | Works on the iPad with a Bluetooth gamepad |
| End-to-end latency | Camera-to-screen with a stopwatch in the frame | Pending on the iPad as well |
| Video holds through the Herelink | With the 5 s stall patience and UDP kept (core, 2026-11-06): how often "VIDEO FROZEN" shows, whether "VIDEO RECONNECTING…" and "Video over TCP" still appear, a `.tlog` with the detailed log and the HUD's "Frame gaps" | Android tablet 2026-11 over the sea: repeated freezes, TCP fallback, "no video packets after PLAY" (see `docs/protocol/orus-l.md`); no tlog |

### Android specifics

- Confirm with the frame-gap metric that an idle Android tablet does not show the ~410 ms
  holds (the low-latency Wi-Fi lock should prevent them); otherwise "Keep Wi-Fi awake".
- MediaCodec low-latency on the operator's tablet: "App: decode" near one frame period.
- Bind-to-network: mobile data must not steal the route to the RTSP host.

### Desktop specifics

- Windows on the Herelink's USB tethering showed the same video holds as the Android tablet
  (2026-11): check the firewall allows the app on private networks (inbound UDP), and that
  the RNDIS adapter's power management is off; then the same checklist as Android.

- Wi-Fi power save is the OS's: Windows "Wireless Adapter Settings → Power Saving Mode",
  `iw dev <if> set power_save off` on Linux. Document for operators if an idle laptop bursts.
- Hardware decoding (D3D11VA / DXVA2 / VA-API) has only run in the CI smoke test; check the
  HUD's decoder line and decode time, with `IMAGO_HWDEC=0` as the comparison.
- Drawing the decoder's GPU surface directly (ADR 0002) once latency is measured.
- HOTAS through SDL2: the mapping must survive reconnecting the stick.

## 5. Core and protocol items that reach every app

- Combined-mode zoom table (`C_V_ZM_CB_LV`) above index 10, from the `zoomsweep` run.
- Where the Herelink's RTSP output stalls (its own screen keeps going): a `.tlog` with the
  detailed log next to the Herelink's RSSI, and whether its server takes more than 3 s to send
  the first packet after PLAY (`kFirstPacketTimeoutNs`).
- Wi-Fi keep-awake: if the field test confirms it removes the iPad's holds without hurting the
  video, turn it on by default in the Herelink profile of every app.
- Restarting a payload application in flight (`MAV_CMD_USER_4` 4/4, application index
  unknown): try on the bench with `imago-cli` before exposing it.
- The pending field items in `docs/protocol/orus-l.md` (yaw flags in lock mode, lens behaviour
  on a new level mid-travel, `RC_MODE` 1).
