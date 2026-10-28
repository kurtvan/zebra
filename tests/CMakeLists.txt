add_executable(gv_tests
    common/test_log.cpp
    common/test_ring_buffer.cpp
    model/test_performance_json.cpp
    model/test_document.cpp
    io/test_midi_decode.cpp
    engine/test_scene_pool.cpp
    engine/test_cycle_timer.cpp
    library/test_library.cpp)
target_link_libraries(gv_tests PRIVATE gv_warnings gv_common gv_model gv_io gv_engine gv_library Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(gv_tests)
