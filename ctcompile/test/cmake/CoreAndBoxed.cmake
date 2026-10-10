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
#
# THE ROLE CLASSIFIER IS HEADER-ONLY and reads only ctbrowser's ABI table, so
# this test needs no MLIR - which is the point of the experiment it carries:
# whether the roles are derivable is answerable before a line of MLIR is written.
ctcompile_unit_test(inventories LIBS ctcompile::support)

# PHASE 13'S COVERAGE MEASUREMENT, derived from both sides rather than written
# down. The opcode list comes from the X-macro so it cannot disagree with the
# engine; what the importer handles is read out of the importer's own SOURCE, so
# it cannot disagree with the importer. Neither side is a transcription, which
# is what has drifted here before.
#
# IT NEEDS NO MLIR either - it reads a .cpp as text.
ctcompile_unit_test(importer-coverage LIBS ctcompile::support DEFS
  CTCOMPILE_IMPORTER_SOURCE="${PROJECT_SOURCE_DIR}/lib/CTJS/Import/BytecodeImport.cpp"
  CTCOMPILE_IMPORTER_DISPATCH="${PROJECT_SOURCE_DIR}/lib/CTJS/Import/Bytecode/Instructions.cpp"
  CTCOMPILE_IMPORTER_TABLES="${PROJECT_SOURCE_DIR}/lib/CTJS/Import/Bytecode/OperatorTables.h")

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
  PASS_REGULAR_EXPRESSION "USAGE: ctcompile \\[options\\] <application-directory>")

# An unknown option is a USAGE error, not a crash and not a silent success.
# LLVM's command-line parser reports it before any application work begins.
add_test(NAME ctcompile_rejects_nonsense COMMAND ctcompile-tool --not-an-option)
set_tests_properties(ctcompile_rejects_nonsense PROPERTIES WILL_FAIL TRUE)

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME ctcompile_cli
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/test/Packaging/cli.py"
      "$<TARGET_FILE:ctcompile-tool>" "$<TARGET_FILE:ctcompile-tool-ctbaseline>"
      "$<TARGET_FILE:ctcompile-tool-ctpageload>")
endif()

# DOES THE ABI TABLE POINT AT CODE THAT EXISTS? Core/def-citations.test, over
# Core/source-citations.py, which says what it can and cannot catch.

# THE COMPARATOR THAT ACCEPTS PHASE 16A, and the negative cases that stop it
# from accepting anything. A comparator too lenient does not fail to catch a
# bad blueprint - it certifies one.
ctcompile_unit_test(html_comparator LIBS ctcompile::html)

# THE COMPARATOR THAT ACCEPTS PHASE 16B. Every case below leaves
# selector_count() and rule_count() untouched and changes what the page looks
# like - which is exactly the class of difference the two counts cannot see and
# the reason engine::for_each_rule exists.
ctcompile_unit_test(css_comparator LIBS ctcompile::css)

# THE PROGRAM IMAGE: a round trip that must be exact, and a loader that must
# refuse. An image is executable input - the VM reads its pools with unchecked
# operator[] and one of those reads is a WRITE - so the negative cases here are
# the point of the file.
ctcompile_unit_test(program_image LIBS ctcompile::javascript)

# THE MANIFEST. Phase 1's gate asks for one; this checks the part of it that can
# be wrong, which is the string escaping - a resource name is whatever the
# document said, and a document may say `p5"; drop</script>`.
ctcompile_unit_test(manifest LIBS ctcompile::support)

# THE APPLICATION BUNDLE, on the same terms as the program image above and for
# a sharper reason: a bundle is a table of offsets read out of a file, and what
# it carries is program images, which are executable input. The last section of
# that file is the one that matters - it asserts the COUNTER that says the
# images were used, because a page that quietly recompiled produces an
# identical document.
#
# THE BINARY DIRECTORY IS AN ARGUMENT because two of its cases cannot be run
# here: they are bundles the LAUNCHER has to refuse, and the guard that refuses
# them lives behind a window. It writes them out and Packaging/roundtrip.test
# feeds them to the real ctrun. ctbrowser/test/support/options.hpp parses it.
ctcompile_unit_test(app_bundle LIBS ctbrowser::ctbrowser Boost::program_options
  ARGS ${CMAKE_CURRENT_BINARY_DIR})

