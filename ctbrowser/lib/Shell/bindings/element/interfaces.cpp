// dom_bindings - the interface objects: the table of DOM interfaces and their
// prototype chain, and the prototypes a page can name.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

// ONE INTERFACE. `parent` is the interface whose prototype this one's chains
// to - empty means Object.prototype, which is where EventTarget and the
// collections stop - and `tags` is every HTML tag name that IS one, so the
// tag-to-prototype map and the inheritance chain are the same table rather than
// two that can disagree.
struct dom_interface {
    std::string_view name;
    std::string_view parent;
    std::string_view tags;
};

// THE CHAIN, spelled out:
//   HTMLDivElement -> HTMLElement -> Element -> Node -> EventTarget -> Object
//
// EventTarget comes first so that everything below it can name it, and the
// interfaces another translation unit already owns - EventTarget, HTMLElement -
// are ADOPTED rather than rebuilt: see install_dom_interfaces.
constexpr dom_interface interface_table[] = {
    {"EventTarget", "", ""},
    {"Node", "EventTarget", ""},
    {"Element", "Node", ""},
    {"CharacterData", "Node", ""},
    {"Text", "CharacterData", ""},
    {"CDATASection", "Text", ""},
    {"Comment", "CharacterData", ""},
    {"ProcessingInstruction", "CharacterData", ""},
    {"DocumentType", "Node", ""},
    {"Document", "Node", ""},
    {"XMLDocument", "Document", ""},
    {"DocumentFragment", "Node", ""},
    // A ShadowRoot IS a DocumentFragment - `instanceof` has to answer true for
    // BOTH, which is what putting it in the chain rather than beside it buys.
    // No tags: which fragment is a shadow root is a question about
    // `shadow_hosts_`, not about a name. See prototype_for_node.
    {"ShadowRoot", "DocumentFragment", ""},
    {"Attr", "Node", ""},
    // NOT A NODE AND NOT IN ANY CHAIN: `DOMStringMap` exists so that
    // `el.dataset instanceof DOMStringMap` can be true and so that the name is
    // a global a page can feature-detect. `dataset.html` asks it of an HTML, an
    // SVG and a MathML element.
    {"DOMStringMap", "", ""},
    {"Window", "EventTarget", ""},
    // The collections. They are not nodes and inherit from nothing, and they
    // are here because `document.links instanceof HTMLCollection` and
    // `assert_true(x instanceof NodeList)` are what the corpus asks - see
    // interface_prototype(), which is how another file reaches them.
    {"NodeList", "", ""},
    {"HTMLCollection", "", ""},
    // The forms' three, HTML 4.10.20.1 and 2.6.2: `form.elements`,
    // `select.options` and the list `elements.namedItem` answers for a
    // radio group. Made by make_live_collection; the extra members are
    // installed by install_control_methods.
    {"HTMLFormControlsCollection", "HTMLCollection", ""},
    {"HTMLOptionsCollection", "HTMLCollection", ""},
    {"RadioNodeList", "NodeList", ""},
    // `control.validity`, HTML 4.10.20.3 - an object of booleans, not a node.
    {"ValidityState", "", ""},
    {"DOMTokenList", "", ""},
    {"NamedNodeMap", "", ""},
    {"DOMImplementation", "", ""},
    {"DOMStringMap", "", ""},
    // DOM 6's two walkers - not nodes, not constructible; made by
    // bindings/document/traversal.cpp.
    {"TreeWalker", "", ""},
    {"NodeIterator", "", ""},
    // DOM 5's ranges. `Range` and `StaticRange` are globals install_range
    // made - constructible, adopted here - and this is what chains both to
    // AbstractRange, which is made here and constructs nothing.
    {"AbstractRange", "", ""},
    {"Range", "AbstractRange", ""},
    {"StaticRange", "AbstractRange", ""},

    // EVERY TAG THAT IS A PLAIN HTMLElement, listed rather than left to the
    // fallback, so that anything NOT here can be told apart from them: HTML
    // says an unrecognised tag is an HTMLUnknownElement, and `historical.html`
    // asserts exactly that about <blink>, <isindex> and six others.
    {"HTMLElement", "Element",
     "abbr acronym address article aside b bdi bdo big center cite code dd dfn dt em figcaption "
     "figure footer header hgroup i kbd main mark nav nobr noembed noframes noscript plaintext rb "
     "rp rt rtc ruby s samp search section small strike strong sub summary sup tt u var wbr"},
    // NOT an HTMLElement, and the distinction is load-bearing:
    // `Body-FrameSet-Event-Handlers.html` asserts an element in a foreign
    // namespace is `instanceof Element` and NOT `instanceof HTMLElement`.
    {"SVGElement", "Element", ""},
    {"HTMLUnknownElement", "HTMLElement", ""},

    {"HTMLAnchorElement", "HTMLElement", "a"},
    {"HTMLAreaElement", "HTMLElement", "area"},
    {"HTMLBRElement", "HTMLElement", "br"},
    {"HTMLBaseElement", "HTMLElement", "base"},
    {"HTMLBodyElement", "HTMLElement", "body"},
    {"HTMLButtonElement", "HTMLElement", "button"},
    {"HTMLCanvasElement", "HTMLElement", "canvas"},
    {"HTMLDListElement", "HTMLElement", "dl"},
    {"HTMLDataElement", "HTMLElement", "data"},
    {"HTMLDataListElement", "HTMLElement", "datalist"},
    {"HTMLDetailsElement", "HTMLElement", "details"},
    {"HTMLDialogElement", "HTMLElement", "dialog"},
    {"HTMLDirectoryElement", "HTMLElement", "dir"},
    {"HTMLDivElement", "HTMLElement", "div"},
    {"HTMLEmbedElement", "HTMLElement", "embed"},
    {"HTMLFieldSetElement", "HTMLElement", "fieldset"},
    {"HTMLFontElement", "HTMLElement", "font"},
    {"HTMLFormElement", "HTMLElement", "form"},
    {"HTMLFrameElement", "HTMLElement", "frame"},
    {"HTMLFrameSetElement", "HTMLElement", "frameset"},
    {"HTMLHRElement", "HTMLElement", "hr"},
    {"HTMLHeadElement", "HTMLElement", "head"},
    {"HTMLHeadingElement", "HTMLElement", "h1 h2 h3 h4 h5 h6"},
    {"HTMLHtmlElement", "HTMLElement", "html"},
    {"HTMLIFrameElement", "HTMLElement", "iframe"},
    {"HTMLImageElement", "HTMLElement", "img"},
    {"HTMLInputElement", "HTMLElement", "input"},
    {"HTMLLIElement", "HTMLElement", "li"},
    {"HTMLLabelElement", "HTMLElement", "label"},
    {"HTMLLegendElement", "HTMLElement", "legend"},
    {"HTMLLinkElement", "HTMLElement", "link"},
    {"HTMLMapElement", "HTMLElement", "map"},
    {"HTMLMarqueeElement", "HTMLElement", "marquee"},
    {"HTMLMediaElement", "HTMLElement", ""},
    {"HTMLAudioElement", "HTMLMediaElement", "audio"},
    {"HTMLVideoElement", "HTMLMediaElement", "video"},
    {"HTMLMenuElement", "HTMLElement", "menu"},
    {"HTMLMetaElement", "HTMLElement", "meta"},
    {"HTMLMeterElement", "HTMLElement", "meter"},
    {"HTMLModElement", "HTMLElement", "ins del"},
    {"HTMLOListElement", "HTMLElement", "ol"},
    {"HTMLObjectElement", "HTMLElement", "object"},
    {"HTMLOptGroupElement", "HTMLElement", "optgroup"},
    {"HTMLOptionElement", "HTMLElement", "option"},
    {"HTMLOutputElement", "HTMLElement", "output"},
    {"HTMLParagraphElement", "HTMLElement", "p"},
    {"HTMLParamElement", "HTMLElement", "param"},
    {"HTMLPictureElement", "HTMLElement", "picture"},
    {"HTMLPreElement", "HTMLElement", "pre listing xmp"},
    {"HTMLProgressElement", "HTMLElement", "progress"},
    {"HTMLQuoteElement", "HTMLElement", "blockquote q"},
    {"HTMLScriptElement", "HTMLElement", "script"},
    {"HTMLSelectElement", "HTMLElement", "select"},
    {"HTMLSlotElement", "HTMLElement", "slot"},
    {"HTMLSourceElement", "HTMLElement", "source"},
    {"HTMLSpanElement", "HTMLElement", "span"},
    {"HTMLStyleElement", "HTMLElement", "style"},
    {"HTMLTableCaptionElement", "HTMLElement", "caption"},
    {"HTMLTableCellElement", "HTMLElement", "td th"},
    {"HTMLTableColElement", "HTMLElement", "col colgroup"},
    {"HTMLTableElement", "HTMLElement", "table"},
    {"HTMLTableRowElement", "HTMLElement", "tr"},
    {"HTMLTableSectionElement", "HTMLElement", "tbody tfoot thead"},
    {"HTMLTemplateElement", "HTMLElement", "template"},
    {"HTMLTextAreaElement", "HTMLElement", "textarea"},
    {"HTMLTimeElement", "HTMLElement", "time"},
    {"HTMLTitleElement", "HTMLElement", "title"},
    {"HTMLTrackElement", "HTMLElement", "track"},
    {"HTMLUListElement", "HTMLElement", "ul"},
};

