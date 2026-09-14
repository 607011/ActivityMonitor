cmake_minimum_required(VERSION 3.16)
project(ActivityMonitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "ActivityMonitor is Windows-only (uses GDI and PDH).")
endif()

add_executable(ActivityMonitor WIN32
    src/main.cpp
    src/CpuMonitor.cpp
    src/CpuMonitor.h
    src/GraphWindow.cpp
    src/GraphWindow.h
    src/resource.rc
)

target_link_libraries(ActivityMonitor PRIVATE pdh gdi32 user32 comctl32)
target_compile_definitions(ActivityMonitor PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_link_options(ActivityMonitor PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/app.manifest")
endif()
