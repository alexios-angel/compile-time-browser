# Shell Browser Geometry contracts

<a id="contract-1"></a>

`void load_author_styles();`

The author's sheets: <style> elements AND <link rel=stylesheet>, in
DOCUMENT ORDER, because the cascade's last tie-break is source order and
a <style> after a <link> has to be able to override it.

<link> resolves through the same asset_registry as <script src> and
<img src> - registry, then data:, then the filesystem - so there is still
no socket here. That is enough for a page that ships its own stylesheet
beside it, which is what every fixture and every real local page is.

<a id="contract-2"></a>

`void note_resource_load(node_id id, bool ok);`

A `<link rel=stylesheet>`, `<style>` or `<script>` this browser has
applied, or failed to find the bytes of: its `load`/`error` event is owed.
Recorded once per element - the styles walk repeats on every restyle -
and handed to the bindings by announce_resource_loads, because at page
load the walk runs BEFORE run_scripts has built them.

<a id="contract-3"></a>

`void run_scripts();`

Run every <script> in the document, in order. Errors are recorded rather
than thrown: a page whose script fails still has to render, which is what
every browser does and what makes a broken script a broken feature rather
than a blank window.

<a id="contract-4"></a>

`struct frame_layout {`

--- NESTED BROWSING CONTEXTS (browser/nested.cpp) ----------------------

A frame's document run through the same stages as the page - the cascade
with its own sheets, the box tree, layout - at the size its <iframe> box
got, so `frame.contentWindow.getComputedStyle(el).height` and `100vw`
inside the frame answer about the frame. Nothing is PAINTED: a frame is
still an empty box on screen, and this is the half the DOM reads.

<a id="contract-5"></a>

`void refresh_chrome();`

The scrollbar, as its OWN non-scrolling layer.

A layer rather than a paint into the page: it must not move when the page
does, and the compositor already knows how to hold a layer still. That is
also why it survives a scroll without re-recording anything - a scroll
moves the page layer and leaves this one where it is.
Rebuilt on every frame whose scroll moved, NOT only when the page
re-records: a scroll deliberately skips recording. Cheap enough to do
unconditionally: it is two rectangles.

<a id="contract-6"></a>

`void record_chrome();`

Everything the BROWSER draws rather than the page: the scrollbar, and an
open <select>'s option list. Each is its own non-scrolling layer for the
same reason - chrome does not move when the page does, and the compositor
already knows how to hold a layer still.

<a id="contract-7"></a>

`bool paint_svg(node_id id, const rect & box, ctbrowser::paint::display_list & into);`

A vector graphic, rasterised for THIS box rather than scaled into it.

The rect handed to draw_image is the SNAPPED size, not `box`. That is the
point of the whole size-aware path: draw_image scales nearest-neighbour,
so passing the unsnapped box would have it resample a bitmap that is
already the right size to within a fraction of a pixel - reintroducing
exactly the stair-stepping this exists to remove. Snapped, it is a 1:1
blit.

<a id="contract-8"></a>

`void paint_replaced(node_id id, const rect & box, const rect & content,`

What a <canvas> or a form control draws. Everything here is chrome the
UA supplies rather than anything the document asked for, which is why the
palette comes from :ua and not from the cascade.

`box` is the BORDER box and `content` the content box - the recorder
resolved the padding and the border to draw them, so it hands over where
they left off rather than the shell keeping a constant of its own that a
sheet could not change.

<a id="contract-9"></a>

`void check_mark(const rect & box, ctbrowser::paint::display_list & into, node_id id);`

THE TICK IN A CHECKED CHECKBOX.

Drawn as a staircase of 1px rows, the same way the select's drop-down
triangle is: `paint_op` has fill_rect, fill_ellipse, text_run, image and
the two clips, and nothing else - no line, no path, no transform - so a
stroke at 45 degrees is not expressible and a stack of short rows is what
a diagonal IS here. A glyph is not an option either: the goldens render
with font8x8, which has no U+2713.

