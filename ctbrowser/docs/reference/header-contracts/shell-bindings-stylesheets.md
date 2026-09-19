# Shell Bindings Stylesheets contracts

<a id="contract-1"></a>

`void install_style_sheets(context & cx);`

`document.styleSheets`, `document.adoptedStyleSheets` and the interface
objects. Called from `install`, AFTER install_document - it hangs the
accessor off the document object.

<a id="contract-2"></a>

`void install_sheet_property(context & cx, script::object_object & obj, node_id id);`

`styleElement.sheet` / `linkElement.sheet` - the LinkStyle interface.
Called from install_element_views, which is the only place an element
wrapper is built. `HTMLLinkElement.disabled` and a ShadowRoot's
`styleSheets` / `adoptedStyleSheets` ride on the same prototypes and are
installed here too.

<a id="contract-3"></a>

`[[nodiscard]] std::string_view preferred_sheet_title();`

HTML 4.2.6, the preferred style sheet set - STICKY, as every engine has
it: the first titled sheet to ARRIVE names the set, and a titled sheet
inserted before it later does not take over
(preferred-stylesheet-reversed-order.html). Arrival order is node
creation order, which is what the smallest owner handle among the
titled sheets picks out; the sheets are re-derived to find it when
nothing has named the set yet.
ponytail: never reset, so a page that removes its preferred sheet keeps
the name; clear it on removal if a page ever needs that.

<a id="contract-4"></a>

`[[nodiscard]] static std::string resolve_sheet_href(std::string_view base,`

An `@import`'s URL against the sheet it sits in. A `<style>`'s sheet has
no href, so its imports resolve as the document's own paths do; a
`<link href="a/b.css">` importing `c.css` names `a/c.css`.

<a id="contract-5"></a>

`[[nodiscard]] std::string author_style_text();`

Every enabled document sheet, serialised, in document order - exactly
what browser::load_author_styles would have concatenated, plus whatever
the CSSOM has since done to it.

<a id="contract-6"></a>

`void sync_style_sheets(context & cx);`

The document's sheets, re-derived from the DOM. Cheap and idempotent: an
owner node that already has a record keeps it, which is what makes
`document.styleSheets[0] === styleElement.sheet` and what keeps a page's
expando on a sheet object alive across a read.

<a id="contract-7"></a>

`void sync_sheet_list(context & cx, node_id from, script::object_object & list,`

One tree's sheets - the document's, or a shadow root's - into one list.
`order` receives the document-order indices the cascade reads, and is
null for a shadow tree, which the cascade does not render.

<a id="contract-8"></a>

`[[nodiscard]] value sheet_object_for(context & cx, std::size_t sheet);`

THE object for a record - made once, held on the internals object, so
`rule.styleSheet`, `sheet.parentStyleSheet`, `document.styleSheets[i]`
and `el.sheet` all answer the same object for the same record.

<a id="contract-9"></a>

`[[nodiscard]] std::size_t load_imported_sheet(std::size_t rule, std::string_view href);`

An `@import`'s sheet: a record of its own, fetched through the same
registry a `<link>` is, parsed - imports and all - and hung off `rule`.

<a id="contract-10"></a>

`[[nodiscard]] std::size_t parse_one_rule(std::size_t sheet, std::string_view text,`

One rule, for insertRule. Returns the new rule's index, or npos with
`error` naming the DOMException the caller must throw.

<a id="contract-11"></a>

`[[nodiscard]] std::vector<std::string> * receiver_media(context & cx);`

The media query list `this` is a view of - a sheet's or a media rule's.
One function for both because a MediaList carries whichever private slot
names its owner and CSSOM gives the two the same interface.

<a id="contract-12"></a>

`[[nodiscard]] value media_list_object(context & cx, script::object_object & owner);`

A MediaList over one of those lists, cached on `owner` under a private
slot so that `sheet.media === sheet.media` - which is [SameObject].

<a id="contract-13"></a>

`std::vector<std::unique_ptr<css_sheet_record>> css_sheets_;`

unique_ptr rather than a bare vector because a record is addressed by
INDEX from script and by REFERENCE from C++, and inserting a rule while a
reference to another one is live would otherwise dangle.

<a id="contract-14"></a>

`std::vector<std::size_t> css_document_sheets_;`

...and the same sheets in DOCUMENT ORDER, which is what the cascade needs
and what `css_sheets_` is not: a record whose `<style>` has since been
removed stays in the store (a rule object the page still holds must keep
answering) and must not appear in the author CSS.

<a id="contract-15"></a>

`std::vector<std::uint64_t> enabled_links_;`

The `<link>`s whose `disabled` attribute a script removed - HTML's
"explicitly enabled" flag, which is what lets an alternate sheet apply.

<a id="contract-16"></a>

`value cssom_internals_;`

The StyleSheetList, the adopted array and the interface prototypes. Held
on the DOCUMENT under a non-configurable private key as well as here, so
the collector reaches them through `mark(document_)` and this member
needs no line in register_roots.

<a id="contract-17"></a>

`[[nodiscard]] node_id find_by_id(const std::string & want);`

getElementById's walk, which the browser's fragment scroll and focus
navigation share rather than keeping a second one.

