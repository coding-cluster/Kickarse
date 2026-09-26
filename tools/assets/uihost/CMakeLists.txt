# Kickarse UI snapshot host (design tooling, not shipped). Standalone CMake project:
#   cmake -S tools/assets/uihost -B %LOCALAPPDATA%\KickarseBuild\uihost -G "Visual Studio 17 2022" -A x64
#   cmake --build %LOCALAPPDATA%\KickarseBuild\uihost --config Release
cmake_minimum_required(VERSION 3.22)
project(kickarse_uihost LANGUAGES CXX)
add_executable(kickarse_uihost uihost.cpp)
target_compile_features(kickarse_uihost PRIVATE cxx_std_17)
target_include_directories(kickarse_uihost PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../external/DPF/distrho/src")
target_compile_definitions(kickarse_uihost PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(kickarse_uihost PRIVATE user32)