[[nodiscard]] constexpr std::size_t interface_index(std::string_view name) {
    for (std::size_t i = 0; i < std::size(interface_table); ++i) {
        if (interface_table[i].name == name) { return i; }
    }
    return std::size(interface_table);
}

// The interface an HTML tag is. A tag the table does not name at all is an
// HTMLUnknownElement - UNLESS its name contains a hyphen, which makes it a valid
// custom element name and therefore an ordinary HTMLElement. Both halves are
// tested: `historical.html` asserts <blink> and seven other dead tags ARE
// HTMLUnknownElement, and a page's own <my-widget> is not.
//
// Either way it inherits from HTMLElement, so nothing about the global
// attributes depends on getting this right - only the name does.
[[nodiscard]] constexpr std::size_t interface_for_tag(std::string_view tag) {
    for (std::size_t i = 0; i < std::size(interface_table); ++i) {
        if (lists_token(interface_table[i].tags, tag)) { return i; }
    }
    return interface_index(tag.find('-') == std::string_view::npos ? "HTMLUnknownElement"
                                                                   : "HTMLElement");
}

// THE RENDERED TEXT FRAGMENT, which is what the `innerText` and `outerText`
// SETTERS both build. The assigned string is cut at every U+000A, U+000D or
// CRLF pair: each run of other code points is a Text node and each break is a
// `br` element, so `el.innerText = "a\nb"` leaves three children where
// `textContent` would have left one.
//
// NOTHING IS PARSED AND NOTHING IS ESCAPED, which is the reason this builds
// nodes rather than markup for set_inner_html to re-read: `abc<def` is seven
// characters of TEXT, and a U+0000 in the middle survives - the HTML tokenizer
// would have made an element of the first and U+FFFD of the second, and
// innertext-setter-tests.js asserts both by name.
template <typename OnText, typename OnBreak>
void each_rendered_text_part(std::string_view text, OnText && on_text, OnBreak && on_break) {
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t start = at;
        while (at < text.size() && text[at] != '\n' && text[at] != '\r') { ++at; }
        if (at > start) { on_text(text.substr(start, at - start)); }
        while (at < text.size() && (text[at] == '\n' || text[at] == '\r')) {
            // ONE break for CRLF and two for CR CR: the pair is a single line
            // ending, which is the only place the two characters are not
            // independent.
            if (text[at] == '\r' && at + 1 < text.size() && text[at + 1] == '\n') { ++at; }
            ++at;
            on_break();
        }
    }
}

// --- innerText, THE GETTER HALF: the "inner text collection steps", HTML 3.2.7
//
// The specification walks the element's descendants and reads FOUR computed
// properties - `display`, `white-space`, `visibility` and `text-transform` -
// to decide what a text node contributes and where a line break goes. This
// engine lays out on a FRAME rather than on demand, so the box tree a getter
// could ask describes the page before the script's own mutations, and
// `container.innerHTML = x; e.innerText` - the shape of every getter test -
// would answer about the page as it was. What is read instead is the
// element's own `style` attribute over the UA sheet's per-tag defaults, which
// is what a browser computes for those four properties on nearly every
// element of nearly every page - AND, when the getter could flush the cascade
// first (a connected element of the page's own document), the cascade's
// answer for the element, which is where `.table { display: table }` lives.
// ponytail: text-transform is ASCII (the engine's rule, see
// core/algorithms.hpp), and ::first-line/::first-letter are not consulted.

struct text_style {
    bool preserve = false;        // white-space: pre / pre-wrap / break-spaces
    bool preserve_breaks = false; // white-space: pre-line
    bool hidden = false;          // visibility: hidden / collapse
    int transform = 0;            // +1 uppercase, -1 lowercase
};

// One property of a `style="..."` attribute, lowercased, or "" when it is not
// declared. The split declarations.cpp seeds `el.style` with, and the same
// ceiling: a `;` inside a string ends a declaration, which none of the four
// keyword properties read here can contain.
[[nodiscard]] std::string inline_declaration(std::string_view style, std::string_view property) {
    std::size_t at = 0;
    while (at < style.size()) {
        std::size_t end = style.find(';', at);
        if (end == std::string_view::npos) { end = style.size(); }
        const std::string_view declared = style.substr(at, end - at);
        const std::size_t colon = declared.find(':');
        if (colon != std::string_view::npos &&
            ascii_iequals(trim(declared.substr(0, colon), html_whitespace), property)) {
            return ascii_lower_copy(trim(declared.substr(colon + 1), html_whitespace));
        }
        at = end + 1;
    }
    return {};
}

