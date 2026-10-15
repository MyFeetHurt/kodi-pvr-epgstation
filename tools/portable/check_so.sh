#!/usr/bin/env bash
# check_so.sh <addon .so> <x86_64|aarch64>: architecture, symbol versions, libraries, dlopen.
set -euo pipefail
so=$1 arch=$2
# What the README promises: glibc 2.34+ (Ubuntu 22.04 has glibc 2.35, libstdc++ 3.4.30). The
# Debian 12 build image could link newer symbols (2.36) without this failing otherwise.
MAX_GLIBC=2.34 MAX_GLIBCXX=3.4.30
machine=$(readelf -h "$so" | sed -n 's/ *Machine: *//p')
case "$arch" in
  x86_64) [[ $machine == *X86-64* ]] ;;
  aarch64) [[ $machine == *AArch64* ]] ;;
esac || { echo "FAIL $so is $machine, not $arch"; exit 1; }

newest() { objdump -T "$so" | grep -o "$1_[0-9.]*" | sed "s/$1_//" | sort -V | tail -1; }
glibc=$(newest GLIBC) glibcxx=$(newest GLIBCXX)
for pair in "GLIBC $glibc $MAX_GLIBC" "GLIBCXX $glibcxx $MAX_GLIBCXX"; do
  set -- $pair
  if [ "$(printf '%s\n%s\n' "$2" "$3" | sort -V | tail -1)" != "$3" ]; then
    echo "FAIL $so needs $1 $2 (> $3)"; exit 1
  fi
done
libs=$(readelf -d "$so" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | tr '\n' ' ')
for l in $libs; do
  case "$l" in libc.so.*|libm.so.*|libstdc++.so.*|libgcc_s.so.*) ;;
    *) echo "FAIL $so needs $l"; exit 1 ;;
  esac
done

# dlopen in a plain Debian 12 of that architecture: every library and symbol version resolves.
platform=linux/amd64; [ "$arch" = aarch64 ] && platform=linux/arm64
dir=$(cd "$(dirname "$so")" && pwd)
podman run --rm --platform "$platform" -v "$dir:/addon:ro,Z" docker.io/library/python:3.11-slim-bookworm \
  python3 -c "import ctypes,glob; l=ctypes.CDLL(glob.glob('/addon/pvr.epgstation.so.*')[0]); print('loaded, ADDON_Create at', hex(ctypes.cast(l.ADDON_Create, ctypes.c_void_p).value))" \
  | sed 's/^/  /'
echo "ok   $arch: $machine, GLIBC <= $glibc, GLIBCXX <= $glibcxx, needs: $libs"
