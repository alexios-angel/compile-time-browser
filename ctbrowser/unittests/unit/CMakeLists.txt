# Unit tests. Tests whose names share a first word live in a folder named for
# it; the rest live here. Each folder's CMakeLists.txt calls
# ctbrowser_test_directory() (cmake/modules/CTTest.cmake), which names a test
# for its path below this directory (aot/basics.cpp is `aot_basics`), and adds
# its own subfolders.
set(CTBROWSER_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
ctbrowser_test_directory()
add_subdirectory(aot)
add_subdirectory(control)
add_subdirectory(core)
add_subdirectory(css)
add_subdirectory(cssom)
add_subdirectory(document)
add_subdirectory(dom)
add_subdirectory(element)
add_subdirectory(event)
add_subdirectory(events)
add_subdirectory(forms)
add_subdirectory(html)
add_subdirectory(image)
add_subdirectory(input)
add_subdirectory(layout)
add_subdirectory(node)
add_subdirectory(page)
add_subdirectory(paint)
add_subdirectory(script)
add_subdirectory(shadow)
add_subdirectory(style)
add_subdirectory(url)
add_subdirectory(xml)

# The navigation test drives run_app, which initialises SDL. SDL reaches
# libdbus, whose own lock order TSan reports as an inversion, and its offscreen
# video driver initialises EGL without releasing it, which LeakSanitizer
# reports. Both are suppressed by library name (see tsan.supp and lsan.supp),
# so a real report in our code is still caught.
#
# set_tests_properties resolves a test name in the directory scope that
# created it, so a property for a test in this directory belongs in this file.
# ENVIRONMENT is one property, so this replaces the font path ctbrowser_test()
# set rather than adding to it. navigation.cpp's
# test_environment_overrides_are_opt_in looks for CTBROWSER_CLOCK.
set_tests_properties(navigation PROPERTIES ENVIRONMENT
  "CTBROWSER_FONT_PATH=${PROJECT_SOURCE_DIR}/resources/fonts;CTBROWSER_CLOCK=86400000;SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;TSAN_OPTIONS=suppressions=${CTBROWSER_TEST_SUPPORT_DIR}/tsan.supp;LSAN_OPTIONS=suppressions=${CTBROWSER_TEST_SUPPORT_DIR}/lsan.supp")