// The UA sheet's `display` for an HTML tag. Every foreign element is inline.
[[nodiscard]] std::string_view default_display(std::string_view tag) {
    if (lists_token("script style template noscript head title meta link base area param source "
                    "track datalist rp col colgroup",
                    tag)) {
        return "none";
    }
    if (lists_token("html body div p h1 h2 h3 h4 h5 h6 ul ol li dl dt dd pre listing xmp plaintext "
                    "blockquote address article aside footer header hr main nav section figure "
                    "figcaption fieldset legend form details summary dialog center dir menu "
                    "optgroup option hgroup search frameset frame",
                    tag)) {
        return "block";
    }
    if (tag == "table") { return "table"; }
    if (tag == "tr") { return "table-row"; }
    if (tag == "td" || tag == "th") { return "table-cell"; }
    if (tag == "caption") { return "table-caption"; }
    if (lists_token("tbody thead tfoot", tag)) { return "table-row-group"; }
    if (lists_token("input button select textarea img video audio canvas iframe object embed "
                    "meter progress",
                    tag)) {
        return "inline-block";
    }
    return "inline";
}

// The elements whose CONTENTS are never text: a replaced element, and the
// widgets whose children are not what they render.
constexpr std::string_view replaced_tags =
    "img input textarea iframe audio video canvas object embed meter progress";

// The SVG elements that never render their contents: the `<defs>` and
// gradient family, and the metadata the DOM keeps but no box shows.
constexpr std::string_view unrendered_svg_tags =
    "defs stop symbol clipPath mask marker pattern linearGradient radialGradient metadata title "
    "desc script style";

class inner_text_collector {
public:
    inner_text_collector(const read_txn & txn, atom_table & atoms, const style::style_map * styles)
        : txn_(txn), atoms_(atoms), styles_(styles), style_(atoms.intern("style")),
          type_(atoms.intern("type")), hidden_(atoms.intern("hidden")),
          open_(atoms.intern("open")) {}

    [[nodiscard]] std::string_view tag_of(node_id node) const {
        return atoms_.text(txn_.tag(node).value_or(atom{}));
    }
    [[nodiscard]] bool is_html(node_id node) const {
        return txn_.element_ns(node) == node_ns::html;
    }
    [[nodiscard]] std::string_view own_style(node_id node) const {
        return txn_.attribute_value(node, style_);
    }
    // One of the four properties as the cascade left it on this element -
    // own declaration first, inherited otherwise, exactly what `get` means -
    // or the inline declaration when there is no cascade to ask.
    [[nodiscard]] std::string declared(node_id node, std::string_view property) const {
        if (styles_ != nullptr) {
            const auto found = styles_->find(style::engine::key_of(node));
            if (found != styles_->end() && found->second) {
                const std::string_view text = found->second->get(atoms_.intern(property));
                if (!text.empty()) { return ascii_lower_copy(trim(text, html_whitespace)); }
            }
        }
        return inline_declaration(own_style(node), property);
    }

    // The element's computed `display` keyword, as far as an inline style over
    // the UA sheet can say. A float or an out-of-flow position makes any
    // display block-level, which is the one place `display` is not the whole
    // answer and the corpus tests it on a <span>.
    [[nodiscard]] std::string display_of(node_id node) const {
        const std::string_view tag = tag_of(node);
        const bool html = is_html(node);
        // `noscript` is `display: none !important` while scripting is on, and
        // a <template>'s children are its contents, which live elsewhere.
        if (html && (tag == "noscript" || tag == "template")) { return "none"; }
        if (txn_.element_ns(node) == node_ns::svg && lists_token(unrendered_svg_tags, tag)) {
            return "none";
        }
        // `[hidden]` and `<input type=hidden>` are `display: none` in the UA
        // sheet - this engine's has no such rule, so they are asked here, and
        // an inline `display` still wins over them as the cascade would have it.
        if (html &&
            (txn_.has_attribute(node, hidden_) ||
             (tag == "input" && ascii_iequals(txn_.attribute_value(node, type_), "hidden"))) &&
            inline_declaration(own_style(node), "display").empty()) {
            return "none";
        }
        std::string display = declared(node, "display");
        // THE UA SHEET SAYS `tr, td { display: block }` because that is how
        // this engine lays a table out; the collection wants the CSS table
        // display the tag has, so the sheet's answer yields to it unless the
        // element's own style said block.
        if (html && display == "block" && default_display(tag).starts_with("table") &&
            inline_declaration(own_style(node), "display").empty()) {
            display.clear();
        }
        if (display.empty()) {
            display = html ? std::string{default_display(tag)} : std::string{"inline"};
        }
        if (display == "none") { return display; }
        const std::string floated = declared(node, "float");
        const std::string position = declared(node, "position");
        if (floated == "left" || floated == "right" || position == "absolute" ||
            position == "fixed") {
            return "block";
        }
        return display;
    }

    // Is this element (or any ancestor) `display: none`? The specification's
    // "not being rendered", which sends the getter to textContent instead.
    [[nodiscard]] bool unrendered(node_id node) const {
        for (node_id at = node; at && txn_.kind(at).value_or(node_kind::text) == node_kind::element;
             at = txn_.parent(at)) {
            if (display_of(at) == "none") { return true; }
        }
        return false;
    }

    // The three inherited properties as this element leaves them for its
    // children: its own inline declarations over what it inherited.
    [[nodiscard]] text_style style_under(node_id node, text_style inherited) const {
        const std::string white_space = declared(node, "white-space");
        if (!white_space.empty()) {
            inherited.preserve =
                white_space == "pre" || white_space == "pre-wrap" || white_space == "break-spaces";
            inherited.preserve_breaks = white_space == "pre-line";
        } else if (is_html(node) &&
                   lists_token("pre listing xmp plaintext textarea", tag_of(node))) {
            inherited.preserve = true;
            inherited.preserve_breaks = false;
        }
        const std::string visibility = declared(node, "visibility");
        if (visibility == "hidden" || visibility == "collapse") {
            inherited.hidden = true;
        } else if (visibility == "visible") {
            inherited.hidden = false;
        }
        const std::string transform = declared(node, "text-transform");
        if (transform == "uppercase") {
            inherited.transform = 1;
        } else if (transform == "lowercase") {
            inherited.transform = -1;
        } else if (transform == "none") {
            inherited.transform = 0;
        }
        return inherited;
    }

