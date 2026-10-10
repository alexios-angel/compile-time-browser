# --- A whole application, compiled to C++, linked, and run -----------------
#
# The other tests check whether one piece of the compiler works. This one
# checks the end result: a JavaScript application compiled to C++ by the real
# pipeline, linked into a native executable, installing its compiled bodies at
# startup with no hand-written table, and printing the same bytes as the same
# application interpreted.
#
# Two executables from one driver. Application.cpp is compiled twice: once
# alone, once with the generated bodies and the generated entry table. The two
# builds differ in exactly those two generated objects and a preprocessor
# symbol, which is what makes the comparison meaningful.
#
# The counters are the assertion, not the output. Both builds run the same
# program, so an application that silently interpreted everything would print
# identical bytes and exit 0. Runtime/Launcher/application.test asserts
# instead that the compiled build never crossed C++ -> VM, VM -> AOT or
# AOT -> VM: the interpreter did not run at all.
if(TARGET ctjs-translate AND TARGET ctjs-opt AND MLIR_TRANSLATE_EXE)
  set(_app_js "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Launcher/application.js")

  # The driver embeds the same file the pipeline compiles, for the reason given
  # beside differential.js in CoreAndBoxed.cmake.
  ctcompile_embed_js(launcher.js.inc "${_app_js}")
  set(_app_inc "${CMAKE_CURRENT_BINARY_DIR}/launcher.js.inc")

  # The whole file, with nothing renamed. Other uses of compile-js-to-cpp.cmake
  # pass -DENTRIES= to rename functions to fixed symbols, because a driver must
  # declare what it calls. An application declares nothing: the symbols keep
  # the names the backend gave them, which carry each body's function index to
  # the installer.
  ctcompile_compile_js(launcher.generated.cpp "${_app_js}")
  set(_app_cpp "${CMAKE_CURRENT_BINARY_DIR}/launcher.generated.cpp")

  # The table that installs them, read out of the emitted C++ rather than
  # written down: the symbols in the object file are the definitive list of
  # what this build can install.
  ctcompile_aot_entry_table(launcher.entries.cpp launcher.generated.cpp ctc_launcher_entries
                            launcher.js)
  set(_app_table "${CMAKE_CURRENT_BINARY_DIR}/launcher.entries.cpp")
  set_source_files_properties("${_app_cpp}" PROPERTIES
    COMPILE_OPTIONS "${CTCOMPILE_GENERATED_WARNINGS}")

  # The interpreter-only build, from the same source file. Nothing generated is
  # linked into it, so it can only interpret; that is what gives the compiled
  # build's counters their meaning.
  add_executable(ctcompile-test-launcher-vm "${_app_inc}")
  target_include_directories(ctcompile-test-launcher-vm PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(ctcompile-test-launcher-vm PRIVATE ctbrowser::script)
  ctcompile_target(ctcompile-test-launcher-vm)

  add_executable(ctcompile-test-launcher-aot "${_app_cpp}" "${_app_table}" "${_app_inc}")
  target_include_directories(ctcompile-test-launcher-aot PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  # The table's name is a build parameter, not a fixed symbol, because an
  # application with several compiled scripts has one table each and they
  # cannot share a name.
  target_compile_definitions(ctcompile-test-launcher-aot PRIVATE
    CTCOMPILE_LAUNCHER_AOT=1 CTCOMPILE_LAUNCHER_TABLE=ctc_launcher_entries)
  target_link_libraries(ctcompile-test-launcher-aot PRIVATE ctbrowser::script)
  ctcompile_target(ctcompile-test-launcher-aot)

  # Runtime/Launcher/application.test runs both builds, compares them and
  # asserts the counters.
endif()

# --- A real page from ctbrowser/examples, compiled ahead of time ------------
#
# The block above runs a JavaScript program against a bare script::context.
# This one is `examples/pages/invaders.html` (a game with a canvas, a sprite
# sheet, event listeners and requestAnimationFrame) running on the whole engine
# with all four of its functions compiled ahead of time.
#
# It is the page rather than a corpus library because its whole script lowers:
# ctjs-translate imports four functions from it and ctjs-opt refuses none, so
# strict AOT-only is reachable on a real page today. Nothing else in
# examples/pages is close.
if(TARGET ctjs-translate AND TARGET ctjs-opt AND MLIR_TRANSLATE_EXE)
  set(_page_html "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/examples/pages/invaders.html")

  # The script as the browser will assemble it, including the trailing newline;
  # the header of extract-inline-script.cmake explains why that newline
  # matters and what checks the extraction.
  set(_page_js "${CMAKE_CURRENT_BINARY_DIR}/invaders.js")
  add_custom_command(
    OUTPUT "${_page_js}"
    COMMAND "${CMAKE_COMMAND}" -DPAGE=${_page_html} -DOUTPUT=${_page_js}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Support/extract-inline-script.cmake"
    DEPENDS "${_page_html}" "${CMAKE_CURRENT_SOURCE_DIR}/Support/extract-inline-script.cmake"
    COMMENT "Extracting invaders.html's script"
    VERBATIM)

  ctcompile_compile_js(invaders.generated.cpp "${_page_js}")
  set(_page_cpp "${CMAKE_CURRENT_BINARY_DIR}/invaders.generated.cpp")
  ctcompile_aot_entry_table(invaders.entries.cpp invaders.generated.cpp ctc_invaders_entries
                            invaders.html)
  set(_page_table "${CMAKE_CURRENT_BINARY_DIR}/invaders.entries.cpp")

  set_source_files_properties("${_page_cpp}" PROPERTIES
    COMPILE_OPTIONS "${CTCOMPILE_GENERATED_WARNINGS}")

  # The asset root is ctbrowser/, because the page names its sprite sheet
  # `examples/assets/sprites.bmp`, a path relative to that directory, which is
  # where ctbrowse is run from.
  set(_page_defs
    CTCOMPILE_LAUNCHER_PAGE="${_page_html}"
    CTCOMPILE_LAUNCHER_ASSET_ROOT="${CTBROWSER_MONOREPO_ROOT}/ctbrowser")

  # Both arms take <canvas-output> through ctbrowser/tools/support/options.hpp.
  add_executable(ctcompile-test-launcher-page-vm)
  target_compile_definitions(ctcompile-test-launcher-page-vm PRIVATE ${_page_defs})
  target_link_libraries(ctcompile-test-launcher-page-vm PRIVATE ctbrowser::ctbrowser
    ctbrowser-options)
  ctcompile_target(ctcompile-test-launcher-page-vm)

  add_executable(ctcompile-test-launcher-page-aot "${_page_cpp}" "${_page_table}")
  target_compile_definitions(ctcompile-test-launcher-page-aot PRIVATE ${_page_defs}
    CTCOMPILE_LAUNCHER_AOT=1 CTCOMPILE_LAUNCHER_TABLE=ctc_invaders_entries)
  target_link_libraries(ctcompile-test-launcher-page-aot PRIVATE ctbrowser::ctbrowser
    ctbrowser-options)
  ctcompile_target(ctcompile-test-launcher-page-aot)

  # Runtime/Launcher/page.test runs both builds, compares the canvases and
  # asserts the counters.
endif()

# ---------------------------------------------------------------------------
# --- The JavaScript-global to C++/Boost map ---------------------------------
#
# include/ctcompile/StdLib/StdLibMap.def is an X-macro table mapping JavaScript
# globals to C++/Boost equivalents; the test walks it directly. It needs no
# MLIR, so it also runs when CTCOMPILE_ENABLE_MLIR=OFF, where most of this
# file does not.

# Boost.JSON and Boost.Regex, which the engine does not already ask for:
# ctbrowser/cmake/dependencies.cmake requests only url and program_options,
# and both are compiled libraries rather than header-only. They are needed
# here because the divergences this test checks are divergences of Boost, and
# a refusal must be checked by running the thing being refused.
find_package(Boost CONFIG QUIET COMPONENTS json regex)

if(TARGET Boost::json AND TARGET Boost::regex)
  ctcompile_unit_test(stdlib_map LIBS ctbrowser::script Boost::json Boost::regex)
else()
  # Warn instead of skipping silently: a suite that quietly drops a test on a
  # machine missing a dependency would pass while measuring nothing.
  message(WARNING
    "ctcompile: the Phase 61 standard-library map test is NOT registered. "
    "Boost_json_FOUND=${Boost_json_FOUND} Boost_regex_FOUND=${Boost_regex_FOUND} - "
    "install Boost (brew install boost) and configure again.")
endif()

# --- The ctnative type lattice ----------------------------------------------
#
# It is an executable rather than a lit test. test/CTNative/*.mlir already
# covers what lit is for: that the ODS parses and prints what it claims. This
# asks something that cannot be written as text: whether `meet` is a lattice
# join. The associativity sweep alone is 9261 triples, and the divergence check
# has to compile and run JavaScript in the real interpreter to find out what
# `"\u{1F600}".length` answers, because a number written down here could go
# stale (as GC/Roots.cpp describes).
#
# It is the only test that links both CTNativeDialect (the lattice) and
# ctbrowser::script (the interpreter that judges it): every native result is
# checked against what the interpreter computes.
if(CTCOMPILE_ENABLE_MLIR)
  # MLIRAsmParser is required, and leaving it out fails confusingly. The meet
  # table spells its types the way a .mlir file does and calls mlir::parseType,
  # which lives there and not in MLIRIR: without it everything compiles and the
  # link fails on a mangled name that says nothing about parsing.
  ctcompile_unit_test(ctnative-lattice
    LIBS CTNativeDialect MLIRIR MLIRAsmParser ctbrowser::script)
endif()
