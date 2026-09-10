# TWO IMPLEMENTATIONS OF THE ESCAPE ORACLE'S CHECKER, COMPARED - Phase 55O.
#
# check-type-oracle.cmake's discipline, applied to the escape half:
# `TypeOracle.cpp --escape` walks the recorder in memory and runs the verdict
# table over two stub inferences; `tools/check/escape-oracle.py` parses the
# recording FILE and runs the same table. Neither is a transcription of the
# other and they share no arithmetic. This runs both over the same probe and
# makes the C++ side's counters the Python side's EXPECTATIONS, so a
# disagreement is a failure rather than two numbers nobody compared.
#
# THE FILE FORMAT IS WHAT SITS BETWEEN THEM: a recorder bug shows up in both
# and would agree with itself; a writer or parser bug shows up in exactly one.
#
#   -DEXE=    the type-oracle executable
#   -DPYTHON= the interpreter
#   -DSCRIPT= tools/check/escape-oracle.py
#   -DWORK=   a writable directory for the recording

set(_rec "${WORK}/escape-oracle-selftest.rec")

execute_process(
  COMMAND "${EXE}" --escape --out "${_rec}"
  OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
message(STATUS "${_out}${_err}")
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "the escape oracle's own self-test failed (exit ${_rc})")
endif()

# THE SAME SKIP THE EXECUTABLE TAKES, on its own word.
if(_out MATCHES "SKIPPED")
  message(STATUS "escape oracle: skipped - this build has no recording hook")
  return()
endif()

# `<stub> claimed A observed B unobserved C violations D sound E partial F pending G
#  imprecise H exact I unclaimed J inconclusive K mismatch L`, as the C++ side
# printed it. Anchored, because this file exists because two numbers that were
# never compared looked like agreement.
# CMAKE_MATCH_<n> STOPS AT 9. A twelve-field line therefore needs two anchored
# matches, each of at most nine groups; the first version of this file matched
# all twelve in one expression and read an EMPTY `mismatch`, which `NOT ""
# EQUAL 0` treated as a failure. Two halves, the shared prefix re-anchored.
set(_head "claimed ([0-9]+) observed ([0-9]+) unobserved ([0-9]+) violations ([0-9]+) sound ([0-9]+) partial ([0-9]+) pending ([0-9]+)")
set(_tail "imprecise ([0-9]+) exact ([0-9]+) unclaimed ([0-9]+) inconclusive ([0-9]+) mismatch ([0-9]+)")

function(_escape_tally stub prefix)
  if(NOT _out MATCHES "${stub} ${_head}")
    message(FATAL_ERROR "the executable did not report a ${stub} tally:\n${_out}")
  endif()
  set(${prefix}_claimed "${CMAKE_MATCH_1}" PARENT_SCOPE)
  set(${prefix}_observed "${CMAKE_MATCH_2}" PARENT_SCOPE)
  set(${prefix}_unobserved "${CMAKE_MATCH_3}" PARENT_SCOPE)
  set(${prefix}_violations "${CMAKE_MATCH_4}" PARENT_SCOPE)
  set(${prefix}_sound "${CMAKE_MATCH_5}" PARENT_SCOPE)
  set(${prefix}_partial "${CMAKE_MATCH_6}" PARENT_SCOPE)
  set(${prefix}_pending "${CMAKE_MATCH_7}" PARENT_SCOPE)
  if(NOT _out MATCHES "${stub} ${_head} ${_tail}")
    message(FATAL_ERROR "the ${stub} tally is missing its second half:\n${_out}")
  endif()
  set(${prefix}_imprecise "${CMAKE_MATCH_8}" PARENT_SCOPE)
  set(${prefix}_exact "${CMAKE_MATCH_9}" PARENT_SCOPE)
  # Past the ninth group: re-match the tail alone, anchored on the exact text
  # that preceded it so it cannot pick up the other stub's line.
  if(NOT _out MATCHES "pending ${CMAKE_MATCH_7} ${_tail}")
    message(FATAL_ERROR "the ${stub} tally's tail did not re-match:\n${_out}")
  endif()
  set(${prefix}_unclaimed "${CMAKE_MATCH_3}" PARENT_SCOPE)
  set(${prefix}_inconclusive "${CMAKE_MATCH_4}" PARENT_SCOPE)
  set(${prefix}_mismatch "${CMAKE_MATCH_5}" PARENT_SCOPE)
endfunction()

_escape_tally("all-confined" _c)
_escape_tally("all-escapes" _e)

# THE DELIBERATELY WRONG INFERENCE MUST HAVE BEEN CAUGHT, in a recording that
# observed something and left something unobserved. A checker reporting zero
# violations over an empty recording is the vacuous pass this guards against.
if(_c_violations LESS_EQUAL 0)
  message(FATAL_ERROR "all-confined produced ${_c_violations} violations - the checker caught nothing")