    // What the target inherits: its ancestors' styles applied root-first, then
    // its own - the target's `white-space: pre` governs its text too.
    [[nodiscard]] text_style inherited_style(node_id target) const {
        std::vector<node_id> chain;
        for (node_id at = target;
             at && txn_.kind(at).value_or(node_kind::text) == node_kind::element;
             at = txn_.parent(at)) {
            chain.push_back(at);
        }
        text_style style;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            style = style_under(*it, style);
        }
        return style;
    }

    // The children of `node`, under `style`. Where the walk starts for the
    // target, whose own display contributes nothing - "No tab on table-cell
    // itself", "No newline on table-row itself".
    void collect_children(node_id node, const text_style & style,
                          std::string_view display = "block") {
        const bool html = is_html(node);
        const std::string_view tag = tag_of(node);
        if (html && lists_token(replaced_tags, tag)) { return; }
        const bool closed_details = html && tag == "details" && !txn_.has_attribute(node, open_);
        // A <select> renders its options and an <optgroup> its options: text
        // beside them, and a group inside a group, has no box.
        const bool grouped = html && tag == "optgroup" && is_html(txn_.parent(node)) &&
                             tag_of(txn_.parent(node)) == "select";
        const std::string_view widget_children =
            html && tag == "select" ? "option optgroup" : (grouped ? "option" : "");
        // "Blockification": the in-flow children of a flex or grid container
        // are block-level whatever their display says.
        const bool blockify = lists_token("flex inline-flex grid inline-grid", display);
        for (const node_id child : txn_.children(node)) {
            if (closed_details && !(is_html(child) && tag_of(child) == "summary")) { continue; }
            if (!widget_children.empty() &&
                !(is_html(child) && lists_token(widget_children, tag_of(child)))) {
                continue;
            }
            collect(child, style, blockify);
        }
    }

    // The items, joined: this is the CSS white-space processing the
    // specification defers to, done on the flat list - a collapsible run is
    // one space, dropped at the start of a line and before a break, and a run
    // of required line breaks is the longest of them, never at either end.
    [[nodiscard]] std::string finish() const {
        std::string out;
        bool pending_space = false;
        bool line_start = true;
        int pending_breaks = 0;
        const auto flush_breaks = [&] {
            if (pending_breaks > 0 && !out.empty()) {
                out.append(static_cast<std::size_t>(pending_breaks), '\n');
            }
            pending_breaks = 0;
        };
        for (const text_item & item : items_) {
            switch (item.kind) {
            case text_item::collapsible:
                for (const char c : item.text) {
                    if (html_whitespace.find(c) != std::string_view::npos) {
                        if (!line_start) { pending_space = true; }
                        continue;
                    }
                    flush_breaks();
                    if (pending_space) { out += ' '; }
                    pending_space = false;
                    line_start = false;
                    out += c;
                }
                break;
            case text_item::preserved:
                if (item.text.empty()) { break; }
                flush_breaks();
                if (pending_space) { out += ' '; }
                pending_space = false;
                out += item.text;
                line_start = item.text.back() == '\n';
                break;
            case text_item::separator:
                pending_space = false;
                flush_breaks();
                out += item.text;
                line_start = true;
                break;
            case text_item::open:
                // An atomic inline is a box of its own: a space before it is
                // real, and its own leading and trailing spaces are not.
                flush_breaks();
                if (pending_space) { out += ' '; }
                pending_space = false;
                line_start = true;
                break;
            case text_item::close:
                pending_space = false;
                line_start = false;
                break;
            case text_item::required:
                pending_space = false;
                line_start = true;
                pending_breaks = std::max(pending_breaks, item.breaks);
                break;
            }
        }
        return out;
    }

private:
    struct text_item {
        enum {
            collapsible,
            preserved,
            separator,
            open,
            close,
            required
        } kind = collapsible;
        std::string text;
        int breaks = 0;
    };

    void collect(node_id node, const text_style & inherited, bool blockify = false) {
        const node_kind kind = txn_.kind(node).value_or(node_kind::comment);
        if (kind == node_kind::text) {
            if (inherited.hidden) { return; }
            std::string text{txn_.text(node)};
            if (inherited.transform > 0) { ascii_upper_in_place(text); }
            if (inherited.transform < 0) { text = ascii_lower_copy(text); }
            if (inherited.preserve) {
                items_.push_back({text_item::preserved, std::move(text), 0});
            } else if (inherited.preserve_breaks) {
                // pre-line: a newline is a forced break, everything else collapses.
                std::size_t at = 0;
                while (at <= text.size()) {
                    const std::size_t nl = text.find('\n', at);
                    const std::size_t stop = nl == std::string::npos ? text.size() : nl;
                    items_.push_back({text_item::collapsible, text.substr(at, stop - at), 0});
                    if (nl == std::string::npos) { break; }
                    items_.push_back({text_item::separator, "\n", 0});
                    at = nl + 1;
                }
            } else {
                items_.push_back({text_item::collapsible, std::move(text), 0});
            }
            return;
        }
        if (kind != node_kind::element) { return; }
        std::string display = display_of(node);
        if (display == "none") { return; }
        if (blockify && display != "contents" && !display.starts_with("table-")) {
            display = "block";
        }
        const bool html = is_html(node);
        const std::string_view tag = tag_of(node);
        if (html && tag == "br") {
            items_.push_back({text_item::separator, "\n", 0});
            return;
        }
        const text_style style = style_under(node, inherited);
        // "If node's computed value of visibility is not visible, then return
        // items": the children's text, and none of the breaks or tabs this
        // element's own box would have added.
        if (style.hidden) {
            collect_children(node, style, display);
            return;
        }
        // "If node is a p element, append 2" - by TAG, whatever its display;
        // the corpus asks for the blank lines around a `display: inline-block`
        // <p> by name. Otherwise a block-level box is a line of its own.
        const int breaks =
            html && tag == "p"                                                                ? 2
            : lists_token("block table flex grid list-item flow-root table-caption", display) ? 1
                                                                                              : 0;
        const bool atomic = breaks == 0 && (display.starts_with("inline-") ||
                                            (html && lists_token(replaced_tags, tag)));
        if (breaks > 0) { items_.push_back({text_item::required, {}, breaks}); }
        if (atomic) { items_.push_back({text_item::open, {}, 0}); }
        collect_children(node, style, display);
        if (display == "table-cell" && !last_of_kind(node, "table-cell")) {
            items_.push_back({text_item::separator, "\t", 0});
        }
        if (display == "table-row" && !last_row(node)) {
            items_.push_back({text_item::separator, "\n", 0});
        }
        if (atomic) { items_.push_back({text_item::close, {}, 0}); }
        if (breaks > 0) { items_.push_back({text_item::required, {}, breaks}); }
    }

    // Is there a later sibling element of this display? The specification
    // asks about boxes - "the last table-cell box of its enclosing table-row
    // box" - and a sibling in the same row is that box.
    [[nodiscard]] bool last_of_kind(node_id node, std::string_view display) const {
        bool after = false;
        for (const node_id sibling : txn_.children(txn_.parent(node))) {
            if (sibling == node) {
                after = true;
                continue;
            }
            if (after && txn_.kind(sibling).value_or(node_kind::text) == node_kind::element &&
                display_of(sibling) == display) {
                return false;
            }
        }
        return true;
    }
    // ...and a row's table is one level further up: the rows of a later row
    // group are still rows of this table.
    [[nodiscard]] bool last_row(node_id node) const {
        if (!last_of_kind(node, "table-row")) { return false; }
        const node_id group = txn_.parent(node);
        if (!group || display_of(group) != "table-row-group") { return true; }
        bool after = false;
        for (const node_id sibling : txn_.children(txn_.parent(group))) {
            if (sibling == group) {
                after = true;
                continue;
            }
            if (!after || txn_.kind(sibling).value_or(node_kind::text) != node_kind::element) {
                continue;
            }
            for (const node_id row : txn_.children(sibling)) {
                if (txn_.kind(row).value_or(node_kind::text) == node_kind::element &&
                    display_of(row) == "table-row") {
                    return false;
                }
            }
        }
        return true;
    }

    const read_txn & txn_;
    atom_table & atoms_;
    const style::style_map * styles_;
    atom style_;
    atom type_;
    atom hidden_;
    atom open_;
    std::vector<text_item> items_;
};

} // namespace

