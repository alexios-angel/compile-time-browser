add_llvm_executable(ctjs-opt ctjs-opt.cpp)
llvm_update_compile_flags(ctjs-opt)
target_link_libraries(ctjs-opt
  PRIVATE
    CTJSDialect
    ctcompile::mlir-support
    MLIRIR
    MLIROptLib
    MLIRPass
    MLIRArithDialect
    MLIRControlFlowDialect
    MLIRFuncDialect)
add_dependencies(ctjs-opt CTJSTransformsPassIncGen)
