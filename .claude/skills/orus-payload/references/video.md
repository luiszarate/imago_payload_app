# Video — reception policy and field behaviour

Policy code: `core/src/video/video_receiver.cpp`, `lag_tracker.*`; RTSP/RTP client in
`core/src/rtsp/` and `core/src/rtp/` (ours; no FFmpeg/live555). Field facts:
`docs/protocol/orus-l.md` §"Wi-Fi bursts" and §"Video holds through the Herelink".
Narrative: `README.md` §"Video that falls behind live", `docs/manual-programador.md` §8.1.

## Pipeline (no queues anywhere)

core video thread → each access unit handed over **the moment its last packet arrives** (no
jitter buffer, the core never decodes) → platform decoder takes it at once → display shows it
or drops it.

| Platform | Decoder → display |
| --- | --- |
| iOS / Mac | VideoToolbox (AVCC) → `AVSampleBufferDisplayLayer` (`Video/VideoPipeline.swift`) |
| Android | frames never reach Java: `AMediaCodec` (Annex B) in `cpp/video_decoder.cpp` → `SurfaceView` |
| Desktop | FFmpeg low-delay + hwaccel → `QVideoSink` (`src/video/VideoDecoder.cpp`) |
| Quest | Android's decoder → `SurfaceTexture` → `updateTexImage` on the XR thread (newest frame) |

Apps report decoded/presented frames with `imago_report_frame_presented` for the HUD.

## Two different failures, two different remedies

| Failure | Detection | Remedy |
| --- | --- | --- |
| **Queue** (frames arrive, but late: Herelink, radio, Wi-Fi, TCP) | Each frame's arrival vs. its RTP timestamp, relative to the fastest recent frame; every frame for 2 s more than the limit behind (500 ms default, per profile) | Restart the RTSP session (drops the queue), `IMAGO_EVENT_VIDEO_RESYNC`; badge "CATCHING UP…" / "VIDEO x.x s BEHIND" |
| **Hold** (no packets at all) | No RTP for a while | Wait **5 s** before restarting (a restart whose PLAY lands during the hold fails too); keep the last picture with "VIDEO FROZEN x.x s" / "VIDEO RECONNECTING…" |

Transport AUTO falls back to TCP **only** when UDP has never delivered a packet in the
connection (two empty UDP sessions in a row). Once UDP worked, silence is the link, not a
firewall; TCP would turn every loss into retransmission and growing lag. (Field 2026-11, sea
flight: the old 2 s stall + one-empty-session fallback chained into TCP, 1 s lag, and a
restart every 10 s.)

## Herelink and Wi-Fi

- iPad Wi-Fi dozes while it transmits nothing: holds of 410-430 ms (4 beacon intervals) about
  twice a second, video included; gone while the joystick streams. Remedy option
  `wifi_keep_awake` ("Keep Wi-Fi awake (experimental)"): one zero byte every 33 ms to the
  MAVLink peer's **discard port 9**. Never send keep-alives to the RTSP server or its RTCP
  port (that stalled the Herelink's stream).
- `video_smoothing_ms` ("Smoothing"): delays each frame by the smoothing minus its lateness
  against the fastest frame of the last 2 s; for 30-100 ms clumping, not the 410 ms holds.
- Holds also appear on a PC tethered by USB to the Herelink (no Wi-Fi hop): likely the ground
  unit's RTSP output stalling while its own screen conceals radio loss *(unverified)*.
- Other players look smoother because they buffer 0.5-1.5 s; this app shows frames on arrival.
- Windows on Herelink tethering: firewall must allow the app on private networks (blocked
  inbound UDP is the one case where TCP fallback is right); disable RNDIS power saving.

## HUD metrics and badges

"Frame gaps p95 / max" is the honest burstiness figure; "behind live" is a minimum over the
last second and hides holds. Badge priority: `videoResyncing` → "CATCHING UP…";
`!videoActive && videoHadPicture` → "VIDEO RECONNECTING…"; `videoFrozenMs > 0` (app-side,
decoder's rendered counter unchanged for 1 s) → "VIDEO FROZEN x.x s"; `videoLagMs ≥ 250` →
"VIDEO x.x s BEHIND". The connect panel shows only before the first picture of a connection.

## Changing video behaviour

1. Reproduce on the bench: `orus-sim --video tools/orus-sim/testdata/bars-720p30.h264` plus
   `--lag MS [--lag-every S] [--lag-catch-up]`, `--burst N`, `--udp-hold MS`.
2. Test in `core/tests/test_video.cpp`.
3. Field finding with date into `orus-l.md`. The change reaches every app through the core;
   apps only render badges from state.
4. A decodable clip: `ffmpeg -f lavfi -i testsrc2=size=1280x720:rate=30 -t 10 -c:v libx264 -profile:v baseline -bf 0 -g 30 -x264-params slices=1 -pix_fmt yuv420p clip.h264`.
