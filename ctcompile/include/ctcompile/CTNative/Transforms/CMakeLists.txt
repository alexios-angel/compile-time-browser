set(LLVM_TARGET_DEFINITIONS Passes.td)
mlir_tablegen(Passes.h.inc -gen-pass-decls -name CTNative)
add_public_tablegen_target(CTNativeTransformsPassIncGen)
add_mlir_doc(Passes CTNativePasses ctcompile/CTNative/ -gen-pass-doc)

target_sources(ctcompile-ctnative-lowering PRIVATE
  Passes.h
  Passes.td
)
