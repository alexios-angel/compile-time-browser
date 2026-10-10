//===- interfaces.cpp - DOM interface objects -----------------------------===//
//
// The interface objects: the table of DOM interfaces and their prototype
// chain, the prototypes a page can name, and install_dom_interfaces, which
// builds them and installs the reflected attributes and operations on them.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

/// One interface. `parent` is the interface whose prototype this one's chains
/// to (empty means Object.prototype, where EventTarget and the collections
/// stop), and `tags` is every HTML tag name whose element is this interface, so
/// the tag-to-prototype map and the inheritance chain are one table rather than
/// two that can disagree.
struct dom_interface {
    std::string_view name;
    std::string_view parent;
    std::string_view tags;
};

/// Every interface, with its parent. A typical chain:
///   HTMLDivElement -> HTMLElement -> Element -> Node -> EventTarget -> Object
///
/// EventTarget comes first so that everything below it can name it, and the
/// interfaces another translation unit already owns (EventTarget, HTMLElement)
/// are adopted rather than rebuilt: see install_dom_interfaces.
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
    // A ShadowRoot is a DocumentFragment: `instanceof` has to answer true for
    // both, which is why it is in the chain rather than beside it.
    // No tags: which fragment is a shadow root is a question about
    // `shadow_hosts_`, not about a name. See prototype_for_node.
    {"ShadowRoot", "DocumentFragment", ""},
    {"Attr", "Node", ""},
    // Not a node and not in any chain: `DOMStringMap` exists so that
    // `el.dataset instanceof DOMStringMap` can be true and so that the name is
    // a global a page can feature-detect, for HTML, SVG and MathML elements.
    {"DOMStringMap", "", ""},
    {"Window", "EventTarget", ""},
    // The collections. They are not nodes and inherit from nothing; they are
    // here so that `document.links instanceof HTMLCollection` and
    // `x instanceof NodeList` work. Other files reach them through
    // interface_prototype().
    {"NodeList", "", ""},
    {"HTMLCollection", "", ""},
    // The three form collections, HTML 4.10.20.1 and 2.6.2: `form.elements`,
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
    // DOM 6's two walkers: not nodes, not constructible; made by
    // bindings/document/traversal.cpp.
    {"TreeWalker", "", ""},
    {"NodeIterator", "", ""},
    // DOM 5's ranges. `Range` and `StaticRange` are constructible globals that
    // install_range made and that are adopted here; this table chains both to
    // AbstractRange, which is made here and constructs nothing.
    {"AbstractRange", "", ""},
    {"Range", "AbstractRange", ""},
    {"StaticRange", "AbstractRange", ""},

    // Every tag that is a plain HTMLElement, listed rather than left to the
    // fallback, so that anything not here can be told apart from them: HTML
    // says an unrecognised tag (<blink>, <isindex>) is an HTMLUnknownElement.
    {"HTMLElement", "Element",
     "abbr acronym address article aside b bdi bdo big center cite code dd dfn dt em figcaption "
     "figure footer header hgroup i kbd main mark nav nobr noembed noframes noscript plaintext rb "
     "rp rt rtc ruby s samp search section small strike strong sub summary sup tt u var wbr"},
    // Not an HTMLElement: an element in a foreign namespace is
    // `instanceof Element` but not `instanceof HTMLElement`.
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

/// The table index of interface `name`, or the table size when there is none.
[[nodiscard]] constexpr std::size_t interface_index(std::string_view name) {
    for (std::size_t i = 0; i < std::size(interface_table); ++i) {
        if (interface_table[i].name == name) { return i; }
    }
    return std::size(interface_table);
}

/// The table index of the interface for an HTML tag. A tag the table does not
/// name is an HTMLUnknownElement, unless its name contains a hyphen, which
/// makes it a valid custom element name and therefore an ordinary HTMLElement:
/// <blink> is HTMLUnknownElement, a page's own <my-widget> is not.
///
/// Either way it inherits from HTMLElement, so only the interface name, not
/// the global attributes, depends on getting this right.
[[nodiscard]] constexpr std::size_t interface_for_tag(std::string_view tag) {
    for (std::size_t i = 0; i < std::size(interface_table); ++i) {
        if (lists_token(interface_table[i].tags, tag)) { return i; }
    }
    return interface_index(tag.find('-') == std::string_view::npos ? "HTMLUnknownElement"
                                                                   : "HTMLElement");
}

} // namespace

