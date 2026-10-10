target_sources(ctbrowser-script PRIVATE
  case_mapping.cpp
  globals.cpp
  internal.hpp
  regexp.cpp
  string.cpp
  symbol.cpp
)

add_subdirectory(regexp)
