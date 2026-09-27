# THE INCLUDE PATH TABLEGEN SEES, and it is a DIRECTORY PROPERTY rather than a
# flag variable - which is not obvious and cost a build to find out. LLVM's
# tablegen() does `get_directory_property(tblgen_includes INCLUDE_DIRECTORIES)`
# and turns each entry into a -I; LLVM_TABLEGEN_FLAGS is passed through
# untouched and never becomes an include path. Said once here: the TableGen
# directories below inherit it.
#
# The policy asks that "EXTRA_INCLUDES must reach both the project's own .td
# files and MLIR's", and this is how. Without it a .td that includes a sibling
# by its PROJECT path - which is how the C++ side spells it, so that the two
# agree - does not resolve at all.
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
