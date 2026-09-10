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
foreach(tool ctbrowse ctdrive ctrun ct262)
  add_executable(ctbrowser-tool-${tool} "${CMAKE_CURRENT_SOURCE_DIR}/${tool}/${tool}.cpp")
  target_link_libraries(ctbrowser-tool-${tool} PRIVATE ctbrowser::ctbrowser)
  ctbrowser_target(ctbrowser-tool-${tool})
  set_target_properties(ctbrowser-tool-${tool} PROPERTIES OUTPUT_NAME ${tool})
endforeach()

# The Windows dist ships what a user runs, so not the conformance harness. A
# GLOBAL property because examples/CMakeLists.txt, a sibling scope, assembles it.
set_property(GLOBAL APPEND PROPERTY CTBROWSER_DIST_TARGETS
             ctbrowser-tool-ctbrowse ctbrowser-tool-ctdrive ctbrowser-tool-ctrun)
