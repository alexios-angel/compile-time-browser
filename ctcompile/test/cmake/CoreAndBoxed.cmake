# ctcompile's core unit tests and the boxed-tier runtime tests (the lit tests
# are registered in Lit.cmake). The repository has no CI, so every invariant
# that would otherwise be a CI check is a test or a build error.
#
# The bytecode inventories (opcode table, call-path and GC-root lists), walked
# as data. bytecode.hpp already static_asserts the opcode count; this test
# checks what a static_assert cannot: that the columns agree with each other,
# that the tables can be read as tables rather than only expanded as macros,
# and that the call-path and GC-root lists hold the properties the compiler
# relies on. It also compiles EngineContract.hpp, which is only static_asserts
# about the engine's layout.
#
# The role classifier is header-only and reads only ctbrowser's ABI table, so
# this test needs no MLIR: it shows the operand roles can be derived without
# any MLIR at all.
ctcompile_unit_test(inventories LIBS ctcompile::support)

# Importer coverage: which bytecode opcodes the importer handles. Both sides
# are derived rather than written down: the opcode list comes from the X-macro,
# so it cannot disagree with the engine, and the handled set is read out of the
# importer's own source, so it cannot disagree with the importer. A
# hand-written list is what drifted before.
#
# It needs no MLIR either: it reads the importer's .cpp files as text.
ctcompile_unit_test(importer-coverage LIBS ctcompile::support DEFS
  CTCOMPILE_IMPORTER_SOURCE="${PROJECT_SOURCE_DIR}/lib/CTJS/Import/BytecodeImport.cpp"
  CTCOMPILE_IMPORTER_DISPATCH="${PROJECT_SOURCE_DIR}/lib/CTJS/Import/Bytecode/Instructions.cpp"
  CTCOMPILE_IMPORTER_TABLES="${PROJECT_SOURCE_DIR}/lib/CTJS/Import/Bytecode/OperatorTables.h")

# Run the tool, not just build it: an executable that links but dies on a
# missing symbol at startup would otherwise go unnoticed.
add_test(NAME ctcompile_version COMMAND ctcompile-tool --version)
set_tests_properties(ctcompile_version PROPERTIES
  PASS_REGULAR_EXPRESSION "ctcompile [0-9]+\\.[0-9]+\\.[0-9]+ \\(ctbrowser .* bytecode operations\\)")

# The usage path: `ctcompile` with no arguments must fail, since exiting 0 would
# mean the argument handling is missing.
add_test(NAME ctcompile_usage COMMAND ctcompile-tool)
set_tests_properties(ctcompile_usage PROPERTIES WILL_FAIL TRUE)

# `--help` succeeds and prints the usage line, not just an option list.
add_test(NAME ctcompile_help COMMAND ctcompile-tool --help)
set_tests_properties(ctcompile_help PROPERTIES
  PASS_REGULAR_EXPRESSION "USAGE: ctcompile \\[options\\] <application-directory>")

# An unknown option is a usage error, not a crash and not a silent success.
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

# Whether the ABI table cites code that exists is checked by
# Core/def-citations.test, over Core/source-citations.py, which documents what
# it can and cannot catch.

# The HTML document comparator, with negative cases that keep it from
# accepting anything: a comparator that is too lenient certifies a bad
# blueprint instead of catching it.
ctcompile_unit_test(html_comparator LIBS ctcompile::html)

# The CSS stylesheet comparator. Every case leaves selector_count() and
# rule_count() unchanged but changes how the page looks, which is exactly the
# difference those counts cannot see and why engine::for_each_rule exists.
ctcompile_unit_test(css_comparator LIBS ctcompile::css)

# The program image: a round trip that must be exact, and a loader that must
# refuse bad input. An image is executable input (the VM reads its pools with
# unchecked operator[], and one of those accesses is a write), so the negative
# cases matter most.
ctcompile_unit_test(program_image LIBS ctcompile::javascript)

# The build manifest: checks its string escaping, the part that can go wrong,
# because a resource name is whatever the document said, for example
# `p5"; drop</script>`.
ctcompile_unit_test(manifest LIBS ctcompile::support)

# The application bundle, tested like the program image: a bundle is a table
# of offsets read from a file, and it carries program images, which are
# executable input. The test's last section asserts the counter showing the
# images were used, because a page that silently recompiled produces an
# identical document.
#
# The binary directory is passed as an argument because two cases cannot run
# here: they are bundles the launcher must refuse, and that guard runs only
# with a window. The test writes them out and Packaging/roundtrip.test feeds
# them to the real ctrun. ctbrowser/tools/support/options.hpp parses it.
ctcompile_unit_test(app_bundle LIBS ctbrowser::ctbrowser ctbrowser-options
  ARGS ${CMAKE_CURRENT_BINARY_DIR})

# The whole packaging path, directory in and executable out, is
# Packaging/roundtrip.test.

# LLVM's mlir-translate is part of the backend, not a convenience: it is the
# stage that turns EmitC into C++.
find_program(MLIR_TRANSLATE_EXE mlir-translate HINTS "${LLVM_TOOLS_BINARY_DIR}")

