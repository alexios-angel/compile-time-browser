# The focused tests: one file per subsystem, plus the language suites.
#
# `ctbrowser_test(<bucket>/<name>)` comes from cmake/modules/CTTest.cmake, and
# the argument is a PATH whose BASENAME is everything derived from it - the
# target, and more importantly the ctest name. `ctest -R vm_basics` has to keep
# working: it is in docs/build.md's command blocks and in what the ratchet tools
# pass through. The suite gained directories on 2026-08-09 and a project
# directory in the monorepo split, and the test names have not changed once.

# The subsystems, one file each.
ctbrowser_test(unit/aot_basics)
ctbrowser_test(unit/aot_dispatch)
ctbrowser_test(unit/aot_gc)
ctbrowser_test(unit/aot_helpers)
ctbrowser_test(unit/aot_program)
ctbrowser_test(unit/aot_throw)
ctbrowser_test(unit/core_basics)
ctbrowser_test(unit/dom_basics)
ctbrowser_test(unit/html_parse)
ctbrowser_test(unit/page_scripts)
# The compiler's debug side tables - local names, live ranges, and one source
# offset per instruction. Its own file rather than more of page_scripts: it asks
# about the SHAPE of a compiled program rather than about running one, and it
# carries the corpus measurement behind CTBROWSER_DEBUG_TABLE_COST=1.
ctbrowser_test(unit/script_debug)
ctbrowser_test(unit/gl_basics)
ctbrowser_test(unit/net_basics)
ctbrowser_test(unit/data_url)
ctbrowser_test(unit/url_basics)
ctbrowser_test(unit/image_basics)
ctbrowser_test(unit/style_basics)
# The CSS tokenizer, against the cases the submodule it replaced got wrong.
ctbrowser_test(unit/css_syntax)
ctbrowser_test(unit/layout_basics)
# The flex formatting context. Its own file rather than more of layout_basics:
# S9.7's freeze loop has more cases than the other three contexts together.
ctbrowser_test(unit/flex_basics)
# Positioning, which is the one part of layout where a box's place is decided
# by an ancestor rather than by its parent - see layout/position.hpp.
ctbrowser_test(unit/position_basics)
ctbrowser_test(unit/paint_basics)
ctbrowser_test(unit/raster_basics)
ctbrowser_test(unit/fonts_basics)
ctbrowser_test(unit/svg_basics)
ctbrowser_test(unit/pipeline_basics)
ctbrowser_test(unit/shell_basics)
ctbrowser_test(unit/bindings_basics)
ctbrowser_test(unit/widgets_basics)
ctbrowser_test(unit/chrome_basics)
# The Bootstrap fixtures against test/baseline/. The Chrome half of that harness
# is tools/check/css-parity.py and is deliberately not a ctest; this is the half
# that catches a regression without a browser download. See docs/plans/bootstrap.md.
ctbrowser_test(unit/bootstrap_layout)

# The language, against V8. The nine per-type suites were carved out of
# vm_basics and share js_expect.hpp; number_format and obfuscated ask the
# same question of real minifier output.
ctbrowser_test(js/vm_basics)
# The differential half runs the same expressions through ctjs's old
# interpreter, whose public header also reaches its nested ctc dependency.
target_include_directories(ctbrowser-test-vm_basics PRIVATE
  "${CTBROWSER_CTJS}/include"
  "${CTBROWSER_CTC}/include")
ctbrowser_test(js/number_format)
ctbrowser_test(js/math_basics)
ctbrowser_test(js/string_basics)
ctbrowser_test(js/type_basics)
ctbrowser_test(js/obfuscated)
ctbrowser_test(js/undefined_basics)
ctbrowser_test(js/null_basics)
ctbrowser_test(js/boolean_basics)
ctbrowser_test(js/number_basics)
ctbrowser_test(js/symbol_basics)
ctbrowser_test(js/bigint_basics)

# The GPU test needs SDL3 to BUILD; it does not need a GPU to PASS. Where there
# is no device it skips the hardware half and still checks the fallback, which
# is the path such a machine actually takes.
if(SDL3_FOUND)
  ctbrowser_test(unit/gpu_basics)
  target_link_libraries(ctbrowser-test-gpu_basics PRIVATE ctbrowser::gpu SDL3::SDL3)
  # `offscreen` rather than `dummy`: dummy has no Vulkan surface support, so a
  # device cannot be created under it even where the driver is fine.
  # The Vulkan driver is not TSan-instrumented, so its internal locking is
  # reported as races. Suppressed by library only - see test/support/tsan.supp.
  set_tests_properties(gpu_basics PROPERTIES ENVIRONMENT
    "CTBROWSER_FONT_PATH=${PROJECT_SOURCE_DIR}/resources/fonts;SDL_VIDEODRIVER=offscreen;TSAN_OPTIONS=suppressions=${CTBROWSER_TEST_SUPPORT_DIR}/tsan.supp")
endif()

# bindings_basics drives run_app, which initialises SDL - and SDL reaches
# libdbus, whose own lock order TSan reports as an inversion. Same suppression
# file, same rule: by LIBRARY, so a real one in our code is still caught.
# ...and its offscreen video driver initialises EGL without releasing it, which
# LeakSanitizer reports. Both suppressed by LIBRARY - see the files.
#
# Set HERE rather than beside the corpus properties in test/: set_tests_properties
# resolves a test name in the directory scope that created it, so a property for
# a unittest belongs in this file.
set_tests_properties(bindings_basics PROPERTIES ENVIRONMENT
  "CTBROWSER_FONT_PATH=${PROJECT_SOURCE_DIR}/resources/fonts;SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;TSAN_OPTIONS=suppressions=${CTBROWSER_TEST_SUPPORT_DIR}/tsan.supp;LSAN_OPTIONS=suppressions=${CTBROWSER_TEST_SUPPORT_DIR}/lsan.supp")

# --- the three crash classes test262 found, 2026-09-02 (append-only) ---------
# The toString recursion, the dense-array allocation and the two string methods
# that did not terminate. Its own file rather than more of number_basics or
# string_basics: every case in it fails by KILLING the process rather than by
# printing a mismatch, which is a different kind of test from a differential
# one. See docs/test262.md and the file's own header.
ctbrowser_test(js/crash_guards)

# --- property attributes, the largest non-crash gap test262 found -----------
# Its own file for the same reason crash_guards has one: it is about the SHAPE
# of a property rather than about the value an expression computes, so it does
# not belong in the per-type differential suites - and it would otherwise be
# measured only by test262, which is opt-in, needs a 273 MB corpus that is not
# in the repository, and takes five minutes.
ctbrowser_test(js/property_attributes)
