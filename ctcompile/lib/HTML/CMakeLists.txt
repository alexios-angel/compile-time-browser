add_library(ctcompile-html)
target_sources(ctcompile-html
  PRIVATE
    DocumentComparator.cpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${PROJECT_SOURCE_DIR}/include"
      FILES
        "${PROJECT_SOURCE_DIR}/include/ctcompile/HTML/DocumentComparator.hpp")
ctcompile_target(ctcompile-html)

# Core for atoms and handles, DOM for the document itself. NOT the ctbrowser
# umbrella: comparing two documents needs neither a window nor a rasteriser,
# and linking the umbrella here would make the compiler depend on SDL3 to
# answer a question about a tree.
target_link_libraries(ctcompile-html PUBLIC ctbrowser::core ctbrowser::dom)
add_library(ctcompile::html ALIAS ctcompile-html)
