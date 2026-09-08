cmake_minimum_required(VERSION 3.24)
project(RiftLoop VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /EHsc)
  add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# --- third party -------------------------------------------------------------
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite)
if(MSVC)
  # sqlite amalgamation is C; silence its warnings at /W4
  target_compile_options(sqlite3 PRIVATE /W0)
endif()

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE third_party)

# --- core --------------------------------------------------------------------
file(GLOB CORE_SOURCES src/core/*.cpp)
add_library(riftloop_core STATIC ${CORE_SOURCES})
target_include_directories(riftloop_core PUBLIC src)
target_link_libraries(riftloop_core PUBLIC sqlite3 nlohmann_json)
target_link_libraries(riftloop_core PUBLIC winhttp crypt32 shell32 ole32 advapi32)
# Media Foundation: only videocut.cpp uses it (clip cutting, RF-REC-003).
target_link_libraries(riftloop_core PUBLIC mfplat mfreadwrite mfuuid propsys)

# --- apps --------------------------------------------------------------------
add_executable(RiftLoop.Analyzer src/analyzer/analyzer_main.cpp)
target_link_libraries(RiftLoop.Analyzer PRIVATE riftloop_core)

add_executable(RiftLoop.Agent WIN32 src/agent/agent_main.cpp)
target_link_libraries(RiftLoop.Agent PRIVATE riftloop_core)

file(GLOB DESKTOP_SOURCES src/desktop/*.cpp)
add_executable(RiftLoop.Desktop WIN32 ${DESKTOP_SOURCES})
target_link_libraries(RiftLoop.Desktop PRIVATE riftloop_core comctl32 gdi32 user32
                      uxtheme dwmapi msimg32 windowscodecs ole32)

add_executable(RiftLoop.Overlay WIN32 src/overlay/overlay_main.cpp)
target_link_libraries(RiftLoop.Overlay PRIVATE riftloop_core d2d1 dwrite user32)

add_executable(RiftLoop.Capture src/capture/capture_main.cpp)
target_link_libraries(RiftLoop.Capture PRIVATE riftloop_core windowsapp d3d11 dxgi
                      mfplat mfreadwrite mfuuid)
target_compile_options(RiftLoop.Capture PRIVATE /await)

# --- assets ------------------------------------------------------------------
# Twenty game icons that Data Dragon does not ship, 88 KB: five lanes, four
# scoreboard marks and eleven rank emblems (assets/icons/SOURCE.md). They
# sit next to the executable so the app reads them from disk and downloads
# nothing.
add_custom_target(riftloop_assets ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:RiftLoop.Desktop>/assets")
add_dependencies(RiftLoop.Desktop riftloop_assets)

# --- tests -------------------------------------------------------------------
enable_testing()
file(GLOB TEST_SOURCES tests/*.cpp)
add_executable(riftloop_tests ${TEST_SOURCES})
target_link_libraries(riftloop_tests PRIVATE riftloop_core)
target_compile_definitions(riftloop_tests PRIVATE RL_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
add_test(NAME core_tests COMMAND riftloop_tests)
