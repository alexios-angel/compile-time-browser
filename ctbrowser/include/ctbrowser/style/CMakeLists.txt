target_sources(ctbrowser-style PUBLIC FILE_SET HEADERS FILES
  computed.hpp
  easing.hpp
  engine.hpp
  selector.hpp
  style.hpp
  ua.hpp
)

add_subdirectory(css)
