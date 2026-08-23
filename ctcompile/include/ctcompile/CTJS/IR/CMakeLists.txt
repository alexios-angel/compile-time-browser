# THE POLICY'S WIRING, VERBATIM. Every generator invocation gets an
# add_public_tablegen_target, and every library that consumes a generated header
# DEPENDS on it - "not optional. Omitting them produces builds that succeed
# serially and fail under -j".
#
# The types are generated HERE, by add_mlir_dialect over CTJSOps.td, which is
# why that file includes CTJSTypes.td. There is no -gen-typedef-decls call
# anywhere in the policy and there is deliberately none here.

# THE INCLUDE PATH TABLEGEN SEES, and it is a DIRECTORY PROPERTY rather than a
# flag variable - which is not obvious and cost a build to find out. LLVM's
# tablegen() does `get_directory_property(tblgen_includes INCLUDE_DIRECTORIES)`
# and turns each entry into a -I; LLVM_TABLEGEN_FLAGS is passed through
# untouched and never becomes an include path.
#
# The policy asks that "EXTRA_INCLUDES must reach both the project's own .td
# files and MLIR's", and this is how. Without it a .td that includes a sibling
# by its PROJECT path - which is how the C++ side spells it, so that the two
# agree - does not resolve at all.
include_directories("${PROJECT_SOURCE_DIR}/include")

set(LLVM_TARGET_DEFINITIONS CTJSOps.td)
add_mlir_dialect(CTJSOps ctjs)
add_mlir_doc(CTJSOps CTJSDialect ctcompile/CTJS/ -gen-dialect-doc)

set(LLVM_TARGET_DEFINITIONS CTJSEnums.td)
mlir_tablegen(CTJSEnums.h.inc -gen-enum-decls)
mlir_tablegen(CTJSEnums.cpp.inc -gen-enum-defs)
add_public_tablegen_target(CTJSEnumsIncGen)

set(LLVM_TARGET_DEFINITIONS CTJSAttrs.td)
mlir_tablegen(CTJSAttrs.h.inc -gen-attrdef-decls -attrdefs-dialect=ctjs)
mlir_tablegen(CTJSAttrs.cpp.inc -gen-attrdef-defs -attrdefs-dialect=ctjs)
add_public_tablegen_target(CTJSAttrsIncGen)

set(LLVM_TARGET_DEFINITIONS CTJSInterfaces.td)
mlir_tablegen(CTJSOpInterfaces.h.inc -gen-op-interface-decls)
mlir_tablegen(CTJSOpInterfaces.cpp.inc -gen-op-interface-defs)
add_public_tablegen_target(CTJSInterfacesIncGen)