namespace detail {

/// Whether the interface for `tag`, or one it inherits from, has a reflected
/// `width` attribute row (e.g. <video> through HTMLMediaElement). The element
/// wrapper asks this before installing its own width/height pair. Walked
/// rather than cached because it is asked only of an element that carries a
/// width or height attribute.
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

std::string_view dom_bindings::interface_name_for_tag(std::string_view tag) {
    return interface_table[interface_for_tag(tag)].name;
}

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
        // One node kind, two interfaces. A shadow root is a fragment that
        // `attachShadow` recorded a host and a mode for; anything else a page
        // built with createDocumentFragment is the plain interface.
        return interface_prototype(shadow_tree_of(id) != nullptr ? "ShadowRoot"
                                                                 : "DocumentFragment");
    case node_kind::element: break;
    }
    // The namespace decides first. `document.createElementNS(svgNS, "title")`
    // is an SVGElement and `<title>` is an HTMLTitleElement, though both names
    // intern to the same atom.
    if (txn.element_ns(id) == node_ns::svg) { return interface_prototype("SVGElement"); }
    if (txn.element_ns(id) != node_ns::html) { return interface_prototype("Element"); }
    // By local name: `createElementNS(HTML, "foo:span")` is an HTMLSpanElement.
    const std::size_t at = interface_for_tag(txn.local_name(id));
    return at < interface_prototypes_.size() ? interface_prototypes_[at]
                                             : interface_prototype("HTMLElement");
}

void dom_bindings::ensure_dom_interfaces(context & cx) {
    if (interfaces_linked_) { return; }
    // Not yet: `install()` builds the document before the events, and building
    // the document makes the first two element wrappers, so the first call to
    // wrap() happens while `EventTarget.prototype` does not exist and a chain
    // built then would end one link short. Waiting costs those two wrappers
    // nothing: install_dom_interfaces re-links every wrapper that already exists.
    if (!event_target_prototype_.is_object()) { return; }
    install_dom_interfaces(cx);
}

