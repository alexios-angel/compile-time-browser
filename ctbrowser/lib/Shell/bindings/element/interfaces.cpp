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
            auto * ctor = cx.allocate<script::native_object>(
                name, [this, name, constructible](context & c, std::span<value> args) {
                    if (constructible) { return construct_node_interface(c, name, args); }
                    // ...AND EVERY HTML ELEMENT INTERFACE, FROM A CUSTOMIZED
                    // BUILT-IN: `class S extends HTMLScriptElement` with
                    // `customElements.define("s-1", S, {extends: "script"})`
                    // reaches `super()` here. HTML's "HTML element constructor"
                    // is one algorithm for all of them - HTMLElement's, with
                    // the check that the definition's local name has THIS
                    // interface (Node-appendChild-cereactions-vs-script).
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

    install_rendered_text(cx);

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
    // `style`, on the three prototypes that mix in ElementCSSInlineStyle.
    install_style_accessor(cx);

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
