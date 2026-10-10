# The compiler's libraries. CTJS, CTNative and Target need MLIR and build only
# with CTCOMPILE_ENABLE_MLIR; Support, HTML, CSS and JavaScript always build.
if(CTCOMPILE_ENABLE_MLIR)
  add_subdirectory(CTJS)
  add_subdirectory(CTNative)
  add_subdirectory(Target)
endif()

add_subdirectory(Support)
add_subdirectory(HTML)
add_subdirectory(CSS)
add_subdirectory(JavaScript)
