target_sources(ctbrowser-script PRIVATE
  async.cpp
  date.cpp
  internal.hpp
  json.cpp
  values.cpp
)

add_subdirectory(async)
add_subdirectory(collections)
add_subdirectory(objects)
add_subdirectory(text)
