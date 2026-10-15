# Imago Payload Control for Android

Phones and tablets, Android 7.0 (API 24) and later, landscape only. Same features as the iOS app:
video, on-screen joystick and gamepads, camera and zoom, tracking, Eagle Eye and AI boxes,
gimbal attitude instrument, network HUD and MAVLink log, connection profiles and settings.

## Layout

| Path | Contents |
| --- | --- |
| `app/src/main/cpp/` | JNI bridge to the core, MediaCodec decoder, state serializer |
| `app/src/main/java/.../core/` | Kotlin wrapper of the core (session, state, profiles, settings catalog) |
| `app/src/main/java/.../control/` | Gimbal input shaping, gamepad mapping and input |
| `app/src/main/java/.../app/` | `AppModel` (state and actions), network binding |
| `app/src/main/java/.../ui/` | Jetpack Compose screens |

## Video path

The core's frames never reach Java. On the core's video thread each frame goes straight into a
MediaCodec input buffer (NDK `AMediaCodec`, decoder low-latency options for Android 11+,
Qualcomm, MediaTek and HiSilicon). An output thread renders each picture to a `SurfaceView`
timestamped "now"; when several are ready, only the newest is shown. With playout smoothing on,
the frames the core holds back are timestamped for their moment instead (`display_delay_ns`).

## Networking

Herelink's Wi-Fi has no internet, so Android may keep mobile data as the default network. On
connect, the app binds itself to the network whose subnet holds the payload IP (or the RTSP
host), and holds a low-latency Wi-Fi lock while connected.

## Building

Requirements: JDK 17 or later, Android SDK with platform 37, NDK 29.0.14206865 and CMake 3.31.6
(`sdkmanager "platforms;android-37.0" "ndk;29.0.14206865" "cmake;3.31.6"`).

```sh
cd android
./gradlew assembleDebug          # app/build/outputs/apk/debug/app-debug.apk
./gradlew testDebugUnitTest      # JVM unit tests
./gradlew lintDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

The CI builds the debug APK on every push (artifact `imago-payload-control-debug-apk`).

`PayloadSnapshotTest` reads `app/src/test/resources/state_fixture.bin`, written by the native
state serializer. Regenerate it with `tools/make_state_fixture.sh` after changing
`state_writer.cpp` (the CI does it on every run).

### Release signing

Create a keystore once and keep it safe (every update must be signed with the same key):

```sh
keytool -genkeypair -v -keystore imago-release.jks -alias imago -keyalg RSA -keysize 4096 -validity 10000
```

Then either add `android/keystore.properties` (not in git):

```properties
storeFile=../imago-release.jks
storePassword=...
keyAlias=imago
keyPassword=...
```

or set `IMAGO_KEYSTORE_FILE`, `IMAGO_KEYSTORE_PASSWORD`, `IMAGO_KEY_ALIAS`, `IMAGO_KEY_PASSWORD`,
and run `./gradlew assembleRelease`.

### With the simulator

The `orus-sim (emulator host)` profile reaches `tools/orus-sim` on the development computer from
the Android emulator (10.0.2.2). On a real device, use the computer's IP instead.

## Notes

- Target SDK 36: Android 17 adds a local network permission for apps targeting it.
- On Android 16 tablets the app opts out of the forced rotation for API 36 apps, since the
  operator layout is landscape only.
- Fonts are static instances of the variable Space Grotesk and Manrope (Android 7 cannot set
  the weight axis), generated with fontTools from `apple/.../Fonts`; OFL licenses in
  `app/src/main/assets/licenses/`.
