# Tests named for their path below unittests/unit/: debug.cpp here is
# `script_debug` (ctbrowser_test_directory, cmake/modules/CTTest.cmake).
ctbrowser_test_directory()

# script_gc's command line - a case-name filter - is parsed by
# tools/support/options.hpp.
target_link_libraries(ctbrowser-test-script_gc PRIVATE ctbrowser-options)
