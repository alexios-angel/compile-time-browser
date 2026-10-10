ctbrowser_test(p5_ratchet)
ctbrowser_test(p5_api)
ctbrowser_test(p5_page)

# The ratchet calls the reference lexer and parser directly.
target_include_directories(ctbrowser-test-p5_ratchet PRIVATE
  "${CTBROWSER_CTJS}/include")
# --source, --sketch and a bundle path, parsed by tools/support/options.hpp.
target_link_libraries(ctbrowser-test-p5_ratchet PRIVATE ctbrowser-options)

# These tests reach the ANGLE driver; suppress only its known library races.
set_tests_properties(p5_ratchet p5_page PROPERTIES ENVIRONMENT
  "CTBROWSER_FONT_PATH=${PROJECT_SOURCE_DIR}/resources/fonts;SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;TSAN_OPTIONS=suppressions=${CTBROWSER_TEST_SUPPORT_DIR}/tsan.supp")