Two rows per step so the stroke reads as a stroke at 13px rather than as
a dotted line, and the short arm rises half as far as the long one, which
is what makes it a tick rather than a V.

<a id="contract-10"></a>

`struct field_layout {`

A control's text geometry, in ONE place. The painter draws from it and a
click is mapped through it, so a caret cannot land where the text is not:
two copies of "where does line 2 start" is how a click ends up putting
the caret somewhere the glyphs never were.

<a id="contract-11"></a>

`std::size_t visible_lines = 1;`

How many lines fit in the box. A textarea is a replaced box sized by
its `rows`, so wrapping can produce more lines than it can show and
the rest are scrolled to rather than grown into. At least one, or a
box too short for a single line could never show the caret.

<a id="contract-12"></a>

`std::size_t scroll_line = 0;`

THE EFFECTIVE VIEW ORIGIN - control_state's, clamped to what the
value and the box currently are. Everything that draws or hit-tests
reads these rather than the stored ones, so a scroll left stale by a
shrinking value, a scripted `el.value =`, or a resize that rewraps to
fewer lines can never be OBSERVED: it is corrected here, once, where
the geometry is derived. The stored value is only ever a request.

<a id="contract-13"></a>

`[[nodiscard]] static std::string masked_text(std::string_view text);`

`<input type=password>` shows BULLETS. Masked per CODE POINT rather than
per byte, so a value with anything non-ASCII in it does not come out with
three bullets for one character - and the caret, which counts the same
way, still lands between them.

<a id="contract-14"></a>

`// Scroll a field the MINIMUM needed to bring the caret back into view, in`

THREE THINGS MOVE A FIELD'S VIEW, and they must never run on the same
event or they fight each other. Every bug in this area is a violation of
this split, so it is written here once:

  1. The CARET moves and the view follows - typing, editing keys, paste,
     cut, select-all, `el.value =`. That is reveal_caret, below. The
     caret drives.
  2. The USER moves the view directly - the wheel. The scroll moves and
     the caret does NOT; it is left off screen if that is where it was,
     which is what every browser does. Nothing re-reveals here: calling
     reveal_caret on a wheel would snap the view straight back and make
     the wheel useless. The next keystroke brings it back, by rule 1.
  3. AUTO-SCROLL during a drag - the scroll steps and the caret is then
     re-derived from where the pointer is. The view drives.

<a id="contract-15"></a>

`void reveal_caret(node_id id, control_state & control, control_kind kind);`

Scroll a field the MINIMUM needed to bring the caret back into view, in
BOTH axes. Called after anything that moves the caret or changes the
value - typing off the edge of a box that cannot grow is otherwise typing
into somewhere you cannot see.

Both axes for both kinds. The vertical half is a natural no-op for a
single-line field, which has exactly one line; the horizontal half is NOT
a no-op for a textarea, because an unbreakable word longer than the line
overflows sideways there too.

<a id="contract-16"></a>

`[[nodiscard]] static std::vector<std::pair<std::size_t, std::size_t>> value_lines(`

A value's VISUAL lines as [begin, end) offsets: split on newlines, then
soft-wrapped within each of those to `wrap_width`. Always at least one,
so an empty value still has a line for the caret to be on.

THE TWO BREAKS DIFFER, and everything downstream depends on how. A hard
'\n' is CONSUMED, so the next line begins at end + 1. A soft break
consumes nothing, so the next line begins exactly AT end - which is how a
consumer tells them apart without a flag, and why caret_line() below has
to exist. Trailing spaces stay on the earlier line, inside [begin, end):
browsers hang them the same way, and it is what keeps end == begin true.

A zero or negative width means no wrapping - a degenerate box must not
turn into an infinite loop of empty lines.

<a id="contract-17"></a>

`[[nodiscard]] ctbrowser::layout::text_face face_of(node_id id) const;`

