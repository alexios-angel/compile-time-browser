# THE COMPILATION-UNIT GATE - ctcompile Phase 62½-D. The test that DEFINES
# "native".
#
# Part 24 §1.2: a program compiles to native only when the output links neither
# the interpreter nor the collector. This script is that sentence as a test,
# run over ONE EmitC module and the JavaScript program it is the lowering of:
#
#   (a) the module goes through the FORKED emitter to C++
#       (ctjs-translate --mlir-to-cpp, in its default mode - see below)
#   (b) that C++ is compiled by the configured compiler STANDALONE: no include
#       path into ctbrowser/, no library on the link line, -Wall -Wextra -Werror
#       -pedantic, and -ffp-contract=off because JavaScript has no fused
#       multiply-add and the interpreter's separate opcodes never fuse one
#   (c) `nm -C` on the binary shows no `ctbrowser::script::` symbol (nor a
#       `ctbrowser::aot::` one - that is the boxed tier's helper ABI, which
#       links the interpreter). AND THE CHECK IS PROVED LOAD-BEARING FIRST: the
#       same nm invocation is run against a binary that does link the
#       interpreter and must find such a symbol there, or a wrong nm spelling,
#       a stripped binary or a wrong pattern would pass this gate vacuously
#   (d) the binary runs and prints its globals
#   (e) the interpreter runs the same JavaScript and prints its globals
#       (ctcompile-test-native-reference) - after a probe program with known
#       answers has shown the reference formats, sorts and classifies as the
#       convention says, so that a reference printing nothing cannot agree
#       with a binary printing nothing
#   (f) the two texts are compared line by line; a difference NAMES the global
#   (g) the counters are asserted and one line is printed:
#         native unit (<name>): N globals agree, 0 ctbrowser symbols
#
# THE OUTPUT CONVENTION both sides print is defined in native-fixture.emitc.mlir
# and restated here because this is what checks it: one line per numeric
# global, ascending bytewise by name, `<name>=<value>` with the value as
# printf("%.17g") of the double. The native side prints it from `main` after
# the last top-level statement; the reference prints every Number-valued global
# the program created or changed. `-nan` and `nan` compare equal: a NaN's sign
# is not observable in JavaScript, x86 arithmetic produces negative NaNs and
# constant folding produces positive ones, and the interpreter and the binary
# get one each.
#
# WHY NOT --declare-variables-at-top, which the boxed pipeline needs: the
# emitter declares a variable for the `emitc.expression` that feeds a `do`
# loop's condition and never assigns it (it is printed inline in the `while`),
# which is an unused variable, which -Wall -Werror rejects. Native functions are
# single-block - their control flow is emitc.if/for/do, not cf branches - so
# the flag is not needed here, and its absence is what keeps that emitter
# quirk out of this gate. A lowering that emits multi-block functions would
# need the flag and would meet the quirk; that is a one-hunk fix in the fork.
#
#   -DTRANSLATE=  ctjs-translate (hosts the forked emitter under -mlir-to-cpp)
#   -DMODULE=     the EmitC module (.mlir)
#   -DJS=         the JavaScript program the module is the lowering of
#   -DCXX=        the C++ compiler
#   -DNM=         nm or llvm-nm
#   -DREFERENCE=  ctcompile-test-native-reference
#   -DVM_LINKED=  a binary that links the interpreter (ctcompile-test-type-oracle)
#   -DWORK=       a writable directory; this uses WORK/native-unit-NAME/
#   -DNAME=       what to call this unit in the report
#
# NEGATIVE PROOFS, so the gate's teeth stay in the suite:
#   -DMUTATE=<global>   after (a), `<global> = <global> + 1;` is inserted in
#                       front of the first print - the gate must then FAIL
#                       naming <global>
#   -DPREBUILT=<exe>    skip (a) and (b) and gate this binary instead; used
#                       with ctcompile-test-native-vm-linked, which is the
#                       fixture's own C++ plus one object that reaches the
#                       interpreter - the gate must FAIL on its nm check
#   -DEXPECT_FAILURE=<regex>
#                       run the gate as a child process and PASS only if it
#                       FAILED and its output matches the regex. ctest's
#                       WILL_FAIL cannot do the second half, and a negative
#                       test that passes on any failure at all - a missing
#                       compiler, say - has stopped proving anything.
cmake_minimum_required(VERSION 3.20)

