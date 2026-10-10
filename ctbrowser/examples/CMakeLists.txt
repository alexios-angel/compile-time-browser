# Example programs for the engine: ordinary C++ with no compile-time pages.
#
# Each subdirectory (corpus/, demos/) globs its own .cpp files, so adding an
# example means dropping a .cpp into one of them. Each example links one target
# (ctbrowser::ctbrowser) and includes one header (<ctbrowser.hpp>);
# test/lint/api_surface enforces that.

if(NOT CTBROWSER_BUILD_EXAMPLES)
  return()
endif()

# Child directories discover only their own sources. Put every example binary
# in this directory, which screenshots, packaging and command lines expect.
set(CTBROWSER_EXAMPLE_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")
if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
  file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# `ctbrowser_example(<source>)` builds one example and, when testing, registers
# its smoke test and any golden render tests.
function(ctbrowser_example source)
  get_filename_component(example "${source}" NAME_WE)
  add_executable(ctbrowser-example-${example} "${source}")
  target_link_libraries(ctbrowser-example-${example} PRIVATE ctbrowser::ctbrowser)
  ctbrowser_target(ctbrowser-example-${example})
  # The target name is prefixed to avoid clashing with other targets; the
  # binary keeps the bare example name because that is what a user types.
  set_target_properties(ctbrowser-example-${example} PROPERTIES OUTPUT_NAME ${example})

  # No console behind the window (unless CTBROWSER_WINDOWS_CONSOLE). A Windows
  # exe is a console application by default, so double-clicking one opens a
  # terminal beside the page.
  #
  # -mwindows is enough: mingw-w64's startup provides the WinMain that calls
  # main, so the sources do not change. Output still appears when the exe is
  # run from a shell, and goes nowhere otherwise, as for any GUI application.
  if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT CTBROWSER_WINDOWS_CONSOLE)
    target_link_options(ctbrowser-example-${example} PRIVATE -mwindows)
  endif()

  # Every example is also a test: a bounded, fixed-timestep run that writes a
  # screenshot. `run_app` reads these env vars itself, so the example source
  # contains no test scaffolding at all.
  if(NOT CMAKE_CROSSCOMPILING AND BUILD_TESTING)
    add_test(NAME example-${example} COMMAND ctbrowser-example-${example}
             WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    # CTBROWSER_NETWORK=0: a test that reaches the internet fails for reasons
    # unrelated to this code. fetchboard's network card takes its catch branch
    # here, which covers the offline path.
    set_tests_properties(example-${example} PROPERTIES ENVIRONMENT
      "CTBROWSER_FONT_PATH=${PROJECT_SOURCE_DIR}/resources/fonts;SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;CTBROWSER_TEST_FRAMES=30;CTBROWSER_NETWORK=0;CTBROWSER_SCREENSHOT=${CMAKE_BINARY_DIR}/shot-${example}.ppm")

    # Pages that exist to show what the engine draws also compare their render
    # to a golden image. Exiting 0 is not evidence: a page with empty buttons,
    # no caret, a <details> stuck open or labels glued to their controls still
    # exits 0. Only pages whose frame is deterministic belong here (static,
    # noLoop, no Math.random, or a library that draws once in create()); a page
    # that animates or fetches would pin a clock rather than a layout.
    set(golden-examples widgets elements p5basic p5text p5transform p5shape p5pixels
        p5image webgltriangle p5webgl phaserbasic phaserinvaders babylonscene
        babylonorbit)
    # plutosvg_FOUND, not CTBROWSER_WITH_SVG: the latter is a compile
    # definition on a target, not a variable this scope can test.
    if(plutosvg_FOUND)
      list(APPEND golden-examples svg)
    endif()
    # The imageformats golden is registered only when SDL3_image is found or
    # CTBROWSER_WITH_IMAGE is set. The engine now decodes PNG itself (libpng),
    # so this gate is stricter than the page needs.
    if(SDL3_image_FOUND OR CTBROWSER_WITH_IMAGE)
      list(APPEND golden-examples imageformats)
    endif()
    # Pages whose pixels come from WebGL. They are rendered only through ANGLE,
    # so their goldens live in test/golden/angle/ and they skip the plain render
    # test (a golden in test/golden/ for one of them came from a software GL
    # rasteriser that no longer exists, and never matches ANGLE: on p5webgl, 4
    # of 93,600 pixels differ, all on triangle edges). phaserbasic and
    # phaserinvaders use Phaser's canvas renderer, so they keep the plain test.
    set(webgl-examples webgltriangle p5webgl babylonscene babylonorbit)

    if(example IN_LIST golden-examples)
      # A game needs more frames than a drawing. The invaders page must fire,
      # travel and hit before its frame shows anything, so it gets a frame
      # count that reaches a state worth comparing.
      set(render-frames 30)
      if(example STREQUAL phaserinvaders)
        set(render-frames 150)
      endif()
      # Babylon compiles its effects through promises, so the first frames
      # legitimately draw nothing while they settle; at 30 frames the golden
      # would be pure background.
      if(example STREQUAL babylonscene OR example STREQUAL babylonorbit)
        set(render-frames 90)
      endif()
      if(NOT example IN_LIST webgl-examples)
        add_test(NAME render-${example} COMMAND ${CMAKE_COMMAND}
                 -DEXE=$<TARGET_FILE:ctbrowser-example-${example}>
                 -DOUT=${CMAKE_BINARY_DIR}/render-${example}.ppm
                 -DGOLDEN=${PROJECT_SOURCE_DIR}/test/golden/${example}.ppm
                 -DWORKDIR=${PROJECT_SOURCE_DIR}
                 -DFRAMES=${render-frames}
                 -P ${CTBROWSER_MONOREPO_ROOT}/tools/check/check-render.cmake
                 WORKING_DIRECTORY "${CTBROWSER_EXAMPLE_BINARY_DIR}")
      endif()

      # WebGL pages render through ANGLE against their own golden in
      # test/golden/angle/, rather than against another rasteriser with a
      # tolerance: two rasterisers differ at triangle edges (1.0% of pixels on
      # webgltriangle), and a tolerance large enough for that stops catching
      # regressions.
      if(CTBROWSER_WITH_ANGLE AND example IN_LIST webgl-examples)
        add_test(NAME render-angle-${example} COMMAND ${CMAKE_COMMAND}
                 -DEXE=$<TARGET_FILE:ctbrowser-example-${example}>
                 -DOUT=${CMAKE_BINARY_DIR}/render-angle-${example}.ppm
                 -DGOLDEN=${PROJECT_SOURCE_DIR}/test/golden/angle/${example}.ppm
                 -DWORKDIR=${PROJECT_SOURCE_DIR}
                 -DFRAMES=${render-frames}
                 -DBACKEND=angle
                 -P ${CTBROWSER_MONOREPO_ROOT}/tools/check/check-render.cmake
                 WORKING_DIRECTORY "${CTBROWSER_EXAMPLE_BINARY_DIR}")
      endif()
    endif()
  endif()
endfunction()

set(example-sources)
add_subdirectory(corpus)
add_subdirectory(demos)

# --- Windows cross builds -------------------------------------------------
#
# `windows-dist` collects the exes, the tools, any runtime DLLs (SDL3 and
# SDL3_ttf when linked shared, the ANGLE/SwiftShader set when WebGL is on) and
# the data the examples load into examples-windows/, ready to copy to a Windows
# host and run.
#
# The pages and assets have to come too: an example calls run_app_file with a
# path relative to ctbrowser/, so the folder mirrors that layout and the exes
# work when run from its root.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_CROSSCOMPILING)
  # The monorepo root, not the project one: examples-windows/ is beside
  # ctbrowser/ and ctcompile/, and tools/remote-build.sh rsyncs it back from
  # exactly that path.
  set(dist-dir "${CTBROWSER_MONOREPO_ROOT}/examples-windows")
  set(dist-commands
      COMMAND ${CMAKE_COMMAND} -E make_directory "${dist-dir}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CTBROWSER_MONOREPO_ROOT}/LICENSE" "${dist-dir}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CTBROWSER_MONOREPO_ROOT}/NOTICE" "${dist-dir}")
  foreach(source ${example-sources})
    get_filename_component(example "${source}" NAME_WE)
    list(APPEND dist-commands
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$<TARGET_FILE:ctbrowser-example-${example}>" "${dist-dir}/${example}.exe")
    list(APPEND dist-targets ctbrowser-example-${example})
  endforeach()
  # The tools too (ctbrowse and ctdrive), since they open the pages by name.
  # Copied by TARGET_FILE_NAME so OUTPUT_NAME stays the single source of what
  # the binary is called.
  get_property(tool-targets GLOBAL PROPERTY CTBROWSER_DIST_TARGETS)
  foreach(tool IN LISTS tool-targets)
    list(APPEND dist-commands
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$<TARGET_FILE:${tool}>" "${dist-dir}/$<TARGET_FILE_NAME:${tool}>")
    list(APPEND dist-targets ${tool})
  endforeach()
  # A statically linked SDL3 has no DLL to ship, so each exe runs on its own.
  # With the shared SDL3 devel package, SDL3.dll (and SDL3_ttf.dll) are copied.
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
  # The whole ANGLE runtime, beside the exes. libEGL.dll and libGLESv2.dll are
  # what the exe imports; vulkan-1.dll, vk_swiftshader.dll and
  # vk_swiftshader_icd.json are what SwiftShader needs. ANGLE locates the ICD
  # relative to its own module directory, so they must sit together or the
  # deterministic driver cannot start.
  #
  # Windows has no rpath. On Linux the BUILD_RPATH in lib/Raster/CMakeLists.txt
  # is enough and nothing is copied; on Windows the only mechanism is the
  # directory the exe is in.
  #
  # Globbed, like the assets below: a named list goes stale when a release adds
  # a file, and the symptom is an exe that starts and reports no WebGL.
  if(CTBROWSER_WITH_ANGLE)
    file(GLOB angle-runtime CONFIGURE_DEPENDS
         "${CTBROWSER_MONOREPO_ROOT}/third-party/angle/windows-x64/*.dll"
         "${CTBROWSER_MONOREPO_ROOT}/third-party/angle/windows-x64/*.json")
    if(NOT angle-runtime)
      message(FATAL_ERROR
        "windows-dist: CTBROWSER_WITH_ANGLE is ON but third-party/angle/windows-x64 has no "
        "DLLs - run tools/fetch-angle.sh")
    endif()
    foreach(file ${angle-runtime})
      list(APPEND dist-commands
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${file}" "${dist-dir}")
    endforeach()
  endif()

  # Globbed, not named: a hand-written list goes stale, and the symptom
  # (Windows goldens differing from Linux on a cross build meant to be
  # byte-identical) looks like an engine divergence, not a missing file. A
  # directory is copied as a directory, which keeps each corpus's licence beside
  # its code.
  file(GLOB asset-entries CONFIGURE_DEPENDS LIST_DIRECTORIES true
       "${PROJECT_SOURCE_DIR}/examples/assets/*")
  list(APPEND dist-commands
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/pages" "${dist-dir}/examples/pages"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${dist-dir}/examples/assets")

  # vendor/ goes at the dist root: pages resolve the vendor bundle path relative
  # to the page, so the layout must mirror the repository, with vendor/ beside
  # examples/, not inside it.
  list(APPEND dist-commands
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/vendor" "${dist-dir}/vendor")
  foreach(entry ${asset-entries})
    get_filename_component(asset-name "${entry}" NAME)
    if(IS_DIRECTORY "${entry}")
      list(APPEND dist-commands
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${entry}" "${dist-dir}/examples/assets/${asset-name}")
    else()
      list(APPEND dist-commands
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${entry}" "${dist-dir}/examples/assets")
    endif()
  endforeach()
  # The OFL faces back serif/sans-serif/monospace. Without them every exe falls
  # back to the built-in bitmap font. The licences travel with the fonts.
  #
  # When the fonts are embedded in the exe (CTBROWSER_EMBED_FONTS, as the
  # windows preset does), shipping the directory as well would add 5.2 MB that
  # nothing reads, so only the licences are copied.
  if(CTBROWSER_EMBED_FONTS)
    # Globbed rather than named: the licences are whatever in fonts/ is not a
    # face, and a named list would miss the licence of a newly added font.
    file(GLOB font-licences "${PROJECT_SOURCE_DIR}/resources/fonts/*.txt"
                            "${PROJECT_SOURCE_DIR}/resources/fonts/*.md")
    list(APPEND dist-commands
      COMMAND ${CMAKE_COMMAND} -E make_directory "${dist-dir}/fonts"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${font-licences} "${dist-dir}/fonts")
  else()
    list(APPEND dist-commands
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${PROJECT_SOURCE_DIR}/resources/fonts" "${dist-dir}/fonts")
  endif()
  add_custom_target(windows-dist ${dist-commands}
    DEPENDS ${dist-targets}
    COMMENT "collecting Windows exes + pages into examples-windows/"
    VERBATIM)
endif()
