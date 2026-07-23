# every test is an executable running HEADLESS (no SDL): the page's
# HTML/CSS/JS parse at compile time inside each TU, then the binary
# runs its checks - a non-zero exit fails ctest
add_custom_target(ctbrowser-test ALL)

file(GLOB test-sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

foreach(source ${test-sources})
  get_filename_component(test "${source}" NAME_WE)
  if(test STREQUAL "render" AND NOT SDL3_FOUND)
    message(STATUS "ctbrowser: skipping the render test (no SDL3)")
    continue()
  endif()
  add_executable(ctbrowser-test-${test} "${source}")
  if(test STREQUAL "pong" AND TARGET ctbrowser-pong-inc)
    add_dependencies(ctbrowser-test-${test} ctbrowser-pong-inc)
  endif()
  target_link_libraries(ctbrowser-test-${test} PRIVATE ctbrowser::ctbrowser)
  target_compile_options(ctbrowser-test-${test} PRIVATE ${CTBROWSER_WARNING_OPTIONS})
  if(test STREQUAL "render")
    target_link_libraries(ctbrowser-test-${test} PRIVATE SDL3::SDL3 ${CTBROWSER_SAT_LIBS})
    target_compile_definitions(ctbrowser-test-${test} PRIVATE ${CTBROWSER_SAT_DEFS})
  endif()
  if(TARGET ctbrowser-pch-anchor)
    target_precompile_headers(ctbrowser-test-${test} REUSE_FROM ctbrowser-pch-anchor)
  endif()
  add_dependencies(ctbrowser-test ctbrowser-test-${test})
  add_test(NAME run-${test} COMMAND ctbrowser-test-${test}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endforeach()
