# CLAUDE.md — ctbrowser

A browser engine in C++23 named modules. `src/` is the engine, `tests/` the
suite, `examples/` the programs that use it. Namespace `ctbrowser`.
**CMake + Ninja is THE build**, CMake >= 3.28 for modules; an ordinary clang or
gcc with C++23. Work on `main`. Prefer `rg`.

The compile-time engine this repository is named for is GONE from the tree
(2026-07-27) and lives in the git history: the page was a structural NTTP and
the parsers ran in constant evaluation. What that cost and what it left behind
is in `docs/`. Two bricks remain as submodules doing runtime work - ctcss parses CSS, ctjs
parses script. cthtml does not, and is no longer a submodule at all: the DOM
has its own WHATWG tokenizer and tree builder, and `src/dom/entities.hpp` is
the entity table carried forward from it.

## Build & test
```bash
git submodule update --init --recursive    # ctcss + ctjs (+ nested ctc)
cmake --preset default && cmake --build --preset default && ctest --preset default
cmake --preset tsan && ctest --preset tsan     # and asan
# examples build when SDL3 is found; tests are always headless
```
Flags: `-O2 -pedantic -Wall -Wextra -Werror -Wconversion`. Tests are
EXECUTABLES, SDL-free, headless. `tools/format.sh --check` is the formatting
gate and CI runs it.

## Tooling
- `tools/gen-assets.py` — regenerates `examples/assets/` (sprites.bmp, blip.wav)
  deterministically, so no foreign binary is committed.
- `tools/gen-shaders.py` — GLSL -> the SPIR-V in `src/gpu/shaders/tile_spv.hpp`.
- `tools/format.sh`, `tools/check-package.sh`, `tools/check-render.cmake`,
  `tools/remote-build.sh`.


## JAVASCRIPT (2026-07-25)

**The MDN breakout tutorial runs, unmodified** — `examples/pong.cpp` loads
`examples/pages/pong.html`, a byte-for-byte copy. `examples/fetchboard.html`
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

**Promises are SETTLED-ONLY**, like the previous engine's: no job queue, no `new Promise(executor)`,
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

`tests/page_scripts` compiles the real example pages and asserts what each
one does; `tests/vm_basics` has a test per language feature.

## BUILD SPEED (2026-07-25)

Measured, then fixed. A clean the engine build was **143.7 s wall / 237.9 s CPU**; it is
now **116.7 s / 221 s**, and the parts a user waits on moved most:
`tests` 221 s → 127 s, `examples` 138 s → 36 s. A bare
`import ctbrowser;` with an empty `main` costs **0.89 s**, so the umbrella is
cheap — a consumer pays for what it USES.

Three things did it:

1. **lld.** The same executable links in 0.65 s against 4.31 s with the default
   `ld`, and the engine builds twenty-six of them. `CTBROWSER_USE_LLD=OFF` opts out;
   `ctbrowser_target()` is the one place that decides how the engine is built.
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
`CTBROWSER_SINGLE_LIB=OFF` skips it; CMake users keep using
`ctbrowser::ctbrowser`, which also carries the include paths and BMIs an archive
cannot.

**The opt-out:** a modules project has no header-only mode, so the knob that
buys back what an all-inline engine gave is `CTBROWSER_LTO=ON` — inlining
across the library boundary at LINK time rather than by recompiling the engine
in every TU. Off by default, because the default is meant to be fast to build.

`tools/check-package.sh` is what catches the other half of this: an exported
target that links `Freetype::Freetype` needs a matching `find_dependency` in the
installed config, and stage 6 shipped without one.

## APPLICATION API (2026-07-25)

**`import ctbrowser;` + `ctbrowser::run_app(html, options)` is the whole
API.** One module, one link target (`ctbrowser::ctbrowser` in-tree,
`ctbrowser::ctbrowser` installed), NO SDL header in the application. See
`examples/counter.cpp` — 40 lines, most of it the page.

`run_app` owns the window, the event loop, the clock (it calls `tick()`, so
timers and rAF actually fire), vsync, fps pacing, screenshots and teardown.
`app_options` mirrors the previous engine's: size, `logical_width/height` letterboxing,
`max_frames`, `max_fps`, `fixed_dt`, `screenshot_path`, `assets`,
`on_native_window` (the escape hatch — hands you the `SDL_Window*` as `void*`)
and `on_ready`. Env: `CTBROWSER_TEST_FRAMES`, `CTBROWSER_SCREENSHOT`,
`CTBROWSER_RENDERER` — which is how an example becomes a ctest with no test
code in it.

**SDL3 is OPTIONAL AT BUILD TIME.** `ctbrowser-app` always builds;
`CTBROWSER_WITH_SDL3` selects an SDL host or a headless one at runtime. Without
SDL3 the engine still renders and `run_app` still works.

**Installing:** `tools/check-package.sh` is the proof — installs the engine to a temp
prefix, builds `tests/package/` against it via `find_package`. GLM and the
submodules are no longer configure requirements.

`tests/api_surface` lints the claim: application sources must contain exactly
one `import ctbrowser;` and no SDL symbol, and the engine modules must stay
SDL-free.

