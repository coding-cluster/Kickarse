# Kickarse DSP test runner (self-contained, no external framework).
# Standalone:  cmake -S tests -B %LOCALAPPDATA%\KickarseBuild\dsp -G "Visual Studio 17 2022" -A x64
# Or added by the top-level project after src/ (then kickarse_core already exists).
cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Static CRT so the tests link exactly like the plugin.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    project(KickarseTests LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(NOT TARGET kickarse_core)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../src" "${CMAKE_CURRENT_BINARY_DIR}/kickarse_core")
endif()

add_executable(kickarse_tests
    TestMain.cpp
    TestEnvelope.cpp
    TestDsp.cpp
    TestEngine.cpp
    TestBench.cpp)
target_link_libraries(kickarse_tests PRIVATE kickarse_core)
target_include_directories(kickarse_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_compile_features(kickarse_tests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(kickarse_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
    target_compile_definitions(kickarse_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(kickarse_tests PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME kickarse_tests COMMAND kickarse_tests)
