# The ctnative dialect's generators. Same wiring as ../../CTJS/IR, and
# deliberately not a variation on it: every generator invocation gets an
# add_public_tablegen_target, and every library consuming a generated header
# DEPENDS on it, because omitting that "produces builds that succeed serially
# and fail under -j".

# THE INCLUDE PATH TABLEGEN SEES IS A DIRECTORY PROPERTY, not a flag variable.
# LLVM's tablegen() reads INCLUDE_DIRECTORIES and turns each entry into a -I;
# LLVM_TABLEGEN_FLAGS never becomes an include path. Without this line a .td
# that includes a sibling by its PROJECT path does not resolve at all. The note
# in ../../CTJS/IR/CMakeLists.txt says it cost a build to find out.
include_directories("${PROJECT_SOURCE_DIR}/include")

# THE TYPES COME OUT OF THIS ONE CALL. add_mlir_dialect runs the op, dialect AND
# typedef backends over the file it is named for, which is why CTNativeOps.td
# includes CTNativeTypes.td and why the generated header is CTNativeOpsTypes.h.inc
# rather than CTNativeTypes.h.inc. Phase 53 has no operations, so the two op
# outputs are empty files; that costs nothing and means the first operation is a
# change to a .td rather than to this file.
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

set(LLVM_TARGET_DEFINITIONS CTNativeInterfaces.td)
mlir_tablegen(CTNativeOpInterfaces.h.inc -gen-op-interface-decls)
mlir_tablegen(CTNativeOpInterfaces.cpp.inc -gen-op-interface-defs)
add_public_tablegen_target(CTNativeInterfacesIncGen)
