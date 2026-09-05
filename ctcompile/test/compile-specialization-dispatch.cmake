# Preserve boxed dispatch after native argument specialization and heap PE.
cmake_minimum_required(VERSION 3.20)
foreach(required TRANSLATE OPT MLIR_TRANSLATE SOURCE OUTPUT)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "compile-specialization-dispatch.cmake: -D${required}= is required")
  endif()
endforeach()
execute_process(
  COMMAND "${TRANSLATE}" --ctbrowser-js-to-ctjs "${SOURCE}"
  COMMAND "${OPT}" --ctjs-resolve-globals --ctjs-lift-to-scf
          --ctnative-specialize=report=true --ctnative-partial-evaluate=report=true
          --ctnative-prune-unreachable
          --ctjs-lower-to-emitc --ctjs-drop-uncompiled --emitc-eliminate-block-arguments
          --mlir-print-op-on-diagnostic=false
  COMMAND "${MLIR_TRANSLATE}" --mlir-to-cpp --declare-variables-at-top
  OUTPUT_VARIABLE generated ERROR_VARIABLE complaints RESULTS_VARIABLE outcomes)
foreach(outcome IN LISTS outcomes)
  if(NOT outcome EQUAL 0)
    message(FATAL_ERROR "specialization dispatch pipeline failed (${outcomes})\n${complaints}")
  endif()
endforeach()
if(NOT complaints MATCHES "specialization: [1-9][0-9]* variant" OR
   NOT complaints MATCHES "partial evaluation: [1-9][0-9]* function")
  message(FATAL_ERROR "specialization dispatch did not exercise both transformations\n${complaints}")
endif()
foreach(entry IN ITEMS formula drive)
  string(REGEX REPLACE "([^A-Za-z0-9_])${entry}_[0-9]+\\(" "\\1ctc_${entry}(" generated "${generated}")
  if(NOT generated MATCHES "ctc_${entry}\\(")
    message(FATAL_ERROR "optimized boxed entry ${entry} was not generated\n${complaints}")
  endif()
endforeach()
file(WRITE "${OUTPUT}" "${generated}")
