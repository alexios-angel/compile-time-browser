# The reserved compiled PC stays an ordinary numeric coordinate in format v2,
# so the existing checker must read it as UNCLAIMED without a parser change.
set(recording "${WORK}/escape-oracle-aot.rec")
execute_process(COMMAND "${EXE}" --out "${recording}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "AOT escape oracle regression failed:\n${out}${err}")
endif()
if(out MATCHES "SKIPPED")
  message(STATUS "${out}")
  return()
endif()
execute_process(
  COMMAND "${PYTHON}" "${SCRIPT}" --recording "${recording}" --infer all-escapes
          --name aot-unchecked --expect-unclaimed 4 --expect-violations 0 --expect-sound 0
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "The checker misread the compiled observations:\n${out}${err}")
endif()
message(STATUS "${out}")