# The whole thing, directory in and executable out: Packaging/roundtrip.test.

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
  set(_gc_js "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/GC/roots.js")
  # THE DRIVER READS THE SAME FILE THE PIPELINE COMPILES, for the reason
  # spelled out over differential.js below - and gc-roots.js had the defect in
  # a worse form. Its C++ copy was not a transcription that had drifted but a
  # DIFFERENT PROGRAM: the compiled bodies here, the drivers there. `held`
  # builds a closure and a compiled body bakes that closure's function index,
  # so the two agreed only because the extra functions happened to sit after
  # `keep`.
  ctcompile_embed_js(gc-roots.js.inc "${_gc_js}")
  # THE SYMBOL IS RENAMED so the driver's declaration does not depend on how
  # the importer numbers functions - `f` becomes `f$1` becomes `f_1`, and a
  # second function in the fixture would renumber it.
  ctcompile_compile_js(gc-roots.generated.cpp "${_gc_js}" ENTRIES f held built)
  ctcompile_boxed_test(gc_roots gc-roots.generated.cpp EMBED gc-roots.js.inc)

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
  set(_diff_js "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Differential/differential.js")
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
  ctcompile_embed_js(differential.js.inc "${_diff_js}")
  ctcompile_compile_js(differential.generated.cpp "${_diff_js}" ENTRIES
    "plus;ge;strict;loose;pick;globals;apply;step;counter;middle;methodish;fn;neg;bnot;put;greet;pack;kindOf;thrower;build;Point;newBad;coalesce;chain;dflt;total;chars;spread;hasIt;isA;drop;greetChain;Kid;spreadCall;spreadMethod;spreadNew;merge;mergeArray;firstLoop;accessors;guarded;plusOf;coerce;keysOf;dropNamed;bigLits;howMany;sumAll;restOf;bothOf;wrapped;passes;arrayOut")
  ctcompile_boxed_test(differential differential.generated.cpp EMBED differential.js.inc)

# --- and the same question for a MODULE ---------------------------------------
#
# ctcompile_differential compiles ONE CLASSIC SCRIPT and runs it with cx.run.
# op::load_import, op::bind_export and op::load_namespace are emitted by
# compile_program's module arm and by nowhere else, so a classic script cannot
# contain one and that harness cannot reach three of the four ES-module opcodes
# at all. This builds a three-module graph the way browser/scripts.cpp does and installs
# a compiled entry on the IMPORTING module's TOP LEVEL - the first time the
# backend is run over functions[0] rather than over a named function.
#
# ONLY module-main.js IS COMPILED. dep and user stay interpreted in every arm,
# which is what holds one variable still: dep is where the live binding is
# written, and user is the only thing that can see what bind_export decided.
  set(_mod_main "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Modules/main.js")
  set(_mod_dep "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Modules/dep.js")
  set(_mod_user "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Modules/user.js")
  # ALL THREE ARE EMBEDDED, for the reason differential.js is: the driver must
  # read the same bytes the pipeline compiled. A compiled body bakes the
  # function INDEX of every closure it builds, so a transcription that drifted
  # in ORDER makes it close over a different function while both tiers agree.
  set(_mod_incs "")
  foreach(_mod_js IN ITEMS "${_mod_main}" "${_mod_dep}" "${_mod_user}")
    get_filename_component(_mod_name "${_mod_js}" NAME)
    ctcompile_embed_js(module-${_mod_name}.inc "${_mod_js}")
    list(APPEND _mod_incs "module-${_mod_name}.inc")
  endforeach()

  # KIND module SELECTS --ctbrowser-module-to-ctjs. Without it the fixture is
  # compiled as a classic script, every `import` is a syntax error, and the
  # failure reads as a bad fixture rather than as a missing flag.
  #
  # `_script_` IS THE TOP LEVEL. compile/entry.cpp names functions[0]
  # "<script>", the importer sanitises that to `_script_$0` and the backend
  # spells it `_script__0`, which this script renames to ctc__script_.
  ctcompile_compile_js(module-main.generated.cpp "${_mod_main}" KIND module
                       ENTRIES _script_ raise2 loadTwo fn)
  ctcompile_boxed_test(module_differential module-main.generated.cpp EMBED ${_mod_incs})

# --- and does any of it LINK? -------------------------------------------------
#
# aot.hpp declares 69 helpers and lib/Script/aot_bridge/ defines 32. A call to one of
# the other 37 compiles perfectly and fails at link - which is exactly how
# ct_aot_global_get and ct_aot_negate were emitted for two commits with a green
# suite, because every EmitC lit test uses -fsyntax-only.
#
# This compiles a function exercising everything the backend accepts and LINKS
# it. The link is the whole assertion; the executable barely does anything.
  ctcompile_compile_js(linkable.generated.cpp
                       "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Linking/linkable.js" ENTRIES everything)
  ctcompile_boxed_test(linkable linkable.generated.cpp)
else()
  message(STATUS "ctcompile: gc-roots test unavailable (needs ctjs-opt and mlir-translate)")
endif()
