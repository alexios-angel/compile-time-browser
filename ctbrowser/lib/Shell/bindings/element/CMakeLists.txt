target_sources(ctbrowser-shell PRIVATE
  attributes.cpp
  autocomplete.cpp
  character_data.cpp
  child_views.cpp
  control_methods.cpp
  dataset.cpp
  declarations.cpp
  geometry_views.cpp
  hyperlink.cpp
  interfaces.cpp
  internal.hpp
  methods.cpp
  namespace_methods.cpp
  node_methods.cpp
  pre_insert.cpp
  reflection.cpp
  reflection_rows.cpp
  rendered_text.cpp
  shadow.cpp
  style_views.cpp
  view_geometry.hpp
  views.cpp
  wrapper.cpp
)

add_subdirectory(control_methods)
