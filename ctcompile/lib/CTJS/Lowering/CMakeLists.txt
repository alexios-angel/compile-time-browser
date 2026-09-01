# The lowering, as an ordinary library for the reason the importer is one: it is
# hand-written C++ and generates nothing.
add_library(ctcompile-ctjs-lowering
  CTJSToEmitC.cpp
  CTJSToRuntime.cpp
  DropUncompiled.cpp
  EmitCBlockArguments.cpp
  LiftToSCF.cpp)
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

target_include_directories(ctcompile-ctjs-lowering SYSTEM
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)

add_library(ctcompile::ctjs-lowering ALIAS ctcompile-ctjs-lowering)
