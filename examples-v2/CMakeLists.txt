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
  ctbrowser_v2_target(ctbrowser-v2-example-${example})
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
    # CTBROWSER_NETWORK=0: a test that reaches the internet fails for reasons
    # that have nothing to do with this code. fetchboard's network card takes
    # its catch branch here, which is the offline path and worth covering.
    set_tests_properties(v2-example-${example} PROPERTIES ENVIRONMENT
      "SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;CTBROWSER_TEST_FRAMES=30;CTBROWSER_NETWORK=0;CTBROWSER_SCREENSHOT=${CMAKE_BINARY_DIR}/v2-shot-${example}.ppm")

    # And for the two pages that exist to show what the ENGINE draws, compare
    # the render to a golden. Exiting 0 is not evidence: a page renders with
    # empty buttons, no caret, a <details> stuck open and every label glued to
    # its control and still exits 0 - which is how all of those shipped.
    #
    # Only these two: the others animate or fetch, so a byte-comparison would
    # be pinning a clock rather than a layout.
    if(example STREQUAL "widgets" OR example STREQUAL "elements")
      add_test(NAME v2-render-${example} COMMAND ${CMAKE_COMMAND}
               -DEXE=$<TARGET_FILE:ctbrowser-v2-example-${example}>
               -DOUT=${CMAKE_BINARY_DIR}/v2-render-${example}.ppm
               -DGOLDEN=${PROJECT_SOURCE_DIR}/tests-v2/golden/${example}.ppm
               -DWORKDIR=${PROJECT_SOURCE_DIR}
               -P ${PROJECT_SOURCE_DIR}/tools/check-render.cmake)
    endif()
  endif()
endforeach()

# --- Windows cross builds -------------------------------------------------
#
# `windows-dist-v2` collects the exes, SDL3.dll (the ONE runtime dependency -
# everything else is static) and the DATA the examples load into
# examples-windows-v2/, ready to copy to a Windows host and run.
#
# The pages and assets have to come too: a v2 example calls run_app_file with a
# REPOSITORY-RELATIVE path, so the folder mirrors that layout and the exes work
# when run from its root.
#
# Its own directory rather than v1's examples-windows/: several names collide
# (counter, game, invaders, fetchboard) and overwriting v1's binaries with v2's
# would be silent.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_CROSSCOMPILING)
  set(v2-dist-dir "${PROJECT_SOURCE_DIR}/examples-windows-v2")
  set(v2-dist-commands COMMAND ${CMAKE_COMMAND} -E make_directory "${v2-dist-dir}")
  foreach(source ${v2-example-sources})
    get_filename_component(example "${source}" NAME_WE)
    list(APPEND v2-dist-commands
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$<TARGET_FILE:ctbrowser-v2-example-${example}>" "${v2-dist-dir}/${example}.exe")
    list(APPEND v2-dist-targets ctbrowser-v2-example-${example})
  endforeach()
  # A statically linked SDL3 has no DLL to ship, which is the whole point:
  # the folder is exes and data, and each exe runs on its own. With the shared
  # SDL3 devel package instead, SDL3.dll (and SDL3_ttf.dll) come along.
  if(NOT CTBROWSER_SDL3_IS_STATIC)
    find_file(CTBROWSER_SDL3_DLL SDL3.dll PATH_SUFFIXES bin)
    if(CTBROWSER_SDL3_DLL)
      list(APPEND v2-dist-commands
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CTBROWSER_SDL3_DLL}" "${v2-dist-dir}")
    endif()
  endif()
  if(CTBROWSER_WITH_TTF AND NOT CTBROWSER_SDL3_TTF_IS_STATIC)
    find_file(CTBROWSER_SDL3_TTF_DLL SDL3_ttf.dll PATH_SUFFIXES bin)
    if(CTBROWSER_SDL3_TTF_DLL)
      list(APPEND v2-dist-commands
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CTBROWSER_SDL3_TTF_DLL}" "${v2-dist-dir}")
    endif()
  endif()
  # Named files, not the whole assets tree: that also holds v1's glTF models
  # and p5.js, which is 20 MB no v2 example opens.
  list(APPEND v2-dist-commands
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/pages" "${v2-dist-dir}/examples-v2/pages"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${v2-dist-dir}/examples/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/examples/assets/sprites.bmp"
            "${PROJECT_SOURCE_DIR}/examples/assets/blip.wav"
            "${v2-dist-dir}/examples/assets"
    # The OFL faces back serif/sans-serif/monospace. Without them every exe
    # falls back to the built-in bitmap font, which works but is not what the
    # gallery is for; the licence rides along because the fonts do.
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/fonts" "${v2-dist-dir}/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/examples/assets/fonts"
            "${v2-dist-dir}/examples/assets/fonts")
  add_custom_target(windows-dist-v2 ${v2-dist-commands}
    DEPENDS ${v2-dist-targets}
    COMMENT "collecting v2 Windows exes + pages into examples-windows-v2/"
    VERBATIM)
endif()
