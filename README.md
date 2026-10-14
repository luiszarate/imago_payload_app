# Imago Payload Control

Low-latency iPad and iPhone app to operate the **Gremsy Orus L** payload, with feature parity with
Gremsy's Android app *Payload Plus*. Built by Imago Aerospace.

- Design document (Spanish): https://claude.ai/code/artifact/3bef4f57-a422-4fd7-a8d1-2f8c278a3712
- Architecture decisions: [`docs/adr/`](docs/adr/)
- Orus L protocol notes: [`docs/protocol/orus-l.md`](docs/protocol/orus-l.md)

## Layout

| Path | Contents |
| --- | --- |
| `core/` | Portable C++20 core: plain C API, MAVLink link, RTSP/RTP client, payload state, metrics |
| `core/tests/` | Unit tests (doctest), run on Linux and macOS |
| `core/third_party/` | Vendored MAVLink C headers (common dialect) and doctest |
| `tools/orus-sim/` | Orus L simulator (MAVLink + RTSP) for development without hardware |
| `tools/imago-cli/` | Terminal client of the C API, for testing against the simulator or a real Orus L |
| `apple/` | SwiftUI app for iOS, iPadOS and macOS (phase 1) |
| `docs/` | ADRs and protocol notes |

`android/` and `windows/` will be added in phase 4 on top of the same core.

## Building the core

Requirements: CMake 3.22+, a C++20 compiler (GCC 11+, Clang 14+, Apple Clang 15+).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Sanitizer builds: add `-DIMAGO_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug`.

## Apple

`Package.swift` at the repository root exposes the core to Swift as the `ImagoCore` module
(`import ImagoCore`, plain C API). `swift test` runs the Swift smoke tests on macOS.

The iOS / iPadOS app lives in `apple/`; see [`apple/README.md`](apple/README.md) to build and run it.

## Tools

`orus-sim` stands in for the payload: MAVLink on UDP 14566 and an RTSP server on 8554.

```sh
./build/tools/orus-sim/orus-sim                       # synthetic video (not decodable)
./build/tools/orus-sim/orus-sim --video clip.h264     # real H.264 for the app's player
./build/tools/orus-sim/orus-sim --push 127.0.0.1:14550  # behaves like a Herelink feed
./build/tools/orus-sim/orus-sim --lag 800                # every 20 s the stream falls 0.8 s behind
./build/tools/orus-sim/orus-sim --lag 800 --lag-catch-up # a hiccup that catches up by itself
```

`--heading 30` sets the aircraft heading the simulated gimbal reports. `--autopilot` adds a PX4-like
autopilot whose gimbal manager only moves the gimbal for the component in primary control.

Make a decodable clip with ffmpeg:

```sh
ffmpeg -f lavfi -i testsrc2=size=1280x720:rate=30 -t 10 -c:v libx264 -profile:v baseline \
       -bf 0 -g 30 -x264-params slices=1 -pix_fmt yuv420p clip.h264
```

`imago-cli` drives a session from the terminal through the public C API only. It works with the
simulator and with a real Orus L, and prints link, gimbal, tracking and video metrics every second.

```sh
./build/tools/imago-cli/imago-cli 192.168.55.1                       # direct to the Orus L
./build/tools/imago-cli/imago-cli - --rtsp rtsp://192.168.43.1:8554/fpv_stream   # via Herelink
./build/tools/imago-cli/imago-cli 127.0.0.1 --local-port 14567 --dump out.h264    # simulator
```

Type `rate 10 0`, `rate 0 0`, `photo`, `track 0.5 0.5`, `detect 1`, `view 2`, `get C_V_FM`,
`zoomrange 50`... (full list at the top of `tools/imago-cli/main.cpp`). `--dump` writes the
received video as playable Annex B; `--tlog out.tlog` records all MAVLink in and out.

## Video that falls behind live

Players that pace frames by their timestamps keep any delay they ever pick up: after a stall
the backlog plays at normal speed and the picture stays behind for good. Here nothing waits for
its turn. The core hands each frame over as soon as its last packet arrives (no jitter buffer),
VideoToolbox decodes it right away, and the display layer shows it immediately or skips it. A
burst after a hiccup is shown at once and the picture is live again.

A queue outside the app (Herelink, the radio, Wi-Fi, a TCP connection) can still hold frames
back. The core measures it: each frame's arrival against its RTP timestamp, relative to the
fastest frame seen recently. When every frame for 2 s is more than the limit behind (500 ms by
default, per connection profile in the app), it restarts the RTSP session, which drops that
queue, and reports `IMAGO_EVENT_VIDEO_RESYNC`. The app labels the picture as stale while it is
behind and says so when it keeps happening, which means the link cannot carry the bitrate.

## Rules for the core

1. No platform UI code inside `core/`. Sockets, threads and clocks go through `core/src/platform/`.
2. The public API (`core/include/imago/imago_core.h`) is plain C: no exceptions, no C++ types.
3. Hot paths never block: video frames are handed over without queues, control commands are sent
   on the caller's thread the moment they are issued.

## License

Proprietary. Copyright (c) 2026 Imago Aerospace. All rights reserved. See [`LICENSE`](LICENSE).
Third-party components keep their own licenses (see `core/third_party/*/README.md`).
