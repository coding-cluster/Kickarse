# Kickarse editor-model test runner (self-contained, no external framework).
#   kickarse_editor_tests              run everything
#   kickarse_editor_tests <substring>  run only the tests whose name contains <substring>
# Added by the top-level CMakeLists (needs the kickarse_editor target).
file(GLOB KICKARSE_EDITOR_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(kickarse_editor_tests ${KICKARSE_EDITOR_TEST_SOURCES})
target_link_libraries(kickarse_editor_tests PRIVATE kickarse_editor)
target_compile_features(kickarse_editor_tests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(kickarse_editor_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    target_compile_definitions(kickarse_editor_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(kickarse_editor_tests PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME kickarse_editor_tests COMMAND kickarse_editor_tests)
