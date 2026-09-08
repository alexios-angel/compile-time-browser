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
# THE COLLECTOR, FORCED TO RUN WHERE A PAGE CANNOT MAKE IT RUN. Its own file
# because every case in it needs `gc()` inside a callback - which no shipped
# page can do, `collect_if_due` being once per tick - and because what it
# measures is a POINTER staying valid rather than an expression's value. That
# is the class of defect test262 and WPT cannot see: they measure conformance
# from outside, and a use-after-free the corpus never triggers looks green.
ctbrowser_test(unit/script_gc)
ctbrowser_test(unit/script_this)
# THE IMAGE AS AN UNTRUSTED FILE. `ctcompile/test/ProgramImage.cpp` asks whether
# an image round-trips and whether a corrupt one is refused; this asks the
# narrower question the round-trip cannot see - whether a constant that is a
# VALID double and an INVALID value gets into the VM. It lives here rather than
# there because it is the ENGINE's trust boundary and must be gated in a
# runtime-only build, where ctcompile is not in the tree at all.
ctbrowser_test(unit/image_trust)
ctbrowser_test(unit/gl_basics)
ctbrowser_test(unit/net_basics)
ctbrowser_test(unit/data_url)
ctbrowser_test(unit/url_basics)
ctbrowser_test(unit/image_basics)
ctbrowser_test(unit/style_basics)
# The CSS tokenizer, against the cases the submodule it replaced got wrong.
ctbrowser_test(unit/css_syntax)
ctbrowser_test(unit/css_values)
# The CSSOM object model - document.styleSheets and the objects under it. Its
# own file rather than more of css_values: that one asks what a VALUE is worth,
# this one asks what the object model SERIALISES to, and every case in it is a
# string a `css/cssom` test compares byte for byte.
ctbrowser_test(unit/cssom)
# `querySelector` and the Selectors engine behind it. Its own file rather than
# more of css_syntax: that one asks what the parser BUILDS, and this asks what a
# script gets back when it matches with it - which used to be a second, weaker
# matcher entirely.
ctbrowser_test(unit/selectors)
# The document as a Node. Its own file rather than more of node_methods
# because every case in it turns on ONE structural fact - this tree builder has
# no Document node above `<html>` - so what the cases pin down is a MODEL being
# consistent with itself, including where it honestly refuses. See the block
# above install_document_as_node in lib/Shell/bindings/document.cpp.
ctbrowser_test(unit/document_node)
# `document.title`, the eight HTML collections and named access. Its own file
# because every case in it is the same shape - write and read back in ONE
# statement - and that shape is what the accessors exist for.
ctbrowser_test(unit/tree_accessors)
# createHTMLDocument and createDocument. Its own file because what it pins down
# is a MODEL - a second Document is a second dom_bindings over its own tree -
# and two of its cases assert that something deliberately does NOT work.
ctbrowser_test(unit/second_document)
# The XML front end. Its own file because what it pins down is a SECOND parser
# - nothing implied, every tag self-closing, case preserved, CDATA as character
# data and well-formedness fatal - and every case in it is a place where the
# HTML tree builder gives a different, and for an .xhtml file wrong, answer.
ctbrowser_test(unit/xml_parse)
# The same front end from the bindings' side: a page loaded as XML, its
# contentType and compatMode, a <script> in a marked section that RUNS, and a
# tag whose case survives to `tagName`.
ctbrowser_test(unit/xml_document)
ctbrowser_test(unit/layout_basics)
# The flex formatting context. Its own file rather than more of layout_basics:
# S9.7's freeze loop has more cases than the other three contexts together.
ctbrowser_test(unit/flex_basics)
# <iframe>: a second document inside the first. See lib/Shell/bindings/frames.cpp.
ctbrowser_test(unit/frames)
# Positioning, which is the one part of layout where a box's place is decided
# by an ancestor rather than by its parent - see layout/position.hpp.
ctbrowser_test(unit/position_basics)
ctbrowser_test(unit/paint_basics)
ctbrowser_test(unit/raster_basics)
ctbrowser_test(unit/fonts_basics)
ctbrowser_test(unit/svg_basics)
ctbrowser_test(unit/pipeline_basics)
ctbrowser_test(unit/shell_basics)
# --- the DOM bindings, eight files since 2026-09-07 -------------------------
# bindings_basics was 3,365 lines, and every carve-out in this file had named it
# as the file it was NOT more of. It is gone: its cases are these eight,
# verbatim, grouped the way its own section comments grouped them, sharing
# unit/page_probe.hpp. Each line says what its file asks that the others do not.
#
# Script WRITING to the document and the pixels following - classList and
# style, the document API and the document's own properties, a stale handle
# staying inert - and the two robustness cases. The original's header is here.
ctbrowser_test(unit/dom_mutation)
# The shape a wrapper has before a method is called on it: the window as the
# global object, HTML 2.6 reflection, and the interface objects and their chain.
ctbrowser_test(unit/dom_interfaces)
# The Node and Element METHOD surface: tree walking, the live collections, the
# constructible DOM, querySelector on an element, and the four ways of putting
# markup or text into one.
ctbrowser_test(unit/node_methods)
# Event dispatch: the whole path, EventTarget, the interface hierarchy, what
# dispatchEvent refuses, the listener options and the handler properties. The
# listener-invocation RULES are event_listeners, below, from the chrome side.
ctbrowser_test(unit/event_dispatch)
# The canvas contexts as a SCRIPT sees them - paths, text alignment, the fill
# rule, compositing, ImageData, clip, and WebGL constructing and refusing.
# widgets_basics asks the same canvas what it draws.
ctbrowser_test(unit/canvas_context)
# What happens on a later TURN: await, fetch, collection running on its own,
# timers and animation frames. Every case needs an event loop.
ctbrowser_test(unit/page_async)
# Leaving the page: location and cookies, alert, reload, a link handed to the
# embedder - and the three cases that drive run_app, which is why the SDL
# suppressions below are set on this test and no other.
ctbrowser_test(unit/navigation)
# Input reaching script, with the legacy keyCode and which, and real pages -
# breakout, pong, the invaders page - played by injecting input.
ctbrowser_test(unit/page_input)
# MutationObserver. Its own file rather than more of dom_mutation because
# what it asks about is a DIFF - records reconstructed from a snapshot rather
# than reported by the mutation - and every case in it is about the SHAPE of a
# record and about WHEN the callback runs, neither of which any other test
# observes. See lib/Shell/bindings/mutation.cpp.
ctbrowser_test(unit/mutation_observer)
ctbrowser_test(unit/widgets_basics)
# --- the chrome, five files since 2026-09-07 --------------------------------
# chrome_basics was 2,299 lines. Its cases are these five, verbatim, grouped by
# its own section comments; the helpers they share are unit/chrome_probe.hpp.
#
# Form controls as they LOOK and as they are EDITED: the caret in the drawing
# face, clicks and arrows placing it, password bullets, disabled, the checkbox
# tick, a textarea's lines and wraps, and what a script reads off a control.
ctbrowser_test(unit/control_editing)
# A field's VIEW: scrolling to keep the caret visible, the wheel over a field,
# auto-scroll while drag-selecting, and the caret blink's wakeups.
ctbrowser_test(unit/control_scrolling)
# Tables, list markers, disclosure triangles, <details> and <select> - the
# things a page puts on screen that are not boxes and text, which was the
# original file's stated subject. Its header is here.
ctbrowser_test(unit/tables_and_markers)
# The browser's own chrome on a page: the scrollbar, the select popup, the
# context menu, the clipboard, the cursor, and page-level text selection.
ctbrowser_test(unit/page_chrome)
# The listener-invocation rules dom/events was getting wrong, one assertion
# apiece: the default passive value, the copied listener list, a throwing
# listener, handleEvent, the bare EventTarget globals, and window.event.
ctbrowser_test(unit/event_listeners)
# The Bootstrap fixtures against test/baseline/. The Chrome half of that harness
# is tools/check/css-parity.py and is deliberately not a ctest; this is the half
# that catches a regression without a browser download. See docs/plans/bootstrap.md.
ctbrowser_test(unit/bootstrap_layout)
# The attribute API on Element. Its own file rather than more of
# node_methods because every case in it turns on a DISTINCTION - the
# qualified lookup against the namespaced one, an HTML element against a
# foreign one, a name that SERIALISES against a name that parses - and each of
# those used to have one answer where two belong.
ctbrowser_test(unit/element_attrs)
# CharacterData and the constructible node interfaces. Its own file because
# every offset in it is a UTF-16 CODE UNIT and this engine stores UTF-8 - a
# byte offset answers `substringData(39, 2)` wrongly only once the text stops
# being ASCII, which is exactly the case the corpus tests and a suite of
# English fixtures never would.
ctbrowser_test(unit/character_data)
# attachShadow and the shadow root. Its own file because half of what it asserts
# is what a shadow tree is NOT: the host gains no children, `document.
# querySelector` finds nothing in it, and the light DOM keeps the shape every
# other test in this directory measures. A rung that makes a shadow tree render
# has to keep those true.
ctbrowser_test(unit/shadow_dom)

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

