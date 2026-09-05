# Test registration by subsystem. Includes share this directory scope so
# source paths, generated outputs and helper functions keep their meaning.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/CoreAndBoxed.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/Lit.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/Runtime.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/Analysis.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/Native.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/NativeGuards.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/NativeFixtures.cmake")
