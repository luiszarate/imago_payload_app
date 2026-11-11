#!/usr/bin/env bash
# Which parts of the repository a push touched, as GitHub outputs (area=true|false), so CI
# builds only what could have changed:
#
#   .github/scripts/changed-areas.sh <base commit>
#
# The core is shared: a change to it (or to the workflow itself) counts for every area.
# With no usable base (a new branch, a forced push, a manual run) everything counts.
set -euo pipefail

base="${1:-}"
if [ -n "$base" ] && ! [[ "$base" =~ ^0+$ ]] && git cat-file -e "$base^{commit}" 2>/dev/null; then
    files=$(git diff --name-only "$base" HEAD)
else
    files=ALL
fi

touched() { # a regular expression over the changed paths
    [ "$files" = ALL ] || grep -qE "$1" <<<"$files"
}

area() { # name, expression
    if touched "$2"; then echo "$1=true"; else echo "$1=false"; fi
}

shared='^(core/|CMakeLists\.txt$|\.github/)'
area core "$shared|^(tools/|Package\.swift$|apple/Tests/)"
area apple "$shared|^(apple/|Package\.swift$|tools/orus-sim/)"
area android "$shared|^android/"
# The Quest app compiles the Android app's decoder and JNI helpers.
area quest "$shared|^(quest/|android/app/src/main/cpp/)"
# The desktop jobs build the tools for their smoke test and ship the fonts' licences.
area desktop "$shared|^(desktop/|tools/|android/app/src/main/assets/licenses/)"