# navigation drives run_app (bindings_basics did, until it was split), which
# initialises SDL - and SDL reaches libdbus, whose own lock order TSan reports
# as an inversion. Same suppression file, same rule: by LIBRARY, so a real one
# in our code is still caught.
# ...and its offscreen video driver initialises EGL without releasing it, which
# LeakSanitizer reports. Both suppressed by LIBRARY - see the files.
#
# Set HERE rather than beside the corpus properties in test/: set_tests_properties
# resolves a test name in the directory scope that created it, so a property for
# a unittest belongs in this file.
set_tests_properties(navigation PROPERTIES ENVIRONMENT
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
ctbrowser_test(js/call_order)
# --- the per-constructor error prototypes, the second-largest gap -----------
# Six NativeError constructors and prototypes existed; what did not was any way
# for an error the ENGINE raised to land on the right one. Its own file for the
# same reason as the one above: it asks about the SHAPE of a thrown value rather
# than about the value an expression computes.
ctbrowser_test(js/error_types)
# --- Array.prototype over a receiver that is not an Array -------------------
# Every one of these methods is specified generic and every one of them opened
# with `this_array()`, which answers nullptr for anything else - about 580
# test262 files across the nine iteration methods. Its own file for the same
# reason the two above have one, and it also covers the seven Array.prototype
# methods that did not exist at all.
ctbrowser_test(js/array_generics)
# --- JSON, the whole of 25.5 ------------------------------------------------
# `stringify` had no replacer, no `space`, no `toJSON` and no stack - so a
# self-referential object recursed until the C++ stack ran out where the
# specification has a TypeError - and `parse` accepted source no other reader
# will and answered `undefined` instead of throwing where it could not. Its own
# file for the same reason the four above have one: it asks what a value
# SERIALISES to and what text is REFUSED, not what an expression computes.
ctbrowser_test(js/json_basics)
# --- the early errors, the largest gap test262 had left ---------------------
# 2,812 tests asserted that source text is a SyntaxError while this engine
# parsed it, ran it, and reported whatever happened next. Its own file for the
# same reason the five above have one: every case asks whether source is
# REFUSED rather than what an expression computes - and each refusal is paired
# with the valid near-miss that must still run, which is the half that stops a
# rule growing into a false positive that would stop a page loading.
ctbrowser_test(js/early_errors)
# --- the four Promise statics that were missing -----------------------------
# `withResolvers` is the one WPT asked for by name; `allSettled`, `any` and
# `race` were absent beside it. Its own file because every case has to WAIT -
# a combinator's reaction is a microtask, so what it produced is only readable
# after the turn drains, and each one is exercised over an input that is still
# pending when the combinator is called.
ctbrowser_test(js/promise_combinators)
