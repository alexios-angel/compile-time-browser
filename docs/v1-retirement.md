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

## What v2 does not have yet

Deleting v1 today removes working, tested functionality with no replacement:

- **DOM script bindings.** v2 has the ctjs VM (`ctbrowser.script`) but nothing
  binds `document`, `getElementById`, `addEventListener`, `setTimeout`,
  `requestAnimationFrame` or `fetch` to it. Stage 2 built the VM first at the
  user's direction; the binding port was never done. Every scripted page in
  `examples/` depends on this.
- **Form controls and editing.** Inputs, textareas, selects, checkboxes, radios,
  the caret, selection, clipboard, focus, submit/reset. `tests/editing.cpp`,
  `tests/forms.cpp`, `tests/select.cpp`, `tests/browserui.cpp`.
- **Canvas 2D.** The whole `getContext("2d")` surface, and with it the pong,
  invaders and p5-style examples.
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

Delete v1 **after** the script bindings and form controls land, so the deletion
removes duplication rather than capability. Until then v1 costs nothing to keep:
it is auto-detected, builds only where the toolchain supports it, and v2 builds
and tests on stock clang without it.

If the deletion is wanted now regardless, it is one commit — `git rm -r
include/ctbrowser tests examples external/compile-time-*` plus the CMake that
references them — and this document is the record of what went with it.
