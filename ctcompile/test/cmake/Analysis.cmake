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
add_executable(ctcompile-test-type-oracle Analysis/Types/Oracle.cpp)
target_link_libraries(ctcompile-test-type-oracle PRIVATE ctbrowser::ctbrowser)
ctcompile_target(ctcompile-test-type-oracle)

# THE SELF-TEST ALONE, always registered. Its numbers are hand-computed - see
# the fixture's comment in TypeOracle.cpp - so it fails with a name rather than
# with a diff.
add_test(NAME ctcompile_type_oracle COMMAND ctcompile-test-type-oracle)

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
  add_executable(ctcompile-test-type-inference
    Analysis/Types/TypeInference/Main.cpp
    Analysis/Types/TypeInference/Helpers.cpp
    Analysis/Types/TypeInference/IdentityFields.cpp
    Analysis/Types/TypeInference/MapPresence.cpp
    Analysis/Types/TypeInference/MapDeletePresence.cpp
    Analysis/Types/TypeInference/FieldEffects.cpp
    Analysis/Types/TypeInference/ComparisonIdentity.cpp
    Analysis/Types/TypeInference/ScalarGlobals.cpp)
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
# is checking. Analysis/Types/claims-*.test runs both over one corpus and the Python
# checker over their two outputs, and the gate is zero soundness violations.
#
# FOUR CORPORA. The fixture is small, runs in milliseconds, and calls every
# guarded operator with BigInts so an unsound claim is OBSERVED rather than
# merely possible. Bootstrap is the real thing, prepended to the driver that
# Phase 54B wrote for it - and its precision is stated against the registers
# it actually reaches, which is an eighth of the bundle.
if(CTCOMPILE_ENABLE_MLIR AND Python3_Interpreter_FOUND)
  add_executable(ctcompile-test-type-claims Analysis/Types/Claims.cpp)
  target_link_libraries(ctcompile-test-type-claims
    PRIVATE CTNativeAnalysis CTNativeDialect CTJSDialect ctcompile::ctjs-import
            MLIRIR MLIRAnalysis ctbrowser::ctbrowser)
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
set(_cyc_inc "${CMAKE_CURRENT_BINARY_DIR}/escape-cycle.js.inc")
add_custom_command(
  OUTPUT "${_cyc_inc}"
  COMMAND "${CMAKE_COMMAND}" -DSOURCE=${_cyc_js} -DOUTPUT=${_cyc_inc}
          -P "${CMAKE_CURRENT_SOURCE_DIR}/Support/embed-js.cmake"
  DEPENDS "${_cyc_js}" "${CMAKE_CURRENT_SOURCE_DIR}/Support/embed-js.cmake"
  COMMENT "Embedding escape-cycle.js for the driver"
  VERBATIM)
