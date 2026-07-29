# the engine examples. Ordinary programs: no NTTP, no page<>, no compile-time anything.
#
# Registration is a glob - dropping a .cpp in here is the whole process, the way
# the previous engine's examples worked. Each one links ONE target and imports ONE module; that
# claim is enforced by tests/api_surface, not just asserted here.

if(NOT CTBROWSER_BUILD_EXAMPLES)
  return()
endif()

file(GLOB example-sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
  file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
endif()

foreach(source ${example-sources})
  get_filename_component(example "${source}" NAME_WE)
  add_executable(ctbrowser-example-${example} "${source}")
  target_link_libraries(ctbrowser-example-${example} PRIVATE ctbrowser::ctbrowser)
  ctbrowser_target(ctbrowser-example-${example})
  # The target is prefixed so it cannot collide with the previous engine's; the BINARY is not,
  # because `ctbrowse page.html` is what someone types.
  set_target_properties(ctbrowser-example-${example} PROPERTIES OUTPUT_NAME ${example})

  # NO CONSOLE BEHIND THE WINDOW. A Windows exe is a console application by
  # default, so double-clicking one of these opened a terminal beside the page
  # - which is right for a tool and wrong for something that only ever draws in
  # a window.
  #
  # NOT for ctbrowse and ctdrive, whatever the option says. Both are CLIs whose
  # output is the product: ctbrowse prints its usage and a page's script errors,
  # and ctdrive prints the PORT IT IS LISTENING ON, which is the only way its
  # client learns it. Give those the GUI subsystem and the harness hangs waiting
  # for a line that can no longer be written.
  #
  # -mwindows is all it takes; mingw-w64's startup provides the WinMain that
  # calls main, so nothing in the sources changes. Anything printed still
  # arrives when a console is attached deliberately (running it from a shell),
  # and goes nowhere when one is not - which is what a GUI application does.
  if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT CTBROWSER_WINDOWS_CONSOLE
     AND NOT example STREQUAL "ctbrowse" AND NOT example STREQUAL "ctdrive")
    target_link_options(ctbrowser-example-${example} PRIVATE -mwindows)
  endif()

  # Every example is also a test: a bounded, fixed-timestep run that writes a
  # screenshot. `run_app` reads these env vars itself, so the example source
  # contains no test scaffolding at all.
  #
  # ctbrowse and ctdrive are the exceptions - both are CLIs that open a file you
  # name, so they have nothing to render with no arguments (and ctdrive would
  # sit waiting on a socket forever). Both are still built, just not run.
  if(NOT CMAKE_CROSSCOMPILING AND BUILD_TESTING
     AND NOT example STREQUAL "ctbrowse" AND NOT example STREQUAL "ctdrive")
    add_test(NAME example-${example} COMMAND ctbrowser-example-${example}
             WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    # CTBROWSER_NETWORK=0: a test that reaches the internet fails for reasons
    # that have nothing to do with this code. fetchboard's network card takes
    # its catch branch here, which is the offline path and worth covering.
    set_tests_properties(example-${example} PROPERTIES ENVIRONMENT
      "SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;CTBROWSER_TEST_FRAMES=30;CTBROWSER_NETWORK=0;CTBROWSER_SCREENSHOT=${CMAKE_BINARY_DIR}/shot-${example}.ppm")

    # And for the two pages that exist to show what the ENGINE draws, compare
    # the render to a golden. Exiting 0 is not evidence: a page renders with
    # empty buttons, no caret, a <details> stuck open and every label glued to
    # its control and still exits 0 - which is how all of those shipped.
    #
    # Only these two: the others animate or fetch, so a byte-comparison would
    # be pinning a clock rather than a layout.
    # svg joins these two, and it is goldenable for a reason worth stating:
    # plutosvg 0.0.8 has NO text support at all, so nothing it draws depends on
    # a font, a font backend or CTBROWSER_FONTS. The page is static and
    # scriptless, and plutovg is a pure software rasteriser pinned to the same
    # version on both platforms - so one image compares byte-for-byte on Linux
    # and Windows. Gated on the dependency: without plutosvg the graphics are
    # simply absent, and the comparison would fail for a reason that is not a
    # regression.
    # p5basic joins them: the page is scriptless in the sense that matters -
    # noLoop, no random, no text and no image - so the frame it draws is the
    # same on every platform, and the golden is the only proof that the bundle
    # actually PAINTS rather than merely running.
    set(golden-examples widgets elements p5basic p5text p5transform p5shape)
    # plutosvg_FOUND, not CTBROWSER_WITH_SVG: the latter is a compile
    # definition on a target and is not a variable this scope can test.
    if(plutosvg_FOUND)
      list(APPEND golden-examples svg)
    endif()
    if(example IN_LIST golden-examples)
      add_test(NAME render-${example} COMMAND ${CMAKE_COMMAND}
               -DEXE=$<TARGET_FILE:ctbrowser-example-${example}>
               -DOUT=${CMAKE_BINARY_DIR}/render-${example}.ppm
               -DGOLDEN=${PROJECT_SOURCE_DIR}/tests/golden/${example}.ppm
               -DWORKDIR=${PROJECT_SOURCE_DIR}
               -P ${PROJECT_SOURCE_DIR}/tools/check-render.cmake)
    endif()
  endif()
endforeach()

# --- Windows cross builds -------------------------------------------------
#
# `windows-dist` collects the exes, SDL3.dll (the ONE runtime dependency -
# everything else is static) and the DATA the examples load into
# examples-windows/, ready to copy to a Windows host and run.
#
# The pages and assets have to come too: a the engine example calls run_app_file with a
# REPOSITORY-RELATIVE path, so the folder mirrors that layout and the exes work
# when run from its root.
#
# Its own directory rather than the previous engine's examples-windows/: several names collide
# (counter, game, invaders, fetchboard) and overwriting the previous engine's binaries with this engine's
# would be silent.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_CROSSCOMPILING)
  set(dist-dir "${PROJECT_SOURCE_DIR}/examples-windows")
  set(dist-commands COMMAND ${CMAKE_COMMAND} -E make_directory "${dist-dir}")
  foreach(source ${example-sources})
    get_filename_component(example "${source}" NAME_WE)
    list(APPEND dist-commands
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$<TARGET_FILE:ctbrowser-example-${example}>" "${dist-dir}/${example}.exe")
    list(APPEND dist-targets ctbrowser-example-${example})
  endforeach()
  # A statically linked SDL3 has no DLL to ship, which is the whole point:
  # the folder is exes and data, and each exe runs on its own. With the shared
  # SDL3 devel package instead, SDL3.dll (and SDL3_ttf.dll) come along.
  if(NOT CTBROWSER_SDL3_IS_STATIC)
    find_file(CTBROWSER_SDL3_DLL SDL3.dll PATH_SUFFIXES bin)
    if(CTBROWSER_SDL3_DLL)
      list(APPEND dist-commands
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CTBROWSER_SDL3_DLL}" "${dist-dir}")
    endif()
  endif()
  if(CTBROWSER_WITH_TTF AND NOT CTBROWSER_SDL3_TTF_IS_STATIC)
    find_file(CTBROWSER_SDL3_TTF_DLL SDL3_ttf.dll PATH_SUFFIXES bin)
    if(CTBROWSER_SDL3_TTF_DLL)
      list(APPEND dist-commands
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CTBROWSER_SDL3_TTF_DLL}" "${dist-dir}")
    endif()
  endif()
  # Named files, not the whole assets tree: that also holds the previous
  # engine's glTF models, which nothing here opens.
  #
  # p5.js DOES travel now, and it is 4.4 MB. It used to be excluded on the
  # grounds that no example opened it; four do - p5basic, p5text, p5transform
  # and p5shape - and the whole pages directory is copied, so leaving the
  # bundle behind would ship four pages that load nothing and render blank.
  list(APPEND dist-commands
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/pages" "${dist-dir}/examples/pages"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${dist-dir}/examples/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/examples/assets/sprites.bmp"
            "${PROJECT_SOURCE_DIR}/examples/assets/blip.wav"
            "${PROJECT_SOURCE_DIR}/examples/assets/chart.svg"
            "${PROJECT_SOURCE_DIR}/examples/assets/p5.js"
            "${dist-dir}/examples/assets"
    # PressStart2P is a PAGE's font, fetched by @font-face through a URL
    # (elements.html), so it travels whatever the build did with the UA faces -
    # embedding covers the three families use_real_fonts loads and nothing else.
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/examples/assets/fonts"
            "${dist-dir}/examples/assets/fonts")
  # The OFL faces back serif/sans-serif/monospace. Without them every exe falls
  # back to the built-in bitmap font, which works but is not what the gallery is
  # for; the licence rides along because the fonts do.
  #
  # Unless they are IN the exe, which is what the windows preset now does - then
  # shipping the directory as well would be shipping 5.2 MB nothing reads. The
  # licence still travels: it is next to the faces inside fonts/ and is copied
  # with them, so it is copied here on its own.
  if(CTBROWSER_EMBED_FONTS)
    # Globbed rather than named: the licences that need to travel are whatever
    # is in fonts/ that is not a face, and naming them is how you ship a dist
    # missing the licence for a font you added last month.
    file(GLOB font-licences "${PROJECT_SOURCE_DIR}/fonts/*.txt"
                            "${PROJECT_SOURCE_DIR}/fonts/*.md")
    list(APPEND dist-commands
      COMMAND ${CMAKE_COMMAND} -E make_directory "${dist-dir}/fonts"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${font-licences} "${dist-dir}/fonts")
  else()
    list(APPEND dist-commands
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${PROJECT_SOURCE_DIR}/fonts" "${dist-dir}/fonts")
  endif()
  add_custom_target(windows-dist ${dist-commands}
    DEPENDS ${dist-targets}
    COMMENT "collecting Windows exes + pages into examples-windows/"
    VERBATIM)
endif()
