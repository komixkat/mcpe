#!/usr/bin/env bash
# Build the vendored launcher mods into loadable .so files.
#
# Usage:
#   ANDROID_NDK_HOME=/path/to/ndk ./build-mods.sh [abi ...]
#
# ABIs default to every ABI the launcher ships: x86_64 and arm64-v8a.
# Output: out/<abi>/lib*.so
#
# The Android NDK is required because the mods are loaded into the game's
# Android process and link against the Android log library (and, for shulker,
# the Android C++ runtime layout). The launcher provides those symbols at
# runtime, but the code has to be compiled for the Android target triples.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/out"

NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
if [[ -z "${NDK}" ]]; then
    for candidate in \
        /home/lua/mcpe-dev/android-ndk/android-ndk-r27c \
        "$HOME/android-ndk"/android-ndk-* \
        /opt/android-ndk*; do
        if [[ -f "$candidate/build/cmake/android.toolchain.cmake" ]]; then
            NDK="$candidate"
            break
        fi
    done
fi
if [[ -z "${NDK:-}" || ! -f "$NDK/build/cmake/android.toolchain.cmake" ]]; then
    echo "Android NDK not found. Set ANDROID_NDK_HOME to an NDK r26/r27 install." >&2
    exit 1
fi
TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"

if [[ $# -gt 0 ]]; then
    ABIS=("$@")
else
    ABIS=(x86_64 arm64-v8a)
fi

MODS=(fullbright snaplook zoom shulkerpreview discordrpc)

for abi in "${ABIS[@]}"; do
    echo "==== building mods for $abi ===="
    mkdir -p "$OUT/$abi"
    for mod in "${MODS[@]}"; do
        src="$HERE/$mod"
        [[ -d "$src" ]] || { echo "  skip: $mod (not vendored)"; continue; }
        build="$src/build-$abi"
        echo "---- $mod ($abi) ----"
        cmake -S "$src" -B "$build" \
            -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
            -DANDROID_PLATFORM=21 \
            -DANDROID_ABI="$abi" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
            -Wno-dev >/dev/null
        cmake --build "$build" --parallel "$(nproc)" >/dev/null

        so="$(find "$build" -maxdepth 3 -type f -name 'lib*.so' | head -1)"
        if [[ -z "$so" ]]; then
            echo "  ERROR: no .so produced for $mod ($abi)" >&2
            exit 1
        fi
        dest="$OUT/$abi/$(basename "$so")"
        cp -f "$so" "$dest"
        strip_bin="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"
        [[ -x "$strip_bin" ]] && "$strip_bin" --strip-unneeded "$dest" || true
        echo "  -> out/$abi/$(basename "$so")"
    done
done

echo
echo "Built mods:"
find "$OUT" -type f -name '*.so' -printf '  %p (%s bytes)\n' | sort
