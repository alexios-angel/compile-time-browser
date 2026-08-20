# The system-level suite, and the DATA the rest of the suite reads.
#
# What lives here rather than in unittests/: the lint (a sweep over the tree,
# not a behaviour test), the corpus ratchets (somebody else's bundle, measured),
# the lock-free stress tests, and the directories that are pure data - golden/,
# baseline/, package/ and support/. `ctbrowser_test()` and the probes behind it
# come from cmake/modules/CTTest.cmake.

# The API-surface lint. Not a behaviour test - it sweeps include/, lib/
# and the application sources and fails on an engine include or an SDL symbol
# that is not on its allow-list.
ctbrowser_test(lint/api_surface)

# p5.js - how FAR the bundle gets, and how WIDE the surface is.
ctbrowser_test(corpus/p5/p5_ratchet)
# This ratchet calls ctjs's lexer and parser directly so it can report which
# stage rejected the corpus; vparse.hpp itself has no ctc dependency.
target_include_directories(ctbrowser-test-p5_ratchet PRIVATE
  "${CTBROWSER_CTJS}/include")
ctbrowser_test(corpus/p5/p5_api)

# Phaser 4. invaders asks a third question: is the GAME playable?
ctbrowser_test(corpus/phaser/phaser_ratchet)
ctbrowser_test(corpus/phaser/phaser_api)
ctbrowser_test(corpus/phaser/phaser_invaders)

# Babylon.js, the only corpus that exercises WebGL 2 properly.
ctbrowser_test(corpus/babylon/babylon_ratchet)
ctbrowser_test(corpus/babylon/babylon_api)
ctbrowser_test(corpus/babylon/babylon_lighting)
ctbrowser_test(corpus/babylon/babylon_interaction)

# WebGL 2 itself, measured through the bundles above.
ctbrowser_test(corpus/webgl2/webgl2_ratchet)
ctbrowser_test(corpus/webgl2/webgl2_api)

# ES modules - the same ratchet shape over the engine's own corpus.
ctbrowser_test(corpus/modules/module_ratchet)

# The lock-free read paths under contention. stress_slab is the one that
# must never be skipped: it is the evidence the path is sound.
ctbrowser_test(stress/stress_slab)
ctbrowser_test(stress/stress_dom)

# THE CORPUS TESTS REACH THE DRIVER TOO, once ANGLE is on. gpu_basics and
# bindings_basics got the suppression file because they were the only tests
# that touched Vulkan; a WebGL 2 or Babylon ratchet built with
# -DCTBROWSER_WITH_ANGLE=ON now does as well, and reported races inside
# SwiftShader's command buffers with no ctbrowser frame on the stack.
#
# ANGLE is on by default since 2026-08-09, so every TSan run gets these now;
# before that it took a build configured by hand, which is why it went unseen.
#
# CTBROWSER_FONT_PATH is repeated here because ENVIRONMENT is ONE property:
# ctbrowser_test() set it, and this call REPLACES rather than appends. Dropping
# it would send these eight tests to the 8x8 bitmap font and move their goldens.
foreach(angle-test webgl2_ratchet webgl2_api babylon_ratchet babylon_api
                   babylon_lighting babylon_interaction p5_ratchet phaser_ratchet)
  if(TEST ${angle-test})
    set_tests_properties(${angle-test} PROPERTIES ENVIRONMENT
      "CTBROWSER_FONT_PATH=${PROJECT_SOURCE_DIR}/resources/fonts;SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;TSAN_OPTIONS=suppressions=${CTBROWSER_TEST_SUPPORT_DIR}/tsan.supp")
  endif()
endforeach()

# The stress test is the one that must never be skipped: it is the evidence
# that the lock-free read path is actually sound. Give it room under TSan.
set_tests_properties(stress_slab PROPERTIES TIMEOUT 300)
