target_sources(ctbrowser-style PRIVATE
  compiled.cpp
  keyframes.cpp
  media.cpp
  parser.cpp
  selector.cpp
  substitute.cpp
  token.cpp
)

add_subdirectory(calc)
add_subdirectory(parser)
add_subdirectory(properties)
add_subdirectory(selector)
add_subdirectory(substitute)
