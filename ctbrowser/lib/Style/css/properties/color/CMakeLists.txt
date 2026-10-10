# CSS <color>: parsing, colour spaces, specified-value serialisation and
# resolution to a used colour.
target_sources(ctbrowser-style PRIVATE
  internal.hpp
  parser.cpp
  resolution.cpp
  spaces.cpp
  specified.cpp
)
