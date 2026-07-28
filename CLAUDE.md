# CLAUDE.md — compile-time-browser (ctbrowser)

The assembly of the compile-time web stack: ONE HTML source (markup +
<style> + <script>). page.hpp hands the engine three constexpr
strings (html/style/script text, linear extraction from the NTTP);
the bricks' constexpr VALUE parsers prove them at compile time
(static_assert over cthtml::parse / ctcss::parse_value+query /
ctjs::vp::is_valid) and build them at startup, running against a
mutable DOM, the ctcss cascade, a block layout pass and an SDL3
window. (The type-level grammar paths were removed from all three
bricks 2026-07 — value parsers are the only path; builds are
grammar-bake-free and take seconds.) Namespace `ctbrowser`. **ONLY the project's std::embed clang
is supported, C++23 and up** — tools/clang-std-embed (fork:
alexios-angel/llvm-project branch std-embed; distributed via the embed
repo's clang-std-embed GitHub release, which CI and the build server
fetch). std::embed is load-bearing (assets.hpp); CMake FATAL_ERRORs
without __builtin_std_embed. No gcc/MSVC/stock-clang paths. **CMake +
Ninja is THE build** (Makefiles retired 2026-07-23). Work on `main`.
Prefer `rg`.

## v2 JAVASCRIPT (2026-07-25)

**The MDN breakout tutorial runs, unmodified** — `examples-v2/pong.cpp` loads
`examples-v2/pages/pong.html`, a byte-for-byte copy. `examples/fetchboard.html`
compiles too, and the 66 KB bundled `space-invaders.html` stops at exactly ONE
thing: a regex literal.

The compiler covers the language now: `+=` and friends, member/index `++`, real
`this`, `break`/`continue`/labels, `do..while`, `try`/`catch`/`finally`/`throw`
(VM handler stack, unwinds call frames), computed method calls (`a[m]()` keeps
its receiver), `for..of`/`for..in`, template literals, `switch` (with
fallthrough), `class` + `new` + `extends` + **`super`**, optional chaining,
spread (array and object), computed object keys, `delete`, `in`, `instanceof`,
the bitwise operators (ToInt32/ToUint32, so `-1 >>> 0` is 4294967295), and
`async`/`await`.

**Still rejected, each by name rather than mis-compiled**: `regex` (no regex
engine — this is what stops space-invaders), `yield`/generators, tagged
templates, object-literal get/set accessors. The comma operator is a ctjs
PARSER gap, not a compiler one.

**Functions are objects** — a closure carries a property table, which is where a
class keeps its statics, its `prototype` and the `__home` that makes `super`
resolve against the class a method was WRITTEN in rather than against `this`
(three-deep hierarchies recurse forever otherwise).

**Promises are SETTLED-ONLY**, like v1's: no job queue, no `new Promise(executor)`,
`then` runs its callback immediately. `async function` returns a settled promise
(`op::wrap_promise`, through a factory hook the standard library installs — the
VM cannot build a promise by itself). Enough for `await fetch(url)` and
`.then(r => r.json())`; NOT enough for code that depends on ordering between a
`then` and the statements around it.

**`===` compares STRINGS BY CONTENT** — it compared the NaN-boxed words, which
is right for objects (identity) and singletons and wrong for strings, since two
strings with the same characters are almost never the same allocation. So
`e.code === "Space"` was false for every event, `switch` on a string never
matched a case, and `indexOf`/`includes` could not find a string in an array.
`==` always did compare content, which is why the one page in the suite that
uses it (pong) worked and invaders did not. `NaN`, `Infinity` and `undefined`
are defined globals now too — `NaN` was an undefined global, so `NaN === NaN`
was TRUE.

**Standard library** is `src/script/builtins.cppm` — `Math`, `Array.prototype`
(incl. map/filter/reduce/sort, which call back into the VM via
`context::call`), `String.prototype`, `Number.prototype`, `Object` statics,
`JSON` parse/stringify, `Promise` (resolve/reject/all),
`parseInt`/`parseFloat`/`isNaN`/`String`/`Number`. Reached through **prototype
tables per value kind** (`context::set_prototype`) plus **per-object prototype
chains** (`class`/`extends`). `Math.random` is seeded and DETERMINISTIC by
default — the test story is byte-comparable goldens, and a page drawing with
random cannot have one otherwise.

Top-level `var` is a GLOBAL by design (pages define functions the host calls by
name), so the only frame-0 locals are for..of items and catch parameters — and
those ARE capturable, which is what makes `for (const x of xs) fns.push(() => x)`
close over each element at the top level.

`tests-v2/page_scripts` compiles the real example pages and asserts what each
one does; `tests-v2/vm_basics` has a test per language feature.

## v2 BUILD SPEED (2026-07-25)

Measured, then fixed. A clean v2 build was **143.7 s wall / 237.9 s CPU**; it is
now **116.7 s / 221 s**, and the parts a user waits on moved most:
`tests-v2` 221 s → 127 s, `examples-v2` 138 s → 36 s. A bare
`import ctbrowser;` with an empty `main` costs **0.89 s**, so the umbrella is
cheap — a consumer pays for what it USES.

Three things did it:

1. **lld.** The same executable links in 0.65 s against 4.31 s with the default
   `ld`, and v2 builds twenty-six of them. `CTBROWSER_V2_USE_LLD=OFF` opts out;
   `ctbrowser_v2_target()` is the one place that decides how v2 is built.
2. **Module implementation units.** `script/{vm,builtins}.cpp`,
   `shell/{app,net}.cpp` — interfaces DECLARE, `module X;` units DEFINE. Every
   TU that imported the engine used to re-instantiate and re-optimise it:
   `vm_basics.cpp.o` was 1.6 MB / 2698 symbols, of which `install_builtins` was
   21 KB and the VM's `run_loop` 15 KB.
3. **THIRD-PARTY HEADERS ARE NOT ALLOWED IN AN INTERFACE'S GMF.** This is the
   one to remember: a module's global module fragment is **serialized into its
   BMI**. `#include <boost/asio.hpp>` in `:net` made that BMI **27 MB**, and
   `<SDL3/SDL.h>` made `ctbrowser.app.pcm` 26 MB — for headers whose types
   neither module exposes. Moving them into the implementation units took
   net.pcm to 3.7 MB. Put Boost/SDL/FreeType includes in a `.cpp`, never in a
   `.cppm` interface, unless the type is genuinely in the public API.

**One archive:** `libctbrowser.a` merges all nine engine libraries (an `ar`
merge of the same objects, not a rebuild), so a non-CMake build links ONE file.
`CTBROWSER_V2_SINGLE_LIB=OFF` skips it; CMake users keep using
`ctbrowser::v2`, which also carries the include paths and BMIs an archive
cannot.

**The opt-out:** a modules project has no header-only mode, so the knob that
buys back what an all-inline engine gave is `CTBROWSER_V2_LTO=ON` — inlining
across the library boundary at LINK time rather than by recompiling the engine
in every TU. Off by default, because the default is meant to be fast to build.

`tools/check-package.sh` is what catches the other half of this: an exported
target that links `Freetype::Freetype` needs a matching `find_dependency` in the
installed config, and stage 6 shipped without one.

## v2 APPLICATION API (2026-07-25)

**`import ctbrowser;` + `ctbrowser::run_app(html, options)` is the whole
API.** One module, one link target (`ctbrowser::v2` in-tree,
`ctbrowser::ctbrowser-v2` installed), NO SDL header in the application. See
`examples-v2/counter.cpp` — 40 lines, most of it the page.

`run_app` owns the window, the event loop, the clock (it calls `tick()`, so
timers and rAF actually fire), vsync, fps pacing, screenshots and teardown.
`app_options` mirrors v1's: size, `logical_width/height` letterboxing,
`max_frames`, `max_fps`, `fixed_dt`, `screenshot_path`, `assets`,
`on_native_window` (the escape hatch — hands you the `SDL_Window*` as `void*`)
and `on_ready`. Env: `CTBROWSER_TEST_FRAMES`, `CTBROWSER_SCREENSHOT`,
`CTBROWSER_RENDERER` — which is how an example becomes a ctest with no test
code in it.

**SDL3 is OPTIONAL AT BUILD TIME.** `ctbrowser-app` always builds;
`CTBROWSER_WITH_SDL3` selects an SDL host or a headless one at runtime. Without
SDL3 the engine still renders and `run_app` still works.

**Installing:** `tools/check-package.sh` is the proof — installs v2 to a temp
prefix, builds `tests-v2/package/` against it via `find_package`. GLM and the
submodules are v1-only configure requirements now.

`tests-v2/api_surface` lints the claim: application sources must contain exactly
one `import ctbrowser;` and no SDL symbol, and the engine modules must stay
SDL-free.

## v2 IS THE ENGINE (stage 7, 2026-07-25)

`ctbrowse` (examples-v2/) is the browser: `ctbrowse page.html`, or
`ctbrowse page.html --headless out.ppm --size W H` with no display at all.
`ctbrowser.shell::browser` is the assembly and is SDL-FREE — `ctbrowser.app`
is the only module that knows SDL exists. A frame runs only what changed:
a scroll re-composites, an idle frame does nothing, a resize re-lays-out.

