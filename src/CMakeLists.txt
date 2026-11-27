configure_file(common/app_info.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/app_info.hpp" @ONLY)

add_library(gv_common STATIC
    common/log.cpp
    common/log.hpp
    common/paths.mm
    common/paths.hpp
    common/memory.cpp
    common/memory.hpp
    common/SpscRingBuffer.hpp
    common/FitRect.hpp
    common/ThumbnailVisible.hpp)
target_include_directories(gv_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(gv_common PUBLIC cxx_std_20)
target_link_libraries(gv_common PRIVATE gv_warnings "-framework Foundation")

add_library(gv_model STATIC
    model/Arrange.cpp
    model/Arrange.hpp
    model/Graph.cpp
    model/Graph.hpp
    model/LegacyScene.cpp
    model/LegacyScene.hpp
    model/NodeList.cpp
    model/NodeList.hpp
    model/Performance.cpp
    model/Performance.hpp
    model/PerformanceJson.cpp
    model/PerformanceJson.hpp
    model/Document.cpp
    model/Document.hpp
    model/nodes/NodeDef.cpp
    model/nodes/NodeDef.hpp
    model/nodes/NodeJson.hpp
    model/nodes/VideoSourceNode.cpp
    model/nodes/ContrastNode.cpp
    model/nodes/SaturationNode.cpp
    model/nodes/LumaGateNode.cpp
    model/nodes/OpacityNode.cpp
    model/nodes/LfoNode.cpp
    model/nodes/LfoNode.hpp
    model/nodes/AudioInNode.cpp
    model/nodes/EnvelopeNode.cpp
    model/nodes/BandNode.cpp
    model/nodes/BandNode.hpp
    model/nodes/MilkdropPresetNode.cpp
    model/nodes/SwitcherNode.cpp
    model/nodes/ViewportNode.cpp
    model/nodes/IsfShaderNode.cpp
    model/nodes/IsfShaderNode.hpp)
# Public: model/nodes/NodeDef.hpp names nlohmann::json in the node definitions' signatures, and
# model/nodes/IsfShaderNode.hpp the parsed ISF header.
target_link_libraries(gv_model PUBLIC gv_common gv_isf nlohmann_json::nlohmann_json PRIVATE gv_warnings)

add_library(gv_library STATIC
    library/PresetParsing.cpp
    library/PresetParsing.hpp
    library/Library.cpp
    library/Library.hpp
    library/PresetStepping.cpp
    library/PresetStepping.hpp)
target_link_libraries(gv_library PUBLIC gv_common PRIVATE gv_warnings nlohmann_json::nlohmann_json)

add_library(gv_thumbnails STATIC
    thumbnails/ThumbnailService.cpp
    thumbnails/ThumbnailService.hpp
    thumbnails/SlotPlan.hpp)
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

# ISF shaders: the header parser and GLSL translator are pure (unit tested); the program needs GL.
add_library(gv_isf STATIC
    isf/IsfHeader.cpp
    isf/IsfHeader.hpp
    isf/IsfSize.cpp
    isf/IsfSize.hpp)
target_link_libraries(gv_isf PUBLIC gv_common PRIVATE gv_warnings nlohmann_json::nlohmann_json)

add_library(gv_isf_gl STATIC
    isf/IsfBuffers.cpp
    isf/IsfBuffers.hpp
    isf/IsfProgram.cpp
    isf/IsfProgram.hpp)
target_compile_definitions(gv_isf_gl PRIVATE GL_SILENCE_DEPRECATION)
target_link_libraries(gv_isf_gl PUBLIC gv_isf OpenGL::GL PRIVATE gv_warnings)

add_library(gv_engine STATIC
    engine/ScenePool.cpp
    engine/ScenePool.hpp
    engine/Lane.cpp
    engine/Lane.hpp
    engine/Engine.cpp
    engine/Engine.hpp
    engine/FileWatcher.cpp
    engine/FileWatcher.hpp
    engine/SwitcherClock.cpp
    engine/SwitcherClock.hpp
    engine/BeatHit.cpp
    engine/BeatHit.hpp
    engine/EnvelopeFollower.cpp
    engine/EnvelopeFollower.hpp
    engine/BandFilter.cpp
    engine/BandFilter.hpp
    engine/SceneAudio.cpp
    engine/SceneAudio.hpp
    engine/VideoChain.cpp
    engine/VideoChain.hpp
    engine/RenderPlan.cpp
    engine/RenderPlan.hpp)
target_link_libraries(gv_engine PUBLIC gv_common gv_model gv_isf_gl SDL2::SDL2 OpenGL::GL libprojectM::projectM PRIVATE gv_warnings)
target_compile_definitions(gv_engine PRIVATE GL_SILENCE_DEPRECATION)

# The canvas's node types (and the widgets they draw with): a library of their own, so tests/nodes can draw
# them headless, with a fake NodeContext.
add_library(gv_app_nodes STATIC
    app/Widgets.cpp
    app/Widgets.hpp
    app/nodes/NodeUi.cpp
    app/nodes/NodeUi.hpp
    app/nodes/VideoSourceNode.cpp
    app/nodes/ContrastNode.cpp
    app/nodes/SaturationNode.cpp
    app/nodes/LumaGateNode.cpp
    app/nodes/OpacityNode.cpp
    app/nodes/LfoNode.cpp
    app/nodes/AudioInNode.cpp
    app/nodes/EnvelopeNode.cpp
    app/nodes/BandNode.cpp
    app/nodes/MilkdropPresetNode.cpp
    app/nodes/SwitcherNode.cpp
    app/nodes/ViewportNode.cpp
    app/nodes/IsfShaderNode.cpp)
target_link_libraries(gv_app_nodes PUBLIC gv_common gv_model gv_library imgui PRIVATE gv_warnings)

# The user's settings, including which editor areas are collapsed: pure, so tests can round-trip them.
add_library(gv_app_settings STATIC
    app/PaneLayout.cpp
    app/PaneLayout.hpp
    app/Settings.cpp
    app/Settings.hpp)
target_link_libraries(gv_app_settings PUBLIC gv_common nlohmann_json::nlohmann_json PRIVATE gv_warnings)

add_executable(govtech-visualizer
    app/main.cpp
    app/Shell.cpp
    app/Shell.hpp
    app/GraphEditor.cpp
    app/GraphEditor.hpp
    app/CanvasZoom.hpp
    app/WireRoute.hpp
    app/EditorView.hpp
    app/MiniMap.hpp
    app/MiniMapWidget.hpp
    app/VideoManager.cpp
    app/VideoManager.hpp
    app/macos/FileDialog.mm
    app/macos/FileDialog.hpp
    app/macos/Menu.mm
    app/macos/Menu.hpp
    app/macos/Window.mm
    app/macos/Window.hpp)
target_link_libraries(govtech-visualizer PRIVATE
    gv_warnings
    gv_common
    gv_model
    gv_io
    gv_engine
    gv_library
    gv_thumbnails
    gv_app_nodes
    gv_app_settings
    imgui_node_editor # and imgui
    nlohmann_json::nlohmann_json
    SDL2::SDL2
    OpenGL::GL
    "-framework AppKit"
    "-framework UniformTypeIdentifiers")
target_compile_definitions(govtech-visualizer PRIVATE GL_SILENCE_DEPRECATION)
# Syphon.framework's install name is @rpath-relative.
set_target_properties(govtech-visualizer PROPERTIES BUILD_RPATH "${SYPHON_FRAMEWORK_DIR}")

# Objective-C++ sources use ARC.
set_source_files_properties(common/paths.mm io/macos/CoreMidiInput.mm app/macos/FileDialog.mm app/macos/Menu.mm app/macos/Window.mm
    io/macos/FileVideoSource.mm io/macos/SyphonVideoSource.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
