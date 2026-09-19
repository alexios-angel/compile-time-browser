ctbrowser_test(webgl2_ratchet)
ctbrowser_test(webgl2_api)

# These tests reach the ANGLE driver; suppress only its known library races.
set_tests_properties(webgl2_ratchet webgl2_api PROPERTIES ENVIRONMENT
  "CTBROWSER_FONT_PATH=${PROJECT_SOURCE_DIR}/resources/fonts;SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;TSAN_OPTIONS=suppressions=${CTBROWSER_TEST_SUPPORT_DIR}/tsan.supp")
