# The early-error checker (see ../early_errors.hpp), split by concern: the
# private class in checker.hpp and one .cpp per group of rules.
target_sources(ctbrowser-script PRIVATE
  checker.hpp
  entry.cpp
  expressions.cpp
  functions.cpp
  patterns.cpp
  regexp.cpp
  scopes.cpp
  statements.cpp
)