namespace detail {

// ...and the question the wrapper asks before it installs its own pair. The
// INHERITED rows count: `width` is on HTMLMediaElement, so a <video> has one
// even though no row names HTMLVideoElement. Walked rather than cached because
// it is asked only of an element that carries a width or height attribute.
[[nodiscard]] bool interface_reflects_size(std::string_view tag) {
    constexpr std::size_t count = std::size(interface_table);
    for (std::size_t at = interface_for_tag(tag); at < count;
         at = interface_index(interface_table[at].parent)) {
        for (const reflected_attribute & row : reflection_rows()) {
            if (row.idl == "width" && interface_index(row.interface) == at) { return true; }
        }
    }
    return false;
}

} // namespace detail

value dom_bindings::interface_prototype(std::string_view name) const {
    const std::size_t at = interface_index(name);
    return at < interface_prototypes_.size() ? interface_prototypes_[at] : value::undefined();
}

value dom_bindings::prototype_for_node(const read_txn & txn, node_id id) const {
    if (interface_prototypes_.empty() || !id) { return value::undefined(); }
    switch (txn.kind(id).value_or(node_kind::element)) {
    case node_kind::text: return interface_prototype("Text");
    case node_kind::comment: return interface_prototype("Comment");
    case node_kind::document: return interface_prototype("Document");
    case node_kind::document_type: return interface_prototype("DocumentType");
    case node_kind::processing_instruction: return interface_prototype("ProcessingInstruction");
    case node_kind::cdata_section: return interface_prototype("CDATASection");
    case node_kind::document_fragment:
        // THE SAME NODE KIND, TWO INTERFACES. A shadow root is a fragment that
        // `attachShadow` recorded a host and a mode for; anything else a page
        // built with createDocumentFragment is the plain interface.
        return interface_prototype(shadow_tree_of(id) != nullptr ? "ShadowRoot"
                                                                 : "DocumentFragment");
    case node_kind::element: break;
    }
    // THE NAMESPACE DECIDES FIRST. `document.createElementNS(svgNS, "title")` is
    // an SVGElement and `<title>` is an HTMLTitleElement, and the two intern to
    // the same atom - the trap the CLAUDE.md invariant names for anything
    // walking the DOM for <title>, <style> or <script>.
    if (txn.element_ns(id) == node_ns::svg) { return interface_prototype("SVGElement"); }
    if (txn.element_ns(id) != node_ns::html) { return interface_prototype("Element"); }
    // BY LOCAL NAME: `createElementNS(HTML, "foo:span")` is an HTMLSpanElement.
    const std::size_t at = interface_for_tag(txn.local_name(id));
    return at < interface_prototypes_.size() ? interface_prototypes_[at]
                                             : interface_prototype("HTMLElement");
}

void dom_bindings::ensure_dom_interfaces(context & cx) {
    if (interfaces_linked_) { return; }
    // NOT YET. `install()` builds the document before the events, and building
    // the document makes the first two element wrappers - so the first call to
    // wrap() happens while `EventTarget.prototype` does not exist, and a chain
    // built then would end one link short. Waiting costs those two wrappers
    // nothing: install_dom_interfaces re-links every wrapper that already exists.
    if (!event_target_prototype_.is_object()) { return; }
    install_dom_interfaces(cx);
}

