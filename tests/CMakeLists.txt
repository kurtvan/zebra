add_executable(gv_tests
    common/test_log.cpp
    common/test_ring_buffer.cpp
    model/test_performance_json.cpp
    io/test_midi_decode.cpp
    engine/test_scene_pool.cpp)
target_link_libraries(gv_tests PRIVATE gv_warnings gv_common gv_model gv_io gv_engine Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(gv_tests)
