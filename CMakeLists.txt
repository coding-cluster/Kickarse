# Kickarse -- top-level build. See docs/ARCHITECTURE.md for the full contract; this file only
# wires DPF to kickarse_core and the plugin/** glue. Build trees must live outside this directory
# (OneDrive locks .obj/.pdb files) -- see build.ps1 / docs/ARCHITECTURE.md for the convention.

cmake_minimum_required(VERSION 3.22)

# Static CRT (/MT, /MTd in Debug) so the plugin needs no VC++ redistributable. Must be set before
# the languages are enabled (i.e. before project()) and before DPF's targets are created.
cmake_policy(SET CMP0091 NEW)

project(Kickarse VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------------------------
# DPF (vendored, untouched) -- add_subdirectory brings in dpf_add_plugin() via its own
# include(DPF-plugin), see external/DPF/CMakeLists.txt.
add_subdirectory(external/DPF)

# ---------------------------------------------------------------------------------------------
# kickarse_core: the framework-independent engine (src/dsp + src/shared), owned by the DSP agent.
#
# KICKARSE_CORE_STUB (default OFF) swaps it for plugin/dev/CoreStub.cpp, a minimal pass-through
# Engine/Envelope, so the DPF plumbing (parameters, ports, state, run(), the UI, the validator,
# hosttest) can be built and exercised before/independently of the real DSP landing. If the real
# sources genuinely aren't there yet we fall back to the stub automatically (with a warning)
# regardless of the flag, so a plain `cmake --build` never hard-fails on an empty src/dsp; this
# does NOT change the cached option's default.
option(KICKARSE_CORE_STUB
    "Build kickarse_core from plugin/dev/CoreStub.cpp instead of src/ (placeholder engine)" OFF)

set(_kickarse_have_real_core FALSE)
if(EXISTS "${CMAKE_SOURCE_DIR}/src/shared/Envelope.cpp")
    file(GLOB _kickarse_dsp_sources "${CMAKE_SOURCE_DIR}/src/dsp/*.cpp")
    if(_kickarse_dsp_sources)
        set(_kickarse_have_real_core TRUE)
    endif()
endif()

if(KICKARSE_CORE_STUB)
    set(_kickarse_use_stub TRUE)
elseif(NOT _kickarse_have_real_core)
    message(WARNING
        "Kickarse: src/dsp/*.cpp and/or src/shared/Envelope.cpp not found -- falling back to "
        "plugin/dev/CoreStub.cpp for this configure. Re-run cmake once the DSP agent's sources "
        "land to pick up the real engine (KICKARSE_CORE_STUB stays OFF by default).")
    set(_kickarse_use_stub TRUE)
else()
    set(_kickarse_use_stub FALSE)
endif()

if(_kickarse_use_stub)
    add_library(kickarse_core STATIC plugin/dev/CoreStub.cpp)
    target_include_directories(kickarse_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(kickarse_core PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(kickarse_core PRIVATE /W4 /permissive- /Zc:__cplusplus)
        target_compile_definitions(kickarse_core PUBLIC _USE_MATH_DEFINES NOMINMAX)
    else()
        target_compile_options(kickarse_core PRIVATE -Wall -Wextra)
    endif()
    message(STATUS "Kickarse: kickarse_core = STUB (plugin/dev/CoreStub.cpp)")
else()
    add_subdirectory(src)
    message(STATUS "Kickarse: kickarse_core = real engine (src/)")
endif()

# Off switch for local iteration while tests/ is still being actively written by the DSP agent
# (its CMakeLists.txt can reference sources that land a moment later); on by default otherwise.
option(KICKARSE_BUILD_TESTS "Add tests/ (DSP agent's self-contained test runner) if present" ON)
if(KICKARSE_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------------------------
# Generated resources (fonts/images -> byte arrays), see cmake/GenerateResources.cmake. Works
# fine with resources/fonts and/or resources/images empty or missing entirely.
include(cmake/GenerateResources.cmake)

set(KICKARSE_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${KICKARSE_GENERATED_DIR}")

kickarse_generate_resources(
    "${KICKARSE_GENERATED_DIR}/KickarseResources.hpp"
    "${KICKARSE_GENERATED_DIR}/KickarseResources.cpp"
    "${CMAKE_SOURCE_DIR}/resources/fonts"
    "${CMAKE_SOURCE_DIR}/resources/images")

# ---------------------------------------------------------------------------------------------
# The plugin itself. Output names come straight from DPF's own convention for NAME=Kickarse:
#   bin/Kickarse.vst3/Contents/<arch>-win/Kickarse.vst3   (bundle folder)
#   bin/Kickarse-vst2.dll
#   bin/Kickarse.clap
#   bin/Kickarse.exe                                      (jack target, native RtAudio fallback
#                                                           on Windows -- see ARCHITECTURE.md)
file(GLOB KICKARSE_UI_EXTRA_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/ui/*.cpp")

dpf_add_plugin(Kickarse
    TARGETS vst2 vst3 clap jack
    UI_TYPE opengl
    FILES_DSP
        plugin/KickarsePlugin.cpp
    FILES_UI
        plugin/KickarseUI.cpp
        "${KICKARSE_GENERATED_DIR}/KickarseResources.cpp"
        ${KICKARSE_UI_EXTRA_SOURCES}
)

target_include_directories(Kickarse PUBLIC
    plugin
    "${CMAKE_SOURCE_DIR}/src"
    "${KICKARSE_GENERATED_DIR}")

target_link_libraries(Kickarse PUBLIC kickarse_core)

# ---------------------------------------------------------------------------------------------
# kickarse_editor: headless envelope-editor model (tools, selection, snap, Quick Shift, undo) used
# by the UI. Framework independent, owned by the editor agent; see src/editor/.
if(EXISTS "${CMAKE_SOURCE_DIR}/src/editor/CMakeLists.txt")
    add_subdirectory(src/editor)
    target_link_libraries(Kickarse PUBLIC kickarse_editor)
endif()
if(KICKARSE_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/tests/editor/CMakeLists.txt")
    add_subdirectory(tests/editor)
endif()

# ---------------------------------------------------------------------------------------------
add_subdirectory(tools/hosttest)

# ---------------------------------------------------------------------------------------------
# Preset agent block (owned by the preset agent; see docs/ARCHITECTURE.md). Embeds
# resources/presets/**/*.kkpreset and resources/shapes/**/*.kkshape (factory content for
# src/shared/Presets.*/Shapes.*) into kickarse_core, and adds its own test target.
include(cmake/EmbedFactory.cmake)

set(KICKARSE_FACTORY_DIR "${CMAKE_BINARY_DIR}/generated_factory")
file(MAKE_DIRECTORY "${KICKARSE_FACTORY_DIR}")
kickarse_embed_factory("${KICKARSE_FACTORY_DIR}/KickarseFactory.cpp")
target_sources(kickarse_core PRIVATE "${KICKARSE_FACTORY_DIR}/KickarseFactory.cpp")

if(WIN32)
    # UserData.cpp resolves <Documents> via SHGetKnownFolderPath (Shell32) / CoTaskMemFree (Ole32).
    target_link_libraries(kickarse_core PUBLIC shell32 ole32)
endif()

if(KICKARSE_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/tests/presets/CMakeLists.txt")
    add_subdirectory(tests/presets)
endif()
