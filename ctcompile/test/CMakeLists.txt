# ctcompile's tests. There is no CI in this repository and none is planned, so
# every invariant that would otherwise be a CI check is a test or a build
# error - see the plan's "There is no CI" section. lit and FileCheck arrive
# with the first textual IR, in Phase 7; until then a test is an executable or
# a command with an expected output.
#
# PHASE 0'S INVENTORIES, WALKED AS DATA. bytecode.hpp already static_asserts the
# opcode count; this covers what a static_assert cannot - that the columns are
# internally consistent, that the tables read as TABLES rather than only expand
# as macros, and that the call-path and GC-root inventories hold the properties
# later phases are built on. It also compiles EngineContract.hpp, which is
# nothing but static_asserts about the engine's layout.
add_executable(ctcompile-test-inventories Inventories.cpp)
# THE ROLE CLASSIFIER IS HEADER-ONLY and reads only ctbrowser's ABI table, so
# this test needs no MLIR - which is the point of the experiment it carries:
# whether the roles are derivable is answerable before a line of MLIR is written.
target_link_libraries(ctcompile-test-inventories PRIVATE ctcompile::support)
ctcompile_target(ctcompile-test-inventories)
add_test(NAME ctcompile_inventories COMMAND ctcompile-test-inventories)

# THE STUB HAS TO RUN. Phase -1's gate says ctcompile builds an executable, and
# an executable that builds and then dies on a missing symbol has passed a
# compile and failed the gate. This runs it.
add_test(NAME ctcompile_version COMMAND ctcompile-tool --version)
set_tests_properties(ctcompile_version PROPERTIES
  PASS_REGULAR_EXPRESSION "ctcompile [0-9]+\\.[0-9]+\\.[0-9]+ \\(ctbrowser .* bytecode operations\\)")

# And the usage path, because `ctcompile` with no arguments exiting 0 would mean
# the argument handling is not there at all.
add_test(NAME ctcompile_usage COMMAND ctcompile-tool)
set_tests_properties(ctcompile_usage PROPERTIES WILL_FAIL TRUE)

# `--help` is a SUCCESS, and it prints the usage line rather than an option
# list with no verb in it.
add_test(NAME ctcompile_help COMMAND ctcompile-tool --help)
set_tests_properties(ctcompile_help PROPERTIES
  PASS_REGULAR_EXPRESSION "usage: ctcompile \\[options\\] <application-directory>")

# An unknown option is a USAGE error, not a crash and not a silent success.
# This is the half of the command line Boost.Program_options owns, and the
# reason the catch block names it separately.
add_test(NAME ctcompile_rejects_nonsense COMMAND ctcompile-tool --not-an-option)
set_tests_properties(ctcompile_rejects_nonsense PROPERTIES WILL_FAIL TRUE)

# DOES THE ABI TABLE POINT AT CODE THAT EXISTS? Six of its DELEGATES TO
# citations ended up past the end of files that Phases 3-5 shrank. See the
# header of check-def-citations.cmake for what this can and cannot catch.
add_test(NAME ctcompile_def_citations
         COMMAND ${CMAKE_COMMAND}
                 -DDEF=${CTBROWSER_MONOREPO_ROOT}/ctbrowser/include/ctbrowser/aot/aot_helpers.def
                 -DROOT=${CTBROWSER_MONOREPO_ROOT}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check-def-citations.cmake)

# THE COMPARATOR THAT ACCEPTS PHASE 16A, and the negative cases that stop it
# from accepting anything. A comparator too lenient does not fail to catch a
# bad blueprint - it certifies one.
add_executable(ctcompile-test-html_comparator HtmlComparator.cpp)
target_link_libraries(ctcompile-test-html_comparator PRIVATE ctcompile::html)
ctcompile_target(ctcompile-test-html_comparator)
add_test(NAME ctcompile_html_comparator COMMAND ctcompile-test-html_comparator)

