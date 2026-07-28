# Style and layout — the cascade, formatting contexts, generated content

`include/ctbrowser/style/` (selector matching, the cascade, computed values,
the UA sheet)
and `include/ctbrowser/layout/` (`box.hpp`, `algorithm.hpp`, `engine.hpp`,
`fragment.hpp`).

## STYLE: the `style` attribute, with Chrome/Firefox precedence (2026-07-25)

Read at last — the engine saw `<style>` ELEMENTS only, so `<div style="height:2000px">`
laid out as one line. It is NOT a separate origin: author-level with a
specificity above every selector, which puts it in the cascade at

    normal selector  <  normal inline  <  important selector  <  important inline

so `engine::resolve` SPLICES the attribute's normal declarations in at the
importance boundary rather than appending them at the end. Appending is the
easy mistake and it is invisible until a page uses `!important` to override a
widget's inline style; `tests/style_basics` has a test per step, verified by
planting the mistake and watching exactly those two fail.

Parsed through the SHEET parser wrapped in `*{...}`, not ctcss's declaration
splitter — the latter peels `!important` off and discards the flag, which is
the entire question. Cached by attribute TEXT, so a table styling forty rows
identically parses once and a re-resolve after a hover parses nothing.

## TABLES AND GENERATED CONTENT (stage 7, 2026-07-25)

**`table_flow` is the third formatting context** the `LayoutAlgorithm` concept
was written for, and the one that justifies the concept: a table cannot be laid
out as blocks because a cell's width is not its own business — every cell in a
column shares that column's width, so the whole table must be MEASURED before
any of it is placed. That is exactly the `measure`/`arrange` split. Auto column
sizing (widest natural content), rows through transparent
`<thead>`/`<tbody>`/`<tfoot>`, cells stretched to the row height, and a stated
width scales the columns rather than being ignored. `<table width=400>` works:
**presentational attributes** (`width` on table/td/th/col) map to the CSS
property at the bottom of the cascade, which is how a lot of existing HTML
sizes a table.

Finding it turned up a real bug in `block_flow::measure`: a TEXT child was
handed to `inline_flow::measure`, which measures a box's CHILDREN — and a text
box has none, so **every block whose content was text measured as zero wide**.
Nothing noticed until a table asked how wide its columns wanted to be.

**Generated content** — list markers (`<ol>` numbers counted among siblings,
`<ul>` bullets) and `<summary>` disclosure triangles — is drawn by the recorder
in the gutter the UA sheet's `padding-left` already reserves. There is no
element behind it, so it is drawn rather than laid out; the ordinal and the
open/closed state are decided in the box builder, which is the only place that
knows what the siblings and the parent are.

**HTML WHITESPACE COLLAPSES** — every run of space/tab/newline is one space,
except under `white-space: pre` (which the UA sheet now gives `pre` and
`textarea`, and which INHERITS). the engine never did this: the newlines in a page's own
SOURCE went straight to the rasterizer. font8x8 drew nothing for them so nobody
noticed for six stages; a real font draws `.notdef`, which is a BOX, and a page
full of boxes is what finally showed it. It also broke wrapping — the wrap
splits on `' '` alone, so two words joined by a newline were one unbreakable
word and the line ran off the end of its box.

**`</table>` NEVER POPPED THE TABLE.** `close_element` refuses to unwind past a
special element looking for its match — and the implied `<tbody>` is special, so
the search stopped there and the table stayed open. Everything after it was then
foster-parented BEFORE it: two consecutive tables came out in reverse document
order and the `<p>` after them ended up inside the second one's `<tbody>`. The
guard now lets table structure be unwound when closing table structure.

**A table is BLOCK-level.** Left out of `is_block_level()` it shared a line with
whatever came before, so two tables sat side by side and a table sat beside the
link above it.

**Inline text of different sizes shares a BASELINE.** Every item on a line is
placed so `y + ascent` is the same — which is what sharing a baseline MEANS, and
what the rasterizer then draws. Aligning boxes is only right when every item has
the same metrics: tops made `<big>` hang above its neighbours, and bottoms are a
box's descent below the baseline, which two faces do not share. A REPLACED item
sits ON the baseline (its ascent is its whole height), so an image in a line of
text does not sink into the descenders.

