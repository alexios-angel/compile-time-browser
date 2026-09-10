# THE ORACLE, CLOSED - Phase 54A checked by Phase 54B.
#
# Three steps over ONE corpus, and the whole point is that steps 1 and 2 never
# meet until step 3:
#
#   1. the INTERPRETER runs the corpus with a recorder installed and writes what
#      every register actually held      (ctcompile-test-type-oracle --script)
#   2. the COMPILER imports the same corpus, runs the inference, and writes what
#      it claims every register will hold (ctcompile-test-type-claims --script)
#   3. tools/check/type-oracle.py compares the two.
#
# ZERO SOUNDNESS VIOLATIONS IS THE GATE. Precision is reported, never gated:
# part 24 says "soundness failing is a defect; precision being low is a
# backlog", and a gate on precision would be an invitation to buy it with
# soundness.
#
# AND THE VACUOUS PASS IS GUARDED. A corpus that never ran observes nothing,
# and nothing has no violations. So `observed` must be positive, and so must
# `beat-boxed` - an inference that fell back to `boxed` everywhere is sound
# the way a stopped clock is right.
#
#   -DORACLE=  ctcompile-test-type-oracle
#   -DCLAIMS=  ctcompile-test-type-claims
#   -DPYTHON=  the interpreter
#   -DSCRIPT=  tools/check/type-oracle.py
#   -DCORPUS=  the JavaScript file, self-contained
#   -DPREFIX=  optional: a file to prepend to CORPUS (a vendored bundle)
#   -DWORK=    a writable directory
#   -DNAME=    what to call this corpus in the report

# THE SAME SKIP THE ORACLE'S OWN TEST TAKES. Its self-test says SKIPPED when
# the interpreter was built without the recording hook, and there is then no
# recording for a claim to be checked against.
execute_process(COMMAND "${ORACLE}" OUTPUT_VARIABLE _self ERROR_VARIABLE _selferr RESULT_VARIABLE _rc)
if(_self MATCHES "SKIPPED")
  message(STATUS "type claims (${NAME}): skipped - this build has no recording hook")
  return()
endif()
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "the type oracle's self-test failed before the claims ran:\n${_self}${_selferr}")
endif()

set(_corpus "${CORPUS}")
if(PREFIX)
  # ONE FILE, because both sides compile one file into one program and the
  # program's source hash is the key that joins a claim to an observation.
  file(READ "${PREFIX}" _prefix)
  file(READ "${CORPUS}" _driver)
  set(_corpus "${WORK}/type-claims-${NAME}.js")
  file(WRITE "${_corpus}" "${_prefix}\n${_driver}")
endif()

set(_rec "${WORK}/type-claims-${NAME}.rec")
set(_claims "${WORK}/type-claims-${NAME}.claims")

execute_process(
  COMMAND "${ORACLE}" --script "${_corpus}" --out "${_rec}"
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
# A VALUE IN A LIVE BLOCK THE SOLVER NEVER VISITED IS AN ANALYSIS GAP. The
# emitter boxes it so the claim stays sound, which is exactly why a number
# alone would never fail anything - so the number is asserted here instead.
if(NOT "${_out}${_err}" MATCHES "([0-9]+) unvisited live values boxed")
  message(FATAL_ERROR "the claims emitter did not report its unvisited count:\n${_out}${_err}")
endif()
if(NOT CMAKE_MATCH_1 EQUAL 0)
  message(FATAL_ERROR "${NAME}: ${CMAKE_MATCH_1} value(s) in live blocks were never visited by the solver - a reachability gap in the analysis, hidden behind `boxed`")
endif()

execute_process(
  COMMAND "${PYTHON}" "${SCRIPT}" --recording "${_rec}" --claims "${_claims}"
          --name "${NAME}" --max-report 0 --expect-violations 0
  OUTPUT_VARIABLE _pyout ERROR_VARIABLE _pyerr RESULT_VARIABLE _pyrc)
message(STATUS "${_pyout}${_pyerr}")

# THE NUMBERS, BY NAME, so the log line below is what the plan asks to record
# per corpus: violations and precision "stated against the number of registers
# actually OBSERVED, not the number that exist".
if(NOT _pyout MATCHES "observed registers +([0-9]+)")
  message(FATAL_ERROR "the checker did not report an observed count:\n${_pyout}")
endif()
set(_observed "${CMAKE_MATCH_1}")
if(NOT _pyout MATCHES "SOUNDNESS violations ([0-9]+)")
  message(FATAL_ERROR "the checker did not report violations:\n${_pyout}")
endif()
set(_violations "${CMAKE_MATCH_1}")
if(NOT _pyout MATCHES "PRECISION beat-boxed ([0-9]+)/([0-9]+) = ([0-9.]+)%")
  message(FATAL_ERROR "the checker did not report precision:\n${_pyout}")
endif()
set(_beat "${CMAKE_MATCH_1}")
set(_checked "${CMAKE_MATCH_2}")
set(_pct "${CMAKE_MATCH_3}")

if(_observed LESS_EQUAL 0)
  message(FATAL_ERROR "${NAME}: nothing was observed - the corpus did not run, so nothing was checked")
endif()
if(NOT _violations EQUAL 0)
  message(FATAL_ERROR "${NAME}: ${_violations} SOUNDNESS violation(s) - the inference claimed narrower than the interpreter observed; every one is named above")
endif()
if(_beat LESS_EQUAL 0)
  message(FATAL_ERROR "${NAME}: the inference beat boxed on 0 registers - sound the way a stopped clock is right")
endif()
if(NOT _pyrc EQUAL 0)
  message(FATAL_ERROR "${NAME}: the checker exited ${_pyrc}")
endif()

message(STATUS "type claims (${NAME}): ${_observed} observed registers, ${_violations} soundness violations, "
               "precision ${_beat}/${_checked} = ${_pct}% beat boxed")