**`std::embed` is no longer required to configure.** `CTBROWSER_BUILD_V1` is
now `AUTO`: v1 builds where the compiler has `__builtin_std_embed` and is
skipped with a STATUS message where it does not. `cmake -S . -B build
-DCMAKE_CXX_COMPILER=clang++` on stock clang builds and tests v2 alone.
`-DCTBROWSER_BUILD_V1=ON` still hard-errors on the wrong toolchain, on
purpose — silently building something other than what was asked for is how
CI reports success for a target it never built.

**Script and HTML parsing are v2's own now.** `ctbrowser.shell:bindings`
gives pages `document`/element methods/events/timers/rAF (handles, not
`node *`, so a stale reference fails a lookup instead of corrupting memory).
`ctbrowser.dom:tokenizer` + `:treebuilder` replaced the cthtml wrapper with
the WHATWG algorithms — implied `<html>/<head>/<body>`, unclosed `<p>`/`<li>`,
table section inference, foster parenting, and the adoption agency. **v2 no
longer uses `external/compile-time-html`** (ctcss and ctjs's parser remain).

**Form controls and canvas 2D work.** `ctbrowser.shell:forms` holds control
state (value, caret, selection, checked) keyed by node_id — NOT on the node,
which is what left v1's `node` carrying thirty UI-only fields.
`ctbrowser.shell:canvas` is the 2D context, with its pixels in a store the
display list shares by `shared_ptr`. Replaced elements (`canvas`, `input`,
`button`, `select`, `textarea`, `img`) are `box_kind::replaced` and are sized
by `intrinsic_size_of`, not by their children.

A canvas draw marks `dirty::raster` — tiles are stale, the display list is
not — so an animation re-rasters without re-recording or re-laying-out.

**v1 IS NOT DELETED.** What it still has that v2 does not: the BabylonJS shim
and its software 3D rasterizer, textarea soft-wrap, and canvas gradients. See
`docs/v1-retirement.md`.

## v2 STYLE: the `style` attribute, with Chrome/Firefox precedence (2026-07-25)

Read at last — v2 saw `<style>` ELEMENTS only, so `<div style="height:2000px">`
laid out as one line. It is NOT a separate origin: author-level with a
specificity above every selector, which puts it in the cascade at

    normal selector  <  normal inline  <  important selector  <  important inline

so `engine::resolve` SPLICES the attribute's normal declarations in at the
importance boundary rather than appending them at the end. Appending is the
easy mistake and it is invisible until a page uses `!important` to override a
widget's inline style; `tests-v2/style_basics` has a test per step, verified by
planting the mistake and watching exactly those two fail.

Parsed through the SHEET parser wrapped in `*{...}`, not ctcss's declaration
splitter — the latter peels `!important` off and discards the flag, which is
the entire question. Cached by attribute TEXT, so a table styling forty rows
identically parses once and a re-resolve after a hover parses nothing.

## v2 TABLES AND GENERATED CONTENT (stage 7, 2026-07-25)

**`table_flow` is the third formatting context** the `LayoutAlgorithm` concept
was written for, and the one that justifies the concept: a table cannot be laid
out as blocks because a cell's width is not its own business — every cell in a
column shares that column's width, so the whole table must be MEASURED before
any of it is placed. That is exactly the `measure`/`arrange` split. Auto column
sizing (widest natural content), rows through transparent
`<thead>`/`<tbody>`/`<tfoot>`, cells stretched to the row height, and a stated
width scales the columns rather than being ignored. `<table width=400>` works:
**presentational attributes** (`width` on table/td/th/col) map to the CSS
property at the bottom of the cascade, which is how a lot of existing HTML
sizes a table.

Finding it turned up a real bug in `block_flow::measure`: a TEXT child was
handed to `inline_flow::measure`, which measures a box's CHILDREN — and a text
box has none, so **every block whose content was text measured as zero wide**.
Nothing noticed until a table asked how wide its columns wanted to be.

**Generated content** — list markers (`<ol>` numbers counted among siblings,
`<ul>` bullets) and `<summary>` disclosure triangles — is drawn by the recorder
in the gutter the UA sheet's `padding-left` already reserves. There is no
element behind it, so it is drawn rather than laid out; the ordinal and the
open/closed state are decided in the box builder, which is the only place that
knows what the siblings and the parent are.

**HTML WHITESPACE COLLAPSES** — every run of space/tab/newline is one space,
except under `white-space: pre` (which the UA sheet now gives `pre` and
`textarea`, and which INHERITS). v2 never did this: the newlines in a page's own
SOURCE went straight to the rasterizer. font8x8 drew nothing for them so nobody
noticed for six stages; a real font draws `.notdef`, which is a BOX, and a page
full of boxes is what finally showed it. It also broke wrapping — the wrap
splits on `' '` alone, so two words joined by a newline were one unbreakable
word and the line ran off the end of its box.

**`</table>` NEVER POPPED THE TABLE.** `close_element` refuses to unwind past a
special element looking for its match — and the implied `<tbody>` is special, so
the search stopped there and the table stayed open. Everything after it was then
foster-parented BEFORE it: two consecutive tables came out in reverse document
order and the `<p>` after them ended up inside the second one's `<tbody>`. The
guard now lets table structure be unwound when closing table structure.

**A table is BLOCK-level.** Left out of `is_block_level()` it shared a line with
whatever came before, so two tables sat side by side and a table sat beside the
link above it.

**Inline text of different sizes shares a BASELINE.** Every item on a line is
placed so `y + ascent` is the same — which is what sharing a baseline MEANS, and
what the rasterizer then draws. Aligning boxes is only right when every item has
the same metrics: tops made `<big>` hang above its neighbours, and bottoms are a
box's descent below the baseline, which two faces do not share. A REPLACED item
sits ON the baseline (its ascent is its whole height), so an image in a line of
text does not sink into the descenders.

That needed the font's real ascent in LAYOUT, so `measure_text_fn` is now a
`text_metrics` struct — measure, ascent, descent — bundled because they travel
together through every formatting context. It is still callable directly, so a
measurement reads as it did. `shell::metrics_for(font_backend)` is the adapter,
and it lives in the SHELL because it must name both a layout type and a raster
one, and neither may name the other: paint and raster sit downstream of layout.

**font8x8 hides this bug**: it quantises 13px, 16px and 19px to the same 8x8
cell, so all three have the same ascent and every alignment looks identical.
`tests-v2/chrome_basics` therefore tests it with REAL fonts, and the test is
verified against BOTH wrong alignments — top and bottom each fail it.

**`white-space: pre` breaks lines.** A preserved newline is a LINE BREAK, not a
character; handing it to the rasterizer draws `.notdef`, which is why a `<pre>`
block was full of boxes.

**Table parts are `display: block`** in the UA sheet. Left inline, `normalise`
wraps them in anonymous boxes, and the table formatting context then looks for
its rows and its caption among children that are no longer them — the
`<caption>` simply vanished. `<caption>` lays out above the grid;
`<table border=N>` frames the table AND each cell, which is what the attribute
has always meant.

**The scrollbar is its own non-scrolling LAYER**, not a paint into the page —
the compositor already knows how to hold a layer still, so a scroll moves the
page layer and leaves the bar where it is without re-recording anything. Thumb
drag (with a grab offset, so it does not jump to centre itself), track clicks
that page towards the pointer, and a click on the bar never reaches the page.
A tall page LAYOUT IS RUN TWICE: content laid out at the full width would run
under the bar. That terminates because narrowing a page can only make it
taller, so a page that overflowed still overflows. `browser_options::
scrollbar_width = 0` hides it, which is what a fixed-size game wants.

**PAGE-LEVEL TEXT SELECTION.** A position is `(node, code point IN THAT NODE)`,
not a fragment pointer: a node's text is split across as many fragments as it
has visual lines, and a relayout rebuilds every one of them — a selection has to
survive a window resize, and there is a test that resizes and compares the text.

The GLYPH GEOMETRY is not stored on the fragment. It is derived on demand from
the same measure layout used, which costs a few measurements per click and left
the fragment tree exactly the shape it was — the plan called publishing per-line
glyph geometry the item most likely to spill, and this is why it did not have
to.

**A wrap DROPS the space it broke at**, so the fragments do not partition the
node's text. Summing their lengths puts every position past the first line one
character early; offsets are found by SEARCHING the node's text instead. For the
same reason `selected_text()` extracts from each node's own text over the whole
selected range rather than joining the runs — joining silently deletes one space
per line from anything copied off a wrapped paragraph.

**Browser chrome is a stack of non-scrolling LAYERS** — the scrollbar, an open
`<select>`'s option list, the context menu — rebuilt together by
`record_chrome()` and invalidated with `discard_layer()` so the page's tiles
survive. A `<select>` opens on click, closes on Escape or a click anywhere else,
and the click that closes it does NOT reach the page.

**Right button, context menu, clipboard, cursors.** `input_event` numbers
buttons the way the DOM does (right = 2); SDL numbers from 1 and calls it 3, so
passing SDL's number through made a right-click look like button 3, which
nothing was looking for. The menu is Copy/Cut/Paste/Select All, and the page
gets a CANCELABLE `contextmenu` first — `preventDefault` suppresses ours.
Ctrl+C/X/V do the same verbs. The clipboard is TWO HOOKS on the browser, filled
in by the app layer from SDL; without them copy and paste still work within the
page, which is what makes the whole path testable headlessly.
`browser::cursor_at()` returns a NAME (`pointer`, `text`, `default`) so the
engine needs no cursor vocabulary and the app maps it to a system cursor.

**`<select>` shows its option.** It drew an EMPTY RECTANGLE before — it passed
an empty string as the label and never read `<option>` at all. Now: the
`selected` option, else the first, else whatever the user picked, plus a
drop-down arrow. The popup itself is still missing.

## FORMATTING (2026-07-27)

**`tools/format.sh`**, and `--check` in CI on its own runner. `.clang-format`
is **LLVM with five deviations**, and the deviations are not preferences - they
are what the repository was measured to already be: tabs (30,000 tab-indented
lines against 2,600), 100 columns, `const rect & box` (881 against 16), one-line
`if (x) { return; }` (1,057 of them), and unindented namespaces. Stock LLVM
would rewrite every line; the point of a formatter is to be a no-op on code
that is already right.

Two settings are non-obvious and were both wrong on the first pass:
`AllowShortIfStatementsOnASingleLine` must be `WithoutElse`, not `Never` -
`AllowShortBlocksOnASingleLine` governs the block but the `if` is governed
here, and `Never` overrules it - and `AccessModifierOffset` must be **-4**,
since LLVM's -2 assumes a 2-space indent. Getting those two right halved the
diff, from 14,394 lines to 7,065.

**What is NOT formatted is in `.clang-format-ignore`**: generated files
(font8x8, entities, the SPIR-V blobs), vendored ones (stb), submodules and the
fetched toolchain. Formatting a generated file makes "the generator changed"
and "the formatter ran" indistinguishable in a diff.

Sanitizer suppressions grew alongside: the one test that drives `run_app`
initialises SDL, which reaches libdbus (a lock-order inversion TSan reports)
and leaves EGL allocated (a leak LSan reports). Both suppressed BY LIBRARY in
`tests-v2/{tsan,lsan}.supp`, and both files say they were verified by planting
a fault in our own code and confirming it is still caught - which was actually
done, for the leak, in the commit that added it.

Two repository problems the formatting turned up, neither of them formatting:
**`build-timing/` was committed** - 457 files, 408 MB, from a `git add -A` -
and is untracked now, though the history still carries it. And **the goldens
were never tracked at all**: `*.ppm` in `.gitignore` swallowed
`tests-v2/golden/`, so every golden test would have failed on a fresh clone
with "no golden". There is an exception for them now.

## v2 EDITING, DISABLED, AND THE COLLECTOR (2026-07-27)

**A control drew in a DIFFERENT FACE from the one it measured.** `into.text()`
was called without a face, so every control's text came out in the default
serif while the caret was measured with the element's own — and a textarea is
monospace by UA rule. The caret ran ahead by the difference on every character,
which reads as a gap that grows as you type. `paint_face_of()` is the
conversion and every control text draw goes through it.

**A field's geometry lives in ONE place** (`layout_of_field`): the inset, the
line height, and where each line begins in the value. The painter draws from it
and a click is mapped through it, so a caret cannot land where the glyphs are
not — two copies of "where does line 2 start" is how a click ends up putting
the caret somewhere the text never was.

That is what made these possible at all, and none of them existed before:
**clicking places the caret** (a textarea had no path from a point to an
offset), **dragging selects** inside a field and Ctrl+C copies exactly that,
**up and down move by VISUAL LINE** keeping the column — as a distance, not a
character count, since two lines of a proportional font do not share character
positions — and **Home/End are that line's ends**, not the whole value's.

**Escape drops a field selection and so does clicking away.** Ctrl+A left the
whole value highlighted forever otherwise; a highlight in a field nobody is
typing in reads as still selected.

**`type=password` shows BULLETS**, masked per CODE POINT so a value with
anything non-ASCII in it does not come out with three bullets for one
character. Measurement uses what is SHOWN — a bullet is wider than most
letters, so measuring the letters puts the caret inside them.

**`disabled` did nothing at all** — not visually, not behaviourally. A disabled
control is greyed (and ignores `color` from the cascade: greyed-out is the only
signal the control is dead), takes no focus, activates nothing and dispatches
nothing. Inherited from an enclosing `<fieldset>`, which is how a form greys a
whole section.

**A radio is ROUND.** `paint_op::fill_ellipse` exists for it, antialiased at
the edge, because a hard-edged circle 13 pixels across looks like a polygon.
Drawn as squares a radio and a checkbox are indistinguishable, and the shape is
what tells you one of them is exclusive.

**`<a href>` reaches the SYSTEM BROWSER.** `app_options::on_navigate` gives the
application first refusal — `ctbrowse` loads a local `.html` that way — and
anything it does not claim goes to `SDL_OpenURL`. It is an app_options hook
rather than a browser one because setting `browser::set_navigate_hook` directly
REPLACES the fallback, which is how `ctbrowse` silently swallowed every
external link it was handed.

**THE COLLECTOR NEVER RAN.** Not rarely — there was no automatic trigger at
all, so a document accumulated every object it ever made. It could not simply
be switched on: the DOM bindings hold every listener, every timer callback and
every element wrapper in C++ containers the collector cannot see, and the VM's
own per-function string cache is not in any of its root structures either. Both
are roots now (`context::set_external_roots`), and `collect_if_due()` runs
between callbacks on a growth threshold. Verified by removing the external
roots and watching the test **crash** — which is what a page dispatching a
freed listener does.

**`std::clock()` is WALL TIME on some Windows runtimes.** Walked into twice, so
`ctbrowser.core:cpu_time` is the one portable `process_cpu_seconds()` and both
the profiler and the idle-pool test use it.

## v2 CPU AND THE PROFILER (2026-07-27)

**`CTBROWSER_PROFILE=out.csv CTBROWSER_PROFILE_SECONDS=10 ./widgets.exe`** — a
record per loop iteration (poll / tick / frame / present / asleep, layouts,
whether it drew), a summary on stdout, and **CPU time against wall time**,
because "it uses 65% of my CPU" is not the same question as frames per second.
`tests-v2/bench_interaction` is the headless half: what a mouse move, a hover
change and a scroll each COST, with the implied CPU at 60 fps printed beside
them.

Measuring first was worth it — every guess was wrong.

**The pool's idle poll was not the problem** (it looked like the obvious one: a
worker waking every millisecond on every core, forever). It is fixed anyway —
idle workers now sleep on a global "is there work anywhere" condition, since
`submit` notifies one queue and a worker finds STEALABLE work by looking — but
it measured well under 1%.

