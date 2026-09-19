# Each top-level source is one independently runnable test.
file(GLOB tests RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS *.cpp)
foreach(test IN LISTS tests)
  get_filename_component(name "${test}" NAME_WE)
  ctbrowser_test(${name})
endforeach()

add_subdirectory(vm_functions)
