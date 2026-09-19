ctbrowser_test(phaser_ratchet)
ctbrowser_test(phaser_api)
ctbrowser_test(phaser_invaders)

# These tests reach the ANGLE driver; suppress only its known library races.
set_tests_properties(phaser_ratchet PROPERTIES ENVIRONMENT
  "CTBROWSER_FONT_PATH=${PROJECT_SOURCE_DIR}/resources/fonts;SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;TSAN_OPTIONS=suppressions=${CTBROWSER_TEST_SUPPORT_DIR}/tsan.supp")
