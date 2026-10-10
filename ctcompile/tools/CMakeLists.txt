# The built executables: ctcompile (the product), measurement tools, and the MLIR drivers.
add_subdirectory(ctcompile)

# The measurement tools are built by `all` on purpose; never mark them
# EXCLUDE_FROM_ALL. A measurement binary older than the engine reports numbers
# for code that no longer exists, and stale binaries have already put wrong
# figures into ctbrowser/docs/history/computed-goto.md,
# ctbrowser/docs/performance.md and ctcompile/docs/baseline/page-load.json. A
# plain add_executable is in `all`, which is all this needs.
add_subdirectory(ctbaseline)
add_subdirectory(ctpageload)

# Compiled CSS: a stylesheet to C++ constants. It needs no MLIR either.
add_subdirectory(ctcss-translate)

# The MLIR drivers, built only when CTCOMPILE_ENABLE_MLIR builds the pipeline
# they drive.
if(CTCOMPILE_ENABLE_MLIR)
  add_subdirectory(ctjs-opt)
  add_subdirectory(ctjs-translate)
endif()
