configure_file(common/app_info.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/app_info.hpp" @ONLY)

add_library(gv_common STATIC
    common/log.cpp
    common/log.hpp
    common/paths.mm
    common/paths.hpp
    common/memory.cpp
    common/memory.hpp
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
    model/PerformanceJson.hpp
    model/Document.cpp
    model/Document.hpp)
target_link_libraries(gv_model PUBLIC gv_common PRIVATE gv_warnings nlohmann_json::nlohmann_json)

add_library(gv_library STATIC
    library/PresetParsing.cpp
    library/PresetParsing.hpp
    library/Library.cpp
    library/Library.hpp)
target_link_libraries(gv_library PUBLIC gv_common PRIVATE gv_warnings nlohmann_json::nlohmann_json)

add_library(gv_thumbnails STATIC
    thumbnails/ThumbnailService.cpp
    thumbnails/ThumbnailService.hpp)
target_include_directories(gv_thumbnails SYSTEM PRIVATE "${stb_SOURCE_DIR}")
target_compile_definitions(gv_thumbnails PRIVATE GL_SILENCE_DEPRECATION)
target_link_libraries(gv_thumbnails PUBLIC gv_common SDL2::SDL2 OpenGL::GL libprojectM::projectM PRIVATE gv_warnings)

add_library(gv_io STATIC
    io/MidiInput.hpp
    io/MidiDecode.cpp
    io/macos/CoreMidiInput.mm
    io/AudioInput.hpp
    io/sdl/SdlAudioInput.cpp
    io/VideoSource.hpp
    io/macos/RectToTexture.cpp
    io/macos/RectToTexture.hpp
    io/macos/FileVideoSource.mm
    io/macos/SyphonVideoSource.mm)
target_compile_options(gv_io PRIVATE "-F${SYPHON_FRAMEWORK_DIR}")
target_compile_definitions(gv_io PRIVATE GL_SILENCE_DEPRECATION)
target_link_libraries(gv_io PUBLIC gv_common SDL2::SDL2 OpenGL::GL PRIVATE gv_warnings "${SYPHON_FRAMEWORK}"
    "-framework CoreMIDI" "-framework CoreFoundation" "-framework AVFoundation" "-framework CoreVideo"
    "-framework CoreMedia" "-framework QuartzCore" "-framework Foundation")

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
    app/VideoManager.cpp
    app/VideoManager.hpp
    app/macos/FileDialog.mm
    app/macos/FileDialog.hpp
    app/macos/Menu.mm
    app/macos/Menu.hpp)
target_link_libraries(govtech-visualizer PRIVATE
    gv_warnings
    gv_common
    gv_model
    gv_io
    gv_engine
    gv_library
    gv_thumbnails
    imgui
    nlohmann_json::nlohmann_json
    SDL2::SDL2
    OpenGL::GL
    "-framework AppKit"
    "-framework UniformTypeIdentifiers")
target_compile_definitions(govtech-visualizer PRIVATE GL_SILENCE_DEPRECATION)
# Syphon.framework's install name is @rpath-relative.
set_target_properties(govtech-visualizer PROPERTIES BUILD_RPATH "${SYPHON_FRAMEWORK_DIR}")

# Objective-C++ sources use ARC.
set_source_files_properties(common/paths.mm io/macos/CoreMidiInput.mm app/macos/FileDialog.mm app/macos/Menu.mm
    io/macos/FileVideoSource.mm io/macos/SyphonVideoSource.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
