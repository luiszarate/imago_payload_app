# Third-party software in Imago Payload Control (Windows and Linux)

Imago Payload Control is © Imago Aerospace. It uses the following libraries, each under its own
license. The license texts are in this folder.

| Component | License | Use | Source |
|---|---|---|---|
| Qt 6 (Core, Gui, Qml, Quick, Quick Controls, Multimedia) | LGPL-3.0 (`LGPL-3.0.txt`, `GPL-3.0.txt`) | Window, UI, video display | https://download.qt.io/official_releases/qt/ |
| FFmpeg libavcodec, libavutil | LGPL-2.1 or later (`LGPL-2.1.txt`) | Video decoding | https://ffmpeg.org/download.html |
| SDL 2 | zlib (`zlib-SDL2.txt`) | Gamepads and joysticks | https://www.libsdl.org |
| MAVLink C headers | MIT | Protocol messages | https://github.com/mavlink/c_library_v2 |
| Space Grotesk, Manrope | SIL Open Font License 1.1 (`OFL-*.txt`) | Fonts | https://fonts.google.com |
| Material Icons | Apache-2.0 (`LICENSE-MaterialIcons.txt`) | Icons | https://github.com/google/material-design-icons |

Qt and FFmpeg are linked dynamically: their libraries (`Qt6*.dll`, `avcodec-*.dll`, `avutil-*.dll` on
Windows; shared libraries on Linux) sit next to the application and can be replaced with other
builds of the same version. On Windows, FFmpeg is built from its sources by vcpkg
(https://github.com/microsoft/vcpkg, port `ffmpeg`, LGPL configuration, without the `gpl` or
`nonfree` features); the exact version is set in `desktop/vcpkg.json` of the application's
source repository. Qt Multimedia may also bring its own FFmpeg build (LGPL), shipped by Qt.

The application does not use the Gremsy PayloadSdk.
