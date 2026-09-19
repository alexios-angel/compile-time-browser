target_sources(ctbrowser-style PRIVATE
  alignment.cpp
  backgrounds.cpp
  cascade.cpp
  color.cpp
  declaration.cpp
  display.cpp
  expansion.hpp
  filter.cpp
  font_family.cpp
  grammar.cpp
  grid.cpp
  image.cpp
  internal.hpp
  keywords.cpp
  shadows.cpp
  shorthands.cpp
  table.cpp
  table_modules.cpp
  transforms.cpp
  urange.cpp
)

add_subdirectory(color)
add_subdirectory(grammar)
add_subdirectory(shorthands)
