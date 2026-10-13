# Kodi 21 add-on SDK config for cross-building the Android libraries with the NDK.
# Like kodi-windows/: the distro's KodiConfig.cmake is Linux-desktop only; this one points at /sdk
# (only Kodi's and nlohmann's headers, mounted by tools/portable/build.sh). On Android Kodi
# expects lib<id>.so (AddonHelpers) and copies it to its own binary directory before loading.
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

# As Kodi's own Android build (cmake/platform/android/defines.txt), the add-on relevant part.
add_definitions(-DTARGET_POSIX -DTARGET_LINUX -DTARGET_ANDROID -D_LINUX -DBUILD_KODI_ADDON)

set(OS android) # AddonHelpers: lib prefix (Kodi loads lib<id>.so on Android)
set(CORE_SYSTEM_NAME android)
set(PLATFORM android)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(AddonHelpers)
