set(LLVM_TARGET_DEFINITIONS Passes.td)
mlir_tablegen(Passes.h.inc -gen-pass-decls -name CTJS)
add_public_tablegen_target(CTJSTransformsPassIncGen)
add_mlir_doc(Passes CTJSPasses ctcompile/CTJS/ -gen-pass-doc)

target_sources(ctcompile-ctjs-lowering PRIVATE
  Passes.h
  Passes.td
)
