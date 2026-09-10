# THE DEDUCTION PROBE AS A GATE - part 24 §3.3 constraint 3 and the Phase 53
# gate: probes/deduction.cpp compiles at the generated code's flags on every
# compiler the box has, and RUNS, because the two C++23 rows are answered by
# its exit status (2 * move_only_function + ranges_to; 0 is the measured
# answer on libstdc++ 13.3 and the one the plan's tables carry). A compiler
# that is not installed is skipped by name, never silently.
#   -DPROBE= the source   -DCOMPILERS= a ;-list of compiler paths   -DWORK= a dir
foreach(required PROBE COMPILERS WORK)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "check-deduction-probe.cmake: -D${required}= is required")
  endif()
endforeach()
set(_ran 0)
foreach(_cxx IN LISTS COMPILERS)
  if(NOT EXISTS "${_cxx}")
    message(STATUS "deduction probe: ${_cxx} not installed - skipped")
    continue()
  endif()
  execute_process(COMMAND "${_cxx}" --version OUTPUT_VARIABLE _v ERROR_QUIET)
  string(REGEX MATCH "[^\n]*" _v "${_v}")
  execute_process(
    COMMAND "${_cxx}" -std=c++20 -pedantic -Wall -Wextra -Werror -Wconversion
            "${PROBE}" -o "${WORK}/deduction-probe"
    OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "deduction probe does not compile with ${_v}:\n${_out}${_err}")
  endif()
  execute_process(COMMAND "${WORK}/deduction-probe" RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "deduction probe on ${_v}: exit ${_rc} - a C++23 library row changed "
                        "(2 = move_only_function present, 1 = ranges::to present); part 24 §3.2 "
                        "and the stages that plan against it must be re-read before this is accepted")
  endif()
  message(STATUS "deduction probe: ${_v} - every row as measured")
  math(EXPR _ran "${_ran} + 1")
endforeach()
if(_ran EQUAL 0)
  message(FATAL_ERROR "deduction probe: no compiler ran - the gate proved nothing")
endif()
