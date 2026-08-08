# Run an example headless and byte-compare its screenshot to a golden.
#
# The example ctests used to check only that the process EXITED 0, which is a
# smoke test rather than a render test: a page can come out with empty buttons,
# a missing caret, a <details> stuck open and every label glued to its control
# and still exit 0. Every one of those shipped.
#
# Rendered with CTBROWSER_FONTS=font8x8 on purpose. The bitmap font is part of
# the engine, so the golden does not move when FreeType, HarfBuzz or the
# vendored faces do - it pins LAYOUT and what is drawn, which is what regressed,
# and leaves glyph rasterisation to tests/fonts_basics.
#
# REGOLDEN=1 ctest ... regenerates instead of comparing.
#
# Required: -DEXE=, -DOUT=, -DGOLDEN=, -DWORKDIR=

set(ENV{SDL_VIDEODRIVER} offscreen)
set(ENV{SDL_AUDIODRIVER} dummy)
# 30 FRAMES SUITS A DRAWING, and not a game. A page that renders once needs one
# frame; phaser-invaders has to fire, travel and connect before there is
# anything to compare, and at 30 frames nothing has happened yet. Overridable
# per test with -DFRAMES= rather than by making the game unrealistic to suit the
# harness.
if(NOT DEFINED FRAMES)
  set(FRAMES 30)
endif()
set(ENV{CTBROWSER_TEST_FRAMES} ${FRAMES})
set(ENV{CTBROWSER_NETWORK} 0)
set(ENV{CTBROWSER_FONTS} font8x8)
# WHICH WebGL BACK END, when the caller asks for one. The software rasteriser is
# the default and is what the goldens in tests/golden/ hold; ANGLE gets its own
# goldens under tests/golden/angle/, because two rasterisers legitimately
# disagree at the edges of a triangle and pretending otherwise would mean
# comparing with a tolerance and losing the byte comparison entirely.
#
# MEASURED: ANGLE over SwiftShader is byte-identical between Linux and the
# Windows cross build, which is the property that lets it be goldened at all.
if(DEFINED BACKEND)
  set(ENV{CTBROWSER_WEBGL} ${BACKEND})
  # AND PIN THE DEVICE, not just the back end. "ANGLE" is not one rasteriser: it
  # is whatever Vulkan device the loader hands it, and tests/golden/angle/ was
  # made on SwiftShader. A machine with something else renders something else -
  # legitimately, and then the byte comparison fails for a reason that is not a
  # regression.
  #
  # MEASURED BOTH WAYS on 2026-08-08, and it bites on each platform from the
  # opposite direction. On the Windows exe, which reaches a real Intel Arc:
  # webgltriangle 54.19% of pixels differ, babylonscene 0.82%, p5webgl 5 pixels
  # of 93,600. With this set, all four are byte-identical. On a Linux
  # workstation the thief is Mesa's llvmpipe rather than a GPU - see
  # docs/platform.md, which also has the VK_ICD_FILENAMES half, because the
  # Vulkan LOADER picks the ICD before ANGLE's preference applies.
  #
  # It has never failed on the devbox or in the old CI for the least reassuring
  # reason available: neither had any other Vulkan device to lose to.
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
