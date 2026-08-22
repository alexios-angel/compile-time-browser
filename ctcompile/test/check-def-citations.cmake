# Does the ABI table point at code that exists?
#
# aot_helpers.def is the contract two code generators are written against, and
# every row carries a DELEGATES TO paragraph citing the runtime that owns its
# semantics - by file and line. The plan is emphatic that those rows were
# "DERIVED, NOT INVENTED": each was proposed against a handler, refuted against
# the same code, then merged.
#
# A LINE NUMBER IN A COMMENT IS A FACT WITH AN EXPIRY DATE. Phases 3, 4 and 5
# moved several hundred lines of run_loop.cpp, call.cpp and vm.hpp, and six
# citations ended up pointing PAST THE END of a file that had shrunk - which is
# the one class of rot a machine can see. The rest of the drift it cannot: a
# citation that still lands inside the file now names a different handler, and
# only a reader can notice.
#
# So this checks the class it can, and the .def's header says what to do about
# the class it cannot: cite by NAME. `run_loop.cpp's VM_CASE(cell_get)` cannot
# drift, and every row repaired here was rewritten that way rather than given a
# fresh number to rot.
#
# Required: -DDEF= -DROOT=

file(READ ${DEF} text)
string(REGEX MATCHALL "[A-Za-z_0-9]+\\.(cpp|hpp|def|h):[0-9]+" citations "${text}")
list(REMOVE_DUPLICATES citations)

set(checked 0)
set(bad "")
foreach(citation IN LISTS citations)
  string(REGEX REPLACE "^(.*):([0-9]+)$" "\\1" cited_file "${citation}")
  string(REGEX REPLACE "^(.*):([0-9]+)$" "\\2" cited_line "${citation}")

  # WHERE THAT FILE IS. The rows cite bare basenames - `run_loop.cpp`,
  # `vm.hpp` - so the tree is searched for them. A basename that matches more
  # than one file is checked against ALL of them and passes if any one is long
  # enough, because the row does not say which it meant and guessing would
  # invent a failure.
  file(GLOB_RECURSE found "${ROOT}/ctbrowser/*/${cited_file}" "${ROOT}/ctbrowser/${cited_file}")
  if(NOT found)
    continue() # not a file in this tree; the row may be citing a standard header
  endif()
  set(fits FALSE)
  foreach(path IN LISTS found)
    # NEWLINES, NOT file(STRINGS). That reads a file as a LIST, which splits on
    # semicolons, mangles escapes and drops lines it cannot represent - so the
    # count it gives for a C++ file is not the number of lines in it. The first
    # version of this check reported run_loop.cpp:1460 as past the end of a
    # 1502-line file for exactly that reason.
    file(READ "${path}" content)
    string(REGEX MATCHALL "\n" newlines "${content}")
    list(LENGTH newlines count)
    if(NOT cited_line GREATER count)
      set(fits TRUE)
    endif()
  endforeach()
  math(EXPR checked "${checked} + 1")
  if(NOT fits)
    list(APPEND bad "${citation}")
  endif()
endforeach()

if(bad)
  string(REPLACE ";" "\n  " report "${bad}")
  message(FATAL_ERROR
          "the ABI table cites lines past the end of the files it names:\n  ${report}\n"
          "Repair by citing the handler BY NAME - \"run_loop.cpp's VM_CASE(cell_get)\" - rather "
          "than by a fresh line number, which will rot again the next time that file moves.")
endif()
message("ok def_citations (${checked} file:line citations, all inside their files)")
