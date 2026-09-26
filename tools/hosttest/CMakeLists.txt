# kickarse_hosttest -- headless VST2 + CLAP smoke test. Loads Kickarse-vst2.dll / Kickarse.clap
# from its own output directory at runtime (LoadLibrary), so it only needs the vendored,
# clean-room VST2 header and DPF's bundled CLAP headers at compile time -- no link-time
# dependency on the Kickarse plugin targets themselves.
add_executable(kickarse_hosttest main.cpp)

target_compile_features(kickarse_hosttest PRIVATE cxx_std_17)

target_include_directories(kickarse_hosttest PRIVATE
    "${DPF_ROOT_DIR}/distrho/src")

if(WIN32)
    target_compile_definitions(kickarse_hosttest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# Land next to the plugin binaries (same convention DPF itself uses: bin/, no per-config
# subdirectory) so the host test can find Kickarse-vst2.dll / Kickarse.clap next to itself.
set_target_properties(kickarse_hosttest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/$<0:>")
