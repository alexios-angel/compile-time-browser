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

**v1 IS NOT DELETED.** What it still has that v2 does not: real fonts, audio,
the BabylonJS shim, `<select>` popups and page-level text selection. See
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
