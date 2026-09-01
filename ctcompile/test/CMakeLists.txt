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

# PHASE 13'S COVERAGE MEASUREMENT, derived from both sides rather than written
# down. The opcode list comes from the X-macro so it cannot disagree with the
# engine; what the importer handles is read out of the importer's own SOURCE, so
# it cannot disagree with the importer. Neither side is a transcription, which
# is what has drifted here before.
#
# IT NEEDS NO MLIR either - it reads a .cpp as text.
add_executable(ctcompile-test-importer-coverage ImporterCoverage.cpp)
target_compile_definitions(ctcompile-test-importer-coverage PRIVATE
  CTCOMPILE_IMPORTER_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/../lib/CTJS/Import/BytecodeImport.cpp")
target_link_libraries(ctcompile-test-importer-coverage PRIVATE ctcompile::support)
ctcompile_target(ctcompile-test-importer-coverage)
add_test(NAME ctcompile_importer_coverage COMMAND ctcompile-test-importer-coverage)

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
  # THE DRIVER READS THE SAME FILE THE PIPELINE COMPILES, for the reason
  # spelled out over differential.js below - and gc-roots.js had the defect in
  # a worse form. Its C++ copy was not a transcription that had drifted but a
  # DIFFERENT PROGRAM: the compiled bodies here, the drivers there. `held`
  # builds a closure and a compiled body bakes that closure's function index,
  # so the two agreed only because the extra functions happened to sit after
  # `keep`.
  set(_gc_inc "${CMAKE_CURRENT_BINARY_DIR}/gc-roots.js.inc")
  add_custom_command(
    OUTPUT "${_gc_inc}"
    COMMAND "${CMAKE_COMMAND}" -DSOURCE=${_gc_js} -DOUTPUT=${_gc_inc}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/embed-js.cmake"
    DEPENDS "${_gc_js}" "${CMAKE_CURRENT_SOURCE_DIR}/embed-js.cmake"
    COMMENT "Embedding gc-roots.js for the driver"
    VERBATIM)
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
            "-DENTRIES=f;held;built"
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

  add_executable(ctcompile-test-gc_roots GCRoots.cpp "${_gc_cpp}" "${_gc_inc}")
  target_include_directories(ctcompile-test-gc_roots PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
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
            "-DENTRIES=plus;ge;strict;loose;pick;globals;apply;step;counter;middle;methodish;fn;neg;bnot;put;greet;pack;kindOf;thrower;build;Point;newBad;coalesce;chain;dflt;total;chars;spread;hasIt;isA;drop;greetChain;Kid;spreadCall;spreadMethod;spreadNew;merge;mergeArray;firstLoop;accessors;guarded;plusOf;coerce;keysOf;dropNamed;bigLits"
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

# --- PHASE 18: an application, generated, linked, and RUN ---------------------
#
# ONE BLOCK, APPENDED, so that the merge with the other tracks working in this
# file is a concatenation. Everything it needs is its own file except the two
# scripts it reuses.
#
# What every test above this asks is whether a piece of the compiler works.
# What this asks is whether the thing exists: a JavaScript application, compiled
# to C++ by the real pipeline, linked into a native executable, installing its
# own compiled bodies at startup with no hand-written table anywhere - and
# printing the same bytes as the same application interpreted.
#
# TWO EXECUTABLES FROM ONE DRIVER. LauncherApp.cpp is compiled twice: once
# alone, once with the generated bodies and the generated entry table. That is
# what makes the comparison mean something - the arms differ in exactly the two
# generated objects and a preprocessor symbol.
#
# THE COUNTERS ARE THE ASSERTION, not the transcripts. Both arms run the same
# program, so an application that silently interpreted everything prints
# identical bytes and exits 0. check-launcher.cmake asserts instead that the
# compiled arm never crossed C++ -> VM, VM -> AOT or AOT -> VM: the interpreter
# did not run at all.
if(TARGET ctjs-translate AND TARGET ctjs-opt AND MLIR_TRANSLATE_EXE)
  set(_app_js "${CMAKE_CURRENT_SOURCE_DIR}/launcher.js")

  # The driver reads the same file the pipeline compiles, for the reason spelled
  # out over differential.js above.
  set(_app_inc "${CMAKE_CURRENT_BINARY_DIR}/launcher.js.inc")
  add_custom_command(
    OUTPUT "${_app_inc}"
    COMMAND "${CMAKE_COMMAND}" -DSOURCE=${_app_js} -DOUTPUT=${_app_inc}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/embed-js.cmake"
    DEPENDS "${_app_js}" "${CMAKE_CURRENT_SOURCE_DIR}/embed-js.cmake"
    COMMENT "Embedding launcher.js for the launcher driver"
    VERBATIM)

  # THE WHOLE FILE, WITH NOTHING RENAMED. Every other use of
  # compile-js-to-cpp.cmake passes -DENTRIES= a list of functions to rename to
  # fixed symbols, because a driver has to DECLARE what it calls. An application
  # declares nothing: the symbols keep the names the backend gave them, which is
  # what carries each body's function index to the installer.
  set(_app_cpp "${CMAKE_CURRENT_BINARY_DIR}/launcher.generated.cpp")
  add_custom_command(
    OUTPUT "${_app_cpp}"
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
            -DOPT=$<TARGET_FILE:ctjs-opt>
            -DMLIR_TRANSLATE=${MLIR_TRANSLATE_EXE}
            -DSOURCE=${_app_js}
            "-DENTRIES="
            -DOUTPUT=${_app_cpp}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/compile-js-to-cpp.cmake"
    DEPENDS "${_app_js}" ctjs-translate ctjs-opt
            "${CMAKE_CURRENT_SOURCE_DIR}/compile-js-to-cpp.cmake"
    COMMENT "Compiling launcher.js through the EmitC backend"
    VERBATIM)

  # AND THE TABLE THAT INSTALLS THEM, read out of the emitted C++ rather than
  # written down: the symbols that exist in the object file are the definitive
  # list of what this build can install.
  set(_app_table "${CMAKE_CURRENT_BINARY_DIR}/launcher.entries.cpp")
  add_custom_command(
    OUTPUT "${_app_table}"
    COMMAND "${CMAKE_COMMAND}"
            -DGENERATED=${_app_cpp}
            -DOUTPUT=${_app_table}
            -DTABLE=ctc_launcher_entries
            -DSOURCE_NAME=launcher.js
            -P "${CMAKE_CURRENT_SOURCE_DIR}/aot-entry-table.cmake"
    DEPENDS "${_app_cpp}" "${CMAKE_CURRENT_SOURCE_DIR}/aot-entry-table.cmake"
    COMMENT "Writing the entry table for launcher.js"
    VERBATIM)

  # Set above beside the first generated file; repeated here only if this block
  # is ever separated from it, because generated code is not this project's code
  # and -Werror on it makes an unused parameter a build failure.
  if(NOT DEFINED CTCOMPILE_GENERATED_WARNINGS)
    set(CTCOMPILE_GENERATED_WARNINGS
        "-Wno-unused-parameter;-Wno-unused-variable;-Wno-unused-but-set-variable")
  endif()
  set_source_files_properties("${_app_cpp}" PROPERTIES
    COMPILE_OPTIONS "${CTCOMPILE_GENERATED_WARNINGS}")

  # THE BLINDED ARM, and it is the same source file. Nothing generated is linked
  # into it, so it can only interpret - which is what makes the counters the
  # compiled arm reports mean anything at all.
  add_executable(ctcompile-test-launcher-vm LauncherApp.cpp "${_app_inc}")
  target_include_directories(ctcompile-test-launcher-vm PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(ctcompile-test-launcher-vm PRIVATE ctbrowser::script)
  ctcompile_target(ctcompile-test-launcher-vm)

  add_executable(ctcompile-test-launcher-aot
    LauncherApp.cpp "${_app_cpp}" "${_app_table}" "${_app_inc}")
  target_include_directories(ctcompile-test-launcher-aot PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  # THE TABLE'S NAME IS A BUILD PARAMETER, not a fixed symbol, because an
  # application with several compiled scripts has one table each and they cannot
  # all be called the same thing.
  target_compile_definitions(ctcompile-test-launcher-aot PRIVATE
    CTCOMPILE_LAUNCHER_AOT=1 CTCOMPILE_LAUNCHER_TABLE=ctc_launcher_entries)
  target_link_libraries(ctcompile-test-launcher-aot PRIVATE ctbrowser::script)
  ctcompile_target(ctcompile-test-launcher-aot)

  add_test(NAME ctcompile_launcher
           COMMAND ${CMAKE_COMMAND}
                   -DVM=$<TARGET_FILE:ctcompile-test-launcher-vm>
                   -DAOT=$<TARGET_FILE:ctcompile-test-launcher-aot>
                   -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/check-launcher.cmake)
endif()

# --- PHASE 18, WIDENED: a real page from ctbrowser/examples ------------------
#
# The block above is a JavaScript program against a bare script::context. This
# one is `examples/pages/invaders.html` - a game with a canvas, a sprite sheet,
# event listeners and requestAnimationFrame - running the whole engine with all
# FOUR of its functions compiled ahead of time.
#
# It is the page rather than a corpus library because its whole script lowers:
# ctjs-translate imports four functions from it and ctjs-opt refuses none, so
# strict AOT-only is reachable on a real page today. Nothing else in
# examples/pages is close.
if(TARGET ctjs-translate AND TARGET ctjs-opt AND MLIR_TRANSLATE_EXE)
  set(_page_html "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/examples/pages/invaders.html")

  # THE SCRIPT AS THE BROWSER WILL ASSEMBLE IT, trailing newline and all - see
  # the header of extract-inline-script.cmake for why that newline is not
  # cosmetic and what checks the extraction.
  set(_page_js "${CMAKE_CURRENT_BINARY_DIR}/invaders.js")
  add_custom_command(
    OUTPUT "${_page_js}"
    COMMAND "${CMAKE_COMMAND}" -DPAGE=${_page_html} -DOUTPUT=${_page_js}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/extract-inline-script.cmake"
    DEPENDS "${_page_html}" "${CMAKE_CURRENT_SOURCE_DIR}/extract-inline-script.cmake"
    COMMENT "Extracting invaders.html's script"
    VERBATIM)

  set(_page_cpp "${CMAKE_CURRENT_BINARY_DIR}/invaders.generated.cpp")
  add_custom_command(
    OUTPUT "${_page_cpp}"
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
            -DOPT=$<TARGET_FILE:ctjs-opt>
            -DMLIR_TRANSLATE=${MLIR_TRANSLATE_EXE}
            -DSOURCE=${_page_js}
            "-DENTRIES="
            -DOUTPUT=${_page_cpp}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/compile-js-to-cpp.cmake"
    DEPENDS "${_page_js}" ctjs-translate ctjs-opt
            "${CMAKE_CURRENT_SOURCE_DIR}/compile-js-to-cpp.cmake"
    COMMENT "Compiling invaders.html's script through the EmitC backend"
    VERBATIM)

  set(_page_table "${CMAKE_CURRENT_BINARY_DIR}/invaders.entries.cpp")
  add_custom_command(
    OUTPUT "${_page_table}"
    COMMAND "${CMAKE_COMMAND}"
            -DGENERATED=${_page_cpp}
            -DOUTPUT=${_page_table}
            -DTABLE=ctc_invaders_entries
            -DSOURCE_NAME=invaders.html
            -P "${CMAKE_CURRENT_SOURCE_DIR}/aot-entry-table.cmake"
    DEPENDS "${_page_cpp}" "${CMAKE_CURRENT_SOURCE_DIR}/aot-entry-table.cmake"
    COMMENT "Writing the entry table for invaders.html"
    VERBATIM)

  set_source_files_properties("${_page_cpp}" PROPERTIES
    COMPILE_OPTIONS "${CTCOMPILE_GENERATED_WARNINGS}")

  # THE ASSET ROOT IS ctbrowser/, because the page names its sprite sheet
  # `examples/assets/sprites.bmp` - a path relative to the tree, which is how
  # ctbrowse is run and therefore what the page was written against.
  set(_page_defs
    CTCOMPILE_LAUNCHER_PAGE="${_page_html}"
    CTCOMPILE_LAUNCHER_ASSET_ROOT="${CTBROWSER_MONOREPO_ROOT}/ctbrowser")

  add_executable(ctcompile-test-launcher-page-vm LauncherPage.cpp)
  target_compile_definitions(ctcompile-test-launcher-page-vm PRIVATE ${_page_defs})
  target_link_libraries(ctcompile-test-launcher-page-vm PRIVATE ctbrowser::ctbrowser)
  ctcompile_target(ctcompile-test-launcher-page-vm)

  add_executable(ctcompile-test-launcher-page-aot
    LauncherPage.cpp "${_page_cpp}" "${_page_table}")
  target_compile_definitions(ctcompile-test-launcher-page-aot PRIVATE ${_page_defs}
    CTCOMPILE_LAUNCHER_AOT=1 CTCOMPILE_LAUNCHER_TABLE=ctc_invaders_entries)
  target_link_libraries(ctcompile-test-launcher-page-aot PRIVATE ctbrowser::ctbrowser)
  ctcompile_target(ctcompile-test-launcher-page-aot)

  add_test(NAME ctcompile_launcher_page
           COMMAND ${CMAKE_COMMAND}
                   -DVM=$<TARGET_FILE:ctcompile-test-launcher-page-vm>
                   -DAOT=$<TARGET_FILE:ctcompile-test-launcher-page-aot>
                   -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/check-launcher-page.cmake)
endif()
