# JAVASCRIPT TO AN EMITC MODULE THROUGH THE NATIVE PIPELINE - part 24 Phase
# 62½-C. The four passes in the order the lowering documents: recover
# structure, lower every proved function (refuse the rest by name), then
# upstream's scf and arith conversions. The module this writes is what
# check-native-unit.cmake turns into a binary and compares with the
# interpreter, so a refused function here is a red gate there - which is the
# point: the program is native only when every function is.
#
#   -DTRANSLATE= ctjs-translate   -DOPT= ctjs-opt   -DSOURCE= the JS   -DOUTPUT= the module
#
# The module is written WITH debug info: every op keeps its JavaScript
# line:col, which is what Stage 53F's pins name. Without it, re-parsing the
# file gives every op the position in the .mlir file, and a pin would name a
# line of generated IR instead of a line of the program.
cmake_minimum_required(VERSION 3.20)
foreach(required TRANSLATE OPT SOURCE OUTPUT)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "native-pipeline.cmake: -D${required}= is required")
  endif()
endforeach()
execute_process(
  COMMAND "${TRANSLATE}" --ctbrowser-js-to-ctjs "${SOURCE}" --mlir-print-debuginfo
  # EVERYTHING AFTER THE LOWERING RUNS INSIDE emitc.func ONLY. A refused
  # function is still a ctjs.func full of ctjs operations, and the canonicalizer
  # folding an scf.if it left there into an arith.select of ctjs.constants
  # read through a null attribute in arith's fold (valgrind: address 0x10).
  # The nested pipeline is what MLIR provides for exactly this scoping.
  COMMAND "${OPT}" "--pass-pipeline=builtin.module(ctjs-resolve-globals, ctjs-lift-to-scf, ctnative-lower-to-emitc, emitc.func(canonicalize, convert-scf-to-emitc, convert-arith-to-emitc))"
          --mlir-print-debuginfo
  OUTPUT_VARIABLE module
  ERROR_VARIABLE complaints
  RESULTS_VARIABLE outcomes)
foreach(outcome IN LISTS outcomes)
  if(NOT outcome EQUAL 0)
    message(FATAL_ERROR "native-pipeline.cmake: a stage failed (${outcomes})\n${complaints}")
  endif()
endforeach()
# A REFUSED FUNCTION IS NAMED HERE, not discovered as a translation failure
# three steps later: the diagnostic is on the ctjs.func the pass left behind.
string(REGEX MATCHALL "ctnative.not_native = \"[^\"]*\"" refusals "${module}")
if(refusals)
  list(JOIN refusals "\n  " refusals)
  message(FATAL_ERROR "native-pipeline.cmake: ${SOURCE} is not native:\n  ${refusals}")
endif()
file(WRITE "${OUTPUT}" "${module}")
message(STATUS "native pipeline: ${SOURCE} -> ${OUTPUT}, no function refused")
