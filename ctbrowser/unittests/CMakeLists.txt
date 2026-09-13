# The focused tests: unit/ one file per subsystem, js/ the language suites.
#
# `ctbrowser_test(<bucket>/<name>)` comes from cmake/modules/CTTest.cmake; the
# ctest name is the BASENAME, so `ctest -R vm_` keeps working and a new test is
# a new .cpp in one of the two directories - nothing to register here.
file(GLOB tests RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS unit/*.cpp js/*.cpp)
foreach(test IN LISTS tests)
  string(REPLACE ".cpp" "" test "${test}")
  ctbrowser_test(${test})
endforeach()

# The native DOM clients must link without the interpreter or Shell.
# Keep the test helper's configured trace libraries and DOM/Core dependencies.
foreach(client dom_token_list dom_element)
  get_target_property(_dom_libs ctbrowser-test-${client} LINK_LIBRARIES)
  list(FILTER _dom_libs EXCLUDE REGEX "^ctbrowser::(script|style|layout|paint|raster|shell|ctbrowser)$")
  set_property(TARGET ctbrowser-test-${client} PROPERTY LINK_LIBRARIES "${_dom_libs}")
endforeach()

# vm_operators is the one file that still runs ctjs - `diff_vs_v1` sends an
# expression through the previous engine's interpreter as well - and that
# public header also reaches its nested ctc dependency.
target_include_directories(ctbrowser-test-vm_operators PRIVATE
  "${CTBROWSER_CTJS}/include"
  "${CTBROWSER_CTC}/include")

# navigation drives run_app, which initialises SDL - and SDL reaches libdbus,
# whose own lock order TSan reports as an inversion; its offscreen video driver
# initialises EGL without releasing it, which LeakSanitizer reports. Both
# suppressed by LIBRARY - see the files - so a real one in our code is still
# caught.
#
# Set HERE rather than beside the corpus properties in test/: set_tests_properties
# resolves a test name in the directory scope that created it, so a property for
# a unittest belongs in this file. ENVIRONMENT is one property, so this REPLACES
# the font path ctbrowser_test() set rather than adding to it.
set_tests_properties(navigation PROPERTIES ENVIRONMENT
  "CTBROWSER_FONT_PATH=${PROJECT_SOURCE_DIR}/resources/fonts;SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;TSAN_OPTIONS=suppressions=${CTBROWSER_TEST_SUPPORT_DIR}/tsan.supp;LSAN_OPTIONS=suppressions=${CTBROWSER_TEST_SUPPORT_DIR}/lsan.supp")
