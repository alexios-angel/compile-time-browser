# Each top-level source is one independently runnable test.
file(GLOB tests RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS *.cpp)
foreach(test IN LISTS tests)
  get_filename_component(name "${test}" NAME_WE)
  ctbrowser_test(${name})
endforeach()

add_subdirectory(css_values)

# navigation drives run_app, which initialises SDL - and SDL reaches libdbus,
# whose own lock order TSan reports as an inversion; its offscreen video driver
# initialises EGL without releasing it, which LeakSanitizer reports. Both
# suppressed by LIBRARY - see the files - so a real one in our code is still
# caught.
#
# set_tests_properties resolves a test name in the directory scope that created it, so a property for
# a unittest belongs in this file. ENVIRONMENT is one property, so this REPLACES
# the font path ctbrowser_test() set rather than adding to it.
set_tests_properties(navigation PROPERTIES ENVIRONMENT
  "CTBROWSER_FONT_PATH=${PROJECT_SOURCE_DIR}/resources/fonts;SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;TSAN_OPTIONS=suppressions=${CTBROWSER_TEST_SUPPORT_DIR}/tsan.supp;LSAN_OPTIONS=suppressions=${CTBROWSER_TEST_SUPPORT_DIR}/lsan.supp")
