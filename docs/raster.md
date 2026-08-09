# Raster — tiles, backends, and real fonts

`include/ctbrowser/raster/` — `draw.hpp`, `surface.hpp` and `tile.hpp` at the
top; `backend/` holds `backend.hpp`, `software.hpp`, `renderer.hpp`,
`compositor.hpp` and `pipeline.hpp`; `text/` holds `ttf.hpp` (SDL3_ttf) and
`font8x8.hpp`, the built-in bitmap fallback the goldens are rendered with.

## FONTS: real ones (stage 6, 2026-07-25)

**Text is drawn with outline faces.** `raster/text/ttf.hpp` is a `font_backend`
over **SDL3_ttf** — the one place the engine knows about SDL, and a deliberate
exception rather than an oversight. `TTF_Init` needs no video subsystem, so real
text is still TESTABLE with no display, which is what makes the exception safe.
`tests/lint/api_surface` SWEEPS `src/` and names the exceptions; the old
hand-written list could not catch a new file that used SDL, and did not.

## SVG

`raster/svg.hpp` is two declarations — `svg_available()` and `render_svg(source,
w, h)` — and `src/raster/svg.cpp` is the only file in the tree that includes a
**plutosvg** header. Stricter than `raster/text/ttf.hpp`, which puts `<SDL_ttf.h>` in
a public header and pays with an `api_surface` allow-list entry; plutosvg hides
completely and needs no exception.

**It rasterises at the size asked for**, and that is the whole point.
`draw_image` scales nearest-neighbour, so a vector graphic decoded once at its
natural size and then enlarged looks WORSE than a PNG — stair-stepped along every
diagonal. `shell/page/svg_cache.hpp` caches by `(content, width, height)` and the painter
passes the *snapped* box, so the blit is 1:1.

The bitmap is **unpremultiplied** on the way in: plutovg stores premultiplied
ARGB, `paint::bitmap` is straight, and copying one to the other looks *almost*
right — just dark wherever alpha is partial, which is every antialiased edge.

**What plutosvg 0.0.8 does NOT do**, measured rather than assumed: no `<text>` or
`<tspan>` AT ALL, and `clip-path`, `mask`, filters, patterns and markers are
parsed and silently ignored. `opacity` and `transform` work. `examples/pages/svg.html`
shows the unsupported ones beside a plain shape so the gap is visible.

That no-text limitation has a useful consequence: SVG rasterisation touches no
font, no FreeType, no HarfBuzz and no `CTBROWSER_FONTS`, so it is **deterministic
and goldenable**. `tests/golden/svg.ppm` is one image compared on both platforms.
That only holds while the versions match — plutosvg 0.0.8 / plutovg 1.3.3 are
PINNED to the Windows sysroot's, `tools/Brewfile` names them, and
`src/CMakeLists.txt` warns on a mismatch.

Optional, like SDL3_ttf: without plutosvg a page lays out IDENTICALLY (the
natural-size scan in `shell/page/svg_cache.hpp` is in-engine and never asks plutosvg) and
simply draws no graphics.

## Fonts on Windows

The Windows cross-build links a **static** SDL3_ttf with the **full stack** —
FreeType, HarfBuzz and plutosvg — built by `../llvm-mingw/build-sdl3.sh`. The
engine now links plutosvg **directly** as well, for the above; on Windows it was
already in the sysroot for SDL3_ttf's colour glyphs. That
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

## FONT8X8 HAS FOUR STYLES NOW (2026-07-28)

It had one face, so `<b>` and `<h1>` drew identically to body text — and since
the goldens render with `CTBROWSER_FONTS=font8x8`, they could not see a
font-weight bug **at all**. That is how a UA sheet with no `font-weight` in it
survived: the one test that byte-compares pixels was blind to weight by
construction.

Bold and italic are **synthesised from the one set of bitmaps**, which is what
bitmap fonts have always done and the only option here — there is no second
table and no outline to thicken. Bold is the glyph OR'd with itself one pixel
right; italic shears the rows, two pixels of lean over the cell, top-heavy.
Neither needs data of its own.

**The advance does not change.** A styled glyph OVERHANGS its 8-wide cell
rather than widening it — one column for the smear, two for the lean. Layout
measures with `font8x8_advance` and the rasterizer draws with `draw_text`; a
style that advanced differently from the way it is drawn would put every caret
and every wrap in the wrong place, which is worse than a bold occupying the same
cells as its regular. An italic overhanging slightly is what italics do anyway.

The comment this replaces said bold and italic were *deliberately* not
synthesised, "because a fake that is wrong by a pixel is worse than an honest
sameness". That reasoning was about the ADVANCE, and it still holds — the
advance is unchanged. It did not follow that the glyphs had to be identical too.

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
`tests/unit/fonts_basics` drives it from twelve threads on COLD glyphs. Going
through the browser does not test it: layout measures every run before raster
draws it, so the parallel path only ever reads. Removing the lock and watching
TSan stay silent is what showed that up.

Rendering turned out to be byte-identical between FreeType 2.14.3 (linux) and
2.13.3 (the mingw sysroot) for these faces — every example matches across
platforms. That is not guaranteed in general, which is what `CTBROWSER_FONTS`
is for.

## PNG AND JPEG (2026-07-29)

The engine decodes **BMP** on its own. Everything else arrives through the
optional **SDL3_image**, installed by `install_image_decoder` in
`src/app/app.cpp` — the one place in the tree where SDL and image decoding
meet, because the shell must not learn that SDL exists. `IMG_Load_IO` handles
PNG and JPEG (and whatever else that build of SDL3_image was compiled with);
the surface is converted to ARGB8888 and copied into a `paint::bitmap`.

