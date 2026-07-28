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
set(ENV{CTBROWSER_TEST_FRAMES} 30)
set(ENV{CTBROWSER_NETWORK} 0)
set(ENV{CTBROWSER_FONTS} font8x8)
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
