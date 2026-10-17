# Runs a test executable only if nm shows it links no VM, and fails with the
# offending symbols otherwise. A native test (`<name>.native`) is registered
# through this, so "runs without the VM" is checked on every run rather than
# assumed from the link line.
#
#   cmake -DNM=<nm> -DEXE=<test executable> [-DFORBID=<regex>] -P run-without-vm.cmake
#
# FORBID adds symbols the executable must not contain either; css_syntax.native
# passes the CSS tokenizer and parser, since it must load compiled CSS without
# parsing any.
cmake_minimum_required(VERSION 3.20)
foreach(required NM EXE)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "run-without-vm.cmake: -D${required}= is required")
  endif()
endforeach()
execute_process(COMMAND "${NM}" -C "${EXE}" OUTPUT_VARIABLE symbols RESULT_VARIABLE listed
                ERROR_VARIABLE complaint)
# An empty or failed listing would pass every check below vacuously.
if(NOT listed EQUAL 0 OR NOT symbols MATCHES "[ \t]main\n")
  message(FATAL_ERROR "run-without-vm.cmake: nm could not list ${EXE}: ${complaint}")
endif()
# The same pattern the native gate uses (ctcompile/test/CTNative/harness.py VM_SYMBOL).
set(_vm "ctbrowser::(script|aot)::|ct_aot_")
if(DEFINED FORBID)
  set(_vm "${_vm}|${FORBID}")
endif()
# One line per list item, then a filter: a single regex over the whole
# listing that matches `[^\n]*(...)` scans each line again from every start
# position, which took 17 s on bootstrap_layout.native's 4,000 symbols.
# `;`, `[` and `]` would split or bracket CMake list items, so they become
# `_` in the lines (only the report shows them).
string(REGEX REPLACE "[];[]" "_" lines "${symbols}")
string(REPLACE "\n" ";" lines "${lines}")
set(hits ${lines})
list(FILTER hits INCLUDE REGEX "${_vm}")
if(hits)
  list(LENGTH hits count)
  list(SUBLIST hits 0 12 shown)
  list(JOIN shown "\n  " shown)
  message(FATAL_ERROR "${EXE} links ${count} forbidden symbol(s):\n  ${shown}")
endif()
execute_process(COMMAND "${EXE}" RESULT_VARIABLE outcome)
if(NOT outcome EQUAL 0)
  message(FATAL_ERROR "${EXE} exited ${outcome}")
endif()
