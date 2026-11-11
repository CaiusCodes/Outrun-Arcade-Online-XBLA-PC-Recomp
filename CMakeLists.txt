# outrun - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
option(OUTRUN_SPATIAL_FILTERS "Build against coherent spatial-filter renderer" OFF)
if(OUTRUN_SPATIAL_FILTERS)
    add_compile_definitions(REX_HAS_FIDELITYFX_SDK=1)
endif()
project(outrun LANGUAGES CXX)

# Release builds name no folder of the PC they were built on: source paths in
# the binary (__FILE__ in asserts and logs) are made relative, and the PDB is
# named without its folder.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=outrun")
    foreach(_or_dir IN ITEMS OUTRUN_SDK_PREFIX OUTRUN_PLUME_DIR OUTRUN_XENOSRECOMP_DIR)
        if(${_or_dir})
            get_filename_component(_or_name "${${_or_dir}}" NAME)
            add_compile_options("-ffile-prefix-map=${${_or_dir}}=${_or_name}")
        endif()
    endforeach()
endif()
if(WIN32)
    add_link_options("LINKER:/PDBALTPATH:%_PDB%")
endif()

# Development SDK overlay headers must match the selected runtime DLL cohort.
set(OUTRUN_TIMING_SDK_HEADERS "" CACHE PATH "Frame pacing SDK overlay headers")
if(OUTRUN_TIMING_SDK_HEADERS)
    include_directories(BEFORE "${OUTRUN_TIMING_SDK_HEADERS}")
    add_compile_definitions(OUTRUN_PASSIVE_DIALOGS=1)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(renderer_query_clock_test tests/renderer_query_clock_test.cpp)
    target_compile_features(renderer_query_clock_test PRIVATE cxx_std_17)
    target_include_directories(renderer_query_clock_test PRIVATE src)
    target_link_libraries(renderer_query_clock_test PRIVATE d3d12 dxgi)
    add_test(NAME renderer_query_clock_headless COMMAND renderer_query_clock_test)
    set_tests_properties(renderer_query_clock_headless PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 20)
    add_executable(renderer_query_submission_test tests/renderer_query_submission_test.cpp)
    target_compile_features(renderer_query_submission_test PRIVATE cxx_std_17)
    target_include_directories(renderer_query_submission_test PRIVATE src)
    add_test(NAME renderer_query_submission_budget COMMAND renderer_query_submission_test)
    add_executable(renderer_fence_wait_test tests/renderer_fence_wait_test.cpp)
    target_compile_features(renderer_fence_wait_test PRIVATE cxx_std_17)
    target_include_directories(renderer_fence_wait_test PRIVATE src)
    add_test(NAME renderer_fence_completion_event COMMAND renderer_fence_wait_test)
    set_tests_properties(renderer_fence_completion_event PROPERTIES TIMEOUT 10)
    add_executable(renderer_query_barrier_test tests/renderer_query_barrier_test.cpp)
    target_compile_features(renderer_query_barrier_test PRIVATE cxx_std_17)
    target_include_directories(renderer_query_barrier_test PRIVATE src)
    add_test(NAME renderer_query_barrier COMMAND renderer_query_barrier_test)
    set_tests_properties(renderer_query_barrier PROPERTIES TIMEOUT 10)
    add_executable(button_pulse_test tests/button_pulse_test.cpp)
    target_include_directories(button_pulse_test PRIVATE src)
    add_test(NAME keyboard_button_pulse COMMAND button_pulse_test)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OUTRUN_SDK_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/out/sdk-install" CACHE PATH "Outrun's pinned SDK install prefix")
if(NOT EXISTS "${OUTRUN_SDK_PREFIX}/lib/cmake/rexglue/rexglueConfig.cmake")
    message(FATAL_ERROR "Outrun's pinned SDK install is missing at ${OUTRUN_SDK_PREFIX}; run tools/Setup-Sdk.ps1")
