# See ../IR/CMakeLists.txt: TableGen reads the directory's INCLUDE_DIRECTORIES.
include_directories("${PROJECT_SOURCE_DIR}/include")

set(LLVM_TARGET_DEFINITIONS Passes.td)
mlir_tablegen(Passes.h.inc -gen-pass-decls -name CTNative)
add_public_tablegen_target(CTNativeTransformsPassIncGen)
add_mlir_doc(Passes CTNativePasses ctcompile/CTNative/ -gen-pass-doc)
