target_sources(ctbrowser-script PRIVATE
  array.cpp
  array_iteration.cpp
  disposable.cpp
  iterator.cpp
  iterator_internal.hpp
  keyed.cpp
  weak.cpp
)

add_subdirectory(iterator)
add_subdirectory(typed_arrays)
