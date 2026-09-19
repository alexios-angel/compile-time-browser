# Test registration by subsystem. Includes share this directory scope so
# generated outputs, test commands and helper functions keep their meaning.
# Handwritten sources are registered by the directories that own them below.

# ONE check(), ONE COUNTER: ctbrowser/test/support/check.hpp - check(),
# CHECK, CHECK_EQ, REPORT and the terminate/signal trace - for every test
# executable below, where 24 private `int failures; void check()` copies were
# until 2026-09-15. Directory-wide, because every add_executable in the
# included files is a test. cpptrace is what check.hpp reaches for when its
# header is on the path (CTTest.cmake found it for the engine's suite; a box
# without it prints frames and addresses rather than nothing).
include_directories("${CTBROWSER_MONOREPO_ROOT}/ctbrowser/test/support")
if(CTBROWSER_CPPTRACE AND CTBROWSER_CPPTRACE_INCLUDE)
  include_directories(SYSTEM "${CTBROWSER_CPPTRACE_INCLUDE}")
  # ponytail: the shared library only - ctcompile is Linux-and-devbox only, and
  # the static/mingw dependency chain lives in CTTest.cmake if that changes.
  link_libraries("${CTBROWSER_CPPTRACE}")
endif()
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

# Local source ownership; target options and generated fixtures are configured above.
add_subdirectory(Analysis)
add_subdirectory(CTJS)
add_subdirectory(CTNative)
add_subdirectory(Comparison)
add_subdirectory(Core)
add_subdirectory(Packaging)
add_subdirectory(Runtime)
