# Analysis tests: the type and escape analyses and the oracles that check them
# against the interpreter.
#
# Type oracle: checks the static type inference against the types the
# interpreter actually observed at run time. The native backend compiles a
# function to statically typed C++ only where it can prove the function is in
# the static subset, and later passes rely on that proof. The interpreter
# settles whether the proof is right: it knows every value's real type at run
# time, so `--record-types` writes down what it saw and the checker compares a
# static claim against it.
#
# The check is implemented twice on purpose: test/Analysis/Types/Oracle.cpp
# walks the recorder in memory and tools/check/type-oracle.py parses the file it
# writes. Analysis/Types/oracle-checker.test runs both and uses one's counters
# as the other's expectations, so a bug in the writer or the parser shows up on
# exactly one side.
#
# It needs no MLIR: a recording is the interpreter's view, and asking the
# compiler anything here would make the check circular.
#
# The self-test, always registered. Its expected counts are hand-computed (see
# the fixture comment in test/Analysis/Types/Oracle.cpp), so a failure names
# the wrong counter instead of printing a diff. Options are parsed by
# ctbrowser/tools/support/options.hpp.
ctcompile_unit_test(type-oracle LIBS ctbrowser::ctbrowser ctbrowser-options)

# Python is found here because the claims executables below need it.
find_package(Python3 QUIET COMPONENTS Interpreter)

# ============================================================================
# Type inference transfer-function table.
#
# The oracle answers "is the inference sound over a corpus". This test answers
# "which rule is wrong", which the oracle cannot: it is a table of JavaScript
# facts, one small function each. Half its rows are negative (an operator whose
# operands might be BigInts must refuse to claim a number), and those are the
# rows that fail if anyone makes the analysis unconditional.
#
# Behind the MLIR guard, like every other target that names a dialect.
if(CTCOMPILE_ENABLE_MLIR)
  ctcompile_unit_test(type-inference
    LIBS CTNativeAnalysis CTNativeDialect CTJSDialect MLIRIR MLIRAnalysis MLIRParser
         MLIRControlFlowDialect MLIRSCFDialect MLIRUBDialect)
endif()

# ============================================================================
# Type claims: the compiler's side of the type oracle.
#
# ctcompile-test-type-claims imports a corpus, runs the inference and writes
# one claim per register in the checker's format. It is separate from
# ctcompile-test-type-oracle because that one must not link MLIR, so a
# recording cannot share a bug with the claim it checks. The tests under
# Analysis/Types/claims/ run both over one corpus and the Python checker over
# their two outputs, and require zero soundness violations.
#
# There are four corpora. The fixture is small, runs in milliseconds, and calls
# every guarded operator with BigInts so an unsound claim is observed rather
# than merely possible. Bootstrap is a real library, prepended to
# Analysis/Types/bootstrap-driver.js; its precision is stated against the
# registers the driver actually reaches, about an eighth of the bundle.
if(CTCOMPILE_ENABLE_MLIR AND Python3_Interpreter_FOUND)
  add_executable(ctcompile-test-type-claims)
  # --script, --out and --dump, parsed by ctbrowser/tools/support/options.hpp.
  target_link_libraries(ctcompile-test-type-claims
    PRIVATE CTNativeAnalysis CTNativeDialect CTJSDialect ctcompile::ctjs-import
            MLIRIR MLIRAnalysis ctbrowser::ctbrowser ctbrowser-options)
  ctcompile_target(ctcompile-test-type-claims)

  # The four corpora (the fixture, bootstrap behind its driver, p5 and phaser)
  # and the scalar-unions fixture are tests under Analysis/Types/claims/.
endif()

