# The built executables. `lib/` is reusable implementation and this is what you
# run: ctbrowse opens a page and prints, ctdrive listens on a port and is driven.
#
# They were examples until the monorepo split, and being examples is what gave
# them their odd exceptions - the examples/ CMakeLists had to say "not ctbrowse
# and not ctdrive" three times: no -mwindows, no golden, no ctest, because a CLI
# with no arguments has nothing to render and ctdrive would wait on a socket
# forever. Those exceptions are gone with the move; a tool is simply not an
# example, and this file states what a tool is instead of what it is not.
#
# THE BINARY NAMES ARE UNCHANGED. `ctbrowse page.html` is what someone types,
# tools/check/compare.py spawns ./build/tools/ctdrive by path, and the target
# names moved from ctbrowser-example-* to ctbrowser-tool-* because they are not
# examples any more - that is the one rename, and it is not cosmetic.
foreach(tool ctbrowse ctdrive)
  add_executable(ctbrowser-tool-${tool} "${CMAKE_CURRENT_SOURCE_DIR}/${tool}/${tool}.cpp")
  target_link_libraries(ctbrowser-tool-${tool} PRIVATE ctbrowser::ctbrowser)
  ctbrowser_target(ctbrowser-tool-${tool})
  set_target_properties(ctbrowser-tool-${tool} PROPERTIES OUTPUT_NAME ${tool})
  # NO -mwindows, ever, whatever CTBROWSER_WINDOWS_CONSOLE says. Both are CLIs
  # whose OUTPUT IS THE PRODUCT: ctbrowse prints its usage and a page's script
  # errors, and ctdrive prints the PORT IT IS LISTENING ON, which is the only
  # way its client ever learns it. Give these the Windows GUI subsystem and the
  # parity harness hangs waiting for a line that can no longer be written.

  # The Windows dist ships them beside the examples. A GLOBAL property rather
  # than a variable because examples/CMakeLists.txt is a sibling scope, and it
  # is what assembles the dist.
  set_property(GLOBAL APPEND PROPERTY CTBROWSER_DIST_TARGETS ctbrowser-tool-${tool})
endforeach()
