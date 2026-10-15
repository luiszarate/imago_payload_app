# Imago Payload Control for Windows and Linux

Windows 10 and 11 (x64) and Linux, in Qt 6 / QML over the shared C++ core. Same features as the
iOS and Android apps: video, on-screen joystick, gamepads and HOTAS, camera and zoom, tracking,
Eagle Eye and AI boxes, gimbal attitude instrument, network HUD and MAVLink log, connection
profiles and settings. On the desktop the mouse wheel over the video zooms, F11 toggles full
screen and Esc closes a page.

## Layout

| Path | Contents |
| --- | --- |
| `src/core/` | C++ wrapper of the core: session, state snapshot for QML, profiles, settings catalog |
| `src/control/` | Gimbal input shaping, controller mapping, controllers through SDL2 |
| `src/video/` | FFmpeg decoder feeding a Qt video sink |
| `src/app/` | `AppModel` (the `App` QML singleton: state and actions), OS integration |
| `qml/` | The screens: operator view, settings, connections |
| `tests/` | Logic tests (doctest) |
| `vcpkg.json` | Windows dependencies: FFmpeg (LGPL, libavcodec only) and SDL2 |

## Video path

Each compressed frame is copied off the core's video thread into a queue and decoded on the
decoder's own thread with libavcodec in low-delay mode (one picture out for each frame in, slice
threads only). The GPU decodes when it can: D3D11VA (then DXVA2, CUDA) on Windows, VA-API (then
CUDA, VDPAU) on Linux, with software decoding as the fallback. The picture is copied back to
memory once and handed to the window's `VideoOutput` through a `QVideoSink`, which uploads and
draws it on the next refresh; a picture replaced before that refresh is counted as skipped. With
playout smoothing, frames the core holds back wait on a presenter thread until their moment.

The HUD shows which decoder runs. `IMAGO_HWDEC=0` forces software decoding, and
`IMAGO_FFMPEG_LOG=1` prints FFmpeg's warnings.

On Windows the app asks for a 1 ms timer resolution, so the core's timers and the presenter wake
on time, and keeps the display on while connected (on Linux through the freedesktop screen
saver interface).

## Controllers

SDL2 sees Xbox, PlayStation and most Bluetooth or USB gamepads as gamepads, with named sticks
and buttons; any other joystick (HOTAS, flight sticks, pedals) by axis, button and hat numbers.
Every input can be assigned in Settings › Controller. Controllers keep working while another
window (a map, a ground station) has the focus.

## Building

Requirements: CMake 3.22+, a C++20 compiler, Qt 6.8 or later with Qt Multimedia (tested with
6.10.3), FFmpeg's libavcodec and libavutil, SDL2.

### Linux

```sh
sudo apt install libavcodec-dev libavutil-dev libsdl2-dev libgl1-mesa-dev
cmake -S desktop -B desktop/build -DCMAKE_PREFIX_PATH=~/Qt/6.10.3/gcc_64
cmake --build desktop/build -j
ctest --test-dir desktop/build
./desktop/build/ImagoPayloadControl
```

FFmpeg comes from pkg-config: the distribution's packages, not the libraries Qt ships for its
own multimedia backend.

### Windows

Visual Studio 2022 (or later) with the C++ workload, Qt for MSVC 2022 x64 and vcpkg:

```bat
cmake -S desktop -B desktop\build ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.10.3\msvc2022_64 ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DFFMPEG_ROOT=%CD%\desktop\build\vcpkg_installed\x64-windows
cmake --build desktop\build --config Release
C:\Qt\6.10.3\msvc2022_64\bin\windeployqt --release --qmldir desktop\qml desktop\build\Release\ImagoPayloadControl.exe
```

vcpkg builds FFmpeg from source the first time (several minutes). The CI builds, tests and
packages the app on every push: artifact `imago-payload-control-windows-x64`, a folder to unzip
and run (`ImagoPayloadControl.exe`), with the third-party licenses in `licenses/`.

## Development aids

```sh
./desktop/build/ImagoPayloadControl --profile "orus-sim on this computer" --connect \
    --open hud --delay 6000 --screenshot shot.png
```

saves the window and the decoded picture (`shot-video.png`) and quits, and prints whether the
link and the video came up. Against `orus-sim --video tools/orus-sim/testdata/bars-720p30.h264`
this is the CI's smoke test on both platforms. `QT_QPA_PLATFORM=offscreen` runs without a
display, but Qt Quick then draws in software, without the video layer.

`--view <view>` switches the camera once the payload is up (`eo-ir`, `eo`, `ir`, `ir-eo`, `sync`
or `side-by-side`); `--view ir` shows the thermal bar, and with `orus-sim --no-isotherm` the bar
without its isotherm button.

Settings live in `~/.config/Imago Aerospace/Imago Payload Control.ini` (Linux) and
`%APPDATA%\Imago Aerospace\Imago Payload Control.ini` (Windows); MAVLink logs in the app's data
folder (`Logs/`, opened from the HUD).

## Licenses

Qt (LGPLv3) and FFmpeg (LGPLv2.1+, built without GPL parts) are linked dynamically, and no
GPL-only Qt module is used. See `resources/licenses/THIRD_PARTY_NOTICES.md`, shipped with the
Windows package.
