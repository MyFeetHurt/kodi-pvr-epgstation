#!/usr/bin/env bash
# check_android.sh <lib.so> <arm64-v8a|armeabi-v7a|x86>: right machine, Kodi's add-on entry points
# exported, only Android system libraries needed (the C++ runtime is linked statically).
set -euo pipefail
so=$1 abi=$2
re=/opt/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf
machine=$($re -h "$so" | sed -n 's/ *Machine: *//p')
case "$abi" in
  arm64-v8a) [[ $machine == *AArch64* ]] ;;
  armeabi-v7a) [[ $machine == *ARM* && $machine != *AArch64* ]] ;;
  x86) [[ $machine == *80386* ]] ;;
esac || { echo "FAIL $so is $machine, not $abi"; exit 1; }
syms=$($re --dyn-syms "$so")
for sym in ADDON_Create ADDON_GetTypeVersion ADDON_GetTypeMinVersion; do
  grep -q " $sym\$" <<<"$syms" || { echo "FAIL $so does not export $sym"; exit 1; }
done
needed=$($re -d "$so" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | tr '\n' ' ')
for l in $needed; do
  case "$l" in libc.so|libm.so|libdl.so|liblog.so|libandroid.so) ;;
    *) echo "FAIL $so needs $l (not an Android system library)"; exit 1 ;;
  esac
done
echo "ok   $abi: $machine, exports ADDON_*, needs: $needed($(stat -c %s "$so") bytes)"
