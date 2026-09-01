# RUN A REAL PAGE TWICE - ONCE COMPILED, ONCE INTERPRETED - AND COMPARE WHAT IT
# DREW.
#
# check-launcher.cmake beside this does the same for a bare `script::context`.
# This one drives the whole engine: `examples/pages/invaders.html`, its canvas,
# its sprite sheet and its requestAnimationFrame loop, with the compiled bodies
# installed through `browser::set_script_prepared_hook`.
#
# THE PIXELS ARE THE CONTROL AND THE COUNTERS ARE THE ASSERTION, for the reason
# spelled out in check-launcher.cmake: both arms run the same game, so a page
# that installed nothing draws exactly the same picture.
#
# Required: -DVM= -DAOT= -DWORK=
cmake_minimum_required(VERSION 3.20)

foreach(required VM AOT WORK)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "check-launcher-page.cmake: -D${required}= is required")
  endif()
endforeach()

# THE PAGE HAS FOUR FUNCTIONS AND ALL FOUR COMPILE: the top level, `frame`, and
# the two anonymous keydown/keyup listeners.
set(EXPECTED_INSTALLED 4)

# NO REAL FONTS. `ctx.fillText("SCORE ...")` goes through the text stack, and
# two FreeType versions do not rasterise identically - so a comparison of two
# runs of THIS build is fine either way, and asking for the bitmap font makes
# the picture the same on any machine. It also means this needs no font files.
set(ENV{CTBROWSER_FONTS} font8x8)
set(ENV{CTBROWSER_NETWORK} 0)

function(run_arm which binary out_stderr)
  execute_process(
    COMMAND ${binary} "${WORK}/launcher-page-${which}.ppm"
    RESULT_VARIABLE code
    OUTPUT_QUIET
    ERROR_VARIABLE complaints)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "check-launcher-page.cmake: the ${which} arm exited ${code}\n${complaints}")
  endif()
  set(${out_stderr} "${complaints}" PARENT_SCOPE)
endfunction()