The face a control's text is drawn in - the same one layout measured it
with. Measuring a caret position with a different font from the one that
drew the text is how the caret ends up a character or two past the end of
what you typed.
Every control's text MUST draw with it too: drawing a control's text
with the default face while measuring the caret with the element's own
is a caret that drifts further right with every character typed. A
textarea is monospace by UA rule and was drawn in the default serif.

<a id="contract-18"></a>

`[[nodiscard]] std::string selected_option(const read_txn & txn, node_id id);`

The LABEL a <select> shows: the text of the option whose value is the
control's. Label and value are different things - `<option value=g>green
</option>` is worth "g" to a form and shows "green" to a reader - so the
control stores the value and this maps it back for display. Reads the
DOM directly rather than caching, because a script may have just changed
the option list.

<a id="contract-19"></a>

`[[nodiscard]] text_position position_at(float x, float y);`

Where in the node's text a point falls: the nearest character boundary on
the nearest line. Above the first line is its start and below the last is
its end, so dragging past the edge takes whole lines - which is what a
drag off the top of a paragraph has to do.

<a id="contract-20"></a>

`void run_clipboard_verb(std::string_view verb);`

Copy / Cut / Paste / Select All, from the context menu or from Ctrl+key.
The page gets a CANCELABLE event first for the three that correspond to
one, which is how an editor takes them over. The wrapper reveals the
caret afterwards whatever the verb did; `clipboard_verb` is the verb.

<a id="contract-21"></a>

`[[nodiscard]] node_id labelled_control(const read_txn & txn, node_id from);`

The control a <label> labels, per HTML: its `for` attribute resolved by
id, or failing that the FIRST labelable element inside it.

`from` is where the click landed, which for label text is the TEXT NODE -
hit testing returns the deepest fragment's source, never the <label>
itself - so this walks up to the enclosing label first.

LABELABLE is exactly "is a control": control_kind_of already maps
`input type=hidden` to none, so HTML's notion falls out of what is here
rather than needing a second list to keep in step.

<a id="contract-22"></a>

`[[nodiscard]] node_id control_ancestor(node_id from);`

The control a click landed in. A click on the text inside a <button> has
to focus the button, not the text node - and a click on a <label>'s text
has to reach the control that label names, which is a SIBLING of the text
rather than an ancestor of it, so the upward walk alone cannot find it.

<a id="contract-23"></a>

`[[nodiscard]] bool via_label(node_id from);`

Whether `from` reaches its control only THROUGH a label. A label click
focuses and activates, but must not place a caret or begin a selection:
the pointer is over the label's text, nowhere near the field's glyphs,
so mapping the click through offset_at_point would put the caret at
whichever end of the value the label happened to sit on.

<a id="contract-24"></a>

`[[nodiscard]] std::vector<node_id> focusable_controls();`

Every control the user can Tab to, in DOCUMENT ORDER.

Document order IS the tab order here, because there is no `tabindex` -
which is also what a document without one gets in a real browser. Two
deliberate gaps, so nobody has to rediscover them: a positive `tabindex`
does not reorder anything, and each radio button is its own stop rather
than a group being one.

<a id="contract-25"></a>

`[[nodiscard]] bool is_focusable(const read_txn & txn, node_id id);`

A control takes focus if it IS one, is not disabled (by its own attribute
or an enclosing <fieldset>'s), and is actually RENDERED. `display: none`
leaves no fragment, and tabbing into something nobody can see is how
focus appears to vanish. Note a control scrolled off-screen still HAS a
fragment and so stays tabbable, which is correct - it is reachable, just
not visible yet.

<a id="contract-26"></a>

`// How far outside its field the pointer is, on each axis. Zero when inside.`

AUTO-SCROLL WHILE DRAG-SELECTING - rule 3 of the three at reveal_caret.

Holding the pointer outside a field you are selecting in has to keep
scrolling it, with no further mouse events: offset_at_point clamps to
what the value has, so a stationary pointer one pixel below the box picks
the same offset forever and the selection freezes one line short.

