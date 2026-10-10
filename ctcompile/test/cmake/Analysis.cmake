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
#
# THE SELF-TEST ALONE, always registered. Its numbers are hand-computed - see
# the fixture's comment in TypeOracle.cpp - so it fails with a name rather than
# with a diff. Its modes and recording options are parsed by
# ctbrowser/test/support/options.hpp.
ctcompile_unit_test(type-oracle LIBS ctbrowser::ctbrowser Boost::program_options)

# THE TWO-IMPLEMENTATION COMPARISON - TypeOracle.cpp walking the recorder in
# memory against tools/check/type-oracle.py parsing the file it writes, one's
# counters the other's expectations - is Analysis/Types/oracle-checker.test.
# Python is still found here because the claims executables below need it.
find_package(Python3 QUIET COMPONENTS Interpreter)

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
  ctcompile_unit_test(type-inference
    LIBS CTNativeAnalysis CTNativeDialect CTJSDialect MLIRIR MLIRAnalysis MLIRParser
         MLIRControlFlowDialect MLIRSCFDialect MLIRUBDialect)
endif()

# ============================================================================
# PHASE 54A CHECKED BY PHASE 54B - THE ORACLE, CLOSED. Appended as ONE block.
#
# ctcompile-test-type-claims is the compiler's half: it imports a corpus, runs
# the inference and writes one claim per register in the checker's format. It
# is a SEPARATE executable from ctcompile-test-type-oracle on purpose - that
# one refuses to link MLIR so a recording cannot share a bug with the claim it
# is checking. Analysis/Types/claims-*.test runs both over one corpus and the Python
# checker over their two outputs, and the gate is zero soundness violations.
#
# FOUR CORPORA. The fixture is small, runs in milliseconds, and calls every
# guarded operator with BigInts so an unsound claim is OBSERVED rather than
# merely possible. Bootstrap is the real thing, prepended to the driver that
# Phase 54B wrote for it - and its precision is stated against the registers
# it actually reaches, which is an eighth of the bundle.
if(CTCOMPILE_ENABLE_MLIR AND Python3_Interpreter_FOUND)
  add_executable(ctcompile-test-type-claims)
  # --script, --out and --dump, parsed by ctbrowser/test/support/options.hpp.
  target_link_libraries(ctcompile-test-type-claims
    PRIVATE CTNativeAnalysis CTNativeDialect CTJSDialect ctcompile::ctjs-import
            MLIRIR MLIRAnalysis ctbrowser::ctbrowser Boost::program_options)
  ctcompile_target(ctcompile-test-type-claims)

  # FOUR CORPORA - the fixture, bootstrap behind its driver, p5 and phaser -
  # are Analysis/Types/claims-*.test, and scalar-unions' fixture beside them.
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
# escape-oracle work and its escape-claims/check.py; 4(g) is deferred by the
# design itself until 55B emits anything.
set(_cyc_js "${CMAKE_CURRENT_SOURCE_DIR}/Analysis/Escape/cycle.js")
# THE DRIVER READS THE SAME FILE EVERYTHING ELSE DOES - the recorder, the
# claims, the printed module - for gc-roots.js's reason: two copies of one
# fixture are two programs.
ctcompile_embed_js(escape-cycle.js.inc "${_cyc_js}")
set(_cyc_sources "${CMAKE_CURRENT_BINARY_DIR}/escape-cycle.js.inc")
set(_cyc_has_module OFF)
if(TARGET ctjs-translate)
  # THE PRINTED MODULE, embedded the same way. ctjs-translate is an
  # MlirTranslateMain tool, so `-o` is its own; the .mlir is then wrapped in
  # the raw string embed-js.cmake writes - the content is MLIR text and cannot
  # contain the delimiter. When there is no ctjs-translate the executable says
  # SKIPPED for (f) rather than the suite silently shrinking.
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
#
# FOUR EXECUTABLES SINCE 2026-09-08, because the one EscapeAnalysis.cpp had
# reached 2,763 lines: the sinks-and-carriers rows (sites, allocation, stores,
# operators, calls), the post-pass rows (control flow, accounting, R1/R4, the
# closure hole, the default rule, regions), the storage-evidence rows (targets,
# the all-write census, loads, provenance) and the array contents/retention
# tables. Every row is where it was, verbatim; they share EscapeAnalysisHarness.h.
if(CTCOMPILE_ENABLE_MLIR)
  foreach(_part IN ITEMS sinks completion storage arrays)
    ctcompile_unit_test(escape-analysis-${_part}
      LIBS CTNativeAnalysis CTNativeDialect CTJSDialect MLIRIR MLIRAnalysis MLIRParser
           MLIRControlFlowDialect)
  endforeach()
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
  add_executable(ctcompile-test-escape-claims)
  # --script and --out, parsed by ctbrowser/test/support/options.hpp.
  target_link_libraries(ctcompile-test-escape-claims
    PRIVATE CTNativeAnalysis CTNativeDialect CTJSDialect ctcompile::ctjs-import
            MLIRIR MLIRAnalysis ctbrowser::ctbrowser Boost::program_options)
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
# checker parses the file; Analysis/Escape/oracle-checker.test makes one's numbers the
# other's expectations. No MLIR anywhere near it.
add_test(NAME ctcompile_escape_oracle COMMAND ctcompile-test-type-oracle --escape)
# Its two checkers compared: Analysis/Escape/oracle-checker.test. And EVERY
# PLACE A FRAME ENDS, AS A TABLE - FrameEnds.def's citations are checked
# beside the ABI table's in Core/def-citations.test.

