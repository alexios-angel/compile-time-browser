# getComputedStyle's values: the entries, the layout-dependent values and their
# serialisation.
target_sources(ctbrowser-page PRIVATE
  entries.cpp
  geometry.hpp
  internal.hpp
  serialize.cpp
)
