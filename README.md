> **Attribution:** built on
> [compile-time-html](https://github.com/alexios-angel/compile-time-html),
> [compile-time-javascript](https://github.com/alexios-angel/compile-time-javascript)
> and [compile-time-css](https://github.com/alexios-angel/compile-time-css)
> (all on the CTLL parser from
> [CTRE](https://github.com/hanickadot/compile-time-regular-expressions)
> by Hana Dusíková, via [notre](https://github.com/alexios-angel/notre)),
> rendered with [SDL3](https://libsdl.org). Text renders with the
> public-domain [font8x8](https://github.com/dhepper/font8x8).
> Apache License 2.0 with LLVM Exceptions; see [NOTICE](NOTICE).

# ctbrowser — the compile-time browser

**One HTML string is the whole application.** The markup, the
`<style>` and the `<script>` are parsed *while your code compiles* —
by [cthtml](https://github.com/alexios-angel/compile-time-html),
[ctcss](https://github.com/alexios-angel/compile-time-css) and
[ctjs](https://github.com/alexios-angel/compile-time-javascript)
respectively, chained type→text→type — so a missing quote in the HTML,
a bad selector in the CSS or a missing semicolon in the JS **fails the
build** with a caret diagnostic. At runtime there is no parsing at
all: the DOM instantiates from its type, the script (compiled by the
C++ optimizer into code specialized for that script) drives real DOM
and `<canvas>` APIs, styles re-resolve through the CSS cascade after
every mutation, and **SDL3** draws the result in a window on Windows,
macOS, Linux and the BSDs — which is exactly the substrate multimedia
software wants: games and emulators are a `<canvas>`, an `onFrame(dt)`
and an `onKey` away.

```c++
#include <ctbrowser.hpp>
#include <ctbrowser/app.hpp>
#include <SDL3/SDL_main.h>

constexpr auto & app = ctbrowser::source<R"(<!DOCTYPE html>
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

// the initial style is a compile-time fact:
constexpr ctcss::element_ref chain[] = {{"html"}, {"body"}, {"p", "count", ""}};
static_assert(ctcss::query(decltype(app)::sheet_type{}, chain, "color") == "gray");

int main(int, char **) { return ctbrowser::run_app<decltype(app)>(); }
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

**At compile time** ([`page.hpp`](include/ctbrowser/page.hpp)):
cthtml parses the page into a DOM *type*; the text of every `<style>`
element is collected *from that type* and re-entered into
`ctcss::parse` as a template argument (`to_fixed`, the type→text→type
pivot), and every `<script>` likewise into ctjs. You get
`page::doc_type`, `page::sheet_type`, `page::script_type`,
`page::title()` — and `static_assert`s over any of them, including
full cascade resolution against element chains.

**At runtime**:

* [`dom.hpp`](include/ctbrowser/dom.hpp) — the DOM type instantiates a
  mutable tree (tag/id/classes/attributes/text/children, `<canvas>`
  pixel buffers, inline styles); nodes are stable so script bindings
  hold plain pointers

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

## v0.1 boundaries

Everything the three bricks document applies (their subsets ARE this
project's subsets). Browser-side: block layout only (no inline flow,
floats or flex); px lengths; scripts mutate elements but do not
create/remove them; no `<img>`, links or scrolling yet. The bricks'
own APIs remain fully available alongside — `decltype(app)::doc_type`
is an ordinary cthtml document, the sheet an ordinary ctcss sheet.

## Building

```bash
git submodule update --init --recursive   # the three bricks (+ their lark)
make            # bakes the combined grammar PCH ONCE (the JS tables are
                # tens of minutes - one time), then builds + RUNS the
                # headless engine suite
# windowed examples (need SDL3; via CMake or the examples Makefile):
cmake -B build && cmake --build build -j && ctest --test-dir build
./build/examples/ctbrowser-example-counter
./build/examples/ctbrowser-example-game
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
`include/{ctbrowser,cthtml,ctjs,ctcss,ctlark,ctll}` side by side and
ships `ctbrowser.pc`.

## The stack

| layer | repo | at compile time | at runtime |
|-------|------|-----------------|------------|
| markup | [compile-time-html](https://github.com/alexios-angel/compile-time-html) | page → DOM type | DOM instantiates, scripts mutate |
| style | [compile-time-css](https://github.com/alexios-angel/compile-time-css) | `<style>` → sheet type, initial styles provable | same `query()` restyles after mutations |
| behavior | [compile-time-javascript](https://github.com/alexios-angel/compile-time-javascript) | `<script>` → AST type, specialized by the optimizer | closures, DOM/canvas APIs, events |
| output | [SDL3](https://libsdl.org) | — | window, input, textures, cross-platform |

## License

Apache License 2.0 with LLVM Exceptions (see [LICENSE](LICENSE)).
CTLL is Hana Dusíková's work via notre; font8x8 is public domain
(Daniel Hepper / Marcel Sondaar); SDL3 is zlib-licensed and not
bundled; see [NOTICE](NOTICE).
