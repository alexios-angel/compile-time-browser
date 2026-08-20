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
