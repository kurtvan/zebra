configure_file(common/app_info.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/app_info.hpp" @ONLY)

add_library(gv_common STATIC
    common/log.cpp
    common/log.hpp
    common/paths.mm
    common/paths.hpp)
target_include_directories(gv_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(gv_common PUBLIC cxx_std_20)
target_link_libraries(gv_common PRIVATE gv_warnings "-framework Foundation")
set_source_files_properties(common/paths.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

add_executable(govtech-visualizer
    app/main.cpp)
target_link_libraries(govtech-visualizer PRIVATE
    gv_warnings
    gv_common
    imgui
    libprojectM::projectM
    SDL2::SDL2
    OpenGL::GL)
target_compile_definitions(govtech-visualizer PRIVATE GL_SILENCE_DEPRECATION)
