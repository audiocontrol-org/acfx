# adapters/web — the browser adapter. One extern-"C" ABI over acfx::SvfEffect
# compiles two ways: to svf.wasm via Emscripten (EMSCRIPTEN set by the `web`
# preset toolchain), and to a native reference CLI + test via the host compiler
# (the `web-ref` preset). Depends only inward on acfx_core (Constitution VI).
message(STATUS "adapters/web: EMSCRIPTEN=${EMSCRIPTEN}")

if(NOT EMSCRIPTEN)
  # Host build: the native ABI library + its doctest. (web-ref preset.)
  add_library(acfx_web_abi STATIC svf-web-abi.cpp svf-web-analysis.cpp)
  target_include_directories(acfx_web_abi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(acfx_web_abi PUBLIC acfx_core)
  target_compile_features(acfx_web_abi PUBLIC cxx_std_20)

  add_executable(svf-reference svf-reference-main.cpp)
  target_link_libraries(svf-reference PRIVATE acfx_web_abi acfx_core)
  target_compile_features(svf-reference PRIVATE cxx_std_20)

  if(ACFX_BUILD_TESTS AND TARGET doctest)
    enable_testing()
    add_executable(acfx_web_abi_native_test test/svf-web-abi-native-test.cpp)
    target_link_libraries(acfx_web_abi_native_test PRIVATE acfx_web_abi acfx_core doctest)
    target_compile_definitions(acfx_web_abi_native_test PRIVATE DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
    add_test(NAME acfx_web_abi_native_test COMMAND acfx_web_abi_native_test)
  endif()
endif()

if(EMSCRIPTEN)
  # WASM build: one module from the ABI source. (web preset.)
  add_executable(svf svf-web-abi.cpp svf-web-analysis.cpp)
  target_link_libraries(svf PRIVATE acfx_core)
  target_compile_features(svf PRIVATE cxx_std_20)
  set_target_properties(svf PROPERTIES
    OUTPUT_NAME "svf"
    SUFFIX ".mjs"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  target_link_options(svf PRIVATE
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sENVIRONMENT=web,worker,node"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sEXPORTED_FUNCTIONS=['_svf_create','_svf_destroy','_svf_prepare','_svf_set_param','_svf_process','_svf_render_impulse','_svf_get_frequency_response','_svf_get_pole_zero','_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPF32','HEAP32']"
    "-sEXPORT_NAME=createSvfModule"
  )
endif()
