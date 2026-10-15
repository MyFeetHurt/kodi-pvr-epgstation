cmake_minimum_required(VERSION 3.10)
project(pvr.epgstation)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)
if(NLOHMANN_JSON_INCLUDE_DIR)
  # Cross builds (Windows): the header-only library from a given directory instead of the build
  # host's package, whose include path (/usr/include) must not reach the cross compiler.
  add_library(nlohmann_json INTERFACE)
  target_include_directories(nlohmann_json INTERFACE ${NLOHMANN_JSON_INCLUDE_DIR})
  add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
else()
  find_package(nlohmann_json 3.2 REQUIRED)
endif()

include_directories(${KODI_INCLUDE_DIR}/.. # same include hack as pvr.demo
                    ${PROJECT_SOURCE_DIR}/src)

# PVR API major of the headers we build against (8 = Kodi 21 "Omega", 9 = Kodi 22 "Piers"): the
# recorded-stream calls changed between them. Read from the headers, not KodiConfig.cmake, so the
# hand-written Windows/Android configs in tools/portable work for either Kodi.
file(STRINGS ${KODI_INCLUDE_DIR}/versions.h PVR_API_LINE
     REGEX "^#define ADDON_INSTANCE_VERSION_PVR +\"[0-9]+")
string(REGEX MATCH "\"([0-9]+)\\." _ "${PVR_API_LINE}")
set(PVR_API_MAJOR ${CMAKE_MATCH_1})
if(NOT PVR_API_MAJOR MATCHES "^(8|9)$")
  message(FATAL_ERROR "unsupported Kodi PVR API '${PVR_API_LINE}' (expected 8.x or 9.x)")
endif()
message(STATUS "Kodi PVR API ${PVR_API_MAJOR}")
add_definitions(-DPVR_API_MAJOR=${PVR_API_MAJOR})

set(EPGSTATION_SOURCES src/Addon.cpp
                       src/Client.cpp
                       src/KodiHttp.cpp
                       src/epgstation/Api.cpp
                       src/epgstation/Channels.cpp
                       src/epgstation/GrowingStream.cpp
                       src/epgstation/Live.cpp
                       src/epgstation/Programs.cpp
                       src/epgstation/Recordings.cpp
                       src/epgstation/Rules.cpp
                       src/epgstation/Timers.cpp)

set(EPGSTATION_HEADERS src/Addon.h
                       src/Client.h
                       src/KodiHttp.h
                       src/epgstation/Api.h
                       src/epgstation/Channels.h
                       src/epgstation/GenreNames.inc
                       src/epgstation/GrowingStream.h
                       src/epgstation/Live.h
                       src/epgstation/Programs.h
                       src/epgstation/Recordings.h
                       src/epgstation/Rules.h
                       src/epgstation/Timers.h)

set(DEPLIBS nlohmann_json::nlohmann_json)

# <platform> in addon.xml: Kodi's architecture tag (xbmc/addons/addoninfo/AddonInfoBuilder.cpp),
# so a repository can serve one build per architecture and Kodi installs the matching one.
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" ADDON_ARCH)
if(ADDON_ARCH MATCHES "^(x86_64|amd64)$")
  set(ADDON_ARCH x86_64)
elseif(ADDON_ARCH MATCHES "^(aarch64|arm64)$")
  set(ADDON_ARCH aarch64)
endif()
if(ANDROID)
  # Kodi's tags (AddonInfoBuilder.cpp): android-aarch64, android-armv7, android-i686
  if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(ADDON_PLATFORM_TAG android-aarch64)
  elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(ADDON_PLATFORM_TAG android-armv7)
  elseif(ANDROID_ABI STREQUAL "x86")
    set(ADDON_PLATFORM_TAG android-i686)
  else()
    message(FATAL_ERROR "no Kodi platform tag for Android ABI ${ANDROID_ABI}")
  endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(ADDON_PLATFORM_TAG "linux-${ADDON_ARCH}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(ADDON_PLATFORM_TAG "windows-${ADDON_ARCH}")
else()
  message(FATAL_ERROR "no Kodi platform tag for ${CMAKE_SYSTEM_NAME} yet")
endif()

# <news> in addon.xml: what Kodi shows as "latest changes". Kodi caps it at 1500 characters.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/pvr.epgstation/changelog.txt ADDON_NEWS)
string(SUBSTRING "${ADDON_NEWS}" 0 1500 ADDON_NEWS)
string(REPLACE "&" "&amp;" ADDON_NEWS "${ADDON_NEWS}")
string(REPLACE "<" "&lt;" ADDON_NEWS "${ADDON_NEWS}")
string(REPLACE ">" "&gt;" ADDON_NEWS "${ADDON_NEWS}")
string(STRIP "${ADDON_NEWS}" ADDON_NEWS)

build_addon(pvr.epgstation EPGSTATION DEPLIBS)

include(CPack)
