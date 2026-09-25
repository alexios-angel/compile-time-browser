target_sources(ctbrowser-script PRIVATE
  case_mapping.cpp
  case_mapping.inc
  globals.cpp
  internal.hpp
  regexp.cpp
  string.cpp
  symbol.cpp
)

add_subdirectory(regexp)