<a id="contract-18"></a>

`void install_document_as_node(context & cx, script::object_object & doc);`

--- THE DOCUMENT AS A NODE (bindings/document/as_node.cpp) -----------

There is NO Document node in this tree: `txn.root()` is the `<html>`
element and `document` is a plain script object carrying no handle at
all. Every Node and ParentNode member below therefore answers as if
there were a Document whose one child is `documentElement`. The whole of
that decision, and what it makes impossible, is written down above
`install_document_as_node` in bindings/document/as_node.cpp - read it before
adding to any of these.

<a id="contract-19"></a>

`void install_traversal(context & cx, script::object_object & doc);`

DOM 6: `createTreeWalker`, `createNodeIterator` and the `NodeFilter`
constants - bindings/document/traversal.cpp.

<a id="contract-20"></a>

`[[nodiscard]] bool is_the_document(value v) const;`

Is this value the `document` object itself? By IDENTITY, because shape
cannot tell: the Document is the one node-like object with no handle
property, so `handle_of` reports the same empty handle for it as for a
number or a plain object.

<a id="contract-21"></a>

`[[nodiscard]] std::string locate_namespace(node_id element, const std::string * prefix);`

DOM 4.4, "locate a namespace", run at an ELEMENT. `prefix` is the null
prefix when the pointer is null, which is what `lookupNamespaceURI(null)`
and `isDefaultNamespace` both ask for. An empty answer IS the null
namespace and reports as `null`.

<a id="contract-22"></a>

`void normalize_subtree(node_id root);`

`normalize()`: merge adjacent Text children and drop empty ones, over a
whole subtree. Reads the shape out first and mutates afterwards - a
structural write inside a live read_txn is a shape nothing else in these
bindings has.

<a id="contract-23"></a>

`[[nodiscard]] std::shared_ptr<const paint::bitmap> image_argument(value v);`

The 2D context. Its methods close over the canvas node, so the object can
be stored and reused - which is what every canvas page does.
What a page can pass to drawImage: a loadImage() handle (a number) or an
<img> element wrapper. Anything else is nothing to draw.

<a id="contract-24"></a>

`[[nodiscard]] value matrix_object(context & cx, const transform & t);`

A DOMMatrix over a transform: the six numbers plus the methods a page
composes them with. See the getTransform binding for why six bare numbers
was not enough.

<a id="contract-25"></a>

`[[nodiscard]] value webgl_context_object(context & cx, node_id id, int version);`

`canvas.getContext('webgl')`. The JavaScript surface is in its own file -
seventy-nine methods and a constant table would bury the DOM in this one -
and the state machine it drives is in shell/page/webgl.hpp.

<a id="contract-26"></a>

`void install_webgl_constants(script::object_object * obj, bool webgl2);`

Its three halves, called in this order: the constant table, then every
method. See lib/Shell/bindings/webgl/.

<a id="contract-27"></a>

`void resize_webgl_context(node_id id, int width, int height);`

SETTING canvas.width RESIZES THE DRAWING BUFFER, and for a WebGL canvas
that is not cosmetic: canvas_context::resize REALLOCATES the bitmap, so a
context still holding the old pointer is drawing into freed memory.

<a id="contract-28"></a>

`void present_webgl_contexts();`

Copy every live WebGL context's surface into its canvas bitmap. Called
once at the end of a frame, never per draw.

<a id="contract-29"></a>

`flat_map<std::uint64_t, std::unique_ptr<webgl_context>> webgl_contexts_;`

ONE CONTEXT PER CANVAS, kept for the document's life. getContext is
idempotent in the spec: a page that calls it twice gets the same object
with the same buffers and programs still bound, and a fresh one each time
would quietly lose everything it had uploaded.

<a id="contract-30"></a>

`flat_map<std::uint64_t, script::object_object *> webgl_objects_;`

The JS wrapper for each, so getContext hands back the SAME object - and a
GC root, because the page may drop its reference and ask again.

<a id="contract-31"></a>

`static bool apply_canvas_font(canvas_context & canvas, std::string_view font);`

`ctx.font = "..."`, read as the CSS `font` shorthand it is: the size in
px, the first family, bold and italic. False for a string that is not
one, which the specification says leaves the font as it was.

<a id="contract-32"></a>

`[[nodiscard]] long long size_attribute(const read_txn & txn, node_id id, std::string_view name,`

A `width`/`height` content attribute as HTML 2.6.9 reflects an unsigned
long: the rules for parsing non-negative integers, and `fallback` when it
is absent, not a number, negative or past 2^31-1. ZERO IS A VALUE - a
`<canvas width=0>` is a canvas nothing can be drawn on, not a 300-wide
one - so the 2D context, the WebGL context, toBlob and `canvas.width`
all read the same number. Defined in element/views.cpp.

<a id="contract-33"></a>

`[[nodiscard]] script::object_object * install_performance(context & cx);`

`performance`: `now`, the entry list and `getEntries*` over it, plus the
`PerformanceEntry` and `PerformancePaintTiming` globals. Returns the
object so install_window can hang it on the window and the global scope.
Its own file: lib/Shell/bindings/performance.cpp.
