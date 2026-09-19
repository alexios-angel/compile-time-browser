add_library(ctcompile-css)
target_sources(ctcompile-css
  PRIVATE
    StyleProgramComparator.cpp)
ctcompile_target(ctcompile-css)

# Style pulls DOM in for the matcher's read transactions; neither needs a window.
target_link_libraries(ctcompile-css PUBLIC ctbrowser::core ctbrowser::dom ctbrowser::style)
add_library(ctcompile::css ALIAS ctcompile-css)
