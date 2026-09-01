# RUN THE GENERATED APPLICATION, AND THE SAME APPLICATION INTERPRETED.
#
# Two executables built from one driver: one linked against the compiled bodies
# and their entry table, one not. This runs both and asks two different
# questions of them.
#
# THE FIRST QUESTION IS WHETHER THEY AGREE, and it is the easy one - the
# transcripts must be byte-identical.
#
# THE SECOND IS WHETHER THE COMPILED ARM COMPILED ANYTHING, and it is the one
# this file exists for. The two arms run the same program, so an application
# that installed nothing and interpreted everything passes the first question
# perfectly. Only the dispatch counters can tell them apart, and only if they
# are asserted rather than printed: `ctbrowser/lib/Script/dispatch.cpp` counts
# every crossing between C++, the interpreter and a compiled body, so a strict
# build must show the interpreter NEVER RAN.
#
# Required: -DVM= -DAOT= -DWORK=
cmake_minimum_required(VERSION 3.20)

foreach(required VM AOT WORK)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "check-launcher.cmake: -D${required}= is required")
  endif()
endforeach()

# HOW MANY TIMES ONE COMPILED BODY CALLS ANOTHER, and it is pinned rather than
# bounded. `> 0` would pass on an application whose top level is compiled and
# whose every call fell back, which is the interesting half of the failure. The
# number is the fixture's own call count - 6 frames of 4 ships, plus the
# scaffolding - and if launcher.js changes, this changes with it: run the aot
# arm and read the line it prints.
set(EXPECTED_AOT_TO_AOT 80)
# And the whole program is one crossing from C++: `context::execute` asks
# `enter_compiled` for the top level before it pushes a frame, so a program
# whose top level compiled is entered exactly once and never returns to C++.
set(EXPECTED_CXX_TO_AOT 1)

function(run_arm which binary out_stdout out_stderr)
  execute_process(
    COMMAND ${binary}
    RESULT_VARIABLE code
    OUTPUT_FILE "${WORK}/launcher-${which}.out"
    ERROR_VARIABLE complaints)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "check-launcher.cmake: the ${which} arm exited ${code}\n${complaints}")
  endif()
  file(READ "${WORK}/launcher-${which}.out" produced)
  set(${out_stdout} "${produced}" PARENT_SCOPE)
  set(${out_stderr} "${complaints}" PARENT_SCOPE)
endfunction()

# READ ONE COUNTER OUT OF AN ARM'S REPORT, refusing rather than defaulting when
# the line is absent: a missing counter that read as zero would make every
# assertion below pass on an arm that printed nothing at all.
function(counter report name into)
  string(REPLACE "+" "\\+" pattern "${name}")
  if(NOT report MATCHES "transition \"${pattern}\" = ([0-9]+)")
    message(FATAL_ERROR
      "check-launcher.cmake: no counter named ${name} in this arm's report:\n${report}")
  endif()
  set(${into} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

run_arm(vm "${VM}" vm_out vm_err)
run_arm(aot "${AOT}" aot_out aot_err)

# ---- the transcripts ---------------------------------------------------------
if(NOT vm_out STREQUAL aot_out)
  message(FATAL_ERROR
    "check-launcher.cmake: the generated application does not print what the interpreted one "
    "prints.\n  interpreted:\n${vm_out}\n  compiled:\n${aot_out}")
endif()
string(LENGTH "${vm_out}" transcript_bytes)
if(transcript_bytes EQUAL 0)
  message(FATAL_ERROR "check-launcher.cmake: both arms printed nothing, so they agree vacuously")
endif()

# ---- did the compiled arm install anything? ----------------------------------
#
# The count comes from the application rather than from here, because only it
# knows how many functions its program has - and asserting `interpreted 0` is
# what makes this a STRICT build rather than a hybrid one that happened to
# compile most of it.
if(NOT aot_err MATCHES "installed ([0-9]+) of ([0-9]+), interpreted ([0-9]+)")
  message(FATAL_ERROR "check-launcher.cmake: the aot arm did not report an install:\n${aot_err}")
endif()
set(installed ${CMAKE_MATCH_1})
set(functions ${CMAKE_MATCH_2})
set(left ${CMAKE_MATCH_3})
if(NOT left EQUAL 0)
  message(FATAL_ERROR
    "check-launcher.cmake: ${left} of ${functions} functions were left interpreted - "
    "install_strict was supposed to refuse to start")
endif()
if(installed LESS 2)
  message(FATAL_ERROR
    "check-launcher.cmake: the aot arm installed ${installed} entr(y/ies). A single-entry "
    "application cannot cross AOT -> AOT and this test would be asserting nothing.")
endif()

# ---- and did the interpreter run? -------------------------------------------
#
# THIS IS THE ASSERTION THE TEST IS FOR. All three of these are ways for the
# interpreter to be reached, and in an AOT-only build every one of them must be
# zero - a nonzero `vm -> aot` means the interpreter entered a compiled body,
# which means the interpreter was running.
foreach(never "C++ -> VM" "VM -> AOT" "AOT -> VM")
  counter("${aot_err}" "${never}" seen)
  if(NOT seen EQUAL 0)
    message(FATAL_ERROR
      "check-launcher.cmake: the aot arm crossed ${never} ${seen} time(s), so the interpreter "
      "ran. This is not an AOT-only application.\n${aot_err}")
  endif()
endforeach()

counter("${aot_err}" "C++ -> AOT" entered)
if(NOT entered EQUAL ${EXPECTED_CXX_TO_AOT})
  message(FATAL_ERROR
    "check-launcher.cmake: the aot arm was entered from C++ ${entered} time(s), expected "
    "${EXPECTED_CXX_TO_AOT}")
endif()
counter("${aot_err}" "AOT -> AOT" chained)
if(NOT chained EQUAL ${EXPECTED_AOT_TO_AOT})
  message(FATAL_ERROR
    "check-launcher.cmake: one compiled body called another ${chained} time(s), expected "
    "${EXPECTED_AOT_TO_AOT}. If launcher.js changed, this number changes with it; if it did "
    "not, a call fell back to the interpreter.")
endif()

# ---- THE BLINDED ARM --------------------------------------------------------
#
# The same driver with nothing linked into it. If IT also reports compiled
# crossings then the counters are not measuring what this file claims, and every
# assertion above is about something else.
foreach(never "C++ -> AOT" "VM -> AOT" "AOT -> AOT" "AOT -> VM")
  counter("${vm_err}" "${never}" seen)
  if(NOT seen EQUAL 0)
    message(FATAL_ERROR
      "check-launcher.cmake: the arm with NO generated code crossed ${never} ${seen} time(s) - "
      "the counters are not measuring compiled dispatch:\n${vm_err}")
  endif()
endforeach()
counter("${vm_err}" "C++ -> VM" ran_interpreted)
if(ran_interpreted LESS 1)
  message(FATAL_ERROR
    "check-launcher.cmake: the interpreted arm never entered the interpreter:\n${vm_err}")
endif()

message("ok ctcompile_launcher: ${installed}/${functions} functions compiled, "
        "${transcript_bytes} bytes of transcript identical, ${chained} AOT -> AOT crossings, "
        "the interpreter never ran")
