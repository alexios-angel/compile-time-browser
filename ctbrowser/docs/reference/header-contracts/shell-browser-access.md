# Shell Browser Access contracts

<a id="contract-1"></a>

`browser(const browser &) = delete;`

Neither copyable nor movable. run_scripts() hands dom_bindings two
`this`-capturing callbacks and record() installs a third on the recorder,
so a moved-from browser leaves three lambdas pointing at the old address.
The implicit move was available and would have done exactly that.

<a id="contract-2"></a>

`enum class source_kind : std::uint8_t {`

WHICH FRONT END PARSES THE SOURCE, and it is the CALLER's to decide -
never sniffed. `<?xml ...?>` is optional in XML 1.0 and turns up in
plenty of documents served as text/html, so guessing from the bytes gets
both directions wrong. A file gets it from its extension
(`is_xml_extension`) and a fetch from its content type.

<a id="contract-3"></a>

`struct text_position {`

PAGE-LEVEL TEXT SELECTION.

A position is (node, code point WITHIN THAT NODE'S TEXT) rather than a
fragment pointer: a node's text is broken across as many fragments as it
has visual lines, and a relayout rebuilds all of them - a selection has
to survive a window resize, and pointers do not.

The GLYPH GEOMETRY is not stored on the fragment. It is derived on demand
from the same measure layout used, which costs a few measurements per
click and keeps the fragment tree exactly the shape it was.

<a id="contract-4"></a>

`void define_native(std::string name, script::native_fn fn);`

Natives the EMBEDDER supplies - `playSound` from the SDL layer is the
reason this exists. Re-installed on every navigation, because each page
gets a fresh script context and a hook registered once would silently
stop existing after the first `location.reload()`.

<a id="contract-5"></a>

`bool add_script_image(std::vector<std::byte> image);`

HAND THE PAGE ITS SCRIPTS ALREADY COMPILED.

Parsing and compiling JavaScript is about forty percent of a page load
and running it is 1.4% (docs/performance.md), so this is the largest
saving available to a packaged application: measured on the devbox,
loading babylon from an image is 77 ms against 300 ms to compile it.

ONE IMAGE PER <script>, NOT ONE PER PAGE: each script is its own program
keyed on its own bytes, so editing a two-line sketch beside a 4.5 MB
library does not invalidate the library.

AN IMAGE IS USED ONLY IF IT MATCHES, and this is where a mistake becomes
wrong code rather than a slow page: an image whose source hash is not
this script's, or which was compiled as a module, is refused rather than
run. `add_script_image` refuses at the door too - bytes that are not an
image this build would load return false immediately, so a packager hears
about it when it hands them over rather than as a cache that never hits.

A refusal at USE is silent by design and countable by
scripts_compiled_from_source(): the page still works, it just paid for
the compile. That counter is how a test proves the fast path was taken
rather than assuming it.

<a id="contract-6"></a>

`bool use_real_fonts(std::string_view directory = {});`

Turn on real fonts. Loads the vendored OFL faces through the asset
registry - so an application that baked them in never touches the disk -
and leaves font8x8 in place if SDL3_ttf is absent or none of them load.

OPT-IN rather than automatic: the goldens are font8x8's pixels, and a
page that silently changed how it renders because a font file happened to
be next to the binary would be a worse default than one that looks the
same everywhere.

DEFAULTED TO NOTHING, which means "wherever this build keeps them":
$CTBROWSER_FONT_PATH if it is set, and `fonts` beside the executable
otherwise. Those are two different places on purpose - a shipped
application carries `fonts/` next to its binary, while in the source tree
the faces are `ctbrowser/resources/fonts/` and the build sets the
variable for anything it runs. Passing a directory explicitly overrides
both and is what a caller with its own faces wants.

<a id="contract-7"></a>

`[[nodiscard]] const control_state * control_state_of(node_id id);`

A control's live state - value, caret, selection, checked. Read-only and
null for anything that is not a control: this is what a test asks where
the caret ended up, and what an embedder asks to read a form without
submitting it.

<a id="contract-8"></a>

`[[nodiscard]] double next_wakeup_ms();`

Milliseconds until this page next has something to do on its own - a
timer, an animation frame, or the caret's next blink. Infinity when it
has nothing, which is what lets an idle application BLOCK instead of
waking up sixty times a second to discover there was nothing to do.

<a id="contract-9"></a>

`[[nodiscard]] bool caret_visible() const noexcept;`

THE CARET BLINKS, in Chrome's 500 ms halves. The phase is measured from
the last caret ACTIVITY rather than from page load: a caret that blinks
out from under the character you just typed looks broken, so typing,
moving and clicking all restart it solid.

<a id="contract-10"></a>

`void set_alert_hook(std::function<void(const std::string &)> hook);`

The system dialog `alert()` raises. The engine is SDL-free and has no
window to put a dialog in, so this is a hook like the clipboard's; without
one the messages are still recorded and readable, which is what makes
alert testable headlessly.

<a id="contract-11"></a>

`struct download_record {`

WHAT A PAGE EXPORTED, and the one behaviour this engine invents rather than
copies. A browser shows a save dialog for an `<a download>`; there is
nobody here to show one to, so the bytes are written out. See
browser::save_download for why the alternative was not "do nothing".

<a id="contract-12"></a>

`void set_navigate_hook(std::function<void(const std::string &)> hook);`

Where a link that leaves this page goes. the engine does not navigate, so the
embedder decides - `ctbrowse` opens a local .html, the SDL app hands an
http(s) URL to the system browser, and a program with no hook does
nothing rather than pretending it followed the link.

<a id="contract-13"></a>

`void set_location(std::string href);`

THE DOCUMENT'S ADDRESS, set BEFORE load_html. The engine is handed bytes,
not a URL, so without this a page has none: `document.URL` is empty and
every URL-reflecting attribute (`a.href`, `img.src`, ...) hands back its
raw text instead of resolving against the document (HTML "reflecting
content attributes in IDL attributes", the USVString URL case). An
embedder that opened a file gives its `file://` URL; a fragment lands in
location_hash().

<a id="contract-14"></a>

`[[nodiscard]] std::string_view cursor_at(float x, float y);`

What the pointer should look like at a viewport point: the CSS `cursor`
of the element under it, with the UA's defaults - a link is a pointer, an
editable is a text beam. A name rather than a handle, so the engine needs
no cursor vocabulary and the app layer maps it to whatever the platform
has.
