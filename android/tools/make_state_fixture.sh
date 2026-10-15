#!/usr/bin/env bash
# Regenerates app/src/test/resources/state_fixture.bin with the host compiler, from the same
# state_writer.cpp the app uses. CI runs it before the unit tests, so PayloadSnapshotTest checks
# the Kotlin reader against the current native writer.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
root="$(cd "$here/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -I "$root/core/include" \
    "$here/app/src/test/cpp/make_state_fixture.cpp" "$here/app/src/main/cpp/state_writer.cpp" \
    -o "$build/make_state_fixture"
"$build/make_state_fixture" "$here/app/src/test/resources/state_fixture.bin"
echo "wrote app/src/test/resources/state_fixture.bin"