# --- Running what the backend generated, under GC stress --------------------
#
# Everything else about the EmitC backend is checked by reading its output or by
# compiling it. Neither can see a use-after-free: the freed memory is still
# there and still holds the right bytes until something reuses it. This runs the
# generated code against the real runtime with set_gc_stress on, which is the
# only way to make the collection happen at the moment that matters.
#
# Example defect it catches: `a + b + c` keeping the first addition's result in
# a plain C++ local across the second call, although ct_aot_binary_op is a
# safepoint. Under stress the compiled body returned six characters where the
# interpreter returned sixty-five.
#
# The pipeline runs at build time rather than being checked in, because a
# generated file in the tree can disagree with the generator, and this test
# must exercise the current backend.
if(TARGET ctjs-translate AND TARGET ctjs-opt AND MLIR_TRANSLATE_EXE)
  set(_gc_js "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/GC/roots.js")
  # The driver embeds the same file the pipeline compiles (see differential.js
  # below for why).
  ctcompile_embed_js(gc-roots.js.inc "${_gc_js}")
  # ENTRIES renames each symbol so the driver's declaration does not depend on
  # how the importer numbers functions: `f` becomes `f$1` and then `f_1`, and
  # another function in the fixture would renumber it.
  ctcompile_compile_js(gc-roots.generated.cpp "${_gc_js}" ENTRIES f held built)
  ctcompile_boxed_test(gc_roots gc-roots.generated.cpp EMBED gc-roots.js.inc)

# --- Differential test: compiled code against the interpreter --------------
#
# The lit tests read the emitted C++ and compile it, ctcompile_linkable links
# it, and ctcompile_gc_roots runs one body with the collector hostile. None of
# them asks whether the answer is right, and a backend can emit fluent,
# linkable, correctly-rooted code that computes the wrong thing.
#
# This runs each body twice against the same context - interpreted, then with
# its compiled entry installed - and compares. Nothing writes an expected answer
# down: "when a CTJS operation and the ctbrowser VM disagree, the VM is correct
# by definition."
  set(_diff_js "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Differential/differential.js")
  # The driver embeds the same file the pipeline compiles, as a raw string. A
  # compiled body bakes in the function index of every closure it builds (the
  # ABI row for ct_aot_make_closure: "a function index means nothing outside
  # the program it was compiled in"), so a hand-copied fixture that differed
  # even in function order would make compiled code close over the wrong
  # function, and a case could pass with the wrong answer.
  ctcompile_embed_js(differential.js.inc "${_diff_js}")
  ctcompile_compile_js(differential.generated.cpp "${_diff_js}" ENTRIES
    "plus;ge;strict;loose;pick;globals;apply;step;counter;middle;methodish;fn;neg;bnot;put;greet;pack;kindOf;thrower;build;Point;newBad;coalesce;chain;dflt;total;chars;spread;hasIt;isA;drop;greetChain;Kid;spreadCall;spreadMethod;spreadNew;merge;mergeArray;firstLoop;accessors;guarded;plusOf;coerce;keysOf;dropNamed;bigLits;howMany;sumAll;restOf;bothOf;wrapped;passes;arrayOut")
  ctcompile_boxed_test(differential differential.generated.cpp EMBED differential.js.inc)

# --- The same differential test for an ES module ---------------------------
#
# ctcompile_differential compiles one classic script and runs it with cx.run.
# op::load_import, op::bind_export and op::load_namespace are emitted only by
# compile_program's module arm, so a classic script cannot contain them and
# that harness cannot reach three of the four ES-module opcodes. This builds a
# three-module graph the way ctbrowser/lib/Shell/browser/scripts.cpp does and
# installs a compiled entry on the importing module's top level, so the
# backend also runs over functions[0] rather than only over named functions.
#
# Only module-main.js is compiled. dep and user stay interpreted in every arm,
# so only one variable changes: dep is where the live binding is written, and
# user is the only module that can see what bind_export decided.
  set(_mod_main "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Modules/main.js")
  set(_mod_dep "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Modules/dep.js")
  set(_mod_user "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Modules/user.js")
  # All three are embedded, as differential.js is, so the driver reads the
  # same bytes the pipeline compiled.
  set(_mod_incs "")
  foreach(_mod_js IN ITEMS "${_mod_main}" "${_mod_dep}" "${_mod_user}")
    get_filename_component(_mod_name "${_mod_js}" NAME)
    ctcompile_embed_js(module-${_mod_name}.inc "${_mod_js}")
    list(APPEND _mod_incs "module-${_mod_name}.inc")
  endforeach()

  # KIND module selects --ctbrowser-module-to-ctjs. Without it the fixture is
  # compiled as a classic script, every `import` is a syntax error, and the
  # failure looks like a bad fixture rather than a missing flag.
  #
  # `_script_` is the top level. ctbrowser/lib/Script/compile/entry.cpp names functions[0]
  # "<script>", the importer sanitises that to `_script_$0` and the backend
  # spells it `_script__0`, which this script renames to ctc__script_.
  ctcompile_compile_js(module-main.generated.cpp "${_mod_main}" KIND module
                       ENTRIES _script_ raise2 loadTwo fn)
  ctcompile_boxed_test(module_differential module-main.generated.cpp EMBED ${_mod_incs})

# --- Link test --------------------------------------------------------------
#
# A call to an AOT helper that aot.hpp declares but lib/Script/aot_bridge/
# does not define compiles fine and fails only at link time, and every EmitC
# lit test uses -fsyntax-only, so nothing else would notice.
#
# This compiles a function exercising everything the backend accepts and links
# it. The link is the whole assertion; the executable barely does anything.
  ctcompile_compile_js(linkable.generated.cpp
                       "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Linking/linkable.js" ENTRIES everything)
  ctcompile_boxed_test(linkable linkable.generated.cpp)
else()
  message(STATUS "ctcompile: gc-roots test unavailable (needs ctjs-opt and mlir-translate)")
endif()
