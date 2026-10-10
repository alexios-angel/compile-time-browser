# The style engine's implementation files, split by concern, compiled into
# the ctbrowser-style library.
target_sources(ctbrowser-style PRIVATE
  cascade.cpp
  cascade_values.cpp
  cascade_values.hpp
  selectors.cpp
  sheets.cpp
)
