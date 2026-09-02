# The native tier's lowering: proved CTJS -> EmitC over plain C++ types. Links
# the analyses it consults and the dialects it emits into; nothing here names a
# runtime symbol, and part 24 §1.2 is why - the output of this pass links no
# ctbrowser library at all.
add_library(ctcompile-ctnative-lowering
  LowerToEmitC.cpp)
ctcompile_target(ctcompile-ctnative-lowering)
add_dependencies(ctcompile-ctnative-lowering CTNativeTransformsPassIncGen)

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
    MLIRControlFlowDialect)

target_include_directories(ctcompile-ctnative-lowering SYSTEM
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)

add_library(ctcompile::ctnative-lowering ALIAS ctcompile-ctnative-lowering)