**Nor was the engine.** A hover change is 0.9 ms and a scroll 0.5 ms; 60 fps of
hover changes is about 5% of a core.

**It was that the loop never stopped drawing.** An idle page redrew because
nothing distinguished "nothing happened" from "nothing was asked for", and a
busy page ignored `max_fps` entirely.

**An idle application now BLOCKS.** `browser::needs_frame()` and
`next_wakeup_ms()` are the contract: the loop asks the page whether anything
changed and how long until it next has something to do on its own — a timer, an
animation frame, the caret's next blink — and blocks on the event queue for
exactly that long. Idle widgets.exe went 0.8% → **0.2% of one core**, and a
caret blinks ON TIME instead of whenever the next event happens to arrive,
which was the same shape as the scrollbar that did not appear until you moved
the mouse.

**`max_fps` IS the throttle**, and it did nothing before: pacing asked "is
there more to draw", which is false the instant a frame finishes, so every
frame took the wait branch and an animating page ran at whatever vsync allowed.
It asks "did we draw" now. Measured on pong.exe with a real window: 60 fps =
12.8%, 30 = 7.5%, 15 = 3.9%. `CTBROWSER_MAX_FPS` sets it without a rebuild.

**`SDL_WaitEventTimeout(&event, ...)` then `SDL_PushEvent` is not a wait.** SDL
posts an internal poll sentinel to bound PollEvent loops, and pushing that back
re-arms it, so the wait returns instantly forever - eight million iterations in
ten seconds, and a 306 MB profile. Pass **NULL** to wait without taking the
event. The profiler's history is capped for the same reason: a profile of a
loop that has gone wrong is exactly when it explodes.

## v2 FORM CONTROLS: the batch a real page found (2026-07-27)

Eight bugs from ONE screenshot of the widget gallery, and the reason they all
shipped is at the bottom of this section.

**PER-SIDE LONGHANDS DID NOTHING.** Layout read the `padding` and `margin`
SHORTHANDS and nothing else, so `padding-left: 18px` was parsed, cascaded,
resolved — and ignored. The UA sheet's own `ul { padding-left: 40px }` and
`summary { padding-left: 18px }` were dead, which is why a disclosure triangle
was drawn on top of its own label. A shorthand is now EXPANDED into four
longhands **when it is recorded**, not where it is read: the four carry the
shorthand's source order, so a longhand written after it wins and one written
before it loses. Reading "shorthand, then longhand if present" gets that
backwards.

