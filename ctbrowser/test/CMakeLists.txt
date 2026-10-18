# The system-level suite, and the data the rest of the suite reads.
#
# What lives here rather than in unittests/: the lint (a sweep over the tree,
# not a behaviour test), the corpus ratchets (third-party bundles, measured),
# and the directories that are pure data, golden/ and baseline/. The
# standalone package/ consumer and the shared support/ headers have their own
# CMake entry points. `ctbrowser_test()` and the probes behind it come from
# cmake/modules/CTTest.cmake.

# Put every test executable directly in this binary directory, where scripts
# look for them.
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
endif()

# test_review_tools.py and test_baseline.py are pytest files: running one with
# Python only defines its tests. pytest comes from the repository's uv
# environment (pyproject.toml's `test` group, which tools/remote-build.sh
# syncs into .venv at the repository root), so these run that environment's
# pytest, as ctcompile/test/cmake/Lit.cmake runs its lit.
find_program(CTBROWSER_PYTEST
  NAMES pytest
  HINTS "${CTBROWSER_MONOREPO_ROOT}/.venv/bin"
  NO_DEFAULT_PATH
  DOC "pytest from the repository's uv environment, for the tool_* tests")
if(CTBROWSER_PYTEST)
  foreach(check review_tools baseline)
    add_test(NAME tool_${check}
             COMMAND "${CTBROWSER_PYTEST}" -q -p no:cacheprovider
                     "${CTBROWSER_MONOREPO_ROOT}/tools/check/test_${check}.py")
    set_tests_properties(tool_${check} PROPERTIES TIMEOUT 30)
  endforeach()
else()
  # Say so rather than skip silently: a missing test looks like a passing one.
  message(STATUS
    "ctbrowser: pytest not found in ${CTBROWSER_MONOREPO_ROOT}/.venv - tool_review_tools "
    "and tool_baseline are unavailable. Run `uv sync --group test` at the repository root")
endif()

# === test262 ===
# The ECMAScript conformance suite. It is opt-in and off by default because the
# corpus is 53,580 files kept outside this repository (never vendored; see
# tools/fetch-test262.sh), and a machine that has not fetched it cannot run any
# of this. `-DCTBROWSER_TEST262=ON` after `tools/fetch-test262.sh` registers two
# tests; without the fetch the option says so and registers nothing, rather
# than adding a red test whose only meaning is "you did not download 200 MB".
#
# Two tests, and the first is the one that matters:
#
#   test262_selftest  plants eleven answers the runner must classify a
#                     particular way (a false assertion reported as FAIL, a
#                     `negative: {phase: parse}` test scored on both its type and
#                     its phase, an async test that never calls $DONE, a skip,
#                     a timeout) and fails if any comes back wrong. A harness
#                     that says PASS to everything looks exactly like a
#                     conforming engine from the outside; this tells them apart.
#   test262_gate      a fixed ~250-test subset against
#                     test/test262/expectations.txt. Neither regressions nor
#                     unexpected passes are allowed: a test that starts passing
#                     fails the gate too, so the expectations file cannot rot
#                     into a list of excuses nobody has re-measured.
#
# The full run is a documented command, not a test: `tools/check/test262.py
# --dir test/language` takes ~6 minutes of process spawns and belongs in a
# terminal somebody is watching. docs/test262.md has the numbers and commands.
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
      # Labelled, so `ctest -L test262` runs only this suite and `ctest -LE
      # test262` runs everything else: the run takes minutes, and a developer
      # waiting on a layout fix should be able to skip it.
      set_tests_properties(test262_${mode} PROPERTIES LABELS test262 TIMEOUT 900)
    endforeach()
    message(STATUS "ctbrowser: test262 registered against ${CTBROWSER_TEST262_DIR}")
  endif()
endif()
