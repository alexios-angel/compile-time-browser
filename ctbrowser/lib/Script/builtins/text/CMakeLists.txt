target_sources(ctbrowser-script PRIVATE
  globals.cpp
  internal.hpp
  regexp.cpp
  string.cpp
  symbol.cpp
)

add_subdirectory(regexp)