**A whitespace-only text node is not always nothing.** Between two inline-level
boxes it collapses to one SPACE and is RENDERED — `</label> <input>` is a
label, a space and a field. It was dropped outright, so every label was glued
to its control. It IS nothing between blocks and at the start of a line;
`drop_collapsible_spaces` decides that once the siblings are known.

That exposed a second one: **`words_that_fit` could never consume a leading
space**, so a text run that IS a space fit nothing and forced a line break —
which put every label on its own line above its control.

**A BUTTON IS NOT A SELECT.** They shared one painter arm, so a button was
asked for its selected `<option>` — it has none, so the label came out empty —
and then got the drop-down arrow anyway. Every button was an empty box with an
arrow in it. `button_label()` already existed and was never called.

**The caret was measured with font8x8 while the text was drawn with the real
face**, so it sat about twice as far along as the text — reading as a caret
that jumps a character ahead of every keystroke. font8x8 hides this too: it
quantises every size to one cell, so the two measurements agree there.

**A textarea drew its whole value as ONE run.** A caret on the second line
landed the width of the first line past the box, where the clip threw it away
— which is why a textarea appeared to have no caret at all — and the newline
reached the rasterizer as a glyph. Both are per-line now.

**The caret BLINKS** in Chrome's 500 ms halves, measured from the last caret
ACTIVITY: typing, moving and clicking restart it solid, because a caret that
blinks out from under the character you just typed reads as a dropped
keystroke. A blink re-PAINTS and does not re-lay-out — `browser::layout_count()`
is observable so a test can hold that line.

**`<details>` had no behaviour at all**: a closed one laid out its contents (so
nothing was ever hidden) and clicking `<summary>` did nothing. The state is the
`open` ATTRIBUTE, so a script reading it agrees with what the user did. A
closed details' non-summary children are built away in the box builder, which
cannot be a UA rule — `details > :not(summary)` needs a selector the cascade
does not have, and the state lives on the PARENT.

**A wrapper's properties were a SNAPSHOT** taken when it was made, and no
element had `value` or `checked` at all. A page that does `const c =
getElementById('color')` and later reads `c.value` — which is every page — read
the page-load value forever; the gallery reported `color: undefined`. There is
now ONE wrapper per element (so `getElementById('x') === getElementById('x')`,
which a browser guarantees and this did not), refreshed before every dispatch
and every frame. The VM has no property accessors, so a live property is a
SYNC rather than a getter: what the page wrote wins, else the control's state
does.

**A `<select>`'s value is its selected OPTION's**, and value is not label —
`<option value=g>green</option>` is worth "g" to a form and shows "green" to a
reader. The store seeds from the attribute and the painter maps it back for
display.

**`getAttribute` returned null for a present-but-EMPTY attribute**, which is
every boolean one: `<details open>`, `<input disabled>`, `<option selected>`.

**The VM keyed its string cache by INDEX INTO THE CURRENT PROGRAM.** A context
can run more than one — `browser::run_script` evaluates a snippet that calls a
function the page defined — so the running frame's proto belongs to a different
program, and subtracting its address from the wrong program's vector gave a
garbage index and a SEGFAULT. Keyed by the function now.

**Why all eight shipped: the example ctests only checked that the process
exited 0.** A page renders with empty buttons, no caret, a `<details>` stuck
open and every label glued to its control and still exits 0. `v2-render-widgets`
and `v2-render-elements` now byte-compare the render against
`tests-v2/golden/`, with `CTBROWSER_FONTS=font8x8` so the golden pins LAYOUT
and does not move when FreeType does. Both goldens are byte-identical from the
Windows exes.

## v2 NAVIGATION: alert, location, `<a href>` (2026-07-27)

The last three things v1's script surface had and v2's did not — and the proof
is that **MDN's breakout now survives its own game over.** It ends by calling
`alert("GAME OVER")` and then `document.location.reload()`; both were undefined
identifiers, so the one page in the suite that proves web compatibility died at
the exact point every other test had stopped looking.

**`location.reload()` cannot reload the page it is called from** — the reload
tears down the script context and the program still running inside it. It
records the request; `tick()` drains it BETWEEN callbacks. Same as v1.

**`document.location`, `window.location` and the global `location` are ONE
object**, not three copies — a page reads whichever it learned. `href`/`hash`
are written THROUGH on every navigation: setting them once when the object was
built made them a page-load snapshot, so a page could never read a link it had
just followed.

**Alerts are recorded on the BROWSER, not on the bindings.** A reload replaces
the bindings, and the alert that caused the reload is exactly the one you still
want to read. `set_alert_hook` is the app's modal (`SDL_ShowSimpleMessageBox`);
without a hook the messages are still recorded, which is what makes alert
testable headlessly — the clipboard's design.

**`<a href>` takes the nearest `<a>` ANCESTOR**, since what gets clicked is the
link's text. A `#fragment` is not a navigation: it scrolls this document and
lands in `location.hash`. Everything else goes to `set_navigate_hook` — the SDL
app hands it to the system browser, and `ctbrowse` loads a local `.html`,
because deciding what a relative URL means is a BROWSER's business and the
engine has no idea what a URL is.

## v2 FONTS: real ones (stage 6, 2026-07-25)

**Text is drawn with outline faces.** `ctbrowser.raster:ttf` is a `font_backend`
over **SDL3_ttf** — the one place the engine knows about SDL, and a deliberate
exception rather than an oversight. `TTF_Init` needs no video subsystem, so real
text is still TESTABLE with no display, which is what makes the exception safe.
`tests-v2/api_surface` SWEEPS `src/` and names the exceptions; the old
hand-written list could not catch a new file that used SDL, and did not.

The Windows cross-build links a **static** SDL3_ttf with the **full stack** —
FreeType, HarfBuzz and plutosvg — built by `../llvm-mingw/build-sdl3.sh`. That
matters beyond features: without HarfBuzz the same page KERNS DIFFERENTLY, so
the Windows renders stopped matching the Linux ones until HarfBuzz 14.2.1 (the
version a linuxbrew host has) was on both sides. All six examples are
byte-identical across platforms again.

`PKG_CONFIG_LIBDIR` is pinned to the sysroot in the toolchain file:
`CMAKE_FIND_ROOT_PATH_MODE_*` governs find_package, but **pkg-config is a
separate program with its own search path**, and SDL3_ttf's config resolves
HarfBuzz through it — it found the HOST's and put `-L/home/linuxbrew/...`,
`-lglib-2.0` and `-lgraphite2` on a Windows link line.

The seam is `raster::font_backend`: `advance()`, `draw_run()` and `ascent()`
together, because those are the ones that must agree — layout measures with the
first and the rasterizer draws with the second, and text lands where layout
thought only if ONE object answers both (`browser::fonts()` / `measure()`).
`renderer::set_fonts()` hands it to both raster backends. `font8x8` is still an
implementation of the same interface and still the default, so **the goldens do
not move**.

Font identity now runs the length of the pipeline: layout resolves
`font-family` (first name of the list, unquoted), `font-weight` (≥600 is bold),
`font-style` and `text-decoration` with the inherited-resolver pattern;
`paint_command` carries the face and decoration because the rasterizer has no
cascade to ask; underline and line-through are drawn as bands whose thickness
follows the size. `layout::text_face` and `paint::font_face` are deliberately
separate types — `:values` depends on nothing, and layout importing paint would
invert the dependency the pipeline is built on.

**Opt in with `browser::use_real_fonts()`**; `run_app` does it by default
(`app_options::real_fonts`, `CTBROWSER_FONTS=font8x8` to force the bitmap font).
It loads the vendored OFL faces (Tinos/Fira Sans/Cousine → serif/sans-serif/
monospace) through the ASSET REGISTRY, so a binary that baked them in never
touches the disk, and then the page's own `@font-face` files. An unknown family
falls back to the default face, and a missing bold/italic variant to the
upright one.

**The glyph cache is the only shared mutable state in the text path** — tiles
raster in parallel and an FT_Face is not reentrant — so it is mutex-guarded and
`tests-v2/fonts_basics` drives it from twelve threads on COLD glyphs. Going
through the browser does not test it: layout measures every run before raster
draws it, so the parallel path only ever reads. Removing the lock and watching
TSan stay silent is what showed that up.

Rendering turned out to be byte-identical between FreeType 2.14.3 (linux) and
2.13.3 (the mingw sysroot) for these faces — every example matches across
platforms. That is not guaranteed in general, which is what `CTBROWSER_FONTS`
is for.

## v2 INPUT: the page gets the events (2026-07-25)

**Keys and the pointer reach SCRIPT, and the browser's own behaviour is the
DEFAULT ACTION.** `handle_key` dispatches `keydown` first and only scrolls or
moves a caret if no listener called `preventDefault`. Before this the browser
consumed keys itself and never told the page, so a game could register a
`keydown` listener and receive nothing, forever, with no error — Space scrolled
the document instead of firing.

Dispatched now: `keydown`, `keyup`, `mousemove`, `mousedown`, `mouseup` (plus
`click`, which already worked). Events carry what pages actually read —
`code`/`key`/`shiftKey`/`ctrlKey`, and `clientX`/`clientY`/`button`.

