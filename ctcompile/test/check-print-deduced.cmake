# THE PRINTING GATE - ctcompile Phase 62½-E (part 24 Stages 53E and 53F).
#
# Three modules of ONE program: the plain lowering, the same with
# --ctnative-print-deduced applied, and the same with the pass's mutation.
# The compilation-unit gate (check-native-unit.cmake) already runs the deduced
# module through the whole of 62½-D - same printed answers as the
# interpreter, no ctbrowser symbol - so what is left to prove here is the
# printing itself:
#
#   (a) every `auto` declaration has exactly one pin after it, and there is at
#       least one - a pass that marks nothing would make (b) to (d) vacuous
#   (b) the plain file has no `auto` and no pin at all
#   (c) the two files DIFFER ONLY IN SPELLING: with the pin lines, the macro
#       block and its include removed from the deduced file, and every
#       declaration's type spelling normalised in both, the texts are
#       identical, line for line
#   (d) the byte counts - plain, deduced, deduced without its pins - are
#       printed; the difference is the first number part 24 Phase 63 Step 4
#       reports
#   (e) the MUTATED file, where one deduced double is pinned as int32_t, FAILS
#       to compile and the compiler's message names the JavaScript site; the
#       same file compiles CLEAN under -DCTCOMPILE_NO_TYPE_PINS, which proves
#       both that the pin was the failure and that the switch strips it
#
#   -DTRANSLATE= ctjs-translate   -DPLAIN= -DDEDUCED= -DMUTATED= the modules
#   -DCXX= the C++ compiler       -DWORK= a writable directory   -DNAME=
foreach(required TRANSLATE PLAIN DEDUCED MUTATED CXX WORK NAME)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "check-print-deduced: -D${required}= is required")
  endif()
endforeach()
set(work "${WORK}/print-deduced-${NAME}")
file(MAKE_DIRECTORY "${work}")

function(translate module out)
  execute_process(COMMAND "${TRANSLATE}" --mlir-to-cpp "${module}"
                  OUTPUT_FILE "${out}" ERROR_VARIABLE err RESULT_VARIABLE r)
  if(NOT r EQUAL 0)
    message(FATAL_ERROR "print-deduced (${NAME}): the emitter refused ${module}:\n${err}")
  endif()
endfunction()
translate("${PLAIN}" "${work}/plain.cpp")
translate("${DEDUCED}" "${work}/deduced.cpp")
translate("${MUTATED}" "${work}/mutated.cpp")

# Split a file into a CMake list of lines, with ';' and '[' ']' kept literal.
function(read_lines path out)
  file(READ "${path}" text)
  string(REPLACE ";" "\;" text "${text}")
  string(REPLACE "[" "\\[" text "${text}")
  string(REPLACE "]" "\\]" text "${text}")
  string(REPLACE "\n" ";" lines "${text}")
  set(${out} "${lines}" PARENT_SCOPE)
endfunction()
read_lines("${work}/plain.cpp" plain_lines)
read_lines("${work}/deduced.cpp" deduced_lines)

# (a) and (b): count, and pair every auto with the pin on the next line.
set(n_auto 0)
set(n_pin 0)
set(n_plain_auto 0)
set(expect_pin OFF)
set(stripped "")     # the deduced file without pins, macro block, its include
set(in_macro OFF)
foreach(line IN LISTS deduced_lines)
  if(line MATCHES "^#ifndef CTCOMPILE_NO_TYPE_PINS")
    set(in_macro ON)
  endif()
  if(in_macro)
    if(line MATCHES "^#endif")
      set(in_macro OFF)
    endif()
    continue()
  endif()
  if(line MATCHES "^#include <type_traits>")
    continue()
  endif()
  if(line MATCHES "^[ \t]*CTCOMPILE_PIN\\(")
    if(NOT expect_pin)
      message(FATAL_ERROR "print-deduced (${NAME}): a pin with no auto declaration before it: ${line}")
    endif()
    math(EXPR n_pin "${n_pin} + 1")
    set(expect_pin OFF)
    continue()
  endif()
  if(expect_pin)
    message(FATAL_ERROR "print-deduced (${NAME}): an auto declaration without its pin: ${line}")
  endif()
  if(line MATCHES "^[ \t]*auto [A-Za-z_0-9]+ = ")
    math(EXPR n_auto "${n_auto} + 1")
    set(expect_pin ON)
  endif()
  list(APPEND stripped "${line}")
