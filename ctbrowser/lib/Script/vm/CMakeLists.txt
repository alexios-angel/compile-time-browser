target_sources(ctbrowser-script PRIVATE
  coerce.cpp
  run_loop.cpp
)

add_subdirectory(call)
add_subdirectory(objects)
add_subdirectory(run_loop)
