#!/usr/bin/env bash
# check_dll.sh <addon .dll>: PE32+ (x64), Kodi's add-on entry points exported, only Windows' own
# system DLLs imported (the MinGW runtime is linked statically; Windows has no copy of it), and the
# C runtime is UCRT like Kodi's (never msvcrt.dll: Kodi frees memory the add-on allocated).
set -euo pipefail
dll=$1
dump=$(x86_64-w64-mingw32ucrt-objdump -p "$dll" 2>/dev/null || objdump -p "$dll")
grep -q "^Magic.*(PE32+)" <<<"$dump" || { echo "FAIL $dll is not a PE32+ (x64) DLL"; exit 1; }
for sym in ADDON_Create ADDON_GetTypeVersion ADDON_GetTypeMinVersion; do
  grep -q " $sym\$" <<<"$dump" || { echo "FAIL $dll does not export $sym"; exit 1; }
done
imports=$(sed -n 's/.*DLL Name: //p' <<<"$dump" | tr '\n' ' ')
for d in $imports; do
  case "${d,,}" in kernel32.dll|api-ms-win-crt-*.dll|ucrtbase.dll|ws2_32.dll|advapi32.dll|user32.dll|shell32.dll|ole32.dll|bcrypt.dll) ;;
    msvcrt.dll) echo "FAIL $dll uses msvcrt.dll; Kodi uses UCRT (heap mismatch crashes Kodi)"; exit 1 ;;
    *) echo "FAIL $dll imports $d (not a Windows system DLL)"; exit 1 ;;
  esac
done
echo "ok   windows-x86_64: PE32+, exports ADDON_*, imports: $imports($(stat -c %s "$dll") bytes)"
