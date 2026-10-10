# The page-level pieces of the shell: accessibility, canvas 2D, forms, input
# types, the SVG raster cache and WebGL.
target_sources(ctbrowser-shell PRIVATE
  accessibility.cpp
  canvas.cpp
  canvas_path.cpp
  forms.cpp
  input_types.cpp
  svg_cache.cpp
  webgl.cpp
)
