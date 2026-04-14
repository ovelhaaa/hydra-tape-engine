cmake_minimum_required(VERSION 3.20)
project(hydra_tape_engine LANGUAGES C CXX)

option(BUILD_WEB "Build WebAssembly artifacts with Emscripten" OFF)

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
    "SHELL:-sALLOW_MEMORY_GROWTH=1"
    "SHELL:-sENVIRONMENT=web,worker"
    "SHELL:-sEXPORTED_FUNCTIONS=['_hydra_dsp_create','_hydra_dsp_destroy','_hydra_dsp_prepare','_hydra_dsp_reset','_hydra_dsp_set_parameter','_hydra_dsp_get_parameter','_hydra_dsp_commit','_hydra_dsp_process','_malloc','_free']"
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
endif()
