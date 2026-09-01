# TWO IMPLEMENTATIONS OF THE TYPE ORACLE'S CHECKER, COMPARED - Phase 54B.
#
# `TypeOracle.cpp` walks the recorder in memory; `tools/check/type-oracle.py`
# parses the recording FILE. Neither is a transcription of the other and they
# share no arithmetic. This runs both over the same fixture and makes the C++
# side's counters the Python side's EXPECTATIONS, so a disagreement is a failure
# rather than two numbers nobody compared.
#
# THE FILE FORMAT IS WHAT SITS BETWEEN THEM, which is the reason to do it this
# way at all: a recorder bug shows up in both and would agree with itself; a
# writer or parser bug shows up in exactly one.
#
#   -DEXE=    the type-oracle executable
#   -DPYTHON= the interpreter
#   -DSCRIPT= tools/check/type-oracle.py
#   -DWORK=   a writable directory for the recording

set(_rec "${WORK}/type-oracle-selftest.rec")

execute_process(
  COMMAND "${EXE}" --out "${_rec}"
  OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
message(STATUS "${_out}${_err}")
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "the type oracle's own self-test failed (exit ${_rc})")
endif()

# THE SAME SKIP THE EXECUTABLE TAKES. CTBROWSER_SCRIPT_RECORD_TYPES=OFF leaves
# the interpreter with no recording hook, so there is no recording to compare
# two implementations over. Matched on the executable's own word rather than
# re-deriving the build flag here, which would be a second copy of the decision.
if(_out MATCHES "SKIPPED")
  message(STATUS "type oracle: skipped - this build has no recording hook")
  return()
endif()

# `all-i32 observed A unobserved B violations C beat-boxed D`, as the C++ side
# printed it. Matched with an anchored expression rather than a substring: this
# file exists because two numbers that were never compared looked like agreement.
if(NOT _out MATCHES "all-i32 observed ([0-9]+) unobserved ([0-9]+) violations ([0-9]+) beat-boxed ([0-9]+)")
  message(FATAL_ERROR "the executable did not report an all-i32 tally:\n${_out}")
endif()
set(_observed "${CMAKE_MATCH_1}")
set(_unobserved "${CMAKE_MATCH_2}")
set(_violations "${CMAKE_MATCH_3}")
set(_beat "${CMAKE_MATCH_4}")

# THE DELIBERATELY WRONG INFERENCE MUST HAVE BEEN CAUGHT, and there must have
# been something to catch it in. A checker reporting zero violations because the
# recording was empty is the vacuous pass this whole file guards against.
if(_violations LESS_EQUAL 0)
  message(FATAL_ERROR "all-i32 produced ${_violations} violations - the checker caught nothing")
endif()
if(_observed LESS_EQUAL 0)
  message(FATAL_ERROR "the recording observed ${_observed} registers - nothing ran")
endif()
if(_unobserved LESS_EQUAL 0)
  message(FATAL_ERROR "the recording has ${_unobserved} unobserved registers - the fixture's "
                      "never_called() should have left some")
endif()

# --- the wrong one, with the C++ side's numbers as expectations ---------------
execute_process(
  COMMAND "${PYTHON}" "${SCRIPT}" --recording "${_rec}" --infer all-i32
          --name selftest
          --expect-violations "${_violations}"
          --expect-observed "${_observed}"
          --expect-unobserved "${_unobserved}"
          --expect-precision "${_beat}"
  OUTPUT_VARIABLE _pyout ERROR_VARIABLE _pyerr RESULT_VARIABLE _pyrc)
message(STATUS "${_pyout}${_pyerr}")
if(NOT _pyrc EQUAL 0)
  message(FATAL_ERROR "the Python checker disagrees with the C++ one on all-i32")
endif()
# AND IT MUST NAME THE SITES. "Soundness violations: 4" that cannot say where is
# a number, not a bug report, and the plan asks for the name.
if(NOT _pyout MATCHES "VIOLATION program [0-9a-f]+ function [0-9]+ \\([A-Za-z_-]+\\) register [0-9]+")
  message(FATAL_ERROR "the checker counted violations without naming one:\n${_pyout}")
endif()

# --- the trivial one, which must be sound and useless ------------------------
execute_process(
  COMMAND "${PYTHON}" "${SCRIPT}" --recording "${_rec}" --infer all-boxed
          --name selftest
          --expect-violations 0
          --expect-observed "${_observed}"
          --expect-unobserved "${_unobserved}"
          --expect-precision 0
  OUTPUT_VARIABLE _pyout ERROR_VARIABLE _pyerr RESULT_VARIABLE _pyrc)
message(STATUS "${_pyout}${_pyerr}")
if(NOT _pyrc EQUAL 0)
  message(FATAL_ERROR "all-boxed is sound by construction and the checker says otherwise")
endif()

message(STATUS "type oracle: ${_observed} observed registers, ${_unobserved} unobserved, "
               "all-i32 ${_violations} soundness violations, all-boxed 0")
