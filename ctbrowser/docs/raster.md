# Raster — tiles, backends, and real fonts

`include/ctbrowser/raster/` — `draw.hpp`, `surface.hpp` and `tile.hpp` at the
top; `backend/` holds `software.hpp` and `compositor.hpp`; `text/` holds `ttf.hpp` (SDL3_ttf) and
`font8x8.hpp`, the built-in bitmap fallback the goldens are rendered with.

## FONTS: real ones (stage 6, 2026-07-25)

**Text is drawn with outline faces.** `raster/text/ttf.hpp` declares a
`font_backend` interface and `make_ttf_backend()`; `lib/Raster/text/ttf.cpp` is
the implementation over **SDL3_ttf** — the one place the engine knows about SDL,
and a deliberate exception rather than an oversight. `TTF_Init` needs no video
subsystem, so real text is still TESTABLE with no display, which is what makes
the exception safe. `test/lint/api_surface` SWEEPS `lib/` and names the
exceptions; the old hand-written list could not catch a new file that used SDL,
and did not.

## SVG

`raster/svg.hpp` is two declarations — `svg_available()` and `render_svg(source,
w, h)` — and `ctbrowser/lib/Raster/svg.cpp` is the only file in the tree that includes a
**plutosvg** header — the same shape as `raster/text/ttf.hpp`, whose SDL
includes live in its `.cpp` too.

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
and goldenable**. `ctbrowser/test/golden/svg.ppm` is one image compared on both platforms.
That only holds while the versions match — plutosvg 0.0.8 / plutovg 1.3.3 are
PINNED to the Windows sysroot's, `tools/Brewfile` names them, and
`lib/Raster/CMakeLists.txt` warns on a mismatch.

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
`software_backend::fonts` is where the browser hands it over. `font8x8` is still an
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
follows the size. `paint::font_face` is an alias of `layout::text_face` — the
type lives in layout because `values.hpp` depends on nothing, and layout naming
a paint type would invert the dependency the pipeline is built on.

**Opt in with `browser::use_real_fonts()`**; `run_app` does it by default
(`app_options::real_fonts`, `CTBROWSER_FONTS=font8x8` to force the bitmap font).
It loads the vendored OFL faces (Tinos/Fira Sans/Cousine → serif/sans-serif/
monospace) through the ASSET REGISTRY, so a binary that baked them in never
touches the disk, and then the page's own `@font-face` files. An unknown family
falls back to the default face, and a missing bold/italic variant to the
upright one.

**The glyph cache is the only shared mutable state in the text path** — tiles
raster in parallel and an FT_Face is not reentrant — so it is mutex-guarded and
`ctbrowser/unittests/unit/fonts_basics` drives it from twelve threads on COLD glyphs. Going
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
`ctbrowser/lib/App/app/run.cpp` — the one place in the tree where SDL and image decoding
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
called by `run_app`, so a unit test - which `ctbrowser/test/lint/api_surface` requires to be
SDL-free - reads BMP only. That is why the coverage here is an EXAMPLE with a
golden rather than a test in `tests/`.

The golden is gated on `CTBROWSER_WITH_IMAGE` in `examples/CMakeLists.txt`,
exactly as the SVG one is gated on plutosvg: without the dependency the page
lays out identically and draws nothing, and comparing then fails for the wrong
reason.
