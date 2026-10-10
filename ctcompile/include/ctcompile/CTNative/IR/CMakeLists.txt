# The ctnative dialect's generators. Same wiring as
# ctcompile/include/ctcompile/CTJS/IR, deliberately: every generator invocation
# gets an add_public_tablegen_target, and every library consuming a generated
# header DEPENDS on it, because without that builds succeed serially and fail
# under -j.

# The types come out of this one call. add_mlir_dialect runs the op, dialect and
# typedef backends over the file it is named for, which is why CTNativeOps.td
# includes CTNativeTypes.td and why the generated header is CTNativeOpsTypes.h.inc
# rather than CTNativeTypes.h.inc. The same invocation generates the typed
# native operation declarations and definitions.
set(LLVM_TARGET_DEFINITIONS CTNativeOps.td)
add_mlir_dialect(CTNativeOps ctnative)
add_mlir_doc(CTNativeOps CTNativeDialect ctcompile/CTNative/ -gen-dialect-doc)

# The enums are a separate invocation because they are not attributes.
# CTNativeEnums.td sets genSpecializedAttr = 0 (they parameterize types), so
# there is no -gen-attrdef-* call here, and the dialect declares no attribute
# printer/parser hooks to go with one.
set(LLVM_TARGET_DEFINITIONS CTNativeEnums.td)
mlir_tablegen(CTNativeEnums.h.inc -gen-enum-decls)
mlir_tablegen(CTNativeEnums.cpp.inc -gen-enum-defs)
add_public_tablegen_target(CTNativeEnumsIncGen)

target_sources(CTNativeDialect PRIVATE
  CTNativeAttrNames.h
  CTNativeBase.td
  CTNativeDialect.h
  CTNativeEnums.h
  CTNativeEnums.td
  CTNativeLattice.h
  CTNativeOps.h
  CTNativeOps.td
  CTNativeTypes.h
  CTNativeTypes.td
)