void dom_bindings::install_dom_interfaces(context & cx) {
    constexpr std::size_t count = std::size(interface_table);
    interface_prototypes_.assign(count, value::undefined());

    // THE ONES ANOTHER FILE ALREADY OWNS ARE ADOPTED, NOT REBUILT. `EventTarget`
    // is constructible and carries addEventListener; `HTMLElement` comes from
    // install_custom_elements. Taking `Ctor.prototype` off the global that
    // exists is what keeps ONE prototype per interface however many places make
    // one, and it is what makes this function safe to call twice.
    for (std::size_t i = 0; i < count; ++i) {
        const std::string name{interface_table[i].name};
        if (!cx.has_global(name)) { continue; }
        const value proto = cx.lookup_property(cx.global(name), "prototype");
        if (proto.is_object()) { interface_prototypes_[i] = proto; }
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (interface_prototypes_[i].is_object()) { continue; }
        interface_prototypes_[i] = cx.make_object();
    }

    // A GC ROOT FOR ALL OF THEM AT ONCE - see the note on interface_keeper_.
    const value keeper = cx.make_array();
    auto * held = static_cast<script::array_object *>(keeper.as_heap());
    for (const value & proto : interface_prototypes_) { held->items.push_back(proto); }
    interface_keeper_ = keeper;

    // The chain, and the interface object in front of each link.
    for (std::size_t i = 0; i < count; ++i) {
        auto * proto = static_cast<script::object_object *>(interface_prototypes_[i].as_heap());
        const std::size_t parent = interface_index(interface_table[i].parent);
        if (parent < count) { proto->prototype = interface_prototypes_[parent]; }
        const std::string name{interface_table[i].name};
        value ctor_value = value::undefined();
        if (cx.has_global(name)) {
            // ADOPTED: the interface object another file made, kept as it is.
            // Redefining it would replace a CONSTRUCTIBLE `EventTarget` with a
            // stub, which is a regression rather than a feature.
            ctor_value = cx.global(name);
        } else {
            // NOT CONSTRUCTIBLE - for all but three of them. `new
            // HTMLDivElement()` throws in a browser too, and saying so is
            // better than handing back an object that is not an element - the
            // same choice install_window made for CanvasRenderingContext2D.
            //
            // THE THREE THAT ARE: `new Text("x")`, `new Comment("x")` and `new
            // DocumentFragment()`. The DOM makes exactly those constructible
            // and nothing else in this table, because they are the three nodes
            // a page can build without naming a document to build them in -
            // there is no `new HTMLDivElement`, there is `createElement`. Seven
            // files in `dom/nodes` open with one of them and lose every subtest
            // they have to the throw; see construct_node_interface.
            //
            // AND `new Document()`, DOM 4.5 - the fourth, and the one that
            // names no document because it IS one: a new XML document with
            // no browsing context. See make_xml_document.
            const bool constructible = name == "Text" || name == "Comment" ||
                                       name == "DocumentFragment" || name == "Document";
            auto * ctor = cx.allocate<script::native_object>(name, [this, name, constructible,
                                                                    i](context & c,
                                                                       std::span<value> args) {
                if (constructible) { return construct_node_interface(c, name, args); }
                // ...AND EVERY HTML ELEMENT INTERFACE, FROM A CUSTOMIZED
                // BUILT-IN: `class S extends HTMLScriptElement` with
                // `customElements.define("s-1", S, {extends: "script"})`
                // reaches `super()` here. HTML's "HTML element constructor"
                // is one algorithm for all of them - HTMLElement's, with
                // the check that the definition's local name has THIS
                // interface (Node-appendChild-cereactions-vs-script).
                const value self = c.current_this();
                if (name.starts_with("HTML") && self.is_object()) {
                    const std::size_t definition = custom_definition_of(c, self);
                    if (definition != std::numeric_limits<std::size_t>::max()) {
                        if (interface_for_tag(custom_definitions_[definition].local_name) != i) {
                            c.throw_error("TypeError", "Illegal constructor: the custom element "
                                                       "definition does not extend " +
                                                           name);
                            return value::undefined();
                        }
                        const value html_element = c.global("HTMLElement");
                        if (html_element.is_callable()) { return c.call(html_element, args, self); }
                    }
                }
                c.throw_error("TypeError",
                              "Illegal constructor: " + name + " cannot be constructed by a page");
                return value::undefined();
            });
            ctor->set("prototype", interface_prototypes_[i]);
            ctor->retained.push_back(keeper);
            ctor_value = value::object(ctor);
            cx.define_global(name, ctor_value);
        }
        // `constructor` and `@@toStringTag`, ON AN ADOPTED PROTOTYPE TOO - the
        // interfaces adopted from elsewhere carry neither, so
        // `canvas.constructor.name` read `HTMLElement` off the parent link until
        // this ran for them as well. Never OVERWRITTEN: a prototype that already
        // names its constructor knows better than this loop does.
        //
        // `constructor` is what `eventTarget.constructor.name` reads, and it is
        // what stands in front of `passive-by-default.html`: that test names its
        // subtests with a template literal that reads it, so an undefined
        // `constructor` threw before one assertion could run. `@@toStringTag` is
        // what `assert_class_string` reads, through
        // `Object.prototype.toString` - so `[object HTMLBodyElement]` is only
        // right if that builtin consults the tag, which is the one thing here
        // that lives outside this file.
        //
        // NOT ENUMERABLE, either of them. `Body-FrameSet-Event-Handlers.html`
        // enumerates an element with `for (var attribute in element)` and
        // compares the result against the IDL; a reflected attribute IS
        // enumerable there and `constructor` is not, which is what
        // `script::attr_builtin` spells.
        if (proto->find("constructor") == nullptr) {
            proto->set("constructor", ctor_value);
            proto->set_attrs("constructor", script::attr_builtin);
        }
        if (proto->find("@@toStringTag") == nullptr) {
            proto->set("@@toStringTag", cx.string(name));
            proto->set_attrs("@@toStringTag", script::attr_builtin);
        }
    }

    // THE REFLECTED ATTRIBUTES, one accessor pair per row, on the prototype of
    // the interface the row names. A row naming an interface that is not in the
    // table above is a typo rather than a feature, and is skipped rather than
    // silently landing on Object.prototype.
    for (const reflected_attribute & row : reflection_rows()) {
        const std::size_t at = interface_index(row.interface);
        if (at >= count) { continue; }
        auto * proto = static_cast<script::object_object *>(interface_prototypes_[at].as_heap());
        const std::string property{row.idl};
        // A POINTER INTO A STATIC TABLE, captured by value. The rows outlive
        // every page, so the accessors do not have to carry a copy of one.
        const reflected_attribute * held_row = &row;
        // THE DOCUMENT THAT OWNS THE RECEIVER answers, for the reason
        // define_operation gives: the prototype is shared by every document in
        // the realm, and `frameDoc.body.id` read the PRIMARY's tree at the
        // frame's node id before this.
        proto->define_accessor(property,
                               value::object(cx.allocate<script::native_object>(
                                   property,
                                   [this, held_row](context & c, std::span<value>) {
                                       dom_bindings * owner = owner_of(c.current_this());
                                       return (owner ? owner : this)->reflected_get(c, held_row);
                                   })),
                               value::object(cx.allocate<script::native_object>(
                                   property, [this, held_row](context & c, std::span<value> a) {
                                       dom_bindings * owner = owner_of(c.current_this());
                                       return (owner ? owner : this)->reflected_set(c, held_row, a);
                                   })));
    }

    // --- innerText AND outerText, THE SETTER HALF -----------------------------
    //
    // `el.innerText = "a\nb"` is NOT `textContent = "a\nb"`: the newline becomes
    // a <br> element and the text on either side of it becomes a Text node.
    // That rule - "the rendered text fragment" - is the whole of both setters
    // and it reads no layout at all, which is why the two halves of this
    // property can be separated. innertext-setter.html is 126 subtests of it.
    //
    // THE GETTER is the other half and reads no layout either - see
    // inner_text_collector above for what it reads instead, and why.
    if (const value html_interface = interface_prototype("HTMLElement");
        html_interface.is_object()) {
        auto * proto = static_cast<script::object_object *>(html_interface.as_heap());
        // ON HTMLElement AND NOT ON Element, which is a rule with a test behind
        // it: `svg.innerText = "abc"` must leave the <svg> empty, and
        // innertext-setter-tests.js checks a MathML element as well.
        //
        // [LegacyNullToEmptyString], so `null` clears the element and
        // `undefined` writes those nine letters - the same asymmetry
        // `CharacterData.data` has.
        const auto assigned = [](context & c, std::span<value> args) {
            return arg(args, 0).is_null() ? std::string{} : arg_string(c, args, 0);
        };
        // The fragment, as a list of nodes in document order. Built before
        // anything is removed: these calls only MAKE nodes, and a fragment that
        // failed to build should not have emptied the element on its way out.
        const auto rendered_nodes = [this](std::string_view text) {
            std::vector<node_id> made;
            each_rendered_text_part(
                text,
                [&](std::string_view run) {
                    if (const node_id node = doc_->create_text(run)) { made.push_back(node); }
                },
                [&] {
                    if (const node_id node = doc_->create_element(atoms_->intern_lower("br"))) {
                        made.push_back(node);
                    }
                });
            return made;
        };
        // "Merge with the next text node": a Text node followed by a Text node
        // becomes one, and NOTHING ELSE is normalised. outerText leaves
        // `A|B|Replaced|D|E` as `A|BReplacedD|E` on purpose, which the corpus
        // spells out in a subtest called "does not completely normalize".
        const auto merge_forward = [this](node_id node) {
            if (!node) { return; }
            std::string joined;
            node_id next;
            {
                const auto txn = doc_->read();
                if (txn.kind(node) != node_kind::text) { return; }
                const node_id parent = txn.parent(node);
                if (!parent) { return; }
                const std::span<const node_id> kids = txn.children(parent);
                for (std::size_t i = 0; i + 1 < kids.size(); ++i) {
                    if (kids[i] == node) {
                        next = kids[i + 1];
                        break;
                    }
                }
                if (!next || txn.kind(next) != node_kind::text) { return; }
                joined = std::string{txn.text(node)} + std::string{txn.text(next)};
            }
            (void)doc_->set_text(node, joined);
            (void)doc_->remove_child(next);
        };
        const auto set_inner_text = [this, assigned, rendered_nodes](context & c,
                                                                     std::span<value> args) {
            const node_id id = receiver(c);
            if (!id) { return value::undefined(); }
            const std::vector<node_id> made = rendered_nodes(assigned(c, args));
            // "Replace all with fragment within this."
            std::vector<node_id> existing;
            {
                const auto txn = doc_->read();
                for (const node_id child : txn.children(id)) { existing.push_back(child); }
            }
            for (const node_id child : existing) { (void)doc_->remove_child(child); }
            for (const node_id child : made) { (void)doc_->append_child(id, child); }
            mutated();
            return value::undefined();
        };
        const auto set_outer_text = [this, assigned, rendered_nodes,
                                     merge_forward](context & c, std::span<value> args) {
            const node_id id = receiver(c);
            if (!id) { return value::undefined(); }
            node_id parent;
            node_id next;
            node_id previous;
            {
                const auto txn = doc_->read();
                parent = txn.parent(id);
                if (parent) {
                    const std::span<const node_id> kids = txn.children(parent);
                    for (std::size_t i = 0; i < kids.size(); ++i) {
                        if (kids[i] != id) { continue; }
                        if (i + 1 < kids.size()) { next = kids[i + 1]; }
                        if (i > 0) { previous = kids[i - 1]; }
                        break;
                    }
                }
            }
            // "If this's parent is null, then throw a
            // NoModificationAllowedError" - the one way either setter can fail,
            // and the only reason outerText needs a body of its own at all.
            if (!parent) {
                throw_dom_exception(c, "NoModificationAllowedError",
                                    "outerText: the element has no parent to replace it in");
                return value::undefined();
            }
            std::vector<node_id> made = rendered_nodes(assigned(c, args));
            // "If fragment has no children, append a new Text node whose data
            // is the empty string": `el.outerText = ""` REPLACES the element
            // with an empty text node rather than removing it, which is what
            // lets the merge below join the text on either side of it.
            if (made.empty()) {
                if (const node_id empty = doc_->create_text("")) { made.push_back(empty); }
            }
            for (const node_id node : made) { (void)doc_->insert_before(parent, node, id); }
            (void)doc_->remove_child(id);
            // The two merges the specification names, in its order: the node
            // now in front of `next` first, then `previous`.
            if (!made.empty() && next) { merge_forward(made.back()); }
            merge_forward(previous);
            mutated();
            return value::undefined();
        };
        // THE GETTER, shared by both names: `outerText` reads exactly what
        // `innerText` reads. "If this is not being rendered, return this's
        // descendant text content" - and an element that is not connected, or
        // is under a `display: none`, is not rendered. See inner_text_collector.
        const auto rendered_text = [this](context & c, std::span<value>) {
            const node_id id = receiver(c);
            if (!id) { return value::undefined(); }
            bool not_rendered = false;
            std::string text;
            // The cascade as the script left the tree, so a class from the
            // page's own stylesheet counts; `styles_` stays null for a document
            // nothing lays out, and the collector reads inline style then.
            flush_layout();
            {
                const auto txn = doc_->read();
                inner_text_collector collector{txn, *atoms_, styles_};
                if (!is_document_root(txn, root_of_tree(txn, id, true)) ||
                    collector.unrendered(id)) {
                    not_rendered = true;
                } else {
                    collector.collect_children(id, collector.inherited_style(id),
                                               collector.display_of(id));
                    text = collector.finish();
                }
            }
            // Outside the read above: text_content opens one of its own.
            return c.string(not_rendered ? text_content(id) : text);
        };
        proto->define_accessor(
            "innerText",
            value::object(cx.allocate<script::native_object>("innerText", rendered_text)),
            value::object(cx.allocate<script::native_object>("innerText", set_inner_text)));
        proto->define_accessor(
            "outerText",
            value::object(cx.allocate<script::native_object>("outerText", rendered_text)),
            value::object(cx.allocate<script::native_object>("outerText", set_outer_text)));

        // --- translate, HTML 3.2.6.3 ---------------------------------------
        //
        // A boolean over an INHERITED enumerated attribute: `yes` and "" are
        // translate-enabled, `no` is no-translate, and anything else - the
        // absent attribute included - is the "inherit" state, which is the
        // PARENT ELEMENT's mode. The walk stops at the first node that is not
        // an element, so a child of a DocumentFragment or of a ShadowRoot is
        // translate-enabled whatever the host says, which
        // translate-inherit-no-parent-element.html spells out. Only an HTML
        // element's attribute counts; a foreign element is in the inherit
        // state and passes the question up.
        proto->define_accessor(
            "translate",
            value::object(cx.allocate<script::native_object>(
                "translate",
                [this](context & c, std::span<value>) {
                    const auto txn = doc_->read();
                    const atom name = atoms_->intern("translate");
                    for (node_id at = receiver(c);
                         at && txn.kind(at).value_or(node_kind::text) == node_kind::element;
                         at = txn.parent(at)) {
                        if (txn.element_ns(at) != node_ns::html || !txn.has_attribute(at, name)) {
                            continue;
                        }
                        const std::string mode = ascii_lower_copy(txn.attribute_value(at, name));
                        if (mode.empty() || mode == "yes") { return value::boolean(true); }
                        if (mode == "no") { return value::boolean(false); }
                    }
                    return value::boolean(true);
                })),
            value::object(cx.allocate<script::native_object>(
                "translate", [this](context & c, std::span<value> a) {
                    if (const node_id id = receiver(c)) {
                        (void)doc_->set_attribute(id, atoms_->intern("translate"),
                                                  !a.empty() && context::truthy(a[0]) ? "yes"
                                                                                      : "no");
                        mutated();
                    }
                    return value::undefined();
                })));
    }

    // HTMLHyperlinkElementUtils, HTML 4.6.3, on <a> and <area>: the whole mixin
    // over net/url.hpp's record, in element/hyperlink.cpp. AFTER the reflected
    // rows above, because its `href` replaces the `url`-typed row - an <a>
    // resolves against the document BASE url and a reflected row does not.
    install_hyperlink_utils(cx);

    // THE OPERATIONS THAT ARE NOT REFLECTED ATTRIBUTES, and so far that is the
    // whole of CharacterData and the two Text adds to it. Here rather than in
    // the loop above because a table of five signatures would be longer than
    // the five functions - see install_character_data.
    install_character_data(cx);
    install_named_node_map(cx);

    // EVERY OTHER OPERATION - Node's, Element's, the ParentNode and ChildNode
    // mixins', the canvas three - on the prototype WebIDL names, through one
    // mechanism. See define_operation in element/methods.cpp.
    install_operations(cx);

    // `animate` and `getAnimations` on Element.prototype, and the Animation
    // interfaces beside them - here because this is where that prototype
    // exists. See lib/Shell/bindings/animations.cpp.
    install_animations(cx);

    // The document and the window are EventTargets with interfaces of their own,
    // and `passive-by-default.html` reads `eventTarget.constructor.name` for
    // both of them before it can even name its subtests.
    if (auto * doc = document_object()) {
        doc->prototype = interface_prototype("Document");
        // ...and `document.implementation`, made with the document.
        if (const value * held = doc->find("implementation");
            held != nullptr && held->is_object()) {
            static_cast<script::object_object *>(held->as_heap())->prototype =
                interface_prototype("DOMImplementation");
        }
        // THE DOCUMENT'S OPERATIONS ON Document.prototype TOO. install_document
        // puts each on the document OBJECT - one closure per document over its
        // own tree - so `Document.prototype.createTextNode` was undefined and
        // `inner.Document.prototype.createTextNode.apply(document, ["x"])`
        // (node-creation-realm.html, and every `.call(doc, ...)` idiom) died
        // reading `apply`. Each name gets a forwarder that calls the RECEIVER'S
        // own method, which is the one closed over the right document; a
        // receiver with none - a plain object - is an illegal invocation.
        // DOMImplementation's two-plus-three the same way. Once per realm: the
        // secondary documents share the prototypes and their objects carry the
        // same names.
        const auto forward = [&cx](script::object_object & from, const value proto_value) {
            auto * proto = prototype_object(proto_value);
            if (proto == nullptr) { return; }
            std::vector<std::string> names;
            from.each_own_key([&](const std::string & key) {
                const value * held = from.find(key);
                // Not one the CHAIN already answers: appendChild is Node's and
                // addEventListener is EventTarget's, and Document.prototype
                // must not grow its own copies of either.
                if (held != nullptr && held->is_callable() && !key.starts_with("@@") &&
                    cx.lookup_property(proto_value, key).is_undefined()) {
                    names.push_back(key);
                }
            });
            for (const std::string & name : names) {
                // The forwarder's own identity, so a receiver that has no
                // method of that name and reaches the forwarder again through
                // the chain is told apart from one that has.
                const auto self_slot = std::make_shared<const script::heap_object *>(nullptr);
                auto * native = cx.allocate<script::native_object>(
                    name, [name, self_slot](context & c, std::span<value> args) {
                        const value self = c.current_this();
                        const value own = self.is_object_like() ? c.lookup_property(self, name)
                                                                : value::undefined();
                        if (own.is_callable() && own.as_heap() != *self_slot) {
                            return c.call(own, args, self);
                        }
                        c.throw_error("TypeError", "Illegal invocation: " + name +
                                                       " called on something that is not a " +
                                                       "Document");
                        return value::undefined();
                    });
                *self_slot = native;
                proto->define(name, value::object(native), script::attr_builtin);
            }
        };
        forward(*doc, interface_prototype("Document"));
        if (const value * held = doc->find("implementation");
            held != nullptr && held->is_object()) {
            forward(*static_cast<script::object_object *>(held->as_heap()),
                    interface_prototype("DOMImplementation"));
        }
    }
    if (auto * win = window_object()) { win->prototype = interface_prototype("Window"); }
    // "Window objects must also have a ... property named HTMLDocument whose
    // value is the Document interface object" (HTML 3.1.1).
    cx.define_global("HTMLDocument", cx.global("Document"));

    // EVERY WRAPPER THAT ALREADY EXISTS, RE-LINKED. Two of them are made by
    // install_document before this can run at all, and they are `document.body`
    // and `document.documentElement` - the two elements a test is most likely to
    // ask an instanceof about.
    {
        const auto txn = doc_->read();
        for (auto & [packed, obj] : wrappers_) {
            if (obj == nullptr) { continue; }
            const value proto = prototype_for_node(txn, unpack(packed));
            if (proto.is_object()) { obj->prototype = proto; }
        }
    }
    interfaces_linked_ = event_target_prototype_.is_object();
}