function(counter report name into)
  string(REPLACE "+" "\\+" pattern "${name}")
  if(NOT report MATCHES "transition \"${pattern}\" = ([0-9]+)")
    message(FATAL_ERROR
      "check-launcher-page.cmake: no counter named ${name} in this arm's report:\n${report}")
  endif()
  set(${into} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

run_arm(vm "${VM}" vm_err)
run_arm(aot "${AOT}" aot_err)

# ---- what the two arms drew --------------------------------------------------
execute_process(
  COMMAND ${CMAKE_COMMAND} -E compare_files
          "${WORK}/launcher-page-vm.ppm" "${WORK}/launcher-page-aot.ppm"
  RESULT_VARIABLE same)
if(NOT same EQUAL 0)
  message(FATAL_ERROR
    "check-launcher-page.cmake: the compiled page does not draw what the interpreted page draws. "
    "Both files are PPMs - open them.\n  ${WORK}/launcher-page-vm.ppm"
    "\n  ${WORK}/launcher-page-aot.ppm")
endif()

# A BLANK CANVAS WOULD ALSO MATCH. The page fills its background every frame, so
# a run in which the game drew nothing is one uniform colour - and two of those
# compare equal. Both numbers are asserted: the size, so this is a 320x240 PPM
# rather than a truncated write, and the colour count, so something was drawn on
# it. Three is the background, the sprites and the score text.
file(SIZE "${WORK}/launcher-page-aot.ppm" drawn_bytes)
if(drawn_bytes LESS 230400)
  message(FATAL_ERROR
    "check-launcher-page.cmake: the canvas is ${drawn_bytes} bytes, which is not a 320x240 PPM")
endif()
if(NOT aot_err MATCHES "colours = ([0-9]+)")
  message(FATAL_ERROR "check-launcher-page.cmake: the aot arm did not count its colours")
endif()
if(CMAKE_MATCH_1 LESS 3)
  message(FATAL_ERROR
    "check-launcher-page.cmake: the canvas has ${CMAKE_MATCH_1} colour(s) on it - the game did "
    "not draw, and two blank canvases compare equal")
endif()

# ---- and did the interpreter run? -------------------------------------------
if(NOT aot_err MATCHES "installed ([0-9]+) across ([0-9]+) script")
  message(FATAL_ERROR "check-launcher-page.cmake: the aot arm did not report an install:\n${aot_err}")
endif()
set(installed ${CMAKE_MATCH_1})
if(NOT installed EQUAL ${EXPECTED_INSTALLED})
  message(FATAL_ERROR
    "check-launcher-page.cmake: ${installed} bodies were installed, expected "
    "${EXPECTED_INSTALLED}. install_strict refuses a partial install, so this is a page whose "
    "function count moved.")
endif()

# THE INTERPRETER MUST NEVER HAVE RUN. Unlike the bare-context arm this page is
# driven ENTIRELY from C++ - the top level once from load_html, then `frame`
# once per requestAnimationFrame - so C++ -> AOT is many and AOT -> AOT is zero:
# invaders' four functions never call one another, only natives. What must be
# zero is every path through the interpreter.
foreach(never "C++ -> VM" "VM -> AOT" "AOT -> VM")
  counter("${aot_err}" "${never}" seen)
  if(NOT seen EQUAL 0)
    message(FATAL_ERROR
      "check-launcher-page.cmake: the aot arm crossed ${never} ${seen} time(s), so the "
      "interpreter ran. This page is not AOT-only.\n${aot_err}")
  endif()
endforeach()

# AND IT MUST HAVE BEEN ENTERED ONCE PER FRAME AT LEAST. 20 ticks plus the
# top level is 21 crossings from C++ into compiled code; asserting `> 20` rather
# than a pin because a rAF callback that the engine also invoked for another
# reason would be a change in the engine, not a fall back to the interpreter.
counter("${aot_err}" "C++ -> AOT" entered)
if(entered LESS 21)
  message(FATAL_ERROR
    "check-launcher-page.cmake: C++ entered a compiled body only ${entered} time(s) - the page "
    "runs 20 animation frames plus its top level, so this is fewer than one frame each")
endif()

# AND COMPILED CODE MUST HAVE CALLED BACK INTO THE ENGINE. This is the mixed
# stack the plan's Definition of Done asks for by name - `ctx.drawImage`,
# `ctx.fillRect`, `Math.floor` and `requestAnimationFrame` are C++ natives, and
# a compiled body reaching one of them is an AOT -> C++ crossing. Zero here
# would mean the page drew nothing from compiled code.
counter("${aot_err}" "AOT -> C++" into_cxx)
if(into_cxx LESS 1)
  message(FATAL_ERROR
    "check-launcher-page.cmake: no compiled body called a native - the game cannot have drawn "
    "from compiled code:\n${aot_err}")
endif()

# ---- THE BLINDED ARM --------------------------------------------------------
foreach(never "C++ -> AOT" "VM -> AOT" "AOT -> AOT" "AOT -> VM")
  counter("${vm_err}" "${never}" seen)
  if(NOT seen EQUAL 0)
    message(FATAL_ERROR
      "check-launcher-page.cmake: the arm with NO generated code crossed ${never} ${seen} "
      "time(s) - the counters are not measuring compiled dispatch:\n${vm_err}")
  endif()
endforeach()
counter("${vm_err}" "C++ -> VM" ran_interpreted)
if(ran_interpreted LESS 21)
  message(FATAL_ERROR
    "check-launcher-page.cmake: the interpreted arm entered the interpreter only "
    "${ran_interpreted} time(s), so it did not run the frames either and the comparison above "
    "is between two pages that did nothing:\n${vm_err}")
endif()

counter("${aot_err}" "AOT -> AOT" chained)
message("ok ctcompile_launcher_page: ${installed} bodies compiled, ${entered} entries from C++, "
        "${chained} AOT -> AOT, ${into_cxx} AOT -> C++, the canvas identical, the interpreter "
        "never ran")
