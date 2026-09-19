# The native tier's lowering: proved CTJS -> EmitC over plain C++ types. Links
# the analyses it consults and the dialects it emits into. Partial evaluation
# also reuses compiler-side primitive helpers from ctbrowser; the generated
# C++ uses only the public platform libraries it needs, never Script.
add_library(ctcompile-ctnative-lowering
  LowerToEmitC.cpp
  LoweringSupport.cpp
  PrintDeduced.cpp
  PruneDeadStores.cpp)
ctcompile_target(ctcompile-ctnative-lowering)
add_dependencies(ctcompile-ctnative-lowering CTNativeTransformsPassIncGen)

target_link_libraries(ctcompile-ctnative-lowering
  PRIVATE
    # Compiler-side primitive semantics only. Generated native C++ links no VM.
    ctbrowser::script
  PUBLIC
    ctcompile::ctjs-lowering
    CTNativeAnalysis
    CTNativeDialect
    CTJSDialect
    MLIRIR
    MLIRPass
    MLIRControlFlowToSCF
    MLIRUBDialect
    MLIRAnalysis
    MLIREmitCDialect
    MLIRSCFDialect
    MLIRControlFlowDialect
    # PatternApplicator and FrozenRewritePatternSet, which the symbolic
    # rewrite and the unary-plus pattern set use directly.
    MLIRRewrite
    MLIRTransforms)

add_library(ctcompile::ctnative-lowering ALIAS ctcompile-ctnative-lowering)

target_sources(ctcompile-ctnative-lowering PRIVATE
  LoweringSupport.h
)

add_subdirectory(Admission)
add_subdirectory(ClosureLifting)
add_subdirectory(EmitC)
add_subdirectory(Exceptions)
add_subdirectory(ObjectValues)
add_subdirectory(StringValues)
