# ctbrowser.page - what a page answers about its laid-out elements.
#
# The values getComputedStyle reports and the rectangles getBoundingClientRect
# and getClientRects report, computed from the document, the cascade, the box
# tree and the fragment tree. No script engine: the shell's bindings adapt these
# functions to the VM, and a caller with no VM calls them directly and gets the
# same answers.
add_library(ctbrowser-page)
target_sources(ctbrowser-page PUBLIC FILE_SET HEADERS BASE_DIRS "${CTBROWSER_INCLUDE_DIR}")
target_sources(ctbrowser-page PRIVATE
  client_rects.cpp
)

add_subdirectory(computed_style)

target_compile_features(ctbrowser-page PUBLIC cxx_std_23)
ctbrowser_target(ctbrowser-page)
target_link_libraries(ctbrowser-page PUBLIC ctbrowser-core ctbrowser-dom ctbrowser-style
                                            ctbrowser-layout ctbrowser-paint ctbrowser-raster)

add_library(ctbrowser::page ALIAS ctbrowser-page)
