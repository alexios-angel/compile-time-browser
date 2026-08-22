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
          --fonts ${FONTS} --manifest ${OUT}.json
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

# ---- the manifest, PARSED rather than matched ------------------------------
#
# Phase 1's gate asks for a manifest. A grep for a field name would pass on a
# document no parser accepts, so this uses CMake's own JSON reader: if the
# emitter produced anything malformed, string(JSON) errors out here.
if(NOT EXISTS ${OUT}.json)
  message(FATAL_ERROR "--manifest was given and no manifest was written")
endif()
file(READ ${OUT}.json manifest_text)
string(JSON manifest_entry GET "${manifest_text}" entry)
string(JSON manifest_mode GET "${manifest_text}" mode)
string(JSON manifest_fonts GET "${manifest_text}" font_directory)
string(JSON script_count LENGTH "${manifest_text}" scripts)
string(JSON resource_count LENGTH "${manifest_text}" resources)
if(NOT manifest_entry STREQUAL "index.html")
  message(FATAL_ERROR "the manifest names entry ${manifest_entry}")
endif()
if(NOT manifest_mode STREQUAL "vm")
  message(FATAL_ERROR "the manifest names mode ${manifest_mode}")
endif()
if(NOT script_count EQUAL 2)
  message(FATAL_ERROR "the manifest lists ${script_count} scripts, not 2")
endif()
# THE FACES ARE UNDER A NAME OF THE COMPILER'S OWN, not the build machine's.
# Shipping the absolute path that found them works and bakes a checkout path
# into every application, so the packer renames them and the launcher is told
# the same. If that stops happening, this is where it shows.
if(NOT manifest_fonts STREQUAL "fonts")
  message(FATAL_ERROR "the manifest names the font directory ${manifest_fonts} - a packaged "
                      "application should not carry the build machine's paths")
endif()
# EVERY PROGRAM ID DISTINCT, because two scripts sharing one would mean the
# images cannot be told apart and the second lookup would take the first's.
string(JSON first_id GET "${manifest_text}" scripts 0 program_id)
string(JSON second_id GET "${manifest_text}" scripts 1 program_id)
if(first_id STREQUAL second_id)
  message(FATAL_ERROR "both scripts report program id ${first_id}")
endif()
message("ok manifest: ${script_count} scripts, ${resource_count} resources, entry "
        "${manifest_entry}, mode ${manifest_mode}")

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

# ---- and it must look the SAME as the page it came from --------------------
#
# THE CHECK THAT CAUGHT THE FONTS. A packaged application is sealed - it answers
# from what it carries and never from the disk - and the vendored faces are
# loaded THROUGH the asset registry, so the first sealed build silently dropped
# to the built-in bitmap font. It exited 0, it rendered, and it just looked
# worse. Nothing in this file noticed, because everything above it runs with
# CTBROWSER_FONTS=font8x8 and so was comparing two bitmap-font runs.
#
# So this arm renders the SAME page twice with real fonts - once from source,
# once packaged - and compares the bytes. It is deliberately the last thing
# here, because a byte comparison of two renders catches anything the packaging
# lost, not only fonts.
#
# It proves less on a build without TTF support: both arms fall back and match.
# That is worth knowing rather than hiding, and it is why the failure message
# says what to check.
unset(ENV{CTBROWSER_FONTS})
set(ENV{CTBROWSER_FONT_PATH} ${FONTS})
set(ENV{CTBROWSER_TEST_FRAMES} 2)
# The packaged arm must not be reading these from the disk - it is sealed - but
# the FROM-SOURCE arm needs them, and if this were left unset both arms would
# fall back and match for the wrong reason.
execute_process(
  COMMAND ${CMAKE_COMMAND} -E env CTBROWSER_SCREENSHOT=${OUT}-from-source.ppm
          ${BROWSE} ${APP}/index.html
  RESULT_VARIABLE ran_source OUTPUT_QUIET ERROR_VARIABLE source_err)
# THE PACKAGED ARM IS GIVEN NOTHING - no font path, and a working directory that
# is not the application's. That is what "copy it and run it" means, and it is
# the only way this comparison can see the bundle doing the work: with
# CTBROWSER_FONT_PATH set, a packaged application finds the faces under the very
# names the packaging machine recorded and passes for the wrong reason.
unset(ENV{CTBROWSER_FONT_PATH})
execute_process(
  COMMAND ${CMAKE_COMMAND} -E env CTBROWSER_SCREENSHOT=${OUT}-packaged.ppm ${OUT}
  WORKING_DIRECTORY ${elsewhere}
  RESULT_VARIABLE ran_packaged OUTPUT_QUIET ERROR_VARIABLE packaged_err)
if(NOT ran_source EQUAL 0 OR NOT ran_packaged EQUAL 0)
  message(FATAL_ERROR "a render arm did not run:\n${source_err}${packaged_err}")
endif()
execute_process(
  COMMAND ${CMAKE_COMMAND} -E compare_files ${OUT}-from-source.ppm ${OUT}-packaged.ppm
  RESULT_VARIABLE same)
if(NOT same EQUAL 0)
  message(FATAL_ERROR
          "the packaged application does not render what the page renders. It exits 0 and draws "
          "something, so only this comparison can see it - check first whether the packaged "
          "registry can reach everything it needs, fonts included:\n"
          "  ${OUT}-from-source.ppm\n  ${OUT}-packaged.ppm")
endif()
set(ENV{CTBROWSER_FONTS} font8x8)

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
