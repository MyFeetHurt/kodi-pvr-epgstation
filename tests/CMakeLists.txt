# Unit tests for the pure EPGStation layer (src/epgstation): no Kodi needed.
cmake_minimum_required(VERSION 3.16)
project(pvr_epgstation_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json 3.2 REQUIRED)
find_package(Catch2 3 REQUIRED)

add_executable(unit_tests
  test_channels.cpp
  test_api.cpp
  test_programs.cpp
  test_live.cpp
  test_recordings.cpp
  test_growing.cpp
  test_timers.cpp
  test_rules.cpp
  ../src/epgstation/Api.cpp
  ../src/epgstation/Channels.cpp
  ../src/epgstation/GrowingStream.cpp
  ../src/epgstation/Live.cpp
  ../src/epgstation/Programs.cpp
  ../src/epgstation/Recordings.cpp
  ../src/epgstation/Rules.cpp
  ../src/epgstation/Timers.cpp)
target_include_directories(unit_tests PRIVATE ../src)
target_compile_definitions(unit_tests PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_compile_options(unit_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(unit_tests PRIVATE nlohmann_json::nlohmann_json Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(unit_tests)