Built on the caret blink's shape - the one clock tick() already advances,
a due time, and a next_wakeup_ms contribution - because that is how this
engine does "happens on a timer" and an idle loop must still block.

THE VIEW DRIVES HERE, the caret follows. reveal_caret is the other way
round, so the drag path must never call it: one moves the scroll to
follow the caret and the other moves the caret to follow the scroll, and
together they oscillate.

<a id="contract-27"></a>

`struct autoscroll_state {`

How far outside its field the pointer is, on each axis. Zero when inside.
A step is due only when this is non-zero AND the scroll can still move
that way - otherwise the wakeup is never scheduled and an idle loop with
a pointer parked below a fully-scrolled field does not spin.

<a id="contract-28"></a>

`[[nodiscard]] bool scroll_field_under(const input_event & event);`

A wheel notch aimed at a scrollable field. True when the field took it.

Rule 2 of the three at reveal_caret: this moves the VIEW and leaves the
caret alone, off screen if that is where it was. Revealing the caret here
would snap the view straight back and make the wheel useless; the next
keystroke brings it back instead, which is what browsers do.

<a id="contract-29"></a>

`bool toggle_details(node_id target);`

Clicking a <summary> opens or closes its <details>. The state is the
`open` ATTRIBUTE, as the spec says, so a script that reads or sets it
agrees with what the user did - and layout, which builds a closed
details' children away, picks it up from the same place.

<a id="contract-30"></a>

`std::vector<std::unique_ptr<script::program>> classic_programs_;`

ONE PROGRAM PER CLASSIC <script>, kept alive for the page's lifetime, for
the same reason the modules below are: a function declared at a script's
top level holds a `const function_proto *` into its program, and a timer
or an event listener dereferences it long after run_scripts returned.

<a id="contract-31"></a>

`struct held_image {`

PRECOMPILED IMAGES, KEYED BY WHAT THEY WERE BUILT FROM. See
add_script_image. A vector rather than a map on purpose: a page has a
handful of scripts and a packager hands over a few dozen images, so a
linear scan over 64-bit keys is nothing next to a hash - and the public
header gains no include for a container it does not need.

<a id="contract-32"></a>

`bool loading_ = false;`

A LOAD IS IN FLIGHT, AND A SCRIPT ASKED FOR ANOTHER ONE. See load_html:
a navigation from inside a script is queued and performed after the
scripts stop, because doing it where it was asked for destroys the
context the asking script is running on.

<a id="contract-33"></a>

`bool load_event_pending_ = false;`

THE DOCUMENT FINISHED LOADING AND NOBODY HAS BEEN TOLD YET. Set when a
page's scripts have run, cleared by the first tick that fires
`DOMContentLoaded` and `load` at the window.

Fired from tick() rather than from load_one_page() for the same reason a
navigation is queued there: a listener runs script, and running script
inside the load is what the whole `loading_` dance exists to prevent.

testharness.js listens for `load` unconditionally and `Tests.all_done()`
waits for it: without the event every web-platform-test times out.

<a id="contract-34"></a>

`std::vector<std::unique_ptr<script::program>> module_programs_;`

ONE PROGRAM PER MODULE, kept alive for the page's lifetime. A module's
top-level declarations live in its own frame and its functions close over
them, so the program cannot be a temporary the way a classic script's
could be. See docs/plans/modules.md.

<a id="contract-35"></a>

`void load_module(const std::string & source, const std::string & specifier);`

Load a module and everything it imports, depth-first, evaluating each
once. See the definition for why post-order is the only order that works.
TWO PASSES over the graph, because a cycle cannot be done in one - see
the definitions. load_module runs both.

<a id="contract-36"></a>

`std::vector<std::unique_ptr<script::program>> extra_programs_;`

Every program run by run_script AFTER the page's own. They accumulate for
the life of the page because a closure from any of them may still be
reachable - a listener, a timer, a rAF callback - and a program that
outlives nothing is a use-after-free waiting for the first callback.