// `new Text("x")`, `new Comment("x")`, `new DocumentFragment()`.
//
// THE NODE IS OWNED BY THIS DOCUMENT AND IS NOT IN ITS TREE, which is the whole
// of what those three constructors do: `document.createTextNode` by another
// name, reachable without naming the document. `Comment-Text-constructor.js`
// asserts `object.ownerDocument === document` on every one of its fourteen
// cases and asserts the prototype chain runs Text -> CharacterData -> Node,
// which it does because `wrap` links a wrapper by the node's KIND and the chain
// was already built by the time anything can call this.
//
// ONE ARGUMENT, CONVERTED ONCE. The IDL is `(DOMString data = "")`, so a missing
// argument and an `undefined` one are both the empty string - and the second
// argument is never looked at, which the corpus checks with a `toString` that
// calls `assert_unreached`. `arg_string` would have made `undefined` the word
// "undefined", which is right for `appendData` and wrong here for the same
// reason a defaulted argument is not a passed one.
// `which` rather than `interface`, which is a MACRO on Windows: the mingw SDK
// headers define it as `struct`, and a parameter by that name is a build that
// fails on one platform only - the exact shape of defect the devbox exists to
// catch and the cross build finds later still.
value dom_bindings::construct_node_interface(context & cx, std::string_view which,
                                             std::span<value> args) {
    if (which == "DocumentFragment") { return wrap(cx, doc_->create_fragment()); }
    if (which == "Document") { return make_xml_document(cx, {}, {}, false); }
    const value given = arg(args, 0);
    const std::string data = given.is_undefined() ? std::string{} : cx.to_string(given);
    return wrap(cx, which == "Comment" ? doc_->create_comment(data) : doc_->create_text(data));
}

} // namespace ctbrowser::shell
