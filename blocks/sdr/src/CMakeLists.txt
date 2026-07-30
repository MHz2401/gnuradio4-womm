add_executable(ex_RTL2832_capture ex_RTL2832_capture.cpp)
target_link_libraries(
  ex_RTL2832_capture
  PRIVATE gnuradio-options
          gnuradio-core
          gnuradio-blocklib-core
          gr-sdr
          gr-testing)

if(EMSCRIPTEN)
  set(RTL2832_SHELL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/RTL2832_shell.html")

  target_compile_options(ex_RTL2832_capture PRIVATE -Wno-error)
  target_link_options(
    ex_RTL2832_capture
    PRIVATE
    "SHELL:--shell-file ${RTL2832_SHELL_FILE}"
    "SHELL:-s ASYNCIFY"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','HEAPU8','wasmMemory']"
    "SHELL:-s EXPORTED_FUNCTIONS=['_main','_malloc','_free','_rtl2832_getQueueBuf','_rtl2832_getQueueHead','_rtl2832_getQueueTail','_rtl2832_getQueueCapacity','_rtl2832_getQueueMask','_gr_requestAllPermissions','_rtl2832Connect','_startRTL2832Capture','_stopRTL2832Capture']"
    --bind)
  set_target_properties(ex_RTL2832_capture PROPERTIES SUFFIX ".html")

  add_custom_target(
    serve_rtl2832
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "  RTL2832 capture example served at:"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "    http://localhost:8081/ex_RTL2832_capture.html"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "  Open in Chrome or Edge [WebUSB required]."
    COMMAND ${CMAKE_COMMAND} -E echo "  Press Ctrl+C to stop the server."
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/serve_coop.py 8081 ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ex_RTL2832_capture
    COMMENT "Serving RTL2832 WASM example on http://localhost:8081/"
    VERBATIM USES_TERMINAL)
endif()

# womm: end-to-end B210 rate sweep (see RESULTS.md)
add_executable(womm_b210_sweep womm_b210_sweep.cpp)
target_link_libraries(
  womm_b210_sweep
  PRIVATE gnuradio-options
          gnuradio-core
          gnuradio-blocklib-core
          gr-sdr
          gr-math
          gr-testing)

# womm: B_max - spare scheduler capacity while radios hold a hard deadline (see RESULTS.md)
add_executable(womm_bmax womm_bmax.cpp)
target_link_libraries(
  womm_bmax
  PRIVATE gnuradio-options
          gnuradio-core
          gnuradio-blocklib-core
          gr-sdr
          gr-math
          gr-testing)

# womm: hold-open two-channel RX - eyes-on proof that both channels stream (see RESULTS.md)
add_executable(womm_rx_hold womm_rx_hold.cpp)
target_link_libraries(
  womm_rx_hold
  PRIVATE gnuradio-options
          gnuradio-core
          gnuradio-blocklib-core
          gr-sdr
          gr-fileio
          gr-testing)

# RECEIVE-ONLY GATE. The harness must be structurally incapable of transmitting, not
# merely written so as not to. Assert on the LINKED BINARY that no SoapySink symbol
# is present - a comment or a code review cannot make that guarantee, and a future
# edit that pulls in a transmit path fails the build here rather than on the air.
add_custom_command(
  TARGET womm_bmax
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -DTARGET_FILE=$<TARGET_FILE:womm_bmax> -P
    ${CMAKE_CURRENT_SOURCE_DIR}/assert_no_tx.cmake
  COMMENT "womm: verifying womm_bmax links no transmit path")

add_custom_command(
  TARGET womm_rx_hold
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -DTARGET_FILE=$<TARGET_FILE:womm_rx_hold> -P
    ${CMAKE_CURRENT_SOURCE_DIR}/assert_no_tx.cmake
  COMMENT "womm: verifying womm_rx_hold links no transmit path")

# womm: REVERSIBLE EXPERIMENT - is multi-process necessary? N radios x 2 channels
# in ONE graph and ONE scheduler, against the 122.88 MS/s taken with N processes.
# Delete this block and womm_mt_test.cpp to revert; nothing else depends on it.
add_executable(womm_mt_test womm_mt_test.cpp)
target_link_libraries(
  womm_mt_test
  PRIVATE gnuradio-options
          gnuradio-core
          gnuradio-blocklib-core
          gr-sdr
          gr-fileio
          gr-testing)

add_custom_command(
  TARGET womm_mt_test
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -DTARGET_FILE=$<TARGET_FILE:womm_mt_test> -P
    ${CMAKE_CURRENT_SOURCE_DIR}/assert_no_tx.cmake
  COMMENT "womm: verifying womm_mt_test links no transmit path")

# womm: S2-3 - operations under load. Injects one named operation at a known instant
# and records what the driver sensed either side of it, attributed on the device clock.
add_executable(womm_ops womm_ops.cpp)
target_link_libraries(
  womm_ops
  PRIVATE gnuradio-options
          gnuradio-core
          gnuradio-blocklib-core
          gr-sdr
          gr-testing)

add_custom_command(
  TARGET womm_ops
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -DTARGET_FILE=$<TARGET_FILE:womm_ops> -P
    ${CMAKE_CURRENT_SOURCE_DIR}/assert_no_tx.cmake
  COMMENT "womm: verifying womm_ops links no transmit path")

# womm: PoC — the two-node flowgraph with NO device in the loop. Separates "gr4 works"
# from "gr4 works with our radios", which nothing else here does.
add_executable(womm_poc_nograph womm_poc_nograph.cpp)
target_link_libraries(
  womm_poc_nograph
  PRIVATE gnuradio-options
          gnuradio-core
          gnuradio-blocklib-core
          gr-testing)
