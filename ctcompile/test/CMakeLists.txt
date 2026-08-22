# ctcompile's tests. There is no CI in this repository and none is planned, so
# every invariant that would otherwise be a CI check is a test or a build
# error - see the plan's "There is no CI" section. lit and FileCheck arrive
# with the first textual IR, in Phase 7; until then a test is an executable or
# a command with an expected output.
#
# PHASE 0'S INVENTORIES, WALKED AS DATA. bytecode.hpp already static_asserts the
# opcode count; this covers what a static_assert cannot - that the columns are
# internally consistent, that the tables read as TABLES rather than only expand
# as macros, and that the call-path and GC-root inventories hold the properties
# later phases are built on. It also compiles EngineContract.hpp, which is
# nothing but static_asserts about the engine's layout.
add_executable(ctcompile-test-inventories Inventories.cpp)
target_link_libraries(ctcompile-test-inventories PRIVATE ctcompile::support)
ctcompile_target(ctcompile-test-inventories)
add_test(NAME ctcompile_inventories COMMAND ctcompile-test-inventories)

# THE STUB HAS TO RUN. Phase -1's gate says ctcompile builds an executable, and
# an executable that builds and then dies on a missing symbol has passed a
# compile and failed the gate. This runs it.
add_test(NAME ctcompile_version COMMAND ctcompile-tool --version)
set_tests_properties(ctcompile_version PROPERTIES
  PASS_REGULAR_EXPRESSION "ctcompile [0-9]+\\.[0-9]+\\.[0-9]+ \\(ctbrowser .* bytecode operations\\)")

# And the usage path, because `ctcompile` with no arguments exiting 0 would mean
# the argument handling is not there at all.
add_test(NAME ctcompile_usage COMMAND ctcompile-tool)
set_tests_properties(ctcompile_usage PROPERTIES WILL_FAIL TRUE)

# `--help` is a SUCCESS, and it prints the usage line rather than an option
# list with no verb in it.
add_test(NAME ctcompile_help COMMAND ctcompile-tool --help)
set_tests_properties(ctcompile_help PROPERTIES
  PASS_REGULAR_EXPRESSION "usage: ctcompile \\[options\\] <application-directory>")

# An unknown option is a USAGE error, not a crash and not a silent success.
# This is the half of the command line Boost.Program_options owns, and the
# reason the catch block names it separately.
add_test(NAME ctcompile_rejects_nonsense COMMAND ctcompile-tool --not-an-option)
set_tests_properties(ctcompile_rejects_nonsense PROPERTIES WILL_FAIL TRUE)

# THE COMPARATOR THAT ACCEPTS PHASE 16A, and the negative cases that stop it
# from accepting anything. A comparator too lenient does not fail to catch a
# bad blueprint - it certifies one.
add_executable(ctcompile-test-html_comparator HtmlComparator.cpp)
target_link_libraries(ctcompile-test-html_comparator PRIVATE ctcompile::html)
ctcompile_target(ctcompile-test-html_comparator)
add_test(NAME ctcompile_html_comparator COMMAND ctcompile-test-html_comparator)

# THE COMPARATOR THAT ACCEPTS PHASE 16B. Every case below leaves
# selector_count() and rule_count() untouched and changes what the page looks
# like - which is exactly the class of difference the two counts cannot see and
# the reason engine::for_each_rule exists.
add_executable(ctcompile-test-css_comparator CssComparator.cpp)
target_link_libraries(ctcompile-test-css_comparator PRIVATE ctcompile::css)
ctcompile_target(ctcompile-test-css_comparator)
add_test(NAME ctcompile_css_comparator COMMAND ctcompile-test-css_comparator)

# THE PROGRAM IMAGE: a round trip that must be exact, and a loader that must
# refuse. An image is executable input - the VM reads its pools with unchecked
# operator[] and one of those reads is a WRITE - so the negative cases here are
# the point of the file.
add_executable(ctcompile-test-program_image ProgramImage.cpp)
target_link_libraries(ctcompile-test-program_image PRIVATE ctcompile::javascript)
ctcompile_target(ctcompile-test-program_image)
add_test(NAME ctcompile_program_image COMMAND ctcompile-test-program_image)

# THE MANIFEST. Phase 1's gate asks for one; this checks the part of it that can
# be wrong, which is the string escaping - a resource name is whatever the
# document said, and a document may say `p5"; drop</script>`.
add_executable(ctcompile-test-manifest Manifest.cpp)
target_link_libraries(ctcompile-test-manifest PRIVATE ctcompile::support)
ctcompile_target(ctcompile-test-manifest)
add_test(NAME ctcompile_manifest COMMAND ctcompile-test-manifest)

# THE APPLICATION BUNDLE, on the same terms as the program image above and for
# a sharper reason: a bundle is a table of offsets read out of a file, and what
# it carries is program images, which are executable input. The last section of
# that file is the one that matters - it asserts the COUNTER that says the
# images were used, because a page that quietly recompiled produces an
# identical document.
add_executable(ctcompile-test-app_bundle AppBundle.cpp)
target_link_libraries(ctcompile-test-app_bundle PRIVATE ctbrowser::ctbrowser)
ctcompile_target(ctcompile-test-app_bundle)
# THE BINARY DIRECTORY IS AN ARGUMENT because two of its cases cannot be run
# here: they are bundles the LAUNCHER has to refuse, and the guard that refuses
# them lives behind a window. It writes them out and check-package.cmake below
# feeds them to the real ctrun.
add_test(NAME ctcompile_app_bundle
         COMMAND ctcompile-test-app_bundle ${CMAKE_CURRENT_BINARY_DIR})

# AND THE WHOLE THING, THE WAY SOMEONE USES IT: a directory in, an executable
# out, and that executable started from somewhere else entirely. It is the only
# test here that runs the compiler, the launcher and the engine together, and
# the only one that could catch them disagreeing.
#
# A pass means more than "exit 0" - `run_bundle` sets require_script_images, so
# the packaged application refuses to start if any of its scripts had to be
# compiled from source. See the header of check-package.cmake.
#
# NOT GUARDED WITH if(TARGET). ctcompile is configured after ctbrowser's tools,
# so the launcher is always there - and a guard would turn "the launcher stopped
# being built" into a test that quietly stops existing, which is the failure
# this file is least able to notice.
add_test(NAME ctcompile_package
         COMMAND ${CMAKE_COMMAND}
                 -DCTCOMPILE=$<TARGET_FILE:ctcompile-tool>
                 -DLAUNCHER=$<TARGET_FILE:ctbrowser-tool-ctrun>
                 -DAPP=${CMAKE_CURRENT_SOURCE_DIR}/app
                 -DOUT=${CMAKE_CURRENT_BINARY_DIR}/packaged-fixture
                 -DREFUSALS=${CMAKE_CURRENT_BINARY_DIR}
                 -DBROWSE=$<TARGET_FILE:ctbrowser-tool-ctbrowse>
                 -DFONTS=${CTBROWSER_MONOREPO_ROOT}/ctbrowser/resources/fonts
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check-package.cmake)
# ORDER, not a mere preference: the two bundles the launcher must refuse are
# written by the test above.
set_tests_properties(ctcompile_package PROPERTIES DEPENDS ctcompile_app_bundle)
