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
#
# FOUR EXECUTABLES SINCE 2026-09-08, because the one EscapeAnalysis.cpp had
# reached 2,763 lines: the sinks-and-carriers rows (sites, allocation, stores,
# operators, calls), the post-pass rows (control flow, accounting, R1/R4, the
# closure hole, the default rule, regions), the storage-evidence rows (targets,
# the all-write census, loads, provenance) and the array contents/retention
# tables. Every row is where it was, verbatim; they share EscapeAnalysisHarness.h.
if(CTCOMPILE_ENABLE_MLIR)
  add_executable(ctcompile-test-escape-analysis-sinks EscapeAnalysisSinks.cpp)
  target_link_libraries(ctcompile-test-escape-analysis-sinks
    PRIVATE CTNativeAnalysis CTNativeDialect CTJSDialect MLIRIR MLIRAnalysis MLIRParser
            MLIRControlFlowDialect)
  ctcompile_target(ctcompile-test-escape-analysis-sinks)
  add_test(NAME ctcompile_escape_analysis_sinks COMMAND ctcompile-test-escape-analysis-sinks)

  add_executable(ctcompile-test-escape-analysis-completion EscapeAnalysisCompletion.cpp)
  target_link_libraries(ctcompile-test-escape-analysis-completion
    PRIVATE CTNativeAnalysis CTNativeDialect CTJSDialect MLIRIR MLIRAnalysis MLIRParser
            MLIRControlFlowDialect)
  ctcompile_target(ctcompile-test-escape-analysis-completion)
  add_test(NAME ctcompile_escape_analysis_completion
           COMMAND ctcompile-test-escape-analysis-completion)

  add_executable(ctcompile-test-escape-analysis-storage EscapeAnalysisStorage.cpp)
  target_link_libraries(ctcompile-test-escape-analysis-storage
    PRIVATE CTNativeAnalysis CTNativeDialect CTJSDialect MLIRIR MLIRAnalysis MLIRParser
            MLIRControlFlowDialect)
  ctcompile_target(ctcompile-test-escape-analysis-storage)
  add_test(NAME ctcompile_escape_analysis_storage COMMAND ctcompile-test-escape-analysis-storage)

  add_executable(ctcompile-test-escape-analysis-arrays EscapeAnalysisArrays.cpp)
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

# EVERY PLACE A FRAME ENDS, AS A TABLE. FrameEnds.def lists nine exit paths
# and says which three are hooked; the escape self-test
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

# A compiled frame's ip contains a catch-pad id, never a bytecode coordinate.
# Real AOT entries cover return, caught throw and mixed AOT/VM unwind without
# allowing their uninstrumented allocations to join native escape claims.
add_executable(ctcompile-test-escape-oracle-aot EscapeOracleAOT.cpp)
target_link_libraries(ctcompile-test-escape-oracle-aot PRIVATE ctbrowser::ctbrowser)
ctcompile_target(ctcompile-test-escape-oracle-aot)
add_test(NAME ctcompile_escape_oracle_aot COMMAND ctcompile-test-escape-oracle-aot)
if(Python3_Interpreter_FOUND)
  add_test(NAME ctcompile_escape_oracle_aot_checker
           COMMAND ${CMAKE_COMMAND}
                   -DEXE=$<TARGET_FILE:ctcompile-test-escape-oracle-aot>
                   -DPYTHON=${Python3_EXECUTABLE}
                   -DSCRIPT=${CTBROWSER_MONOREPO_ROOT}/tools/check/escape-oracle.py
                   -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/check-escape-oracle-aot.cmake)
endif()

