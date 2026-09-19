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

# Constructor and receiver cases remain part of the vm_functions executable.
target_sources(ctbrowser-test-vm_functions PRIVATE js/vm_functions/constructors.cpp)

# Public Core clients also run without the browser or interpreter.
foreach(client core_basics core_text core_uri core_json)
  get_target_property(_core_libs ctbrowser-test-${client} LINK_LIBRARIES)
  list(FILTER _core_libs EXCLUDE REGEX "^ctbrowser::(dom|script|style|layout|paint|raster|shell|ctbrowser)$")
  set_property(TARGET ctbrowser-test-${client} PROPERTY LINK_LIBRARIES "${_core_libs}")
endforeach()

# The native DOM clients must link without the interpreter or Shell.
# Keep the test helper's configured trace libraries and DOM/Core dependencies.
foreach(client dom_token_list dom_element dom_shadow dom_dataset)
  get_target_property(_dom_libs ctbrowser-test-${client} LINK_LIBRARIES)
  list(FILTER _dom_libs EXCLUDE REGEX "^ctbrowser::(script|style|layout|paint|raster|shell|ctbrowser)$")
  set_property(TARGET ctbrowser-test-${client} PROPERTY LINK_LIBRARIES "${_dom_libs}")
endforeach()

# Public Style clients must also link without Script or Shell.
foreach(client style_easing style_closest)
  get_target_property(_style_libs ctbrowser-test-${client} LINK_LIBRARIES)
  list(FILTER _style_libs EXCLUDE REGEX "^ctbrowser::(script|layout|paint|raster|shell|ctbrowser)$")
  set_property(TARGET ctbrowser-test-${client} PROPERTY LINK_LIBRARIES "${_style_libs}")
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

# --- Precompiled header: parse the umbrella once, not once per test ----------
#
# `<ctbrowser.hpp>` is the one-include public API, and parsing it costs ~2.9 s
# (measured; an empty TU is 0.01 s). Every full test re-parsed it, so the ~480
# test TUs spent ~1,400 s of the suite's build - over half of it - on the same
# parse. Built ONCE here and reused across the targets, which roughly halves
# the unittests' compile time.
#
# LEFT OUT: the ten tests that deliberately link a SUBSET of the libraries (the
# three foreach blocks above) and do not include the umbrella - forcing it onto
# them would reference libraries their link line drops. `selectors` is the
# arbitrary full-engine anchor that builds the shared PCH.
set(_pch_anchor ctbrowser-test-selectors)
target_precompile_headers(${_pch_anchor} PRIVATE <ctbrowser.hpp>)
set(_pch_excluded core_basics core_text core_uri core_json
                  dom_token_list dom_element dom_shadow dom_dataset
                  style_easing style_closest)
foreach(test IN LISTS tests)
  get_filename_component(_pch_name "${test}" NAME_WE)
  if(_pch_name STREQUAL selectors OR _pch_name IN_LIST _pch_excluded)
    continue()
  endif()
  target_precompile_headers(ctbrowser-test-${_pch_name} REUSE_FROM ${_pch_anchor})
endforeach()