# THE COMPARATOR THAT ACCEPTS PHASE 16B. Every case below leaves
# selector_count() and rule_count() untouched and changes what the page looks
# like - which is exactly the class of difference the two counts cannot see and
# the reason engine::for_each_rule exists.
add_executable(ctcompile-test-css_comparator CssComparator.cpp)
target_link_libraries(ctcompile-test-css_comparator PRIVATE ctcompile::css)
ctcompile_target(ctcompile-test-css_comparator)
add_test(NAME ctcompile_css_comparator COMMAND ctcompile-test-css_comparator)

# THE PROGRAM IMAGE: a round trip that must be exact, and a loader that must
# refuse. An image is executable input - the VM reads its pools with unchecked
# operator[] and one of those reads is a WRITE - so the negative cases here are
# the point of the file.
add_executable(ctcompile-test-program_image ProgramImage.cpp)
target_link_libraries(ctcompile-test-program_image PRIVATE ctcompile::javascript)
ctcompile_target(ctcompile-test-program_image)
add_test(NAME ctcompile_program_image COMMAND ctcompile-test-program_image)

# THE MANIFEST. Phase 1's gate asks for one; this checks the part of it that can
# be wrong, which is the string escaping - a resource name is whatever the
# document said, and a document may say `p5"; drop</script>`.
add_executable(ctcompile-test-manifest Manifest.cpp)
target_link_libraries(ctcompile-test-manifest PRIVATE ctcompile::support)
ctcompile_target(ctcompile-test-manifest)
add_test(NAME ctcompile_manifest COMMAND ctcompile-test-manifest)

# THE APPLICATION BUNDLE, on the same terms as the program image above and for
# a sharper reason: a bundle is a table of offsets read out of a file, and what
# it carries is program images, which are executable input. The last section of
# that file is the one that matters - it asserts the COUNTER that says the
# images were used, because a page that quietly recompiled produces an
# identical document.
add_executable(ctcompile-test-app_bundle AppBundle.cpp)
target_link_libraries(ctcompile-test-app_bundle PRIVATE ctbrowser::ctbrowser)
ctcompile_target(ctcompile-test-app_bundle)
# THE BINARY DIRECTORY IS AN ARGUMENT because two of its cases cannot be run
# here: they are bundles the LAUNCHER has to refuse, and the guard that refuses
# them lives behind a window. It writes them out and check-package.cmake below
# feeds them to the real ctrun.
add_test(NAME ctcompile_app_bundle
         COMMAND ctcompile-test-app_bundle ${CMAKE_CURRENT_BINARY_DIR})

# AND THE WHOLE THING, THE WAY SOMEONE USES IT: a directory in, an executable
# out, and that executable started from somewhere else entirely. It is the only
# test here that runs the compiler, the launcher and the engine together, and
# the only one that could catch them disagreeing.
#
# A pass means more than "exit 0" - `run_bundle` sets require_script_images, so
# the packaged application refuses to start if any of its scripts had to be
# compiled from source. See the header of check-package.cmake.
#
# NOT GUARDED WITH if(TARGET). ctcompile is configured after ctbrowser's tools,
# so the launcher is always there - and a guard would turn "the launcher stopped
# being built" into a test that quietly stops existing, which is the failure
# this file is least able to notice.
add_test(NAME ctcompile_package
         COMMAND ${CMAKE_COMMAND}
                 -DCTCOMPILE=$<TARGET_FILE:ctcompile-tool>
                 -DLAUNCHER=$<TARGET_FILE:ctbrowser-tool-ctrun>
                 -DAPP=${CMAKE_CURRENT_SOURCE_DIR}/app
                 -DOUT=${CMAKE_CURRENT_BINARY_DIR}/packaged-fixture
                 -DREFUSALS=${CMAKE_CURRENT_BINARY_DIR}
                 -DBROWSE=$<TARGET_FILE:ctbrowser-tool-ctbrowse>
                 -DFONTS=${CTBROWSER_MONOREPO_ROOT}/ctbrowser/resources/fonts
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check-package.cmake)
# ORDER, not a mere preference: the two bundles the launcher must refuse are
# written by the test above.
set_tests_properties(ctcompile_package PROPERTIES DEPENDS ctcompile_app_bundle)

