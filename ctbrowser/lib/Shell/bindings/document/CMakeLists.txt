target_sources(ctbrowser-shell PRIVATE
  as_node.cpp
  collections.cpp
  entry.cpp
  geometry.cpp
  install.cpp
  internal.hpp
  live_properties.cpp
  named_access.cpp
  range.cpp
  second_document.cpp
  traversal.cpp
  tree_accessors.cpp
  tree_ops.cpp
  write.cpp
)

add_subdirectory(range)
