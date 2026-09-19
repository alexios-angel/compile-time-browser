ctbrowser_test(babylon_ratchet)
ctbrowser_test(babylon_api)
ctbrowser_test(babylon_lighting)
ctbrowser_test(babylon_interaction)

# These tests reach the ANGLE driver; suppress only its known library races.
set_tests_properties(babylon_ratchet babylon_api babylon_lighting babylon_interaction PROPERTIES ENVIRONMENT
  "CTBROWSER_FONT_PATH=${PROJECT_SOURCE_DIR}/resources/fonts;SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;TSAN_OPTIONS=suppressions=${CTBROWSER_TEST_SUPPORT_DIR}/tsan.supp")