# mlir-translate is LLVM's, and it is part of the backend rather than a
# convenience: it is the stage that turns EmitC into C++.
find_program(MLIR_TRANSLATE_EXE mlir-translate HINTS "${LLVM_TOOLS_BINARY_DIR}")

# --- the one test that RUNS what the backend generated ------------------------
#
# Everything else about the EmitC backend is checked by reading its output or by
# compiling it. Neither can see a use-after-free: the freed memory is still
# there and still holds the right bytes until something reuses it. This runs the
# generated code against the real runtime with set_gc_stress on, which is the
# only way to make the collection happen at the moment that matters.
#
# It guards a defect that was real and shipped: `a + b + c` kept the first
# addition's result in a plain C++ local across the second call, and
# ct_aot_binary_op is a safepoint. Under stress the compiled body returned six
# characters where the interpreter returned sixty-five.
#
# THE PIPELINE RUNS AT BUILD TIME rather than being checked in, because a
# generated file in the tree is a copy that can disagree with the generator -
# and this test's whole value is that it exercises the CURRENT backend.
if(TARGET ctjs-translate AND TARGET ctjs-opt AND MLIR_TRANSLATE_EXE)
  set(_gc_js "${CMAKE_CURRENT_SOURCE_DIR}/gc-roots.js")
  set(_gc_cpp "${CMAKE_CURRENT_BINARY_DIR}/gc-roots.generated.cpp")
  add_custom_command(
    OUTPUT "${_gc_cpp}"
    # THE SYMBOL IS RENAMED so the driver's declaration does not depend on how
    # the importer numbers functions - `f` becomes `f$1` becomes `f_1`, and a
    # second function in the fixture would renumber it.
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
            -DOPT=$<TARGET_FILE:ctjs-opt>
            -DMLIR_TRANSLATE=${MLIR_TRANSLATE_EXE}
            -DSOURCE=${_gc_js}
            -DENTRIES=f
            -DOUTPUT=${_gc_cpp}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/compile-js-to-cpp.cmake"
    DEPENDS "${_gc_js}" ctjs-translate ctjs-opt
            "${CMAKE_CURRENT_SOURCE_DIR}/compile-js-to-cpp.cmake"
    COMMENT "Compiling gc-roots.js through the EmitC backend"
    VERBATIM)

  # GENERATED CODE IS NOT THIS PROJECT'S CODE, and the flags that keep the
  # hand-written sources honest do not apply to it. The entry's signature is
  # fixed by ct_aot_entry_fn, so a function that never looks at `argc` has an
  # unused parameter by construction; a JavaScript expression whose value is
  # discarded leaves a variable that is set and never read; and the backend
  # emits no dead-code elimination because that is a later phase's job.
  # -Werror would make every such program a build failure. Narrowed to the
  # three warnings inherent to emitted code rather than turning -Werror off.
  set(CTCOMPILE_GENERATED_WARNINGS
      "-Wno-unused-parameter;-Wno-unused-variable;-Wno-unused-but-set-variable")
  set_source_files_properties("${_gc_cpp}" PROPERTIES
    COMPILE_OPTIONS "${CTCOMPILE_GENERATED_WARNINGS}")

  add_executable(ctcompile-test-gc_roots GCRoots.cpp "${_gc_cpp}")
  target_link_libraries(ctcompile-test-gc_roots PRIVATE ctbrowser::script)
  ctcompile_target(ctcompile-test-gc_roots)
  add_test(NAME ctcompile_gc_roots COMMAND ctcompile-test-gc_roots)
else()
  message(STATUS "ctcompile: gc-roots test unavailable (needs ctjs-opt and mlir-translate)")
endif()

