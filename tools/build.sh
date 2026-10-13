#!/usr/bin/env bash
# Build pvr.epgstation for this host's Kodi and assemble a ready-to-copy addon folder:
#   build/pvr.epgstation/  (addon.xml, resources/, the .so)
# Needs kodi-addons-dev (Kodi headers + KodiConfig.cmake) and nlohmann-json3-dev.
# OUT (default build) and CMAKE_ARGS (e.g. a cross toolchain) let tools/portable/build.sh reuse it.
set -euo pipefail
cd "$(dirname "$0")/.."
KODI_CMAKE=${KODI_CMAKE:-/usr/lib/$(gcc -dumpmachine)/kodi/cmake}
OUT=${OUT:-build}

# Without OVERRIDE_PATHS=1 KodiConfig.cmake forces Kodi's own prefix (/usr) and ignores ours.
# shellcheck disable=SC2086 # CMAKE_ARGS is a list
cmake -S . -B "$OUT/addon" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DKodi_DIR="$KODI_CMAKE" -DOVERRIDE_PATHS=1 -DCMAKE_INSTALL_PREFIX="$PWD/$OUT/stage" \
  ${CMAKE_ARGS:-} >/dev/null
cmake --build "$OUT/addon"
rm -rf "$OUT/stage" "$OUT/pvr.epgstation"
[ -f "$OUT/addon/pvr.epgstation.dll" ] || cmake --install "$OUT/addon" >/dev/null

if [ -f "$OUT/addon/pvr.epgstation.dll" ]; then
  # Windows (cross build): AddonHelpers installs a DLL only when packaging zips; assemble the
  # folder from the sources, the generated addon.xml and the DLL.
  mkdir -p "$OUT/pvr.epgstation"
  cp -r pvr.epgstation/. "$OUT/pvr.epgstation/"
  rm "$OUT/pvr.epgstation/addon.xml.in"
  cp "$OUT/addon/pvr.epgstation/addon.xml" "$OUT/pvr.epgstation/addon.xml"
  cp "$OUT/addon/pvr.epgstation.dll" "$OUT/pvr.epgstation/"
  echo "built $OUT/pvr.epgstation (pvr.epgstation.dll)"
  exit 0
fi

# AddonHelpers splits share/ and lib/; a user-dir addon needs both in one folder.
lib=$(sed -n 's/.*library_[a-z]*="\([^"]*\)".*/\1/p' "$OUT/stage/share/kodi/addons/pvr.epgstation/addon.xml")
cp -r "$OUT/stage/share/kodi/addons/pvr.epgstation" "$OUT/"
cp -L "$OUT/stage/lib/kodi/addons/pvr.epgstation/$lib" "$OUT/pvr.epgstation/"
echo "built $OUT/pvr.epgstation ($lib)"
