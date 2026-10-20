# Imago Payload Control

App to operate the Gremsy Orus L payload: a portable C++20 core with a C API (`core/`) and
native apps on top of it: iOS/iPadOS (`apple/`, the reference; the same app runs on macOS
through Mac Catalyst, with Mac-only code under `#if targetEnvironment(macCatalyst)`), Android
(`android/`) and Windows/Linux (`desktop/`, Qt 6). Start with `README.md` and `docs/manual-programador.md`.

## Rules for every change

1. **Manuals.** Every change that alters behaviour, UI, the core's API, the build or the
   tools updates the manuals in the same commit:
   - `docs/manual-usuario.md` (operators: what they see and do),
   - `docs/manual-programador.md` (developers: architecture, API, build, tools, tests).
   Both are written in Spanish; UI strings are quoted in English as they appear on screen.
2. **iOS first, then a list of differences.** The iOS app is the reference. When the iOS app
   changes, do **not** port the change to Android or desktop unless the user explicitly asks
   for it. Instead, record each difference in `docs/pending-android-desktop.md`, section
   "1. To bring from iOS" (iOS behaviour, iOS files, what is missing on Android, what is
   missing on desktop), and mention it in the user manual's platform differences where the
   operator would notice. When the user asks to port, bring Android and desktop up to iOS and
   remove the rows that are done; deliberate platform differences go to section 2.
3. **Protocol logic lives in the core**, never in Swift, Kotlin or QML: builders in
   `core/src/payload/orus_protocol.*`, the session in `core/src/session/`, the C API in
   `core/include/imago/imago_core.h`, a simulation in `tools/orus-sim/`, tests in
   `core/tests/`, and the facts with their source in `docs/protocol/orus-l.md` (mark what is not
   confirmed on a real Orus L as *(unverified)*).
4. **Gremsy's PayloadSdk is GPLv3: never copy its code.** Use it only to learn the protocol and
   describe it in our own words.
5. Hot paths (video frame → decoder → screen, joystick/gamepad → UDP) never wait for the main
   thread and never queue.

## Conventions

- Talk to the user in Spanish. App UI text, code and code comments in English.
- Match the surrounding code's idioms and comment density; comments explain why, citing field
  tests with their date.
- `git fetch` before pushing: the user also pushes to `main`. Commit messages end with the
  attribution lines the session provides.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ctest --test-dir build
cd android && ./gradlew assembleDebug testDebugUnitTest lintDebug
cmake -S desktop -B desktop/build -DCMAKE_PREFIX_PATH=~/Qt/6.10.3/gcc_64 && cmake --build desktop/build -j && ctest --test-dir desktop/build
```

The iOS and Mac app cannot be built on Linux: the CI job "iOS and Mac app (build, unsigned)" is
the check.
CI status is public: `https://api.github.com/repos/luiszarate/imago_payload_app/actions/runs?head_sha=<sha>`.
