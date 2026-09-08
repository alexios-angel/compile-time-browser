# The focused tests: one file per subsystem, plus the language suites.
#
# `ctbrowser_test(<bucket>/<name>)` comes from cmake/modules/CTTest.cmake, and
# the argument is a PATH whose BASENAME is everything derived from it - the
# target, and more importantly the ctest name. `ctest -R vm_` has to keep
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
# THE STYLE SUITE IS SIX FILES. style_basics was 2,155 lines and 40 tests when
# it was split by topic on 2026-09-08; they share style_fixture.hpp, the
# document-and-resolve fixture, and `ctest -R style_` runs the family.
#
# Shorthands: a shorthand IS its longhands, expanded at record time so the
# cascade sees four declarations carrying the shorthand's source order.
ctbrowser_test(unit/style_shorthands)
# Selectors: matching, the buckets, the combinators, and what a rule compiles to.
ctbrowser_test(unit/style_selectors)
# Pseudo-classes: the structural ones, :nth-child() and its arithmetic, the
# functional ones and the element-state ones. Their own file rather than more
# of style_selectors because nth_child and functional_pseudos alone are as
# long as the rest of the matcher's cases together.
ctbrowser_test(unit/style_pseudo_classes)
# The cascade: specificity and source order, the origins, the style attribute
# and !important, inheritance, the explicit defaulting keywords, @media and
# @font-face.
ctbrowser_test(unit/style_cascade)
# Custom properties: var() substitution and `initial` on a custom property
# being the guaranteed-invalid value - the whole of Bootstrap 5.3's theming.
ctbrowser_test(unit/style_custom_properties)
# calc() and the math functions: what they resolve to, when the answer is a
# <number>, min()/max()/clamp(), and calc() inside the cascade.
ctbrowser_test(unit/style_calc)
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
# The document as a Node. Its own file rather than more of bindings_basics
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
ctbrowser_test(unit/bindings_basics)
# MutationObserver. Its own file rather than more of bindings_basics because
# what it asks about is a DIFF - records reconstructed from a snapshot rather
# than reported by the mutation - and every case in it is about the SHAPE of a
# record and about WHEN the callback runs, neither of which any other test
# observes. See lib/Shell/bindings/mutation.cpp.
ctbrowser_test(unit/mutation_observer)
ctbrowser_test(unit/widgets_basics)
ctbrowser_test(unit/chrome_basics)
# The Bootstrap fixtures against test/baseline/. The Chrome half of that harness
# is tools/check/css-parity.py and is deliberately not a ctest; this is the half
# that catches a regression without a browser download. See docs/plans/bootstrap.md.
ctbrowser_test(unit/bootstrap_layout)
# The attribute API on Element. Its own file rather than more of
# bindings_basics because every case in it turns on a DISTINCTION - the
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
#
# THE VM SUITE ITSELF IS SIX FILES. vm_basics was 3,047 lines and 117 tests
# when it was split by topic on 2026-09-08; the six share vm_expect.hpp, the
# whole-program assertion js_expect.hpp cannot express (a class body, a
# labelled loop, a statement). `ctest -R vm_` runs the family.
#
# Expressions: operators, coercion, literals, and what a value says it is. It
# is also the one file that still runs ctjs - `diff_vs_v1` sends an expression
# through the previous engine's interpreter as well - so the include
# directories below belong to this target alone.
ctbrowser_test(js/vm_operators)
# The differential half runs the same expressions through ctjs's old
# interpreter, whose public header also reaches its nested ctc dependency.
target_include_directories(ctbrowser-test-vm_operators PRIVATE
  "${CTBROWSER_CTJS}/include"
  "${CTBROWSER_CTC}/include")
# Functions: parameters, `this`, `arguments`, closures and the scopes they
# capture, and the function object itself.
ctbrowser_test(js/vm_functions)
# Objects and classes: fields, accessors, descriptors, prototypes,
# `new.target`, and the class-expression scoping that broke p5.js.
ctbrowser_test(js/vm_objects)
# Statements - loops, labels, switch, try/catch/finally, what an exception
# carries - and the handful of cases that drive the VM from C++ (the
# collector, a native binding, an error report), which need a `context` of
# their own rather than a program.
ctbrowser_test(js/vm_control_flow)
# The standard library: Math, Array, String, RegExp, JSON, Date, the
# collections, typed arrays over a shared ArrayBuffer, btoa/atob and
# structuredClone.
ctbrowser_test(js/vm_stdlib)
# Pending promises, `await`, the microtask queue and generators - everything
# that suspends and resumes. Every promise case reads its answer AFTER THE
# TURN, because a handler runs when the queue drains, not when it is attached.
ctbrowser_test(js/vm_async)
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
