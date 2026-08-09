# the engine examples. Ordinary programs: no NTTP, no page<>, no compile-time anything.
#
# Registration is a glob - dropping a .cpp in here is the whole process, the way
# the previous engine's examples worked. Each one links ONE target and imports ONE module; that
# claim is enforced by tests/lint/api_surface, not just asserted here.

if(NOT CTBROWSER_BUILD_EXAMPLES)
  return()
endif()

# GLOB_RECURSE since the examples were foldered on 2026-08-09 into demos/ (the
# engine showing what it draws), corpus/ (somebody else's library, each with a
# golden) and cli/ (the two that take arguments and print).
#
# Nothing downstream had to change: every use below derives from NAME_WE, so the
# target names, the OUTPUT_NAMEs, the golden-examples and webgl-examples lists,
# the -mwindows exclusions and the dist loop all still key on the bare name.
file(GLOB_RECURSE example-sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
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
    # p5webgl is the one that goes all the way through: p5's RendererGL, its
    # shaders and its matrices, onto this engine's software rasteriser. It has a
    # golden for the same reason p5basic does - the page runs noLoop with fixed
    # rotations and no random, so the frame is the same everywhere, and nothing
    # short of the pixels proves the geometry actually landed.
    # phaserbasic is the second library's answer to p5basic, and it is a
    # different claim: somebody else's game framework, its own display list,
    # its own Graphics object and its own Canvas renderer, onto this engine.
    # The page draws only in create() with an empty update(), so the frame does
    # not depend on which tick the screenshot landed on - Phaser owns its game
    # loop, and a scene that animated would have no stable golden.
    # phaserinvaders is the one that is a GAME rather than a drawing: sprites
    # under arcade physics, a formation stepping on a timer, bullets, overlap
    # callbacks destroying objects mid-frame and a score that updates. Every
    # piece of that passes a probe on its own; the golden is the only thing that
    # says they work together. Deterministic by construction - no Math.random,
    # and the player sweeps on a frame-count script when no key is held, so the
    # same frame cap always produces the same frame.
    # babylonscene is the THIRD library and the hardest of the three: a 3D
    # engine with its own scene graph, camera and light model, a material system
    # and a shader assembled at run time - and every uniform delivered through a
    # BUFFER, which is what made it the corpus that found the WebGL 2 gaps. The
    # page renders a fixed number of times rather than running a render loop,
    # because Babylon compiles its effects asynchronously and a loop would make
    # the frame depend on timing.
    # babylonorbit is the INTERACTIVE one, and its golden is the frame before
    # anybody touches it: the scene does not move on its own, so the picture is
    # stable, and tests/corpus/babylon/babylon_interaction.cpp is what drives a drag through it.
    set(golden-examples widgets elements p5basic p5text p5transform p5shape p5pixels
        p5image webgltriangle p5webgl phaserbasic phaserinvaders babylonscene
        babylonorbit)
    # plutosvg_FOUND, not CTBROWSER_WITH_SVG: the latter is a compile
    # definition on a target and is not a variable this scope can test.
    if(plutosvg_FOUND)
      list(APPEND golden-examples svg)
    endif()
    # PNG arrives through SDL3_image, so the golden only means anything when it
    # is there. Same gate and same reasoning as the SVG one above: without the
    # dependency the page lays out identically and draws no image, and comparing
    # then fails for a reason that is not a regression.
    if(SDL3_image_FOUND OR CTBROWSER_WITH_IMAGE)
      list(APPEND golden-examples imageformats)
    endif()
    # THE PAGES WHOSE PIXELS COME OUT OF GL. There is no software rasteriser
    # any more (56bb66f), so `tests/golden/<page>.ppm` for one of these is a
    # picture with no producer: the plain test renders through ANGLE and
    # compares against what softgl drew, which can only ever fail. Measured on
    # p5webgl - 4 pixels of 93,600, all of them triangle edges.
    #
    # So these get ONE test, against tests/golden/angle/. The others keep the
    # plain one; phaserbasic and phaserinvaders match their existing goldens
    # and are deliberately left alone.
    set(webgl-examples webgltriangle p5webgl babylonscene babylonorbit)

    if(example IN_LIST golden-examples)
      # A GAME NEEDS LONGER THAN A DRAWING. 30 frames is one render; the
      # invaders page has to fire, travel and connect before its frame says
      # anything, so it gets a count that reaches a state worth comparing.
      set(render-frames 30)
      if(example STREQUAL phaserinvaders)
        set(render-frames 150)
      endif()
      # Babylon needs longer than a drawing for a different reason than the
      # game does: it compiles its effects through PROMISES, so the first
      # frames legitimately draw nothing while they settle. 30 frames is a
      # golden of pure background.
      if(example STREQUAL babylonscene OR example STREQUAL babylonorbit)
        set(render-frames 90)
      endif()
      if(NOT example IN_LIST webgl-examples)
        add_test(NAME render-${example} COMMAND ${CMAKE_COMMAND}
                 -DEXE=$<TARGET_FILE:ctbrowser-example-${example}>
                 -DOUT=${CMAKE_BINARY_DIR}/render-${example}.ppm
                 -DGOLDEN=${PROJECT_SOURCE_DIR}/tests/golden/${example}.ppm
                 -DWORKDIR=${PROJECT_SOURCE_DIR}
                 -DFRAMES=${render-frames}
                 -P ${PROJECT_SOURCE_DIR}/tools/check/check-render.cmake)
      endif()

      # AND THE SAME PAGE THROUGH ANGLE, where there is one. A SECOND GOLDEN
      # rather than a tolerance: two rasterisers differ at the edges of a
      # triangle - measured at 1.0% of pixels on webgltriangle - so comparing
      # them against each other would need a fudge factor, and a fudge factor is
      # a golden that stops catching things. Each back end holds still against
      # ITSELF instead, which is what a golden is for.
      #
      # SUPERSEDED: this used to read "only the pages the ANGLE path actually
      # renders, which today is one", because p5webgl, babylonscene and
      # babylonorbit were goldened once and the files were LOOKED AT and came
      # back BLANK WHITE.
      #
      # They are not blank any more. What was blanking them was never
      # drawElements: it was `deleteShader` destroying live buffers through a
      # kind-less delete_object, and a canvas resize destroying the whole
      # context along with the page's depth state. Both are fixed, and each of
      # these goldens is a file somebody has opened.
      if(CTBROWSER_WITH_ANGLE AND example IN_LIST webgl-examples)
        add_test(NAME render-angle-${example} COMMAND ${CMAKE_COMMAND}
                 -DEXE=$<TARGET_FILE:ctbrowser-example-${example}>
                 -DOUT=${CMAKE_BINARY_DIR}/render-angle-${example}.ppm
                 -DGOLDEN=${PROJECT_SOURCE_DIR}/tests/golden/angle/${example}.ppm
                 -DWORKDIR=${PROJECT_SOURCE_DIR}
                 -DFRAMES=${render-frames}
                 -DBACKEND=angle
                 -P ${PROJECT_SOURCE_DIR}/tools/check/check-render.cmake)
      endif()
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
  # ANGLE, ALL OF IT, BESIDE THE EXES. Five files and every one is load-bearing:
  # libEGL.dll and libGLESv2.dll are what the exe imports, and vulkan-1.dll,
  # vk_swiftshader.dll and vk_swiftshader_icd.json are what SwiftShader needs -
  # ANGLE locates the ICD RELATIVE TO ITS OWN MODULE DIRECTORY, so they have to
  # sit together or the deterministic driver cannot come up.
  #
  # Windows has no rpath. On Linux the BUILD_RPATH in src/CMakeLists.txt is the
  # whole answer and there is nothing to copy; here the only mechanism is the
  # directory the exe is in, so an rpath-only solution is a Linux-only solution
  # and this is the other half of it.
  #
  # GLOBBED for the same reason the assets below are - a named list of five
  # files goes stale the first time the release adds a sixth, and the symptom is
  # an exe that starts and reports no WebGL.
  if(CTBROWSER_WITH_ANGLE)
    file(GLOB angle-runtime CONFIGURE_DEPENDS
         "${PROJECT_SOURCE_DIR}/third_party/angle/windows-x64/*.dll"
         "${PROJECT_SOURCE_DIR}/third_party/angle/windows-x64/*.json")
    if(NOT angle-runtime)
      message(FATAL_ERROR
        "windows-dist: CTBROWSER_WITH_ANGLE is ON but third_party/angle/windows-x64 has no "
        "DLLs - run tools/fetch-angle.sh")
    endif()
    foreach(file ${angle-runtime})
      list(APPEND dist-commands
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${file}" "${dist-dir}")
    endforeach()
  endif()

  # GLOBBED, NOT NAMED, and that is this list's third lesson rather than a
  # preference.
  #
  # It was a hand-written list of files because assets/ used to hold the
  # previous engine's glTF models, which nothing here opened. Those are GONE,
  # so the reason is gone with them - and in the meantime the list went stale
  # twice in exactly the same way. phaser.js was added to the tree after the
  # list was written, so phaserbasic.exe and phaserinvaders.exe shipped and
  # rendered a page with no framework in it. Then babylon.js did the same, and
  # babylonscene.exe came up with `BABYLON.Engine is undefined`.
  #
  # THE FAILURE READS LIKE AN ENGINE DIVERGENCE, which is what makes it worth
  # this much comment: the Windows goldens differ from the Linux ones, on a
  # cross-build that is meant to be byte-identical, and nothing says the page
  # was missing its library.
  #
  # A directory is copied as a directory - which also carries each corpus's
  # licence beside its code, where it has to stay - and a loose file as a file.
  file(GLOB asset-entries CONFIGURE_DEPENDS LIST_DIRECTORIES true
       "${PROJECT_SOURCE_DIR}/examples/assets/*")
  list(APPEND dist-commands
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/pages" "${dist-dir}/examples/pages"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${dist-dir}/examples/assets")

  # AND vendor/, AT THE DIST ROOT, which is not a detail. The corpora moved out
  # of examples/assets/ on 2026-08-09 because they are test data first, and the
  # pages now ask for `../../vendor/<lib>/<lib>.js`. That resolves against the
  # PAGE, so the only layout in which it finds anything is one that mirrors the
  # repository: vendor/ beside examples/, not inside it.
  #
  # This is the third instalment of the lesson the comment above records. Both
  # earlier times the symptom was the same and reads like an engine bug rather
  # than a packaging one: the Windows goldens differ from the Linux ones on a
  # cross-build that is meant to be byte-identical, and nothing anywhere says
  # the page was served without its framework.
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
