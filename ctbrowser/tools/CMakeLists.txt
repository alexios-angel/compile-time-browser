# The built executables. `lib/` is reusable implementation and this is what you
# run: ctbrowse opens a page and prints, ctdrive listens on a port and is
# driven, ctrun is the launcher a packaged application is a copy of (so a
# launcher that was not built is a compiler that cannot emit anything), and
# ct262 runs one test262 file for tools/check/test262.py.
#
# THE BINARY NAMES ARE THE INTERFACE: `ctbrowse page.html` is what someone types
# and tools/check/compare.py spawns ./build/tools/ctdrive by path. NO -mwindows,
# ever, whatever CTBROWSER_WINDOWS_CONSOLE says: these are CLIs whose output is
# the product - ctdrive prints the port it is listening on, which is the only
# way its client ever learns it.
if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# THE COMMAND-LINE HELPER: support/options.hpp, which these tools and the test
# programs that take arguments (here and in ctcompile) parse argv through. Linking
# this target brings the header and Boost::program_options, found beside
# Boost.URL in cmake/dependencies.cmake.
add_library(ctbrowser-options INTERFACE)
target_include_directories(ctbrowser-options INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_sources(ctbrowser-options INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/support/options.hpp")
target_link_libraries(ctbrowser-options INTERFACE Boost::program_options)
# Directory names must not collide with the executables kept in this directory.
add_subdirectory(ctbrowse ctbrowse-build)
add_subdirectory(ctdrive ctdrive-build)
add_subdirectory(ctrun ctrun-build)
add_subdirectory(ct262 ct262-build)

# The Windows dist ships what a user runs, so not the conformance harness. A
# GLOBAL property because examples/CMakeLists.txt, a sibling scope, assembles it.
set_property(GLOBAL APPEND PROPERTY CTBROWSER_DIST_TARGETS
             ctbrowser-tool-ctbrowse ctbrowser-tool-ctdrive ctbrowser-tool-ctrun)