# The escape claims over four corpora: Analysis/Escape/escape-claims/*.test.

# A compiled frame's ip contains a catch-pad id, never a bytecode coordinate.
# Real AOT entries cover return, caught throw and mixed AOT/VM unwind without
# allowing their uninstrumented allocations to join native escape claims.
# [--out RECORDING], parsed by ctbrowser/test/support/options.hpp - as is the
# return oracle's below.
ctcompile_unit_test(escape-oracle-aot LIBS ctbrowser::ctbrowser Boost::program_options)
# Both AOT recordings through the Python checker: Analysis/Escape/aot-oracle.test.

# Observe the real boxed emitter's normalized return value, retaining the
# compiled sentinel coordinate rather than inventing a static source site.
if(TARGET ctjs-translate AND TARGET ctjs-opt AND MLIR_TRANSLATE_EXE)
  set(_aot_return_js "${CMAKE_CURRENT_SOURCE_DIR}/Analysis/Escape/aot-return.js")
  ctcompile_embed_js(escape-oracle-aot-return.js.inc "${_aot_return_js}")
  ctcompile_compile_js(escape-oracle-aot-return.generated.cpp "${_aot_return_js}"
                       ENTRIES oracleReturn oracleLocal oracleCtor)
  ctcompile_boxed_test(escape-oracle-aot-return escape-oracle-aot-return.generated.cpp
                       EMBED escape-oracle-aot-return.js.inc
                       LIBS ctbrowser::ctbrowser Boost::program_options)
endif()

# A slot census is usable only after its entire bounded proof completes.
#
# checkSharedMap - the shared two- and three-method Map family and its refusals -
# has been its own executable since 2026-09-08, when OwnedGlobalMethods.cpp
# reached 1,099 lines. Same fixtures (OwnedGlobalMethodsFixtures.h).
#
# Private provider transactions preserve JS Map equality and withhold partial state.
if(CTCOMPILE_ENABLE_MLIR)
  foreach(_test IN ITEMS owned-method-table-slots owned-global-roots owned-global-methods
                         owned-global-shared-map provider-state)
    ctcompile_unit_test(${_test} LIBS CTNativeAnalysis MLIRParser)
  endforeach()
endif()
