# Builds libprojectM from a patched copy of the pinned submodule.
#
# The submodule at third_party/projectm is never modified. At configure time it is
# copied to ${CMAKE_BINARY_DIR}/projectm-src and every third_party/patches/*.patch is
# applied in filename order. The copy is redone whenever the submodule commit or any
# patch file changes (tracked by a stamp file).

set(GV_PROJECTM_SUBMODULE "${CMAKE_SOURCE_DIR}/third_party/projectm")
set(GV_PROJECTM_PATCH_DIR "${CMAKE_SOURCE_DIR}/third_party/patches")
set(GV_PROJECTM_SRC "${CMAKE_BINARY_DIR}/projectm-src")
set(GV_PROJECTM_STAMP "${CMAKE_BINARY_DIR}/projectm-src.stamp")

find_package(Git REQUIRED)

if(NOT EXISTS "${GV_PROJECTM_SUBMODULE}/CMakeLists.txt"
        OR NOT EXISTS "${GV_PROJECTM_SUBMODULE}/vendor/projectm-eval/CMakeLists.txt")
    message(FATAL_ERROR
        "projectM submodule is missing or incomplete. Run:\n"
        "  git submodule update --init --recursive")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${GV_PROJECTM_SUBMODULE}"
    OUTPUT_VARIABLE _gv_pm_head
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)

file(GLOB _gv_pm_patches "${GV_PROJECTM_PATCH_DIR}/*.patch")
list(SORT _gv_pm_patches)

# Spike-only instrumentation patches (milestone 1). Use a separate build directory for these builds.
if(GV_SPIKE)
    file(GLOB _gv_pm_spike_patches "${CMAKE_SOURCE_DIR}/tools/spike_compile/patches/*.patch")
    list(SORT _gv_pm_spike_patches)
    list(APPEND _gv_pm_patches ${_gv_pm_spike_patches})
endif()

set(_gv_pm_stamp_content "submodule=${_gv_pm_head}\n")
foreach(_patch IN LISTS _gv_pm_patches)
    file(SHA256 "${_patch}" _hash)
    get_filename_component(_name "${_patch}" NAME)
    string(APPEND _gv_pm_stamp_content "${_name}=${_hash}\n")
endforeach()

set(_gv_pm_old_stamp "")
if(EXISTS "${GV_PROJECTM_STAMP}")
    file(READ "${GV_PROJECTM_STAMP}" _gv_pm_old_stamp)
endif()

if(NOT _gv_pm_old_stamp STREQUAL _gv_pm_stamp_content OR NOT EXISTS "${GV_PROJECTM_SRC}")
    message(STATUS "projectM: preparing patched source copy in ${GV_PROJECTM_SRC}")
    file(REMOVE_RECURSE "${GV_PROJECTM_SRC}")
    file(REMOVE "${GV_PROJECTM_STAMP}")
    file(COPY "${GV_PROJECTM_SUBMODULE}/" DESTINATION "${GV_PROJECTM_SRC}"
        PATTERN ".git" EXCLUDE)

    # The copy lives inside this repo's build directory. Without its own repository, `git apply`
    # would resolve paths against this repo's root and silently skip every file.
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" init --quiet
        WORKING_DIRECTORY "${GV_PROJECTM_SRC}"
        COMMAND_ERROR_IS_FATAL ANY)

    foreach(_patch IN LISTS _gv_pm_patches)
        get_filename_component(_name "${_patch}" NAME)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${_patch}"
            WORKING_DIRECTORY "${GV_PROJECTM_SRC}"
            RESULT_VARIABLE _result
            ERROR_VARIABLE _error)
        if(NOT _result EQUAL 0)
            message(FATAL_ERROR "projectM: patch ${_name} failed to apply:\n${_error}")
        endif()
        message(STATUS "projectM: applied ${_name}")
    endforeach()

    file(WRITE "${GV_PROJECTM_STAMP}" "${_gv_pm_stamp_content}")
endif()

# Re-run configure when the submodule commit or the patch set changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_gv_pm_patches})
if(EXISTS "${CMAKE_SOURCE_DIR}/.git/modules/third_party/projectm/HEAD")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/.git/modules/third_party/projectm/HEAD")
endif()

# projectM build options.
set(ENABLE_PLAYLIST OFF CACHE BOOL "" FORCE)
set(ENABLE_SDL_UI OFF CACHE BOOL "" FORCE)
set(ENABLE_SYSTEM_PROJECTM_EVAL OFF CACHE BOOL "" FORCE)
set(ENABLE_SYSTEM_GLM OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

add_subdirectory("${GV_PROJECTM_SRC}" "${CMAKE_BINARY_DIR}/projectm-build" EXCLUDE_FROM_ALL)

# Shared libraries built by later targets in this project shouldn't inherit projectM's choice.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
