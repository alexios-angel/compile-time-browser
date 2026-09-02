# THE PDLL GUARD'S OWN GATE.
#
# utils/pdll-strict.sh exists because mlir-pdll EXITS 0 on two things that
# silently change what a pattern matches. A guard nobody tests is a guard that
# quietly stops guarding when the tool's output changes - and this one keys off
# text the tool prints - so every case here is asserted TWICE:
#
#   the RAW tool must accept the bad file    (the hole is real; the test is not
#                                             vacuous, and the day upstream
#                                             fixes it this line says so)
#   the GUARD must refuse it, with a reason  (the guard has teeth)
#
# and the accepted file must pass BOTH, so that a guard which simply refused
# everything would fail here rather than pass three ways.
#
#   -DGUARD=     utils/pdll-strict.sh
#   -DPDLL=      the real mlir-pdll
#   -DDIR=       the directory holding the .pdll fixtures
#   -DINCLUDES=  comma-separated -I directories
#   -DWORK=      a writable directory
cmake_minimum_required(VERSION 3.20)
foreach(required GUARD PDLL DIR INCLUDES WORK)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "check-pdll-guard: -D${required}= is required")
  endif()
endforeach()

set(work "${WORK}/pdll-guard")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

string(REPLACE "," ";" includes "${INCLUDES}")
set(flags "")
foreach(dir IN LISTS includes)
  list(APPEND flags -I "${dir}")
endforeach()

# Run one .pdll and hand back the status and everything it said.
function(pdll_run out_result out_output)
  cmake_parse_arguments(RUN "" "FIXTURE;OUTPUT" "COMMAND" ${ARGN})
  execute_process(COMMAND ${RUN_COMMAND} -x=cpp "${DIR}/${RUN_FIXTURE}" ${flags}
                          -o "${work}/${RUN_OUTPUT}"
                  OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE r)
  set(${out_result} "${r}" PARENT_SCOPE)
  set(${out_output} "${out}${err}" PARENT_SCOPE)
endfunction()

set(report "")

# --- the file the guard must let through ------------------------------------
pdll_run(r text COMMAND "${PDLL}" FIXTURE accepted.pdll OUTPUT raw-accepted.inc)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "pdll-guard: mlir-pdll itself refused accepted.pdll, so the fixture is "
                      "wrong rather than the guard:\n${text}")
endif()
pdll_run(r text COMMAND "${GUARD}" "${PDLL}" FIXTURE accepted.pdll OUTPUT accepted.inc)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "pdll-guard: the guard refused accepted.pdll, which is a pattern written "
                      "the way the boundary allows:\n${text}")
endif()
if(NOT EXISTS "${work}/accepted.inc")
  message(FATAL_ERROR "pdll-guard: the guard passed accepted.pdll but wrote no output; it is "
                      "not running the tool it wraps")
endif()
# AND IT REALLY IS OUR DIALECT that got through. A guard that passed a file
# whose ODS include had silently failed would prove nothing about the wiring.
file(READ "${work}/accepted.inc" accepted_text)
if(NOT accepted_text MATCHES "operation \"ctjs.binary\"")
  message(FATAL_ERROR "pdll-guard: accepted.pdll compiled without naming ctjs.binary:\n${accepted_text}")
endif()
string(APPEND report "accepted.pdll: through, matching ctjs.binary; ")

# --- the two files it must refuse, each still accepted by the raw tool -------
#
# THE EXPECTED TEXT IS THE TOOL'S, NOT THE GUARD'S, for the first case: the
# guard reprints what mlir-pdll said, and pinning that string is what notices
# the day the diagnostic changes and the grep stops matching.
set(cases
  "dropped-attribute.pdll|unregistered dialect"
  "misspelled-operation.pdll|ctjs.binry")
foreach(case IN LISTS cases)
  string(REPLACE "|" ";" parts "${case}")
  list(GET parts 0 fixture)
  list(GET parts 1 expected)

  pdll_run(r text COMMAND "${PDLL}" FIXTURE "${fixture}" OUTPUT "raw-${fixture}.inc")
  if(NOT r EQUAL 0)
    message(FATAL_ERROR "pdll-guard: the RAW mlir-pdll refused ${fixture}. That is good news for "
                        "upstream and it makes this case vacuous - the guard is no longer what "
                        "catches it. Re-read utils/pdll-strict.sh before deleting anything:\n${text}")
  endif()

  pdll_run(r text COMMAND "${GUARD}" "${PDLL}" FIXTURE "${fixture}" OUTPUT "${fixture}.inc")
  if(r EQUAL 0)
    message(FATAL_ERROR "pdll-guard: the guard PASSED ${fixture}, which mlir-pdll compiles into a "
                        "pattern that matches the wrong operations:\n${text}")
  endif()
  string(FIND "${text}" "${expected}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "pdll-guard: the guard refused ${fixture} but not for the reason it had "
                        "to ('${expected}'):\n${text}")
  endif()
  string(APPEND report "${fixture}: raw exit 0, guard refused on '${expected}'; ")
endforeach()

message(STATUS "pdll-guard: ${report}")
