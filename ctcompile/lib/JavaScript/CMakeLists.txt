add_library(ctcompile-javascript)
target_sources(ctcompile-javascript
  PRIVATE
    ProgramComparator.cpp)
ctcompile_target(ctcompile-javascript)

# Script for the program itself, Core for atoms and handles. Comparing two
# programs needs no window and no rasteriser.
target_link_libraries(ctcompile-javascript PUBLIC ctbrowser::core ctbrowser::script)
add_library(ctcompile::javascript ALIAS ctcompile-javascript)
