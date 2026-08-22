# Package an application, then RUN it.
#
# This is the one test that exercises the whole feature the way a user does:
# a directory in, an executable out, and that executable started on a machine
# that is told nothing about where it came from. Everything else in this
# directory tests a piece.
#
# WHAT A PASS ACTUALLY PROVES, which is more than "it exited 0". `run_bundle`
# sets require_script_images, so the launcher REFUSES TO START if any of the
# page's scripts had to be compiled from source. A packaged application that
# quietly parsed its own JavaScript would exit 1 here, and that is the failure
# this whole project treats as the worst one because nothing else reports it.
#
# THE NEGATIVE ARM IS THE SECOND HALF. A test that only ever runs the good copy
# cannot tell a launcher that finds its bundle from one that ignores it and
# falls back to something else. So the same executable is truncated by four
# bytes - killing the trailer, not the ELF - and must then refuse to run.
#
# Required: -DCTCOMPILE= -DAPP= -DLAUNCHER= -DOUT=

set(ENV{SDL_VIDEODRIVER} offscreen)
set(ENV{SDL_AUDIODRIVER} dummy)
set(ENV{CTBROWSER_TEST_FRAMES} 2)
set(ENV{CTBROWSER_NETWORK} 0)
# The bitmap font, so this needs no font files and no FreeType to run a page.
set(ENV{CTBROWSER_FONTS} font8x8)

file(REMOVE ${OUT})

execute_process(
  COMMAND ${CTCOMPILE} ${APP} --entry index.html --launcher ${LAUNCHER} -o ${OUT} --verbose
  RESULT_VARIABLE packaged
  OUTPUT_VARIABLE packaging_out
  ERROR_VARIABLE packaging_err)
message("${packaging_out}${packaging_err}")
if(NOT packaged EQUAL 0)
  message(FATAL_ERROR "ctcompile refused to package the fixture application (exit ${packaged})")
endif()
if(NOT EXISTS ${OUT})
  message(FATAL_ERROR "ctcompile reported success and wrote no executable")
endif()

# IT SAID WHAT IT DID, and the numbers are checked rather than the phrasing:
# two scripts is the count that matters, because a packager that quietly packed
# one would produce an application that starts and is refused for a reason
# pointing at the launcher.
if(NOT packaging_err MATCHES "2 scripts compiled")
  message(FATAL_ERROR "expected two compiled scripts, got: ${packaging_err}")
endif()
# AND THE RESOURCES IT WAS NEVER TOLD ABOUT. lib.js and app.css are named by the
# document, not by the command line; a run that packs neither still produces a
# working executable today, and a broken one the moment the page needs them.
if(NOT packaging_err MATCHES "asset  lib\\.js")
  message(FATAL_ERROR "the script's own file was not packaged: ${packaging_err}")
endif()
if(NOT packaging_err MATCHES "asset  app\\.css")
  message(FATAL_ERROR "the stylesheet was not packaged: ${packaging_err}")
endif()
# AND THE ONE A SCRIPT ASKED FOR AFTER THE PAGE WAS UP. fetch queues its request
# and it is drained from a tick, so this name exists only if the packager let the
# page RUN before asking what it wanted. It is the difference between packaging
# a document and packaging an application.
if(NOT packaging_err MATCHES "asset  late\\.json")
  message(FATAL_ERROR
          "the resource a script fetched was not packaged - the probe did not let the page "
          "run:\n${packaging_err}")
endif()

file(SIZE ${OUT} packaged_size)
file(SIZE ${LAUNCHER} launcher_size)
if(NOT packaged_size GREATER launcher_size)
  message(FATAL_ERROR "the packaged executable is no larger than the launcher it was made from")
endif()

# ---- run it, from somewhere that is not the application directory ----------
#
# The working directory is deliberately NOT the fixture: an application that
# only starts next to its own source files has not packaged anything.
get_filename_component(elsewhere ${OUT} DIRECTORY)
execute_process(
  COMMAND ${OUT}
  WORKING_DIRECTORY ${elsewhere}
  RESULT_VARIABLE ran
  OUTPUT_VARIABLE run_out
  ERROR_VARIABLE run_err)
