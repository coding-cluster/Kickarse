# Kickarse preset/shape test runner (self-contained, no external framework).
# Normally built as part of the top-level project (kickarse_core, with the embedded factory
# content, already exists by the time this is added -- see the "Preset agent" block at the end of
# the top-level CMakeLists.txt):
#   cmake -S . -B %LOCALAPPDATA%\KickarseBuild\presets -G "Visual Studio 17 2022" -A x64
#   cmake --build %LOCALAPPDATA%\KickarseBuild\presets --config Release --target kickarse_preset_tests
# Also buildable standalone for quick iteration:
#   cmake -S tests/presets -B %LOCALAPPDATA%\KickarseBuild\presets-standalone -G "Visual Studio 17 2022" -A x64
cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Static CRT so the tests link exactly like the plugin.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    project(KickarsePresetTests LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    if(NOT TARGET kickarse_core)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../src" "${CMAKE_CURRENT_BINARY_DIR}/kickarse_core")
    endif()

    # Standalone configure: kickarse_core exists but has no factory content yet (that normally
    # happens in the top-level CMakeLists.txt's "Preset agent" block) -- embed it here too.
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/EmbedFactory.cmake")
    set(_kkf_dir "${CMAKE_CURRENT_BINARY_DIR}/generated_factory")
    file(MAKE_DIRECTORY "${_kkf_dir}")
    kickarse_embed_factory("${_kkf_dir}/KickarseFactory.cpp")
    target_sources(kickarse_core PRIVATE "${_kkf_dir}/KickarseFactory.cpp")
    if(WIN32)
        target_link_libraries(kickarse_core PUBLIC shell32 ole32)
    endif()
endif()

add_executable(kickarse_preset_tests
    TestMain.cpp
    TestPresetData.cpp
    TestPresetStore.cpp
    TestShapeLibrary.cpp
    TestFactoryContent.cpp)
target_link_libraries(kickarse_preset_tests PRIVATE kickarse_core)
target_include_directories(kickarse_preset_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_compile_features(kickarse_preset_tests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(kickarse_preset_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
    target_compile_definitions(kickarse_preset_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(kickarse_preset_tests PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME kickarse_preset_tests COMMAND kickarse_preset_tests)