endforeach()
foreach(line IN LISTS plain_lines)
  if(line MATCHES "^[ \t]*auto [A-Za-z_0-9]+ = |CTCOMPILE_PIN")
    math(EXPR n_plain_auto "${n_plain_auto} + 1")
  endif()
endforeach()
if(NOT n_plain_auto EQUAL 0)
  message(FATAL_ERROR "print-deduced (${NAME}): the plain file has ${n_plain_auto} auto/pin line(s); the policy leaked into the default emitter")
endif()
if(n_auto EQUAL 0)
  message(FATAL_ERROR "print-deduced (${NAME}): the policy marked nothing; the gate would be vacuous")
endif()
if(NOT n_auto EQUAL n_pin)
  message(FATAL_ERROR "print-deduced (${NAME}): ${n_auto} auto declarations but ${n_pin} pins")
endif()

# (c) spelling only.
function(normalise lines out)
  set(result "")
  foreach(line IN LISTS lines)
    string(REGEX REPLACE "^([ \t]*)(auto|double|bool|int32_t|int64_t|float) ([A-Za-z_0-9]+) = " "\\1T \\3 = " line "${line}")
    list(APPEND result "${line}")
  endforeach()
  set(${out} "${result}" PARENT_SCOPE)
endfunction()
normalise("${plain_lines}" plain_norm)
normalise("${stripped}" deduced_norm)
list(LENGTH plain_norm n_plain)
list(LENGTH deduced_norm n_deduced)
if(NOT n_plain EQUAL n_deduced)
  message(FATAL_ERROR "print-deduced (${NAME}): after normalising, ${n_plain} plain lines vs ${n_deduced} deduced lines - the files differ in more than spelling")
endif()
math(EXPR last "${n_plain} - 1")
foreach(i RANGE ${last})
  list(GET plain_norm ${i} a)
  list(GET deduced_norm ${i} b)
  if(NOT a STREQUAL b)
    math(EXPR n "${i} + 1")
    message(FATAL_ERROR "print-deduced (${NAME}): line ${n} differs in more than spelling:\n  plain:   ${a}\n  deduced: ${b}")
  endif()
endforeach()

# (d) sizes.
file(SIZE "${work}/plain.cpp" b_plain)
file(SIZE "${work}/deduced.cpp" b_deduced)
string(REPLACE ";" "\n" stripped_text "${stripped}")
string(REPLACE "\;" ";" stripped_text "${stripped_text}")
string(REPLACE "\\[" "[" stripped_text "${stripped_text}")
string(REPLACE "\\]" "]" stripped_text "${stripped_text}")
file(WRITE "${work}/deduced-no-pins.cpp" "${stripped_text}\n")
file(SIZE "${work}/deduced-no-pins.cpp" b_nopins)

# (e) the mutation.
set(flags -std=c++23 -O2 -Wall -Wextra -Werror -pedantic -ffp-contract=off
          -Wno-unused-variable -Wno-unused-but-set-variable -fsyntax-only)
execute_process(COMMAND "${CXX}" ${flags} "${work}/mutated.cpp"
                OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE r)
if(r EQUAL 0)
  message(FATAL_ERROR "print-deduced (${NAME}): the mutated file (one double pinned as int32_t) COMPILED - the pins do not bite")
endif()
set(diag "${out}${err}")
# A JAVASCRIPT site: the file the program came from, its line and column.
# A site in the .mlir file (locations dropped between passes) or "unknown"
# (a location the printer did not unwrap) both fail here by name.
if(NOT diag MATCHES "ctcompile: [A-Za-z_0-9]+ @ [^\"\n]*\\.js:[0-9]+:[0-9]+")
  message(FATAL_ERROR "print-deduced (${NAME}): the mutated file failed, but not with a pin naming a JavaScript site (file.js:line:col):\n${diag}")
endif()
string(REGEX MATCH "ctcompile: [A-Za-z_0-9]+ @ [^\"\n]*\\.js:[0-9]+:[0-9]+" site "${diag}")
execute_process(COMMAND "${CXX}" ${flags} -DCTCOMPILE_NO_TYPE_PINS "${work}/mutated.cpp"
                OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE r)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "print-deduced (${NAME}): the mutated file does not compile under -DCTCOMPILE_NO_TYPE_PINS, so the failure above was not (only) the pin:\n${out}${err}")
endif()

message(STATUS "print-deduced (${NAME}): ${n_auto} deduced declarations, each pinned; plain ${b_plain} bytes, deduced ${b_deduced} bytes (${b_nopins} without pins); the mutation failed the build at \"${site}\"")
