# --- PHASE 18: an application, generated, linked, and RUN ---------------------
#
# ONE BLOCK, APPENDED, so that the merge with the other tracks working in this
# file is a concatenation. Everything it needs is its own file except the two
# scripts it reuses.
#
# What every test above this asks is whether a piece of the compiler works.
# What this asks is whether the thing exists: a JavaScript application, compiled
# to C++ by the real pipeline, linked into a native executable, installing its
# own compiled bodies at startup with no hand-written table anywhere - and
# printing the same bytes as the same application interpreted.
#
# TWO EXECUTABLES FROM ONE DRIVER. Application.cpp is compiled twice: once
# alone, once with the generated bodies and the generated entry table. That is
# what makes the comparison mean something - the arms differ in exactly the two
# generated objects and a preprocessor symbol.
#
# THE COUNTERS ARE THE ASSERTION, not the transcripts. Both arms run the same
# program, so an application that silently interpreted everything prints
# identical bytes and exits 0. check-application.cmake asserts instead that the
# compiled arm never crossed C++ -> VM, VM -> AOT or AOT -> VM: the interpreter
# did not run at all.
if(TARGET ctjs-translate AND TARGET ctjs-opt AND MLIR_TRANSLATE_EXE)
  set(_app_js "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Launcher/application.js")

  # The driver reads the same file the pipeline compiles, for the reason spelled
  # out over differential.js above.
  set(_app_inc "${CMAKE_CURRENT_BINARY_DIR}/launcher.js.inc")
  add_custom_command(
    OUTPUT "${_app_inc}"
    COMMAND "${CMAKE_COMMAND}" -DSOURCE=${_app_js} -DOUTPUT=${_app_inc}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Support/embed-js.cmake"
    DEPENDS "${_app_js}" "${CMAKE_CURRENT_SOURCE_DIR}/Support/embed-js.cmake"
    COMMENT "Embedding launcher.js for the launcher driver"
    VERBATIM)

  # THE WHOLE FILE, WITH NOTHING RENAMED. Every other use of
  # compile-js-to-cpp.cmake passes -DENTRIES= a list of functions to rename to
  # fixed symbols, because a driver has to DECLARE what it calls. An application
  # declares nothing: the symbols keep the names the backend gave them, which is
  # what carries each body's function index to the installer.
  set(_app_cpp "${CMAKE_CURRENT_BINARY_DIR}/launcher.generated.cpp")
  add_custom_command(
    OUTPUT "${_app_cpp}"
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
            -DOPT=$<TARGET_FILE:ctjs-opt>
            -DMLIR_TRANSLATE=${MLIR_TRANSLATE_EXE}
            -DSOURCE=${_app_js}
            "-DENTRIES="
            -DOUTPUT=${_app_cpp}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Support/compile-js-to-cpp.cmake"
    DEPENDS "${_app_js}" ctjs-translate ctjs-opt
            "${CMAKE_CURRENT_SOURCE_DIR}/Support/compile-js-to-cpp.cmake"
    COMMENT "Compiling launcher.js through the EmitC backend"
    VERBATIM)

  # AND THE TABLE THAT INSTALLS THEM, read out of the emitted C++ rather than
  # written down: the symbols that exist in the object file are the definitive
  # list of what this build can install.
  set(_app_table "${CMAKE_CURRENT_BINARY_DIR}/launcher.entries.cpp")
  add_custom_command(
    OUTPUT "${_app_table}"
    COMMAND "${CMAKE_COMMAND}"
            -DGENERATED=${_app_cpp}
            -DOUTPUT=${_app_table}
            -DTABLE=ctc_launcher_entries
            -DSOURCE_NAME=launcher.js
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Support/aot-entry-table.cmake"
    DEPENDS "${_app_cpp}" "${CMAKE_CURRENT_SOURCE_DIR}/Support/aot-entry-table.cmake"
    COMMENT "Writing the entry table for launcher.js"
    VERBATIM)

  # Set above beside the first generated file; repeated here only if this block
  # is ever separated from it, because generated code is not this project's code
  # and -Werror on it makes an unused parameter a build failure.
  if(NOT DEFINED CTCOMPILE_GENERATED_WARNINGS)
    set(CTCOMPILE_GENERATED_WARNINGS
        "-Wno-unused-parameter;-Wno-unused-variable;-Wno-unused-but-set-variable")
  endif()
  set_source_files_properties("${_app_cpp}" PROPERTIES
    COMPILE_OPTIONS "${CTCOMPILE_GENERATED_WARNINGS}")

  # THE BLINDED ARM, and it is the same source file. Nothing generated is linked
  # into it, so it can only interpret - which is what makes the counters the
  # compiled arm reports mean anything at all.
  add_executable(ctcompile-test-launcher-vm Runtime/Launcher/Application.cpp "${_app_inc}")
  target_include_directories(ctcompile-test-launcher-vm PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(ctcompile-test-launcher-vm PRIVATE ctbrowser::script)
  ctcompile_target(ctcompile-test-launcher-vm)

  add_executable(ctcompile-test-launcher-aot
    Runtime/Launcher/Application.cpp "${_app_cpp}" "${_app_table}" "${_app_inc}")
  target_include_directories(ctcompile-test-launcher-aot PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  # THE TABLE'S NAME IS A BUILD PARAMETER, not a fixed symbol, because an
  # application with several compiled scripts has one table each and they cannot
  # all be called the same thing.
  target_compile_definitions(ctcompile-test-launcher-aot PRIVATE
    CTCOMPILE_LAUNCHER_AOT=1 CTCOMPILE_LAUNCHER_TABLE=ctc_launcher_entries)
  target_link_libraries(ctcompile-test-launcher-aot PRIVATE ctbrowser::script)
  ctcompile_target(ctcompile-test-launcher-aot)

  add_test(NAME ctcompile_launcher
           COMMAND ${CMAKE_COMMAND}
                   -DVM=$<TARGET_FILE:ctcompile-test-launcher-vm>
                   -DAOT=$<TARGET_FILE:ctcompile-test-launcher-aot>
                   -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Launcher/check-application.cmake)
endif()

# --- PHASE 18, WIDENED: a real page from ctbrowser/examples ------------------
#
# The block above is a JavaScript program against a bare script::context. This
# one is `examples/pages/invaders.html` - a game with a canvas, a sprite sheet,
# event listeners and requestAnimationFrame - running the whole engine with all
# FOUR of its functions compiled ahead of time.
#
# It is the page rather than a corpus library because its whole script lowers:
# ctjs-translate imports four functions from it and ctjs-opt refuses none, so
# strict AOT-only is reachable on a real page today. Nothing else in
# examples/pages is close.
if(TARGET ctjs-translate AND TARGET ctjs-opt AND MLIR_TRANSLATE_EXE)
  set(_page_html "${CTBROWSER_MONOREPO_ROOT}/ctbrowser/examples/pages/invaders.html")

  # THE SCRIPT AS THE BROWSER WILL ASSEMBLE IT, trailing newline and all - see
  # the header of extract-inline-script.cmake for why that newline is not
  # cosmetic and what checks the extraction.
  set(_page_js "${CMAKE_CURRENT_BINARY_DIR}/invaders.js")
  add_custom_command(
    OUTPUT "${_page_js}"
    COMMAND "${CMAKE_COMMAND}" -DPAGE=${_page_html} -DOUTPUT=${_page_js}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Support/extract-inline-script.cmake"
    DEPENDS "${_page_html}" "${CMAKE_CURRENT_SOURCE_DIR}/Support/extract-inline-script.cmake"
    COMMENT "Extracting invaders.html's script"
    VERBATIM)

  set(_page_cpp "${CMAKE_CURRENT_BINARY_DIR}/invaders.generated.cpp")
  add_custom_command(
    OUTPUT "${_page_cpp}"
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSLATE=$<TARGET_FILE:ctjs-translate>
            -DOPT=$<TARGET_FILE:ctjs-opt>
            -DMLIR_TRANSLATE=${MLIR_TRANSLATE_EXE}
            -DSOURCE=${_page_js}
            "-DENTRIES="
            -DOUTPUT=${_page_cpp}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Support/compile-js-to-cpp.cmake"
    DEPENDS "${_page_js}" ctjs-translate ctjs-opt
            "${CMAKE_CURRENT_SOURCE_DIR}/Support/compile-js-to-cpp.cmake"
    COMMENT "Compiling invaders.html's script through the EmitC backend"
    VERBATIM)

  set(_page_table "${CMAKE_CURRENT_BINARY_DIR}/invaders.entries.cpp")
  add_custom_command(
    OUTPUT "${_page_table}"
    COMMAND "${CMAKE_COMMAND}"
            -DGENERATED=${_page_cpp}
            -DOUTPUT=${_page_table}
            -DTABLE=ctc_invaders_entries
            -DSOURCE_NAME=invaders.html
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Support/aot-entry-table.cmake"
    DEPENDS "${_page_cpp}" "${CMAKE_CURRENT_SOURCE_DIR}/Support/aot-entry-table.cmake"
    COMMENT "Writing the entry table for invaders.html"
    VERBATIM)

  set_source_files_properties("${_page_cpp}" PROPERTIES
    COMPILE_OPTIONS "${CTCOMPILE_GENERATED_WARNINGS}")

  # THE ASSET ROOT IS ctbrowser/, because the page names its sprite sheet
  # `examples/assets/sprites.bmp` - a path relative to the tree, which is how
  # ctbrowse is run and therefore what the page was written against.
  set(_page_defs
    CTCOMPILE_LAUNCHER_PAGE="${_page_html}"
    CTCOMPILE_LAUNCHER_ASSET_ROOT="${CTBROWSER_MONOREPO_ROOT}/ctbrowser")

  add_executable(ctcompile-test-launcher-page-vm Runtime/Launcher/Page.cpp)
  target_compile_definitions(ctcompile-test-launcher-page-vm PRIVATE ${_page_defs})
  target_link_libraries(ctcompile-test-launcher-page-vm PRIVATE ctbrowser::ctbrowser)
  ctcompile_target(ctcompile-test-launcher-page-vm)

  add_executable(ctcompile-test-launcher-page-aot
    Runtime/Launcher/Page.cpp "${_page_cpp}" "${_page_table}")
  target_compile_definitions(ctcompile-test-launcher-page-aot PRIVATE ${_page_defs}
    CTCOMPILE_LAUNCHER_AOT=1 CTCOMPILE_LAUNCHER_TABLE=ctc_invaders_entries)
  target_link_libraries(ctcompile-test-launcher-page-aot PRIVATE ctbrowser::ctbrowser)
  ctcompile_target(ctcompile-test-launcher-page-aot)

  add_test(NAME ctcompile_launcher_page
           COMMAND ${CMAKE_COMMAND}
                   -DVM=$<TARGET_FILE:ctcompile-test-launcher-page-vm>
                   -DAOT=$<TARGET_FILE:ctcompile-test-launcher-page-aot>
                   -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/Runtime/Launcher/check-page.cmake)
endif()

# ---------------------------------------------------------------------------
# --- PHASE 61: the JavaScript-global to C++/Boost map -------------------------
#
# `ctcompile-plan/24-native-cpp-backend.md` Phase 61 - the specification's §8
# table, an X-macro in include/ctcompile/StdLib/StdLibMap.def that the test
# walks directly. NO MLIR IS REQUIRED, so the conformance test runs in the
# CTCOMPILE_ENABLE_MLIR=OFF configuration where most of this file does not.

# BOOST.JSON AND BOOST.REGEX, WHICH THE ENGINE DOES NOT ALREADY ASK FOR.
# ctbrowser/cmake/dependencies.cmake requests COMPONENTS url and ctcompile's own
# CMakeLists requests program_options; neither of those pulls in json or regex,
# and both are COMPILED libraries rather than header-only. They are needed HERE
# because the divergences this test pins are divergences OF Boost - a refusal
# asserted without running the thing being refused is a refusal nobody checked.
find_package(Boost CONFIG QUIET COMPONENTS json regex)

if(TARGET Boost::json AND TARGET Boost::regex)
  add_executable(ctcompile-test-stdlib_map Runtime/StdLibMap.cpp)
  target_link_libraries(ctcompile-test-stdlib_map
    PRIVATE ctbrowser::script Boost::json Boost::regex)
  ctcompile_target(ctcompile-test-stdlib_map)
  add_test(NAME ctcompile_stdlib_map COMMAND ctcompile-test-stdlib_map)
else()
  # SAID OUT LOUD RATHER THAN SKIPPED SILENTLY. A suite that quietly drops a
  # test on a box missing a dependency is the "green build that measured
  # nothing" this project has already been bitten by twice.
  message(WARNING
    "ctcompile: the Phase 61 standard-library map test is NOT registered. "
    "Boost_json_FOUND=${Boost_json_FOUND} Boost_regex_FOUND=${Boost_regex_FOUND} - "
    "install Boost (brew install boost) and configure again.")
endif()

# --- PHASE 53: the ctnative type lattice --------------------------------------
#
# ONE BLOCK, APPENDED, which is what Appendix A.3 of the plan's part 23 asks of
# every agent working in this file: it names test/CMakeLists.txt as the most
# collided-on file in the tree and asks for a concatenation rather than an edit.
# Everything this needs is its own file.
#
# WHY IT IS AN EXECUTABLE AND NOT A LIT TEST. test/CTNative/*.mlir already
# covers what lit is for - that the ODS parses and prints what it claims. What
# this asks cannot be written as text: whether `meet` is a lattice join. The
# associativity sweep alone is 9261 triples, and the divergence pin has to
# COMPILE AND RUN JavaScript in the real interpreter to find out what
# `"\u{1F600}".length` answers, because writing the number down here is exactly
# the stale-constant failure GC/Roots.cpp records at the other end of the tree.
#
# IT LINKS BOTH SIDES, and it is the only test that does: CTNativeDialect for
# the lattice, ctbrowser::script for the interpreter that judges it. That is the
# point - part 24 §A.2, "Every phase's gate is a comparison against the
# interpreter."
if(CTCOMPILE_ENABLE_MLIR)
  add_executable(ctcompile-test-ctnative-lattice Analysis/Types/Lattice.cpp)
  # MLIRAsmParser IS NOT OPTIONAL AND ITS ABSENCE LOOKS LIKE NOTHING. The meet
  # table spells its types the way a .mlir file does and calls mlir::parseType,
  # which lives there and not in MLIRIR: without it every line COMPILES and the
  # link fails on a mangled name that says nothing about parsing.
  target_link_libraries(ctcompile-test-ctnative-lattice
    PRIVATE CTNativeDialect MLIRIR MLIRAsmParser ctbrowser::script)
  ctcompile_target(ctcompile-test-ctnative-lattice)
  add_test(NAME ctcompile_ctnative_lattice COMMAND ctcompile-test-ctnative-lattice)
endif()

# ============================================================================