# ============================================================================
# Escape-analysis cycle fixture.
#
# One executable checks, against the running interpreter, what a reference
# cycle means: the tracing collector keeps a ring alive while any member is
# reachable and frees it whole when none is. Beside it, in C++, it shows the two
# ways "replace back-references with weak links" fails (a shared_ptr ring that
# leaks, a compiler-chosen weak_ptr that frees a node JavaScript can still
# reach) and the frame-owned region that is the correct RAII answer for a cycle
# confined to one frame. It also runs the ND-2 and ND-3 probes of
# ctcompile/docs/native-divergences.md and, when ctjs-translate is built, checks
# the fixture's printed ctjs module for `ctnative.weak`.
#
# It needs no MLIR, like ctcompile-test-type-oracle: every question here is put
# to the interpreter, and the module check is a string search over a file the
# build printed. Running the escape recorder over cycle.js is done by
# Analysis/Escape/claims/check.py, not here.
set(_cyc_js "${CMAKE_CURRENT_SOURCE_DIR}/Analysis/Escape/cycle.js")
# The driver embeds the same cycle.js the recorder, the claims and the printed
# module use: two copies of one fixture would drift into two programs.
ctcompile_embed_js(escape-cycle.js.inc "${_cyc_js}")
set(_cyc_sources "${CMAKE_CURRENT_BINARY_DIR}/escape-cycle.js.inc")
set(_cyc_has_module OFF)
if(TARGET ctjs-translate)
  # The printed module, embedded the same way. ctjs-translate is an
  # MlirTranslateMain tool, so `-o` is its own option; the .mlir is then wrapped
  # in the raw string embed-js.cmake writes, which MLIR text cannot terminate
  # early. Without ctjs-translate the executable reports the module check as
  # SKIPPED rather than silently running fewer checks.
  set(_cyc_mlir "${CMAKE_CURRENT_BINARY_DIR}/escape-cycle.ctjs.mlir")
  add_custom_command(
    OUTPUT "${_cyc_mlir}"
    COMMAND $<TARGET_FILE:ctjs-translate> --ctbrowser-js-to-ctjs "${_cyc_js}" -o "${_cyc_mlir}"
    DEPENDS "${_cyc_js}" ctjs-translate
    COMMENT "Importing escape-cycle.js to ctjs for the ctnative.weak check"
    VERBATIM)
  ctcompile_embed_js(escape-cycle.ctjs.inc "${_cyc_mlir}")
  list(APPEND _cyc_sources "${CMAKE_CURRENT_BINARY_DIR}/escape-cycle.ctjs.inc")
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

# ============================================================================
# Escape analysis: the sinks-and-carriers table, one cell at a time.
#
# The table lives in the CTJS op definitions as Arg<..., [CTJS_Sink...]>
# decorators; this tests it row by row: one positive row per NEITHER/CARRY cell
# (the site stays confined), one negative row per SINK cell (that operand alone
# makes the site escape, so a regression names the operand), the kind switch
# both ways, the loop and handler rows, the dead/unvisited accounting, the R1
# guard and R4 rule (see EscapeAnalysis.h), the closure hole, and the default
# rule. Every operand role is also read back through the generated interface,
# so a row can only be wrong by name.
#
# Behind the MLIR guard, like every other target that names a dialect.
#
# Four executables: sinks (sites, allocation, stores, operators, calls),
# completion (control flow, accounting, R1/R4, the closure hole, the default
# rule, regions), storage (targets, write census, loads, provenance) and arrays
# (contents and retention). Sources are in test/Analysis/Escape/ and share
# Harness.h.
if(CTCOMPILE_ENABLE_MLIR)
  foreach(_part IN ITEMS sinks completion storage arrays)
    ctcompile_unit_test(escape-analysis-${_part}
      LIBS CTNativeAnalysis CTNativeDialect CTJSDialect MLIRIR MLIRAnalysis MLIRParser
           MLIRControlFlowDialect)
  endforeach()
endif()

