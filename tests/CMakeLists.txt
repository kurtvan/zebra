add_executable(gv_tests
    common/test_log.cpp
    common/test_ring_buffer.cpp
    model/test_performance_json.cpp
    model/test_document.cpp
    model/test_graph.cpp
    io/test_midi_decode.cpp
    engine/test_scene_pool.cpp
    engine/test_cycle_timer.cpp
    engine/test_beat_hit.cpp
    engine/test_render_plan.cpp
    library/test_library.cpp)
target_link_libraries(gv_tests PRIVATE gv_warnings gv_common gv_model gv_io gv_engine gv_library Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
target_compile_definitions(gv_tests PRIVATE GV_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

include(Catch)
catch_discover_tests(gv_tests)