if(NOT ran EQUAL 0)
  message(FATAL_ERROR "the packaged application did not run (exit ${ran}):\n${run_out}${run_err}")
endif()

# ---- and the same launcher with nothing appended to it --------------------
#
# THE BLINDED ARM, and it is the same binary. The executable that just ran is a
# byte-for-byte copy of this file with a bundle stuck on the end, so if this one
# also starts a page then the bundle was never what made the difference and the
# positive arm above proves nothing about packaging.
execute_process(
  COMMAND ${LAUNCHER}
  WORKING_DIRECTORY ${elsewhere}
  RESULT_VARIABLE ran_bare
  OUTPUT_VARIABLE bare_out
  ERROR_VARIABLE bare_err)
if(ran_bare EQUAL 0)
  message(FATAL_ERROR
          "the launcher ran with no application appended to it - so the packaged copy above "
          "was not running its bundle:\n${bare_out}${bare_err}")
endif()

# AND HANDED SOMETHING THAT IS NOT A BUNDLE. A launcher that reads a table of
# offsets out of whatever file it was pointed at is a launcher that has to
# refuse this one, rather than trusting it and reading somewhere.
execute_process(
  COMMAND ${LAUNCHER} ${APP}/index.html
  WORKING_DIRECTORY ${elsewhere}
  RESULT_VARIABLE ran_html
  OUTPUT_VARIABLE html_out
  ERROR_VARIABLE html_err)
if(ran_html EQUAL 0)
  message(FATAL_ERROR "the launcher accepted an HTML file as an application bundle")
endif()
if(NOT html_err MATCHES "not a ctbrowser application bundle")
  message(FATAL_ERROR "it refused, but not for the right reason:\n${html_out}${html_err}")
endif()

# ---- what the compiler must refuse to package -----------------------------
#
# A PAGE OF MODULE SCRIPTS. Nothing here can compile one ahead of time, so
# packaging it would produce an application whose only property is that it is
# exactly as slow as it was before - and every count in the packager would read
# a truthful zero while it happened. That is the silent failure this whole tool
# exists to avoid, so it is a refusal and it has a test.
get_filename_component(app_parent ${APP} DIRECTORY)
execute_process(
  COMMAND ${CTCOMPILE} ${app_parent}/app-module --entry index.html --launcher ${LAUNCHER}
          -o ${OUT}-module
  RESULT_VARIABLE packaged_module
  OUTPUT_VARIABLE module_out
  ERROR_VARIABLE module_err)
if(packaged_module EQUAL 0)
  message(FATAL_ERROR "ctcompile packaged a page of module scripts as though it could run them")
endif()
if(NOT module_err MATCHES "module script")
  message(FATAL_ERROR "it refused, but not for the right reason:\n${module_out}${module_err}")
endif()

# ---- and what the launcher must refuse to run -----------------------------
#
# Two bundles that ctcompile will not produce and a future packager might.
# ctcompile-test-app_bundle wrote them; the point of running them HERE is that
# the guard is in run_app, behind a window, and nothing else in the suite gets
# that far.
foreach(refusal no-images module-page)
  if(NOT EXISTS ${REFUSALS}/${refusal}.ctapp)
    message(FATAL_ERROR
            "${REFUSALS}/${refusal}.ctapp was not written - ctcompile_app_bundle is supposed to "
            "emit it, and a negative case that silently stops existing is worse than none")
  endif()
  execute_process(
    COMMAND ${LAUNCHER} ${REFUSALS}/${refusal}.ctapp
    WORKING_DIRECTORY ${elsewhere}
    RESULT_VARIABLE ran_refusal
    OUTPUT_VARIABLE refusal_out
    ERROR_VARIABLE refusal_err)
  if(ran_refusal EQUAL 0)
    message(FATAL_ERROR
            "the launcher ran ${refusal}.ctapp - an application that parses its own JavaScript "
            "at every start and reports nothing:\n${refusal_out}${refusal_err}")
  endif()
endforeach()

message("ok ctcompile_package (packaged ${packaged_size} bytes, ran it, and refused four things)")
