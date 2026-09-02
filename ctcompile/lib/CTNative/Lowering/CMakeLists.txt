# THE PDLL PATTERN, COMPILED BY mlir-pdll INTO THE BUILD TREE.
#
# add_mlir_pdll_library IS THE OUT-OF-TREE HELPER AND IT WORKS HERE, verified
# against the installed MLIR 23.1.0 rather than assumed: AddMLIR.cmake defines
# it (line 114 of the installed copy), MLIRConfig.cmake sets
# MLIR_PDLL_TABLEGEN_EXE to the imported `mlir-pdll` target, and the helper's
# _pdll_tablegen turns the DIRECTORY's INCLUDE_DIRECTORIES into -I flags exactly
# as mlir_tablegen does. No add_custom_command fallback was needed.
#
# THE OUTPUT IS A BUILD ARTEFACT (Principle 9): PruneDeadStores.h.inc lands in
# ${CMAKE_CURRENT_BINARY_DIR} and is never committed. The helper ends in
# add_public_tablegen_target, so there IS an explicit target, and the library
# below DEPENDS on it (Principle 10) - without that edge the build succeeds
# serially and fails under -j.
#
# EXTRA_INCLUDES REACHES MLIR'S OWN .td FILES so the `#include
# "mlir/Dialect/EmitC/IR/EmitC.td"` at the top of the pattern resolves and
# `op<emitc.call>` is checked against ODS. The directory already inherits
# ${MLIR_INCLUDE_DIRS} from ctcompile/CMakeLists.txt, but naming it here is
# what the policy asks for and what stops a reorganisation upstairs breaking a
# pattern file downstairs.
add_mlir_pdll_library(CTNativePruneDeadStoresPDLLIncGen
  PruneDeadStores.pdll
  PruneDeadStores.h.inc
  EXTRA_INCLUDES
    ${MLIR_INCLUDE_DIRS})

# The native tier's lowering: proved CTJS -> EmitC over plain C++ types. Links
# the analyses it consults and the dialects it emits into; nothing here names a
# runtime symbol, and part 24 §1.2 is why - the output of this pass links no
# ctbrowser library at all.
add_library(ctcompile-ctnative-lowering
  LowerToEmitC.cpp
  PrintDeduced.cpp
  PruneDeadStores.cpp)
ctcompile_target(ctcompile-ctnative-lowering)
add_dependencies(ctcompile-ctnative-lowering
  CTNativeTransformsPassIncGen
  CTNativePruneDeadStoresPDLLIncGen)

target_link_libraries(ctcompile-ctnative-lowering
  PUBLIC
    CTNativeAnalysis
    CTNativeDialect
    CTJSDialect
    MLIRIR
    MLIRPass
    MLIRAnalysis
    MLIREmitCDialect
    MLIRSCFDialect
    MLIRControlFlowDialect
    # THE PDLL PATTERN'S RUNTIME. mlir-pdll does not compile a matcher: it
    # emits the pattern as PDL TEXT that MLIRParser parses at pass-run time
    # (MLIRPDLDialect), that MLIRPDLToPDLInterp lowers to pdl_interp
    # (MLIRPDLInterpDialect), and that MLIRRewritePDL executes as bytecode. A
    # declarative pattern in this release is an interpreter, not generated code.
    #
    # NONE OF THESE FOUR IS NEW TO THE LINK, and that was measured rather than
    # assumed: MLIRTargets.cmake gives MLIRRewrite the interface libraries
    # "MLIRIR;MLIRSideEffectInterfaces;LLVM;MLIRPDLDialect;MLIRPDLInterpDialect;
    # MLIRPDLToPDLInterp;MLIRRewritePDL", and libMLIRRewrite.a's
    # PatternApplicator.cpp.o has undefined references to PDLByteCode whatever
    # the project does - so anything in this tree that already ran a pattern
    # driver already carried the PDL interpreter. Dropping the four from an
    # otherwise identical relink of ctjs-opt fails with "undefined reference to
    # mlir::detail::PDLByteCode::match". They are named here because this
    # library uses them directly and a transitive edge is not a declaration.
    #
    # WHAT THE PATTERN ACTUALLY COST, measured on the devbox 2026-09-02 by
    # compiling this TU both ways with the same command line and relinking
    # ctjs-opt against each: the object grew 58,424 -> 105,400 bytes and took
    # 2.75 -> 3.27 s, and the binary grew 12,270,824 -> 12,289,440 bytes
    # (+0.15%). mlir-pdll itself runs in 11 ms.
    MLIRParser
    MLIRPDLDialect
    MLIRPDLInterpDialect
    MLIRPDLToPDLInterp
    MLIRRewrite
    MLIRRewritePDL
    MLIRTransforms)

target_include_directories(ctcompile-ctnative-lowering SYSTEM
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)

# WHERE mlir-pdll PUT THE HEADER. PRIVATE and not SYSTEM: it is this library's
# own generated file, included by its own name from PruneDeadStores.cpp, and no
# consumer should be able to reach it.
target_include_directories(ctcompile-ctnative-lowering
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR})

add_library(ctcompile::ctnative-lowering ALIAS ctcompile-ctnative-lowering)
