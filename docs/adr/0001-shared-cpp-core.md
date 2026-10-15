# ADR 0001: Shared C++ core with native UI per platform

- Status: accepted (desktop decoding: see ADR 0002)
- Date: 2026-10-13

## Context

Imago Payload Control must control the Gremsy Orus L from iOS and iPadOS first, then from Android,
macOS and Windows. The top requirement is minimal latency added by the app, for both video and
gimbal control. The protocol work (MAVLink, RTSP/RTP, payload state) is the same on every
platform; video decoding and drawing are not, because each OS exposes its hardware decoder
differently (VideoToolbox, MediaCodec, Media Foundation).

## Decision

- Protocol, video transport and payload state live in a portable C++20 core (`core/`).
- The core exposes a plain C API (`imago_core.h`). Swift imports it directly; Android will use JNI;
  Windows can call it as is.
- Each platform writes only its UI, hardware video decoding and rendering.
- Video frames leave the core still compressed (H.264/H.265 access units). The core never decodes.
- We do not use the Gremsy PayloadSdk code. It is GPLv3, which is incompatible with a proprietary
  App Store app, and its send queue adds up to 20 ms per command. It is used only as a protocol
  reference; the implementation is our own, on top of the MIT-licensed MAVLink C headers.
- We do not use FFmpeg or live555 for RTSP. A small RTSP/RTP client written for this purpose avoids
  their buffering defaults and LGPL obligations, and keeps the frame path free of extra copies.

## Consequences

- One protocol implementation to test and fix for all platforms.
- Each new platform costs a UI plus a decode/render layer, nothing more.
- We own the RTSP client, so camera or server quirks must be handled by us. The Orus L is the only
  server we need to support, which keeps the scope small.
