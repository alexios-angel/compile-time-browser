# THE CAPTURE CENSUS, AS A GATE - part 24 Phase 59 slice 1b.
#
# CTJSOps.td and BytecodeImport.cpp state that 219 of bootstrap's 1,021 capture
# operands are the importer's `undefined` placeholder carrying an
# `enclosing_indices` entry. This runs tools/check/capture-census.py over the
# module the importer actually writes and asserts both figures, so those two
# comments cannot quietly stop being true.
#
# WHY A DRIVER RATHER THAN add_test OF THE SCRIPT DIRECTLY: the census reads a
# module, and the module has to be imported first. Piping inside add_test's
# COMMAND is not portable, so the import happens here and the census reads a
# real file.
#
#   -DTRANSLATE= ctjs-translate
#   -DJS=        the corpus
#   -DCENSUS=    tools/check/capture-census.py
#   -DINDEXED=   expected placeholder-with-an-index slots (floor AND ceiling)
#   -DOPERANDS=  expected total capture operands
#   -DWORK=      a writable directory
cmake_minimum_required(VERSION 3.20)

foreach(_v TRANSLATE JS CENSUS INDEXED OPERANDS WORK)
  if(NOT DEFINED ${_v})
    message(FATAL_ERROR "check-capture-census: -D${_v}= is required")
  endif()
endforeach()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(_module "${WORK}/capture-census-import.mlir")
# WORK MAY NOT EXIST WHEN THIS IS DRIVEN BY HAND. Under ctest it is the binary
# directory and always does, but the way to prove this gate has teeth is to run
# it with a wrong -DINDEXED= from a shell - and without this the import's
# OUTPUT_FILE fails first with "No such file or directory", which reads as the
# gate failing for the reason asked of it when it has not run at all.
file(MAKE_DIRECTORY "${WORK}")

# --mlir-print-debuginfo IS NOT PASSED, and that is deliberate: this census
# counts operands and attributes, and the location suffixes would only make the
# lines longer for a textual parse. Every OTHER use of the importer in this tree
# does pass it, because a lowering that loses its JS sites cannot report a
# refusal a person can act on - see the printing gate.
execute_process(
  COMMAND "${TRANSLATE}" --ctbrowser-js-to-ctjs "${JS}"
  OUTPUT_FILE "${_module}"
  ERROR_VARIABLE _import_err
  RESULT_VARIABLE _import_rc)
if(NOT _import_rc EQUAL 0)
  message(FATAL_ERROR "check-capture-census: the import failed (${_import_rc}): ${_import_err}")
endif()

# THE SCRIPT'S OWN EXIT STATUS IS THE ASSERTION. --expect-indexed and
# --expect-operands are each a floor and a ceiling, so a module that grew the
# attribute where a real binding sits fails exactly as loudly as one that
# stopped writing it.
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${CENSUS}"
          --expect-indexed "${INDEXED}" --expect-operands "${OPERANDS}" "${_module}"
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR
    "check-capture-census: the census refused the module the importer wrote.\n"
    "${_out}${_err}")
endif()
message(STATUS "capture census: ${_out}")
