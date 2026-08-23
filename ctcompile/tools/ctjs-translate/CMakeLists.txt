add_llvm_executable(ctjs-translate ctjs-translate.cpp)
llvm_update_compile_flags(ctjs-translate)
target_link_libraries(ctjs-translate
  PRIVATE
    CTJSDialect
    ctcompile::ctjs-import
    ctcompile::mlir-support
    MLIRIR
    MLIRTranslateLib)