endif()
set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)
set(rexglue_DIR "${OUTRUN_SDK_PREFIX}/lib/cmake/rexglue" CACHE PATH "Outrun's pinned ReXGlue package" FORCE)
list(PREPEND CMAKE_PREFIX_PATH "${OUTRUN_SDK_PREFIX}")
function(outrun_check_sdk_dependency_dirs)
    foreach(_package SDL3 Snappy fmt spdlog utf8cpp)
        if(DEFINED ${_package}_DIR AND NOT "${${_package}_DIR}" STREQUAL "")
            cmake_path(IS_PREFIX OUTRUN_SDK_PREFIX "${${_package}_DIR}" NORMALIZE _is_local)
            if(NOT _is_local)
                message(FATAL_ERROR "${_package}_DIR points outside Outrun's pinned SDK: ${${_package}_DIR}. Clear stale package cache entries with cmake -U '*_DIR' --preset win-amd64-release")
            endif()
        endif()
    endforeach()
endfunction()
outrun_check_sdk_dependency_dirs()

include(generated/rexglue.cmake)
outrun_check_sdk_dependency_dirs()

# Read-only capability inventory for the exact runtime shipped with this port.
add_executable(runtime_settings_probe EXCLUDE_FROM_ALL tools/runtime_settings_probe.cpp)
add_executable(audio_sample_probe EXCLUDE_FROM_ALL tools/audio_sample_probe.cpp)
target_link_libraries(audio_sample_probe PRIVATE rex::runtime)
add_executable(audio_context_merge_probe EXCLUDE_FROM_ALL tools/audio_context_merge_probe.cpp)
target_link_libraries(audio_context_merge_probe PRIVATE rex::runtime)
add_executable(audio_declick_replay EXCLUDE_FROM_ALL tools/audio_declick_replay.cpp)
target_include_directories(audio_declick_replay PRIVATE src)
target_link_libraries(runtime_settings_probe PRIVATE rex::runtime)

# Private, read-only executable inspection; never included in release payloads.
add_executable(menu_image_probe EXCLUDE_FROM_ALL tools/menu_image_probe.cpp)
target_link_libraries(menu_image_probe PRIVATE rex::runtime)