set(_cyc_sources Analysis/Escape/Cycle.cpp "${_cyc_inc}")
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
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Support/embed-js.cmake"
    DEPENDS "${_cyc_mlir}" "${CMAKE_CURRENT_SOURCE_DIR}/Support/embed-js.cmake"
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
#
# FOUR EXECUTABLES SINCE 2026-09-08, because the one EscapeAnalysis.cpp had
# reached 2,763 lines: the sinks-and-carriers rows (sites, allocation, stores,
# operators, calls), the post-pass rows (control flow, accounting, R1/R4, the
# closure hole, the default rule, regions), the storage-evidence rows (targets,
# the all-write census, loads, provenance) and the array contents/retention
# tables. Every row is where it was, verbatim; they share EscapeAnalysisHarness.h.
if(CTCOMPILE_ENABLE_MLIR)
  add_executable(ctcompile-test-escape-analysis-sinks Analysis/Escape/Sinks.cpp)
  target_link_libraries(ctcompile-test-escape-analysis-sinks
    PRIVATE CTNativeAnalysis CTNativeDialect CTJSDialect MLIRIR MLIRAnalysis MLIRParser
            MLIRControlFlowDialect)
  ctcompile_target(ctcompile-test-escape-analysis-sinks)
  add_test(NAME ctcompile_escape_analysis_sinks COMMAND ctcompile-test-escape-analysis-sinks)

  add_executable(ctcompile-test-escape-analysis-completion Analysis/Escape/Completion.cpp)
  target_link_libraries(ctcompile-test-escape-analysis-completion
    PRIVATE CTNativeAnalysis CTNativeDialect CTJSDialect MLIRIR MLIRAnalysis MLIRParser
            MLIRControlFlowDialect)
  ctcompile_target(ctcompile-test-escape-analysis-completion)
  add_test(NAME ctcompile_escape_analysis_completion
           COMMAND ctcompile-test-escape-analysis-completion)

  add_executable(ctcompile-test-escape-analysis-storage Analysis/Escape/Storage.cpp)
  target_link_libraries(ctcompile-test-escape-analysis-storage
    PRIVATE CTNativeAnalysis CTNativeDialect CTJSDialect MLIRIR MLIRAnalysis MLIRParser
            MLIRControlFlowDialect)
  ctcompile_target(ctcompile-test-escape-analysis-storage)
  add_test(NAME ctcompile_escape_analysis_storage COMMAND ctcompile-test-escape-analysis-storage)

  add_executable(ctcompile-test-escape-analysis-arrays
    Analysis/Escape/EscapeAnalysisArrays/main.cpp
    Analysis/Escape/EscapeAnalysisArrays/Harness.cpp
    Analysis/Escape/EscapeAnalysisArrays/Contents.cpp
    Analysis/Escape/EscapeAnalysisArrays/Objects.cpp
    Analysis/Escape/EscapeAnalysisArrays/Selectors.cpp
    Analysis/Escape/EscapeAnalysisArrays/Primitives.cpp
    Analysis/Escape/EscapeAnalysisArrays/Primitives/BigInt.cpp
    Analysis/Escape/EscapeAnalysisArrays/Induction/Setup.cpp
    Analysis/Escape/EscapeAnalysisArrays/Induction/OverwritesAndTransport.cpp
    Analysis/Escape/EscapeAnalysisArrays/Induction/InvariantReads.cpp
    Analysis/Escape/EscapeAnalysisArrays/Induction/SignedStrides.cpp
    Analysis/Escape/EscapeAnalysisArrays/Induction/SignedArithmetic.cpp
    Analysis/Escape/EscapeAnalysisArrays/Induction/PowersAndProducts.cpp
    Analysis/Escape/EscapeAnalysisArrays/Induction/Validation.cpp
    Analysis/Escape/EscapeAnalysisArrays/Induction/Finish.cpp
    Analysis/Escape/EscapeAnalysisArrays/Induction/Cases.cpp
    Analysis/Escape/EscapeAnalysisArrays/Structured/Setup.cpp
    Analysis/Escape/EscapeAnalysisArrays/Structured/Reloads.cpp
    Analysis/Escape/EscapeAnalysisArrays/Structured/InvariantArithmetic.cpp
    Analysis/Escape/EscapeAnalysisArrays/Structured/SignedArithmetic.cpp
    Analysis/Escape/EscapeAnalysisArrays/Structured/PowersAndProducts.cpp
    Analysis/Escape/EscapeAnalysisArrays/Structured/MutationRefusals.cpp
    Analysis/Escape/EscapeAnalysisArrays/Structured/Finish.cpp
    Analysis/Escape/EscapeAnalysisArrays/Structured/Cases.cpp
    Analysis/Escape/EscapeAnalysisArrays/Length/Setup.cpp
    Analysis/Escape/EscapeAnalysisArrays/Length/IndexSnapshots.cpp
    Analysis/Escape/EscapeAnalysisArrays/Length/Arithmetic.cpp
    Analysis/Escape/EscapeAnalysisArrays/Length/ShrinkArithmetic.cpp
    Analysis/Escape/EscapeAnalysisArrays/Length/ShrinkRetention.cpp
    Analysis/Escape/EscapeAnalysisArrays/Length/LiveMutations.cpp
    Analysis/Escape/EscapeAnalysisArrays/Length/Finish.cpp
    Analysis/Escape/EscapeAnalysisArrays/Length/Cases.cpp
    Analysis/Escape/EscapeAnalysisArrays/BigIntErrors.cpp
    Analysis/Escape/EscapeAnalysisArrays/BigIntProducers.cpp
    Analysis/Escape/EscapeAnalysisArrays/BigIntStrings.cpp
    Analysis/Escape/EscapeAnalysisArrays/ControlFlow.cpp)
  target_link_libraries(ctcompile-test-escape-analysis-arrays
    PRIVATE CTNativeAnalysis CTNativeDialect CTJSDialect MLIRIR MLIRAnalysis MLIRParser
            MLIRControlFlowDialect)
  ctcompile_target(ctcompile-test-escape-analysis-arrays)
  add_test(NAME ctcompile_escape_analysis_arrays COMMAND ctcompile-test-escape-analysis-arrays)
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
  add_executable(ctcompile-test-escape-claims Analysis/Escape/Claims.cpp)
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
# checker parses the file; Analysis/Escape/oracle-checker.test makes one's numbers the
# other's expectations. No MLIR anywhere near it.
add_test(NAME ctcompile_escape_oracle COMMAND ctcompile-test-type-oracle --escape)
# Its two checkers compared: Analysis/Escape/oracle-checker.test. And EVERY
# PLACE A FRAME ENDS, AS A TABLE - FrameEnds.def's citations are checked
# beside the ABI table's in Core/def-citations.test.

