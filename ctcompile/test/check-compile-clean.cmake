# THE COMPILE-CLEAN GATE - ctcompile Phase 63 Step 7.
#
# "The generated file compiles clean, and that is a test." One EmitC module
# goes through the forked emitter, and the C++ is compiled - not just parsed:
# -O2 codegen is where -Wmaybe-uninitialized and friends fire - by EVERY
# compiler in -DCOMPILERS= with
#   -std=c++23 -O2 -pedantic -Wall -Wextra -Werror -Wconversion
# and the compile must succeed with NO output at all: a warning that is not
# an error is still a ctcompile bug, because part 24 §2 makes ctcompile the
# diagnostician. And every definition in the file - each function, class and
# global - must sit under a provenance comment naming its JavaScript site, so
# the day a diagnostic appears the mapping back already exists.
#
#   -DTRANSLATE=  ctjs-translate      -DMODULE=  the EmitC module
#   -DCOMPILERS=  comma-separated C++ compilers (all must pass)
#   -DWORK=       a writable directory  -DNAME=   what to call this in the report
#
# NEGATIVE PROOF, so the gate's teeth stay in the suite:
#   -DMUTATE=1    one unused variable is inserted at the top of main - every
#                 compiler must then refuse the file, or -Wall -Werror is not
#                 reaching the compile
#   -DEXPECT_FAILURE=<text>  run this script as a child with the other
#                 arguments and pass only if the child FAILED with <text> in
#                 its output
cmake_minimum_required(VERSION 3.20)
foreach(required TRANSLATE MODULE COMPILERS WORK NAME)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "check-compile-clean: -D${required}= is required")
  endif()
endforeach()

if(DEFINED EXPECT_FAILURE)
  set(args -DTRANSLATE=${TRANSLATE} -DMODULE=${MODULE} "-DCOMPILERS=${COMPILERS}"
           -DWORK=${WORK} -DNAME=${NAME}_child)
  if(DEFINED MUTATE)
    list(APPEND args -DMUTATE=${MUTATE})
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" ${args} -P "${CMAKE_CURRENT_LIST_FILE}"
                  OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE r)
  if(r EQUAL 0)
    message(FATAL_ERROR "compile-clean (${NAME}): the child PASSED; it had to fail with: ${EXPECT_FAILURE}")
  endif()
  string(FIND "${out}${err}" "${EXPECT_FAILURE}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "compile-clean (${NAME}): the child failed, but not with '${EXPECT_FAILURE}':\n${out}${err}")
  endif()
  message(STATUS "compile-clean (${NAME}): the child failed as it had to: ${EXPECT_FAILURE}")
  return()
endif()

set(work "${WORK}/compile-clean-${NAME}")
file(MAKE_DIRECTORY "${work}")
execute_process(COMMAND "${TRANSLATE}" --mlir-to-cpp "${MODULE}"
                OUTPUT_FILE "${work}/unit.cpp" ERROR_VARIABLE err RESULT_VARIABLE r)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "compile-clean (${NAME}): the emitter refused ${MODULE}:\n${err}")
endif()

if(DEFINED MUTATE)
  file(READ "${work}/unit.cpp" text)
  string(REGEX REPLACE "(int32_t main\\(\\) \\{\n)" "\\1  double ctcompile_mutant = 1.0;\n" mutated "${text}")
  if(mutated STREQUAL text)
    message(FATAL_ERROR "compile-clean (${NAME}): the mutation found no main to land in")
  endif()
  file(WRITE "${work}/unit.cpp" "${mutated}")
endif()

# EVERY DEFINITION UNDER A PROVENANCE COMMENT. A definition is a function
# body, a class or a global at file scope; the line before each must be the
# comment. Counted, and the count is in the report.
file(READ "${work}/unit.cpp" text)
string(REPLACE ";" "\;" text "${text}")
string(REPLACE "[" "\\[" text "${text}")
string(REPLACE "]" "\\]" text "${text}")
string(REPLACE "\n" ";" lines "${text}")
set(definitions 0)
set(previous "")
foreach(line IN LISTS lines)
  if(line MATCHES "^[A-Za-z_][A-Za-z_0-9:<>]* [A-Za-z_][A-Za-z_0-9]*\\(.*\\) \\{$"
     OR line MATCHES "^class [A-Za-z_][A-Za-z_0-9]* \\{$"
     OR line MATCHES "^static [A-Za-z_][A-Za-z_0-9:<>]* [A-Za-z_][A-Za-z_0-9]* = ")
    if(NOT previous MATCHES "^// ctcompile: ")
      message(FATAL_ERROR "compile-clean (${NAME}): a definition without a provenance comment above it:\n  ${previous}\n  ${line}")
    endif()
    math(EXPR definitions "${definitions} + 1")
  endif()
  set(previous "${line}")
endforeach()
if(definitions EQUAL 0)
  message(FATAL_ERROR "compile-clean (${NAME}): no definition found in the emitted file; the gate would be vacuous")
endif()

# EVERY COMPILER, A REAL COMPILE, NO OUTPUT.
string(REPLACE "," ";" compilers "${COMPILERS}")
list(LENGTH compilers n)
if(n LESS 2)
  message(FATAL_ERROR "compile-clean (${NAME}): part 24 Phase 63 Step 7 wants two toolchains; got ${n} (${COMPILERS})")
endif()
set(report "")
set(i 0)
set(mutant_rejections 0)
foreach(cxx IN LISTS compilers)
  execute_process(COMMAND "${cxx}" --version OUTPUT_VARIABLE version ERROR_QUIET)
  string(REGEX MATCH "[^\n]*" version "${version}")
  execute_process(COMMAND "${cxx}" -std=c++23 -O2 -pedantic -Wall -Wextra -Werror -Wconversion
                          -ffp-contract=off -c -o "${work}/unit.${i}.o" "${work}/unit.cpp"
                  OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE r)
  if(DEFINED MUTATE)
    if(r EQUAL 0)
      message(FATAL_ERROR "compile-clean (${NAME}): ${version} accepted the generated file with an unused mutant")
    endif()
    string(FIND "${out}${err}" "ctcompile_mutant" mutant_at)
    if(mutant_at EQUAL -1)
      message(FATAL_ERROR "compile-clean (${NAME}): ${version} refused the mutant for an unrelated reason:\n${out}${err}")
    endif()
    math(EXPR mutant_rejections "${mutant_rejections} + 1")
    string(APPEND report "${version}; ")
    math(EXPR i "${i} + 1")
    continue()
  endif()
  if(NOT r EQUAL 0)
    message(FATAL_ERROR "compile-clean (${NAME}): ${version} refused the generated file:\n${out}${err}")
  endif()
  if(NOT "${out}${err}" STREQUAL "")
    message(FATAL_ERROR "compile-clean (${NAME}): ${version} compiled the file but said something, and a warning on generated code is a ctcompile bug:\n${out}${err}")
  endif()
  string(APPEND report "${version}; ")
  math(EXPR i "${i} + 1")
endforeach()
if(DEFINED MUTATE)
  if(NOT mutant_rejections EQUAL n)
    message(FATAL_ERROR "compile-clean (${NAME}): only ${mutant_rejections} of ${n} compilers rejected the mutant")
  endif()
  message(FATAL_ERROR "compile-clean (${NAME}): refused the generated file on all ${n} compilers: ${report}")
endif()
message(STATUS "compile-clean (${NAME}): ${definitions} definitions, each under a provenance comment; clean at -O2 -Wall -Wextra -Wconversion -Werror on ${report}")
