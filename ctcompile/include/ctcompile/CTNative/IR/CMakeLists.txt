# The ctnative dialect's generators. Same wiring as ctcompile/include/ctcompile/CTJS/IR, and
# deliberately not a variation on it: every generator invocation gets an
# add_public_tablegen_target, and every library consuming a generated header
# DEPENDS on it, because omitting that "produces builds that succeed serially
# and fail under -j".

# THE TYPES COME OUT OF THIS ONE CALL. add_mlir_dialect runs the op, dialect AND
# typedef backends over the file it is named for, which is why CTNativeOps.td
# includes CTNativeTypes.td and why the generated header is CTNativeOpsTypes.h.inc
# rather than CTNativeTypes.h.inc. The same invocation generates the typed
# native operation declarations and definitions.
set(LLVM_TARGET_DEFINITIONS CTNativeOps.td)
add_mlir_dialect(CTNativeOps ctnative)
add_mlir_doc(CTNativeOps CTNativeDialect ctcompile/CTNative/ -gen-dialect-doc)

# THE TWO ENUMS ARE A SEPARATE INVOCATION because they are not attributes.
# CTNativeEnums.td sets genSpecializedAttr = 0 - they exist to parameterize
# types - so there is no -gen-attrdef-* call here at all, and the dialect
# declares no attribute printer/parser hooks to go with one.
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