add_executable(leaderboard_runtime_probe EXCLUDE_FROM_ALL tools/leaderboard_runtime_probe.cpp src/leaderboards.cpp)
target_include_directories(leaderboard_runtime_probe PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(leaderboard_runtime_probe PRIVATE rex::runtime)

add_executable(pc_menu_patch_probe EXCLUDE_FROM_ALL tools/pc_menu_patch_probe.cpp src/pc_menu_patch.cpp src/multiplayer_menu_patch.cpp src/frontend_patch.cpp)
target_include_directories(pc_menu_patch_probe PRIVATE src)
target_link_libraries(pc_menu_patch_probe PRIVATE rex::runtime)

# Run Prepare-RegisterPacketTest.py after building the experimental overlay.
add_executable(renderer_register_packet_test EXCLUDE_FROM_ALL tests/renderer_register_packet_test.cpp)
target_include_directories(renderer_register_packet_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/out")
target_link_libraries(renderer_register_packet_test PRIVATE rex::runtime)

if(BUILD_TESTING)
    add_executable(keyboard_runtime_test tests/keyboard_runtime_test.cpp src/menu_pointer.cpp src/audio_diagnostics.cpp src/audio_pcm_capture.cpp)
    target_include_directories(keyboard_runtime_test PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(keyboard_runtime_test PRIVATE rex::runtime)
    add_test(NAME keyboard_runtime_bindings COMMAND keyboard_runtime_test)
endif()

# Sources
set(OUTRUN_SOURCES
    src/main.cpp
    src/audio_diagnostics.cpp
    src/audio_pcm_capture.cpp
    src/audio_sound_hooks.cpp
    src/audio_notification_isolation.cpp
    src/audio_queue_trace.cpp
    src/audio_loop_suppression.cpp
    src/audio_transition_trace.cpp
    src/audio_start_volume.cpp
    src/audio_voice_properties.cpp
    src/keyboard_context.cpp
    src/player_log.cpp
    src/controller_prompts.cpp
    src/menu_affordances.cpp
    src/build_label.cpp
    src/keyboard_prompt.cpp
    src/ui_font.cpp
    src/busy_panel.cpp
    src/restart_prompt.cpp
    src/update_prompt.cpp
    src/exit_game.cpp
    src/gpu_wait_hook.cpp
    src/gpu_wait_backoff.cpp
    src/menu_pointer.cpp
    src/menu_hover_animation.cpp
    src/frontend_patch.cpp
    src/leaderboards.cpp
    src/pc_menu.cpp
    src/pc_menu_patch.cpp
    src/pc_settings_store.cpp
    src/local_session_search.cpp
    src/lan_discovery.cpp
    src/multiplayer_menu_patch.cpp
    src/multiplayer_options.cpp
    src/multiplayer_settings.cpp
    src/multiplayer_ping.cpp
)

# The release version: the title screen (src/build_label.cpp), the build label
# and the executable's file version (src/outrun.rc.in).
set(OUTRUN_VERSION "0.9.0")
string(REPLACE "." "," OUTRUN_VERSION_COMMAS "${OUTRUN_VERSION}")
if(WIN32)
    enable_language(RC)
    configure_file(src/outrun.rc.in "${CMAKE_CURRENT_BINARY_DIR}/outrun.rc" @ONLY)
    add_executable(outrun WIN32 ${OUTRUN_SOURCES})
    target_sources(outrun PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/outrun.rc")
else()
    add_executable(outrun ${OUTRUN_SOURCES})
endif()

rexglue_setup_target(outrun GPU_PLUGINS xenos)
target_link_libraries(outrun PRIVATE ws2_32 iphlpapi xinput9_1_0 winhttp bcrypt)
# Player log (src/player_log.cpp): adapter and driver, controller names, F8 screenshots.
target_link_libraries(outrun PRIVATE dxgi dinput8 dxguid gdiplus)

# Help & Options > Check for Updates (src/outrun_update.h, docs/UPDATES.md): the
# GitHub repository ("owner/name") whose latest release is offered, and the
# release ZIP's name before its version. Keep both equal to
# setup/portable/Updater.cs. An empty repository leaves only the
# OUTRUN_UPDATE_API test override.
set(OUTRUN_UPDATE_REPO "CaiusCodes/Outrun-Arcade-Online-XBLA-PC-Recomp" CACHE STRING "GitHub owner/name for Check for Updates")
set(OUTRUN_UPDATE_ZIP_PREFIX "Outrun-Arcade-Online-XBLA-PC-Recomp-v" CACHE STRING "Release ZIP name before its version")
target_compile_definitions(outrun PRIVATE "OUTRUN_UPDATE_REPO=\"${OUTRUN_UPDATE_REPO}\""
    "OUTRUN_UPDATE_ZIP_PREFIX=\"${OUTRUN_UPDATE_ZIP_PREFIX}\"")

# Title screen build label (src/build_label.cpp): version, commit count, hash and
# date, refreshed at every build (tools/BuildInfo.cmake; rewritten only on change).
# OUTRUN_VERSION is set above, with the executable's version resource.
set(_or_label "${CMAKE_CURRENT_BINARY_DIR}/generated_include/outrun_build_label.h")
add_custom_target(outrun_build_label
    COMMAND ${CMAKE_COMMAND} "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}" "-DOUT=${_or_label}"
        "-DVERSION=${OUTRUN_VERSION}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/BuildInfo.cmake"
    BYPRODUCTS "${_or_label}")
add_dependencies(outrun outrun_build_label)
target_compile_definitions(outrun PRIVATE "OUTRUN_VERSION_TEXT=\"${OUTRUN_VERSION}\"")
target_include_directories(outrun PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated_include")

# Native renderer (gpu_native, src/native_gpu.cpp; native/README.md): Plume (MIT)
# for Direct3D 12 and XenosRecomp's shader recompiler (MIT) with Microsoft's DXC
# (dxcompiler.dll + dxil.dll, redistributable). Upstream clones in
# out/dependencies (tools/Get-NativeRendererDeps.ps1) with patches/plume.patch and
# patches/xenosrecomp.patch applied. The native-probe research hooks
# (src/native_probe.cpp) overlap these hooks and are not built with them.
option(OUTRUN_NATIVE_RENDERER "Build the native Plume renderer into outrun.exe" ON)
if(WIN32 AND OUTRUN_NATIVE_RENDERER)
    set(OUTRUN_PLUME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/out/dependencies/plume" CACHE PATH "Plume checkout")
    set(OUTRUN_XENOSRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/out/dependencies/XenosRecomp" CACHE PATH "XenosRecomp checkout")
    if(NOT EXISTS "${OUTRUN_PLUME_DIR}/plume_d3d12.cpp" OR
       NOT EXISTS "${OUTRUN_XENOSRECOMP_DIR}/thirdparty/dxc-bin/bin/x64/dxcompiler.dll")
        message(FATAL_ERROR "Native renderer libraries missing: run tools/Get-NativeRendererDeps.ps1, "
                            "or configure with -DOUTRUN_NATIVE_RENDERER=OFF")
    endif()
    # Its own object library, so the libraries' include paths and definitions
    # stay off the generated code (no full rebuild when the renderer changes).
    add_library(outrun_native OBJECT src/native_gpu.cpp src/native_shaders.cpp src/native_vk_output.cpp)
    target_compile_features(outrun_native PRIVATE cxx_std_23)
    target_include_directories(outrun_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${REXGLUE_ENTRYPOINT_INCLUDE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/generated_include")
    target_link_libraries(outrun_native PRIVATE rex::runtime)
    rexglue_apply_target_settings(outrun_native)
    target_link_libraries(outrun PRIVATE outrun_native)
    target_compile_definitions(outrun PRIVATE OUTRUN_HAS_NATIVE=1)  # (build_label.cpp names the renderer)
    # Direct3D 12 and Vulkan backends: the renderer uses the API the SDK presenter
    # runs on (gpu = d3d12 or vulkan; native/README.md).
    add_library(outrun_plume STATIC
        "${OUTRUN_PLUME_DIR}/plume_d3d12.cpp"
        "${OUTRUN_PLUME_DIR}/plume_vulkan.cpp"
        "${OUTRUN_PLUME_DIR}/contrib/D3D12MemoryAllocator/src/D3D12MemAlloc.cpp")
    target_include_directories(outrun_plume PUBLIC
        "${OUTRUN_PLUME_DIR}"
        "${OUTRUN_PLUME_DIR}/contrib/D3D12MemoryAllocator/include"
        "${OUTRUN_PLUME_DIR}/contrib/volk"
        "${OUTRUN_PLUME_DIR}/contrib/VulkanMemoryAllocator/include")
    # Plume's Vulkan headers ahead of the SDK's older copy, in Plume and in the
    # renderer (which also includes the SDK's Vulkan presenter headers).
    target_include_directories(outrun_plume BEFORE PUBLIC "${OUTRUN_PLUME_DIR}/contrib/Vulkan-Headers/include")
    target_include_directories(outrun_native BEFORE PRIVATE "${OUTRUN_PLUME_DIR}/contrib/Vulkan-Headers/include")
    # The SDK's Vulkan presenter headers include renderdoc_app.h (installed in include/renderdoc).
    set_property(SOURCE src/native_vk_output.cpp APPEND PROPERTY INCLUDE_DIRECTORIES "${OUTRUN_SDK_PREFIX}/include/renderdoc")
    target_compile_definitions(outrun_plume PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    # The SDK already links volk (volk.lib): Plume's Vulkan backend uses that copy.
    target_compile_definitions(outrun_plume PRIVATE PLUME_EXTERNAL_VOLK)
    target_compile_options(outrun_plume PRIVATE -w)
    target_link_libraries(outrun_plume PUBLIC d3d12 dxgi dxguid d3dcompiler)
    target_link_libraries(outrun_native PRIVATE outrun_plume)
    # Guest memory is read inside __try blocks that must catch a bad page.
    set_property(SOURCE src/native_gpu.cpp APPEND PROPERTY COMPILE_OPTIONS -fasync-exceptions)
    set_property(SOURCE src/native_gpu.cpp src/native_shaders.cpp APPEND PROPERTY COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS)
    set(_or_xr "${OUTRUN_XENOSRECOMP_DIR}/XenosRecomp")
    set(_or_dxc "${OUTRUN_XENOSRECOMP_DIR}/thirdparty/dxc-bin")
    add_library(outrun_xenosrecomp STATIC "${_or_xr}/shader_recompiler.cpp")
    target_include_directories(outrun_xenosrecomp PUBLIC
        "${_or_xr}"
        "${_or_dxc}/inc"
        "${OUTRUN_XENOSRECOMP_DIR}/thirdparty/smol-v/source"
        "${OUTRUN_XENOSRECOMP_DIR}/thirdparty/zstd/lib"
        "${OUTRUN_XENOSRECOMP_DIR}/thirdparty/xxHash")
    target_precompile_headers(outrun_xenosrecomp PRIVATE "${_or_xr}/pch.h")
    target_compile_definitions(outrun_xenosrecomp PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    target_compile_options(outrun_xenosrecomp PRIVATE -w -fms-extensions)
    target_link_libraries(outrun_xenosrecomp PUBLIC fmt::fmt)
    target_link_libraries(outrun_native PRIVATE outrun_xenosrecomp)
    # XenosRecomp's shared definitions, prepended to every translated shader.
    file(READ "${_or_xr}/shader_common.h" _or_shader_common)
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated_include/outrun_shader_common.inc"
        CONTENT "// Generated from XenosRecomp/shader_common.h by CMakeLists.txt.\nR\"ORSC(@_or_shader_common@)ORSC\"\n"
        @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_or_xr}/shader_common.h")
    # native_pipelines.bin: the pipelines of the title, menus, car select and the
    # first stages (shader hashes and render state only), made in the background
    # at boot so a first race does not compile them (native/README.md).
    add_custom_command(TARGET outrun POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_or_dxc}/bin/x64/dxcompiler.dll" "${_or_dxc}/bin/x64/dxil.dll"
            "${CMAKE_CURRENT_SOURCE_DIR}/native/native_pipelines.bin" "$<TARGET_FILE_DIR:outrun>")
    # Settings > HUD = HD: the redrawn HUD art (tools/hud-art/make_hud_art.py), beside the exe in hud.
    add_custom_command(TARGET outrun POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/native/hud" "$<TARGET_FILE_DIR:outrun>/hud")
endif()

if(BUILD_TESTING)
    add_executable(gpu_wait_backoff_test tests/gpu_wait_backoff_test.cpp src/gpu_wait_hook.cpp)
    target_include_directories(gpu_wait_backoff_test PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(gpu_wait_backoff_test PRIVATE rex::runtime)
    add_test(NAME gpu_wait_backoff_scope COMMAND gpu_wait_backoff_test)
    add_executable(gpu_wait_host_test tests/gpu_wait_host_test.cpp src/gpu_wait_backoff.cpp)
    target_include_directories(gpu_wait_host_test PRIVATE src)
    add_test(NAME gpu_wait_host_timer COMMAND gpu_wait_host_test)
    set_tests_properties(gpu_wait_host_timer PROPERTIES TIMEOUT 5)
    add_executable(audio_declick_test tests/audio_declick_test.cpp)
    # Exercise the exact local SDK driver with process-local SDL virtual devices.
    # Installed-SDK-only builds do not require a source checkout for this test.
    set(_sdl_driver_source "${CMAKE_CURRENT_SOURCE_DIR}/out/dependencies/rexglue-sdk/src/input/sdl/sdl_input_driver.cpp")
    if(EXISTS "${_sdl_driver_source}")
        add_executable(sdl_controller_polling_test tests/sdl_controller_polling_test.cpp "${_sdl_driver_source}")
        target_link_libraries(sdl_controller_polling_test PRIVATE rex::runtime)
        add_test(NAME sdl_controller_worker COMMAND sdl_controller_polling_test)
        set_tests_properties(sdl_controller_worker PROPERTIES TIMEOUT 20)
    endif()
    add_executable(runtime_shortcuts_test tests/runtime_shortcuts_test.cpp)
    target_include_directories(runtime_shortcuts_test PRIVATE src)
    target_link_libraries(runtime_shortcuts_test PRIVATE rex::runtime)
    add_test(NAME runtime_shortcut_gate COMMAND runtime_shortcuts_test)
    add_executable(frame_timing_log_test tests/frame_timing_log_test.cpp)
    target_include_directories(frame_timing_log_test PRIVATE src)
    target_link_libraries(frame_timing_log_test PRIVATE rex::runtime)
    add_test(NAME frame_timing_idle_stream COMMAND frame_timing_log_test)
    add_executable(audio_start_volume_test tests/audio_start_volume_test.cpp src/audio_start_volume.cpp)
    target_include_directories(audio_start_volume_test PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(audio_start_volume_test PRIVATE rex::runtime)
    add_test(NAME audio_initial_volume_order COMMAND audio_start_volume_test)
    add_executable(steering_audio_test tests/steering_audio_test.cpp src/steering_audio.cpp)
    target_include_directories(steering_audio_test PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(steering_audio_test PRIVATE rex::runtime)
    add_test(NAME steering_audio_scope COMMAND steering_audio_test)
    add_executable(audio_loop_suppression_test tests/audio_loop_suppression_test.cpp src/audio_loop_suppression.cpp)
    target_include_directories(audio_loop_suppression_test PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(audio_loop_suppression_test PRIVATE rex::runtime)
    add_test(NAME audio_loop_suppression COMMAND audio_loop_suppression_test)
    add_executable(audio_transition_trace_test tests/audio_transition_trace_test.cpp src/audio_transition_trace.cpp)
    target_include_directories(audio_transition_trace_test PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(audio_transition_trace_test PRIVATE rex::runtime)
    add_test(NAME audio_transition_passthrough COMMAND audio_transition_trace_test)
    add_executable(audio_engine_attack_test tests/audio_engine_attack_test.cpp)
    target_include_directories(audio_engine_attack_test PRIVATE src)
    add_test(NAME audio_engine_attack COMMAND audio_engine_attack_test)
    add_executable(audio_sample_mute_test tests/audio_sample_mute_test.cpp src/audio_sample_mute.cpp src/audio_diagnostics.cpp)
    target_include_directories(audio_sample_mute_test PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(audio_sample_mute_test PRIVATE rex::runtime)
    add_test(NAME audio_sample_isolation COMMAND audio_sample_mute_test)
    target_include_directories(audio_declick_test PRIVATE src)
    add_test(NAME audio_declick COMMAND audio_declick_test)
    add_executable(audio_pcm_capture_test tests/audio_pcm_capture_test.cpp src/audio_pcm_capture.cpp src/audio_diagnostics.cpp)
    target_include_directories(audio_pcm_capture_test PRIVATE src)
    target_link_libraries(audio_pcm_capture_test PRIVATE rex::runtime)
    add_test(NAME audio_pcm_capture COMMAND audio_pcm_capture_test)
    add_test(NAME audio_pcm_coasting_capture COMMAND audio_pcm_capture_test --coast)
    add_test(NAME audio_pcm_rolling_capture COMMAND audio_pcm_capture_test --rolling)
    add_executable(audio_notification_isolation_test tests/audio_notification_isolation_test.cpp src/audio_notification_isolation.cpp src/audio_start_volume.cpp)
    target_include_directories(audio_notification_isolation_test PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(audio_notification_isolation_test PRIVATE rex::runtime)
    add_test(NAME audio_notification_isolation COMMAND audio_notification_isolation_test)
    add_executable(audio_voice_properties_test tests/audio_voice_properties_test.cpp src/audio_voice_properties.cpp)
    target_include_directories(audio_voice_properties_test PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(audio_voice_properties_test PRIVATE rex::runtime)
    add_test(NAME audio_voice_properties COMMAND audio_voice_properties_test)
    add_executable(menu_pointer_geometry_test tests/menu_pointer_geometry_test.cpp)
    target_include_directories(menu_pointer_geometry_test PRIVATE src)
    add_test(NAME menu_pointer_geometry COMMAND menu_pointer_geometry_test)
    add_executable(menu_pointer_protocol_test tests/menu_pointer_protocol_test.cpp src/menu_pointer.cpp)
    target_include_directories(menu_pointer_protocol_test PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(menu_pointer_protocol_test PRIVATE rex::runtime)
    add_test(NAME menu_pointer_protocol COMMAND menu_pointer_protocol_test)
    add_executable(controller_prompts_test tests/controller_prompts_test.cpp src/controller_prompts.cpp)
    target_include_directories(controller_prompts_test PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(controller_prompts_test PRIVATE rex::runtime)
    add_test(NAME controller_prompt_visibility COMMAND controller_prompts_test)
    # Manual probe for the isolated aspect runtime; not a default regression
    # until its SDK correction has been validated and promoted.
    add_executable(video_aspect_probe tests/video_aspect_probe.cpp)
    target_link_libraries(video_aspect_probe PRIVATE rex::runtime)
    add_executable(menu_hover_test tests/menu_hover_test.cpp src/menu_pointer.cpp)
    target_include_directories(menu_hover_test PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(menu_hover_test PRIVATE rex::runtime)
    add_test(NAME menu_hover_selection COMMAND menu_hover_test)
    add_executable(audio_trace_budget_test tests/audio_trace_budget_test.cpp)
    target_include_directories(audio_trace_budget_test PRIVATE src)
    add_test(NAME audio_trace_late_event_budget COMMAND audio_trace_budget_test)
    add_executable(audio_queue_trace_test tests/audio_queue_trace_test.cpp src/audio_queue_trace.cpp)
    target_include_directories(audio_queue_trace_test PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(audio_queue_trace_test PRIVATE rex::runtime)
    add_test(NAME audio_queue_trace_passthrough COMMAND audio_queue_trace_test)
    add_executable(menu_hover_animation_test tests/menu_hover_animation_test.cpp src/menu_pointer.cpp src/menu_hover_animation.cpp)
    target_include_directories(menu_hover_animation_test PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(menu_hover_animation_test PRIVATE rex::runtime)
    add_test(NAME menu_hover_animation_isolation COMMAND menu_hover_animation_test)
    add_executable(multiplayer_settings_test tests/multiplayer_settings_test.cpp src/multiplayer_settings.cpp)
    add_executable(first_save_test tests/first_save_test.cpp src/frontend_patch.cpp)
    target_include_directories(first_save_test PRIVATE src)
    target_link_libraries(first_save_test PRIVATE rex::runtime)
    add_test(NAME first_save_preservation COMMAND first_save_test)
    add_executable(empty_leaderboard_test tests/empty_leaderboard_test.cpp)
    target_include_directories(empty_leaderboard_test PRIVATE src)
    add_test(NAME empty_leaderboard COMMAND empty_leaderboard_test)
    target_include_directories(multiplayer_settings_test PRIVATE src)
    add_test(NAME multiplayer_settings COMMAND multiplayer_settings_test)
    add_executable(multiplayer_ping_test tests/multiplayer_ping_test.cpp src/multiplayer_ping.cpp)
    target_include_directories(multiplayer_ping_test PRIVATE src)
    target_link_libraries(multiplayer_ping_test PRIVATE ws2_32)
    add_test(NAME multiplayer_ping COMMAND multiplayer_ping_test)
    set_tests_properties(multiplayer_ping PROPERTIES TIMEOUT 12)
    add_executable(local_session_search_test tests/local_session_search_test.cpp)
    target_include_directories(local_session_search_test PRIVATE src)
    add_test(NAME local_session_search COMMAND local_session_search_test)
    add_executable(lan_discovery_test tests/lan_discovery_test.cpp src/lan_discovery.cpp)
    target_include_directories(lan_discovery_test PRIVATE src)
    target_link_libraries(lan_discovery_test PRIVATE ws2_32 iphlpapi)
    add_test(NAME lan_discovery COMMAND lan_discovery_test)
    set_tests_properties(lan_discovery PROPERTIES TIMEOUT 12)
    add_executable(pc_settings_store_test tests/pc_settings_store_test.cpp src/pc_settings_store.cpp)
    target_include_directories(pc_settings_store_test PRIVATE src)
    add_test(NAME pc_settings_persistence COMMAND pc_settings_store_test)
    add_executable(renderer_work_queue_test tests/renderer_work_queue_test.cpp)
    target_include_directories(renderer_work_queue_test PRIVATE src)
    add_test(NAME renderer_work_queue COMMAND renderer_work_queue_test)
    set_tests_properties(renderer_work_queue PROPERTIES TIMEOUT 10)
endif()

# Keep optimized builds diagnosable. PDBs stay in the build tree, outside the setup payload.
if(OUTRUN_SPATIAL_FILTERS)
    add_executable(spatial_filters_test EXCLUDE_FROM_ALL tests/spatial_filters_test.cpp)
    target_link_libraries(spatial_filters_test PRIVATE rex::runtime)
    target_include_directories(spatial_filters_test PRIVATE out/dependencies/rexglue-sdk/thirdparty/renderdoc out/dependencies/rexglue-sdk/thirdparty/vulkan-headers/include)
endif()

if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(outrun PRIVATE -g -gcodeview)
    target_link_options(outrun PRIVATE "LINKER:/DEBUG")
endif()