## THE SHELL IS THE ENGINE (2026-07-25)

`ctbrowse` (examples/) is the browser: `ctbrowse page.html`, or
`ctbrowse page.html --headless out.ppm --size W H` with no display at all.
`ctbrowser.shell::browser` is the assembly and is SDL-FREE — `ctbrowser.app`
is the only module that knows SDL exists. A frame runs only what changed:
a scroll re-composites, an idle frame does nothing, a resize re-lays-out.

**`std::embed` is no longer required to configure.** `CTBROWSER_BUILD_V1` is
now `AUTO`: the previous engine builds where the compiler has `__builtin_std_embed` and is
skipped with a STATUS message where it does not. `cmake -S . -B build
-DCMAKE_CXX_COMPILER=clang++` on stock clang builds and tests the engine alone.
`-DCTBROWSER_BUILD_V1=ON` still hard-errors on the wrong toolchain, on
purpose — silently building something other than what was asked for is how
CI reports success for a target it never built.

**Script and HTML parsing are the engine's own now.** `ctbrowser.shell:bindings`
gives pages `document`/element methods/events/timers/rAF (handles, not
`node *`, so a stale reference fails a lookup instead of corrupting memory).
`ctbrowser.dom:tokenizer` + `:treebuilder` replaced the cthtml wrapper with
the WHATWG algorithms — implied `<html>/<head>/<body>`, unclosed `<p>`/`<li>`,
table section inference, foster parenting, and the adoption agency. **the engine no
longer uses `external/compile-time-html`** (ctcss and ctjs's parser remain).

**Form controls and canvas 2D work.** `ctbrowser.shell:forms` holds control
state (value, caret, selection, checked) keyed by node_id — NOT on the node,
which is what left the previous engine's `node` carrying thirty UI-only fields.
`ctbrowser.shell:canvas` is the 2D context, with its pixels in a store the
display list shares by `shared_ptr`. Replaced elements (`canvas`, `input`,
`button`, `select`, `textarea`, `img`) are `box_kind::replaced` and are sized
by `intrinsic_size_of`, not by their children.

A canvas draw marks `dirty::raster` — tiles are stale, the display list is
not — so an animation re-rasters without re-recording or re-laying-out.

**THE PREVIOUS ENGINE IS DELETED.** What it still has that the engine does not: the BabylonJS shim
and its software 3D rasterizer, textarea soft-wrap, and canvas gradients. See
`docs/v1-retirement.md`.

## STYLE: the `style` attribute, with Chrome/Firefox precedence (2026-07-25)

Read at last — the engine saw `<style>` ELEMENTS only, so `<div style="height:2000px">`
laid out as one line. It is NOT a separate origin: author-level with a
specificity above every selector, which puts it in the cascade at

    normal selector  <  normal inline  <  important selector  <  important inline

so `engine::resolve` SPLICES the attribute's normal declarations in at the
importance boundary rather than appending them at the end. Appending is the
easy mistake and it is invisible until a page uses `!important` to override a
widget's inline style; `tests/style_basics` has a test per step, verified by
planting the mistake and watching exactly those two fail.

Parsed through the SHEET parser wrapped in `*{...}`, not ctcss's declaration
splitter — the latter peels `!important` off and discards the flag, which is
the entire question. Cached by attribute TEXT, so a table styling forty rows
identically parses once and a re-resolve after a hover parses nothing.

## TABLES AND GENERATED CONTENT (stage 7, 2026-07-25)

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
`textarea`, and which INHERITS). the engine never did this: the newlines in a page's own
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
`tests/chrome_basics` therefore tests it with REAL fonts, and the test is
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
`tests/{tsan,lsan}.supp`, and both files say they were verified by planting
a fault in our own code and confirming it is still caught - which was actually
done, for the leak, in the commit that added it.

Two repository problems the formatting turned up, neither of them formatting:
**`build-timing/` was committed** - 457 files, 408 MB, from a `git add -A` -
and is untracked now, though the history still carries it. And **the goldens
were never tracked at all**: `*.ppm` in `.gitignore` swallowed
`tests/golden/`, so every golden test would have failed on a fresh clone
with "no golden". There is an exception for them now.

## EDITING, DISABLED, AND THE COLLECTOR (2026-07-27)

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

## CPU AND THE PROFILER (2026-07-27)

**`CTBROWSER_PROFILE=out.csv CTBROWSER_PROFILE_SECONDS=10 ./widgets.exe`** — a
record per loop iteration (poll / tick / frame / present / asleep, layouts,
whether it drew), a summary on stdout, and **CPU time against wall time**,
because "it uses 65% of my CPU" is not the same question as frames per second.
`tests/bench_interaction` is the headless half: what a mouse move, a hover
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

## FORM CONTROLS: the batch a real page found (2026-07-27)

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
open and every label glued to its control and still exits 0. `render-widgets`
and `render-elements` now byte-compare the render against
`tests/golden/`, with `CTBROWSER_FONTS=font8x8` so the golden pins LAYOUT
and does not move when FreeType does. Both goldens are byte-identical from the
Windows exes.

