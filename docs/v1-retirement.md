# Retiring v1: what is left

Stage 7 of the v2 plan says "delete v1". Two of its three parts are done — the
new shell exists, and `__builtin_std_embed` is no longer required to configure.
The deletion is **not** done, and this is the list of why.

## What v2 replaced

| v1 | v2 | state |
|---|---|---|
| `dom.hpp` node tree | `ctbrowser.dom` slab + handles | replaced, faster, thread-safe |
| `ctcss::query` per property | `ctbrowser.style` bucketed matching | replaced, ~14× faster |
| `layout.hpp` onto the DOM | `ctbrowser.layout` box + fragment trees | replaced, ~24× faster, parallel |
| paint commands, discarded per frame | `ctbrowser.paint` display lists | replaced, reusable |
| SDL rects and textures | `ctbrowser.raster` tiles + backends | replaced, GPU-capable |
| `engine.hpp` + `app.hpp` | `ctbrowser.shell` + `ctbrowser.app` | replaced |
| `ua.hpp` stylesheet | `ctbrowser.style:ua` | replaced (subset) |
| `script.hpp` DOM bindings | `ctbrowser.shell:bindings` | replaced; handles, not `node *` |
| cthtml wrapper | `ctbrowser.dom:tokenizer` + `:treebuilder` | replaced; WHATWG algorithms |
| form controls on `node` | `ctbrowser.shell:forms` | replaced; state keyed by node_id |
| canvas pixels on `node` | `ctbrowser.shell:canvas` | replaced; 2D context, same idiom |

## What v2 does not have yet

Deleting v1 today removes working, tested functionality with no replacement:

- ~~**DOM script bindings.**~~ **DONE.** `ctbrowser.shell:bindings` binds
  `document`, element methods, events, timers and `requestAnimationFrame`.
  Still missing from v1's surface: `fetch`, `alert`, `location`, and the
  canvas context.
- ~~**Form controls and editing.**~~ **MOSTLY DONE.** Text fields, checkboxes,
  radios, buttons, focus, the caret, typing, editing keys, submit and reset all
  work. Still missing: `<select>` option lists (the box draws, the popup does
  not), textarea soft-wrap and multi-line caret movement, clipboard, and
  page-level text selection.
- ~~**Canvas 2D.**~~ **MOSTLY DONE.** `getContext("2d")`, fills, strokes, paths,
  arcs, transforms, state stack, `fillText`, `measureText`. Still missing:
  `drawImage` from an `<img>` (the plumbing exists, image decoding does not),
  gradients, and nonzero-winding fill (even-odd is used, which differs only on
  self-intersecting paths).
- **Tables.** `emit_table`'s auto layout.
- **The `r3d` software 3D rasterizer and the BabylonJS shim**, plus glTF/GLB
  loading and texture decoding. ~4500 lines with three tests.
- **Widget chrome.** Scrollbar, context menu, disclosure triangles, list
  markers — v2 draws none of these; the UA palette is carried but unused.
- **Real fonts.** v2 renders text with font8x8 only. v1 has the vendored TTF
  stack, `@font-face`, per-element family/weight/style resolution.
- **Images and audio.** `image.hpp`, `audio.hpp`, the embedded asset registry.
- **Compile-time everything.** `std::embed`, `std::fetch`, the `page<>` NTTP.
  Retiring this was the plan's explicit intent, so it is a deliberate loss —
  but `assets.hpp`'s automatic asset embedding has no v2 equivalent at all.

That is **22 v1 tests and 11 examples** whose subject matter v2 cannot yet
execute.

## The other cost: differential testing goes away

Since stage 3, every v2 subsystem has been checked against v1 on the same
input — style resolution, layout geometry, element counts. That caught real
bugs, including a benchmark measuring freed memory and a layout pass that
ignored its own box. Deleting v1 ends it. The replacement is the golden image
(`tests-v2/golden/page.ppm`) plus `ctbrowse --headless`, which is weaker: it
catches changes, not disagreements with a second implementation.

## Recommendation

**Updated after form controls and canvas landed.** All three original blockers
are gone: script drives the page, HTML parsing is v2's own, and forms and canvas
work. What remains is smaller and more specific:

- **image decoding** (`<img>`, and `drawImage` from one) - v1 has a BMP reader
  and an optional SDL3_image path
- **real fonts** - v2 renders text with font8x8 only
- **audio**, and the **BabylonJS shim** with its software 3D rasterizer
- **`<select>` popups**, page-level text selection, and the clipboard
- **tables** as anything but ordinary boxes (no column sizing)

None of those blocks the architecture. Deleting v1 now would still lose them, so
the question is whether they are wanted back - which is a product decision, not
a technical one.

Note also that v2 no longer depends on `external/compile-time-html` at all.
`external/compile-time-css` is still used by the style engine, and
`compile-time-javascript` by the script compiler's parser.

Until form controls land, v1 costs nothing to keep: it is auto-detected, builds
only where the toolchain supports it, and v2 builds and tests on stock clang
without it.

If the deletion is wanted now regardless, it is one commit — `git rm -r
include/ctbrowser tests examples external/compile-time-*` plus the CMake that
references them — and this document is the record of what went with it.
