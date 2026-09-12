# The built executables. One so far, and it is the product.
add_subdirectory(ctcompile)

# THE MEASUREMENT TOOLS ARE BUILT BY `all`, ON PURPOSE - never EXCLUDE_FROM_ALL.
# A measurement binary older than the engine reports a number about code that
# is gone, and this tree has paid three times to learn it (docs/history/
# computed-goto.md, docs/performance.md, ctcompile/docs/baseline/page-load.json:
# a figure that was the previous session's binary). A plain add_executable is
# in `all`; that is the whole build edge.
add_subdirectory(ctbaseline)
add_subdirectory(ctpageload)

# THE MLIR DRIVERS. Built only with the pipeline they drive - see the comment on
# the CTJS subdirectories in ctcompile/CMakeLists.txt.
if(CTCOMPILE_ENABLE_MLIR)
  add_subdirectory(ctjs-opt)
  add_subdirectory(ctjs-translate)
endif()