# === PHASE 55 CLOSED: the escape claims over four corpora ===
# Analysis/Escape/escape-claims/{fixture,bootstrap,p5,phaser}.test, over
# escape-claims/check.py; the fixture is its three parts concatenated by the
# RUN line, byte for byte.

# A compiled frame's ip contains a catch-pad id, never a bytecode coordinate.
# Real AOT entries cover return, caught throw and mixed AOT/VM unwind without
# allowing their uninstrumented allocations to join native escape claims.
add_executable(ctcompile-test-escape-oracle-aot Analysis/Escape/AOTOracle.cpp)
target_link_libraries(ctcompile-test-escape-oracle-aot PRIVATE ctbrowser::ctbrowser)
ctcompile_target(ctcompile-test-escape-oracle-aot)
add_test(NAME ctcompile_escape_oracle_aot COMMAND ctcompile-test-escape-oracle-aot)
# Both AOT recordings through the Python checker: Analysis/Escape/aot-oracle.test.

# Observe the real boxed emitter's normalized return value, retaining the
# compiled sentinel coordinate rather than inventing a static source site.
if(TARGET ctjs-translate AND TARGET ctjs-opt AND MLIR_TRANSLATE_EXE)
  set(_aot_return_js "${CMAKE_CURRENT_SOURCE_DIR}/Analysis/Escape/aot-return.js")
  set(_aot_return_inc "${CMAKE_CURRENT_BINARY_DIR}/escape-oracle-aot-return.js.inc")
  set(_aot_return_cpp "${CMAKE_CURRENT_BINARY_DIR}/escape-oracle-aot-return.generated.cpp")
  add_custom_command(OUTPUT "${_aot_return_inc}"
    COMMAND "${CMAKE_COMMAND}" -DSOURCE=${_aot_return_js} -DOUTPUT=${_aot_return_inc}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/Support/embed-js.cmake"
    DEPENDS "${_aot_return_js}" "${CMAKE_CURRENT_SOURCE_DIR}/Support/embed-js.cmake" VERBATIM)
  add_custom_command(OUTPUT "${_aot_return_cpp}"
    COMMAND "${CMAKE_COMMAND}" -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
      -DOPT=$<TARGET_FILE:ctjs-opt> -DMLIR_TRANSLATE=${MLIR_TRANSLATE_EXE}
      -DSOURCE=${_aot_return_js} "-DENTRIES=oracleReturn;oracleLocal;oracleCtor"
      -DOUTPUT=${_aot_return_cpp} -P "${CMAKE_CURRENT_SOURCE_DIR}/Support/compile-js-to-cpp.cmake"
    DEPENDS "${_aot_return_js}" ctjs-translate ctjs-opt
      "${CMAKE_CURRENT_SOURCE_DIR}/Support/compile-js-to-cpp.cmake" VERBATIM)
  set_source_files_properties("${_aot_return_cpp}" PROPERTIES
    COMPILE_OPTIONS "${CTCOMPILE_GENERATED_WARNINGS}")
  add_executable(ctcompile-test-escape-oracle-aot-return
    Analysis/Escape/AOTReturnOracle.cpp "${_aot_return_cpp}" "${_aot_return_inc}")
  target_include_directories(ctcompile-test-escape-oracle-aot-return PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(ctcompile-test-escape-oracle-aot-return PRIVATE ctbrowser::ctbrowser)
  ctcompile_target(ctcompile-test-escape-oracle-aot-return)
  add_test(NAME ctcompile_escape_oracle_aot_return COMMAND ctcompile-test-escape-oracle-aot-return)
endif()

# A slot census is usable only after its entire bounded proof completes.
if(CTCOMPILE_ENABLE_MLIR)
  add_executable(ctcompile-test-owned-method-table-slots Analysis/Ownership/MethodTableSlots.cpp)
  target_link_libraries(ctcompile-test-owned-method-table-slots PRIVATE CTNativeAnalysis MLIRParser)
  ctcompile_target(ctcompile-test-owned-method-table-slots)
  add_test(NAME ctcompile_owned_method_table_slots COMMAND ctcompile-test-owned-method-table-slots)
  add_executable(ctcompile-test-owned-global-roots Analysis/Ownership/GlobalRoots.cpp)
  target_link_libraries(ctcompile-test-owned-global-roots PRIVATE CTNativeAnalysis MLIRParser)
  ctcompile_target(ctcompile-test-owned-global-roots)
  add_test(NAME ctcompile_owned_global_roots COMMAND ctcompile-test-owned-global-roots)
  add_executable(ctcompile-test-owned-global-methods Analysis/Ownership/GlobalMethods.cpp)
  target_link_libraries(ctcompile-test-owned-global-methods PRIVATE CTNativeAnalysis MLIRParser)
  ctcompile_target(ctcompile-test-owned-global-methods)
  add_test(NAME ctcompile_owned_global_methods COMMAND ctcompile-test-owned-global-methods)
  # checkSharedMap - the shared two- and three-method Map family and its refusals -
  # has been its own executable since 2026-09-08, when OwnedGlobalMethods.cpp
  # reached 1,099 lines. Same fixtures (OwnedGlobalMethodsFixtures.h).
  add_executable(ctcompile-test-owned-global-shared-map
    Analysis/Ownership/OwnedGlobalSharedMap/Main.cpp
    Analysis/Ownership/OwnedGlobalSharedMap/SavedScalarReads.cpp
    Analysis/Ownership/OwnedGlobalSharedMap/NumericEntry.cpp
    Analysis/Ownership/OwnedGlobalSharedMap/CapturedClear.cpp
    Analysis/Ownership/OwnedGlobalSharedMap/CapturedZeroSize.cpp
    Analysis/Ownership/OwnedGlobalSharedMap/CapturedExactSize.cpp
    Analysis/Ownership/OwnedGlobalSharedMap/CapturedDeleteSize.cpp
    Analysis/Ownership/OwnedGlobalSharedMap/DefiniteAbsence.cpp
    Analysis/Ownership/OwnedGlobalSharedMap/LeafReadbacks.cpp
    Analysis/Ownership/OwnedGlobalSharedMap/LeafReadbacks/Mixed.cpp
    Analysis/Ownership/OwnedGlobalSharedMap/LeafObjects.cpp
    Analysis/Ownership/OwnedGlobalSharedMap/LeafObjects/Children.cpp
    Analysis/Ownership/OwnedGlobalSharedMap/NestedCalls.cpp
    Analysis/Ownership/OwnedGlobalSharedMap/ObjectKeyArguments.cpp
    Analysis/Ownership/OwnedGlobalSharedMap/RetainedObjectKeyFamily.cpp
    Analysis/Ownership/OwnedGlobalSharedMap/DOMKeyInputs.cpp
    Analysis/Ownership/OwnedGlobalSharedMap/SharedMap.cpp
    Analysis/Ownership/OwnedGlobalSharedMap/SharedMap/Captured.cpp)
  target_link_libraries(ctcompile-test-owned-global-shared-map PRIVATE CTNativeAnalysis MLIRParser)
  ctcompile_target(ctcompile-test-owned-global-shared-map)
  add_test(NAME ctcompile_owned_global_shared_map COMMAND ctcompile-test-owned-global-shared-map)
endif()

# Private provider transactions preserve JS Map equality and withhold partial state.
if(CTCOMPILE_ENABLE_MLIR)
  add_executable(ctcompile-test-provider-state CTNative/HostContract/ProviderState.cpp)
  target_link_libraries(ctcompile-test-provider-state PRIVATE CTNativeAnalysis MLIRParser)
  ctcompile_target(ctcompile-test-provider-state)
  add_test(NAME ctcompile_provider_state COMMAND ctcompile-test-provider-state)
endif()