## NAVIGATION: alert, location, `<a href>` (2026-07-27)

The last three things the previous engine's script surface had and the engine's did not — and the proof
is that **MDN's breakout now survives its own game over.** It ends by calling
`alert("GAME OVER")` and then `document.location.reload()`; both were undefined
identifiers, so the one page in the suite that proves web compatibility died at
the exact point every other test had stopped looking.

**`location.reload()` cannot reload the page it is called from** — the reload
tears down the script context and the program still running inside it. It
records the request; `tick()` drains it BETWEEN callbacks. Same as the previous engine.

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

## FONTS: real ones (stage 6, 2026-07-25)

**Text is drawn with outline faces.** `ctbrowser.raster:ttf` is a `font_backend`
over **SDL3_ttf** — the one place the engine knows about SDL, and a deliberate
exception rather than an oversight. `TTF_Init` needs no video subsystem, so real
text is still TESTABLE with no display, which is what makes the exception safe.
`tests/api_surface` SWEEPS `src/` and names the exceptions; the old
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
`tests/fonts_basics` drives it from twelve threads on COLD glyphs. Going
through the browser does not test it: layout measures every run before raster
draws it, so the parallel path only ever reads. Removing the lock and watching
TSan stay silent is what showed that up.

Rendering turned out to be byte-identical between FreeType 2.14.3 (linux) and
2.13.3 (the mingw sysroot) for these faces — every example matches across
platforms. That is not guaranteed in general, which is what `CTBROWSER_FONTS`
is for.

## INPUT: the page gets the events (2026-07-25)

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

`tests/bindings_basics` drives all of it, and finishes by holding a key
through MDN's breakout and asserting the frames differ — with a key the page
ignores as the control, so "the frames differ" cannot pass by nondeterminism.

## RESOURCES: assets, images, fetch (2026-07-25)

`ctbrowser.shell:assets` is the registry every load goes through — an
application seeds it from `app_options::assets`, and a miss falls back to the
filesystem (cwd → `asset_path` → two levels up, the previous engine's probe order). Registry
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
consulted first. `tests/net_basics` proves the client against a loopback
server it stands up itself; no test in the suite touches the internet.

Examples: `invaders` (sprite sheet through the 9-argument `drawImage`, keys via
keydown/keyup, `requestAnimationFrame`, sound) and `fetchboard` (a baked-in
resource AND a live HTTP GET) are ported. Both pages were rewritten off the previous engine's
`onFrame`/`isKeyDown`/`getContext(id)` shorthand onto the real web APIs.

## GPU: Linux binaries here see no adapter — WINDOWS ONES DO (2026-07-25)

`src/gpu` (SDL3 `SDL_GPUDevice`) builds and RUNS under this WSL2, but the only
Vulkan ICD that survives loading is **lavapipe** (`lvp_icd.json`) — every
hardware ICD is dropped with "not having any physical devices". `/dev/dxg` and
`/usr/lib/wsl/lib/libd3d12.so` exist, but no `dzn`/`d3d12` Vulkan ICD bridges to
them. `SDL_GetGPUDeviceDriver` says "vulkan" either way — the adapter name
(`SDL_PROP_GPU_DEVICE_NAME_STRING`, exposed as `sdl_gpu_backend::adapter()`) is
what tells you, and `adapter_is_software()` checks it.

**The cross-compiled .exe sees the real GPU.** Run under WSL interop,
`build-windows/src/tests/ctbrowser-test-gpu_basics.exe` selects
**`Intel(R) Arc(TM) Graphics`** and its render matches the software one exactly
(0 of 120000 pixels differ). So GPU **correctness** is verifiable both ways, and
GPU **performance** numbers must come from the Windows build — `bench_gpu`
prints a loud banner on Linux here because its numbers would be two CPU
implementations racing. Headless GPU runs need `SDL_VIDEODRIVER=offscreen`;
`dummy` has no Vulkan surface support and fails device creation outright.

## Windows cross-build (2026-07-25)

`cmake --preset windows -DCTBROWSER_BUILD_V1=OFF && cmake --build --preset
windows && cmake --build --preset windows --target windows-dist` →
**`examples-windows/`** (its own directory: four example names collide with
the previous engine's `examples-windows/`). It carries the exes, SDL3.dll and the pages/assets
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
`SDL3::SDL3-shared` and tells `windows-dist` whether a DLL has to travel.
Cost: 3.5 MB → 7.2 MB per exe.

Toolchain, all fetched rather than built: llvm-mingw std::embed release
(`tools/llvm-mingw/`, 84 MB) and **Boost as an isolated include dir**
(`~/projects/boost-inc/boost` symlinked at the host's) — there is no BoostConfig
for the cross target and none is needed, since the engine links `Boost::headers` and
nothing else. The toolchain file finds it the same way it finds GLM's.

Degrades as designed: no OpenSSL for mingw → `fetch` does http:// only and says
so; no SDL3_image → `<img>` reads BMP only. Asio needs `ws2_32`/`mswsock`, which
nothing links implicitly.

**Verified**: all 19 the engine tests pass as Windows binaries WITH NO DLL BESIDE THEM
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
