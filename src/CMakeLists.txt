# kickarse_core: framework-independent DSP + shared model (no DPF). Linked by the plugin (DSP and UI
# parts) and by the test runner.
file(GLOB KICKARSE_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shared/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dsp/*.cpp")

add_library(kickarse_core STATIC ${KICKARSE_CORE_SOURCES})
target_include_directories(kickarse_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(kickarse_core PUBLIC cxx_std_17)
set_target_properties(kickarse_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(kickarse_core PRIVATE /W4 /permissive- /Zc:__cplusplus)
    target_compile_definitions(kickarse_core PUBLIC _USE_MATH_DEFINES NOMINMAX)
else()
    target_compile_options(kickarse_core PRIVATE -Wall -Wextra)
endif()