**`input_event::key` IS the DOM `code`** ("ArrowLeft", "Space", "KeyA",
"Digit1", "Enter") — one vocabulary, not a private one translated at the edge.
The private one ("Left", "Return", and a "SelectAll" no keyboard produces) was
invisible to pages, which compare against `e.code`. `dom_key_value` derives the
DOM `key` from it (shift-aware: `KeyA` → "a" or "A").

**A letterboxed page keeps its logical size.** SDL announces the window's pixel
size on the first frame, and taking that as a page resize widened a 320x240
game's viewport to 960x720 — leaving the canvas, which is 320x240 by its own
attributes, drawn into a ninth of the page. `app_options::logical_width/height`
now pins the viewport; without them a resize still reflows, which is what a
document wants.

Three things the SDL layer was missing and now has:
- **`SDL_EVENT_KEY_UP`** — `input_kind::key_up`. Without a release every held
  key sticks down forever, so a paddle that starts moving never stops.
- **letters and digits** — the old table had FIFTEEN entries and no letters, so
  a WASD page got nothing: `translate()` returned false and the event was
  dropped before the browser saw it. Keyed by SCANCODE, since `code` is defined
  as the key's position.
- **`SDL_ConvertEventToRenderCoordinates`** — window coordinates are not page
  coordinates under letterboxed presentation. invaders is 320x240 in a 960x720
  window, so every pointer event arrived at three times its true position.

`tests-v2/bindings_basics` drives all of it, and finishes by holding a key
through MDN's breakout and asserting the frames differ — with a key the page
ignores as the control, so "the frames differ" cannot pass by nondeterminism.

## v2 RESOURCES: assets, images, fetch (2026-07-25)

