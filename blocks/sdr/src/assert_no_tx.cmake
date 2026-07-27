# Fail the build if a receive-only harness has linked a transmit path.
#
# A licensed operator carries the consequences of an emission, so "we did not mean
# to transmit" is not a control. This checks the artefact that actually runs.

if(NOT DEFINED TARGET_FILE)
  message(FATAL_ERROR "assert_no_tx.cmake: TARGET_FILE not set")
endif()

find_program(NM_EXECUTABLE nm)
if(NOT NM_EXECUTABLE)
  message(FATAL_ERROR "assert_no_tx.cmake: nm not found - cannot verify the receive-only property, refusing to pass silently")
endif()

execute_process(
  COMMAND ${NM_EXECUTABLE} -C ${TARGET_FILE}
  OUTPUT_VARIABLE symbols
  ERROR_VARIABLE nm_error
  RESULT_VARIABLE nm_result
  OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT nm_result EQUAL 0)
  message(FATAL_ERROR "assert_no_tx.cmake: nm failed on ${TARGET_FILE}: ${nm_error}")
endif()

string(REGEX MATCHALL "SoapySink|writeStream|SOAPY_SDR_TX" tx_symbols "${symbols}")
if(tx_symbols)
  list(REMOVE_DUPLICATES tx_symbols)
  message(FATAL_ERROR "assert_no_tx.cmake: ${TARGET_FILE} links a TRANSMIT path (${tx_symbols}). "
                      "This harness is receive-only by construction; if a transmit path is genuinely "
                      "wanted, that is a deliberate decision requiring the operator's licence and "
                      "explicit band/gain/duty-cycle review - not a build change.")
endif()
