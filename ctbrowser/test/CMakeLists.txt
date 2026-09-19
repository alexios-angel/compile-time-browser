# The system-level suite, and the DATA the rest of the suite reads.
#
# What lives here rather than in unittests/: the lint (a sweep over the tree,
# not a behaviour test), the corpus ratchets (somebody else's bundle, measured),
# and the directories that are pure data - golden/ and baseline/. The standalone package/ consumer and shared
# support/ headers have their own CMake entry points. `ctbrowser_test()` and the probes behind it
# come from cmake/modules/CTTest.cmake.

# Keep the established build/test executable paths as ownership moves below.
if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()
add_subdirectory(support)
add_subdirectory(lint)
add_subdirectory(corpus)

# The comparison/provenance tools must fail promptly and label measured artifacts.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME cmake_hygiene
           COMMAND "${Python3_EXECUTABLE}"
                   "${CTBROWSER_MONOREPO_ROOT}/tools/check/check-cmake-hygiene.py")
  set_tests_properties(cmake_hygiene PROPERTIES TIMEOUT 30)
  foreach(check review_tools baseline)
    add_test(NAME tool_${check}
             COMMAND "${Python3_EXECUTABLE}" "${CTBROWSER_MONOREPO_ROOT}/tools/check/test_${check}.py")
    set_tests_properties(tool_${check} PROPERTIES TIMEOUT 30)
  endforeach()
endif()

# === test262 ===
# The ECMAScript conformance suite. OPT-IN, and off by default for one reason:
# the corpus is 53,580 files that live OUTSIDE this repository (never vendored -
# see tools/fetch-test262.sh) and a machine that has not fetched it cannot run
# any of this. `-DCTBROWSER_TEST262=ON` after `tools/fetch-test262.sh` registers
# two tests; without the fetch the option says so and registers nothing, rather
# than adding a red test whose only meaning is "you did not download 200 MB".
#
# TWO TESTS, and the first is the one that matters:
#
#   test262_selftest  plants eleven answers the runner MUST classify a
#                     particular way - a false assertion reported as FAIL, a
#                     `negative: {phase: parse}` test scored on its type AND its
#                     phase, an async test that never calls $DONE, a skip, a
#                     timeout - and fails if any comes back wrong. A conformance
#                     harness that says PASS to everything looks exactly like a
#                     conforming engine from the outside; this is the difference.
#   test262_gate      a fixed ~250-test subset against
#                     test/test262/expectations.txt. NO REGRESSIONS AND NO
#                     UNEXPECTED PASSES: a test that starts passing fails the
#                     gate too, so the expectations file cannot rot into a list
#                     of excuses nobody has re-measured.
#
# The FULL run is a documented command and not a gate - `tools/check/test262.py
# --dir test/language` is ~6 minutes of process spawns and belongs in a terminal
# somebody is watching. docs/test262.md has the numbers and the commands.
option(CTBROWSER_TEST262 "Register the test262 conformance tests (needs tools/fetch-test262.sh)" OFF)
if(CTBROWSER_TEST262)
  find_package(Python3 QUIET COMPONENTS Interpreter)
  if(DEFINED ENV{TEST262_DIR})
    set(CTBROWSER_TEST262_DIR "$ENV{TEST262_DIR}")
  else()
    set(CTBROWSER_TEST262_DIR "$ENV{HOME}/.cache/ctbrowser/test262")
  endif()
  if(NOT Python3_Interpreter_FOUND)
    message(STATUS "ctbrowser: no Python3 - the test262 tests are not registered")
  elseif(NOT EXISTS "${CTBROWSER_TEST262_DIR}/harness/assert.js")
    message(STATUS "ctbrowser: no test262 corpus at ${CTBROWSER_TEST262_DIR} - "
                   "run tools/fetch-test262.sh; the test262 tests are not registered")
  else()
    set(CTBROWSER_TEST262_RUNNER "${CTBROWSER_MONOREPO_ROOT}/tools/check/test262.py")
    foreach(mode selftest gate)
      if(mode STREQUAL selftest)
        set(flag --self-test)
      else()
        set(flag --gate)
      endif()
      add_test(NAME test262_${mode}
               COMMAND ${Python3_EXECUTABLE} "${CTBROWSER_TEST262_RUNNER}" ${flag}
                       --binary "$<TARGET_FILE:ctbrowser-tool-ct262>"
                       --corpus "${CTBROWSER_TEST262_DIR}"
               WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
      # LABELLED, so `ctest -L test262` is the whole suite and `ctest -LE
      # test262` is everything else - the run is minutes, not seconds, and a
      # developer waiting on a layout fix should be able to say so.
      set_tests_properties(test262_${mode} PROPERTIES LABELS test262 TIMEOUT 900)
    endforeach()
    message(STATUS "ctbrowser: test262 registered against ${CTBROWSER_TEST262_DIR}")
  endif()
endif()
