# Fail the build if a binary that CAN transmit does not also link the arming interlock.
#
# The mirror image of assert_no_tx.cmake. That one proves a receive-only harness carries no
# transmit path; this one proves that a harness which does carry one cannot reach the air
# without an operator at a console.
#
# Checking the linked artefact rather than the source is the point: a transmit path added
# by a future edit, a refactor, or a header include fails here rather than on the air.

if(NOT DEFINED TARGET_FILE)
  message(FATAL_ERROR "assert_tx_gated.cmake: TARGET_FILE not set")
endif()

find_program(NM_EXECUTABLE nm)
if(NOT NM_EXECUTABLE)
  message(FATAL_ERROR "assert_tx_gated.cmake: nm not found - cannot verify the arming property, refusing to pass silently")
endif()

execute_process(
  COMMAND ${NM_EXECUTABLE} -C ${TARGET_FILE}
  OUTPUT_VARIABLE symbols
  ERROR_VARIABLE nm_error
  RESULT_VARIABLE nm_result
  OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT nm_result EQUAL 0)
  message(FATAL_ERROR "assert_tx_gated.cmake: nm failed on ${TARGET_FILE}: ${nm_error}")
endif()

string(REGEX MATCHALL "UhdSink|SoapySink|writeStream|SOAPY_SDR_TX" tx_symbols "${symbols}")
if(NOT tx_symbols)
  # Nothing here can transmit, so there is nothing to gate. Do not require the interlock in
  # a binary that has no use for it - that would only teach people to link it reflexively.
  return()
endif()

list(REMOVE_DUPLICATES tx_symbols)
string(FIND "${symbols}" "womm::txArm" arm_found)
if(arm_found LESS 0)
  message(
    FATAL_ERROR
      "assert_tx_gated.cmake: ${TARGET_FILE} links a TRANSMIT path (${tx_symbols}) but does NOT link "
      "the arming interlock womm::txArm. A transmit-capable binary must route every emission through "
      "womm_tx_arm.hpp, which prints the requested parameters and refuses to proceed unless stdin is a "
      "terminal with an operator at it. Add the interlock, or remove the transmit path.")
endif()