# Escape claims emitter: the compiler's side of the escape oracle. It imports a
# corpus, runs EscapeAnalysis, and writes one claim per allocation site in the
# format tools/check/escape-oracle.py reads. The corpus tests that compare it
# with the interpreter's recording are under Analysis/Escape/claims/.
if(CTCOMPILE_ENABLE_MLIR)
  add_executable(ctcompile-test-escape-claims)
  # --script and --out, parsed by ctbrowser/tools/support/options.hpp.
  target_link_libraries(ctcompile-test-escape-claims
    PRIVATE CTNativeAnalysis CTNativeDialect CTJSDialect ctcompile::ctjs-import
            MLIRIR MLIRAnalysis ctbrowser::ctbrowser ctbrowser-options)
  ctcompile_target(ctcompile-test-escape-claims)
endif()

# ============================================================================
# Escape oracle.
#
# The native backend gives an allocation an RAII lifetime (a stack object, a
# unique_ptr, a shared_ptr) only where an escape analysis can prove the object
# never outlives the activation that made it; anything it cannot prove is
# outside the native subset and is diagnosed. The interpreter is the reference
# semantics that settles whether a proof is right: its precise collector knows,
# at the moment a frame ends, exactly what is still reachable from which root,
# so the recorder borrows that root walk (bounded at the ending frame's base,
# mark only, never sweep) and writes down per site what happened to every
# object born there. The checker compares a static claim against it.
#
# Built like the type oracle: the escape self-test runs in the same executable
# as the type self-test (one recorder, one file format, header version 2),
# walks the recorder in memory and asserts a hand-computed table; the Python
# checker parses the file, and Analysis/Escape/oracle-checker.test compares the
# two. No MLIR is involved.
add_test(NAME ctcompile_escape_oracle COMMAND ctcompile-test-type-oracle --escape)
# FrameEnds.def lists every place a frame ends; its citations are checked
# beside the ABI table's in Core/def-citations.test.

# The escape claims over four corpora: Analysis/Escape/claims/*.test.

# The escape oracle over compiled (AOT) frames. A compiled frame's ip holds a
# catch-pad id, never a bytecode coordinate. Real AOT entries cover return,
# caught throw and mixed AOT/VM unwind without letting their uninstrumented
# allocations join native escape claims. Its [--out RECORDING] option, like
# the return oracle's below, is parsed by ctbrowser/tools/support/options.hpp.
ctcompile_unit_test(escape-oracle-aot LIBS ctbrowser::ctbrowser ctbrowser-options)
# Both AOT recordings through the Python checker: Analysis/Escape/aot/oracle.test.

# Observes the real boxed emitter's normalized return value, keeping the
# compiled sentinel coordinate rather than inventing a static source site.
if(TARGET ctjs-translate AND TARGET ctjs-opt AND MLIR_TRANSLATE_EXE)
  set(_aot_return_js "${CMAKE_CURRENT_SOURCE_DIR}/Analysis/Escape/aot/return.js")
  ctcompile_embed_js(escape-oracle-aot-return.js.inc "${_aot_return_js}")
  ctcompile_compile_js(escape-oracle-aot-return.generated.cpp "${_aot_return_js}"
                       ENTRIES oracleReturn oracleLocal oracleCtor)
  ctcompile_boxed_test(escape-oracle-aot-return escape-oracle-aot-return.generated.cpp
                       EMBED escape-oracle-aot-return.js.inc
                       LIBS ctbrowser::ctbrowser ctbrowser-options)
endif()

# Ownership analysis tests:
#   owned-method-table-slots - a slot census is usable only after its whole
#                              bounded proof completes;
#   owned-global-roots, owned-global-methods, owned-global-shared-map - global
#                              roots and method tables proved owned; the
#                              shared-map test holds the shared two- and
#                              three-method Map cases and their refusals, and
#                              shares Ownership/GlobalMethodsFixtures.h;
#   provider-state           - private provider transactions keep JS Map
#                              equality and publish no partial state.
if(CTCOMPILE_ENABLE_MLIR)
  foreach(_test IN ITEMS owned-method-table-slots owned-global-roots owned-global-methods
                         owned-global-shared-map provider-state)
    ctcompile_unit_test(${_test} LIBS CTNativeAnalysis MLIRParser)
  endforeach()
endif()
