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
            "-DENTRIES=plus;ge;strict;loose;pick;globals;apply;step;counter;middle;methodish;fn;neg;bnot;put;greet;pack;kindOf;thrower;build;Point;newBad;coalesce;chain;dflt;total;chars;spread;hasIt;isA;drop;greetChain;Kid;spreadCall;spreadMethod;spreadNew;merge;mergeArray;firstLoop;accessors;guarded;plusOf;coerce;keysOf;dropNamed;bigLits;howMany;sumAll;restOf;bothOf;wrapped;passes;arrayOut"
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

# --- and the same question for a MODULE ---------------------------------------
#
# ctcompile_differential compiles ONE CLASSIC SCRIPT and runs it with cx.run.
# op::load_import, op::bind_export and op::load_namespace are emitted by
# compile_program's module arm and by nowhere else, so a classic script cannot
# contain one and that harness cannot reach three of the four ES-module opcodes
# at all. This builds a three-module graph the way browser.cpp does and installs
# a compiled entry on the IMPORTING module's TOP LEVEL - the first time the
# backend is run over functions[0] rather than over a named function.
#
# ONLY module-main.js IS COMPILED. dep and user stay interpreted in every arm,
# which is what holds one variable still: dep is where the live binding is
# written, and user is the only thing that can see what bind_export decided.
if(TARGET ctjs-translate AND TARGET ctjs-opt AND MLIR_TRANSLATE_EXE)
  set(_mod_main "${CMAKE_CURRENT_SOURCE_DIR}/module-main.js")
  set(_mod_dep "${CMAKE_CURRENT_SOURCE_DIR}/module-dep.js")
  set(_mod_user "${CMAKE_CURRENT_SOURCE_DIR}/module-user.js")
  # ALL THREE ARE EMBEDDED, for the reason differential.js is: the driver must
  # read the same bytes the pipeline compiled. A compiled body bakes the
  # function INDEX of every closure it builds, so a transcription that drifted
  # in ORDER makes it close over a different function while both tiers agree.
  set(_mod_incs "")
  foreach(_mod_js IN ITEMS "${_mod_main}" "${_mod_dep}" "${_mod_user}")
    get_filename_component(_mod_name "${_mod_js}" NAME)
    set(_mod_inc "${CMAKE_CURRENT_BINARY_DIR}/${_mod_name}.inc")
    add_custom_command(
      OUTPUT "${_mod_inc}"
      COMMAND "${CMAKE_COMMAND}" -DSOURCE=${_mod_js} -DOUTPUT=${_mod_inc}
              -P "${CMAKE_CURRENT_SOURCE_DIR}/embed-js.cmake"
      DEPENDS "${_mod_js}" "${CMAKE_CURRENT_SOURCE_DIR}/embed-js.cmake"
      COMMENT "Embedding ${_mod_name} for the module driver"
      VERBATIM)
    list(APPEND _mod_incs "${_mod_inc}")
  endforeach()

  set(_mod_cpp "${CMAKE_CURRENT_BINARY_DIR}/module-main.generated.cpp")
  add_custom_command(
    OUTPUT "${_mod_cpp}"
    # -DKIND=module SELECTS --ctbrowser-module-to-ctjs. Without it the fixture
    # is compiled as a classic script, every `import` is a syntax error, and the
    # failure reads as a bad fixture rather than as a missing flag.
    #
    # `_script_` IS THE TOP LEVEL. compile/entry.cpp names functions[0]
    # "<script>", the importer sanitises that to `_script_$0` and the backend
    # spells it `_script__0`, which this script renames to ctc__script_.
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
            -DOPT=$<TARGET_FILE:ctjs-opt>
            -DMLIR_TRANSLATE=${MLIR_TRANSLATE_EXE}
            -DSOURCE=${_mod_main}
            -DKIND=module
            "-DENTRIES=_script_;raise2;loadTwo;fn"
            -DOUTPUT=${_mod_cpp}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/compile-js-to-cpp.cmake"
    DEPENDS "${_mod_main}" ctjs-translate ctjs-opt
            "${CMAKE_CURRENT_SOURCE_DIR}/compile-js-to-cpp.cmake"
    COMMENT "Compiling module-main.js through the EmitC backend as a MODULE"
    VERBATIM)

  set_source_files_properties("${_mod_cpp}" PROPERTIES
    COMPILE_OPTIONS "${CTCOMPILE_GENERATED_WARNINGS}")
  add_executable(ctcompile-test-module_differential
                 ModuleDifferential.cpp "${_mod_cpp}" ${_mod_incs})
  target_include_directories(ctcompile-test-module_differential
                             PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(ctcompile-test-module_differential PRIVATE ctbrowser::script)
  ctcompile_target(ctcompile-test-module_differential)
  add_test(NAME ctcompile_module_differential
           COMMAND ctcompile-test-module_differential)
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
    # AND TELL AddLLVM WHICH ONE, or a FRESH TREE CANNOT CONFIGURE AT ALL.
    #
    # add_lit_testsuite goes through get_llvm_lit_path, which defaults
    # LLVM_EXTERNAL_LIT to <llvm-prefix>/bin/llvm-lit - and brew's LLVM bottle
    # ships no llvm-lit, so that path does not exist and configure FAILS. The
    # find_program above had already located a working lit two lines earlier;
    # the two were simply never introduced.
    #
    # IT ONLY STARTED MATTERING WHEN ctcompile JOINED THE GATE. This block runs
    # under CTCOMPILE_ENABLE_MLIR, which used to default OFF - so the failure
    # was invisible to anyone whose tree had been configured by hand once and
    # kept a warm cache. Making tools/remote-build.sh default it ON turned that
    # into "the documented build command does not work on a fresh checkout".
    #
    # FORCE, because get_llvm_lit_path reads the CACHE entry, and LLVM's own
    # AddLLVM.cmake has already created it with the wrong default by the time
    # find_package(MLIR) returns.
    set(LLVM_EXTERNAL_LIT "${CTCOMPILE_LIT}" CACHE FILEPATH
      "The lit ctcompile found - see ctcompile/test/CMakeLists.txt" FORCE)
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

# ---------------------------------------------------------------------------
# PHASE 58 - GENERATORS AS C++ COROUTINES. Appended as one block at the end of
# this file, per Appendix A.3 of `23-lexical-implementation.md`.
#
# OUTSIDE THE MLIR GUARD ON PURPOSE. It compiles no IR and runs no pass: it
# runs the ENGINE's generators against hand-written `ctnative::generator<T>`
# coroutines, which is the only way to ask whether Phase 58's mapping is sound
# before there is a lowering to ask it of. That also means it keeps running on
# a machine with no LLVM, which is where the `<generator>` fact it prints
# matters most.
add_executable(ctcompile-test-native-generator NativeGenerator.cpp)
target_link_libraries(ctcompile-test-native-generator PRIVATE ctbrowser::ctbrowser)
ctcompile_target(ctcompile-test-native-generator)
add_test(NAME ctcompile_native_generator COMMAND ctcompile-test-native-generator)
# ---------------------------------------------------------------------------
# --- PHASE 61: the JavaScript-global to C++/Boost map -------------------------
#
# `ctcompile-plan/24-native-cpp-backend.md` Phase 61 - the specification's §8
# table. The rows live in TableGen (include/ctcompile/StdLib/StdLibMap.td) and
# this turns them into the X-macro header the test walks, so the map has ONE
# spelling: when Phase 53's `ctnative` dialect lands, its declarative rewrite
# rules are `Pat<>`s beside those same records rather than a second table.
#
# NO MLIR IS REQUIRED, deliberately. StdLibMap.td includes nothing - not
# OpBase.td, not a dialect - so `llvm-tblgen --dump-json` reads it on a box with
# no MLIR in the build at all, and the conformance test runs in the
# CTCOMPILE_ENABLE_MLIR=OFF configuration where most of this file does not.
find_program(CTCOMPILE_LLVM_TBLGEN llvm-tblgen
  HINTS "${LLVM_TOOLS_BINARY_DIR}" ${CTBROWSER_BREW_HINTS}
  PATH_SUFFIXES bin opt/llvm/bin)
find_program(CTCOMPILE_PYTHON NAMES python3 python)

# BOOST.JSON AND BOOST.REGEX, WHICH THE ENGINE DOES NOT ALREADY ASK FOR.
# ctbrowser/cmake/dependencies.cmake requests COMPONENTS url and ctcompile's own
# CMakeLists requests program_options; neither of those pulls in json or regex,
# and both are COMPILED libraries rather than header-only. They are needed HERE
# because the divergences this test pins are divergences OF Boost - a refusal
# asserted without running the thing being refused is a refusal nobody checked.
find_package(Boost CONFIG QUIET COMPONENTS json regex)

if(CTCOMPILE_LLVM_TBLGEN AND CTCOMPILE_PYTHON AND TARGET Boost::json AND TARGET Boost::regex)
  set(_stdlib_td "${CMAKE_CURRENT_SOURCE_DIR}/../include/ctcompile/StdLib/StdLibMap.td")
  set(_stdlib_inc "${CMAKE_CURRENT_BINARY_DIR}/StdLibMap.inc")
  add_custom_command(
    OUTPUT "${_stdlib_inc}"
    COMMAND "${CTCOMPILE_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../utils/stdlib-map-emit.py"
            --tblgen "${CTCOMPILE_LLVM_TBLGEN}"
            --input "${_stdlib_td}"
            --output "${_stdlib_inc}"
    DEPENDS "${_stdlib_td}" "${CMAKE_CURRENT_SOURCE_DIR}/../utils/stdlib-map-emit.py"
    COMMENT "Emitting the standard-library map from StdLibMap.td"
    VERBATIM)

  add_executable(ctcompile-test-stdlib_map StdLibMap.cpp "${_stdlib_inc}")
  target_include_directories(ctcompile-test-stdlib_map PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(ctcompile-test-stdlib_map
    PRIVATE ctbrowser::script Boost::json Boost::regex)
  ctcompile_target(ctcompile-test-stdlib_map)
  add_test(NAME ctcompile_stdlib_map COMMAND ctcompile-test-stdlib_map)
else()
  # SAID OUT LOUD RATHER THAN SKIPPED SILENTLY. A suite that quietly drops a
  # test on a box missing a dependency is the "green build that measured
  # nothing" this project has already been bitten by twice.
  message(WARNING
    "ctcompile: the Phase 61 standard-library map test is NOT registered. "
    "llvm-tblgen=${CTCOMPILE_LLVM_TBLGEN} python=${CTCOMPILE_PYTHON} "
    "Boost_json_FOUND=${Boost_json_FOUND} Boost_regex_FOUND=${Boost_regex_FOUND} - "
    "install the missing one (brew install llvm boost) and configure again.")
endif()

# --- PHASE 53: the ctnative type lattice --------------------------------------
#
# ONE BLOCK, APPENDED, which is what Appendix A.3 of the plan's part 23 asks of
# every agent working in this file: it names test/CMakeLists.txt as the most
# collided-on file in the tree and asks for a concatenation rather than an edit.
# Everything this needs is its own file.
#
# WHY IT IS AN EXECUTABLE AND NOT A LIT TEST. test/CTNative/*.mlir already
# covers what lit is for - that the ODS parses and prints what it claims. What
# this asks cannot be written as text: whether `meet` is a lattice join. The
# associativity sweep alone is 9261 triples, and the divergence pin has to
# COMPILE AND RUN JavaScript in the real interpreter to find out what
# `"\u{1F600}".length` answers, because writing the number down here is exactly
# the stale-constant failure GCRoots.cpp records at the other end of the tree.
#
# IT LINKS BOTH SIDES, and it is the only test that does: CTNativeDialect for
# the lattice, ctbrowser::script for the interpreter that judges it. That is the
# point - part 24 §A.2, "Every phase's gate is a comparison against the
# interpreter."
if(CTCOMPILE_ENABLE_MLIR)
  add_executable(ctcompile-test-ctnative-lattice CTNativeLattice.cpp)
  # MLIRAsmParser IS NOT OPTIONAL AND ITS ABSENCE LOOKS LIKE NOTHING. The meet
  # table spells its types the way a .mlir file does and calls mlir::parseType,
  # which lives there and not in MLIRIR: without it every line COMPILES and the
  # link fails on a mangled name that says nothing about parsing.
  target_link_libraries(ctcompile-test-ctnative-lattice
    PRIVATE CTNativeDialect MLIRIR MLIRAsmParser ctbrowser::script)
  ctcompile_target(ctcompile-test-ctnative-lattice)
  add_test(NAME ctcompile_ctnative_lattice COMMAND ctcompile-test-ctnative-lattice)
endif()

# ============================================================================
# PHASE 54B - THE TYPE ORACLE. Appended as ONE block, per part 23's Appendix
# A.3: this file is contended and every agent that touches it appends at the
# end rather than editing in the middle.
#
# The native backend compiles a function to statically typed C++ only where it
# can PROVE the function is in the static subset, and every later phase consumes
# that proof. The interpreter is what settles whether the proof is right: it
# knows every value's real type at run time, so `--record-types` writes down
# what it saw and the checker compares a static claim against it.
#
# TWO EXECUTABLES OF THE CHECK, DELIBERATELY. TypeOracle.cpp walks the recorder
# in memory and tools/check/type-oracle.py parses the file it writes; the cmake
# script below runs both and makes one's counters the other's expectations. The
# file format is what sits between them, so a bug in the writer or the parser
# shows up in exactly one side - which is the only way to catch it.
#
# It needs no MLIR: a recording is the INTERPRETER's opinion, and asking the
# compiler anything here would beg the question.
add_executable(ctcompile-test-type-oracle TypeOracle.cpp)
target_link_libraries(ctcompile-test-type-oracle PRIVATE ctbrowser::ctbrowser)
ctcompile_target(ctcompile-test-type-oracle)

# THE SELF-TEST ALONE, always registered. Its numbers are hand-computed - see
# the fixture's comment in TypeOracle.cpp - so it fails with a name rather than
# with a diff.
add_test(NAME ctcompile_type_oracle COMMAND ctcompile-test-type-oracle)

# AND THE TWO-IMPLEMENTATION COMPARISON, which needs an interpreter for the
# checker. Optional the way plutosvg and SDL are: without Python the C++ half
# still runs and says so, rather than the suite silently shrinking.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME ctcompile_type_oracle_checker
           COMMAND ${CMAKE_COMMAND}
                   -DEXE=$<TARGET_FILE:ctcompile-test-type-oracle>
                   -DPYTHON=${Python3_EXECUTABLE}
                   -DSCRIPT=${CTBROWSER_MONOREPO_ROOT}/tools/check/type-oracle.py
                   -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/check-type-oracle.cmake)
else()
  message(STATUS "ctcompile: no Python3 - the type oracle's checker is not registered")
endif()

# ============================================================================
# PHASE 54A - THE TYPE INFERENCE'S TRANSFER FUNCTION. Appended as ONE block,
# per part 23's Appendix A.3.
#
# The oracle next door answers "is the inference sound over a corpus". This
# answers "which rule is wrong", which the oracle cannot: it is a table of
# JavaScript facts, one small function each. Half its rows are NEGATIVE - an
# operator whose operands might be BigInts must refuse to claim a number - and
# those are the rows that fail if anyone makes the analysis unconditional.
#
# Behind the MLIR guard, like every other target that names a dialect.
if(CTCOMPILE_ENABLE_MLIR)
  add_executable(ctcompile-test-type-inference TypeInference.cpp)
  target_link_libraries(ctcompile-test-type-inference
    PRIVATE CTNativeAnalysis CTNativeDialect CTJSDialect MLIRIR MLIRAnalysis MLIRParser
            MLIRControlFlowDialect MLIRSCFDialect MLIRUBDialect)
  ctcompile_target(ctcompile-test-type-inference)
  add_test(NAME ctcompile_type_inference COMMAND ctcompile-test-type-inference)
endif()

# ============================================================================
# PHASE 54A CHECKED BY PHASE 54B - THE ORACLE, CLOSED. Appended as ONE block.
#
# ctcompile-test-type-claims is the compiler's half: it imports a corpus, runs
# the inference and writes one claim per register in the checker's format. It
# is a SEPARATE executable from ctcompile-test-type-oracle on purpose - that
# one refuses to link MLIR so a recording cannot share a bug with the claim it
# is checking. check-type-claims.cmake runs both over one corpus and the Python
# checker over their two outputs, and the gate is zero soundness violations.
#
# FOUR CORPORA. The fixture is small, runs in milliseconds, and calls every
# guarded operator with BigInts so an unsound claim is OBSERVED rather than
# merely possible. Bootstrap is the real thing, prepended to the driver that
# Phase 54B wrote for it - and its precision is stated against the registers
# it actually reaches, which is an eighth of the bundle.
if(CTCOMPILE_ENABLE_MLIR AND Python3_Interpreter_FOUND)
  add_executable(ctcompile-test-type-claims TypeClaims.cpp)
  target_link_libraries(ctcompile-test-type-claims
    PRIVATE CTNativeAnalysis CTNativeDialect CTJSDialect ctcompile::ctjs-import
            MLIRIR MLIRAnalysis ctbrowser::ctbrowser)
  ctcompile_target(ctcompile-test-type-claims)

  # FOUR CORPORA, which is the plan's "all three" plus the fixture. p5 and
  # phaser need no driver: under --script each bundle's top level runs until it
  # reaches a DOM API this mode does not provide - `performance.now` for p5,
  # `document.createElement` for phaser - and the recorder has by then seen
  # 1,911 and 613 registers respectively. Bootstrap's UMD wrapper defines
  # `bootstrap` and returns, so it gets the driver Phase 54B wrote for it.
  foreach(_corpus fixture bootstrap p5 phaser)
    set(_prefix "")
    if(_corpus STREQUAL "fixture")
      set(_js "${CMAKE_CURRENT_SOURCE_DIR}/type-claims-fixture.js")
    elseif(_corpus STREQUAL "bootstrap")
      set(_js "${CMAKE_CURRENT_SOURCE_DIR}/type-oracle-bootstrap.js")
      set(_prefix "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/bootstrap/bootstrap.bundle.js")
    else()
      set(_js "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/${_corpus}/${_corpus}.js")
    endif()
    add_test(NAME ctcompile_type_claims_${_corpus}
             COMMAND ${CMAKE_COMMAND}
                     -DORACLE=$<TARGET_FILE:ctcompile-test-type-oracle>
                     -DCLAIMS=$<TARGET_FILE:ctcompile-test-type-claims>
                     -DPYTHON=${Python3_EXECUTABLE}
                     -DSCRIPT=${CTBROWSER_MONOREPO_ROOT}/tools/check/type-oracle.py
                     -DCORPUS=${_js}
                     -DPREFIX=${_prefix}
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DNAME=${_corpus}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/check-type-claims.cmake)
  endforeach()
endif()

# === PHASE 55C: escape cycle ===
# ============================================================================
# PHASE 55C - THE CYCLE FIXTURE, gate 4 of 25-escape-analysis.md §5. Appended
# as ONE block, per part 23's Appendix A.3.
#
# One executable pins, against the running interpreter, what a cycle MEANS:
# the VM's tracing collector keeps a ring alive while any member is reachable
# and frees it whole when none is (gates 4(a) and 4(b)); beside it, in C++, the
# two ways "replace back-references with weak links" fails - a shared_ptr ring
# that destroys nothing, a compiler-chosen weak_ptr that destroys a node
# JavaScript can still see - and the frame-owned region that is the one
# RAII-sound answer for a CONFINED cycle (gate 4(c)); the ND-2 and ND-3 probes
# of docs/native-divergences.md (gate 4(e)); and, when ctjs-translate is built,
# the fixture's printed ctjs module read as text for `ctnative.weak` (4(f)).
#
# IT NEEDS NO MLIR, like ctcompile-test-type-oracle: every question here is put
# to the interpreter, and the module check is a string search over a file the
# build printed. Gate 4(d) - the recorder over this file - belongs to the
# escape-oracle work and its check-escape-claims.cmake; 4(g) is deferred by the
# design itself until 55B emits anything.
set(_cyc_js "${CMAKE_CURRENT_SOURCE_DIR}/escape-cycle.js")
# THE DRIVER READS THE SAME FILE EVERYTHING ELSE DOES - the recorder, the
# claims, the printed module - for gc-roots.js's reason: two copies of one
# fixture are two programs.
set(_cyc_inc "${CMAKE_CURRENT_BINARY_DIR}/escape-cycle.js.inc")
add_custom_command(
  OUTPUT "${_cyc_inc}"
  COMMAND "${CMAKE_COMMAND}" -DSOURCE=${_cyc_js} -DOUTPUT=${_cyc_inc}
          -P "${CMAKE_CURRENT_SOURCE_DIR}/embed-js.cmake"
  DEPENDS "${_cyc_js}" "${CMAKE_CURRENT_SOURCE_DIR}/embed-js.cmake"
  COMMENT "Embedding escape-cycle.js for the driver"
  VERBATIM)
set(_cyc_sources EscapeCycle.cpp "${_cyc_inc}")
set(_cyc_has_module OFF)
if(TARGET ctjs-translate)
  # THE PRINTED MODULE, embedded the same way. ctjs-translate is an
  # MlirTranslateMain tool, so `-o` is its own; the .mlir is then wrapped in
  # the raw string embed-js.cmake writes - the content is MLIR text and cannot
  # contain the delimiter. When there is no ctjs-translate the executable says
  # SKIPPED for (f) rather than the suite silently shrinking.
  set(_cyc_mlir "${CMAKE_CURRENT_BINARY_DIR}/escape-cycle.ctjs.mlir")
  set(_cyc_mlir_inc "${CMAKE_CURRENT_BINARY_DIR}/escape-cycle.ctjs.inc")
  add_custom_command(
    OUTPUT "${_cyc_mlir}"
    COMMAND $<TARGET_FILE:ctjs-translate> --ctbrowser-js-to-ctjs "${_cyc_js}" -o "${_cyc_mlir}"
    DEPENDS "${_cyc_js}" ctjs-translate
    COMMENT "Importing escape-cycle.js to ctjs for the ctnative.weak check"
    VERBATIM)
  add_custom_command(
    OUTPUT "${_cyc_mlir_inc}"
    COMMAND "${CMAKE_COMMAND}" -DSOURCE=${_cyc_mlir} -DOUTPUT=${_cyc_mlir_inc}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/embed-js.cmake"
    DEPENDS "${_cyc_mlir}" "${CMAKE_CURRENT_SOURCE_DIR}/embed-js.cmake"
    COMMENT "Embedding the ctjs module of escape-cycle.js for the driver"
    VERBATIM)
  list(APPEND _cyc_sources "${_cyc_mlir_inc}")
  set(_cyc_has_module ON)
endif()
add_executable(ctcompile-test-escape-cycle ${_cyc_sources})
target_include_directories(ctcompile-test-escape-cycle PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(ctcompile-test-escape-cycle PRIVATE ctbrowser::ctbrowser)
if(_cyc_has_module)
  target_compile_definitions(ctcompile-test-escape-cycle PRIVATE CTCOMPILE_ESCAPE_CYCLE_MODULE=1)
endif()
ctcompile_target(ctcompile-test-escape-cycle)
add_test(NAME ctcompile_escape_cycle COMMAND ctcompile-test-escape-cycle)
# === PHASE 55A: escape analysis ===
# ============================================================================
# THE SINKS-AND-CARRIERS TABLE, ONE CELL AT A TIME. Appended as ONE block.
#
# The table lives in CTJSOps.td as Arg<..., [CTJS_Sink...]> decorators; this
# is the row-per-cell test of it: one positive row per NEITHER/CARRY cell (the
# site stays confined), one negative row per SINK cell (that operand alone
# flips the site, so a regression names the operand), the kind switch both
# ways, the loop and handler rows, the dead/unvisited accounting, R1's guard,
# R4, the closure hole, and the DEFAULT RULE. Every operand role is also read
# back through the generated interface, so a row can only be wrong by name.
#
# Behind the MLIR guard, like every other target that names a dialect.
if(CTCOMPILE_ENABLE_MLIR)
  add_executable(ctcompile-test-escape-analysis EscapeAnalysis.cpp)
  target_link_libraries(ctcompile-test-escape-analysis
    PRIVATE CTNativeAnalysis CTNativeDialect CTJSDialect MLIRIR MLIRAnalysis MLIRParser
            MLIRControlFlowDialect)
  ctcompile_target(ctcompile-test-escape-analysis)
  add_test(NAME ctcompile_escape_analysis COMMAND ctcompile-test-escape-analysis)
endif()

# === PHASE 55A-claims: the escape claims emitter ===
#
# The compiler's half of the escape oracle: imports a corpus, runs
# EscapeAnalysis, and writes one claim per allocation site in the format
# tools/check/escape-oracle.py reads (25-escape-analysis.md §3.4). The
# executable alone here; the four corpus tests that run it beside the
# interpreter's recording are registered with the recorder (55O), which they
# need and which lands separately.
if(CTCOMPILE_ENABLE_MLIR)
  add_executable(ctcompile-test-escape-claims EscapeClaims.cpp)
  target_link_libraries(ctcompile-test-escape-claims
    PRIVATE CTNativeAnalysis CTNativeDialect CTJSDialect ctcompile::ctjs-import
            MLIRIR MLIRAnalysis ctbrowser::ctbrowser)
  ctcompile_target(ctcompile-test-escape-claims)
endif()
# === PHASE 55O: escape oracle ===
# ============================================================================
# PHASE 55O - THE ESCAPE ORACLE. Appended as ONE block, per part 23's Appendix
# A.3, and never edited in the middle.
#
# The native backend gives an allocation an RAII lifetime - a stack object, a
# unique_ptr, a shared_ptr - only where an escape analysis can PROVE the object
# never outlives the activation that made it; anything it cannot prove is
# outside the native subset and is diagnosed. The interpreter is the reference
# semantics that settles whether a proof is right: its precise collector knows,
# at the moment a frame ends, exactly what is still reachable from which root,
# so the recorder borrows that root walk (bounded at the ending frame's base,
# mark only, never sweep) and writes down per site what happened to every
# object born there. The checker compares a static claim against it.
#
# THE SAME TWO-IMPLEMENTATION SHAPE AS 54B: the escape self-test lives in the
# same executable as the type one (one recorder, one file, header version 2),
# walks the recorder in memory, and asserts a HAND-COMPUTED table; the Python
# checker parses the file; check-escape-oracle.cmake makes one's numbers the
# other's expectations. No MLIR anywhere near it.
add_test(NAME ctcompile_escape_oracle COMMAND ctcompile-test-type-oracle --escape)

if(Python3_Interpreter_FOUND)
  add_test(NAME ctcompile_escape_oracle_checker
           COMMAND ${CMAKE_COMMAND}
                   -DEXE=$<TARGET_FILE:ctcompile-test-type-oracle>
                   -DPYTHON=${Python3_EXECUTABLE}
                   -DSCRIPT=${CTBROWSER_MONOREPO_ROOT}/tools/check/escape-oracle.py
                   -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/check-escape-oracle.cmake)
else()
  message(STATUS "ctcompile: no Python3 - the escape oracle's checker is not registered")
endif()

# EVERY PLACE A FRAME ENDS, AS A TABLE. FrameEnds.def lists the eight sites
# that truncate `frames_` and says which two are hooked; the escape self-test
# static_asserts its shape, and this checks that the file:line half of each
# citation still lands inside the file it names - the one class of rot a
# machine can see, per check-def-citations.cmake's own header.
add_test(NAME ctcompile_frame_ends_citations
         COMMAND ${CMAKE_COMMAND}
                 -DDEF=${CMAKE_CURRENT_SOURCE_DIR}/../include/ctcompile/JavaScript/FrameEnds.def
                 -DROOT=${CTBROWSER_MONOREPO_ROOT}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check-def-citations.cmake)

# === PHASE 55 CLOSED: the escape claims over four corpora ===
#
# Phase 55A checked by 55O. check-escape-claims.cmake runs the interpreter's
# recording and the compiler's claims over one corpus and the Python checker
# over both; zero soundness violations is the gate. Budgets follow
# 25-escape-analysis.md §5: unlimited on the fixture and bootstrap, bounded on
# the two big bundles, where every frame pop otherwise costs a full mark.
if(CTCOMPILE_ENABLE_MLIR AND Python3_Interpreter_FOUND)
  foreach(_corpus fixture bootstrap p5 phaser)
    set(_prefix "")
    set(_budget "")
    set(_strict OFF)
    if(_corpus STREQUAL "fixture")
      set(_js "${CMAKE_CURRENT_SOURCE_DIR}/escape-claims-fixture.js")
      set(_strict ON)
    elseif(_corpus STREQUAL "bootstrap")
      set(_js "${CMAKE_CURRENT_SOURCE_DIR}/type-oracle-bootstrap.js")
      set(_prefix "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/bootstrap/bootstrap.bundle.js")
    else()
      set(_js "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/${_corpus}/${_corpus}.js")
      if(_corpus STREQUAL "p5")
        set(_budget 8)
      else()
        set(_budget 4)
      endif()
    endif()
    add_test(NAME ctcompile_escape_claims_${_corpus}
             COMMAND ${CMAKE_COMMAND}
                     -DORACLE=$<TARGET_FILE:ctcompile-test-type-oracle>
                     -DCLAIMS=$<TARGET_FILE:ctcompile-test-escape-claims>
                     -DPYTHON=${Python3_EXECUTABLE}
                     -DSCRIPT=${CTBROWSER_MONOREPO_ROOT}/tools/check/escape-oracle.py
                     -DCORPUS=${_js}
                     -DPREFIX=${_prefix}
                     -DBUDGET=${_budget}
                     -DSTRICT=${_strict}
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DNAME=${_corpus}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/check-escape-claims.cmake)
  endforeach()
endif()

# === PHASE 62½-D: the native gate ===
#
# THE TEST THAT DEFINES "NATIVE" - part 24 §1.2 and Phase 62½-D. One block,
# appended, per part 23's Appendix A.3. Everything it needs is its own file:
# native-fixture.js (the program), native-fixture.emitc.mlir (the hand-written
# EmitC module the Phase 62½-C lowering is expected to produce for it - the
# specification of that lowering's output shape), NativeReference.cpp (the
# interpreter's answers, printed the same way), NativeVmLinked.cpp (the
# negative control for the symbol check) and check-native-unit.cmake (the
# gate: emit, compile standalone, nm, run, compare).
#
# THE REFERENCE NEEDS NO MLIR. It is the interpreter's opinion of a JavaScript
# file, built wherever the type oracle is: the answer side of the gate should
# not depend on the compiler side existing, and a lowering author wants it on a
# box with no LLVM too.
add_executable(ctcompile-test-native-reference NativeReference.cpp)
target_link_libraries(ctcompile-test-native-reference PRIVATE ctbrowser::ctbrowser)
ctcompile_target(ctcompile-test-native-reference)

if(CTCOMPILE_ENABLE_MLIR AND TARGET ctjs-translate)
  # nm, the one CMake found beside the compiler at configure time (llvm-nm for
  # the pinned clang). A missing one is said out loud rather than skipped: this
  # gate IS the definition of native, and a suite without it is green on nothing.
  set(_native_nm "${CMAKE_NM}")
  if(NOT _native_nm)
    find_program(_native_nm NAMES llvm-nm nm)
  endif()
  if(NOT _native_nm)
    # FATAL, not WARNING: `ctcompile_add_native_unit` guards ~20 tests, and a
    # skip here unregisters all of them while ctest still reports green - a
    # suite green on nothing, which is what the comment above forbids. The
    # Phase 63 Step 7 block below treats a missing compiler the same way.
    message(FATAL_ERROR "ctcompile: no nm found - the Phase 62½-D native gate cannot be registered, and it IS the definition of native")
  else()
    # ONE FUNCTION, so a second fixture is one line: the module, the program,
    # and any of the driver's negative-proof switches after them.
    function(ctcompile_add_native_unit name module js)
      add_test(NAME ctcompile_native_unit_${name}
               COMMAND ${CMAKE_COMMAND}
                       -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
                       -DMODULE=${module}
                       -DJS=${js}
                       -DCXX=${CMAKE_CXX_COMPILER}
                       -DNM=${_native_nm}
                       -DREFERENCE=$<TARGET_FILE:ctcompile-test-native-reference>
                       -DVM_LINKED=$<TARGET_FILE:ctcompile-test-type-oracle>
                       -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                       -DNAME=${name}
                       ${ARGN}
                       -P ${CMAKE_CURRENT_SOURCE_DIR}/check-native-unit.cmake)
    endfunction()

    set(_native_module "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.emitc.mlir")
    set(_native_js "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.js")

    # THE GATE.
    ctcompile_add_native_unit(fixture "${_native_module}" "${_native_js}")

    # NEGATIVE PROOF 1: one global off by one must FAIL, naming the global. The
    # driver runs itself and passes only if the child failed with that name in
    # its output - see EXPECT_FAILURE in the driver for why not WILL_FAIL.
    ctcompile_add_native_unit(fixture_off_by_one "${_native_module}" "${_native_js}"
      -DMUTATE=fib20 "-DEXPECT_FAILURE=global 'fib20' differs")

    # NEGATIVE PROOF 2: the same C++, plus one object that reaches the
    # interpreter, linked against the engine - must FAIL the symbol check. The
    # C++ is emitted at build time by the same emitter the gate uses, so this
    # binary is the fixture's program in every respect but the one that matters.
    set(_native_cpp "${CMAKE_CURRENT_BINARY_DIR}/native-fixture.generated.cpp")
    add_custom_command(
      OUTPUT "${_native_cpp}"
      COMMAND $<TARGET_FILE:ctjs-translate> --mlir-to-cpp "${_native_module}" -o "${_native_cpp}"
      DEPENDS "${_native_module}" ctjs-translate
      COMMENT "Emitting native-fixture.emitc.mlir to C++ for the VM-linked control"
      VERBATIM)
    add_executable(ctcompile-test-native-vm-linked NativeVmLinked.cpp "${_native_cpp}")
    target_link_libraries(ctcompile-test-native-vm-linked PRIVATE ctbrowser::ctbrowser)
    target_compile_features(ctcompile-test-native-vm-linked PRIVATE cxx_std_23)
    # THE GATE'S OWN FLAGS, not ctcompile_target's: this is the same text under
    # the same compile the gate performs, plus one object and one library.
    target_compile_options(ctcompile-test-native-vm-linked PRIVATE
      -O2 -Wall -Wextra -Werror -pedantic -ffp-contract=off)
    ctcompile_add_native_unit(fixture_vm_linked "${_native_module}" "${_native_js}"
      -DPREBUILT=$<TARGET_FILE:ctcompile-test-native-vm-linked>
      "-DEXPECT_FAILURE=reaches the interpreter")
  endif()
endif()

# === PHASE 62½-C+D: JavaScript through the native pipeline and the gate ===
#
# The programs that go all the way. native-pipeline.cmake runs the closed
# world, the lift and the lowering over a JavaScript file and refuses to write
# a module while any function carries a diagnostic; the gate then compiles it
# with no ctbrowser library, runs it, and compares every numeric global with
# the interpreter. One function, one line per program.
if(CTCOMPILE_ENABLE_MLIR AND TARGET ctjs-translate AND TARGET ctjs-opt
   AND COMMAND ctcompile_add_native_unit)
  # PHASE 63 STEP 7: the generated file compiles clean on BOTH toolchains the
  # plan names, and every definition carries a provenance comment. Two
  # compilers are required, not "whatever is found": a gate that silently
  # drops a compiler passes vacuously (check-compile-clean.cmake).
  find_program(CTCOMPILE_CLEAN_GXX NAMES g++-13 g++)
  find_program(CTCOMPILE_CLEAN_CLANGXX NAMES clang++-18 clang++)
  if(NOT CTCOMPILE_CLEAN_GXX OR NOT CTCOMPILE_CLEAN_CLANGXX)
    message(FATAL_ERROR "Phase 63 Step 7 needs both g++ and clang++ on PATH; found g++='${CTCOMPILE_CLEAN_GXX}' clang++='${CTCOMPILE_CLEAN_CLANGXX}'")
  endif()
  function(ctcompile_add_compile_clean name module)
    add_test(NAME ctcompile_compile_clean_${name}
             COMMAND ${CMAKE_COMMAND}
                     -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
                     -DMODULE=${module}
                     "-DCOMPILERS=${CTCOMPILE_CLEAN_GXX},${CTCOMPILE_CLEAN_CLANGXX}"
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DNAME=${name}
                     ${ARGN}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/check-compile-clean.cmake)
  endfunction()
  # the hand-written fixture module, and the negative proof
  ctcompile_add_compile_clean(fixture "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.emitc.mlir")
  ctcompile_add_compile_clean(fixture_unused_variable "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.emitc.mlir"
    -DMUTATE=1 "-DEXPECT_FAILURE=refused the generated file")
  function(ctcompile_add_native_pipeline name js)
    set(_module "${CMAKE_CURRENT_BINARY_DIR}/${name}.pipeline.emitc.mlir")
    add_custom_command(
      OUTPUT "${_module}"
      COMMAND ${CMAKE_COMMAND}
              -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
              -DOPT=$<TARGET_FILE:ctjs-opt>
              -DSOURCE=${js}
              -DOUTPUT=${_module}
              -P "${CMAKE_CURRENT_SOURCE_DIR}/native-pipeline.cmake"
      DEPENDS "${js}" "${CMAKE_CURRENT_SOURCE_DIR}/native-pipeline.cmake" ctjs-translate ctjs-opt
      COMMENT "Lowering ${name} through the native pipeline")
    add_custom_target(ctcompile-native-pipeline-${name} ALL DEPENDS "${_module}")
    ctcompile_add_native_unit(pipeline_${name} "${_module}" "${js}")
    # AND THE NEGATIVE PROOF, when the caller names a global to break.
    #
    # Every gate here asserts that a GENERATED module agrees with the
    # interpreter; none of them asserted that the comparison can still fail.
    # It could not be registered before 2026-09-03: check-native-unit.cmake
    # anchored its off-by-one on the literal `std::printf(`, which only the
    # hand-written native-fixture.emitc.mlir emits, so -DMUTATE aborted at
    # generate time on every pipeline program. That abort is a FATAL_ERROR
    # rather than a red test, which is why the absence read as a gate nobody
    # had got round to rather than as a driver that could not do it.
    #
    # The global must be one the printing prelude loads AFTER the first
    # printf; the driver checks that itself and says so, because a mutation
    # inserted after the load is invisible and would pass for the wrong
    # reason.
    if(ARGN)
      list(GET ARGN 0 _mutate_global)
      ctcompile_add_native_unit(pipeline_${name}_off_by_one "${_module}" "${js}"
        -DMUTATE=${_mutate_global}
        "-DEXPECT_FAILURE=global '${_mutate_global}' differs")
    endif()
    # PHASE 62½-E: the same module with the printing policy applied goes
    # through the same gate (62½-D stays green), and the printing gate proves
    # the rest: auto and pins pair up, the two files differ only in spelling,
    # the byte counts are reported, and the pin mutation fails the build
    # naming the JavaScript site (check-print-deduced.cmake).
    set(_deduced "${CMAKE_CURRENT_BINARY_DIR}/${name}.pipeline.deduced.emitc.mlir")
    set(_mutated "${CMAKE_CURRENT_BINARY_DIR}/${name}.pipeline.mutated.emitc.mlir")
    add_custom_command(
      OUTPUT "${_deduced}"
      COMMAND $<TARGET_FILE:ctjs-opt> --ctnative-print-deduced "${_module}" --mlir-print-debuginfo -o "${_deduced}"
      DEPENDS "${_module}" ctjs-opt
      COMMENT "Applying the printing policy to ${name}")
    add_custom_command(
      OUTPUT "${_mutated}"
      COMMAND $<TARGET_FILE:ctjs-opt> "--ctnative-print-deduced=mutate=1" "${_module}" --mlir-print-debuginfo -o "${_mutated}"
      DEPENDS "${_module}" ctjs-opt
      COMMENT "Mutating one pin in ${name}")
    add_custom_target(ctcompile-native-deduced-${name} ALL DEPENDS "${_deduced}" "${_mutated}")
    ctcompile_add_native_unit(pipeline_${name}_deduced "${_deduced}" "${js}")
    ctcompile_add_compile_clean(pipeline_${name} "${_module}")
    ctcompile_add_compile_clean(pipeline_${name}_deduced "${_deduced}")
    add_test(NAME ctcompile_print_deduced_${name}
             COMMAND ${CMAKE_COMMAND}
                     -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
                     -DPLAIN=${_module}
                     -DDEDUCED=${_deduced}
                     -DMUTATED=${_mutated}
                     -DCXX=${CMAKE_CXX_COMPILER}
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DNAME=${name}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/check-print-deduced.cmake)
  endfunction()
  # numbers and booleans, no functions
  ctcompile_add_native_pipeline(numeric "${CMAKE_CURRENT_SOURCE_DIR}/native-pipeline-fixture.js" total)
  # the gate's own eight functions: recursion, loops, a boolean predicate
  ctcompile_add_native_pipeline(functions "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.js" sum100)
  # Phase 56: object literals with closed shapes, on the stack
  ctcompile_add_native_pipeline(structs "${CMAKE_CURRENT_SOURCE_DIR}/native-struct-fixture.js" swap_answer)
  # Phase 57A: dense, uniformly numeric array literals, as std::vector<double>
  ctcompile_add_native_pipeline(arrays "${CMAKE_CURRENT_SOURCE_DIR}/native-array-fixture.js" sum)
endif()

# === PART 24 §3.3: the deduction probe ===
#
# What the two devbox compilers accept, measured every build rather than
# tabulated once: constrained auto parameters, templated lambdas, aggregate
# CTAD, a visited variant, static_assert pins over deduced declarations, and
# the absence of two C++23 library features. Phases 56 to 62½ plan against
# this file. One appended block, per Appendix A.3.
add_test(NAME ctcompile_deduction_probe
         COMMAND ${CMAKE_COMMAND}
                 -DPROBE=${CMAKE_CURRENT_SOURCE_DIR}/../probes/deduction.cpp
                 "-DCOMPILERS=/usr/bin/g++;/usr/bin/clang++;${CMAKE_CXX_COMPILER}"
                 -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check-deduction-probe.cmake)

# === PHASE 63 STEPS 2 AND 3: the claimed set, per corpus ===
# ============================================================================
# Part 24 Phase 63 Step 2 asks for per-function selection WITH A REASON and
# says of the refusals "that list is the roadmap"; Step 3 asks how many
# functions the backend claims. tools/check/native-claims.py runs the native
# pipeline over one program and sorts every function into claimed, refused
# (with the reason it carries) or never imported, then asserts two things the
# other gates cannot see:
#
#   * every function not claimed carries a `ctnative.not_native` reason. One
#     left behind WITHOUT a diagnostic is the silent drop part 24 SS2 forbids,
#     and no fixture gate would notice, because fixtures are programs the
#     compiler already handles.
#   * the run is not vacuous - a corpus that failed to parse would otherwise
#     report "0 refused".
#
# --min-claimed is a FLOOR AGAINST SILENT NARROWING, set at the number
# measured on 2026-09-02 (073ea1f). A change that refuses functions it used to
# claim fails here while every fixture stays green. Raising a floor belongs in
# the same commit that earns it.
#
# The corpora are the four Phase 54B and 55O already use. The two large
# bundles take about eight seconds and 1.5 GB each; that is the price of
# measuring the roadmap on real code rather than on the fixture.
#
# AND THE CEILING, WHICH THE THREE BUCKETS COULD NOT SEE. Two closed-world
# rules landed sound and far too coarse and took every global on every real
# corpus with them - bootstrap 0 of 37, p5 0 of 101, phaser 0 of 72, and not
# one ctjs.call_direct anywhere. This check did not move, because the
# functions that stopped being reachable were the ones that take a parameter
# and those were never claimed. It now reads --ctjs-resolve-globals' own
# counters, and prints the per-name refusal reasons as the roadmap for them.
if(CTCOMPILE_ENABLE_MLIR AND TARGET ctjs-translate AND TARGET ctjs-opt AND Python3_EXECUTABLE)
  # `resolved` and `direct` are floors under the closed world. They are 0 on
  # the three real corpora, and THE REASON WRITTEN HERE WAS WRONG. It said all
  # three are open programs - "bootstrap's UMD header passes `globalThis`/
  # `self` into the factory it calls, p5 constructs `Function`, phaser calls
  # `eval` with a computed string" - and named clause 5 as the ceiling. Clause
  # 5 is not the ceiling on any of them. Disabling it OUTRIGHT (returning
  # nullopt from dynamic_global_writes, 2026-09-03) leaves all three at 0
  # resolved and 0 ctjs.call_direct, with the rows reading:
  #
  #   bootstrap  37 of 37  never stored in this program - a host binding
  #   p5         96 of 101 the same, 4 bound outside the hoisting prologue,
  #                        1 bound to something other than a closure
  #   phaser     72 of 72  never stored in this program - a host binding
  #
  # A BUNDLE DECLARES NOTHING AT GLOBAL SCOPE. Each of these hands a factory's
  # result to one property of the window, so there is no global NAME for the
  # census to bind and `resolved` is 0 by clause 1 whatever clause 5 says. The
  # escape reasons these rows carried were decorations on a refusal clauses 1-3
  # make anyway. That does not relax clause 5 - the window proxy's `set` trap
  # really does define_global (Shell/bindings/window.cpp:880-892) - it says
  # clause 5 is not where a vendor bundle is lost.
  #
  # THAT OTHER HALF OF known_callee() IS NOW TAKEN, and `direct` is no longer 0
  # on any of them: --ctjs-resolve-globals rewrites a ctjs.call whose callee is
  # a ctjs.create_closure result, which needs no global name and therefore no
  # clause of the per-name census. Measured 2026-09-03: bootstrap 0 -> 21, p5
  # 0 -> 41, phaser 0 -> 48, of 21 / 42 / 50 such sites (the shortfall is the
  # arity and new.target guards, which are load-bearing).
  #
  # AND `claimed` DID NOT MOVE - 4, 37 and 43 before and after. Naming a callee
  # is upstream of admission, not admission: the functions those calls reach are
  # refused for their own reasons, which the census below prints. Said here
  # because a floor that rises while the number it exists for does not is
  # exactly the kind of green gate this file has been burned by.
  #
  # THE FOURTH FLOOR IS `lifted`, AND IT EXISTS BECAUSE THIS CHECK WAS BLIND.
  # --ctnative-lower-to-emitc's closure lift ALSO makes ctjs.call_direct - 3 on
  # bootstrap, 47 on p5, 3 on phaser - and always did. Counting only the
  # resolver's stage is what let "not one ctjs.call_direct emitted anywhere" be
  # written above about a pipeline that was emitting them.
  #
  # A zero floor still bites the day one of them earns a number: raising it is
  # then a visible act in the commit that earns it.
  #
  # PHASE 59 SLICE 1b MOVED NONE OF THESE EITHER, measured 2026-09-03 before
  # and after in one build directory: claimed 4 / 37 / 43, resolved 0 / 0 / 0,
  # direct 21 / 41 / 48, lifted 3 / 47 / 3 on bootstrap / p5 / phaser -
  # identical to the number. The slice carries a capture filled from the
  # enclosing closure only once the enclosing function has lifted, and the
  # refusal now spells the chain out to whatever stopped that. Walked closure
  # by closure over the callees a ctjs.call_direct reaches: bootstrap's 19 are
  # 3 lifted and 16 whose chain ends in a cell written EXACTLY ONCE from a
  # constant undefined - the hoisted `var`/`const` shape closure-refusals.mlir
  # pins as HOISTED; phaser's 40 are 2 lifted, 1 arrow reading `this`, and 37
  # of the same shape; p5's 26 are 12 lifted, 7 of that shape and 7 other.
  # Module-wide, 250 of bootstrap's 271 closures refused for a reassigned
  # capture have that shape. So these floors stay where they are, on purpose,
  # and the next lever is a binding's single dominating write (part 24 Phase
  # 59 step 3), not another level of nesting.
  function(ctcompile_add_native_claims name js floor resolved direct lifted)
    add_test(NAME ctcompile_native_claims_${name}
             COMMAND ${Python3_EXECUTABLE}
                     ${CTBROWSER_MONOREPO_ROOT}/tools/check/native-claims.py
                     --translate $<TARGET_FILE:ctjs-translate>
                     --opt $<TARGET_FILE:ctjs-opt>
                     --corpus ${js}
                     --name ${name}
                     --json ${CMAKE_CURRENT_BINARY_DIR}/native-claims-${name}.json
                     --min-claimed ${floor}
                     --min-resolved ${resolved}
                     --min-direct ${direct}
                     --min-lifted-direct ${lifted})
  endfunction()
  # THE FIXTURE IS THE VACUITY CHECK: 9 of 9, so a run that reports nothing
  # claimed anywhere is not mistaken for a clean sheet.
  ctcompile_add_native_claims(fixture "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.js" 9 8 16 0)
  # TWO CLOSED PROGRAMS, ADDED BECAUSE THE FLOORS ABOVE ARE NOT ENOUGH.
  # differential.js and launcher.js are already compiled through the EmitC
  # backend by the blocks further up this file, so they cost nothing to fetch
  # and they are the only corpora here whose globals actually resolve: 56 of 72
  # and 9 of 10. A closed-world clause widened by one term takes those to zero
  # and this fails, which is precisely what did NOT happen when two of them
  # were - the three vendor bundles were already at zero and had nothing left
  # to lose.
  ctcompile_add_native_claims(differential "${CMAKE_CURRENT_SOURCE_DIR}/differential.js" 2 56 10 0)
  ctcompile_add_native_claims(launcher "${CMAKE_CURRENT_SOURCE_DIR}/launcher.js" 0 9 9 0)
  ctcompile_add_native_claims(bootstrap
    "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/bootstrap/bootstrap.bundle.js" 4 0 21 3)
  ctcompile_add_native_claims(p5 "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/p5/p5.js" 37 0 41 47)
  ctcompile_add_native_claims(phaser
    "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/phaser/phaser.js" 43 0 48 3)

  # BOTH TEETH, PROVED. Each runs the same check as a child and passes only
  # when that child failed for the stated reason - not ctest's WILL_FAIL,
  # which a crash or a missing corpus would satisfy just as well.
  function(ctcompile_add_native_claims_negative name js)
    add_test(NAME ctcompile_native_claims_${name}
             COMMAND ${Python3_EXECUTABLE}
                     ${CTBROWSER_MONOREPO_ROOT}/tools/check/native-claims.py
                     --translate $<TARGET_FILE:ctjs-translate>
                     --opt $<TARGET_FILE:ctjs-opt>
                     --corpus ${js}
                     --name ${name}
                     ${ARGN})
  endfunction()
  # 1: the floor bites. The fixture claims 9 of 9; a floor of 10 must fail.
  ctcompile_add_native_claims_negative(floor_bites
    "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.js"
    --min-claimed 10 "--expect-failure=the native tier NARROWED")
  # 2: a function left behind with no diagnostic is caught. One refusal reason
  # is deleted from the lowered module, and the check must name the count.
  ctcompile_add_native_claims_negative(silent_drop_caught
    "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/bootstrap/bootstrap.bundle.js"
    --mutate-drop-one-reason "--expect-failure=were dropped silently")
  # 3: the CLOSED-WORLD floor bites too. The fixture resolves 8 globals; a
  # floor of 9 must fail, and with the message that names the closed world
  # rather than the claimed set - the two are different numbers and a check
  # that confused them would have missed the regression this floor exists for.
  ctcompile_add_native_claims_negative(resolved_floor_bites
    "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.js"
    --min-claimed 9 --min-resolved 9 --min-direct 16
    "--expect-failure=the CLOSED WORLD NARROWED")
  # 4: and the pass cannot report a rewrite it did not make. Every
  # ctjs.call_direct is renamed before counting, so the remark says 16 and the
  # IR holds none.
  ctcompile_add_native_claims_negative(call_direct_counter_agrees
    "${CMAKE_CURRENT_SOURCE_DIR}/native-fixture.js"
    --mutate-drop-call-direct
    "--expect-failure=the counter and the rewrite disagree")
endif()

# ============================================================================
# THE PDLL GUARD. Appended as ONE block, per part 23's Appendix A.3.
#
# A .pdll in this tree may now name a `ctjs` operation, because
# lib/CTNative/Lowering/CMakeLists.txt puts our include roots on mlir-pdll's
# search path. That is only safe with utils/pdll-strict.sh in front of the
# tool, because mlir-pdll EXITS 0 on two things that silently change what a
# pattern matches - an attribute literal of an unregistered dialect (which it
# drops, after saying so on stderr) and an operation name ODS never heard of
# (which it does not mention at all).
#
# The guard is installed as MLIR_PDLL_TABLEGEN_EXE, so every pattern in the
# tree already goes through it and a passing build is some evidence. It is not
# enough: the guard keys off text mlir-pdll prints, so a release that reworded
# the diagnostic would turn it off silently. This test runs it over three
# fixtures under PDLL/ that are never compiled into anything, and asserts the
# RAW tool's behaviour beside the guard's - so the day upstream closes a hole,
# the gate says which one rather than passing on.
#
# It costs six mlir-pdll runs, about 70 ms.
if(CTCOMPILE_ENABLE_MLIR)
  # MLIR_PDLL_TABLEGEN_EXE is the two-element list ctcompile/CMakeLists.txt
  # made of it: the guard, then the real tool (a generator expression when MLIR
  # was found as an installed package, which add_test expands).
  list(LENGTH MLIR_PDLL_TABLEGEN_EXE _pdll_parts)
  if(NOT _pdll_parts EQUAL 2)
    message(FATAL_ERROR "ctcompile: MLIR_PDLL_TABLEGEN_EXE is '${MLIR_PDLL_TABLEGEN_EXE}', not "
                        "the guard-and-tool pair ctcompile/CMakeLists.txt sets. The PDLL guard "
                        "is not installed and no pattern in this tree is checked.")
  endif()
  list(GET MLIR_PDLL_TABLEGEN_EXE 0 _pdll_guard)
  list(GET MLIR_PDLL_TABLEGEN_EXE 1 _pdll_real)
  # THE SAME TWO ROOTS THE PATTERNS THEMSELVES GET. Comma-separated because a
  # -D value carrying semicolons is one list argument CMake would split at the
  # wrong level; check-pdll-guard.cmake splits it back.
  string(REPLACE ";" "," _pdll_includes "${PROJECT_SOURCE_DIR}/include;${MLIR_INCLUDE_DIRS}")
  add_test(NAME ctcompile_pdll_guard
           COMMAND ${CMAKE_COMMAND}
                   -DGUARD=${_pdll_guard}
                   -DPDLL=${_pdll_real}
                   -DDIR=${CMAKE_CURRENT_SOURCE_DIR}/PDLL
                   "-DINCLUDES=${_pdll_includes}"
                   -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/check-pdll-guard.cmake)
endif()

# === THE DIFFERENTIAL GATE COMPARES VALUES THAT ARE NOT NUMBERS ===
#
# One appended block, per part 23's Appendix A.3. Everything it needs is its
# own file: native-values-fixture.js (a program with a Number, a Boolean, a
# String, `null` and `undefined` in its globals) and
# native-values-fixture.emitc.mlir (the hand-written EmitC module that is the
# SPECIFICATION of what the lowering has to emit for it - including the
# `ctnative::print_*` helpers, which LowerToEmitC.cpp does not emit yet).
#
# WHY A HAND-WRITTEN MODULE and not the pipeline: the same reason
# native-fixture.emitc.mlir is hand-written. The gate is what defines the tier,
# so it is proved on something BEFORE there is a lowering to gate - otherwise
# the first program with a Boolean global would arrive with the gate and the
# lowering both unproven, and neither could be blamed. When the lowering learns
# these forms, its output for native-values-fixture.js should be this module up
# to value names, and nothing here has to change.
if(COMMAND ctcompile_add_native_unit AND COMMAND ctcompile_add_compile_clean)
  set(_values_module "${CMAKE_CURRENT_SOURCE_DIR}/native-values-fixture.emitc.mlir")
  set(_values_js "${CMAKE_CURRENT_SOURCE_DIR}/native-values-fixture.js")

  # THE GATE, over all five kinds at once.
  ctcompile_add_native_unit(values "${_values_module}" "${_values_js}")

  # ONE NEGATIVE PROOF PER KIND, because a mutation that suits a Number suits
  # nothing else: `<g> = <g> + 1` does not compile on a bool, has no meaning on
  # a std::string, and has nothing to act on for a global whose proven type is
  # `undefined` or `null` - those have no storage. Each of these must FAIL, and
  # must fail NAMING ITS GLOBAL; the driver's EXPECT_FAILURE is what asserts
  # the second half. See check-native-unit.cmake's MUTATE_AS block.
  #
  # `third` AND NOT `answer` FOR THE NUMBER, and that is a fact about the
  # mechanism rather than a preference: the number mutation is an insertion in
  # front of the first print, so it is invisible to a global the printing has
  # already read - which the FIRST global in sorted order always has been. The
  # driver now refuses that case by name, and the next test is the proof.
  ctcompile_add_native_unit(values_number "${_values_module}" "${_values_js}"
    -DMUTATE=third -DMUTATE_AS=number "-DEXPECT_FAILURE=global 'third' differs")
  ctcompile_add_native_unit(values_boolean "${_values_module}" "${_values_js}"
    -DMUTATE=no -DMUTATE_AS=boolean "-DEXPECT_FAILURE=global 'no' differs")
  ctcompile_add_native_unit(values_string "${_values_module}" "${_values_js}"
    -DMUTATE=greeting -DMUTATE_AS=string "-DEXPECT_FAILURE=global 'greeting' differs")
  ctcompile_add_native_unit(values_undefined "${_values_module}" "${_values_js}"
    -DMUTATE=missing -DMUTATE_AS=undefined "-DEXPECT_FAILURE=global 'missing' differs")
  ctcompile_add_native_unit(values_null "${_values_module}" "${_values_js}"
    -DMUTATE=nothing -DMUTATE_AS=null "-DEXPECT_FAILURE=global 'nothing' differs")

  # AND THE GUARD ON THE MUTATION ITSELF. Mutating the first global in sorted
  # order used to PASS - the off-by-one landed after the printing had read the
  # value, so the binary printed the right answer and the negative test could
  # say only "the gate PASSED where it had to fail". This asserts the driver
  # now refuses that mutation and says why, so the next author does not spend
  # the afternoon the last one did.
  ctcompile_add_native_unit(values_vacuous_mutation "${_values_module}" "${_values_js}"
    -DMUTATE=answer -DMUTATE_AS=number
    "-DEXPECT_FAILURE=VACUOUS")

  # THE SYMBOL CHECK, on a binary built from THIS module. The nm check knows
  # nothing about values, so one control per module is all it can prove and
  # five copies of it would be five copies of one proof; this is the module's
  # own C++ plus one object that reaches the interpreter, and the gate must
  # reject it.
  set(_values_cpp "${CMAKE_CURRENT_BINARY_DIR}/native-values-fixture.generated.cpp")
  add_custom_command(
    OUTPUT "${_values_cpp}"
    COMMAND $<TARGET_FILE:ctjs-translate> --mlir-to-cpp "${_values_module}" -o "${_values_cpp}"
    DEPENDS "${_values_module}" ctjs-translate
    COMMENT "Emitting native-values-fixture.emitc.mlir to C++ for the VM-linked control"
    VERBATIM)
  add_executable(ctcompile-test-native-values-vm-linked NativeVmLinked.cpp "${_values_cpp}")
  target_link_libraries(ctcompile-test-native-values-vm-linked PRIVATE ctbrowser::ctbrowser)
  target_compile_features(ctcompile-test-native-values-vm-linked PRIVATE cxx_std_23)
  target_compile_options(ctcompile-test-native-values-vm-linked PRIVATE
    -O2 -Wall -Wextra -Werror -pedantic -ffp-contract=off)
  ctcompile_add_native_unit(values_vm_linked "${_values_module}" "${_values_js}"
    -DPREBUILT=$<TARGET_FILE:ctcompile-test-native-values-vm-linked>
    "-DEXPECT_FAILURE=reaches the interpreter")

  # PHASE 63 STEP 7 over the same module: the value-printing helpers the
  # lowering has to emit must compile clean on BOTH toolchains, not just on the
  # one the gate happens to use, and every definition must carry its
  # provenance comment.
  ctcompile_add_compile_clean(values "${_values_module}")
endif()

# ============================================================================
# THE REFACTOR GOLDENS' OWN GATE. Appended as ONE block, per part 23's
# Appendix A.3.
#
# docs/refactor-goldens.md is the procedure: snapshot the eight post-pipeline
# modules and the four census JSONs before a change, `cmp` them after it, and
# for a PURE REFACTOR not one byte may move. It is deliberately NOT a
# checked-in golden - the artefacts embed the absolute paths the build passed
# to ctjs-translate, and a golden of derived output has no regeneration rule
# that cannot be abused - so nothing in ctest can run the comparison itself.
#
# WHAT ctest CAN DO is prove the INSTRUMENT works, which is the half that
# would otherwise rot silently:
#
#   1. the pipeline is byte-deterministic - re-running it over an unchanged
#      tree reproduces numeric.pipeline.emitc.mlir exactly. Without that, a
#      before/after comparison reports differences that mean nothing and the
#      first person to meet one learns to ignore the tool.
#   2. the comparison bites - one byte appended to a saved copy is caught.
#
# A comparison nobody has watched fail is not a comparison, which is the same
# reason native-claims.py carries floor_bites and silent_drop_caught.
#
# bash EXPLICITLY, not the shebang: core.filemode is false in this checkout
# (a DrvFs working tree), so git does not carry the execute bit and a
# COMMAND that relied on it would fail with "Permission denied" on a fresh
# clone. CMAKE is passed through the environment because the selftest re-runs
# native-pipeline.cmake and `cmake` need not be on PATH.
if(CTCOMPILE_ENABLE_MLIR AND TARGET ctjs-translate AND TARGET ctjs-opt AND UNIX)
  find_program(CTCOMPILE_BASH NAMES bash)
  if(NOT CTCOMPILE_BASH)
    message(FATAL_ERROR "ctcompile: bash not found - native-snapshot.sh cannot be gated, "
                        "so the refactor goldens would have no proof they still work")
  endif()
  add_test(NAME ctcompile_native_snapshot_selftest
           COMMAND ${CTCOMPILE_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/native-snapshot.sh
                   selftest ${CMAKE_BINARY_DIR})
  set_tests_properties(ctcompile_native_snapshot_selftest PROPERTIES
                       ENVIRONMENT "CMAKE=${CMAKE_COMMAND}")
endif()

# === PHASE 63 STEP 5: THE DECLARED DIVERGENCES, AS A PROGRAM ===
#
# ctcompile/docs/native-divergences.md is one row per place the native tier's
# answer differs, or must not differ, from the interpreter's. Half of those
# rows are refusals and are pinned by their text
# (CTNative/Lowering/divergence-refusals.mlir); the other half are EMITTED -
# `**` and its guard, `%` as fmod, -0 through the printing convention,
# undefined carried as NaN where that is exact, and an out-of-range index as
# NaN rather than as undefined behaviour. An emitted divergence is a CLAIM
# THAT THE TWO SIDES AGREE, and part 24 §A.2 says a claim like that is not
# done until a binary and an interpreter have both answered. So the witnesses
# go through the same Phase 62½-D gate every other native program does, and
# through Step 7's two-toolchain compile with them.
#
# One appended block, per part 23 Appendix A.3. `ctcompile_add_native_pipeline`
# is defined inside the block above; a CMake function outlives the `if()` it
# was defined in, so this reads it rather than editing that block.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(divergence
                                "${CMAKE_CURRENT_SOURCE_DIR}/native-divergence-fixture.js")
  # THE NEGATIVE PROOF, AND IT IS A REAL DEFECT RATHER THAN AN INJECTED ONE.
  #
  # The fixture above claims every emitted guard agrees with the interpreter.
  # That claim is worth nothing until the gate is shown catching a guard that
  # does NOT, on a module this pipeline generated. `vec_at` answers NaN for a
  # fractional index where this interpreter truncates - `[10,20,30][0.5]` is
  # 10 - so this program is registered to FAIL, naming the global. When
  # LowerToEmitC.cpp is fixed the gate starts passing, THIS TEST GOES RED, and
  # whoever fixed it is made to come back and update ND-8. See
  # native-index-truncation-fixture.js and ctcompile/docs/native-divergences.md.
  #
  # WHY THIS WITNESS AND NOT JUST THE DRIVER'S -DMUTATE. Until 2026-09-03 the
  # answer was that -DMUTATE could not run here at all: it anchored the
  # off-by-one on the literal `std::printf(`, which only the HAND-WRITTEN
  # native-fixture.emitc.mlir emits, so it aborted with "cannot mutate" on
  # every generated program. That is fixed - the anchor takes the bare
  # `printf(` a generated module writes, and the four base pipeline programs
  # now carry a `_off_by_one` proof each.
  #
  # This witness stays, because it proves something -DMUTATE cannot: -DMUTATE
  # breaks the C++ ON PURPOSE and asks whether the comparison notices. This
  # asks whether the comparison notices a divergence the compiler produces on
  # its own, from unmodified sources, which is the failure the gate actually
  # exists to catch.
  set(_defect_js "${CMAKE_CURRENT_SOURCE_DIR}/native-index-truncation-fixture.js")
  set(_defect_module "${CMAKE_CURRENT_BINARY_DIR}/index-truncation.pipeline.emitc.mlir")
  add_custom_command(
    OUTPUT "${_defect_module}"
    COMMAND ${CMAKE_COMMAND}
            -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
            -DOPT=$<TARGET_FILE:ctjs-opt>
            -DSOURCE=${_defect_js}
            -DOUTPUT=${_defect_module}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/native-pipeline.cmake"
    DEPENDS "${_defect_js}" "${CMAKE_CURRENT_SOURCE_DIR}/native-pipeline.cmake"
            ctjs-translate ctjs-opt
    COMMENT "Lowering the index-truncation defect witness through the native pipeline")
  add_custom_target(ctcompile-native-pipeline-index-truncation ALL DEPENDS "${_defect_module}")
  ctcompile_add_native_unit(pipeline_index_truncation "${_defect_module}" "${_defect_js}"
                            "-DEXPECT_FAILURE=global 'idx_fractional' differs")
  # IT STILL HAS TO COMPILE CLEAN. A wrong answer is not a licence to emit a
  # file Phase 63 Step 7 would reject, and this is the one program in the tree
  # whose C++ is known to be wrong - so it is the one most worth building.
  if(COMMAND ctcompile_add_compile_clean)
    ctcompile_add_compile_clean(pipeline_index_truncation "${_defect_module}")
  endif()
endif()

# === PART 24 PHASE 59 SLICE 1: closures, carried by lifting ===
# ============================================================================
# native-closure-fixture.js is nine functions that each make a closure, put it
# in a local and call it through that local - the shape the tier refused whole
# until the lift: the only closure it carried was a function DECLARATION,
# whose create_closure/store_global pair lowers to nothing. It goes through the
# same Phase 62½-D gate as every other native program: the module is written
# only if no function carries a diagnostic, compiled with no ctbrowser library,
# run, and every global compared with the interpreter's own answer.
#
# THE GATE IS THE POINT, NOT THE IR. A lift that passes the wrong capture, or
# passes the right ones in the wrong order, produces a module that verifies,
# compiles clean under -Werror and prints a wrong number; only the interpreter
# can say so. The parameter ORDER in particular is pinned twice - once as a
# signature in CTNative/Lowering/closure-lift.mlir, once as an answer here.
#
# THE REFUSALS ARE NOT HERE, and cannot be: native-pipeline.cmake refuses to
# write a module while any function carries `ctnative.not_native`, and a
# refusal is contagious in both directions anyway. They are one program apiece
# in CTNative/Lowering/closure-refusals.mlir, under split-file.
#
# One appended block, per part 23 Appendix A.3.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(closures "${CMAKE_CURRENT_SOURCE_DIR}/native-closure-fixture.js")
endif()
# AND THE CLAIMED COUNT, AS A FLOOR. Twenty functions and all of them claimed:
# the top level, nine that make a closure, and ten closures. A change that
# stops lifting one of them fails HERE even if the fixture above were deleted,
# which is what --min-claimed is for. `resolved` and `direct` are the closed
# world's own counters. THE LIFT'S OWN ctjs.call_direct ARE A FOURTH NUMBER,
# `lifted`, because they are made inside --ctnative-lower-to-emitc and the
# resolver's count cannot see them: reading one stage and reporting the total
# is what made three vendor bundles read as 0 for a rewrite that was happening.
# `direct` moved too, because --ctjs-resolve-globals now names a call whose
# callee is a create_closure result - which these fixtures are full of.
if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(closures
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-closure-fixture.js" 20 9 23 17)
endif()

# ============================================================================
# === PART 24: THE RECEIVER IS A PARAMETER - methods lift like closures ===
# ============================================================================
# native-receiver-fixture.js is eight functions whose objects are METHOD
# TABLES: closed-shape literals with numeric fields and function-expression
# fields that reach the data through `this`. That is the shape the tier refused
# whole - `uses `this`` is 6,649 of the 12,916 refusals it makes over the three
# corpora - and the blocker was never that the callee is unknown. It is that no
# C++ type carried a receiver.
#
# THE GATE IS THE POINT, NOT THE IR. A lift that passes the receiver by value
# instead of by pointer produces a module that verifies, compiles clean under
# -Werror and prints 3 where the interpreter prints 9, because the mutation
# never reaches the caller's object. Only the interpreter can say so, and
# `mutating()` calling its method TWICE is what makes the difference show.
#
# THE REFUSALS ARE NOT HERE, and cannot be: native-pipeline.cmake refuses to
# write a module while any function carries `ctnative.not_native`, and a
# refusal is contagious in both directions anyway. They are one program apiece
# in CTNative/Lowering/receiver-refusals.mlir, under split-file.
#
# One appended block, per part 23 Appendix A.3.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(receivers "${CMAKE_CURRENT_SOURCE_DIR}/native-receiver-fixture.js")
endif()
# AND THE CLAIMED COUNT, AS A FLOOR. Sixteen functions and all of them claimed:
# the top level, seven that build a method table, and eight methods. A change
# that stops lifting one of them fails HERE even if the fixture above were
# deleted, which is what --min-claimed is for. `resolved` and `direct` are the
# world's own counters. THE LIFT'S OWN ctjs.call_direct ARE A FOURTH NUMBER,
# `lifted`, because they are made inside --ctnative-lower-to-emitc and the
# resolver's count cannot see them: reading one stage and reporting the total
# is what made three vendor bundles read as 0 for a rewrite that was happening.
# `direct` moved too, because --ctjs-resolve-globals now names a call whose
# callee is a create_closure result - which these fixtures are full of.
if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(receivers
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-receiver-fixture.js" 16 7 8 10)
endif()

# ============================================================================
# === PART 24: AN ARGUMENT IS A PARAMETER TOO - the object carrier at any index
# ============================================================================
# native-object-argument-fixture.js is seven functions whose closed-shape
# literals are PASSED to a call rather than (or as well as) called on. The
# receiver lift made a literal reach a function as a `ctn_x *` and its fields as
# `self->x`; nothing in that carrier was about operand 0, so the same proof
# carries an argument - and `closedAfterLift` stops treating "it is passed to a
# call" as an escape when the callee is one function this rewrite makes direct
# and the parameter is only ever read through constant keys.
#
# WHY IT IS A `this` CHANGE. A method table that was ALSO passed anywhere had an
# open shape, and an open shape refused every method on it by name. `both()` is
# that program.
#
# MEASURED, AND THE NUMBER DID NOT MOVE ON THE CORPORA. All 51 method-bearing
# literals that bootstrap, p5 and phaser block on an argument use pass to an
# OPAQUE callee - `--ctnative-lower-to-emitc=census=1` prints the distribution -
# so the 249 refusals reading "a method field of an object whose shape is not
# closed" are 249 before and after. The fixture is what shows the carrier works;
# the census is what says why the bundles do not benefit.
#
# THE GATE IS THE POINT, NOT THE IR - AND THE PREDICTION HERE WAS WRONG, WHICH
# IS WORTH RECORDING. The expectation was that passing the object BY VALUE would
# verify, compile clean and print 8 where the interpreter prints 40, because the
# write in `mutated()` would never reach the caller. Measured by deleting the
# address-of at the call site: EmitC will not express it at all. `emitc.func`
# rejects an lvalue parameter type and `emitc.call` rejects an lvalue operand
# ("operand #0 must be variadic of type supported by EmitC, but got
# `!emitc.lvalue<!emitc.opaque<"ctn_x">>`"), so the by-value lift is a hard
# verifier error and not a wrong number. What the differential gate DOES catch
# here is the index: an object handed to the wrong parameter position, or a
# receiver and an argument swapped in `both()`, both verify and print wrongly.
#
# THE REFUSALS ARE NOT HERE, and cannot be: native-pipeline.cmake refuses to
# write a module while any function carries `ctnative.not_native`, and a refusal
# is contagious in both directions anyway. They are one program apiece in
# CTNative/Lowering/object-argument-refusals.mlir, under split-file, with the
# census remark asserted alongside them.
#
# One appended block, per part 23 Appendix A.3.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(object_arguments
                                "${CMAKE_CURRENT_SOURCE_DIR}/native-object-argument-fixture.js")
endif()
# AND THE CLAIMED COUNT, AS A FLOOR. Fifteen functions and all of them claimed:
# the top level, seven that build a literal, and seven callees - `both()` making
# two. A change that stops lifting one of them fails HERE even if the fixture
# above were deleted, which is what --min-claimed is for. `resolved` and
# world's own counters. THE LIFT'S OWN ctjs.call_direct ARE A FOURTH NUMBER,
# `lifted`, because they are made inside --ctnative-lower-to-emitc and the
# resolver's count cannot see them: reading one stage and reporting the total
# is what made three vendor bundles read as 0 for a rewrite that was happening.
# `direct` moved too, because --ctjs-resolve-globals now names a call whose
# callee is a create_closure result - which these fixtures are full of.
if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(object_arguments
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-object-argument-fixture.js"
                              16 7 15 9)
endif()

# ============================================================================
# === PART 24: `new` BUILDS A STRUCT IN THE FRAME - the slice before Phase 60
# ============================================================================
# native-constructor-fixture.js is six functions whose objects are built by
# `new` rather than written as literals. The rewrite is two operations that
# already lower - an empty ctjs.create_object for the instance, and the
# ctjs.call_direct the RECEIVER lift already emits for the constructor - so
# after it there is no constructor in the IR at all and the instance is an
# ordinary closed literal. `hasClosedShape`, `groupReceivers`, `fieldsOf`,
# `censusShapes` and `replace` needed no constructor case at all.
#
# THE GATE IS THE POINT, NOT THE IR. `new` that evaluated to the wrong thing
# produces a module that verifies and compiles clean under -Werror and prints
# a different number: a constructor whose body returned an object REPLACES the
# instance in the VM (`produced.is_object_like() ? produced : self`,
# vm/call.cpp), so admitting one would print the instance where the
# interpreter prints the returned object. Only the interpreter can say so.
#
# WHAT IS NOT ADMITTED, AND IT IS THE COMMONEST SHAPE IN REAL CODE: a top-level
# `function Point() {}`. That is stored to a global and `new Point(...)` loads
# it back, so the construct's callee is a `ctjs.load_global` and not a closure.
# Measured: the census row reads "open: the binding is used by ctjs.construct".
# Naming it needs the closed world's per-name verdict, which is the next lever.
#
# THE REFUSALS ARE NOT HERE, and cannot be: native-pipeline.cmake refuses to
# write a module while any function carries `ctnative.not_native`, and a
# refusal is contagious in both directions anyway. They are one program apiece
# in CTNative/Lowering/constructor-refusals.mlir, under split-file.
#
# One appended block, per part 23 Appendix A.3.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(constructors
                                "${CMAKE_CURRENT_SOURCE_DIR}/native-constructor-fixture.js")
endif()
# AND THE CLAIMED COUNT, AS A FLOOR. A change that stops lifting one of them
# fails HERE even if the fixture above were deleted, which is what
# --min-claimed is for. `lifted` is the fourth floor: every one of these calls
# is made by the lift inside --ctnative-lower-to-emitc, where the resolver's
# `direct` count cannot see it.
if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(constructors
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-constructor-fixture.js"
                              14 6 7 8)
endif()

# ============================================================================
# === PART 24 PHASE 59 SLICE 1b: a capture filled from the enclosing closure ===
# ============================================================================
# native-nested-closure-fixture.js is eight functions whose innermost closure
# names a binding two or three frames out. The bytecode compiler boxes that
# binding once, in the frame that owns it; every closure between reaches it
# through its ENCLOSING closure's upvalue, not through a cell of its own frame
# - and that is the shape slice 1 refused whole ("capture N is not a cell of
# this frame"). It is also what a UMD bundle is made of: measured on bootstrap
# before this slice, 15 of the 19 callees a direct call reaches were refused
# for exactly it.
#
# The importer leaves an `undefined` PLACEHOLDER in such a capture operand -
# there is no binding of this frame to point at - and writes the descriptor's
# index on the closure's `enclosing_indices` attribute, parallel with the
# capture list and -1 wherever the operand is the real cell. In the module this
# fixture imports, `mid$25`'s closure for `deep` reads
# `captures %7 {enclosing_indices = array<i32: 0>}` with `%7` a
# `ctjs.constant #ctjs.undefined`. Once the enclosing function is lifted, its
# upvalue k IS its capture parameter - the entry-block argument 3 + k, holding
# a value an outer frame proved constant - and the nested closure lifts on that
# index in the next round of a fixpoint over the classify-then-lift loop. The
# call passes the argument straight on: it already holds the VALUE, never the
# box.
#
# THE INDEX IS ON AN ATTRIBUTE BECAUSE AN OPERAND CHARGED THE BOXED TIER FOR IT.
# Writing it as a live ctjs.load_upvalue of this frame's own closure said the
# same thing, and CTJSToEmitC parked every capture operand into
# ct_aot_make_closure's argument window - 219 runtime upvalue reads on bootstrap
# that the helper then ignored. tools/check/capture-census.py is that census.
#
# THE GATE IS THE POINT, NOT THE IR. Every capture is READ in the innermost
# body, so a lift that passed the wrong value, a stale one, or the cell where
# the value was meant produces a module that verifies, compiles clean under
# -Werror and prints a wrong number - which only the interpreter can say. The
# `_off_by_one` proof (trailing argument) asserts that the comparison can still
# fail, on a global the printing prelude loads after the first one it prints.
#
# THE REFUSED CHAIN IS NOT HERE, and cannot be: native-pipeline.cmake refuses
# to write a module while any function carries `ctnative.not_native`. The
# outer frame that reassigns its binding - refused, with the enclosing
# closure's own reason chained into the sentence - is one program apiece in
# CTNative/Lowering/closure-refusals.mlir, under split-file.
#
# One appended block, per part 23 Appendix A.3.
if(COMMAND ctcompile_add_native_pipeline)
  ctcompile_add_native_pipeline(nested_closures
                                "${CMAKE_CURRENT_SOURCE_DIR}/native-nested-closure-fixture.js"
                                twice727)
endif()
# AND THE CLAIMED COUNT, AS A FLOOR: the four numbers are the MEASURED ones on
# the day this landed, so a change that stops lifting one level fails HERE
# even if the fixture above were deleted. `lifted` is the lift's own
# ctjs.call_direct count, which the resolver's `direct` cannot see.
if(COMMAND ctcompile_add_native_claims)
  ctcompile_add_native_claims(nested_closures
                              "${CMAKE_CURRENT_SOURCE_DIR}/native-nested-closure-fixture.js"
                              27 8 33 27)
endif()

# AND THE TWO FIGURES THE COMMENTS CITE, AS A GATE.
#
# CTJSOps.td and BytecodeImport.cpp both state that 219 of bootstrap's 1,021
# capture operands are the placeholder that carries an index. Those had a
# command but no gate, which in this tree is the same failure as a measurement
# with neither: the comment is specification, and nothing was checking it had
# stayed true. `--expect-indexed` is a floor AND a ceiling, so this fails in
# both directions - a change that stops writing the attribute and one that
# starts writing it where a real binding sits are both caught.
#
# IT MEASURES THE IMPORTER ONLY, one pass, no lowering: the module ctjs-translate
# writes for the bundle. That is deliberate - the figure is about what the
# BOXED tier is handed, and the boxed tier sees the importer's output.
if(TARGET ctjs-translate)
  add_test(NAME ctcompile_capture_census_bootstrap
           COMMAND ${CMAKE_COMMAND}
                   -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
                   -DJS=${CTBROWSER_MONOREPO_ROOT}/ctbrowser/vendor/bootstrap/bootstrap.bundle.js
                   -DCENSUS=${CTBROWSER_MONOREPO_ROOT}/tools/check/capture-census.py
                   -DINDEXED=219
                   -DOPERANDS=1021
                   -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/check-capture-census.cmake)
endif()
