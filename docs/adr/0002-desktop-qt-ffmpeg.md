# ADR 0002: Desktop app in Qt Quick, video decoded with FFmpeg

- Status: accepted
- Date: 2026-10-14

## Context

After iOS and Android, the payload must be operated from Windows 10/11 laptops and from Linux.
ADR 0001 left decoding and drawing to each platform and named Media Foundation for Windows.
macOS keeps the SwiftUI app.

## Decision

- One desktop app for Windows and Linux in Qt 6 / QML, under the LGPLv3: Qt linked dynamically,
  no GPL-only modules. It uses the core's C API like the other apps.
- Video is decoded with FFmpeg's libavcodec (LGPL build, shared libraries, no GPL parts) on both
  systems, using the GPU through FFmpeg's hardware decoders: D3D11VA or DXVA2 on Windows,
  VA-API, CUDA or VDPAU on Linux, with software decoding as the fallback. RTSP and RTP stay in
  the core (ADR 0001): FFmpeg receives finished access units, in low-delay mode.
- Pictures reach the screen through Qt Multimedia's video sink and `VideoOutput`, which uploads
  and draws them with the window's own graphics API.
- Gamepads and joysticks come from SDL2 (zlib license), the only library that covers Xbox and
  PlayStation pads, generic HID joysticks and HOTAS the same way on both systems.

## Why not Media Foundation

- H.265 needs the "HEVC Video Extensions" from the Microsoft Store on most Windows installs, and
  Windows N editions have no H.264 decoder without the Media Feature Pack. FFmpeg's D3D11VA path
  uses the GPU's decoder directly and has a software decoder for both codecs.
- One decoder for Windows and Linux: what is tested on a Linux workstation against `orus-sim`
  is the code that runs on the operator's Windows laptop.

## Consequences

- The Windows package ships FFmpeg's DLLs (built by vcpkg from source, version pinned in
  `desktop/vcpkg.json`) next to Qt's, with the LGPL notices.
- Hardware decoding copies each picture back to memory once before Qt uploads it (about a
  millisecond at 1080p). Drawing the decoder's GPU surface directly would need Qt's graphics
  API to share the decoder's device, a later optimisation.
- The Linux build uses the distribution's FFmpeg; Qt's own FFmpeg (shipped for its multimedia
  backend) is never linked by the app.
