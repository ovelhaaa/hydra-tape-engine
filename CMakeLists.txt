cmake_minimum_required(VERSION 3.20)
project(hydra_tape_engine LANGUAGES C CXX)

option(BUILD_WEB "Build WebAssembly artifacts with Emscripten" OFF)
option(BUILD_TESTING "Build tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

  add_executable(hydra_core_smoke tests/core/smoke_core.cpp)
  target_link_libraries(hydra_core_smoke PRIVATE hydra_dsp_core)
  add_test(NAME core_smoke COMMAND hydra_core_smoke)

  add_executable(hydra_core_regression tests/core/regression_core.cpp)
  target_link_libraries(hydra_core_regression PRIVATE hydra_dsp_core)
  add_test(NAME core_regression COMMAND hydra_core_regression)

  add_test(
    NAME core_abi_symbols
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/core/abi_symbols_check.py
            --nm ${CMAKE_NM}
            --library $<TARGET_FILE:hydra_dsp_core>
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
    "SHELL:-sMODULARIZE=1"
    "SHELL:-sEXPORT_ES6=1"
    "SHELL:-sALLOW_MEMORY_GROWTH=1"
    "SHELL:-sENVIRONMENT=web,worker"
    "SHELL:-sEXPORTED_FUNCTIONS=['_hydra_dsp_get_api_version','_hydra_dsp_get_param_count','_hydra_dsp_get_param_specs','_hydra_dsp_get_param_spec','_hydra_dsp_create','_hydra_dsp_destroy','_hydra_dsp_prepare','_hydra_dsp_reset','_hydra_dsp_set_parameter','_hydra_dsp_get_parameter','_hydra_dsp_set_params','_hydra_dsp_commit','_hydra_dsp_process','_malloc','_free']"
    "SHELL:-sEXPORTED_RUNTIME_METHODS=['cwrap']"
  )

  set(HYDRA_WEB_DIR "${CMAKE_BINARY_DIR}/web")
  add_custom_command(
    TARGET hydra_dsp_web POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${HYDRA_WEB_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/host/web/index.html"
      "${CMAKE_CURRENT_SOURCE_DIR}/host/web/main.js"
      "${CMAKE_CURRENT_SOURCE_DIR}/host/web/hydra-processor.js"
      "${HYDRA_WEB_DIR}"
    VERBATIM
  )

  if(BUILD_TESTING)
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
    add_test(
      NAME native_vs_wasm_equivalence
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/core/compare_native_wasm.py
              --native $<TARGET_FILE:hydra_render_trace_native>
              --wasm-js $<TARGET_FILE:hydra_render_trace_wasm>
              --max-abs-threshold 1e-5
              --rmse-threshold 1e-6
    )

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
