#!/usr/bin/env bash
# Portable builds (on the dev VM):
#   tools/portable/build.sh [x86_64|aarch64|windows-x86_64|android-aarch64|android-armv7|android-i686 ...]
# -> build/portable/<arch>/pvr.epgstation, built in a Debian 12 container (podman, rootless).
# Linux: checks the .so: right architecture, needs nothing newer than glibc 2.36 / GLIBCXX 3.4.30
# (Debian 12), no libraries beyond libc/libstdc++/libm/libgcc, and loads (dlopen) in a Debian 12
# container of that architecture (aarch64 under qemu). Windows: MinGW-w64 (UCRT, Debian 13 image) against
# tools/portable/kodi-windows (Kodi + nlohmann headers only under /sdk), checks the DLL's imports.
# Android: NDK r27c (Containerfile.android), kodi-android config, check_android.sh.
set -euo pipefail
cd "$(dirname "$0")/../.."
ARCHES=${*:-x86_64 aarch64 windows-x86_64 android-aarch64 android-armv7 android-i686}
IMAGE=pvr-epgstation-build:debian12
WIN_IMAGE=pvr-epgstation-build:windows-ucrt
ANDROID_IMAGE=pvr-epgstation-build:android
podman build -q -t "$IMAGE" -f tools/portable/Containerfile tools/portable >/dev/null

KODI_MOUNTS=(-v /usr/include/kodi:/usr/include/kodi:ro
             -v /usr/share/kodi/cmake:/usr/share/kodi/cmake:ro
             -v /usr/lib/x86_64-linux-gnu/kodi/cmake:/kodi-cmake:ro)

for arch in $ARCHES; do
  out=build/portable/$arch
  if [[ $arch == android-* ]]; then
    # Google's NDK (Containerfile.android); Kodi 21 APKs: arm64-v8a, armeabi-v7a, x86; minimum API 21.
    case "$arch" in android-aarch64) abi=arm64-v8a ;; android-armv7) abi=armeabi-v7a ;; android-i686) abi=x86 ;; esac
    podman build -q -t "$ANDROID_IMAGE" -f tools/portable/Containerfile.android tools/portable >/dev/null
    rm -rf "$out"
    podman run --rm -v "$PWD:/src:Z" -w /src \
      -v /usr/include/kodi:/sdk/include/kodi:ro -v /usr/include/nlohmann:/sdk/include/nlohmann:ro \
      -v /usr/share/kodi/cmake:/sdk/share/kodi/cmake:ro \
      -e OUT="$out" -e KODI_CMAKE=/src/tools/portable/kodi-android \
      -e CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=/opt/ndk/build/cmake/android.toolchain.cmake -DANDROID_ABI=$abi -DANDROID_PLATFORM=android-21 -DANDROID_STL=c++_static -DCMAKE_SHARED_LINKER_FLAGS=-s -DNLOHMANN_JSON_INCLUDE_DIR=/sdk/include" \
      "$ANDROID_IMAGE" tools/build.sh
    podman run --rm -v "$PWD:/src:Z" -w /src "$ANDROID_IMAGE" tools/portable/check_android.sh "$out/pvr.epgstation/libpvr.epgstation.so" "$abi"
    continue
  fi
  if [ "$arch" = windows-x86_64 ]; then
    podman build -q -t "$WIN_IMAGE" -f tools/portable/Containerfile.windows tools/portable >/dev/null
    rm -rf "$out"
    podman run --rm -v "$PWD:/src:Z" -w /src \
      -v /usr/include/kodi:/sdk/include/kodi:ro -v /usr/include/nlohmann:/sdk/include/nlohmann:ro \
      -v /usr/share/kodi/cmake:/sdk/share/kodi/cmake:ro \
      -e OUT="$out" -e KODI_CMAKE=/src/tools/portable/kodi-windows \
      -e CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=/src/tools/portable/windows-x86_64.cmake -DNLOHMANN_JSON_INCLUDE_DIR=/sdk/include" \
      "$WIN_IMAGE" tools/build.sh
    podman run --rm -v "$PWD:/src:Z" -w /src "$WIN_IMAGE" tools/portable/check_dll.sh "$out/pvr.epgstation/pvr.epgstation.dll"
    continue
  fi
  # Release: these are the builds that get published (no debug info: ~14 MB -> ~1 MB).
  args="-DCMAKE_BUILD_TYPE=Release"
  [ "$arch" = aarch64 ] && args="$args -DCMAKE_TOOLCHAIN_FILE=/src/tools/portable/aarch64.cmake"
  rm -rf "$out"
  podman run --rm -v "$PWD:/src:Z" "${KODI_MOUNTS[@]}" -w /src \
    -e OUT="$out" -e KODI_CMAKE=/kodi-cmake -e CMAKE_ARGS="$args" \
    "$IMAGE" tools/build.sh
  so=$(ls "$out"/pvr.epgstation/pvr.epgstation.so.*)
  tools/portable/check_so.sh "$so" "$arch"
done