That path had **never been exercised by a test**. `<img src=x.png>` could have
been blank on every build and nothing would have said so, which is the same
shape of gap the p5 API probe exists to close. `examples/pages/image-formats.html`
now draws a generated 16x16 PNG at three sizes against a golden, and
`tools/gen/gen-assets.py` writes that PNG — by hand, from `zlib` and `struct`, so the
script stays dependency-free and no foreign binary is committed.

**PNG is goldened and JPEG is not**, and the reason is byte-exactness rather
than confidence: PNG is lossless, so two platforms decode it identically, while
two libjpeg builds may differ in the last bit and would make the golden
platform-dependent for a reason that is not a regression. JPEG is verified by
hand through `ctbrowse` and works.

**A headless `shell::browser` has no decoder.** `install_image_decoder` is
called by `run_app`, so a unit test - which `tests/lint/api_surface` requires to be
SDL-free - reads BMP only. That is why the coverage here is an EXAMPLE with a
golden rather than a test in `tests/`.

The golden is gated on `CTBROWSER_WITH_IMAGE` in `examples/CMakeLists.txt`,
exactly as the SVG one is gated on plutosvg: without the dependency the page
lays out identically and draws nothing, and comparing then fails for the wrong
reason.


## WEBGL: GLSL AND A SOFTWARE RASTERISER (2026-07-30) — DELETED 2026-08-04

> **Everything from here to the end of this file describes code that no longer
> exists.** `glsl.hpp`, `softgl.hpp` and the SPIR-V emitter were deleted when
> WebGL was rewritten onto ANGLE; `tests/glsl_basics.cpp` went with them, and
> `raster/gl.hpp` is the GL device now. The rest of this document — fonts,
> glyph rasterisation, the font8x8 fallback, SVG — is current.
>
> This section is kept rather than cut because its numbers are the argument for
> the rewrite: an interpreter at 1.03 M frag/s, 60 ms for a 200x200 full-screen
> draw, and a `webgl-triangle.html` sized at 160x160 to stay under it. See
> `history/webgl.md` for the design and `plans/webgl-rewrite.md` for what
> replaced it.
>
> Marked here on 2026-08-09. Until then the only warning lived in `CLAUDE.md`.

`raster/` grew a language. `glsl.hpp` is GLSL ES 1.00 - preprocessor, parser,
type model and a reference evaluator - and `softgl.hpp` is a triangle rasteriser
that runs it. Together they are what makes `canvas.getContext('webgl')` return
something real. `docs/history/webgl.md` has the whole design and the staging; this
is what a reader of `raster/` needs to know.

**Why it is here.** WebGL has no fixed pipeline: `drawArrays` runs a vertex
shader per vertex and a fragment shader per fragment, and nothing draws without
executing them. That is software rasterisation, which is what this directory
does. It stays SDL-free like everything else here, and the GPU back end -
stage 7, not yet written - will live in `gpu/` behind an interface.

**The parse corpus is p5's own shaders.** `tools/gen-glsl-fixtures.py` extracts
the sixteen shaders p5.js ships into `tests/glsl/`, and the test compiles each
one the way p5 would. They are somebody else's GLSL, which is the only kind
worth testing a parser against - and reading them is what added the
preprocessor, structs, function overloading and uniform arrays to the plan.

**The four rules that look almost right when they are wrong**, each with its own
test because none of them shows up as an error:

  * **Matrices are column-major.** `m[i]` is a column and `m * v` sums columns.
    Transposed gives a plausible wrong rotation.
  * **Perspective-correct interpolation.** A varying is interpolated as `attr/w`
    against `1/w` and divided at the end. Screen-linear looks fine on a quad
    facing the camera and bends a texture along each triangle's diagonal
    otherwise.
  * **The top-left fill rule.** A pixel centre on a shared edge belongs to
    exactly one of two triangles; without it a seam double-draws or drops.
  * **The Y flip reverses the winding.** A counter-clockwise triangle in NDC has
    a NEGATIVE doubled area in window coordinates, because y grows downwards
    here and upwards there. Getting the sign backwards culls the faces you meant
    to keep, and the symptom is a black screen.

**What is not drawn**, named rather than discovered: points and lines
(`drawArrays(gl.POINTS)` draws nothing), multisampling, `gl_FragDepth`,
framebuffer objects (every draw goes to the canvas), instancing, and near-plane
clipping - a triangle with any vertex at or behind the eye is dropped rather
than split.

**A PROGRAM CAN BE ENUMERATED, and that is not a detail.** `getProgramParameter`
answers `ACTIVE_UNIFORMS` and `ACTIVE_ATTRIBUTES`, and `getActiveUniform` /
`getActiveAttrib` walk them - names, array sizes and GL type codes, off the
interface the GLSL front end already recorded. Attributes come back in the order
`link_program` assigned, so index i is location i.

Nothing that asks for uniforms **by name** needs any of it, which is why it was
missing: every page and every test here wrote its own shader and knew what was in
it. p5 does the opposite, and got zero for both - so it bound no attributes, set
no matrices, and drew a cube with no vertices, with no GL error and an empty
canvas. The type codes have to be constants on the CONTEXT too, because a caller
dispatches with `switch (uniform.type) { case gl.FLOAT_MAT4: ... }`; undefined
matches no case, and there is no default.

**Speed.** The evaluator is a tree walker at 0.6 M fragments/sec -
`ctbrowser-test-glsl_basics --bench` reports it. That is 60 ms for a 200x200
full-screen draw, which is why `examples/pages/webgl-triangle.html` is 160x160.
Stage 3 of the plan replaces the evaluator with a bytecode VM over
eight-fragment packets; it is scheduled after the pipeline exists, so the
optimisation lands where the measurement says it matters.
