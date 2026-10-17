# ctbrowser.page - what a page answers about its laid-out elements.
#
# The values getComputedStyle reports and the rectangles getBoundingClientRect
# and getClientRects report, computed from the document, the cascade, the box
# tree and the fragment tree; the author sheets, cascade and layout passes a
# frame runs; and static_page, which loads and lays out a document with them.
# No script engine: the shell's browser and bindings run the same functions,
# and a caller with no VM calls them directly and gets the same answers.
#
# ctbrowser-net (lib/Shell/net) is linked for the asset registry, which
# resolves a stylesheet's href; the headers of both stay under shell/ for now.
add_library(ctbrowser-page)
target_sources(ctbrowser-page PUBLIC FILE_SET HEADERS BASE_DIRS "${CTBROWSER_INCLUDE_DIR}")
target_sources(ctbrowser-page PRIVATE
  author_styles.cpp
  client_rects.cpp
  intersection.cpp
  pipeline.cpp
  static_page.cpp
)

add_subdirectory(computed_style)

target_compile_features(ctbrowser-page PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-page)
target_link_libraries(ctbrowser-page PUBLIC ctbrowser-core ctbrowser-dom ctbrowser-style
                                            ctbrowser-layout ctbrowser-paint ctbrowser-raster
                                            ctbrowser-net)

add_library(ctbrowser::page ALIAS ctbrowser-page)
