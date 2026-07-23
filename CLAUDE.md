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
- `include/ctbrowser/babylon.hpp` — **BabylonJS core-API SHIM on a software 3D rasterizer** (SDL-free, in the PCH; GLM math — `glm::dvec3/dvec4/dmat4`, column-major). THE ONE SANCTIONED EXCEPTION to "no library-specific shims" (user-approved: Babylon needs WebGL, ctbrowser has none, so we implement `BABYLON.*` directly instead of WebGL). `namespace ctbrowser::babylon`: `r3d` = pure renderer (LH column-vector matrices, lookAtLH/perspectiveFovLH, z-buffered barycentric triangle raster, flat Lambert shading, Box/Sphere/Ground/Cylinder gens) writing 0xAARRGGBB into a raw pixel span — testable via `CTBROWSER_BABYLON_RENDER_ONLY` (no ctjs/DOM). **The renderer AND the glTF loader are fully `constexpr`** (std::sin/cos/sqrt aren't until C++26): vec/mat arithmetic is GLM's (its construction/+/-/dot/cross/mat*mat/mat*vec ARE constexpr on this clang), while the ops GLM can't fold `if consteval`-split — at COMPILE time a per-degree cos-table trig (`fsin/fcos/ftan`, interpolation + quadrant symmetry, ~5e-5 error) + `norm3`/`fsqrt`/`ffloor`/`fceil` via constexpr helpers (Newton sqrt + int-cast floor/ceil; `glm::abs` is constexpr and used directly); at RUNTIME `glm::sin/cos/tan`/`glm::normalize`/`glm::sqrt/floor/ceil` (full precision). Matrix builders: `glm::mat4(1.0)`/`glm::translate`/`glm::scale` (constexpr); `glm::rotate`/`glm::yawPitchRoll`/`glm::lookAtLH`/`glm::perspectiveLH_ZO` at runtime with the hand-rolled fill at compile time (all conventions — LH, [0,1] depth, YXZ order — verified to agree with the constexpr fills in the test). So a whole 3D frame rasterizes at compile time AND runtime uses GLM; the JSON parser uses `unique_ptr` (out-of-line dtor for the recursive `jval`), a constexpr number parser + `bit_cast` byte reads, so a whole GLB parses at compile time (both proven by static_asserts in tests/babylon.cpp); `detail` = factory-style `BABYLON.*` natives over a shared `world` (meshes/lights/cameras/scenes; JS handles carry `__mesh`/`__scene` indices — the `__node` idiom). Surface: Engine(canvas→`ev.node_of`)/Scene/ArcRotateCamera(+drag orbit via mouse listeners)/FreeCamera/Hemispheric+DirectionalLight/StandardMaterial/MeshBuilder.Create*+legacy Mesh.Create*/Vector3(statics on function props; methods read `cx.current_this`)/Color3/Color4. `engine.runRenderLoop(cb)` = self-re-registering rAF wrapper (weak_ptr<world> to avoid a cycle) pumped by `engine::tick`; `scene.render()` reads mesh transforms back from the live JS Vector3s each frame and rasterizes into the `<canvas>` pixels (presentation is automatic). `install(out, ev, images)` is called from `engine::all_bindings`. **glTF/GLB model loading**: `namespace gltf` is a pure-C++ minimal GLB loader (own tiny JSON parser; POSITION+TEXCOORD_0+indices primitives; node transforms baked into world-space verts; RH→LH conversion — negate Z + flip winding; PBR baseColorFactor→flat diffuse). **baseColor TEXTURES**: the constexpr parse copies each texture's encoded PNG/JPEG bytes (no decode at compile time); at RUNTIME `r3d::decode_texture` (stb_image, vendored, `STB_IMAGE_STATIC`) turns them into a `r3d::texture` (0xAARRGGBB texels) shared on the `mesh_rec` (and copied by clone), and the rasterizer samples it with perspective-correct UVs + an alpha test (`draw_item.tex`). No PBR/IBL/normal maps/hierarchy. `BABYLON.AppendSceneAsync(url, scene)` resolves the `.glb` from the embedded-asset registry (`find_asset`, same path as `fetch` — the url is auto-embedded because `AppendSceneAsync("` is a needle in assets.hpp; build with `--fetch-allow`), parses it, adds meshes+named materials to the scene, returns a SETTLED promise. Stubs so real model-viewer scripts run: `scene.getMaterialById/createDefaultCamera(fits model bounds)/createDefaultSkybox/debugLayer.show().select`, `CubeTexture.CreateFromPrefilteredData`, `engine.hostInformation.isMobile`. OUT OF SCOPE (accepted+ignored / no-op): PBR/OpenPBR shading, IBL skybox, physics, shadows, animations, GUI, WebGL parity. tests/babylon.cpp = headless render proof (incl. a box-winding occlusion guard); tests/texture.cpp = PNG decode + textured-quad sampling proof (RENDER_ONLY); examples/{babylon,babylon-freecam,babylon-model}.cpp (the last loads a real glTF via the `fetch` preset - CTBROWSER_EXAMPLES_FETCH=ON). All need GLM (header-only; NO Boost) + SDL3.
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
