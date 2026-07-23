> **Attribution:** built on
> [compile-time-html](https://github.com/alexios-angel/compile-time-html),
> [compile-time-javascript](https://github.com/alexios-angel/compile-time-javascript)
> and [compile-time-css](https://github.com/alexios-angel/compile-time-css)
> (with [compile-time-containers](https://github.com/alexios-angel/compile-time-containers)
> carrying the page as a structural NTTP string),
> rendered with [SDL3](https://libsdl.org). Text renders with the
> public-domain [font8x8](https://github.com/dhepper/font8x8).
> Apache License 2.0 with LLVM Exceptions; see [NOTICE](NOTICE).

# ctbrowser — the compile-time browser

**One HTML string is the whole application.** The page rides as a
template argument, and the three bricks' *constexpr value parsers* —
[cthtml](https://github.com/alexios-angel/compile-time-html),
[ctcss](https://github.com/alexios-angel/compile-time-css) and
[ctjs](https://github.com/alexios-angel/compile-time-javascript) — can
prove it well-formed and even resolve its initial styles *while your
code compiles* (`static_assert` over the real cascade). At startup the
same parsers build the DOM, the stylesheet and the script from the
page text; the script drives real DOM and `<canvas>` APIs, styles
re-resolve through the CSS cascade after every mutation, and **SDL3**
draws the result in a window on Windows, macOS, Linux and the BSDs —
which is exactly the substrate multimedia software wants: games and
emulators are a `<canvas>`, an `onFrame(dt)` and an `onKey` away.

```c++
#include <ctbrowser.hpp>
#include <ctbrowser/app.hpp>
#include <SDL3/SDL_main.h>

using app = ctbrowser::page<R"(<!DOCTYPE html>
<title>counter</title>
<style>
    h1      { font-size: 32px; color: #222222; }
    #count  { font-size: 48px; color: gray; }
    #count.hot { color: #ff8800; }
</style>
<h1>Clicks</h1>
<p id=count>0</p>
<script>
    let clicks = 0;
    function onClick(id) {
        clicks += 1;
        let el = getElementById("count");
        el.setText(String(clicks));
        if (clicks >= 5) { el.addClass("hot"); }
    }
</script>)">;

// the page's script is provably parseable, and the initial style is a
// compile-time fact - the value parsers run in constant evaluation:
static_assert(ctjs::vp::is_valid(app::script_text()));
constexpr ctcss::element_ref chain[] = {{"html"}, {"body"}, {"p", "count", ""}};
static_assert(ctcss::query(ctcss::parse_value(app::style_text()), chain, "color") == "gray");

int main(int, char **) { return ctbrowser::run_app<app>(); }
```

And the games proof — `examples/game.cpp` is pong written in page
JavaScript on a `<canvas>`:

```html
<canvas id=game width=320 height=200></canvas>
<script>
    let ctx = getContext("game");
    function onKey(key, down) { /* move the paddle */ }
    function onFrame(dt) {
        ctx.fillStyle = "#102030";
        ctx.fillRect(0, 0, 320, 200);
        ctx.fillStyle = "#ff8800";
        ctx.fillRect(x - 4, y - 4, 8, 8);   // the ball, at 60fps
    }
</script>
```

## What happens when

**At compile time** ([`page.hpp`](include/ctbrowser/page.hpp)): the
page NTTP is re-materialized as bytes (`html_bytes`) and the text of
every `<style>`/`<script>`/`<title>` element is extracted with a
linear scan. You get `page::html_text()`, `page::style_text()`,
`page::script_text()`, `page::title()` — all `constexpr`
`string_view`s — and because the bricks' parsers are value functions,
`static_assert`s over any of them work: `ctjs::vp::is_valid(...)`,
full cascade resolution via
`ctcss::query(ctcss::parse_value(...), chain, prop)`, DOM facts via
`cthtml::parse(...)`.

**At runtime**:

* [`dom.hpp`](include/ctbrowser/dom.hpp) — `instantiate_html` builds
  the mutable tree (tag/id/classes/attributes/text/children,
  `<canvas>` pixel buffers, inline styles) from the page text via
  cthtml's parser; nodes are stable so script bindings hold plain
  pointers. The whole DOM is constexpr: parse + instantiate + mutate +
  query fold in constant evaluation (tests/dom.cpp is the proof)

* [`script.hpp`](include/ctbrowser/script.hpp) — the DOM API as ctjs
  host bindings: `getElementById(id)` returns an element handle
  (`text/setText/addClass/removeClass/toggleClass/hasClass/style/attr`),
  `getContext(id)` returns a canvas context with the real idiom
  (`ctx.fillStyle = "#f80"; ctx.fillRect(...)`, plus `putPixel` and
  `clear` for emulator-style framebuffers), `setTitle` retitles the
  window; events arrive as plain script functions `onClick(id)`,
  `onKey(name, down)`, `onFrame(dt)`

* [`layout.hpp`](include/ctbrowser/layout.hpp) — style resolution
  (inline styles → the page stylesheet through ctcss's cascade) and
  CSS-flavored block layout: `width/height/margin/padding/font-size`
  in px, `background(-color)`/`color`, `display:none`, text wrapped in
  the embedded 8×8 font scaled to font-size; produces a paint list +
  hit-test rects

* [`app.hpp`](include/ctbrowser/app.hpp) — the SDL3 shell: window,
  renderer, event loop; boxes as filled rects, text via
  [font8x8](https://github.com/dhepper/font8x8) (no font library
  needed), each `<canvas>` streamed into an `SDL_Texture`;
  `SDL_VIDEODRIVER=dummy` + `CTBROWSER_TEST_FRAMES=N` runs any app
  headless (that is how CI drives the examples)

The engine ([`engine.hpp`](include/ctbrowser/engine.hpp)) is
SDL-free: `ctbrowser::engine<Page>` instantiates the DOM, runs the
script, lays out and delivers events — the whole test suite runs
headless against it.

## The game engine surface (v0.2)

Games and emulators talk to the engine through the page's own script:

* **canvas 2D**: `fillRect`, `clearRect` (transparent, the page shows
  through), `strokeRect`/`strokeStyle`, `putPixel` (emulator
  framebuffers), plus the documented extensions `fillCircle(x, y, r)`,
  `fillText(text, x, y[, scale])` (the embedded 8×8 font) and sprites:
  `loadImage(path)` with `drawImage(img, dx, dy[, dw, dh])` and
  `drawImageRegion(img, sx, sy, sw, sh, dx, dy, dw, dh)` — BMP
  (24/32bpp, alpha honored) built in; PNG/JPG/WebP and more when
  [SDL3_image](https://github.com/libsdl-org/SDL_image) is installed

* **input**: polled - `isKeyDown("Left")`, `mouseX()`, `mouseY()`,
  `isMouseDown()` - and evented - `onKey(name, down)`,
  `onMouseMove(x, y)`, `onMouseDown(x, y)`, `onClick(id)`;
  `element.rect()` converts mouse to element-local coordinates

* **sound**: `playSound(path)` and `setVolume(v)` — with
  [SDL3_mixer](https://github.com/libsdl-org/SDL_mixer) installed,
  sounds mix properly on pooled tracks and WAV/OGG/MP3/FLAC all load;
  without it, a built-in fallback plays plain WAV through raw SDL
  audio streams

* **text**: page text renders in a real TrueType font when
  [SDL3_ttf](https://github.com/libsdl-org/SDL_ttf) is installed
  (`app_options.font_path`, or automatic probing of common system
  fonts), with proper text measurement feeding the layout's line
  wrapping; the embedded public-domain 8×8 font remains the
  zero-dependency fallback — canvas `fillText` always uses it, so
  golden images stay deterministic

* **presentation**: `app_options.logical_w/h` renders a fixed
  resolution letterboxed and scaled to the window (pixel-perfect
  retro), `fullscreen` + `setFullscreen(bool)`, `fixed_dt` for
  deterministic timesteps

* **screenshots**: `app_options.screenshot_path` /
  `CTBROWSER_SCREENSHOT=path` env / `screenshot("shot.png")` from
  script - PNG via the vendored public-domain stb_image_write (a
  `.ppm` path writes raw pixels, which is what the golden tests
  compare)

* **Math**: the ctjs runtime carries the full game-loop set -
  `sin cos tan atan atan2 hypot exp log floor round abs min max
  sqrt pow random` (seeded, reproducible) and friends

`examples/invaders.cpp` uses all of it at once. Rendering is verified
two ways: `tests/render.cpp` drives the REAL SDL shell headless
(dummy video driver = deterministic software renderer), samples pixels
at known coordinates and byte-compares a golden image
(`REGOLDEN=1 ./tests/render` regenerates); CI uploads every example's
screenshot as an artifact so a human can look at what was drawn.

## The interaction model (browser defaults)

Elements behave like they do in Firefox, out of the box:

* **hover/active/focus/checked/disabled styling**: the engine tracks
  the pointer and feeds real state into the cascade, so
  `button:hover { ... }`, `#panel:hover .child { ... }`,
  `input:checked { ... }` just work — restyled every frame. Unknown
  pseudos (`:visited`, `::before`) parse and never match, like a real
  browser.
* **clicks fire on release** (press+release pairing, nearest common
  ancestor), listeners bubble with a REAL `preventDefault()` /
  `stopPropagation()`, and then the element's **default action** runs:
  checkboxes toggle, radios check their group exclusively, `<summary>`
  toggles its `<details>`, `<label>` forwards to its control, and
  **`<a href>` opens the system web browser at that URL** (SDL's
  `SDL_OpenURL`; fragment `#hash` links update
  `document.location.hash` instead). Disabled controls dispatch
  nothing.
* **a Firefox-derived UA stylesheet** ([`ua.hpp`](include/ctbrowser/ua.hpp),
  values from Gecko's `html.css` and the modern form theme) styles
  every element before the page says anything — heading scale (bold
  serif), list markers and quote indents, underlined link blue,
  button/input chrome with the `#8f8f9d` frames and `#0060df` checked
  accent, `<hr>` rules, `<pre>` layout preservation, hidden
  `<head>`/`<template>`. Page styles always win (author beats UA);
  inline styles beat both.
* **real typefaces, multiple per document**: the vendored
  [`fonts/`](fonts/) directory (Tinos serif · Fira Sans · Cousine
  mono, SIL OFL, four styles each) is `std::embed`-ded by
  [`fonts.hpp`](include/ctbrowser/fonts.hpp); every element resolves
  its own `font-family`/`-weight`/`-style` through the cascade and the
  renderer keeps ALL the faces live at once — page `@font-face`
  families included. `text-decoration` draws real underlines and
  strike-throughs. A checkout without `fonts/` falls back to a system
  font or the built-in bitmap face.
* **text editing**: click a text `<input>` or `<textarea>` and type —
  a real caret (code-point-aware Backspace/Delete/arrows/Home/End,
  line motion in textareas), `input` events on every edit, `change` on
  blur, `.value` from script.
* **forms**: `<button>`/`<input type=submit>` submit (Enter in a text
  field does the implicit submission), `type=reset` restores initial
  values, `onsubmit`/`addEventListener("submit")` are cancelable, and
  `form.submit()`/`form.reset()` work from script.
* **scrolling**: pages taller than the window scroll — mouse wheel,
  PageUp/PageDown, Home/End, or grab the Firefox-style overlay
  **scrollbar** on the right (drag the thumb, click the track to page;
  hide it with `scrollbar-width: none`, thin it with `thin`).
  `position: fixed` elements stay viewport-anchored and hit-testing
  follows the scroll. Overflowing `<textarea>`s scroll under the wheel
  with no scrollbar drawn, and editing keeps the caret in view.
  Resizing the window re-lays-out at the new size: words keep their
  size and rewrap.
* **cursors**: the pointer becomes Firefox's hand over links and an
  I-beam over text and fields — driven by the CSS `cursor` property
  (UA defaults `a { cursor: pointer }`, `input, textarea { cursor:
  text }`), so pages override it like anywhere else.
* **selection + clipboard**: drag to select — character-precise inside
  inputs and textareas (Shift+arrows extend, click places the caret at
  the nearest glyph boundary), per-text-block on the page. Ctrl+C/X/V
  and Ctrl+A do what they say through the system clipboard, `copy`/
  `cut`/`paste` events are cancelable, and `user-select: none` keeps
  text out of page selection.
* **the right-click menu**: Chrome-style Copy / Cut / Paste / Select
  All at the pointer, enabled per context; a page listener calling
  `preventDefault()` on `contextmenu` takes the menu over entirely,
  exactly like a real browser.
* **tables**: rows through `thead`/`tbody`/`tfoot`, equal-width
  columns, 2px border-spacing, `<caption>` above, centered-bold
  `<th>`, and the classic 1px grid when the `border` attribute is set
  — Firefox's borderless default otherwise.
* script surface to match: `.checked`, `.disabled`, `.open`, `.href`,
  `.type`, `getAttribute`/`hasAttribute`, `addEventListener("change")`,
  `document.location.href`/`.hash`.

## v0.1 boundaries

Everything the three bricks document applies (their subsets ARE this
project's subsets). Browser-side:

- block layout only (no inline flow, floats or flex — inline elements
  render as their own rows)
- px lengths; margins/paddings honor per-side values and 1-4-value
  shorthands
- editing: no Tab traversal, no IME composition, page selection is
  per-text-block (not per-character across elements)
- tables: no colspan/rowspan/auto column sizing
- no `<img>` yet; no horizontal page scrolling

The bricks' own APIs remain fully available alongside —
`decltype(app)::doc_type` is an ordinary cthtml document, the sheet an
ordinary ctcss sheet.

## Building

```bash
git submodule update --init --recursive   # the three bricks (+ nested ctc)
cmake --preset default                    # Ninja + Release, std::embed clang
cmake --build --preset default            # builds the suite + examples (SDL3)
ctest --preset default                    # headless tests + 30-frame example runs
# preset `fetch` additionally authorizes the compile-time HTTP fetches
# or on the shared Azure devbox (github.com/alexios-angel/infra):
./tools/remote-build.sh                   # sync + converge toolchain + build
./build/examples/ctbrowser-example-counter
./build/examples/ctbrowser-example-game
```

**Windows builds** cross-compile through the same CMake tree: the
`windows` / `windows-fetch` presets use
[`cmake/toolchain-windows-x86_64.cmake`](cmake/toolchain-windows-x86_64.cmake)
(the std::embed [llvm-mingw](https://github.com/alexios-angel/llvm-mingw)
toolchain + libsdl's official SDL3-devel mingw package; see the file
header for the expected locations), and the `windows-dist` target
collects the exes plus `SDL3.dll` — the one runtime dependency,
everything else links static — into `examples-windows/`:

```bash
cmake --preset windows-fetch
cmake --build --preset windows-fetch
cmake --build --preset windows-fetch --target windows-dist
# or end-to-end on the devbox: ./tools/remote-build.sh windows
```

SDL3 comes from your system (`find_package(SDL3)` /
`pkg-config sdl3`); everything else is header-only C++23 (clang only). The
**satellite libraries are optional and auto-detected** — install them
for the full experience (Homebrew/Linuxbrew:
`brew install sdl3_image sdl3_mixer sdl3_ttf`; they also ship on every
platform SDL does) and the build defines
`CTBROWSER_WITH_IMAGE/MIXER/TTF` and links them; without them the
built-in fallbacks (BMP sprites, WAV streams, 8×8 font) keep every
feature working. The CMake install flattens
`include/{ctbrowser,cthtml,ctjs,ctcss,ctc}` side by side and
ships `ctbrowser.pc`.

## The stack

| layer | repo | at compile time | at runtime |
|-------|------|-----------------|------------|
| markup | [compile-time-html](https://github.com/alexios-angel/compile-time-html) | page provable via `cthtml::parse` in a `static_assert` | same parser instantiates the DOM, scripts mutate |
| style | [compile-time-css](https://github.com/alexios-angel/compile-time-css) | initial styles provable via `parse_value` + `query` | same `query()` restyles after mutations |
| behavior | [compile-time-javascript](https://github.com/alexios-angel/compile-time-javascript) | script provable via `ctjs::vp::is_valid` | closures, DOM/canvas APIs, events |
| output | [SDL3](https://libsdl.org) | — | window, input, textures, cross-platform |

## License

Apache License 2.0 with LLVM Exceptions (see [LICENSE](LICENSE)).
ctc (compile-time-containers) is MIT; font8x8 is public domain
(Daniel Hepper / Marcel Sondaar); SDL3 is zlib-licensed and not
bundled; see [NOTICE](NOTICE).
