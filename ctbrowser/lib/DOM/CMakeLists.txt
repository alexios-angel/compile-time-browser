# ctbrowser.dom - the WHATWG tokenizer and tree builder, and the document as a slab.
#
# Sources are relative to this directory; headers are named in full under
# ${CTBROWSER_INCLUDE_DIR}. Adding a file to this subsystem now touches this
# file and nothing else - which is why the 897-line src/CMakeLists.txt was
# split on 2026-08-09. Dependencies are found in cmake/dependencies.cmake.

# --- ctbrowser.dom -------------------------------------------------------
# Its own WHATWG tokenizer and tree builder. This wrapped cthtml's value parser
# at first; the conformant one replaced it, and with it the last reason the DOM
# needed a parser from outside.
add_library(ctbrowser-dom)
target_sources(ctbrowser-dom
  PRIVATE
    document.cpp
    tokenizer.cpp
    treebuilder.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CTBROWSER_INCLUDE_DIR}"
      FILES
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/dom/dom.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/dom/node.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/dom/document.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/dom/tokenizer.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/dom/treebuilder.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/dom/html.hpp"
        "${CTBROWSER_INCLUDE_DIR}/ctbrowser/dom/entities.hpp")
target_compile_features(ctbrowser-dom PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-dom)
target_link_libraries(ctbrowser-dom PUBLIC ctbrowser-core)

add_library(ctbrowser::dom ALIAS ctbrowser-dom)