endif()
if(_c_observed LESS_EQUAL 0)
  message(FATAL_ERROR "the recording observed ${_c_observed} sites - nothing ran")
endif()
if(_c_unobserved LESS_EQUAL 0)
  message(FATAL_ERROR "the recording has ${_c_unobserved} unobserved claims - the probe's "
                      "never_called() should have left one")
endif()
if(NOT _c_mismatch EQUAL 0)
  message(FATAL_ERROR "the recorder's coordinates disagree with its own inventory: ${_c_mismatch} kind mismatch(es)")
endif()
# AND THE BOUNDED WALK IS THE ONE THAT MAKES "CONFINED" SAYABLE: unbounded must
# have reported strictly more.
if(NOT _out MATCHES "escaped bounded ([0-9]+) unbounded ([0-9]+)")
  message(FATAL_ERROR "the executable did not report the bounded/unbounded A/B:\n${_out}")
endif()
if(NOT CMAKE_MATCH_2 GREATER CMAKE_MATCH_1)
  message(FATAL_ERROR "--unbounded reported ${CMAKE_MATCH_2} escapes against bounded ${CMAKE_MATCH_1}; "
                      "the dead-window exclusion changed nothing, so it is not doing its job")
endif()

# --- the wrong one, with the C++ side's numbers as expectations ---------------
execute_process(
  COMMAND "${PYTHON}" "${SCRIPT}" --recording "${_rec}" --infer all-confined
          --name selftest --max-report 0
          --expect-claimed "${_c_claimed}"
          --expect-observed "${_c_observed}"
          --expect-unobserved "${_c_unobserved}"
          --expect-violations "${_c_violations}"
          --expect-sound "${_c_sound}"
          --expect-partial "${_c_partial}"
          --expect-pending "${_c_pending}"
          --expect-imprecise "${_c_imprecise}"
          --expect-exact "${_c_exact}"
          --expect-unclaimed "${_c_unclaimed}"
          --expect-inconclusive "${_c_inconclusive}"
  OUTPUT_VARIABLE _pyout ERROR_VARIABLE _pyerr RESULT_VARIABLE _pyrc)
message(STATUS "${_pyout}${_pyerr}")
if(NOT _pyrc EQUAL 0)
  message(FATAL_ERROR "the Python checker disagrees with the C++ one on all-confined")
endif()
# AND IT MUST NAME A SITE. A count that cannot say where is a number, not a
# bug report, and the plan asks for the name. Every violation is printed
# (--max-report 0) because the first ten are the top level's closures, whose
# function is named `<script>` and falls outside this expression's idea of a
# name; and one hand-computed violation is required BY NAME - `ret`'s object
# literal, the in-flight return value - so "names a site" means the right one.
if(NOT _pyout MATCHES "VIOLATION program [0-9a-f]+ function [0-9]+ \\([A-Za-z_-]+\\) pc [0-9]+")
  message(FATAL_ERROR "the checker counted violations without naming one:\n${_pyout}")
endif()
if(NOT _pyout MATCHES "VIOLATION program [0-9a-f]+ function [0-9]+ \\(ret\\) pc [0-9]+ kind obj: claimed confined, observed escaped 1/made 1 via temporaries:1")
  message(FATAL_ERROR "the checker did not name `ret`'s site with its route:\n${_pyout}")
endif()

# --- the trivial one, which must be sound and useless ------------------------
execute_process(
  COMMAND "${PYTHON}" "${SCRIPT}" --recording "${_rec}" --infer all-escapes
          --name selftest
          --expect-claimed "${_e_claimed}"
          --expect-observed "${_e_observed}"
          --expect-unobserved "${_e_unobserved}"
          --expect-violations 0
          --expect-sound 0
          --expect-partial "${_e_partial}"
          --expect-pending "${_e_pending}"
          --expect-imprecise "${_e_imprecise}"
          --expect-exact "${_e_exact}"
          --expect-unclaimed "${_e_unclaimed}"
          --expect-inconclusive "${_e_inconclusive}"
  OUTPUT_VARIABLE _pyout ERROR_VARIABLE _pyerr RESULT_VARIABLE _pyrc)
message(STATUS "${_pyout}${_pyerr}")
if(NOT _pyrc EQUAL 0)
  message(FATAL_ERROR "all-escapes is sound by construction and the checker says otherwise")
endif()
if(NOT _pyout MATCHES "PRECISION confined 0/")
  message(FATAL_ERROR "all-escapes must have zero precision:\n${_pyout}")
endif()

message(STATUS "escape oracle: ${_c_observed} observed sites, ${_c_unobserved} unobserved claims, "
               "all-confined ${_c_violations} violations, all-escapes 0, "
               "${_c_unclaimed} unclaimed")
