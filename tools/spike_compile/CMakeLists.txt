add_executable(gv_spike_compile main.cpp pipeline.cpp pipeline.hpp)
target_link_libraries(gv_spike_compile PRIVATE gv_warnings gv_common libprojectM::projectM SDL2::SDL2 OpenGL::GL)
target_compile_definitions(gv_spike_compile PRIVATE GL_SILENCE_DEPRECATION)
