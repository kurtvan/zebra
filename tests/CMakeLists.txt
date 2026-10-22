add_executable(gv_tests
    common/test_log.cpp)
target_link_libraries(gv_tests PRIVATE gv_warnings gv_common Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(gv_tests)
