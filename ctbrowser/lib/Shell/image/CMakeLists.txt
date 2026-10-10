# Image decoders that wrap third-party libraries (libjpeg-turbo, libpng).
target_sources(ctbrowser-shell PRIVATE
  jpeg.cpp
  png.cpp
)