`ctbrowser.shell:assets` is the registry every load goes through — an
application seeds it from `app_options::assets`, and a miss falls back to the
filesystem (cwd → `asset_path` → two levels up, v1's probe order). Registry
FIRST is the whole design: a binary that ships its resources works from any
directory, and a test that seeds the registry is hermetic.

`ctbrowser.shell:images` decodes BMP (24/32bpp, either row order) into
`paint::bitmap` with no library at all; **SDL3_image is optional** and arrives
as a decoder hook installed by `ctbrowser.app` — the only place SDL and images
are allowed to meet, since the shell stays SDL-free. `<img>` sizes itself from
the decoded bitmap unless width/height say otherwise (one attribute scales the
other through the aspect ratio); a missing image is zero-sized, not a broken
icon. `loadImage`/`imageWidth`/`imageHeight` and `ctx.drawImage` (3-, 5- and
9-argument forms) take either a handle or an `<img>` element.

`ctbrowser.shell:net` is **real HTTP over Boost.Asio** (header-only, so the
compiled-Boost rule holds; a `CTBROWSER_ASIO_STANDALONE` switch selects
standalone Asio instead). Redirects, chunked bodies and a deadline on every
operation. **https:// needs OpenSSL** — optional, and without it the build says
so by name rather than failing to connect. `fetch(url)` consults the registry
first, then the network when `app_options::network` allows it
(`CTBROWSER_NETWORK=0` turns it off, which is how an example's ctest stays
hermetic). A network failure REJECTS; a 404 resolves with `ok` false.

**Sound** is `playSound(name [, volume])`, installed by `run_app` through
`browser::define_native` (the embedder hook — the shell has no SDL and the
`<audio>` element does not exist). WAV only, mixed by SDL3's own audio streams;
no SDL3_mixer, and a build without SDL3 makes it a no-op returning false.

**Requests BLOCK the frame** — promises here are settled when they are made, so
`await fetch(url)` must have the bytes by the time fetch returns. That is the
honest cost of the settled-promise subset, and it is why the registry is
consulted first. `tests-v2/net_basics` proves the client against a loopback
server it stands up itself; no test in the suite touches the internet.

Examples: `invaders` (sprite sheet through the 9-argument `drawImage`, keys via
keydown/keyup, `requestAnimationFrame`, sound) and `fetchboard` (a baked-in
resource AND a live HTTP GET) are ported. Both pages were rewritten off v1's
`onFrame`/`isKeyDown`/`getContext(id)` shorthand onto the real web APIs.

## v2 GPU: Linux binaries here see no adapter — WINDOWS ONES DO (2026-07-25)

`src/gpu` (SDL3 `SDL_GPUDevice`) builds and RUNS under this WSL2, but the only
Vulkan ICD that survives loading is **lavapipe** (`lvp_icd.json`) — every
hardware ICD is dropped with "not having any physical devices". `/dev/dxg` and
`/usr/lib/wsl/lib/libd3d12.so` exist, but no `dzn`/`d3d12` Vulkan ICD bridges to
them. `SDL_GetGPUDeviceDriver` says "vulkan" either way — the adapter name
(`SDL_PROP_GPU_DEVICE_NAME_STRING`, exposed as `sdl_gpu_backend::adapter()`) is
what tells you, and `adapter_is_software()` checks it.

**The cross-compiled .exe sees the real GPU.** Run under WSL interop,
`build-windows/src/tests-v2/ctbrowser-v2-gpu_basics.exe` selects
**`Intel(R) Arc(TM) Graphics`** and its render matches the software one exactly
(0 of 120000 pixels differ). So GPU **correctness** is verifiable both ways, and
GPU **performance** numbers must come from the Windows build — `bench_gpu`
prints a loud banner on Linux here because its numbers would be two CPU
implementations racing. Headless GPU runs need `SDL_VIDEODRIVER=offscreen`;
`dummy` has no Vulkan surface support and fails device creation outright.

## Windows cross-build (2026-07-25)

`cmake --preset windows -DCTBROWSER_BUILD_V1=OFF && cmake --build --preset
windows && cmake --build --preset windows --target windows-dist-v2` →
**`examples-windows-v2/`** (its own directory: four example names collide with
v1's `examples-windows/`). It carries the exes, SDL3.dll and the pages/assets
the examples load, laid out repo-relatively so the exes work from its root.
The exes import **only SDL3.dll + the system UCRT** — no libc++, no libunwind.

**The exes are SELF-CONTAINED — no SDL3.dll.** `../llvm-mingw/build-sdl3.sh`
builds SDL3 and SDL3_ttf as STATIC libraries into the toolchain's own
`<triple>/` sysroot (run on the devbox, artifacts rsynced into
`tools/llvm-mingw/`), and the toolchain file puts that sysroot FIRST on
`CMAKE_FIND_ROOT_PATH` — which it must also be ON, or `find_package` escapes to
linuxbrew's ELF SDL3 and fails with "IMPORTED_IMPLIB not set". libsdl's official
mingw devel package (`~/projects/sdl3-mingw`) is the fallback, and a build that
lands there ships the DLL. `CTBROWSER_SDL3_STATIC=OFF` forces it.
`ctbrowser_pick_sdl_target()` chooses `SDL3::SDL3-static` over
`SDL3::SDL3-shared` and tells `windows-dist-v2` whether a DLL has to travel.
Cost: 3.5 MB → 7.2 MB per exe.

Toolchain, all fetched rather than built: llvm-mingw std::embed release
(`tools/llvm-mingw/`, 84 MB) and **Boost as an isolated include dir**
(`~/projects/boost-inc/boost` symlinked at the host's) — there is no BoostConfig
for the cross target and none is needed, since v2 links `Boost::headers` and
nothing else. The toolchain file finds it the same way it finds GLM's.

Degrades as designed: no OpenSSL for mingw → `fetch` does http:// only and says
so; no SDL3_image → `<img>` reads BMP only. Asio needs `ws2_32`/`mswsock`, which
nothing links implicitly.

**Verified**: all 19 v2 tests pass as Windows binaries WITH NO DLL BESIDE THEM
(gpu_basics.exe failed that way before), the five renderable examples produce
screenshots BYTE-IDENTICAL to the Linux ones, and counter.exe runs alone in an
otherwise empty directory.

**Running a Windows exe from WSL needs `WSLENV`** or none of the
`CTBROWSER_*`/`SDL_*` environment variables reach it — and the flag is
`/w` (Win32 invoked from WSL), not `/u`:
`WSLENV=CTBROWSER_TEST_FRAMES/w:CTBROWSER_SCREENSHOT/w:SDL_VIDEODRIVER/w`.
Without it the app opens a real window and never exits, because it never sees
the frame cap.

## ⚠️ Working environment & in-flight work (READ FIRST — 2026-07-22)

**Heavy builds go on the shared devbox; grammar-free ctbrowser now
builds fine locally** (the old OOM risk died with the bricks' grammar
bakes). `rsync` from `/mnt/c` into the server is flaky (symlink +
DrvFs). The devbox
(github.com/alexios-angel/infra, sibling checkout `../infra`) replaced the old
per-project build server: 8 vCPU / 32 GB, Ubuntu 24.04, apt toolchain (GLM,
cmake 3.28, LLVM 18 suite), **no SDL3** (so examples skip there). It
**deallocates itself after 30 idle min** — `../infra/azure-build-server/
server.sh start` wakes it (lifecycle: `server.sh
{start|stop|status|ip|ssh|ssh-config|allow-ip}`; ssh timeout after a network
change = your IP rotated → `server.sh allow-ip`). Reach it as `ssh devbox`
(alias written by `server.sh ssh-config`, IdentityAgent included). After a
local reboot the SSH agent is gone: `ssh-agent -a ~/.ssh/build-agent.sock &&
SSH_AUTH_SOCK=~/.ssh/build-agent.sock ssh-add ~/.ssh/id_ed25519` — the
`devbox` alias finds the sock by itself after that.
**Clean clones live at `~/projects/` on the box** (`compile-time-browser`
with submodules init'd + clang toolchain installed, and `embed`) — ssh in and
work there directly, or sync this tree with `./tools/remote-build.sh
[target]` (converges the pinned clang-std-embed toolchain + glm, then
runs the CMake `default` preset in `~/projects/compile-time-browser`).

**Windows cross-builds are CMake presets**: `windows` / `windows-fetch`
+ `cmake/toolchain-windows-x86_64.cmake` (llvm-mingw std::embed clang,
SDL3-devel mingw package, isolated GLM dir - env LLVM_MINGW /
SDL3_MINGW / GLM_INC override the ~/projects/* defaults; -static rides
CXX flags so PCH predefines match; SDL3 links via the import lib's
full path so -static leaves it dynamic). `windows-dist` collects
exes + SDL3.dll into examples-windows/. `./tools/remote-build.sh
windows` runs the whole thing on the devbox and rsyncs the exes back.

**Makefile retirement: DONE (2026-07-23).** CMake+Ninja is the sole
build in all 4 repos. The old findings all landed: GLM find_path on the
build interface, the __builtin_std_embed probe runs with
CMAKE_REQUIRED_FLAGS=-std=c++23, CTBROWSER_WARNING_OPTIONS carries the
strict flags (tests/examples/pch-anchor - the anchor MUST share them or
gcc-style predefine checks reject the PCH), space-invaders.inc
generates, babylon-model gets its fetch-allow under
CTBROWSER_EXAMPLES_FETCH (preset `fetch`). CI = cmake+ninja with apt
ninja-build + libglm-dev. remote-build.sh drives the presets.

## Build & test
```bash
git submodule update --init --recursive    # three bricks + nested ctc
cmake --preset default && cmake --build --preset default && ctest --preset default
# preset `fetch` = same + CTBROWSER_EXAMPLES_FETCH=ON (compile-time HTTP)
# examples build when SDL3 is found; tests are always headless
```
Flags: `-O2 -pedantic -Wall -Wextra -Werror -Wconversion`. Tests are
EXECUTABLES, SDL-free, headless. Examples need SDL3 (linuxbrew's here;
`find_package(SDL3)`).
CMake shares one PCH via the `ctbrowser-pch-anchor` target (REUSE_FROM).

## Tooling (build-time preprocessors, not compile-time)
- `tools/html-to-inc.py` — HTML → raw-string `.inc` for `#include` as a `page<>` NTTP (pong).
- `tools/js-bundle.py` — **compile-time ES MODULE BUNDLER** (ctbrowser's Vite/rollup step). ctjs runs ONE script in ONE global scope with no module system, but real apps are ES modules pulling npm symbols. Given an entry HTML with `<script type=module src=…>`, it resolves the whole import graph, strips import/export, maps bare specifiers onto ctbrowser globals (`@babylonjs/core`→`BABYLON`, `@babylonjs/gui`→`BABYLON.GUI`, `@babylonjs/loaders`→dropped), canonicalises `export default` to the importers' name (no duplicate `const` in the shared scope), topo-orders modules (deps first, entry last), and emits ONE self-contained HTML (stylesheet `<link>`s incl. `.scss` via the `sass` CLI inline as `<style>`). NO syntax down-levelling — ctjs already parses class fields/statics, getters/setters, computed names, `??`/`?.`/`?.()`/optional-index, async/await. Verified on johnpitchers/Space-Invaders: 21 modules → one `node --check`-clean script. (Driving goal: run that BabylonJS game's Traditional-2D mode; remaining = the Babylon 2D API surface in babylon.hpp — Scalar/Axis/Space/Sound/Sprite+SpriteManager/UniversalCamera/GlowLayer/SceneLoader.ImportMeshAsync/AssetContainer/AssetsManager/ActionManager + the whole `BABYLON.GUI`.)

## Compile times (grammar-free stack, 2026-07)
- PCH: seconds. Test/example TUs: seconds-to-tens-of-seconds; the old
  70 s/TU Earley+type-interp costs died with the type paths.
- `-fexperimental-new-constant-interpreter`: still DO NOT.

## Layout
- `include/ctbrowser.hpp` — umbrella, ENGINE only (no SDL): page + dom + layout + script + engine.
- `include/ctbrowser/page.hpp` — the compile-time assembly. `html_bytes<Src>` re-materializes the NTTP as UTF-8 bytes; `raw_tag_text<Src, Tag>` linearly extracts concatenated <style>/<script>/<title> text. `page<Src>`: html_text()/style_text()/script_text()/title(), all constexpr string_views; `ctbrowser::source<Src>` is the page instance.
- `include/ctbrowser/dom.hpp` — runtime `node` tree (tag/id/classes/attrs/text/children/parent, `inline_style` as a constexpr vector-backed `style_map` — std::map is NOT constexpr, canvas_w/h + pixels 0xAARRGGBB, layout rect x/y/w/h), `instantiate(const cthtml::document&)` / `instantiate_html(std::string_view)` from cthtml's value parser, find_by_id/find_first/hit_test, class helpers, ctcss chain(). **The whole DOM is constexpr** (std::string/std::vector/std::unique_ptr): parse+instantiate+mutate+query fold at compile time — tests/dom.cpp is the static_assert proof.
- `include/ctbrowser/layout.hpp` — `style_fn`/`text_measure_fn` are `ctjs::cfunction` (constexpr type-erased callable, NOT std::function — so the engine still isn't templated on the sheet AND layout folds at compile time; ctcss::query is constexpr), `computed_style` (inline styles beat the sheet), block layout → `paint_cmd` list (box/text/canvas) + node rects, all constexpr. Skips head/style/script/title; display:none prunes; text wraps in square font_px glyphs. tests/dom.cpp runs a whole layout pass in a static_assert.
- `include/ctbrowser/script.hpp` — ctjs bindings: getElementById → element handle object (setText/addClass/... + live width/height/offsetLeft + getContext("2d")/addEventListener), getContext → canvas ctx (fillStyle property read back by fillRect/putPixel/clear natives — the real canvas idiom; 2D path API beginPath/rect/arc/fill, partial arcs degrade to discs; fillText is DOM-style: y = BASELINE, size from ctx.font px → font8x8 integer scale), setTitle; `deliver()` calls script fns if defined (onClick(id)/onKey(name,down)/onFrame(dt)). WEB PLATFORM globals: `document` (getElementById/addEventListener/location.reload), requestAnimationFrame, setTimeout/setInterval/clearTimeout/clearInterval (armed against the tick clock, fired by engine tick — same now_ms performance.now reads), alert, **`fetch(url)` → settled Promise of a Response** ({ok,status,url,text(),json(),bytes()}, each method again a settled promise) served from the embedded-asset registry — `const r = await fetch(url)` works because ctjs (since the async bump) has async/await + the SETTLED-promise subset (then/catch/finally, Promise.resolve/reject/all, JSON.parse); URLs never baked in reject TypeError like a network failure; `dom_events` holds the registered callbacks + the ctjs context to call them (detail::dom_key_code maps SDL names → DOM codes, "Right"→"ArrowRight"). tests/pong.cpp runs the UNMODIFIED MDN breakout (examples/pong.html → generated raw-string examples/pong.inc via tools/html-to-inc.py, #include'd as the page<> NTTP).
- WEB PLATFORM (script.hpp/dom.hpp): document.createElement/appendChild/removeChild/setAttribute + document.body (scripts MAY create nodes now - the old never-create rule is relaxed; detached nodes stay owned by document.detached so handles never dangle; handles carry "__node" registry indexes so natives resolve each other's nodes). Canvas 2D: CTM transform stack (save/restore/translate/rotate/scale/resetTransform; points transform at verb time per spec), real subpaths (moveTo/lineTo/closePath), even-odd scanline fill(), lineWidth-thick stroke(), angle-honoring arc(), measureText, globalAlpha. `window` (innerWidth/innerHeight from layout viewport, devicePixelRatio, performance.now, addEventListener sharing the doc registry). tests/webapi.cpp = the library-boot proof (drives the platform exactly as p5 does). NO library-specific shims, ever.
- `include/ctbrowser/babylon.hpp` — **BabylonJS core-API SHIM on a software 3D rasterizer** (SDL-free, in the PCH; GLM math — `glm::dvec3/dvec4/dmat4`, column-major). THE ONE SANCTIONED EXCEPTION to "no library-specific shims" (user-approved: Babylon needs WebGL, ctbrowser has none, so we implement `BABYLON.*` directly instead of WebGL). `namespace ctbrowser::babylon`: `r3d` = pure renderer (LH column-vector matrices, lookAtLH/perspectiveFovLH, z-buffered barycentric triangle raster, flat Lambert shading, Box/Sphere/Ground/Cylinder gens) writing 0xAARRGGBB into a raw pixel span — testable via `CTBROWSER_BABYLON_RENDER_ONLY` (no ctjs/DOM). **The renderer AND the glTF loader are fully `constexpr`** (std::sin/cos/sqrt aren't until C++26): vec/mat arithmetic is GLM's (its construction/+/-/dot/cross/mat*mat/mat*vec ARE constexpr on this clang), while the ops GLM can't fold `if consteval`-split — at COMPILE time a per-degree cos-table trig (`fsin/fcos/ftan`, interpolation + quadrant symmetry, ~5e-5 error) + `norm3`/`fsqrt`/`ffloor`/`fceil` via constexpr helpers (Newton sqrt + int-cast floor/ceil; `glm::abs` is constexpr and used directly); at RUNTIME `glm::sin/cos/tan`/`glm::normalize`/`glm::sqrt/floor/ceil` (full precision). Matrix builders: `glm::mat4(1.0)`/`glm::translate`/`glm::scale` (constexpr); `glm::rotate`/`glm::yawPitchRoll`/`glm::lookAtLH`/`glm::perspectiveLH_ZO` at runtime with the hand-rolled fill at compile time (all conventions — LH, [0,1] depth, YXZ order — verified to agree with the constexpr fills in the test). So a whole 3D frame rasterizes at compile time AND runtime uses GLM; the JSON parser uses `unique_ptr` (out-of-line dtor for the recursive `jval`), a constexpr number parser + `bit_cast` byte reads, so a whole GLB parses at compile time (both proven by static_asserts in tests/babylon.cpp); `detail` = factory-style `BABYLON.*` natives over a shared `world` (meshes/lights/cameras/scenes; JS handles carry `__mesh`/`__scene` indices — the `__node` idiom). Surface: Engine(canvas→`ev.node_of`)/Scene/ArcRotateCamera(+drag orbit via mouse listeners)/FreeCamera/Hemispheric+DirectionalLight/StandardMaterial/MeshBuilder.Create*+legacy Mesh.Create*/Vector3(statics on function props; methods read `cx.current_this`)/Color3/Color4. `engine.runRenderLoop(cb)` = self-re-registering rAF wrapper (weak_ptr<world> to avoid a cycle) pumped by `engine::tick`; `scene.render()` reads mesh transforms back from the live JS Vector3s each frame and rasterizes into the `<canvas>` pixels (presentation is automatic). `install(out, ev, images)` is called from `engine::all_bindings`. **glTF/GLB model loading**: `namespace gltf` is a pure-C++ minimal GLB loader (own tiny JSON parser; POSITION+TEXCOORD_0+indices primitives; node transforms baked into world-space verts; RH→LH conversion — negate Z + flip winding; PBR baseColorFactor→flat diffuse). **baseColor TEXTURES**: the constexpr parse copies each texture's encoded PNG/JPEG bytes (no decode at compile time); at RUNTIME `r3d::decode_texture` (stb_image, vendored, `STB_IMAGE_STATIC`) turns them into a `r3d::texture` (0xAARRGGBB texels) shared on the `mesh_rec` (and copied by clone), and the rasterizer samples it with perspective-correct UVs + an alpha test (`draw_item.tex`). No PBR/IBL/normal maps/hierarchy. `BABYLON.AppendSceneAsync(url, scene)` resolves the `.glb` from the embedded-asset registry (`find_asset`, same path as `fetch` — the url is auto-embedded because `AppendSceneAsync("` is a needle in assets.hpp; build with `--fetch-allow`), parses it, adds meshes+named materials to the scene, returns a SETTLED promise. Stubs so real model-viewer scripts run: `scene.getMaterialById/createDefaultCamera(fits model bounds)/createDefaultSkybox/debugLayer.show().select`, `CubeTexture.CreateFromPrefilteredData`, `engine.hostInformation.isMobile`. OUT OF SCOPE (accepted+ignored / no-op): PBR/OpenPBR shading, IBL skybox, physics, shadows, animations, GUI, WebGL parity. tests/babylon.cpp = headless render proof (incl. a box-winding occlusion guard); tests/texture.cpp = PNG decode + textured-quad sampling proof (RENDER_ONLY); examples/{babylon,babylon-freecam,babylon-model}.cpp (the last loads a real glTF via the `fetch` preset - CTBROWSER_EXAMPLES_FETCH=ON). All need GLM (header-only) + SDL3. (v1 uses NO Boost; the v2 engine under src/ uses HEADER-ONLY Boost - see NOTICE for why compiled Boost, Boost.Context above all, must stay out.)
- `include/ctbrowser/engine.hpp` — `engine<Page>`: doc + title + resolver + script run with bindings; frame(viewport_w) (also refreshes handle offsetLeft/width), click_at, key/mouse_* (deliver conventions AND dispatch DOM listeners), tick (onFrame + rAF pump + location.reload re-instantiation); all_bindings installs the DOM/web globals AND the BABYLON namespace. SDL-free; what the tests drive.
- `include/ctbrowser/app.hpp` — SDL3 shell: run_app<Page>(app_options). Boxes = filled rects, text = font8x8 scaled, canvas = streaming SDL_Texture. `SDL_VIDEODRIVER=dummy` + `CTBROWSER_TEST_FRAMES=N` (env, read by run_app) = headless run.
- `include/ctbrowser/font8x8.hpp` — GENERATED from public-domain font8x8 (dhepper); glyph_pixel(c,row,col).
- `external/compile-time-{html,javascript,css}` — SUBMODULES (ctjs carries ctc nested). ctc resolves through compile-time-javascript's copy — exactly ONE ctc on the include path (ctc::string = the page NTTP, ctc::cfunction = the layout hooks). cthtml/ctcss are submodule-free.

## Decisions
- Scripts may MUTATE and (since the web-platform sweep) CREATE/detach nodes — document owns every node (tree or detached) so raw node* in bindings never dangle; `engine` is noncopyable, doc outlives script result.
- **Interaction model (2026-07-23)**: engine tracks hovered_/pressed_/focused_ (node flags on the whole ancestor chain for hover/active; chain() feeds ctcss ps_* bits, restyled per frame). CLICK FIRES ON RELEASE (down+up paired via nearest common ancestor; select popup consumes on down via click_suppressed_). One SHARED event object per click — preventDefault/stopPropagation are real (flags on the event, read via cx.current_this). Default actions after listeners: checkbox toggle, radio group (document-wide by name), summary→details.open, label→for=/descendant control, a[href]→engine.open_url hook (SDL_OpenURL in the shell; #fragment→location_hash only). Disabled controls dispatch nothing.
- **Text stack (2026-07-23)**: vendored fonts/ (Tinos/FiraSans/Cousine, OFL, 12 TTFs ~5.3MB) std::embed-ded by fonts.hpp into run_app's opts.assets (registry keys ctbrowser:font/<generic>-<style>; headless TUs never carry the bytes). layout resolves font-family (FULL comma list)/-weight/-style/text-decoration per element (inherited-resolver pattern), stamps every text paint_cmd (font_family/bold/italic/deco) + emits 1px decoration bands; text_measure_fn = (text, px, family, bold, italic). app.hpp ttf_text = multi-face registry ((family,bold,italic) -> bytes; page @font-face entries incl. weight/style descriptors + the embedded generics; missing variants get TTF_SetFontStyle synthetic bold/italic; font8x8 fallback fakes bold=double-strike, italic=shear). MULTIPLE fonts per document is the contract.
- **Editing/forms/tables (2026-07-23)**: node.value/caret/value_dirty (inputs from value attr, textarea from RCDATA text - newlines preserved for textarea+pre); engine.text_input() + edit_key() (code-point Backspace/Delete/arrows/Home/End/Up/Down, Return = textarea newline | implicit form submit) gated by cancelable keydown; change fires on BLUR; submit_form/reset_form (+ .submit()/.reset() via ev.request_* hooks, onsubmit/submit listeners cancelable-shaped, <button> defaults to submit); emit_input renders LIVE value + caret bar + suffix-scroll, emit_textarea rows/cols, emit_table (equal columns, 2px spacing, border attr frames, caption above), li markers (ul disc / ol "N."), per-side margins/paddings (1-4-value shorthands + -left/-right/-top/-bottom), buttons/selects shrink-to-fit, select honors the selected attribute.
- **Scrolling (2026-07-23)**: engine scroll_y_ clamped per frame to the laid-out page height; frame() shifts paints AND rects together (hit tests/handles agree), position:fixed subtrees exempt (paint_cmd.fixed + node.viewport_fixed set in place()). wheel(x,y,dy) = textarea-under-pointer scrolls itself (node.scroll_top, clamped by emit_textarea, NO scrollbar) else page; dispatches DOM "wheel" (deltaY>0=down). PageUp/PageDown/Home/End page-scroll when focus is not editing. Edits set node.caret_follow → emit_textarea scrolls the caret into view (manual wheel scrolling is not yanked back). Resize reflows: shell polls window size per frame → resize_viewport + frame(new_w); glyphs never scale (tests/scroll.cpp proves rewrap at constant font_px).
- **Browser chrome (2026-07-23)**: engine.cursor() (CSS `cursor` via styled() = inline-first resolve; UA gives a{pointer}, editables text; bare text = I-beam) -> shell SDL system cursors. Overlay scrollbar drawn in frame() (fixed cmds; thumb drag via sb_dragging_/sb_grab_, track page-jumps; scrollbar-width none/thin override). Selection: editables get char-precise sel_anchor/caret (click = nearest-glyph-boundary via ui_* layout cache + measure; shift+arrows; drag), page selection is CHARACTER-PRECISE: layout publishes per-line glyph geometry (node.ui_lines cp spans + boxes, scroll-shifted by offset_rects), engine maps points to (node, cp) via nearest-line + glyph-midpoint walk (above-line = line start, below = line end - downward drags take whole lines), ranges span nodes in document order (node.sel_from/sel_to cp ranges; user-select:none respected); highlights #B4D5FE drawn by layout. Clipboard = engine hooks (clipboard_get/set; shell = SDL clipboard) behind Ctrl+C/X/V/A with cancelable copy/cut/paste events. Right-click (mouse_button button=2) dispatches cancelable "contextmenu" then opens the engine-drawn Copy/Cut/Paste/Select All menu (menu_* state, Esc/click-away closes). tests/browserui.cpp covers all four.
- **Fidelity batch (2026-07-24)**: caret BLINKS (Chrome 500ms halves: engine caret_base_ms_ resets on any caret activity, frame() computes node.ui_caret_on, emitters draw the bar only when focused && ui_caret_on). textarea SOFT-WRAPS (emit_textarea builds word-boundary visual lines into node.ui_lines with a `hard` flag; engine Up/Down/Home/End walk VISUAL lines via the stale-safe visual_lines() helper — falls back to hard-line spans when ui_lines doesn't match the value; clicks map through ui_lines). input horizontal scroll is PERSISTENT (node.scroll_cp, minimally adjusted by emit_input only when the caret exits the window; caret_from_click adds the scrolled prefix). summary gets drawn ▶/▼ disclosure triangles in the UA 18px gutter. INLINE FLOW subset: layout detail::inline_level_tag/shrink_wrap_tag; consecutive inline-level children run on shared lines (wrap via translate, per-line vertical centering in flush_line, gap font_px/3, CSS `display` overrides the tag default), inline containers shrink-wrap to content width, label = control-first + its text continuing the SAME line. A block's own text still renders ABOVE element children (no mid-line text interleave). tests: editing.cpp (wrap/scroll/blink — NOTE the blink test uses e.tick to advance now_ms), forms.cpp (marker cmds), richtext.cpp (same-row buttons, shrink-wrap).
- **Chrome-parity batch (2026-07-24 #2)**: scrollbar RESERVES layout space (engine frame() two-pass: layout at viewport_w, then if page_h_ > viewport_h re-layout at viewport_w - scrollbar_width(); stable because narrowing only makes pages taller; blink phase now computed BEFORE layout so this frame's paints carry it). AUTO table layout (emit_table: natural_text_w per cell = widest unwrapped text run at each node's own font + cell padding; columns take the max, table shrinks to the sum + n.w shrinks to match; explicit CSS width or overflow → proportional scale; caption placed at table width, centered via UA caption{text-align:center}, OUTSIDE the border — the bordered frame wraps the grid rows only via a scratch rect node; cells stretch to row height). blur clears the field selection (set_focus outgoing sel_anchor=-1). textarea caret CLAMPS inside the box (wrap-space lines can exceed content width by a glyph — the caret pinned outside the border otherwise). widgets example script is browser-idiomatic (document.getElementById + addEventListener + e.preventDefault() in submit).
- **UA stylesheet** (ua.hpp): Firefox values (Gecko html.css + modern widget theme); resolve = author sheet first, UA fallback when empty; widget chrome (frames #8f8f9d, checked accent #0060df) drawn by layout's emit_toggle/emit_input/emit_frame; closed <details> and display:none subtrees get zero_rects (stale layout rects were hit-testable — fixed).
- Click delivery: deepest hit-test node, walk up to first non-empty id, call onClick(id).
- Layout: px only; canvas box = its pixel size; backgrounds paint in a pre-pass (back-to-front), then text/canvas in traversal order.
- The bricks' own semantics/limits apply verbatim (see their CLAUDE.md).

## v0.2 game-engine surface
- `image.hpp` (engine, SDL-free): mini BMP reader (24/32bpp, compression 0/3, top-down or bottom-up; parse_bmp works from memory) + `image_store` behind loadImage/drawImage — sprite tests run headless. `embedded_asset` = compile-time-embedded bytes; image_store and audio_mixer consult `embedded` before the filesystem.
- `embed.hpp` — the PUBLIC compile-time file API: `ctbrowser::embed<T=std::byte>(path[, offset])` → consteval span into compiler-materialized storage (missing/un-#depend-ed file = compile error whose undefined-function name spells the reason); `try_embed` = empty span instead (opportunistic). Lookup is EMBED-DIRS ONLY (never call-site-relative; --embed-dir carries the repo root) — same meaning from every frame, and it avoids the anchor-frame walk that crashed pre-23dd34f8f compilers. Protocol per phd::embed (CC0, see NOTICE).
- `assets.hpp` — AUTOMATIC std::embed AND std::fetch: the engine constexpr-scans the page's script for loadImage("...")/playSound("...")/fetch("...") literals; file paths try_embed, **http(s):// URLs try_fetch — fetched over the network AT COMPILE TIME** (scripts/stylesheets/fonts/JSON/sprites; backs script-side `await fetch(url)`) — into one registry (auto_assets<Page> → engine ctor merge; app_options.assets/user entries win). URL fetches need the build to pass `--fetch-allow=<url-glob>` (fetch.hpp; nothing allowed by default, so offline/default builds skip the network cleanly). OPPORTUNISTIC at every step: no builtin / no `#depend` / missing file / no --fetch-allow → files silently load at runtime, URLs reject at runtime. A TU opts in with ONE guarded line: `#if defined(__has_builtin) && __has_builtin(__builtin_std_embed)` + `#depend "examples/assets/**"` + `#endif` (compilers without the builtin skip the directive - unknown directives in false #if groups are not processed). Builds pass `--embed-dir=<repo root>` on clang so script paths resolve.
- Canvas ctx additions (script.hpp): clearRect→TRANSPARENT (canvas textures get SDL_BLENDMODE_BLEND so the page shows through), strokeRect/strokeStyle, fillCircle, fillText (font8x8 into pixels), drawImage/drawImageRegion (nearest, alpha-test a==0).
- Input state lives ON the engine (keys_down set, mouse x/y/down), fed by the shell, exposed as isKeyDown/mouseX/mouseY/isMouseDown; engine ctor takes `extra` bindings — the shell injects playSound/setVolume (audio.hpp mixer), screenshot, setFullscreen.
- Screenshots (screenshot.hpp, shell): SDL_RenderReadPixels → PNG via vendored stb_image_write; a `.ppm` path writes raw P6 (golden-comparable). Works under the dummy driver.
- `app_options`: fixed_dt (auto 1/60 when CTBROWSER_TEST_FRAMES set → deterministic), logical_w/h (LETTERBOX presentation; mouse events go through SDL_ConvertEventToRenderCoordinates), fullscreen, screenshot_path/screenshot_frame (-1 = last).
- Render verification: tests/render.cpp is the ONLY SDL-linked test (built when find_package(SDL3) succeeds), sets dummy drivers itself, pixel-samples the PPM and byte-compares tests/golden/render.ppm (`REGOLDEN=1 ./tests/render` regenerates). ctest runs tests/examples with WORKING_DIRECTORY = source root (asset paths are repo-relative) and CTBROWSER_SCREENSHOT into the build dir; CI uploads shot-*.png artifacts.
- Assets are GENERATED: `python3 tools/gen-assets.py` (sprites.bmp 24x8 sheet: alien A/B + ship; blip.wav square-wave) — deterministic, no foreign binaries.
- **SDL3 satellites are OPTIONAL, detected by the build** (pkg-config `sdl3-image/-mixer/-ttf`; CMake find_package) → defines `CTBROWSER_WITH_IMAGE/MIXER/TTF` + links. image → `image_store.decoder` hook (IMG_Load→ARGB8888, engine registry stays plain pixels, BMP path still first); mixer → `audio_mixer` MIX_* implementation (MIX_CreateMixerDevice/LoadAudio/pooled tracks, master gain), stream-WAV fallback preserved in the #else; ttf → `detail::ttf_text` in app.hpp (fonts per px size, glyphs rendered WHITE + color-modded, texture cache capped 256, `probe_font()` scans DejaVu/Liberation/Helvetica/Arial when `app_options.font_path` empty) + `engine.measure` hook feeding layout's greedy wrap. Canvas fillText stays font8x8 (goldens deterministic); TTF affects PAGE text only. CI runners lack SDL3 → render test + examples skip there; goldens are a local check.

## GOTCHAS
- **Submodule bumps**: update the brick's gitlink; ctc rides inside ctjs (only compile-time-javascript's copy is on the include path).
- **Constexpr lifetime idioms** (from the bricks): owned constexpr documents/sheets cannot escape constant evaluation — extract scalars inside the asserting expression; bind documents to named locals.
- **Attribution**: preserve NOTICE (ctc MIT; historical CTLL/CTRE lineage; font8x8 public domain, SDL zlib, not bundled).
