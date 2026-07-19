# windowed examples: need SDL3 (found by the top-level list). They run
# under ctest with the dummy video driver for a bounded number of
# frames via CTBROWSER_TEST_FRAMES, so CI exercises them headless.
file(GLOB example-sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# assets sit next to the built binaries so the shell's executable-
# relative path resolution finds them no matter the launch directory
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

foreach(source ${example-sources})
  get_filename_component(example "${source}" NAME_WE)
  add_executable(ctbrowser-example-${example} "${source}")
  if(TARGET ctbrowser-${example}-inc)
    add_dependencies(ctbrowser-example-${example} ctbrowser-${example}-inc)
  endif()
  if(example STREQUAL "fetchboard" AND CTBROWSER_EXAMPLES_FETCH)
    target_compile_options(ctbrowser-example-${example} PRIVATE
      "--fetch-allow=https://example.com/"
      "--fetch-allow=https://raw.githubusercontent.com/**")
  endif()
  target_link_libraries(ctbrowser-example-${example} PRIVATE ctbrowser::ctbrowser SDL3::SDL3 ${CTBROWSER_SAT_LIBS})
  target_compile_definitions(ctbrowser-example-${example} PRIVATE ${CTBROWSER_SAT_DEFS})
  if(TARGET ctbrowser-pch-anchor)
    target_precompile_headers(ctbrowser-example-${example} REUSE_FROM ctbrowser-pch-anchor)
  endif()
  add_test(NAME example-${example} COMMAND ctbrowser-example-${example}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
  set_tests_properties(example-${example} PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;CTBROWSER_TEST_FRAMES=30;CTBROWSER_SCREENSHOT=${CMAKE_BINARY_DIR}/shot-${example}.png")
endforeach()
