# Test registration by subsystem. Includes share this directory scope so
# source paths, generated outputs and helper functions keep their meaning.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/CoreAndBoxed.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/Runtime.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/Analysis.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/Native.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/NativeGuards.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/NativeFixtures.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/BootstrapData.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/PartialEvaluation.cmake")
# Last: lit.site.cfg.py carries CTCOMPILE_BOOTSTRAP_NODE (BootstrapData.cmake)
# and the reference executable (Native.cmake), so they must exist first.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/Lit.cmake")
