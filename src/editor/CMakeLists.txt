# kickarse_editor: headless envelope-editor model (selection, snapping, hit testing, tools, Quick
# Shift, transforms, capture, undo/redo). Framework independent: no DPF / NanoVG includes.
# Added by the top-level CMakeLists after kickarse_core exists; the plugin links it.
file(GLOB KICKARSE_EDITOR_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(kickarse_editor STATIC ${KICKARSE_EDITOR_SOURCES})
target_link_libraries(kickarse_editor PUBLIC kickarse_core)
# Consumers include "editor/EditorModel.h" (src/ is on kickarse_core's public include path too).
target_include_directories(kickarse_editor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(kickarse_editor PUBLIC cxx_std_17)
set_target_properties(kickarse_editor PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(kickarse_editor PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
else()
    target_compile_options(kickarse_editor PRIVATE -Wall -Wextra)
endif()
