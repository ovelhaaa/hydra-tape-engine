cmake_minimum_required(VERSION 3.20)
project(hydra_tape_engine LANGUAGES C CXX)

option(BUILD_WEB "Build WebAssembly artifacts with Emscripten" OFF)
option(BUILD_TESTING "Build tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep multiply/add rounding identical to scalar WASM. Ubuntu 26.04's default
# x86-64-v3 target enables FMA even without -ffast-math; contracting a*b+c
# changes filter/state bits and breaks the strict Native/WASM release gate.
# PlatformIO's separately validated ESP32 build is unaffected by this setting.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-ffp-contract=off>")
endif()

add_library(hydra_dsp_core STATIC
  dsp/core/tape_core.cpp
  dsp/core/hydra_dsp_c_api.cpp
)

target_include_directories(hydra_dsp_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/core
)

if(BUILD_TESTING)
  include(CTest)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME fast_tanh_integrity
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/core/fast_tanh_integrity.py)

  # Phase 7.1: scalar transfer-curve and playback shelf pole stability.
  add_executable(hydra_phase7_integrity tests/core/phase7_integrity_test.cpp)
  target_include_directories(hydra_phase7_integrity PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/dsp/core)
  add_test(NAME phase7_dsp_integrity COMMAND hydra_phase7_integrity)

  # Phase 7.2A experiment is compile-time only; production DSP is unchanged.
  add_executable(hydra_phase72_feedback_routing_test tests/core/phase72_feedback_routing_test.cpp)
  target_include_directories(hydra_phase72_feedback_routing_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/dsp/core)
  add_test(NAME phase72_feedback_routing_contract COMMAND hydra_phase72_feedback_routing_test)

  # Variant C is built and smoke-tested independently, because the normal
  # DSP and its Native/WASM goldens must keep the approved Phase 7.1 topology.
  add_executable(hydra_phase72_classic_core_smoke
    tests/core/smoke_core.cpp
    dsp/core/tape_core.cpp
    dsp/core/hydra_dsp_c_api.cpp
  )
  target_include_directories(hydra_phase72_classic_core_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/core
  )
  target_compile_definitions(hydra_phase72_classic_core_smoke PRIVATE
    HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1
  )
  add_test(NAME phase72_classic_core_smoke COMMAND hydra_phase72_classic_core_smoke)

  add_executable(hydra_core_smoke tests/core/smoke_core.cpp)
  target_link_libraries(hydra_core_smoke PRIVATE hydra_dsp_core)
  add_test(NAME core_smoke COMMAND hydra_core_smoke)

  add_executable(hydra_core_regression tests/core/regression_core.cpp)
  target_link_libraries(hydra_core_regression PRIVATE hydra_dsp_core)
  add_test(NAME core_regression COMMAND hydra_core_regression)

  add_executable(hydra_f2_wrap_contract tests/core/f2_wrap_contract.cpp)
  target_link_libraries(hydra_f2_wrap_contract PRIVATE hydra_dsp_core)
  add_test(NAME f2_physical_wrap_contract COMMAND hydra_f2_wrap_contract)

  add_executable(hydra_audible_controls tests/core/audible_controls.cpp)
  target_link_libraries(hydra_audible_controls PRIVATE hydra_dsp_core)
  add_test(NAME core_audible_controls COMMAND hydra_audible_controls)

  add_executable(hydra_warp_core_test
    tests/core/warp_core_test.cpp
    host/native/esp32/hydra_dsp_esp32_adapter.cpp
  )
  target_include_directories(hydra_warp_core_test PRIVATE host/native/esp32)
  target_link_libraries(hydra_warp_core_test PRIVATE hydra_dsp_core)
  add_test(NAME core_warp_test COMMAND hydra_warp_core_test)

  add_executable(hydra_reverse_core_test
    tests/core/reverse_core_test.cpp
    host/native/esp32/hydra_dsp_esp32_adapter.cpp
  )
  target_include_directories(hydra_reverse_core_test PRIVATE host/native/esp32)
  target_link_libraries(hydra_reverse_core_test PRIVATE hydra_dsp_core)
  add_test(NAME core_reverse_test COMMAND hydra_reverse_core_test)

  add_executable(
    hydra_transport_clock_test
    tests/core/transport_clock_test.cpp
    host/native/esp32/hydra_dsp_esp32_adapter.cpp
  )
  target_include_directories(hydra_transport_clock_test PRIVATE host/native/esp32)
  target_link_libraries(hydra_transport_clock_test PRIVATE hydra_dsp_core)
  add_test(NAME core_transport_clock_test COMMAND hydra_transport_clock_test)

  add_executable(
    hydra_tap_tempo_test
    tests/core/tap_tempo_test.cpp
    host/native/esp32/hydra_dsp_esp32_adapter.cpp
  )
  target_include_directories(hydra_tap_tempo_test PRIVATE host/native/esp32)
  target_link_libraries(hydra_tap_tempo_test PRIVATE hydra_dsp_core)
  add_test(NAME core_tap_tempo_test COMMAND hydra_tap_tempo_test)

  add_executable(
    hydra_midi_clock_test
    tests/core/midi_clock_test.cpp
    host/native/esp32/hydra_dsp_esp32_adapter.cpp
  )
  target_include_directories(hydra_midi_clock_test PRIVATE host/native/esp32)
  target_link_libraries(hydra_midi_clock_test PRIVATE hydra_dsp_core)
  add_test(NAME core_midi_clock_test COMMAND hydra_midi_clock_test)

  add_test(
    NAME web_param_ids
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/core/web_param_ids_check.py
  )

  add_test(
    NAME core_abi_symbols
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/core/abi_symbols_check.py
            --nm ${CMAKE_NM}
            --library $<TARGET_FILE:hydra_dsp_core>
  )

  if(NOT EMSCRIPTEN)
    # Build the consumer against the frozen header, then link it to the current shared library.
    add_library(hydra_abi_test_library SHARED
      dsp/core/tape_core.cpp
      dsp/core/hydra_dsp_c_api.cpp
    )
    target_include_directories(hydra_abi_test_library PRIVATE include dsp/core)
    set_target_properties(hydra_abi_test_library PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    add_executable(hydra_abi_legacy_consumer tests/core/abi_v1_2/consumer.c)
    target_link_libraries(hydra_abi_legacy_consumer PRIVATE hydra_abi_test_library)
    add_test(NAME core_abi_legacy_consumer COMMAND hydra_abi_legacy_consumer)

    add_executable(hydra_listen_native tests/core/listen.cpp)
    target_link_libraries(hydra_listen_native PRIVATE hydra_dsp_core)
    add_test(
      NAME core_transition_characterization
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/core/listening_review.py
              --native $<TARGET_FILE:hydra_listen_native>
              --characterize-only
              --output-dir ${CMAKE_BINARY_DIR}/transition_review
    )

    add_executable(hydra_render_trace_native tests/core/render_trace.cpp)
    target_link_libraries(hydra_render_trace_native PRIVATE hydra_dsp_core)

    add_executable(hydra_golden_regression_native tests/core/golden_regression.cpp)
    target_link_libraries(hydra_golden_regression_native PRIVATE hydra_dsp_core)
    add_test(
      NAME test_golden_delay_modes
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/core/golden_test.py
              --runner $<TARGET_FILE:hydra_golden_regression_native>
              --scenario delay_modes
              --fixtures-root ${CMAKE_SOURCE_DIR}/tests/fixtures
              --compare-script ${CMAKE_SOURCE_DIR}/tests/core/golden_compare.py
    )
    add_test(
      NAME test_golden_reset_state
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/core/golden_test.py
              --runner $<TARGET_FILE:hydra_golden_regression_native>
              --scenario reset_state
              --fixtures-root ${CMAKE_SOURCE_DIR}/tests/fixtures
              --compare-script ${CMAKE_SOURCE_DIR}/tests/core/golden_compare.py
    )
    add_test(
      NAME test_golden_param_automation
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/core/golden_test.py
              --runner $<TARGET_FILE:hydra_golden_regression_native>
              --scenario param_automation
              --fixtures-root ${CMAKE_SOURCE_DIR}/tests/fixtures
              --compare-script ${CMAKE_SOURCE_DIR}/tests/core/golden_compare.py
    )
  endif()
endif()

if(EMSCRIPTEN)
  add_link_options("SHELL:-sWASM_BIGINT=1")
endif()

if(BUILD_WEB)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "BUILD_WEB=ON requires Emscripten toolchain. Use emcmake cmake ...")
  endif()

  add_executable(hydra_dsp_web
    dsp/core/tape_core.cpp
    dsp/core/hydra_dsp_c_api.cpp
  )

  target_include_directories(hydra_dsp_web PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/core
  )

  set_target_properties(hydra_dsp_web PROPERTIES
    OUTPUT_NAME "hydra_dsp"
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web"
  )

  target_link_options(hydra_dsp_web PRIVATE
    "SHELL:--no-entry"
    "SHELL:-sWASM=1"
    "SHELL:-sWASM_BIGINT=1"
    "SHELL:-sMODULARIZE=1"
    "SHELL:-sEXPORT_ES6=1"
    "SHELL:-sALLOW_MEMORY_GROWTH=1"
    # Emscripten only accepts web/webview/worker/node/shell here. AudioWorklet
    # runs the generated ES module as web-compatible JS, not via a separate
    # `worklet` ENVIRONMENT token.
    "SHELL:-sENVIRONMENT=web,worker"
    "SHELL:-sEXPORTED_FUNCTIONS=['_hydra_dsp_get_api_version','_hydra_dsp_get_param_count','_hydra_dsp_get_param_specs','_hydra_dsp_get_param_spec','_hydra_dsp_get_param_spec_for_handle','_hydra_dsp_create','_hydra_dsp_create_v1_4','_hydra_dsp_destroy','_hydra_dsp_prepare','_hydra_dsp_reset','_hydra_dsp_set_parameter','_hydra_dsp_get_parameter','_hydra_dsp_get_reverse_status','_hydra_dsp_get_reverse_diagnostics','_hydra_dsp_transport_play','_hydra_dsp_transport_pause','_hydra_dsp_transport_stop','_hydra_dsp_transport_reset','_hydra_dsp_transport_locate','_hydra_dsp_transport_set_bpm','_hydra_dsp_transport_set_time_signature','_hydra_dsp_transport_get_state','_hydra_dsp_tap_tempo_event','_hydra_dsp_tap_tempo_get_state','_hydra_dsp_tap_tempo_reset','_hydra_dsp_midi_clock_event','_hydra_dsp_midi_clock_get_state','_hydra_dsp_midi_clock_reset','_hydra_dsp_set_tempo_source','_hydra_dsp_get_tempo_source','_hydra_dsp_set_params','_hydra_dsp_set_params_v1_2','_hydra_dsp_set_params_v1_3','_hydra_dsp_set_params_sized','_hydra_dsp_commit','_hydra_dsp_process','_malloc','_free']"
    "SHELL:-sEXPORTED_RUNTIME_METHODS=['cwrap','HEAP32','HEAPF32','HEAPU8']"
  )

  set(HYDRA_WEB_DIR "${CMAKE_BINARY_DIR}/web")
  add_custom_command(
    TARGET hydra_dsp_web POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${HYDRA_WEB_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/host/web/index.html"
      "${CMAKE_CURRENT_SOURCE_DIR}/host/web/main.js"
      "${CMAKE_CURRENT_SOURCE_DIR}/host/web/hydra-processor.js"
      "${CMAKE_CURRENT_SOURCE_DIR}/host/web/transportState.js"
      "${CMAKE_CURRENT_SOURCE_DIR}/host/web/transportController.js"
      "${CMAKE_CURRENT_SOURCE_DIR}/host/web/presetSerialization.js"
      "${CMAKE_CURRENT_SOURCE_DIR}/host/web/runtimeDiagnostics.js"
      "${CMAKE_CURRENT_SOURCE_DIR}/host/web/resetController.js"
      "${CMAKE_CURRENT_SOURCE_DIR}/host/web/webMidiTime.js"
      "${HYDRA_WEB_DIR}"
    VERBATIM
  )

  if(BUILD_TESTING)
    add_executable(hydra_listen_wasm tests/core/listen.cpp)
    target_link_libraries(hydra_listen_wasm PRIVATE hydra_dsp_core)
    set_target_properties(hydra_listen_wasm PROPERTIES
      OUTPUT_NAME "hydra_listen"
      SUFFIX ".js"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/wasm-tests"
    )
    target_link_options(hydra_listen_wasm PRIVATE "SHELL:-sENVIRONMENT=node" "SHELL:-sNODERAWFS=1")

    add_executable(hydra_render_trace_wasm
      dsp/core/tape_core.cpp
      dsp/core/hydra_dsp_c_api.cpp
      tests/core/render_trace.cpp
    )
    target_include_directories(hydra_render_trace_wasm PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/dsp/core
    )
    set_target_properties(hydra_render_trace_wasm PROPERTIES
      OUTPUT_NAME "hydra_render_trace"
      SUFFIX ".js"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/wasm-tests"
    )
    target_link_options(hydra_render_trace_wasm PRIVATE
      "SHELL:-sENVIRONMENT=node"
    )
    set(HYDRA_NATIVE_TRACE_RUNNER "" CACHE FILEPATH "Native hydra_render_trace_native executable for WASM comparison")
    if(HYDRA_NATIVE_TRACE_RUNNER)
      add_test(
        NAME native_vs_wasm_equivalence
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/core/compare_native_wasm.py
                --native "${HYDRA_NATIVE_TRACE_RUNNER}"
                --wasm-js $<TARGET_FILE:hydra_render_trace_wasm>
                --max-abs-threshold 1e-5
                --rmse-threshold 1e-6
      )

      add_test(
        NAME native_wasm_ab_review_package
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/core/build_ab_review_package.py
                --native "${HYDRA_NATIVE_TRACE_RUNNER}"
                --wasm-js $<TARGET_FILE:hydra_render_trace_wasm>
                --null-test-script ${CMAKE_SOURCE_DIR}/tests/core/null_test_wav.py
                --output-dir ${CMAKE_BINARY_DIR}/ab_review
                --max-abs-threshold 1e-5
                --rmse-threshold 1e-6
      )
    else()
      message(STATUS "Set HYDRA_NATIVE_TRACE_RUNNER to enable native/WASM comparisons")
    endif()

    add_executable(hydra_golden_regression_wasm
      dsp/core/tape_core.cpp
      dsp/core/hydra_dsp_c_api.cpp
      tests/core/golden_regression.cpp
    )
    target_include_directories(hydra_golden_regression_wasm PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/dsp/core
    )
    set_target_properties(hydra_golden_regression_wasm PROPERTIES
      OUTPUT_NAME "hydra_golden_regression"
      SUFFIX ".js"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/wasm-tests"
    )
    target_link_options(hydra_golden_regression_wasm PRIVATE
      "SHELL:-sENVIRONMENT=node"
      "SHELL:-sNODERAWFS=1"
    )
    add_test(
      NAME test_golden_delay_modes_wasm
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/core/golden_test.py
              --runner $<TARGET_FILE:hydra_golden_regression_wasm>
              --scenario delay_modes
              --fixtures-root ${CMAKE_SOURCE_DIR}/tests/fixtures
              --compare-script ${CMAKE_SOURCE_DIR}/tests/core/golden_compare.py
              --wasm
    )
    add_test(
      NAME test_golden_reset_state_wasm
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/core/golden_test.py
              --runner $<TARGET_FILE:hydra_golden_regression_wasm>
              --scenario reset_state
              --fixtures-root ${CMAKE_SOURCE_DIR}/tests/fixtures
              --compare-script ${CMAKE_SOURCE_DIR}/tests/core/golden_compare.py
              --wasm
    )
    add_test(
      NAME test_golden_param_automation_wasm
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/core/golden_test.py
              --runner $<TARGET_FILE:hydra_golden_regression_wasm>
              --scenario param_automation
              --fixtures-root ${CMAKE_SOURCE_DIR}/tests/fixtures
              --compare-script ${CMAKE_SOURCE_DIR}/tests/core/golden_compare.py
              --wasm
    )
  endif()
endif()
