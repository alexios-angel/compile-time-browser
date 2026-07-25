# v2 examples. Ordinary programs: no NTTP, no page<>, no compile-time anything.
#
# Registration is a glob - dropping a .cpp in here is the whole process, the way
# v1's examples worked. Each one links ONE target and imports ONE module; that
# claim is enforced by tests-v2/api_surface, not just asserted here.

if(NOT CTBROWSER_BUILD_EXAMPLES)
  return()
endif()

file(GLOB v2-example-sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
  file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
endif()

foreach(source ${v2-example-sources})
  get_filename_component(example "${source}" NAME_WE)
  add_executable(ctbrowser-v2-example-${example} "${source}")
  target_link_libraries(ctbrowser-v2-example-${example} PRIVATE ctbrowser::v2)
  target_compile_options(ctbrowser-v2-example-${example} PRIVATE ${CTBROWSER_V2_WARNINGS})
  # The target is prefixed so it cannot collide with v1's; the BINARY is not,
  # because `ctbrowse page.html` is what someone types.
  set_target_properties(ctbrowser-v2-example-${example} PROPERTIES OUTPUT_NAME ${example})

  # Every example is also a test: a bounded, fixed-timestep run that writes a
  # screenshot. `run_app` reads these env vars itself, so the example source
  # contains no test scaffolding at all.
  #
  # ctbrowse is the exception - it is a CLI that opens a file you name, so it
  # has nothing to render with no arguments. It is still built (and still
  # linted by tests-v2/api_surface), just not run.
  if(NOT CMAKE_CROSSCOMPILING AND BUILD_TESTING AND NOT example STREQUAL "ctbrowse")
    add_test(NAME v2-example-${example} COMMAND ctbrowser-v2-example-${example}
             WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    set_tests_properties(v2-example-${example} PROPERTIES ENVIRONMENT
      "SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;CTBROWSER_TEST_FRAMES=30;CTBROWSER_SCREENSHOT=${CMAKE_BINARY_DIR}/v2-shot-${example}.ppm")
  endif()
endforeach()
