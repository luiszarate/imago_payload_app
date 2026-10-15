# Pending work: Android and desktop apps

Status on 2026-10-14. Both apps were written against the core's C API as it stood that day, so
they already expose everything the iOS app got during the field tests of 2026-10-14: the
level-table zoom and slider, camera settings, controller mapping with learn mode, D-pad speeds,
MAVLink `.tlog` recording, others' commands and external zoom moves, the autopilot's gimbal
manager with the control button, "Keep Wi-Fi awake", "Smoothing" and the frame-gap metric.
What they lack is a field test: neither app has run against a real Orus L yet.

## Field tests to repeat on each app

Same checklist the iOS app went through; the core is shared, so the point is the platform parts
(decoder, display, controllers, networking).

| Item | What to check | iOS finding to compare with |
| --- | --- | --- |
| Bursty link while idle | HUD "Frame gaps p95 / max" with the app idle and with the joystick moving; record a `.tlog` of each | iPad: the Wi-Fi radio dozes when the app sends nothing; video and MAVLink then arrive every ~410 ms (`docs/protocol/orus-l.md`, "Wi-Fi bursts") |
| Smoothing | Does 50–200 ms remove the stutter the link shows, at that cost in latency | Only covers bursts shorter than the setting; it cannot hide ~400 ms holds |
| Keep Wi-Fi awake | Does the 1-byte discard-port datagram remove the idle bursts, without hurting the video | Not tested yet with the Herelink (the earlier RTCP version stalled the video) |
| Zoom slider, combined mode | Levels above 25x; the table is still unverified past index 10 | Combined mode: index 10 → 10x, 11 → 360x, 12 → 240x on the OSD; `zoomsweep` output pending |
| Gimbal manager (PX4) | Take control, move, release; does the Herelink dial keep working after release | Untested on iOS too |
| AI tap-to-track | Tap a detection box, does the tracker lock on it | Sometimes did not follow the tapped box |
| Zoom back to 1x by itself | `.tlog` with the moment it happens, `IMAGO_EVENT_ZOOM_CHANGED_EXTERNALLY` toast | Seen once after a reconnect, also in QGroundControl |
| Controllers | The operator's HOTAS and gamepad: every axis and button through learn mode; `stopAll` when the mapping changes | Works on the iPad with a Bluetooth gamepad |
| End-to-end latency | Camera-to-screen with a stopwatch in the frame, HUD "App:" rows | Pending on the iPad as well |

### Android specifics

- The app holds `WIFI_MODE_FULL_LOW_LATENCY` while connected, which on Android 10+ asks the
  driver to turn Wi-Fi power save off. That is the platform's own answer to the iPad's bursts:
  confirm with the frame-gap metric that an idle Android tablet does not show the ~410 ms holds.
  If it does, "Keep Wi-Fi awake" is the fallback, as on iOS.
- MediaCodec low-latency: the decoder asks for the vendor low-latency options; check on the
  operator's tablet that "App: decode" stays near one frame period and that pictures are not
  held by the decoder (compare with the iPad on the same stream).
- Bind-to-network: with Herelink's Wi-Fi (no internet) the app binds to the Wi-Fi network; check
  that mobile data on the tablet does not steal the route for the RTSP host.

### Desktop specifics

- Wi-Fi power save on laptops is the OS's business: Windows power plan "Wireless Adapter
  Settings → Power Saving Mode" and `iw dev <if> set power_save off` on Linux. Document the
  setting for operators if an idle laptop shows the same bursts; "Keep Wi-Fi awake" is available
  there as well.
- Hardware decoding paths (D3D11VA / DXVA2 on Windows, VA-API on Linux) have only run in the CI
  smoke test against `orus-sim`; check the HUD's decoder line and the decode time on the
  operator's laptop, and `IMAGO_HWDEC=0` as the comparison.
- Drawing the decoder's GPU surface directly (ADR 0002 lists the copy to memory as a later
  optimisation) once latency is measured.
- SDL2 sees HOTAS by axis, button and hat numbers: the operator's stick needs its mapping
  created once and saved; check that it survives reconnecting the device.

## Core and protocol items that reach every app

- Combined-mode zoom table (`C_V_ZM_CB_LV`) above index 10, from the `zoomsweep` run.
- Wi-Fi keep-awake: if the field test confirms it removes the iPad's holds without hurting the
  video, turn it on by default in the Herelink profile of every app.
- The pending field items in `docs/protocol/orus-l.md` (yaw flags in lock mode, lens behaviour
  on a new level mid-travel, `RC_MODE` 1).
