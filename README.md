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
| `tools/orus-sim/` | Orus L simulator for development without hardware |
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

## Running against the simulator

```sh
./build/tools/orus-sim/orus-sim            # MAVLink on UDP 14566
./build/tools/imago-cli/imago-cli 127.0.0.1  # connects, prints state once per second
```

For a test video stream without the camera, serve a test pattern over RTSP with
[mediamtx](https://github.com/bluenviron/mediamtx) and ffmpeg (see `tools/orus-sim/README.md`).

## Rules for the core

1. No platform UI code inside `core/`. Sockets, threads and clocks go through `core/src/platform/`.
2. The public API (`core/include/imago/imago_core.h`) is plain C: no exceptions, no C++ types.
3. Hot paths never block: video frames are handed over without queues, control commands are sent
   on the caller's thread the moment they are issued.

## License

Proprietary. Copyright (c) 2026 Imago Aerospace. All rights reserved. See [`LICENSE`](LICENSE).
Third-party components keep their own licenses (see `core/third_party/*/README.md`).