# --- and does it compute what the interpreter computes? -----------------------
#
# The lit tests read the emitted C++ and compile it, ctcompile_linkable links
# it, and ctcompile_gc_roots runs one body with the collector hostile. None of
# them asks whether the ANSWER is right, and a backend can emit fluent,
# linkable, correctly-rooted code that computes the wrong thing.
#
# This runs each body twice against the same context - interpreted, then with
# its compiled entry installed - and compares. Nothing writes an expected answer
# down: "when a CTJS operation and the ctbrowser VM disagree, the VM is correct
# by definition."
if(TARGET ctjs-translate AND TARGET ctjs-opt AND MLIR_TRANSLATE_EXE)
  set(_diff_js "${CMAKE_CURRENT_SOURCE_DIR}/differential.js")
  # THE DRIVER READS THE SAME FILE THE PIPELINE COMPILES, as a raw string.
  #
  # It used to be transcribed into a C++ string beside it, with a comment saying
  # the two must stay identical - and they drifted, in ORDER rather than in
  # text: a function was appended in a different position in each. That is not
  # cosmetic. A compiled body bakes the function INDEX of every closure it
  # builds, and the ABI row for ct_aot_make_closure says outright that "a
  # function index means nothing outside the program it was compiled in", so a
  # reordered fixture makes a compiled body build a closure over a different
  # function. It presented as a case that passed with the wrong answer.
  set(_diff_inc "${CMAKE_CURRENT_BINARY_DIR}/differential.js.inc")
  add_custom_command(
    OUTPUT "${_diff_inc}"
    COMMAND "${CMAKE_COMMAND}" -DSOURCE=${_diff_js} -DOUTPUT=${_diff_inc}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/embed-js.cmake"
    DEPENDS "${_diff_js}" "${CMAKE_CURRENT_SOURCE_DIR}/embed-js.cmake"
    COMMENT "Embedding differential.js for the driver"
    VERBATIM)
  set(_diff_cpp "${CMAKE_CURRENT_BINARY_DIR}/differential.generated.cpp")
  add_custom_command(
    OUTPUT "${_diff_cpp}"
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
            -DOPT=$<TARGET_FILE:ctjs-opt>
            -DMLIR_TRANSLATE=${MLIR_TRANSLATE_EXE}
            -DSOURCE=${_diff_js}
            "-DENTRIES=plus;ge;strict;loose;pick;globals;apply;step;counter;middle;methodish;fn"
            -DOUTPUT=${_diff_cpp}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/compile-js-to-cpp.cmake"
    DEPENDS "${_diff_js}" ctjs-translate ctjs-opt
            "${CMAKE_CURRENT_SOURCE_DIR}/compile-js-to-cpp.cmake"
    COMMENT "Compiling differential.js through the EmitC backend"
    VERBATIM)

  set_source_files_properties("${_diff_cpp}" PROPERTIES
    COMPILE_OPTIONS "${CTCOMPILE_GENERATED_WARNINGS}")
  add_executable(ctcompile-test-differential Differential.cpp "${_diff_cpp}" "${_diff_inc}")
  target_include_directories(ctcompile-test-differential PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(ctcompile-test-differential PRIVATE ctbrowser::script)
  ctcompile_target(ctcompile-test-differential)
  add_test(NAME ctcompile_differential COMMAND ctcompile-test-differential)
endif()

# --- and does any of it LINK? -------------------------------------------------
#
# aot.hpp declares 69 helpers and aot_bridge.cpp defines 32. A call to one of
# the other 37 compiles perfectly and fails at link - which is exactly how
# ct_aot_global_get and ct_aot_negate were emitted for two commits with a green
# suite, because every EmitC lit test uses -fsyntax-only.
#
# This compiles a function exercising everything the backend accepts and LINKS
# it. The link is the whole assertion; the executable barely does anything.
if(TARGET ctjs-translate AND TARGET ctjs-opt AND MLIR_TRANSLATE_EXE)
  set(_link_js "${CMAKE_CURRENT_SOURCE_DIR}/linkable.js")
  set(_link_cpp "${CMAKE_CURRENT_BINARY_DIR}/linkable.generated.cpp")
  add_custom_command(
    OUTPUT "${_link_cpp}"
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
            -DOPT=$<TARGET_FILE:ctjs-opt>
            -DMLIR_TRANSLATE=${MLIR_TRANSLATE_EXE}
            -DSOURCE=${_link_js}
            -DENTRIES=everything
            -DOUTPUT=${_link_cpp}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/compile-js-to-cpp.cmake"
    DEPENDS "${_link_js}" ctjs-translate ctjs-opt
            "${CMAKE_CURRENT_SOURCE_DIR}/compile-js-to-cpp.cmake"
    COMMENT "Compiling linkable.js through the EmitC backend"
    VERBATIM)

  set_source_files_properties("${_link_cpp}" PROPERTIES
    COMPILE_OPTIONS "${CTCOMPILE_GENERATED_WARNINGS}")
  add_executable(ctcompile-test-linkable Linkable.cpp "${_link_cpp}")
  target_link_libraries(ctcompile-test-linkable PRIVATE ctbrowser::script)
  ctcompile_target(ctcompile-test-linkable)
  add_test(NAME ctcompile_linkable COMMAND ctcompile-test-linkable)
endif()

# --- check-ctcompile: the lit suite -------------------------------------------
#
# SEPARATE FROM ctest ON PURPOSE. The plan: "Keep it separate from the existing
# ctbrowser test targets; the browser's golden and corpus infrastructure stays
# exactly as it is, per the Phase -1 constraints." The tests above are C++
# executables and stay that way; lit is for IR, where the thing under test is
# TEXT and FileCheck is the right instrument.
#
# lit IS NOT A BREW FORMULA and cannot be. The llvm bottle ships FileCheck but
# no llvm-lit, and both Ubuntu's python and brew's refuse `pip install` under
# PEP 668 - so it lives in a virtual environment, and tools/Brewfile says so
# where somebody provisioning a box will read it.
if(CTCOMPILE_ENABLE_MLIR)
  find_program(CTCOMPILE_LIT
    NAMES lit llvm-lit
    HINTS "$ENV{HOME}/.lit-venv/bin" "${LLVM_TOOLS_BINARY_DIR}"
    DOC "The lit test runner, for check-ctcompile")

  if(NOT CTCOMPILE_LIT)
    # SAID OUT LOUD RATHER THAN SKIPPED. A check target that quietly does not
    # exist is indistinguishable from one that passes, and this project has
    # already been bitten once by a test that stopped existing.
    message(STATUS
      "ctcompile: lit not found - check-ctcompile is unavailable. "
      "python3 -m venv ~/.lit-venv && ~/.lit-venv/bin/pip install lit")
  else()
    message(STATUS "ctcompile: lit at ${CTCOMPILE_LIT}")
    set(CTCOMPILE_SOURCE_DIR "${PROJECT_SOURCE_DIR}")
    set(CTCOMPILE_BINARY_DIR "${PROJECT_BINARY_DIR}")
    set(CTCOMPILE_TOOLS_DIRS
      "${PROJECT_BINARY_DIR}/tools/ctjs-opt"
      "${PROJECT_BINARY_DIR}/tools/ctjs-translate"
      "${PROJECT_BINARY_DIR}/tools/ctcompile")
    set(LLVM_TOOLS_DIR "${LLVM_TOOLS_BINARY_DIR}")
    configure_lit_site_cfg(
      "${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in"
      "${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py"
      MAIN_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py")

    add_lit_testsuite(check-ctcompile "Running the ctcompile regression tests"
      "${CMAKE_CURRENT_BINARY_DIR}"
      DEPENDS ctjs-opt ctjs-translate ctcompile-tool FileCheck count not)

    # AND RUN BY ctest TOO, so that a green `ctest` means the IR tests ran.
    # Without this the lit suite is a target somebody has to remember, and the
    # gate this repository actually uses is tools/remote-build.sh, which runs
    # ctest and nothing else.
    add_test(NAME ctcompile_lit
             COMMAND "${CTCOMPILE_LIT}" -v "${CMAKE_CURRENT_BINARY_DIR}")
  endif()
endif()
