// dom_bindings - the interface objects: the table of DOM interfaces and their
// prototype chain, and the prototypes a page can name.
//
// One of twelve files carved out of a 5,442-line bindings/element.cpp on
// 2026-09-08 - which was itself one of six carved out of bindings.cpp on
// 2026-08-09. All are member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp; the helpers more than one of them
// needs are declared in internal.hpp beside this, with external linkage in
// ctbrowser::shell::detail. Nothing about the public header changed.

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
// interfaces another translation unit already owns - EventTarget itself,
// HTMLCanvasElement, HTMLImageElement - are ADOPTED rather than rebuilt: see
// install_dom_interfaces.
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
    {"DOMTokenList", "", ""},
    {"NamedNodeMap", "", ""},
    {"DOMStringMap", "", ""},

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
    const std::size_t at = interface_for_tag(atoms_->text(txn.tag(id).value_or(atom{})));
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
    // is constructible and carries addEventListener; `HTMLCanvasElement` and
    // `HTMLImageElement` are defined by install_window and canvas wrappers are
    // already linked to them. Taking `Ctor.prototype` off the global that
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
            // same choice install_window made for HTMLCanvasElement.
            //
            // THE THREE THAT ARE: `new Text("x")`, `new Comment("x")` and `new
            // DocumentFragment()`. The DOM makes exactly those constructible
            // and nothing else in this table, because they are the three nodes
            // a page can build without naming a document to build them in -
            // there is no `new HTMLDivElement`, there is `createElement`. Seven
            // files in `dom/nodes` open with one of them and lose every subtest
            // they have to the throw; see construct_node_interface.
            const bool constructible =
                name == "Text" || name == "Comment" || name == "DocumentFragment";
            auto * ctor = cx.allocate<script::native_object>(
                name, [this, name, constructible](context & c, std::span<value> args) {
                    if (constructible) { return construct_node_interface(c, name, args); }
                    c.throw_error("TypeError", "Illegal constructor: " + name +
                                                   " cannot be constructed by a page");
                    return value::undefined();
                });
            ctor->set("prototype", interface_prototypes_[i]);
            ctor->retained.push_back(keeper);
            ctor_value = value::object(ctor);
            cx.define_global(name, ctor_value);
        }
        // `constructor` and `@@toStringTag`, ON AN ADOPTED PROTOTYPE TOO - the
        // four interfaces install_window builds carry neither, so
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
        proto->define_accessor(property,
                               value::object(cx.allocate<script::native_object>(
                                   property,
                                   [this, held_row](context & c, std::span<value>) {
                                       return reflected_get(c, held_row);
                                   })),
                               value::object(cx.allocate<script::native_object>(
                                   property, [this, held_row](context & c, std::span<value> a) {
                                       return reflected_set(c, held_row, a);
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
    // THE GETTERS ARE NOT HERE, and reading either still answers `undefined`.
    // `innerText` is the RENDERED text: the specification's first step is "if
    // this is not being rendered, return this's descendant text content" and
    // every step after it reads the box tree - `display`, `white-space`, a
    // ::before, a table cell's tab. This engine lays out on a FRAME rather than
    // on demand, so the boxes a getter would walk here are the ones from before
    // the script's own mutations: `container.innerHTML = x; e.innerText` would
    // answer about the page as it was. Answering out of textContent instead
    // would be a different property wearing this one's name. What the getter
    // needs first is a layout flush a binding can ask for, and that is
    // browser.cpp's to give.
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
        proto->define_accessor(
            "innerText", value::undefined(),
            value::object(cx.allocate<script::native_object>("innerText", set_inner_text)));
        proto->define_accessor(
            "outerText", value::undefined(),
            value::object(cx.allocate<script::native_object>("outerText", set_outer_text)));
    }

    // THE OPERATIONS THAT ARE NOT REFLECTED ATTRIBUTES, and so far that is the
    // whole of CharacterData and the two Text adds to it. Here rather than in
    // the loop above because a table of five signatures would be longer than
    // the five functions - see install_character_data.
    install_character_data(cx);

    // `isEqualNode` and `isSameNode`, ON Node.prototype - so an element, a text
    // node, a comment and a fragment all have them, which is the point. The
    // Document has its OWN pair as own properties (bindings/document.cpp) and
    // those shadow these; with one document per page they can only agree.
    if (const value node_interface = interface_prototype("Node"); node_interface.is_object()) {
        auto * proto = static_cast<script::object_object *>(node_interface.as_heap());
        const auto method = [&](const std::string & name, script::native_fn fn) {
            proto->set(name,
                       value::object(cx.allocate<script::native_object>(name, std::move(fn))));
            proto->set_attrs(name, script::attr_builtin);
        };
        method("isEqualNode", [this](context & c, std::span<value> args) {
            const node_id self = receiver(c);
            // A NULL ARGUMENT IS NOT AN ERROR AND IS NOT EQUAL. The IDL is
            // `Node?`, so `isEqualNode(null)` is a question with the answer
            // false rather than a TypeError.
            const node_id other = handle_of(arg(args, 0));
            if (!self || !other) { return value::boolean(false); }
            const auto txn = doc_->read();
            return value::boolean(nodes_are_equal(txn, self, other));
        });
        method("isSameNode", [this](context & c, std::span<value> args) {
            // IDENTITY, and nothing else: this is `===` with a name, and it is
            // a separate method because `isEqualNode` is not.
            const node_id self = receiver(c);
            const node_id other = handle_of(arg(args, 0));
            return value::boolean(self && other && self == other);
        });
    }

    // The document and the window are EventTargets with interfaces of their own,
    // and `passive-by-default.html` reads `eventTarget.constructor.name` for
    // both of them before it can even name its subtests.
    if (auto * doc = document_object()) { doc->prototype = interface_prototype("Document"); }
    if (auto * win = window_object()) { win->prototype = interface_prototype("Window"); }

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
    const value given = arg(args, 0);
    const std::string data = given.is_undefined() ? std::string{} : cx.to_string(given);
    return wrap(cx, which == "Comment" ? doc_->create_comment(data) : doc_->create_text(data));
}

} // namespace ctbrowser::shell
