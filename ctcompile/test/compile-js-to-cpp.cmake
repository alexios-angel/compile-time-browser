# RUN THE WHOLE BACKEND ON ONE JAVASCRIPT FILE, at build time.
#
# A script rather than a chain of add_custom_command PIPEs, because CMake has no
# pipe: each stage would need its own intermediate file and its own rule, and
# the failure of a middle stage would leave a stale output that the next build
# would happily reuse. execute_process runs the pipeline in one go and this
# checks every stage's result.
#
# THE SYMBOL IS RENAMED at the end. The importer suffixes every function with
# its index - `f` becomes `f$1`, which the backend spells `f_1` - so a driver
# declaring the generated entry by name would break the moment the fixture grew
# a second function. Renaming to a fixed symbol keeps that a property of this
# script rather than of every test that uses it.
cmake_minimum_required(VERSION 3.20)

foreach(required TRANSLATE OPT MLIR_TRANSLATE SOURCE OUTPUT)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "compile-js-to-cpp.cmake: -D${required}= is required")
  endif()
endforeach()

execute_process(
  COMMAND "${TRANSLATE}" --ctbrowser-js-to-ctjs "${SOURCE}"
  COMMAND "${OPT}" --ctjs-lower-to-emitc --emitc-eliminate-block-arguments
  # --declare-variables-at-top IS NOT OPTIONAL: EmitC refuses a multi-block
  # function without it, and every compiled body has at least two blocks,
  # because ct_aot_enter's NULL test is a branch.
  COMMAND "${MLIR_TRANSLATE}" --mlir-to-cpp --declare-variables-at-top
  OUTPUT_VARIABLE generated
  ERROR_VARIABLE complaints
  RESULTS_VARIABLE outcomes)

foreach(outcome IN LISTS outcomes)
  if(NOT outcome EQUAL 0)
    message(FATAL_ERROR
      "compile-js-to-cpp.cmake: a stage failed (${outcomes})\n${complaints}")
  endif()
endforeach()

# AN EMPTY RESULT IS A FAILURE, and it is the one this would otherwise miss: if
# the backend REFUSED the function it exits 0 having lowered nothing, and the
# test would then fail at link with an undefined symbol rather than here with a
# reason.
string(LENGTH "${generated}" produced)
if(produced EQUAL 0)
  message(FATAL_ERROR "compile-js-to-cpp.cmake: the pipeline produced nothing")
endif()
if(NOT generated MATCHES "extern \"C\"")
  message(FATAL_ERROR
    "compile-js-to-cpp.cmake: no compiled entry in the output - the backend "
    "refused ${SOURCE}. Run ctjs-opt --ctjs-lower-to-emitc on it and read the "
    "ctjs.not_lowered attribute it leaves behind.\n${generated}")
endif()

# THE ENTRIES ARE RENAMED TO FIXED SYMBOLS.
#
# The importer suffixes every function with its index - `f` becomes `f$1`, which
# the backend spells `f_1` - and the index depends on how many functions precede
# it. A driver declaring the generated entry by that name would break the moment
# the fixture grew a function above it, so the caller names the functions it
# means and this finds whatever index each was given.
#
# ENTRIES IS A LIST, because a differential harness wants several bodies in one
# translation unit: compiling each to its own file would multiply the pipeline
# runs and the link steps for nothing.
if(NOT DEFINED ENTRIES)
  set(ENTRIES "f")
endif()

foreach(wanted IN LISTS ENTRIES)
  string(REGEX MATCH "${wanted}_[0-9]+" emitted_name "${generated}")
  if(emitted_name STREQUAL "")
    message(FATAL_ERROR
      "compile-js-to-cpp.cmake: no compiled entry named ${wanted} in the output - the backend "
      "refused it. Run ctjs-opt --ctjs-lower-to-emitc on ${SOURCE} and read the "
      "ctjs.not_lowered attribute it leaves behind.")
  endif()
  # THE PREFIX IS FIXED AND THE NAME IS THE FIXTURE'S, so a driver declares
  # ctc_<name> and does not care what index the importer chose.
  string(REGEX REPLACE "([^A-Za-z0-9_])${emitted_name}\\(" "\\1ctc_${wanted}(" generated "${generated}")
endforeach()

file(WRITE "${OUTPUT}" "${generated}")
