cmake_minimum_required(VERSION 3.22)

# Must be set before project() to affect CMAKE_MSVC_RUNTIME_LIBRARY (static CRT, no VC++
# redistributable needed -- same rule the plugin build follows).
cmake_policy(SET CMP0091 NEW)

project(KickarseSetup LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "This installer is Windows/MSVC-only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(KICKARSE_VERSION "1.0.0" CACHE STRING "Kickarse version (X.Y.Z)")

# ---------------------------------------------------------------- payload dir
if(NOT DEFINED KICKARSE_PAYLOAD_DIR)
    message(FATAL_ERROR
        "KICKARSE_PAYLOAD_DIR is not set.\n"
        "Pass -DKICKARSE_PAYLOAD_DIR=<dist folder> containing:\n"
        "  VST3\\Kickarse.vst3\\...   (the whole bundle folder)\n"
        "  VST2\\<name>.dll          (exactly one .dll)\n"
        "  CLAP\\<name>.clap         (exactly one .clap file)\n"
        "  LICENSE.txt or README.txt (optional; a minimal one is generated if absent)")
endif()
get_filename_component(KICKARSE_PAYLOAD_DIR "${KICKARSE_PAYLOAD_DIR}" ABSOLUTE)
if(NOT EXISTS "${KICKARSE_PAYLOAD_DIR}")
    message(FATAL_ERROR "KICKARSE_PAYLOAD_DIR does not exist: ${KICKARSE_PAYLOAD_DIR}")
endif()
if(NOT EXISTS "${KICKARSE_PAYLOAD_DIR}/VST3/Kickarse.vst3")
    message(FATAL_ERROR
        "Missing VST3 bundle: ${KICKARSE_PAYLOAD_DIR}/VST3/Kickarse.vst3 "
        "(expected the whole Kickarse.vst3\\ folder tree)")
endif()
file(GLOB _kk_vst2_dlls "${KICKARSE_PAYLOAD_DIR}/VST2/*.dll")
if(NOT _kk_vst2_dlls)
    message(FATAL_ERROR "Missing a .dll in ${KICKARSE_PAYLOAD_DIR}/VST2/")
endif()
file(GLOB _kk_clap_files "${KICKARSE_PAYLOAD_DIR}/CLAP/*.clap")
if(NOT _kk_clap_files)
    message(FATAL_ERROR "Missing a .clap file in ${KICKARSE_PAYLOAD_DIR}/CLAP/")
endif()

find_program(KICKARSE_POWERSHELL powershell.exe REQUIRED)

# ------------------------------------------------------------ generated blobs
set(KICKARSE_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${KICKARSE_GEN_DIR}")

set(PAYLOAD_DATA_BIN "${KICKARSE_GEN_DIR}/payload.bin")
set(PAYLOAD_MANIFEST_BIN "${KICKARSE_GEN_DIR}/manifest.bin")
set(APP_ICON_ICO "${KICKARSE_GEN_DIR}/kickarse.ico")
set(APP_MANIFEST_XML "${CMAKE_CURRENT_SOURCE_DIR}/src/Kickarse.manifest")

file(GLOB_RECURSE KICKARSE_PAYLOAD_SOURCE_FILES "${KICKARSE_PAYLOAD_DIR}/*")

add_custom_command(
    OUTPUT "${PAYLOAD_DATA_BIN}" "${PAYLOAD_MANIFEST_BIN}"
    COMMAND "${KICKARSE_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/PackPayload.ps1"
            -PayloadDir "${KICKARSE_PAYLOAD_DIR}" -OutDir "${KICKARSE_GEN_DIR}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/PackPayload.ps1" ${KICKARSE_PAYLOAD_SOURCE_FILES}
    COMMENT "Packing Kickarse payload from ${KICKARSE_PAYLOAD_DIR}"
    VERBATIM
)

add_custom_command(
    OUTPUT "${APP_ICON_ICO}"
    COMMAND "${KICKARSE_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/GenerateIcon.ps1" -OutFile "${APP_ICON_ICO}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/GenerateIcon.ps1"
    COMMENT "Generating installer icon"
    VERBATIM
)

add_custom_target(KickarseGenerated DEPENDS "${PAYLOAD_DATA_BIN}" "${PAYLOAD_MANIFEST_BIN}" "${APP_ICON_ICO}")

# ------------------------------------------------------------------ version
string(REPLACE "." ";" _kk_ver_list "${KICKARSE_VERSION}")
list(LENGTH _kk_ver_list _kk_ver_len)
while(_kk_ver_len LESS 4)
    list(APPEND _kk_ver_list "0")
    list(LENGTH _kk_ver_list _kk_ver_len)
endwhile()
string(REPLACE ";" "," KICKARSE_VERSION_COMMA "${_kk_ver_list}")
set(KICKARSE_VERSION_STR "${KICKARSE_VERSION}")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Kickarse.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Kickarse.rc"
    @ONLY
)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/Kickarse.rc" PROPERTIES
    OBJECT_DEPENDS "${PAYLOAD_DATA_BIN};${PAYLOAD_MANIFEST_BIN};${APP_ICON_ICO};${APP_MANIFEST_XML}"
)

# ------------------------------------------------------------------- target
add_executable(Kickarse-Setup WIN32
    src/main.cpp
    src/Payload.cpp
    src/InstallLogic.cpp
    src/Gui.cpp
    src/Args.h
    src/Payload.h
    src/InstallLogic.h
    src/Gui.h
    src/resource.h
    "${CMAKE_CURRENT_BINARY_DIR}/Kickarse.rc"
)
add_dependencies(Kickarse-Setup KickarseGenerated)

target_include_directories(Kickarse-Setup PRIVATE src)
target_compile_definitions(Kickarse-Setup PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    "KICKARSE_VERSION_STR_W=L\"${KICKARSE_VERSION_STR}\""
)
target_link_libraries(Kickarse-Setup PRIVATE
    comctl32 shell32 ole32 uuid advapi32 user32 gdi32 shlwapi version
)
target_link_options(Kickarse-Setup PRIVATE "/ENTRY:wWinMainCRTStartup" "/MANIFEST:NO")

set_target_properties(Kickarse-Setup PROPERTIES
    OUTPUT_NAME "Kickarse-Setup"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
foreach(_cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(Kickarse-Setup PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/bin"
    )
endforeach()
