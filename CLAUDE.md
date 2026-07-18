# CLAUDE.md — compile-time-browser (ctbrowser)

The assembly of the compile-time web stack: ONE HTML source (markup +
<style> + <script>) parses ENTIRELY at compile time — cthtml → DOM
type, ctcss → sheet type, ctjs → script type, chained via the
type→text→type pivot in page.hpp — and runs at runtime against a
mutable DOM, the ctcss cascade, a block layout pass and an SDL3
window. Namespace `ctbrowser`. C++20 only. Work on `main`. Prefer `rg`.

## Build & test
```bash
git submodule update --init --recursive    # three bricks + their lark
make            # bakes the COMBINED PCH once (JS tables = tens of
                # minutes, ~4-6 GB - NEVER build in parallel with other
                # grammar bakes on this 7.5 GB WSL2 box), then builds
                # and RUNS the headless engine suite
cmake -B build && cmake --build build -j && ctest --test-dir build   # + examples if SDL3 found
```
Flags: `-O2 -pedantic -Wall -Wextra -Werror -Wconversion`. Tests are
EXECUTABLES, SDL-free, headless. Examples need SDL3 (linuxbrew's here;
`pkg-config sdl3` in examples/Makefile, `find_package(SDL3)` in CMake).
CMake shares one PCH via the `ctbrowser-pch-anchor` target (REUSE_FROM).

## Layout
- `include/ctbrowser.hpp` — umbrella, ENGINE only (no SDL): page + dom + layout + script + engine.
- `include/ctbrowser/page.hpp` — the compile-time assembly. `to_fixed<Provider>()` re-enters a constexpr string_view as a ctll::fixed_string NTTP; `tag_text<Doc, Tag>` collects the concatenated text of every <style>/<script> element FROM THE DOM TYPE. `page<Src>`: doc_type/sheet_type/script_type/title().
- `include/ctbrowser/dom.hpp` — runtime `node` tree (tag/id/classes/attrs/text/children/parent, inline_style map, canvas_w/h + pixels 0xAARRGGBB, layout rect x/y/w/h), `instantiate<DocType>()`, find_by_id/find_first/hit_test, class helpers, ctcss chain().
- `include/ctbrowser/layout.hpp` — `style_fn` (std::function so the engine isn't templated on the sheet), `computed_style` (inline styles beat the sheet), block layout → `paint_cmd` list (box/text/canvas) + node rects. Skips head/style/script/title; display:none prunes; text wraps in square font_px glyphs.
- `include/ctbrowser/script.hpp` — ctjs bindings: getElementById → element handle object (setText/addClass/...), getContext → canvas ctx (fillStyle property read back by fillRect/putPixel/clear natives — the real canvas idiom), setTitle; `deliver()` calls script fns if defined (onClick(id)/onKey(name,down)/onFrame(dt)).
- `include/ctbrowser/engine.hpp` — `engine<Page>`: doc + title + resolver + script run with bindings; frame(viewport_w), click_at, key, tick. SDL-free; what the tests drive.
- `include/ctbrowser/app.hpp` — SDL3 shell: run_app<Page>(app_options). Boxes = filled rects, text = font8x8 scaled, canvas = streaming SDL_Texture. `SDL_VIDEODRIVER=dummy` + `CTBROWSER_TEST_FRAMES=N` (env, read by run_app) = headless run.
- `include/ctbrowser/font8x8.hpp` — GENERATED from public-domain font8x8 (dhepper); glyph_pixel(c,row,col).
- `external/compile-time-{html,javascript,css}` — SUBMODULES (recursive: each carries lark). ctlark/ctll resolve through compile-time-html's copy — exactly ONE lark on the include path.

## Decisions
- Scripts MUTATE nodes but never create/remove them → bindings hold raw node pointers; `engine` is noncopyable, doc outlives script result.
- Click delivery: deepest hit-test node, walk up to first non-empty id, call onClick(id).
- Layout: px only; canvas box = its pixel size; backgrounds paint in a pre-pass (back-to-front), then text/canvas in traversal order.
- The bricks' own semantics/limits apply verbatim (see their CLAUDE.md).

## v0.2 game-engine surface
- `image.hpp` (engine, SDL-free): mini BMP reader (24/32bpp, compression 0/3, top-down or bottom-up) + `image_store` behind loadImage/drawImage — sprite tests run headless.
- Canvas ctx additions (script.hpp): clearRect→TRANSPARENT (canvas textures get SDL_BLENDMODE_BLEND so the page shows through), strokeRect/strokeStyle, fillCircle, fillText (font8x8 into pixels), drawImage/drawImageRegion (nearest, alpha-test a==0).
- Input state lives ON the engine (keys_down set, mouse x/y/down), fed by the shell, exposed as isKeyDown/mouseX/mouseY/isMouseDown; engine ctor takes `extra` bindings — the shell injects playSound/setVolume (audio.hpp mixer), screenshot, setFullscreen.
- Screenshots (screenshot.hpp, shell): SDL_RenderReadPixels → PNG via vendored stb_image_write; a `.ppm` path writes raw P6 (golden-comparable). Works under the dummy driver.
- `app_options`: fixed_dt (auto 1/60 when CTBROWSER_TEST_FRAMES set → deterministic), logical_w/h (LETTERBOX presentation; mouse events go through SDL_ConvertEventToRenderCoordinates), fullscreen, screenshot_path/screenshot_frame (-1 = last).
- Render verification: tests/render.cpp is the ONLY SDL-linked test (Makefile builds it when `pkg-config sdl3` succeeds), sets dummy drivers itself, pixel-samples the PPM and byte-compares tests/golden/render.ppm (`REGOLDEN=1 ./tests/render` regenerates). ctest runs tests/examples with WORKING_DIRECTORY = source root (asset paths are repo-relative) and CTBROWSER_SCREENSHOT into the build dir; CI uploads shot-*.png artifacts.
- Assets are GENERATED: `python3 tools/gen-assets.py` (sprites.bmp 24x8 sheet: alien A/B + ship; blip.wav square-wave) — deterministic, no foreign binaries.
- **SDL3 satellites are OPTIONAL, detected by the build** (pkg-config `sdl3-image/-mixer/-ttf`; CMake find_package) → defines `CTBROWSER_WITH_IMAGE/MIXER/TTF` + links. image → `image_store.decoder` hook (IMG_Load→ARGB8888, engine registry stays plain pixels, BMP path still first); mixer → `audio_mixer` MIX_* implementation (MIX_CreateMixerDevice/LoadAudio/pooled tracks, master gain), stream-WAV fallback preserved in the #else; ttf → `detail::ttf_text` in app.hpp (fonts per px size, glyphs rendered WHITE + color-modded, texture cache capped 256, `probe_font()` scans DejaVu/Liberation/Helvetica/Arial when `app_options.font_path` empty) + `engine.measure` hook feeding layout's greedy wrap. Canvas fillText stays font8x8 (goldens deterministic); TTF affects PAGE text only. CI runners lack SDL3 → render test + examples skip there; goldens are a local check.

## GOTCHAS
- **Any grammar change in a brick re-bakes the combined PCH** (~30 min).
- **Submodule bumps**: update the brick's gitlink AND check lark stays consistent across bricks (headers must be identical; only compile-time-html's copy is on the include path).
- **Attribution**: preserve NOTICE (CTLL/CTRE via notre, lark-parser, font8x8 public domain, SDL zlib, not bundled).
