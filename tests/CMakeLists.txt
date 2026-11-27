# One test file per node type (tests/nodes/test_<type>.cpp); test_node_types.cpp checks every type has one.
set(GV_NODE_TESTS
    nodes/test_video_source.cpp
    nodes/test_contrast.cpp
    nodes/test_saturation.cpp
    nodes/test_luma_gate.cpp
    nodes/test_opacity.cpp
    nodes/test_milkdrop_preset.cpp
    nodes/test_switcher.cpp
    nodes/test_viewport.cpp
    nodes/test_isf_shader.cpp
    nodes/test_lfo.cpp
    nodes/test_audio_in.cpp
    nodes/test_envelope.cpp
    nodes/test_band.cpp)

add_executable(gv_tests
    common/test_log.cpp
    common/test_ring_buffer.cpp
    common/test_fit_rect.cpp
    common/test_thumbnail_visible.cpp
    common/test_folder_migration.cpp
    model/test_performance_json.cpp
    model/test_document.cpp
    model/test_graph.cpp
    model/test_arrange.cpp
    model/test_node_list.cpp
    model/ClassicView.cpp
    model/ClassicView.hpp
    io/test_midi_decode.cpp
    app/test_canvas_zoom.cpp
    app/test_minimap.cpp
    app/test_pane_layout.cpp
    app/test_pane_docking.cpp
    app/test_wire_route.cpp
    app/test_pin_pill.cpp
    engine/test_scene_pool.cpp
    engine/test_switcher_clock.cpp
    engine/test_file_watcher.cpp
    engine/test_beat_hit.cpp
    engine/test_envelope_follower.cpp
    engine/test_band_filter.cpp
    engine/test_scene_audio.cpp
    engine/test_render_plan.cpp
    library/test_library.cpp
    thumbnails/test_slot_plan.cpp
    nodes/test_node_types.cpp
    isf/test_isf_header.cpp
    ${GV_NODE_TESTS})
target_include_directories(gv_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gv_tests PRIVATE gv_warnings gv_common gv_model gv_io gv_engine gv_library gv_app_nodes gv_app_settings imgui_node_editor Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
list(JOIN GV_NODE_TESTS "," GV_NODE_TEST_LIST)
target_compile_definitions(gv_tests PRIVATE GV_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    GV_NODE_TEST_FILES="${GV_NODE_TEST_LIST}"
    GV_APP_EXECUTABLE="$<TARGET_FILE_NAME:zebra>")

include(Catch)
catch_discover_tests(gv_tests)

# GPU tests (N2.6): need a GL 4.1 context from a hidden window; each test skips itself if there's none.
add_executable(gv_gl_tests gl/test_video_chain.cpp gl/test_effect_nodes.cpp gl/test_isf_program.cpp gl/test_video_steps.cpp)
target_include_directories(gv_gl_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gv_gl_tests PRIVATE gv_warnings gv_engine Catch2::Catch2WithMain)
target_compile_definitions(gv_gl_tests PRIVATE GL_SILENCE_DEPRECATION)
catch_discover_tests(gv_gl_tests PROPERTIES LABELS gl)

# ISF shaders (D1): the parser in gv_tests, the GL program in gv_gl_tests, both reading tests/fixtures/isf.
target_link_libraries(gv_tests PRIVATE gv_isf)
target_link_libraries(gv_gl_tests PRIVATE gv_isf_gl)
target_compile_definitions(gv_gl_tests PRIVATE GV_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
