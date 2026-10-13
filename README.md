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

## Tools

`orus-sim` stands in for the payload: MAVLink on UDP 14566 and an RTSP server on 8554.

```sh
./build/tools/orus-sim/orus-sim                       # synthetic video (not decodable)
./build/tools/orus-sim/orus-sim --video clip.h264     # real H.264 for the app's player
./build/tools/orus-sim/orus-sim --push 127.0.0.1:14550  # behaves like a Herelink feed
```

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

Type `rate 10 0`, `rate 0 0`, `photo`, `track 0.5 0.5`, `detect 1`, `view 2`... (full list at the
top of `tools/imago-cli/main.cpp`). `--dump` writes the received video as playable Annex B.

## Rules for the core

1. No platform UI code inside `core/`. Sockets, threads and clocks go through `core/src/platform/`.
2. The public API (`core/include/imago/imago_core.h`) is plain C: no exceptions, no C++ types.
3. Hot paths never block: video frames are handed over without queues, control commands are sent
   on the caller's thread the moment they are issued.

## License

Proprietary. Copyright (c) 2026 Imago Aerospace. All rights reserved. See [`LICENSE`](LICENSE).
Third-party components keep their own licenses (see `core/third_party/*/README.md`).
