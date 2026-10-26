configure_file(common/app_info.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/app_info.hpp" @ONLY)

add_library(gv_common STATIC
    common/log.cpp
    common/log.hpp
    common/paths.mm
    common/paths.hpp
    common/SpscRingBuffer.hpp)
target_include_directories(gv_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(gv_common PUBLIC cxx_std_20)
target_link_libraries(gv_common PRIVATE gv_warnings "-framework Foundation")

add_library(gv_model STATIC
    model/Performance.cpp
    model/Performance.hpp
    model/PerformanceJson.cpp
    model/PerformanceJson.hpp)
target_link_libraries(gv_model PUBLIC gv_common PRIVATE gv_warnings nlohmann_json::nlohmann_json)

add_library(gv_io STATIC
    io/MidiInput.hpp
    io/MidiDecode.cpp
    io/macos/CoreMidiInput.mm
    io/AudioInput.hpp
    io/sdl/SdlAudioInput.cpp)
target_link_libraries(gv_io PUBLIC gv_common SDL2::SDL2 PRIVATE gv_warnings "-framework CoreMIDI" "-framework CoreFoundation")

add_library(gv_engine STATIC
    engine/ScenePool.cpp
    engine/ScenePool.hpp
    engine/Lane.cpp
    engine/Lane.hpp
    engine/Engine.cpp
    engine/Engine.hpp)
target_link_libraries(gv_engine PUBLIC gv_common gv_model SDL2::SDL2 OpenGL::GL libprojectM::projectM PRIVATE gv_warnings)
target_compile_definitions(gv_engine PRIVATE GL_SILENCE_DEPRECATION)

add_executable(govtech-visualizer
    app/main.cpp
    app/Settings.cpp
    app/Settings.hpp
    app/Shell.cpp
    app/Shell.hpp
    app/macos/FileDialog.mm
    app/macos/FileDialog.hpp)
target_link_libraries(govtech-visualizer PRIVATE
    gv_warnings
    gv_common
    gv_model
    gv_io
    gv_engine
    imgui
    nlohmann_json::nlohmann_json
    SDL2::SDL2
    OpenGL::GL
    "-framework AppKit"
    "-framework UniformTypeIdentifiers")
target_compile_definitions(govtech-visualizer PRIVATE GL_SILENCE_DEPRECATION)

# Objective-C++ sources use ARC.
set_source_files_properties(common/paths.mm io/macos/CoreMidiInput.mm app/macos/FileDialog.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
