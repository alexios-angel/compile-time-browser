# Separate scripts give each prototype mutation a fresh runtime context. The
# existing fixture sources and allocation coordinates stay unchanged.
function(check_inherited_accessors)
  foreach(_case getter setter iteration)
    set(_source "${CMAKE_CURRENT_LIST_DIR}/inherited-${_case}.js")
    set(_rec "${WORK}/escape-inherited-${_case}.rec")
    set(_claims "${WORK}/escape-inherited-${_case}.claims")
    execute_process(
      COMMAND "${ORACLE}" --script "${_source}" --out "${_rec}" --escape-budget 0
      OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR "inherited ${_case} recording failed:\n${_out}${_err}")
    endif()
    execute_process(
      COMMAND "${CLAIMS}" --script "${_source}" --out "${_claims}"
      OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR "inherited ${_case} claims failed:\n${_out}${_err}")
    endif()
    execute_process(
      COMMAND "${PYTHON}" "${SCRIPT}" --recording "${_rec}" --claims "${_claims}"
              --name "inherited ${_case}" --max-report 0 --expect-violations 0
      OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
    message(STATUS "${_out}${_err}")
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR "inherited ${_case} oracle disagrees with the compiler")
    endif()

    # Join by the program/function/pc coordinate, not by allocation order. The
    # child argument must stay Stored after deletion; receiver sinks alone do
    # not prevent an unsound contents proof from clearing that claim.
    file(STRINGS "${_rec}" _lines)
    file(READ "${_claims}" _claim_text)
    set(_rows "")
    set(_function "")
    foreach(_line IN LISTS _lines)
      if(_line MATCHES "^program ([0-9a-f]+) ")
        set(_hash "${CMAKE_MATCH_1}")
      elseif(_line MATCHES "^fn ([0-9]+) .* name ([^ ]+)$")
        set(_index "${CMAKE_MATCH_1}")
        set(_function "${CMAKE_MATCH_2}")
      elseif(_function MATCHES "^(read|write|iterate)Inherited$" AND _line MATCHES "^site [0-9]+ kind obj ")
        if(NOT _line MATCHES "^site ([0-9]+) kind obj made 1 confined 0 escaped 1 unresolved 0 unchecked 0 routes globals:1$")
          message(FATAL_ERROR "inherited ${_case} did not retain its allocation: ${_line}")
        endif()
        set(_pc "${CMAKE_MATCH_1}")
        if(NOT _claim_text MATCHES "escape ${_hash} ${_index} ${_pc} obj ([^\n]+)")
          message(FATAL_ERROR "inherited ${_case}: missing compiler claim at pc ${_pc}")
        endif()
        list(APPEND _rows "${_function} pc${_pc} ${CMAKE_MATCH_1}")
      endif()
    endforeach()
    if(_case STREQUAL "getter")
      set(_expected "readInherited pc1 escapes:passed")
    elseif(_case STREQUAL "setter")
      set(_expected "writeInherited pc2 escapes:stored" "writeInherited pc4 escapes:passed")
    else()
      set(_expected "iterateInherited pc1 escapes:passed")
    endif()
    list(SORT _rows)
    if(NOT _rows STREQUAL _expected)
      message(FATAL_ERROR "inherited ${_case} claims mismatch:\nexpected: ${_expected}\nobserved: ${_rows}")
    endif()
  endforeach()
endfunction()

check_inherited_accessors()
