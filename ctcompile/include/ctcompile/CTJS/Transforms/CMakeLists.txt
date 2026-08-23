# See the note in ../IR/CMakeLists.txt: the include path TableGen reads is the
# directory's INCLUDE_DIRECTORIES property, not LLVM_TABLEGEN_FLAGS.
include_directories("${PROJECT_SOURCE_DIR}/include")

set(LLVM_TARGET_DEFINITIONS Passes.td)
mlir_tablegen(Passes.h.inc -gen-pass-decls -name CTJS)
add_public_tablegen_target(CTJSTransformsPassIncGen)
add_mlir_doc(Passes CTJSPasses ctcompile/CTJS/ -gen-pass-doc)
