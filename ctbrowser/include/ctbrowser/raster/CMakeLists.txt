target_sources(ctbrowser-raster PUBLIC FILE_SET HEADERS FILES
  draw.hpp
  gl.hpp
  raster.hpp
  surface.hpp
  svg.hpp
  tile.hpp
)

add_subdirectory(backend)
add_subdirectory(text)
