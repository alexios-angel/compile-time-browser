# ctbrowser.style - selector matching, the cascade, computed values, the UA sheet.
#
# Sources are relative to this directory; headers are named in full under
# ${CTBROWSER_INCLUDE_DIR}. Adding a file to this subsystem now touches this
# file and nothing else - which is why the 897-line src/CMakeLists.txt was
# split on 2026-08-09. Dependencies are found in cmake/dependencies.cmake.

# --- ctbrowser.style -----------------------------------------------------
# Reuses ctcss's parser and replaces its matching: the previous engine's query() rescanned the
# whole sheet per property per element, this buckets rules by their rightmost
# simple selector and rejects descendant selectors with an ancestor filter.
# Depends on dom for read_txn - matching consumes a stable read view, never
# the live tree, which is what makes it parallelisable.
add_library(ctbrowser-style)
target_sources(ctbrowser-style
  PRIVATE
    engine.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/style.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/computed.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/selector.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/engine.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/style/ua.hpp")
target_compile_features(ctbrowser-style PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-style)
# ctcss's headers are part of style's own headers, so a consumer needs the path.
# `style/engine.hpp` includes <ctcss.hpp> for one private member's parameter
# type (`ctcss::value_sheet::selector`), and that is enough: every consumer of
# the PUBLIC header parses it.
#
# TWO INTERFACES, and it took an install to notice the second was missing. An
# exported target may not name a source-tree path, so the build tree gets
# BUILD_INTERFACE - and until 2026-08-09 that was the ONLY one, which made the
# comment above true of the build tree and false of an install. The symptom was
# not subtle: `find_package(ctbrowser)` succeeded and then the first
# `#include <ctbrowser.hpp>` died on "ctcss.hpp: No such file or directory".
# The package had never been compilable, and nothing said so because
# everything else in this tree consumes the engine through add_subdirectory.
target_include_directories(ctbrowser-style PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/external/compile-time-css/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

# ...and the headers themselves have to travel, or the path above points at
# nothing. ctcss is Apache-2.0 with LLVM Exceptions - the same licence as this
# repository, which began as a clone of it - so its licence rides along with
# it, and NOTICE records that this package SHIPS those headers rather than
# merely building against them.
#
# THE SKEW THIS CREATES IS REAL AND IS THE PRICE: an installed prefix now holds
# a COPY of ctcss's headers, so a consumer that also has its own ctcss can end
# up with two. `include/` rather than `include/.` on purpose - the trailing
# slash copies the directory's CONTENTS, so `ctcss.hpp` lands beside
# `ctbrowser.hpp` exactly as the build tree has it.
install(DIRECTORY "${PROJECT_SOURCE_DIR}/external/compile-time-css/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${PROJECT_SOURCE_DIR}/external/compile-time-css/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}" RENAME LICENSE-ctcss
        OPTIONAL)
target_link_libraries(ctbrowser-style PUBLIC ctbrowser-core ctbrowser-dom Boost::headers)

add_library(ctbrowser::style ALIAS ctbrowser-style)
