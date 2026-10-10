# The include path TableGen sees. It must be a directory property, not a flag
# variable: LLVM's tablegen() runs
# `get_directory_property(tblgen_includes INCLUDE_DIRECTORIES)` and turns each
# entry into a -I, while LLVM_TABLEGEN_FLAGS is passed through untouched and
# never becomes an include path. The TableGen directories below inherit it.
#
# This lets a .td file include a sibling by its project path
# (`ctcompile/...`), the same spelling the C++ side uses, for both this
# project's .td files and MLIR's.
include_directories("${PROJECT_SOURCE_DIR}/include")

add_subdirectory(AOT)
add_subdirectory(CSS)
add_subdirectory(HTML)
add_subdirectory(JavaScript)
add_subdirectory(StdLib)
add_subdirectory(Support)

if(CTCOMPILE_ENABLE_MLIR)
  add_subdirectory(CTJS)
  add_subdirectory(CTNative)
  add_subdirectory(Target)
endif()
