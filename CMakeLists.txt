cmake_minimum_required(VERSION 3.24)

project(GovtechVisualizer VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

if(NOT APPLE)
    message(FATAL_ERROR "Govtech Visualizer currently builds on macOS only.")
endif()

# The app name lives here only; everything else derives from it (see src/common/app_info.hpp.in).
set(GV_APP_NAME "Govtech Visualizer")
set(GV_BUNDLE_ID "com.govtech.visualizer")

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(GV_BUILD_TESTS "Build the unit tests" ON)
option(GV_SPIKE "Build the milestone 1 compile spike tool with timing instrumentation patches" OFF)

include(ProjectMPatched)
include(Dependencies)

# Warnings for our own targets only (third-party code keeps its own flags).
add_library(gv_warnings INTERFACE)
target_compile_options(gv_warnings INTERFACE -Wall -Wextra -Wno-unused-parameter)

add_subdirectory(src)

# Publishes a Syphon test pattern, for checking Syphon input by hand.
add_subdirectory(tools/syphon_test_server)
# Sends CC 20 from a virtual MIDI source, for checking MIDI input and hot-plugging by hand.
add_subdirectory(tools/midi_test_sender)

if(GV_SPIKE)
    add_subdirectory(tools/spike_compile)
endif()

if(GV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
