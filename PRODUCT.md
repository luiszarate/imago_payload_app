# Product

<!-- impeccable:product-schema 1 -->

## Platform

adaptive

## Users

Payload operators at Imago Aerospace and its customers who fly the Gremsy Orus L gimbal
camera on fixed-wing VTOL and multirotor aircraft. In the field they hold a tablet with both
thumbs at the sides (controls stay at the screen edges) or sit at a laptop or PC in a ground
station, often outdoors and under time pressure, while a separate pilot flies the aircraft.
After the flight the same operators review and pull the captured photos and videos off the
payload, mostly from a desktop or laptop, less from a tablet, rarely from a phone.

## Product Purpose

Operate the Orus L from one app on iPad, iPhone, Mac, Android, Windows and Linux: live video
with the lowest possible app-added latency, gimbal, EO zoom and thermal camera, tracking and
AI detection, and the payload's stored media. Success is an operator who trusts what they see
on screen and never loses a target or a capture because of the app.

## Positioning

A replacement for Gremsy's Android-only Payload Plus with feature parity, built on one
portable C++ core that measures and minimises latency, works through Herelink and other IP
datalinks, and runs on every platform the customer's ground station uses.

## Operating Context

- Connection through a Herelink ground unit (Wi-Fi or Ethernet bridge) or another IP
  datalink: payload IP for control (MAVLink UDP), an RTSP URL for video, the payload's web
  server on port 8000 for settings and media files. Bandwidth is a few Mbit/s and shared with
  the live video.
- Physical gamepads and HOTAS drive the gimbal; on touch devices, a virtual joystick.
- Field tests are logged as `.tlog` files and dated in the protocol notes.

## Capabilities and Constraints

- Portable C++20 core with a C API; native UIs: SwiftUI (iOS/iPadOS, Mac Catalyst), Jetpack
  Compose (Android 7.0+, landscape), Qt 6 / QML (Windows 10/11, Linux).
- iOS is the reference app; Android and desktop follow it, differences are listed in
  `docs/pending-android-desktop.md`.
- Protocol logic lives only in the core. Video and gimbal hot paths never wait for the main
  thread.
- App UI text is English; manuals are Spanish.
- Gremsy's PayloadSdk is GPLv3: used only to learn the protocol, never copied.

## Brand Commitments

Imago Aerospace identity (www.imago.aero): near-black ground, magenta to purple accent,
Space Grotesk for display text and figures, Manrope for body text. The operating screen must
not get cluttered (user, 2026-10-16).

## Evidence on Hand

Real Orus L in the user's hands (firmware 3.0.5, G1 thermal core), field logs and protocol
notes in `docs/protocol/orus-l.md`. No customer testimonials or benchmarks to cite.

## Product Principles

1. Latency and reliability first: nothing the app adds may slow the picture or the gimbal.
2. The picture is the interface: controls stay at the edges and out of the payload's OSD.
3. One core, many native apps: each platform feels native, the behaviour is the same.
4. Say what is unverified: facts not confirmed on a real Orus L are marked as such.

## Accessibility & Inclusion

Outdoor use in sunlight and gloves: large touch targets, high contrast on the dark ground.
