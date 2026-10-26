# Third-party dependencies. Every FetchContent dependency is pinned to an exact tag.

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# --- SDL2 (Homebrew: brew install sdl2) ---
find_package(SDL2 2.30 REQUIRED CONFIG)

# --- OpenGL ---
find_package(OpenGL REQUIRED)

# --- Dear ImGui, docking branch ---
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b-docking
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL)
target_compile_definitions(imgui PUBLIC GL_SILENCE_DEPRECATION)

# --- nlohmann/json ---
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# --- stb (PNG load/save for the thumbnail cache), pinned to a commit: the repository has no tags ---
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5)
FetchContent_MakeAvailable(stb)

# --- Catch2 (tests only) ---
if(GV_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
endif()

# --- Syphon.framework ---
# Video input (milestone 3). Its install name is @rpath-relative, so targets add SYPHON_FRAMEWORK_DIR to their rpath.
find_library(SYPHON_FRAMEWORK Syphon
    PATHS "${CMAKE_SOURCE_DIR}/third_party/Syphon" /Library/Frameworks "$ENV{HOME}/Library/Frameworks"
    NO_DEFAULT_PATH)
if(NOT SYPHON_FRAMEWORK)
    message(FATAL_ERROR "Syphon.framework not found in third_party/Syphon, /Library/Frameworks or ~/Library/Frameworks. "
                        "Install it from https://github.com/Syphon/Syphon-Framework/releases.")
endif()
get_filename_component(SYPHON_FRAMEWORK_DIR "${SYPHON_FRAMEWORK}" DIRECTORY)
message(STATUS "Syphon: ${SYPHON_FRAMEWORK}")