# Observe the real boxed emitter's normalized return value, retaining the
# compiled sentinel coordinate rather than inventing a static source site.
if(TARGET ctjs-translate AND TARGET ctjs-opt AND MLIR_TRANSLATE_EXE)
  set(_aot_return_js "${CMAKE_CURRENT_SOURCE_DIR}/escape-oracle-aot-return.js")
  set(_aot_return_inc "${CMAKE_CURRENT_BINARY_DIR}/escape-oracle-aot-return.js.inc")
  set(_aot_return_cpp "${CMAKE_CURRENT_BINARY_DIR}/escape-oracle-aot-return.generated.cpp")
  add_custom_command(OUTPUT "${_aot_return_inc}"
    COMMAND "${CMAKE_COMMAND}" -DSOURCE=${_aot_return_js} -DOUTPUT=${_aot_return_inc}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/embed-js.cmake"
    DEPENDS "${_aot_return_js}" "${CMAKE_CURRENT_SOURCE_DIR}/embed-js.cmake" VERBATIM)
  add_custom_command(OUTPUT "${_aot_return_cpp}"
    COMMAND "${CMAKE_COMMAND}" -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
      -DOPT=$<TARGET_FILE:ctjs-opt> -DMLIR_TRANSLATE=${MLIR_TRANSLATE_EXE}
      -DSOURCE=${_aot_return_js} "-DENTRIES=oracleReturn;oracleLocal;oracleCtor"
      -DOUTPUT=${_aot_return_cpp} -P "${CMAKE_CURRENT_SOURCE_DIR}/compile-js-to-cpp.cmake"
    DEPENDS "${_aot_return_js}" ctjs-translate ctjs-opt
      "${CMAKE_CURRENT_SOURCE_DIR}/compile-js-to-cpp.cmake" VERBATIM)
  set_source_files_properties("${_aot_return_cpp}" PROPERTIES
    COMPILE_OPTIONS "${CTCOMPILE_GENERATED_WARNINGS}")
  add_executable(ctcompile-test-escape-oracle-aot-return
    EscapeOracleAOTReturn.cpp "${_aot_return_cpp}" "${_aot_return_inc}")
  target_include_directories(ctcompile-test-escape-oracle-aot-return PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(ctcompile-test-escape-oracle-aot-return PRIVATE ctbrowser::ctbrowser)
  ctcompile_target(ctcompile-test-escape-oracle-aot-return)
  add_test(NAME ctcompile_escape_oracle_aot_return COMMAND ctcompile-test-escape-oracle-aot-return)
  if(Python3_Interpreter_FOUND)
    add_test(NAME ctcompile_escape_oracle_aot_return_checker
      COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:ctcompile-test-escape-oracle-aot-return>
        -DPYTHON=${Python3_EXECUTABLE} -DSCRIPT=${CTBROWSER_MONOREPO_ROOT}/tools/check/escape-oracle.py
        -DWORK=${CMAKE_CURRENT_BINARY_DIR} -DNAME=aot-return -DUNCLAIMED=5
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check-escape-oracle-aot.cmake)
  endif()
endif()

# A slot census is usable only after its entire bounded proof completes.
if(CTCOMPILE_ENABLE_MLIR)
  add_executable(ctcompile-test-owned-method-table-slots OwnedMethodTableSlots.cpp)
  target_link_libraries(ctcompile-test-owned-method-table-slots PRIVATE CTNativeAnalysis MLIRParser)
  ctcompile_target(ctcompile-test-owned-method-table-slots)
  add_test(NAME ctcompile_owned_method_table_slots COMMAND ctcompile-test-owned-method-table-slots)
  add_executable(ctcompile-test-owned-global-roots OwnedGlobalRoots.cpp)
  target_link_libraries(ctcompile-test-owned-global-roots PRIVATE CTNativeAnalysis MLIRParser)
  ctcompile_target(ctcompile-test-owned-global-roots)
  add_test(NAME ctcompile_owned_global_roots COMMAND ctcompile-test-owned-global-roots)
  add_executable(ctcompile-test-owned-global-methods OwnedGlobalMethods.cpp)
  target_link_libraries(ctcompile-test-owned-global-methods PRIVATE CTNativeAnalysis MLIRParser)
  ctcompile_target(ctcompile-test-owned-global-methods)
  add_test(NAME ctcompile_owned_global_methods COMMAND ctcompile-test-owned-global-methods)
endif()

# Private provider transactions preserve JS Map equality and withhold partial state.
if(CTCOMPILE_ENABLE_MLIR)
  add_executable(ctcompile-test-provider-state ProviderState.cpp)
  target_link_libraries(ctcompile-test-provider-state PRIVATE CTNativeAnalysis MLIRParser)
  ctcompile_target(ctcompile-test-provider-state)
  add_test(NAME ctcompile_provider_state COMMAND ctcompile-test-provider-state)
endif()
