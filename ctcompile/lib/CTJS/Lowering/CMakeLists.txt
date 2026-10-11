# CTJS -> EmitC lowering passes. A plain add_library rather than an MLIR
# TableGen library: every source is hand-written and nothing is generated.
add_library(ctcompile-ctjs-lowering
  ArrayIteration.cpp
  CTJSToEmitC.cpp
  DropUncompiled.cpp
  EmitCBlockArguments.cpp
  LiftToSCF.cpp
  ResolveGlobals.cpp
  Spreads.cpp)
ctcompile_target(ctcompile-ctjs-lowering)
add_dependencies(ctcompile-ctjs-lowering MLIRCTJSOpsIncGen CTJSTransformsPassIncGen)

target_link_libraries(ctcompile-ctjs-lowering
  PUBLIC
    CTJSDialect
    ctbrowser::script
    MLIRIR
    MLIRPass
    MLIRFuncDialect
    MLIREmitCDialect
    MLIRControlFlowDialect
    MLIRSCFDialect
    MLIRUBDialect
    MLIRControlFlowToSCF
    MLIRTransforms)

add_library(ctcompile::ctjs-lowering ALIAS ctcompile-ctjs-lowering)

add_subdirectory(EmitC)
add_subdirectory(Globals)
