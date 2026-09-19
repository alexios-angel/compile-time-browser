target_sources(ctbrowser-script PRIVATE
  ast.cpp
  capture.cpp
  child_slots.hpp
  classes.cpp
  compiler_impl.hpp
  destructuring.cpp
  early_errors.hpp
  entry.cpp
  expression_calls.cpp
  expressions.cpp
  frames.cpp
  helpers.cpp
  limits.cpp
  strings.cpp
  with.cpp
)

add_subdirectory(compiler_impl)
add_subdirectory(early_errors)
add_subdirectory(statements)
