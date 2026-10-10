add_library(ctcompile-html)
target_sources(ctcompile-html
  PRIVATE
    DocumentComparator.cpp)
ctcompile_target(ctcompile-html)

# Link Core for atoms and handles and DOM for the document itself, not the
# ctbrowser umbrella: comparing two documents needs neither a window nor a
# rasteriser, and the umbrella would make the compiler depend on SDL3 just to
# compare two trees.
target_link_libraries(ctcompile-html PUBLIC ctbrowser::core ctbrowser::dom)
add_library(ctcompile::html ALIAS ctcompile-html)