void dom_bindings::install_dom_interfaces(context & cx) {
    constexpr std::size_t count = std::size(interface_table);
    interface_prototypes_.assign(count, value::undefined());

    // Interfaces another file already owns are adopted, not rebuilt.
    // `EventTarget` is constructible and carries addEventListener; `HTMLElement`
    // comes from install_custom_elements. Taking `Ctor.prototype` off the
    // existing global keeps one prototype per interface however many places make
    // one, and makes this function safe to call twice.
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

    // One GC root for all of them at once; see the note on interface_keeper_.
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
            // Adopted: the interface object another file made, kept as it is.
            // Redefining it would replace a constructible `EventTarget` with a
            // stub.
            ctor_value = cx.global(name);
        } else {
            // Not constructible, except four: `new Text("x")`, `new Comment("x")`,
            // `new DocumentFragment()` and `new Document()` (DOM 4.5), the nodes a
            // page can build without naming a document (a new Document is
            // itself an XML document with no browsing context). Elements are
            // made with createElement, and `new HTMLDivElement()` throws as in
            // a browser rather than returning an object that is not an element;
            // see construct_node_interface and make_xml_document.
            const bool constructible = name == "Text" || name == "Comment" ||
                                       name == "DocumentFragment" || name == "Document";
            auto * ctor = cx.allocate<script::native_object>(
                name, [this, name, constructible](context & c, std::span<value> args) {
                    if (constructible) { return construct_node_interface(c, name, args); }
                    // Every HTML element interface is also reached from a
                    // customized built-in: `class S extends HTMLScriptElement`
                    // with `customElements.define("s-1", S, {extends: "script"})`
                    // reaches `super()` here. HTML's "HTML element constructor"
                    // is one algorithm for all of them (HTMLElement's), with
                    // the check that the definition's local name has this
                    // interface.
                    if (name.starts_with("HTML")) {
                        return construct_html_element(c, c.current_this(), name);
                    }
                    c.throw_error("TypeError", "Illegal constructor: " + name +
                                                   " cannot be constructed by a page");
                    return value::undefined();
                });
            ctor->set("prototype", interface_prototypes_[i]);
            ctor->retained.push_back(keeper);
            ctor_value = value::object(ctor);
            cx.define_builtin_global(name, ctor_value);
        }
        // `constructor` and `@@toStringTag`, on adopted prototypes too: the
        // interfaces adopted from elsewhere carry neither, and without them
        // `canvas.constructor.name` would read `HTMLElement` off the parent
        // link. Never overwritten: a prototype that already names its
        // constructor knows better than this loop does.
        //
        // `@@toStringTag` is read through `Object.prototype.toString`, so
        // `[object HTMLBodyElement]` also depends on that builtin consulting
        // the tag. Neither property is enumerable (script::attr_builtin):
        // `for (var attribute in element)` lists reflected attributes but not
        // `constructor`.
        if (proto->find("constructor") == nullptr) {
            proto->set("constructor", ctor_value);
            proto->set_attrs("constructor", script::attr_builtin);
        }
        if (proto->find(CTBROWSER_SYMBOL_KEY("toStringTag")) == nullptr) {
            proto->set(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string(name));
            proto->set_attrs(CTBROWSER_SYMBOL_KEY("toStringTag"), script::attr_builtin);
        }
    }

    // The reflected attributes: one accessor pair per row, on the prototype of
    // the interface the row names. A row naming an interface that is not in the
    // table above is a typo rather than a feature, and is skipped rather than
    // silently landing on Object.prototype.
    for (const reflected_attribute & row : reflection_rows()) {
        const std::size_t at = interface_index(row.interface);
        if (at >= count) { continue; }
        auto * proto = static_cast<script::object_object *>(interface_prototypes_[at].as_heap());
        const std::string property{row.idl};
        // A pointer into a static table, captured by value. The rows outlive
        // every page, so the accessors do not have to carry a copy of one.
        const reflected_attribute * held_row = &row;
        // The document that owns the receiver answers, for the reason
        // define_operation gives: the prototype is shared by every document in
        // the realm, so `frameDoc.body.id` must read the frame's tree, not the
        // primary document's.
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

    install_rendered_text(cx);

    // HTMLHyperlinkElementUtils, HTML 4.6.3, on <a> and <area>: the whole mixin
    // over net/url.hpp's record, in element/hyperlink.cpp. After the reflected
    // rows above, because its `href` replaces the `url`-typed row: an <a>
    // resolves against the document base URL and a reflected row does not.
    install_hyperlink_utils(cx);

    // CharacterData's operations, the two Text adds, and NamedNodeMap's.
    // Installed directly rather than from a table, because a table of five
    // signatures would be longer than the five functions; see
    // install_character_data.
    install_character_data(cx);
    install_named_node_map(cx);

    // Every other operation (Node's, Element's, the ParentNode and ChildNode
    // mixins', the canvas three) on the prototype WebIDL names, through one
    // mechanism. See define_operation in element/methods.cpp.
    install_operations(cx);
    // `style`, on the three prototypes that mix in ElementCSSInlineStyle.
    install_style_accessor(cx);

    // `animate` and `getAnimations` on Element.prototype, and the Animation
    // interfaces beside them; installed here because this is where that
    // prototype exists. See lib/Shell/bindings/animations.cpp.
    install_animations(cx);

    // The document and the window are EventTargets with interfaces of their
    // own, so `document.constructor.name` and `window.constructor.name` work.
    if (auto * doc = document_object()) {
        doc->prototype = interface_prototype("Document");
        // ...and `document.implementation`, made with the document.
        if (const value * held = doc->find("implementation");
            held != nullptr && held->is_object()) {
            static_cast<script::object_object *>(held->as_heap())->prototype =
                interface_prototype("DOMImplementation");
        }
        // The document's operations go on Document.prototype too. install_document
        // puts each on the document object (one closure per document over its
        // own tree), so without this `Document.prototype.createTextNode` would
        // be undefined and `Document.prototype.createTextNode.call(doc, "x")`
        // would fail. Each name gets a forwarder that calls the receiver's own
        // method, which is the one closed over the right document; a receiver
        // with none (a plain object) is an illegal invocation.
        // DOMImplementation's methods are forwarded the same way. Once per
        // realm: the secondary documents share the prototypes and their
        // objects carry the same names.
        const auto forward = [&cx](script::object_object & from, const value proto_value) {
            auto * proto = prototype_object(proto_value);
            if (proto == nullptr) { return; }
            std::vector<std::string> names;
            from.each_own_key([&](const std::string & key) {
                const value * held = from.find(key);
                // Not one the chain already answers: appendChild is Node's and
                // addEventListener is EventTarget's, and Document.prototype
                // must not grow its own copies of either.
                if (held != nullptr && held->is_callable() && !key.starts_with(symbol_key_mark) &&
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
    cx.define_builtin_global("HTMLDocument", cx.global("Document"));

    // Re-link every wrapper that already exists. Two are made by
    // install_document before this can run at all: `document.body` and
    // `document.documentElement`.
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

/// Implements `new Text(data)`, `new Comment(data)`, `new DocumentFragment()`
/// and `new Document()`. The first three make a node owned by this document
/// but not in its tree (like `document.createTextNode`); wrap() links its
/// prototype chain by node kind. Document makes a new XML document. `data` is
/// `DOMString data = ""`: missing and undefined both give "" (arg_string would
/// give "undefined"), and later arguments are never read.
///
/// The parameter is `which`, not `interface`: the MinGW SDK headers
/// `#define interface struct`, so that name breaks the Windows build.
value dom_bindings::construct_node_interface(context & cx, std::string_view which,
                                             std::span<value> args) {
    if (which == "DocumentFragment") { return wrap(cx, doc_->create_fragment()); }
    if (which == "Document") { return make_xml_document(cx, {}, {}, false); }
    const value given = arg(args, 0);
    const std::string data = given.is_undefined() ? std::string{} : cx.to_string(given);
    return wrap(cx, which == "Comment" ? doc_->create_comment(data) : doc_->create_text(data));
}

} // namespace ctbrowser::shell
