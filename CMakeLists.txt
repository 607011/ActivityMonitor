cmake_minimum_required(VERSION 3.16)
project(ActivityMonitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "ActivityMonitor is Windows-only (uses GDI and PDH).")
endif()

# Version, normally supplied by CI from the release tag (e.g. -DAPP_VERSION=1.2.3).
# Defaults to 0.0.0 for local/dev builds.
set(APP_VERSION "0.0.0" CACHE STRING "Application version, e.g. 1.2.3")
if(APP_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    set(APP_VERSION_MAJOR ${CMAKE_MATCH_1})
    set(APP_VERSION_MINOR ${CMAKE_MATCH_2})
    set(APP_VERSION_PATCH ${CMAKE_MATCH_3})
else()
    message(WARNING "APP_VERSION '${APP_VERSION}' is not in MAJOR.MINOR.PATCH form; defaulting to 0.0.0")
    set(APP_VERSION "0.0.0")
    set(APP_VERSION_MAJOR 0)
    set(APP_VERSION_MINOR 0)
    set(APP_VERSION_PATCH 0)
endif()
set(APP_VERSION_BUILD 0)
set(APP_VERSION_STRING ${APP_VERSION})

configure_file(
    ${CMAKE_SOURCE_DIR}/src/version.h.in
    ${CMAKE_BINARY_DIR}/generated/version.h
    @ONLY
)

add_executable(ActivityMonitor WIN32
    src/main.cpp
    src/CpuMonitor.cpp
    src/CpuMonitor.h
    src/GraphWindow.cpp
    src/GraphWindow.h
    src/resource.rc
)

target_link_libraries(ActivityMonitor PRIVATE pdh gdi32 user32 comctl32 shell32)
target_compile_definitions(ActivityMonitor PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(ActivityMonitor PRIVATE ${CMAKE_BINARY_DIR}/generated)

if(MSVC)
    target_link_options(ActivityMonitor PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/app.manifest")
endif()
