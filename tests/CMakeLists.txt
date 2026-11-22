# One test file per node type (tests/nodes/test_<type>.cpp); test_node_types.cpp checks every type has one.
set(GV_NODE_TESTS
    nodes/test_video_source.cpp
    nodes/test_contrast.cpp
    nodes/test_saturation.cpp
    nodes/test_luma_gate.cpp
    nodes/test_opacity.cpp
    nodes/test_milkdrop_preset.cpp
    nodes/test_switcher.cpp
    nodes/test_viewport.cpp)

add_executable(gv_tests
    common/test_log.cpp
    common/test_ring_buffer.cpp
    common/test_fit_rect.cpp
    model/test_performance_json.cpp
    model/test_document.cpp
    model/test_graph.cpp
    io/test_midi_decode.cpp
    engine/test_scene_pool.cpp
    engine/test_cycle_timer.cpp
    engine/test_beat_hit.cpp
    engine/test_render_plan.cpp
    library/test_library.cpp
    nodes/test_node_types.cpp
    ${GV_NODE_TESTS})
target_include_directories(gv_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gv_tests PRIVATE gv_warnings gv_common gv_model gv_io gv_engine gv_library gv_app_nodes Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
list(JOIN GV_NODE_TESTS "," GV_NODE_TEST_LIST)
target_compile_definitions(gv_tests PRIVATE GV_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    GV_NODE_TEST_FILES="${GV_NODE_TEST_LIST}")

include(Catch)
catch_discover_tests(gv_tests)

# GPU tests (N2.6): need a GL 4.1 context from a hidden window; each test skips itself if there's none.
add_executable(gv_gl_tests gl/test_video_chain.cpp gl/test_effect_nodes.cpp)
target_include_directories(gv_gl_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gv_gl_tests PRIVATE gv_warnings gv_engine Catch2::Catch2WithMain)
target_compile_definitions(gv_gl_tests PRIVATE GL_SILENCE_DEPRECATION)
catch_discover_tests(gv_gl_tests PROPERTIES LABELS gl)
