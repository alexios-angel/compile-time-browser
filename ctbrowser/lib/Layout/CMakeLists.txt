# ctbrowser.layout - styled elements -> placed geometry.
#
# Sources are relative to this directory; headers are named in full under
# ${CTBROWSER_INCLUDE_DIR}. Adding a file to this subsystem now touches this
# file and nothing else - which is why the 897-line src/CMakeLists.txt was
# split on 2026-08-09. Dependencies are found in cmake/dependencies.cmake.

# --- ctbrowser.layout ----------------------------------------------------
# Consumes a styled box tree and produces an immutable fragment tree. Depends
# on core (scheduler, geometry), dom (read_txn) and style (computed styles) -
# but NOT the other way round: nothing upstream knows layout exists, which is
# what keeps the pipeline acyclic.
add_library(ctbrowser-layout)
target_sources(ctbrowser-layout
  PRIVATE
    box.cpp
    algorithm.cpp
    flex.cpp
    position.cpp
    engine.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/layout/layout.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/layout/values.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/layout/box.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/layout/fragment.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/layout/algorithm.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/layout/flex.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/layout/position.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/layout/engine.hpp")
target_compile_features(ctbrowser-layout PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-layout)
target_link_libraries(ctbrowser-layout PUBLIC ctbrowser-core ctbrowser-dom ctbrowser-style)

add_library(ctbrowser::layout ALIAS ctbrowser-layout)
