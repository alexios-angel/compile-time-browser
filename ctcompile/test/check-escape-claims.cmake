# THE ESCAPE ORACLE, CLOSED - Phase 55A checked by Phase 55O.
#
# check-type-claims.cmake's shape, over allocation sites instead of registers:
#
#   1. the INTERPRETER runs the corpus with the recorder installed and, at
#      every frame pop, asks its own collector whether each object the frame
#      allocated is still reachable         (ctcompile-test-type-oracle --script)
#   2. the COMPILER imports the same corpus, runs EscapeAnalysis, and writes
#      one claim per allocation site         (ctcompile-test-escape-claims)
#   3. tools/check/escape-oracle.py compares the two.
#
# ZERO SOUNDNESS VIOLATIONS IS THE GATE: a site claimed `confined` that the
# collector found reachable after its frame returned is a defect, and the
# checker names it. Precision is printed, never gated. The vacuous passes are
# guarded the same way as the type oracle's: something must have been
# observed, and at least one confined claim must have been proved SOUND - an
# analysis that says `escapes` everywhere is right the way a stopped clock is.
#
#   -DORACLE=  ctcompile-test-type-oracle     -DCLAIMS= ctcompile-test-escape-claims
#   -DPYTHON=  the interpreter                -DSCRIPT= tools/check/escape-oracle.py
#   -DCORPUS=  the JavaScript file            -DPREFIX= optional file prepended to it
#   -DWORK=    a writable directory           -DNAME=   the corpus's name in the report
#   -DBUDGET=  optional: checks per function before the recorder stops adjudicating
#   -DSTRICT=  ON for the fixture: partial and pending must both be zero

execute_process(COMMAND "${ORACLE}" OUTPUT_VARIABLE _self ERROR_VARIABLE _selferr RESULT_VARIABLE _rc)
if(_self MATCHES "SKIPPED")
  message(STATUS "escape claims (${NAME}): skipped - this build has no recording hook")
  return()
endif()
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "the type oracle's self-test failed before the escape claims ran:\n${_self}${_selferr}")
endif()

set(_corpus "${CORPUS}")
if(PREFIX)
  file(READ "${PREFIX}" _prefix)
  file(READ "${CORPUS}" _driver)
  set(_corpus "${WORK}/escape-claims-${NAME}.js")
  file(WRITE "${_corpus}" "${_prefix}\n${_driver}")
endif()
set(_rec "${WORK}/escape-claims-${NAME}.rec")
set(_claims "${WORK}/escape-claims-${NAME}.claims")

set(_budget_flag "")
if(BUDGET)
  set(_budget_flag --escape-budget "${BUDGET}")
endif()
execute_process(
  COMMAND "${ORACLE}" --script "${_corpus}" --out "${_rec}" ${_budget_flag}
  OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
message(STATUS "${_out}${_err}")
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "recording ${NAME} failed (exit ${_rc})")
endif()

execute_process(
  COMMAND "${CLAIMS}" --script "${_corpus}" --out "${_claims}"
  OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
message(STATUS "${_out}${_err}")
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "claiming ${NAME} failed (exit ${_rc})")
endif()
# A TRACKED SITE OR A SINK OPERAND IN A LIVE BLOCK THE SOLVER NEVER VISITED is
# an analysis gap hidden behind `escapes` - sound, and asserted at zero.
if(NOT "${_out}${_err}" MATCHES "([0-9]+) unvisited live sites, ([0-9]+) unvisited operands")
  message(FATAL_ERROR "the claims emitter did not report its unvisited counts:\n${_out}${_err}")
endif()
if(NOT CMAKE_MATCH_1 EQUAL 0 OR NOT CMAKE_MATCH_2 EQUAL 0)
  message(FATAL_ERROR "${NAME}: ${CMAKE_MATCH_1} unvisited live site(s) and ${CMAKE_MATCH_2} unvisited operand(s) - a reachability gap in the analysis")
endif()

execute_process(
  COMMAND "${PYTHON}" "${SCRIPT}" --recording "${_rec}" --claims "${_claims}"
          --name "${NAME}" --max-report 0 --expect-violations 0
  OUTPUT_VARIABLE _pyout ERROR_VARIABLE _pyerr RESULT_VARIABLE _pyrc)
message(STATUS "${_pyout}${_pyerr}")

if(NOT _pyout MATCHES "observed sites +([0-9]+)")
  message(FATAL_ERROR "the checker did not report observed sites:\n${_pyout}")
endif()
set(_observed "${CMAKE_MATCH_1}")
if(NOT _pyout MATCHES "SOUNDNESS violations ([0-9]+) +sound ([0-9]+) +partial ([0-9]+) +pending ([0-9]+)")
  message(FATAL_ERROR "the checker did not report its verdict tally:\n${_pyout}")
endif()
set(_violations "${CMAKE_MATCH_1}")
set(_sound "${CMAKE_MATCH_2}")
set(_partial "${CMAKE_MATCH_3}")
set(_pending "${CMAKE_MATCH_4}")
if(NOT _pyout MATCHES "UNCLAIMED observed sites ([0-9]+)")
  message(FATAL_ERROR "the checker did not report unclaimed sites:\n${_pyout}")
endif()
set(_unclaimed "${CMAKE_MATCH_1}")
if(NOT _pyout MATCHES "PRECISION confined ([0-9]+)/([0-9]+) = ([0-9.]+)%")
  message(FATAL_ERROR "the checker did not report precision:\n${_pyout}")
endif()
set(_precision "${CMAKE_MATCH_1}/${CMAKE_MATCH_2} = ${CMAKE_MATCH_3}%")
string(REGEX MATCH "reasons: [^\n]*" _reasons "${_pyout}")

if(_observed LESS_EQUAL 0)
  message(FATAL_ERROR "${NAME}: no site was observed - the corpus did not run, so nothing was checked")
endif()
if(NOT _violations EQUAL 0)
  message(FATAL_ERROR "${NAME}: ${_violations} SOUNDNESS violation(s) - a site claimed confined was reachable after its frame returned; every one is named above")
endif()
# THE STOPPED-CLOCK GUARD IS THE FIXTURE'S. A corpus under --script executes
# an eighth of itself, and on bootstrap none of the 28 sites the analysis
# calls confined is in a function that ever runs - so "sound > 0" cannot be
# met there and would gate on execution coverage, not on the analysis. The
# fixture calls every one of its confined sites; that is where the guard has
# teeth. On a corpus the escapes claims still carry evidence (an EXACT count),
# and sound/precision are printed for the record.
if(STRICT)
  if(_sound LESS_EQUAL 0)
    message(FATAL_ERROR "${NAME}: no confined claim was proved sound - the analysis said escapes everywhere, which is right the way a stopped clock is")
  endif()
  if(NOT _partial EQUAL 0 OR NOT _pending EQUAL 0)
    message(FATAL_ERROR "${NAME}: partial ${_partial}, pending ${_pending} - the fixture must adjudicate every confined claim")
  endif()
endif()
if(NOT _pyrc EQUAL 0)
  message(FATAL_ERROR "${NAME}: the checker exited ${_pyrc}")
endif()

message(STATUS "escape claims (${NAME}): ${_observed} sites observed (unclaimed ${_unclaimed}), "
               "${_violations} violations, sound ${_sound}, partial ${_partial}, pending ${_pending}, "
               "precision ${_precision}; ${_reasons}")