foreach(required TRANSLATE MODULE JS CXX NM REFERENCE VM_LINKED WORK NAME)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "check-native-unit.cmake: -D${required}= is required")
  endif()
endforeach()

# --- the negative harness: run myself, require failure AND its reason ---------
if(DEFINED EXPECT_FAILURE)
  set(_forward)
  foreach(_v TRANSLATE MODULE JS CXX NM REFERENCE VM_LINKED WORK NAME MUTATE PREBUILT)
    if(DEFINED ${_v})
      list(APPEND _forward "-D${_v}=${${_v}}")
    endif()
  endforeach()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_forward} -P "${CMAKE_SCRIPT_MODE_FILE}"
    OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
  if(_rc EQUAL 0)
    message(FATAL_ERROR
      "${NAME}: the gate PASSED where it had to fail - it has no teeth for this case\n${_out}${_err}")
  endif()
  if(NOT "${_out}${_err}" MATCHES "${EXPECT_FAILURE}")
    message(FATAL_ERROR
      "${NAME}: the gate failed, but not for the reason expected (wanted /${EXPECT_FAILURE}/):\n${_out}${_err}")
  endif()
  string(REGEX MATCH "[^\n]*${EXPECT_FAILURE}[^\n]*" _named "${_out}${_err}")
  message(STATUS "negative proof (${NAME}): the gate failed as it must, and said why: ${_named}")
  return()
endif()

set(_dir "${WORK}/native-unit-${NAME}")
file(MAKE_DIRECTORY "${_dir}")

# --- (a) EmitC -> C++ through the forked emitter, (b) standalone compile -------
if(DEFINED PREBUILT)
  set(_exe "${PREBUILT}")
  message(STATUS "${NAME}: gating a prebuilt binary, ${_exe}")
