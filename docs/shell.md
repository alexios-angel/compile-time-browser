# The shell — the assembly, the application API, and what a page can do

`include/ctbrowser/shell/` is the engine assembled: `browser.hpp` is the whole
browser, `bindings.hpp` the API a page's script sees, plus `forms`, `canvas`,
`input`, `net`, `images`, `assets`, `metrics`. It is SDL-FREE; `app.hpp` and
`src/shell/app.cpp` are the only places that know SDL exists.

## APPLICATION API (2026-07-25)

**`#include <ctbrowser.hpp>` + `ctbrowser::run_app(html, options)` is
the whole API.** One include, one link target (`ctbrowser::ctbrowser` in-tree,
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
prefix, builds `tests/package/` against it via `find_package`.

`tests/api_surface` lints the claim: application sources must contain exactly
one engine include - the umbrella header - and no SDL symbol, and the engine must stay
SDL-free.

## THE SHELL IS THE ENGINE (2026-07-25)

`ctbrowse` (examples/) is the browser: `ctbrowse page.html`, or
`ctbrowse page.html --headless out.ppm --size W H` with no display at all.
`ctbrowser.shell::browser` is the assembly and is SDL-FREE — `ctbrowser.app`
is the only module that knows SDL exists. A frame runs only what changed:
a scroll re-composites, an idle frame does nothing, a resize re-lays-out.

**The compiler is ORDINARY.** `std::embed` was load-bearing for the compile-time
engine and went with it; stock clang or gcc with C++23 builds this. What it
must have is the ability to REPORT ITS IMPORT GRAPH, or CMake cannot build
modules — and it says so in terms that mention neither modules nor the compiler
version, so the root CMakeLists searches for a new-enough clang before
`project()` locks the toolchain. `CXX=` or `-DCMAKE_CXX_COMPILER=` overrides
that entirely.

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

**CANVAS TEXT GOES THROUGH THE REAL FONT BACKEND (2026-07-28).** It did not:
`fill_text` drew 8x8 bitmap cells scaled by an integer whatever `ctx.font` said,
and `measureText` answered from the same table, so `"16px Arial"` advanced a
monospaced 16px a glyph where the face it asked for takes about seven. That is
what clipped pong's HUD — MDN puts "Lives: N" at `canvas.width - 65`, correct
for the metrics it requested and 63px short of the ones it got. The family was
being parsed and thrown away; `font_face_from` keeps it, with bold and italic,
and it travels on `canvas_context` beside the size. `canvas_store::set_fonts`
hands the backend down from `use_real_fonts()`, and a null one means font8x8 —
the same fallback `browser::fonts()` has, so a build without SDL3_ttf is
unchanged.

`measureText` and `fillText` now both go through `canvas_context::measure_text`,
which is `docs/raster.md`'s rule applied to the canvas: ONE object answers both,
or text does not land where it was measured. `measureText` also gained the
`sync()` it never had — it is the one method that changes no pixels and so was
not wrapped in `draws()`, which meant it read whatever font the last DRAWING
call had left behind.

`draw_run` writes into a `raster::surface` and a canvas owns a `paint::bitmap`,
so the run is drawn into a scratch surface and copied back. **The scratch is
seeded from the destination first** — against a blank one, `blend_over`
composites the antialiased edge onto transparent black and that premultiplied
result is blended in again, which is a dark halo on every glyph. And `where.y`
is the run box TOP, not the baseline: both backends add their own ascent.

**THE PREVIOUS ENGINE IS DELETED.** What it still has that the engine does not: the BabylonJS shim
and its software 3D rasterizer, and canvas gradients. See
`docs/v1-retirement.md`. (Textarea soft-wrap was the last item on that list and
landed 2026-07-28 — see below.)

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

**A TEXTAREA SOFT-WRAPS, AND SCROLLS TO FOLLOW THE CARET (2026-07-28).**
`value_lines` split on `'\n'` and nothing else, so a paragraph with no newline
in it was one visual line however long, drawn straight through the right edge
and clipped. It wraps in `layout_of_field` — the one place — with
`layout::words_that_fit`, the SAME greedy break the page's inline layout uses,
promoted out of `inline_flow` for the purpose. Two wrappers would be two answers
to where a line breaks, and a field disagreeing with the text around it is what
sharing it prevents.

**A soft break consumes no character and a hard one does**, so after a wrap
line *n*'s `end` IS line *n+1*'s `begin`, where a `'\n'` would have left a gap
of one. That is how a consumer tells them apart without a flag — and it is why
`caret_line()` had to become a real function. The painter loops over lines with
no `break`, and got away with it only because the old gap made the range test
`caret >= begin && caret <= end` match once; with a soft break a caret sitting
on the boundary matches BOTH lines and a bar was drawn on each. **Two carets.**
`caret_line` decides once, and `move_caret_by_line` asks it too rather than
keeping a second copy of the rule.

A textarea is a replaced box sized by its `rows` and does not grow into the
lines wrapping adds, so `control_state::scroll_line` is the first visible line
and `reveal_caret()` moves it the minimum needed to keep the caret in view —
called from the two places that can move a caret, `handle_key` and `text_input`.
The painter draws from that offset and `offset_at_point` adds it back; those two
must move together, which is the whole reason the geometry lives in one place.
**A FIELD'S VIEW IS A FIRST-CLASS THING, IN BOTH AXES (2026-07-28).**
`control_state` carries `scroll_line` and `scroll_x` — the latter in PIXELS, not
characters, because the painter and the click mapping both work in pixels
against `measure()` and a character offset would quantise the scroll so a caret
at the right edge could never sit flush. Both axes apply to both kinds: a
textarea overflows sideways too, since `words_that_fit` returns an over-long
unbreakable word whole.

**THREE THINGS MOVE THAT VIEW and they must never run on the same event**, or
they oscillate. The rule is written at `reveal_caret` and every bug in this area
is a violation of it:

1. **The caret drives** — typing, editing keys, the clipboard verbs, `el.value=`.
   `reveal_caret` moves the scroll the minimum needed to show the caret.
2. **The user drives** — the wheel. The scroll moves and the caret does NOT; it
   is left off screen if that is where it was. Nothing re-reveals here, and a
   test pins that: adding a well-meaning `reveal_caret` to the wheel path snaps
   the view back on every notch and makes the wheel useless.
3. **The drag drives** — auto-scroll. The scroll steps and the caret is then
   re-derived from where the pointer is.

The consequence, which is what a browser does: wheel away from the caret and it
stays out of view until the next keystroke, when rule 1 brings it back.

**Stale scroll is impossible by construction rather than by discipline.**
`field_layout` carries the EFFECTIVE origin — `control_state`'s, clamped against
the freshly-wrapped lines — and everything that draws or hit-tests reads that
rather than the stored number, which is only ever a request. So a scroll left
behind by a shrinking value, a scripted `el.value =`, or a resize that rewraps
to fewer lines cannot be observed. Chasing the writers was the alternative and
it is unwinnable: `form_store::set_value` is reached from the bindings' control
refresh, which has no access to the geometry and never will.

**The wheel goes to the field under the pointer** and falls through to the page
once that field is at its end — otherwise a textarea at its last line swallows
every notch and the page looks stuck. This needed a pointer position on the
wheel event, which it never had: `wheel_at` carries one and `wheel_by` remains
for the headless case. It is taken from the host's TRACKED pointer rather than
SDL's `wheel.mouse_x`, because `SDL_ConvertEventToRenderCoordinates` documents
itself as converting mouse, touch and pen and does not name the wheel's — and an
unconverted position is wrong by the letterbox factor, which is a bug the
pointer events already had once. PageUp/PageDown belong to a focused textarea
for the same reason.

**Auto-scroll while drag-selecting** runs off `tick()` on the caret blink's
shape — the one clock, a due time, a `next_wakeup_ms` contribution — because a
pointer held still outside the box produces no events at all, and
`offset_at_point` clamps to what the value has, so the selection would freeze
one line short for ever. The rate rises with distance
(`autoscroll_ms / (1 + d / autoscroll_ramp_px)`, floored). The wakeup is
contributed **only while a step can actually happen**: at the scroll's limit it
reports nothing, or an idle loop with the pointer parked below a fully-scrolled
field would spin at the step interval. `tick` runs a LOOP, so one long tick
performs every step it covers rather than silently making the rate the frame
rate. A reload clears the in-flight drag, since those are handles into a slab
that has just been rebuilt.

Still out of scope, and deliberately: a visible scrollbar on the textarea,
`wrap="off"`, horizontal wheel, and auto-scroll for the PAGE selection drag
(dragging below the viewport while selecting page text) — the machinery here
would serve it, but wiring it is a separate change.

**A `<label>` ACTIVATES ITS CONTROL (2026-07-28).** It was unsupported
outright — not a `control_kind`, `for` never interned, no UA rule.
`control_ancestor` walks strictly UPWARD, and in
`<label><input type=radio> small</label>` the control is a SIBLING of the
clicked text (hit testing returns the deepest fragment's source, which is the
DOM text node), so the walk found nothing and clicking the word *blurred*
whatever was focused. `labelled_control` resolves the nearest label ancestor:
`for` by id if present, else the first labelable descendant — where labelable is
exactly `control_kind_of(...) != none`, which already maps `input type=hidden`
to none, so HTML's notion falls out rather than needing a second list to keep in
step. `control_ancestor` falls back to it, which fixes focus, the caret and
every arm of `activate()` at once, since all of them re-derive through it.

A `for` naming nothing labels NOTHING — no quiet fallback to a control the label
happens to contain. Resolution happens ONCE: a press on the control inside a
label finds it on the upward walk and never reaches the fallback, so a checkbox
cannot toggle twice. And a label for a TEXT field focuses it and stops —
`via_label` is the test — because the pointer is over the label's glyphs, so
mapping the click through `offset_at_point` would drop the caret at whichever
end of the value the label sat on and start a drag from there.

**No UA rule was added**, deliberately: `label` is already inline by default,
and a UA declaration is the one thing here that could move `widgets.ppm`.

**TAB MOVES FOCUS (2026-07-28).** It reached the browser as DOM code `"Tab"`
and nothing claimed it, so focus never moved. `focus_next()` walks
`focusable_controls()` — every control, in document order, that is not disabled
and has a fragment (`display:none` leaves none, and tabbing somewhere invisible
is how focus appears to vanish). It wraps at both ends, and Shift+Tab runs it
backwards. It is a DEFAULT ACTION like every other key here, so it sits after
`dispatch_key` and a page's `preventDefault` suppresses it. Two gaps written
down rather than implied: no `tabindex` ordering, and each radio is its own stop
rather than a group being one. `text_input` now drops control characters too —
SDL never sends a tab there, but the headless path can, and it would be inserted
into the very field Tab exists to leave.

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

**And it is scaled NEAREST.** Letterboxing was treated as a coordinate problem
for a year — convert the pointer, pin the viewport — and the FILTER went
unnoticed: SDL3 defaults a texture to `SDL_SCALEMODE_LINEAR`, so the 320x240
playfield was bilinearly smeared over 960x720 and invaders looked softly out of
focus at an exact 3x. `present()` sets `SDL_SCALEMODE_NEAREST` on the texture it
creates. Unconditional, because the only path that ever SCALES is this one — a
resize without logical presentation reflows the page and the texture is
recreated at the new size, so the blit is 1:1 and the filter is unobservable.

Note that no test can see this. Goldens, `--headless`, and
`CTBROWSER_SCREENSHOT` all read `browser::read_pixels()`, which never reaches
SDL; the scale mode exists only in the window blit. It is verified by running an
example and looking at it.

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

