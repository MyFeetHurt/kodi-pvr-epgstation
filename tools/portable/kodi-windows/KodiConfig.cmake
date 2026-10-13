# Kodi 21 add-on SDK config for cross-building the Windows x64 DLL with MinGW-w64.
# The distro's KodiConfig.cmake is Linux-only (TARGET_LINUX, /usr paths); this one points at /sdk,
# which tools/portable/build.sh fills with only Kodi's and nlohmann's headers (mounted from the dev
# VM), so the Linux /usr/include never reaches the Windows compiler. Version numbers: Kodi 21.3.
set(APP_NAME Kodi)
set(APP_NAME_LC kodi)
set(APP_NAME_UC KODI)
set(APP_PACKAGE org.xbmc.kodi)
set(APP_VERSION_MAJOR 21)
set(APP_VERSION_MINOR 3)
set(APP_VERSION_CODE 21.3.0)
set(KODI_PREFIX /sdk)
set(KODI_INCLUDE_DIR /sdk/include/kodi)
set(KODI_LIB_DIR /sdk/lib/kodi)
set(KODI_DATA_DIR /sdk/share/kodi)
list(APPEND CMAKE_MODULE_PATH /sdk/share/kodi/cmake)

# As Kodi's own Windows build (cmake/platform/windows/defines.txt), minus the MSVC-only ones.
add_definitions(-DTARGET_WINDOWS -DTARGET_WINDOWS_DESKTOP -D_WINDOWS -DWIN32 -D_WIN32_WINNT=0x0601
                -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DBUILD_KODI_ADDON)

set(CORE_SYSTEM_NAME windows)
set(PLATFORM windx)
set(PLATFORM_TAG windows-x86_64)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(AddonHelpers)