else()
  execute_process(
    COMMAND "${TRANSLATE}" --mlir-to-cpp "${MODULE}"
    OUTPUT_VARIABLE _cpp ERROR_VARIABLE _err RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${NAME}: the emitter refused ${MODULE} (exit ${_rc})\n${_err}")
  endif()
  string(LENGTH "${_cpp}" _produced)
  if(_produced EQUAL 0)
    message(FATAL_ERROR "${NAME}: the emitter produced nothing for ${MODULE}")
  endif()
  # THE C++ MUST NOT REACH FOR THE ENGINE'S HEADERS EITHER: a translation unit
  # that includes ctbrowser/ and happens to link is one inline function away
  # from the interpreter, and the standalone compile below would catch the
  # include only by failing on the path - which reads as a build problem.
  if(_cpp MATCHES "#include[ \t]*[<\"]ctbrowser")
    message(FATAL_ERROR "${NAME}: the emitted C++ includes a ctbrowser header - that is not native")
  endif()

  if(DEFINED MUTATE AND NOT MUTATE STREQUAL "")
    # THE OFF-BY-ONE, inserted where the convention says the program has ended
    # and the printing begins. A guard that silently edits nothing looks exactly
    # like a mutation that was caught, so the edit is asserted to have landed.
    string(FIND "${_cpp}" "std::printf(" _at)
    if(_at EQUAL -1)
      message(FATAL_ERROR "${NAME}: cannot mutate - no std::printf( in the emitted C++, the output convention has changed under this script")
    endif()
    string(SUBSTRING "${_cpp}" 0 ${_at} _head)
    string(SUBSTRING "${_cpp}" ${_at} -1 _tail)
    set(_cpp "${_head}${MUTATE} = ${MUTATE} + 1;\n  ${_tail}")
    if(NOT _cpp MATCHES "${MUTATE} = ${MUTATE} \\+ 1;")
      message(FATAL_ERROR "${NAME}: the mutation of ${MUTATE} did not apply")
    endif()
    message(STATUS "${NAME}: MUTATED - ${MUTATE} is one more than the program computed")
  endif()

  set(_src "${_dir}/unit.cpp")
  file(WRITE "${_src}" "${_cpp}")
  set(_exe "${_dir}/unit")
  # NOTHING OF ctbrowser'S ON THIS LINE. No -I, no -l, no library: the whole
  # point. -ffp-contract=off is explained at the top.
  execute_process(
  # Generated code must be clean under the same flags as the dedicated Phase
  # 63 gate. A warning here is a ctcompile bug, not something to suppress.
  COMMAND "${CXX}" -std=c++23 -O2 -Wall -Wextra -Werror -pedantic -Wconversion -ffp-contract=off
          -o "${_exe}" "${_src}"
    OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${NAME}: the emitted C++ does not compile standalone (exit ${_rc})\n${_out}${_err}")
  endif()
endif()

# --- (c) the symbol check, proved load-bearing before it is trusted -----------
execute_process(
  COMMAND "${NM}" -C "${VM_LINKED}"
  OUTPUT_VARIABLE _nm_control ERROR_VARIABLE _err RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "${NAME}: ${NM} failed on the control binary ${VM_LINKED} (exit ${_rc})\n${_err}")
endif()
string(REGEX MATCHALL "[^\n]*ctbrowser::script::[^\n]*" _control_hits "${_nm_control}")
list(LENGTH _control_hits _control_count)
if(_control_count EQUAL 0)
  message(FATAL_ERROR
    "${NAME}: the nm check is NOT load-bearing - ${VM_LINKED} links the interpreter and "
    "`${NM} -C` found no ctbrowser::script:: symbol in it. The invocation, the demangling "
    "or the pattern is wrong, and a pass on the native binary would mean nothing.")
endif()

execute_process(
  COMMAND "${NM}" -C "${_exe}"
  OUTPUT_VARIABLE _nm ERROR_VARIABLE _err RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "${NAME}: ${NM} failed on ${_exe} (exit ${_rc})\n${_err}")
endif()
if(NOT _nm MATCHES "[ \t]main\n")
  message(FATAL_ERROR "${NAME}: `${NM} -C` listed no `main` in ${_exe} - it did not read the binary\n${_nm}")
endif()
string(REGEX MATCHALL "[^\n]*ctbrowser::(script|aot)::[^\n]*" _hits "${_nm}")
list(LENGTH _hits _vm_symbols)
if(NOT _vm_symbols EQUAL 0)
  list(GET _hits 0 _first)
  string(STRIP "${_first}" _first)
  message(FATAL_ERROR
    "${NAME}: the binary reaches the interpreter - ${_vm_symbols} ctbrowser symbol(s) "
    "(the control binary had ${_control_count}); the first is: ${_first}")
endif()

# --- (d) run the binary ---------------------------------------------------------
execute_process(
  COMMAND "${_exe}"
  OUTPUT_VARIABLE _native ERROR_VARIABLE _err RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "${NAME}: the native binary exited ${_rc}\n${_native}${_err}")
endif()

# --- (e) the reference: first proved on a probe, then run on the program ------
#
# THE PROBE has one of everything the convention has to get right: two numbers
# out of source order (sorted), a -0 (printed as -0, not 0), a NaN (whichever
# sign, compared as `nan`), a function (skipped and counted), a string and a
# BOOLEAN (skipped and counted - and that count is what the gate asserts is
# zero for the real program: a boolean is exactly the non-Number a numeric
# fixture could leave behind by accident).
set(_probe "${_dir}/probe.js")
file(WRITE "${_probe}" "function f(x) { return x; }\nvar z = f(2);\nvar a = 1;\nvar m = -0;\nvar q = 0 / 0;\nvar s = \"text\";\nvar t = true;\n")
execute_process(
  COMMAND "${REFERENCE}" "${_probe}"
  OUTPUT_VARIABLE _probe_out ERROR_VARIABLE _probe_err RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "${NAME}: the reference failed on the probe (exit ${_rc})\n${_probe_err}")
endif()
string(REPLACE "=-nan" "=nan" _probe_out "${_probe_out}")
if(NOT _probe_out STREQUAL "a=1\nm=-0\nq=nan\nz=2\n")
  message(FATAL_ERROR "${NAME}: the reference does not print the convention - on the probe it printed:\n${_probe_out}")
endif()
if(NOT _probe_err MATCHES "4 number globals printed, 1 function globals skipped, 2 other globals skipped")
  message(FATAL_ERROR "${NAME}: the reference miscounted the probe:\n${_probe_err}")
endif()

execute_process(
  COMMAND "${REFERENCE}" "${JS}"
  OUTPUT_VARIABLE _reference ERROR_VARIABLE _reference_err RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "${NAME}: the interpreter failed on ${JS} (exit ${_rc})\n${_reference_err}")
endif()
if(NOT _reference_err MATCHES "([0-9]+) number globals printed, ([0-9]+) function globals skipped, ([0-9]+) other globals skipped")
  message(FATAL_ERROR "${NAME}: the reference did not report its counts:\n${_reference_err}")
endif()
set(_numbers "${CMAKE_MATCH_1}")
set(_functions "${CMAKE_MATCH_2}")
set(_others "${CMAKE_MATCH_3}")
if(NOT _others EQUAL 0)
  message(FATAL_ERROR "${NAME}: ${JS} left ${_others} non-Number, non-function global(s) - the fixture is numbers and booleans only, and a boolean cannot be a global here:\n${_reference_err}")
endif()
if(_numbers LESS_EQUAL 0)
  message(FATAL_ERROR "${NAME}: the interpreter left no numeric global - nothing was compared")
endif()

# --- (f) compare, naming the global ---------------------------------------------
string(REPLACE "=-nan" "=nan" _native "${_native}")
string(REPLACE "=-nan" "=nan" _reference "${_reference}")
string(REGEX REPLACE "\n$" "" _native_lines "${_native}")
string(REGEX REPLACE "\n$" "" _reference_lines "${_reference}")
string(REPLACE "\n" ";" _native_lines "${_native_lines}")
string(REPLACE "\n" ";" _reference_lines "${_reference_lines}")
list(LENGTH _native_lines _native_count)
list(LENGTH _reference_lines _reference_count)
if(NOT _reference_count EQUAL _numbers)
  message(FATAL_ERROR "${NAME}: the reference said ${_numbers} globals and printed ${_reference_count} lines")
endif()

set(_agree 0)
math(EXPR _last "${_native_count} - 1")
if(_reference_count GREATER _native_count)
  math(EXPR _last "${_reference_count} - 1")
endif()
foreach(_i RANGE 0 ${_last})
  set(_n "")
  set(_r "")
  if(_i LESS _native_count)
    list(GET _native_lines ${_i} _n)
  endif()
  if(_i LESS _reference_count)
    list(GET _reference_lines ${_i} _r)
  endif()
  if(_n STREQUAL _r)
    math(EXPR _agree "${_agree} + 1")
    continue()
  endif()
  set(_n_name "")
  set(_r_name "")
  if(_n MATCHES "^([A-Za-z_$][A-Za-z0-9_$]*)=(.*)$")
    set(_n_name "${CMAKE_MATCH_1}")
    set(_n_value "${CMAKE_MATCH_2}")
  endif()
  if(_r MATCHES "^([A-Za-z_$][A-Za-z0-9_$]*)=(.*)$")
    set(_r_name "${CMAKE_MATCH_1}")
    set(_r_value "${CMAKE_MATCH_2}")
  endif()
  if(_n_name STREQUAL _r_name AND NOT _n_name STREQUAL "")
    message(FATAL_ERROR
      "${NAME}: global '${_n_name}' differs - native printed ${_n_value}, the interpreter printed ${_r_value}\n"
      "native:\n${_native}interpreter:\n${_reference}")
  endif()
  message(FATAL_ERROR
    "${NAME}: line ${_i} differs - native printed '${_n}', the interpreter printed '${_r}' "
    "(a missing or extra global, or one out of order: the convention sorts by name)\n"
    "native:\n${_native}interpreter:\n${_reference}")
endforeach()

# --- (g) the counters, then the one line --------------------------------------
if(NOT _agree EQUAL _numbers)
  message(FATAL_ERROR "${NAME}: ${_agree} lines agreed but the interpreter has ${_numbers} numeric globals")
endif()
if(NOT _native_count EQUAL _numbers)
  message(FATAL_ERROR "${NAME}: the native binary printed ${_native_count} globals, the interpreter ${_numbers}")
endif()
message(STATUS "native unit (${NAME}): ${_agree} globals agree, ${_vm_symbols} ctbrowser symbols "
               "(${_functions} functions; the control binary showed ${_control_count} interpreter symbols to the same nm)")
