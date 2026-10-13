# Cross toolchain: MinGW-w64 targeting UCRT (posix threads, for std::thread) building the Windows
# x64 DLL; image tools/portable/Containerfile.windows. UCRT because Kodi (MSVC) uses it and frees
# memory the add-on allocated. The GCC runtime (libstdc++, libgcc, winpthread) is linked
# statically: Windows has no copy of it, so the DLL must only import Windows' own DLLs
# (KERNEL32 and the UCRT api-ms-win-crt-* sets; tools/portable/check_dll.sh).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32ucrt-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32ucrt-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32ucrt-windres)
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++ -s")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
