# Tests named for their path below unittests/unit/: color.cpp here is
# `css_values_color` (ctbrowser_test_directory, cmake/modules/CTTest.cmake).
ctbrowser_test_directory()
# css/values.cpp's helpers, beside the tests that share its name.
target_sources(ctbrowser-test-css_values PRIVATE math.hpp support.hpp)
