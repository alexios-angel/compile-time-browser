# every test is an executable running HEADLESS (no SDL): the page's
# HTML/CSS/JS parse at compile time inside each TU, then the binary
# runs its checks - a non-zero exit fails ctest
add_custom_target(ctbrowser-test ALL)

file(GLOB test-sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

foreach(source ${test-sources})
  get_filename_component(test "${source}" NAME_WE)
  add_executable(ctbrowser-test-${test} "${source}")
  target_link_libraries(ctbrowser-test-${test} PRIVATE ctbrowser::ctbrowser)
  if(TARGET ctbrowser-pch-anchor)
    target_precompile_headers(ctbrowser-test-${test} REUSE_FROM ctbrowser-pch-anchor)
  endif()
  add_dependencies(ctbrowser-test ctbrowser-test-${test})
  add_test(NAME run-${test} COMMAND ctbrowser-test-${test})
endforeach()
