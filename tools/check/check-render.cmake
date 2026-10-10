# Run an example headless and byte-compare its screenshot to a golden.
#
# A render test, not a smoke test: exiting 0 says nothing about empty buttons,
# a missing caret, a <details> stuck open or labels glued to their controls, so
# the screenshot is compared byte for byte.
#
# Rendered with CTBROWSER_FONTS=font8x8 on purpose. The bitmap font is part of
# the engine, so the golden does not move when FreeType, HarfBuzz or the
# vendored faces do - it pins layout and what is drawn, which is what regressed,
# and leaves glyph rasterisation to ctbrowser/unittests/unit/fonts_basics.
#
# REGOLDEN=1 ctest ... regenerates instead of comparing.
#
# Required: -DEXE=, -DOUT=, -DGOLDEN=, -DWORKDIR=

set(ENV{SDL_VIDEODRIVER} offscreen)
set(ENV{SDL_AUDIODRIVER} dummy)
# 30 frames suits a static page, not a game. A page that renders once needs one
# frame; phaser-invaders has to fire, travel and connect before there is
# anything to compare, and at 30 frames nothing has happened yet. Override it
# per test with -DFRAMES= rather than making the game unrealistic to suit the
# harness.
if(NOT DEFINED FRAMES)
  set(FRAMES 30)
endif()
set(ENV{CTBROWSER_TEST_FRAMES} ${FRAMES})
set(ENV{CTBROWSER_NETWORK} 0)
set(ENV{CTBROWSER_FONTS} font8x8)
# Which golden set. Without BACKEND the goldens in ctbrowser/test/golden/ are
# used; with it, those under ctbrowser/test/golden/angle/, which were made with
# ANGLE over SwiftShader. Two rasterisers legitimately disagree at triangle
# edges, and a tolerance would give up the byte comparison. ANGLE over
# SwiftShader is byte-identical between Linux and the Windows cross build,
# which is what makes it possible to golden at all.
if(DEFINED BACKEND)
  # The engine only has ANGLE, so BACKEND selects the golden directory and pins
  # the device. ANGLE renders on whatever Vulkan device the loader hands it, and
  # ctbrowser/test/golden/angle/ was made on SwiftShader. Without this pin a real
  # GPU or Mesa's llvmpipe renders WebGL scenes differently (measured: up to 54%
  # of pixels on an Intel Arc), and the byte comparison fails for a reason that
  # is not a regression. ctbrowser/docs/platform.md covers the VK_ICD_FILENAMES
  # half: the Vulkan loader picks the ICD before ANGLE's preference applies.
  set(ENV{CTBROWSER_GL_DRIVER} deterministic)
endif()
set(ENV{CTBROWSER_SCREENSHOT} "${OUT}")

file(REMOVE "${OUT}")
execute_process(COMMAND "${EXE}" WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE ran)
if(NOT ran EQUAL 0)
  message(FATAL_ERROR "${EXE} exited ${ran}")
endif()
if(NOT EXISTS "${OUT}")
  message(FATAL_ERROR "${EXE} wrote no screenshot to ${OUT}")
endif()

if(DEFINED ENV{REGOLDEN} AND NOT "$ENV{REGOLDEN}" STREQUAL "0")
  configure_file("${OUT}" "${GOLDEN}" COPYONLY)
  message(STATUS "regenerated ${GOLDEN}")
  return()
endif()

if(NOT EXISTS "${GOLDEN}")
  message(FATAL_ERROR "no golden at ${GOLDEN} - run with REGOLDEN=1 to create it")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}" "${GOLDEN}"
                RESULT_VARIABLE differs)
if(NOT differs EQUAL 0)
  message(FATAL_ERROR
    "${OUT} does not match ${GOLDEN}.\n"
    "If the change is intended: REGOLDEN=1 ctest --preset default -R <this test>")
endif()