That needed the font's real ascent in LAYOUT, so `measure_text_fn` is now a
`text_metrics` struct — measure, ascent, descent — bundled because they travel
together through every formatting context. It is still callable directly, so a
measurement reads as it did. `shell::metrics_for(font_backend)` is the adapter,
and it lives in the SHELL because it must name both a layout type and a raster
one, and neither may name the other: paint and raster sit downstream of layout.

**font8x8 hides this bug**: it quantises 13px, 16px and 19px to the same 8x8
cell, so all three have the same ascent and every alignment looks identical.
`tests/chrome_basics` therefore tests it with REAL fonts, and the test is
verified against BOTH wrong alignments — top and bottom each fail it.

**`white-space: pre` breaks lines.** A preserved newline is a LINE BREAK, not a
character; handing it to the rasterizer draws `.notdef`, which is why a `<pre>`
block was full of boxes.

**Table parts are `display: block`** in the UA sheet. Left inline, `normalise`
wraps them in anonymous boxes, and the table formatting context then looks for
its rows and its caption among children that are no longer them — the
`<caption>` simply vanished. `<caption>` lays out above the grid;
`<table border=N>` frames the table AND each cell, which is what the attribute
has always meant.

**The scrollbar is its own non-scrolling LAYER**, not a paint into the page —
the compositor already knows how to hold a layer still, so a scroll moves the
page layer and leaves the bar where it is without re-recording anything. Thumb
drag (with a grab offset, so it does not jump to centre itself), track clicks
that page towards the pointer, and a click on the bar never reaches the page.
A tall page LAYOUT IS RUN TWICE: content laid out at the full width would run
under the bar. That terminates because narrowing a page can only make it
taller, so a page that overflowed still overflows. `browser_options::
scrollbar_width = 0` hides it, which is what a fixed-size game wants.

**PAGE-LEVEL TEXT SELECTION.** A position is `(node, code point IN THAT NODE)`,
not a fragment pointer: a node's text is split across as many fragments as it
has visual lines, and a relayout rebuilds every one of them — a selection has to
survive a window resize, and there is a test that resizes and compares the text.

The GLYPH GEOMETRY is not stored on the fragment. It is derived on demand from
the same measure layout used, which costs a few measurements per click and left
the fragment tree exactly the shape it was — the plan called publishing per-line
glyph geometry the item most likely to spill, and this is why it did not have
to.

**A wrap DROPS the space it broke at**, so the fragments do not partition the
node's text. Summing their lengths puts every position past the first line one
character early; offsets are found by SEARCHING the node's text instead. For the
same reason `selected_text()` extracts from each node's own text over the whole
selected range rather than joining the runs — joining silently deletes one space
per line from anything copied off a wrapped paragraph.

**Browser chrome is a stack of non-scrolling LAYERS** — the scrollbar, an open
`<select>`'s option list, the context menu — rebuilt together by
`record_chrome()` and invalidated with `discard_layer()` so the page's tiles
survive. A `<select>` opens on click, closes on Escape or a click anywhere else,
and the click that closes it does NOT reach the page.

**Right button, context menu, clipboard, cursors.** `input_event` numbers
buttons the way the DOM does (right = 2); SDL numbers from 1 and calls it 3, so
passing SDL's number through made a right-click look like button 3, which
nothing was looking for. The menu is Copy/Cut/Paste/Select All, and the page
gets a CANCELABLE `contextmenu` first — `preventDefault` suppresses ours.
Ctrl+C/X/V do the same verbs. The clipboard is TWO HOOKS on the browser, filled
in by the app layer from SDL; without them copy and paste still work within the
page, which is what makes the whole path testable headlessly.
`browser::cursor_at()` returns a NAME (`pointer`, `text`, `default`) so the
engine needs no cursor vocabulary and the app maps it to a system cursor.

**`<select>` shows its option.** It drew an EMPTY RECTANGLE before — it passed
an empty string as the label and never read `<option>` at all. Now: the
`selected` option, else the first, else whatever the user picked, plus a
drop-down arrow. The popup itself is still missing.

