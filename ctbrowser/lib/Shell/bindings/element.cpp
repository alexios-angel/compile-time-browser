// dom_bindings - the element wrapper - what `document.querySelector` hands back.
//
// One of six files carved out of a 3,926-line bindings.cpp on 2026-08-09.
// These are all member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp, so they split across translation
// units with nothing to declare and no linkage to arrange.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>

#include <ctbrowser/style/css/parser.hpp>
#include <ctbrowser/style/css/properties.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// dom_bindings' method bodies - the API a page's script actually calls.
//
// The header lists what a page can reach; this is how each one works.

namespace ctbrowser::shell {

value dom_bindings::wrap(context & cx, node_id id) {
    if (!id) { return value::null(); }
    // The interfaces, if they can be built yet - see ensure_dom_interfaces. A
    // no-op after the first success, and the reason it is HERE is that this is
    // the earliest thing a page can do that needs one.
    ensure_dom_interfaces(cx);
    if (const auto it = wrappers_.find(pack(id)); it != wrappers_.end()) {
        refresh_element(cx, *it->second, id);
        return value::object(it->second);
    }
    auto * obj = static_cast<script::object_object *>(cx.make_object().as_heap());
    value wrapper = value::object(obj);
    obj->set(std::string{handle_property}, value::number(static_cast<double>(pack(id))));
    // WHICH INTERFACE THIS NODE IS. `HTMLCanvasElement.prototype` for a canvas,
    // `Text.prototype` for a text node, `HTMLElement.prototype` for a tag with
    // no interface of its own - and every one of those chains down to
    // `EventTarget.prototype`. It is what `instanceof` answers from AND where
    // the reflected IDL attributes live, so an element with no prototype has
    // neither `id` nor `className`.
    {
        const auto txn = doc_->read();
        const value proto = prototype_for_node(txn, id);
        if (proto.is_object()) { obj->prototype = proto; }
    }
    install_element_methods(cx, *obj);
    install_element_views(cx, *obj, id);
    // AFTER both, because two of the members a ShadowRoot needs - querySelector
    // and querySelectorAll - REPLACE the general ones: the general pair cannot
    // see a detached fragment at all. See install_shadow_root_members.
    if (shadow_tree_of(id) != nullptr) { install_shadow_root_members(cx, *obj, id); }
    refresh_element(cx, *obj, id);
    wrappers_.emplace(pack(id), obj);
    return wrapper;
}

std::uint64_t dom_bindings::pack(node_id id) {
    return (static_cast<std::uint64_t>(id.generation) << 32) | id.slot;
}

node_id dom_bindings::unpack(std::uint64_t bits) {
    node_id id;
    id.slot = static_cast<std::uint32_t>(bits & 0xFFFFFFFFu);
    id.generation = static_cast<std::uint32_t>(bits >> 32);
    return id;
}

node_id dom_bindings::receiver(context & cx) {
    const value self = cx.current_this();
    if (!self.is_object()) { return node_id{}; }
    auto * obj = static_cast<script::object_object *>(self.as_heap());
    const value * slot = obj->find(std::string{handle_property});
    if (slot == nullptr) { return node_id{}; }
    return unpack(static_cast<std::uint64_t>(context::to_number(*slot)));
}

void dom_bindings::refresh_element(context & cx, script::object_object & obj, node_id id) {
    // Here as well as in wrap(), because refresh_wrappers() walks every live
    // wrapper before a dispatch and before a frame - so a page that never asks
    // for a new element still gets its interfaces built, and the wrappers
    // install_document made before EventTarget existed still get linked.
    ensure_dom_interfaces(cx);
    const auto txn = doc_->read();
    // `tagName` is UPPERCASE for an HTML element, and lowercase was a silent
    // wrong answer: p5.js branches on `elt.tagName === 'INPUT'` and on
    // `child.tagName === param` in its XML module, so every such comparison was
    // false and the code behind it never ran. An SVG element keeps its own case -
    // tagName is the qualified name, and only HTML uppercases it.
    {
        std::string tag_name{atoms_->text(txn.tag(id).value_or(atom{}))};
        // AND ONLY IN AN HTML DOCUMENT. `tagName` uppercases an HTML element,
        // and an XHTML one parsed from an `.xhtml` file is in the HTML
        // namespace too - but `Node-nodeName-xhtml.xhtml` asserts `i` and not
        // `I`, because the rule is about the DOCUMENT's language rather than
        // the element's vocabulary. The two agreed as long as the only way to
        // build a document was the HTML tree builder; see dom/xml.hpp.
        if (txn.element_ns(id) == node_ns::html && !doc_->xml()) {
            for (char & c : tag_name) {
                if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - 'a' + 'A'); }
            }
        }
        obj.set("tagName", cx.string(tag_name));
        // `nodeName` AND `nodeType`, which every tree-walking page reads and
        // this wrapper did not have. They are not aliases of `tagName`: a
        // wrapper is made for text and comment nodes too - `childNodes` hands
        // them out - and for those the tag is empty, so `tagName` is "" and
        // `nodeName` is "#text". A page that switches on nodeType to decide
        // whether to recurse got `undefined` and took no branch at all.
        const node_kind kind = txn.kind(id).value_or(node_kind::element);
        switch (kind) {
        case node_kind::element:
            obj.set("nodeName", cx.string(tag_name));
            obj.set("nodeType", value::number(1));
            break;
        case node_kind::text:
            obj.set("nodeName", cx.string("#text"));
            obj.set("nodeType", value::number(3));
            break;
        case node_kind::comment:
            obj.set("nodeName", cx.string("#comment"));
            obj.set("nodeType", value::number(8));
            break;
        case node_kind::document:
            obj.set("nodeName", cx.string("#document"));
            obj.set("nodeType", value::number(9));
            break;
        case node_kind::document_fragment:
            obj.set("nodeName", cx.string("#document-fragment"));
            obj.set("nodeType", value::number(11));
            break;
        }
        // `ownerDocument` - null on the Document itself and `document` on
        // everything else, there being exactly one document for it to be. Six
        // files in `dom/nodes` read it off a node the test has just created,
        // including every valid case of `Document-createElement{,NS}` and all of
        // the Comment and Text constructor tests.
        obj.set("ownerDocument", kind == node_kind::document ? value::null() : document_);
        // `localName`, `prefix` and `namespaceURI` - the three halves of a
        // qualified name, and the pair `tagName` is compared against.
        //
        // localName is the tag WITHOUT the case fold and WITHOUT the prefix:
        // `createElementNS(ns, "a:b")` has tagName "a:b" and localName "b", and
        // reporting the whole qualified name for both makes the two
        // indistinguishable. prefix is null when there is no colon, which is
        // every element the parser builds.
        if (kind == node_kind::element) {
            const std::string_view qualified = atoms_->text(txn.tag(id).value_or(atom{}));
            const std::size_t colon = qualified.find(':');
            if (colon == std::string_view::npos) {
                obj.set("localName", cx.string(std::string{qualified}));
                obj.set("prefix", value::null());
            } else {
                obj.set("localName", cx.string(std::string{qualified.substr(colon + 1)}));
                obj.set("prefix", cx.string(std::string{qualified.substr(0, colon)}));
            }
            const std::string ns = namespace_of(id);
            obj.set("namespaceURI", ns.empty() ? value::null() : cx.string(ns));
        } else {
            obj.set("localName", value::undefined());
            obj.set("prefix", value::null());
            obj.set("namespaceURI", value::null());
        }
    }
    // `id`, `className`, `width` and `height` are NOT set here: they are
    // accessors over the attributes, installed once in install_element_views.
    // As data properties they were write-only in the wrong direction - a page
    // assigning `el.id = 'x'` changed the wrapper and nothing else, and the
    // next refresh put the old value back. p5.js names its canvas and sizes it
    // that way, so both writes vanished.
    const std::string_view tag_text = atoms_->text(txn.tag(id).value_or(atom{}));

    refresh_control(cx, obj, txn, id, tag_text);

    const rect box = box_of(id);
    obj.set("offsetLeft", value::number(static_cast<double>(box.x)));
    obj.set("offsetTop", value::number(static_cast<double>(box.y)));
    obj.set("offsetWidth", value::number(static_cast<double>(box.width)));
    obj.set("offsetHeight", value::number(static_cast<double>(box.height)));
    // `clientWidth`/`clientHeight` - the CONTENT box, and the only way a page
    // asks how big the viewport is: p5's own windowWidth and windowHeight are
    // `document.documentElement.clientWidth`, so both of them read `undefined`
    // and every sketch that sizes itself to the window got NaN.
    //
    // THE ROOT'S CLIENT RECTANGLE IS THE VIEWPORT, and its two axes come from
    // different places on purpose:
    //
    //   width  - the root element's own box. That box fills the initial
    //            containing block, so its width IS the layout viewport: 15px
    //            narrower than the window when the page overflows and a
    //            scrollbar appears. Reading it from the box rather than from a
    //            number the shell pushes in removes an ordering hazard - script
    //            bindings are installed lazily, so whichever of layout and
    //            install ran last decided the answer, and the wrong one won.
    //            Bootstrap's `.container` centred itself in 1009px while the
    //            page was told it had 1024.
    //   height - the VIEWPORT's, not the box's. The root box is as tall as the
    //            document, and `document.documentElement.clientHeight` means
    //            "how tall is the window", which is what p5's windowHeight and
    //            every self-sizing sketch is asking.
    //
    // The body is an ordinary element here: its client box is its own, which is
    // what Chrome reports and differs from the root's whenever the UA margin is
    // in play. For anything else the content box is the border box - nothing
    // here has a scrollbar of its own, and borders are not yet in the box
    // arithmetic.
    //
    // Before the first layout the root has no box, and a sketch that sizes itself
    // in `setup()` would read zero - which is how p5's windowWidth broke when
    // this moved off the number the shell pushes in. The window stands in, FOR
    // THE ROOT ONLY: an ordinary element with no box has a client width of zero,
    // and handing it the viewport instead told Babylon its canvas was
    // window-sized before layout had given it any size at all, which failed
    // WebGL setup outright. "No box" and "as wide as the window" are the same
    // thing for the root and nothing else.
    const bool is_root = tag_text == "html";
    obj.set("clientWidth",
            value::number(is_root && box.width <= 0 ? viewport_width_
                                                    : static_cast<double>(box.width)));
    obj.set("clientHeight",
            value::number(is_root ? viewport_height_ : static_cast<double>(box.height)));
    // `element.attributes` IS NOT SET HERE ANY MORE. It used to be a fresh
    // array of {name, value} pairs rebuilt on every sync, which is two wrong
    // answers at once: it was a SNAPSHOT, so `el.setAttribute(...)` followed by
    // `el.attributes[0].value` in the same statement read the old text, and its
    // members were plain objects rather than Attr nodes - no localName, no
    // prefix, no namespaceURI, no ownerElement, and nothing to write through.
    // It is a live accessor over a real NamedNodeMap now; see
    // install_element_views.
    obj.set("clientLeft", value::number(0));
    obj.set("clientTop", value::number(0));
    obj.set("scrollWidth", value::number(static_cast<double>(box.width)));
    obj.set("scrollHeight", value::number(static_cast<double>(box.height)));
}

namespace {

// `backgroundColor` -> `background-color`. The IDL name and the CSS name are
// different spellings of the same property, and the attribute the style engine
// parses wants the CSS one.
//
// THE CONVERSION MOVED to style/css/properties.hpp, where the property table
// is: it had a second copy inside `computed_style.cpp`'s `getPropertyValue`,
// and both of them got `-webkit-transform` wrong in the same way - the IDL name
// drops the prefix's leading dash, so a plain camel-to-hyphen loop produces
// `webkit-transform` and finds nothing.
using style::css::css_name_of;

// Whether a key on the declaration store is a DECLARATION rather than one of
// the methods sharing the object with them. A method is a callable and is
// skipped on that ground alone; `length`, `cssText` and the indexed properties
// are answered by the proxy and never stored, which is what keeps this test to
// one condition.
[[nodiscard]] bool is_declaration(const value & v) {
    return !v.is_nullish() && !v.is_callable();
}

// The declarations an object holds, as a `style` attribute. Serialising the
// whole object on every write is what keeps the two representations from
// drifting: there is one source of truth, the object, and the attribute is
// derived from it.
std::string style_attribute(script::object_object & held, context & cx) {
    std::string out;
    for (const auto & [name, v] : held.props) {
        // `setProperty` and friends live on the same object, and a CSS value is
        // never a function - without this the methods serialise themselves into
        // the attribute as `set-property: function;`.
        if (!is_declaration(v)) { continue; }
        const std::string text = cx.to_string(v);
        // Assigning "" REMOVES a declaration, which is how a page turns one
        // off - emitting `display: ;` instead would leave the old value in
        // place as far as the parser is concerned.
        if (text.empty()) { continue; }
        out += css_name_of(name);
        out += ": ";
        out += text;
        out += "; ";
    }
    return out;
}

// `cssText`: the same declarations, without the trailing space CSSOM does not
// ask for. A separate function from the one above because the ATTRIBUTE is a
// derived artefact the style engine re-parses and `cssText` is an answer to
// script, and the two have drifted before.
std::string css_text_of(script::object_object & held, context & cx) {
    std::string out;
    for (const auto & [name, v] : held.props) {
        if (!is_declaration(v)) { continue; }
        const std::string text = cx.to_string(v);
        if (text.empty()) { continue; }
        if (!out.empty()) { out += ' '; }
        out += css_name_of(name);
        out += ": ";
        out += text;
        out += ';';
    }
    return out;
}

// ONE WRITE THROUGH THE VALUE GRAMMAR. Every path into the declaration store -
// `el.style.color = x`, `setProperty`, `cssText`, and the seed from the
// element's own `style` attribute - goes through here, so there is exactly one
// answer to "is this valid" and exactly one canonical form.
//
// An INVALID value is a NO-OP, which is what CSSOM §6.7.2 says and what
// `test_invalid_value` measures: the test clears the property, sets the bad
// value, and asserts the read is `""`. Refusing the write is the whole test.
// Before this, `el.style` recorded whatever it was given and handed it back
// unchanged - `expected "" but got "round()"`, ~600 subtests of `css-values`.
// THE PRIORITY RIDES IN THE STORED STRING, and every read strips it. There is
// nowhere else for it to go: the store IS the declaration list, a JS object of
// name to value, and a parallel table keyed on the element would be a second
// thing to keep in step with the first. `important_suffix` is what the two ends
// agree on, `declared_value` takes it off and `declared_priority` reads it.
//
// It has to be kept at all. `style="width: 100px !important"` is ordinary CSS,
// and refusing the value would DROP the declaration - the inline width simply
// stops applying - which is a great deal worse than mis-reporting a priority.
constexpr std::string_view important_suffix = " !important";

[[nodiscard]] std::string_view declared_value(std::string_view stored) {
    return stored.ends_with(important_suffix)
               ? stored.substr(0, stored.size() - important_suffix.size())
               : stored;
}

[[nodiscard]] std::string_view declared_priority(std::string_view stored) {
    return stored.ends_with(important_suffix) ? std::string_view{"important"} : std::string_view{};
}

bool store_declaration(script::object_object & held, context & cx, const std::string & css_name,
                       std::string_view text, bool allow_important, bool force_important) {
    const style::css::value_check checked =
        style::css::check_declaration(css_name, text, allow_important);
    if (!checked.valid) {
        // An empty value REMOVES the declaration; anything else that fails to
        // parse leaves the old one exactly where it was.
        if (trim(text, html_whitespace).empty()) {
            held.erase(css_name);
            return true;
        }
        return false;
    }
    std::string stored = checked.serialized;
    if (checked.important || force_important) { stored += important_suffix; }
    held.set(css_name, cx.string(stored));
    return true;
}

// The declarations already in a `style` attribute, so a write through
// `el.style` extends what the author wrote rather than replacing it. The whole
// object is re-serialised on every write, so anything not read back here is
// LOST on the first assignment - which silently deleted the width and height an
// element was sized by.
void seed_declarations(script::object_object & held, context & cx, std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t colon = text.find(':', i);
        if (colon == std::string_view::npos) { break; }
        std::size_t end = text.find(';', colon);
        if (end == std::string_view::npos) { end = text.size(); }
        const std::string_view name = trim(text.substr(i, colon - i), html_whitespace);
        const std::string_view v = trim(text.substr(colon + 1, end - colon - 1), html_whitespace);
        // THROUGH THE SAME GRAMMAR as a write from script, so the object a page
        // reads back cannot disagree with the attribute it was built from - and
        // so an invalid declaration in the markup is dropped here rather than
        // surviving as a value no engine would compute.
        if (!name.empty()) { store_declaration(held, cx, ascii_lower_copy(name), v, true, false); }
        i = end + 1;
    }
}

// The tokens of a `class` attribute. Whitespace-separated, and a class list
// operation is defined in terms of them rather than of the string.
std::vector<std::string> class_tokens(std::string_view text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t start = text.find_first_not_of(" \t\n\r\f", i);
        if (start == std::string_view::npos) { break; }
        const std::size_t end = text.find_first_of(" \t\n\r\f", start);
        out.emplace_back(text.substr(start, end == std::string_view::npos ? end : end - start));
        i = end == std::string_view::npos ? text.size() : end;
    }
    return out;
}

// WHAT AN ATTRIBUTE MAY BE CALLED - and it is NOT the XML `Name` production,
// which is what this used to approximate.
//
// `setAttribute` must throw an InvalidCharacterError for a name that is not a
// valid one (DOM 4.9.1). This refused everything `Name` refuses, and
// `dom/nodes/productions.js` is blunt about how wrong that is:
//
//     var invalid_names = [""]
//     var valid_names = ["x", "X", ":", "a:0", "invalid^Name", "\\", "'",
//                        '"', "0", "0:a", ":a", "x:y:x", "~"]
//
// Thirteen names, twelve of which `Name` refuses and every one of which
// `attributes.html` and `Document-createAttribute.html` require to SUCCEED.
// Only the empty string throws. The rule the platform enforces is a
// SERIALISATION one: a name has to survive being written into a start tag and
// read back, and the HTML tokenizer's attribute name state ends a name on
// whitespace, `/`, `=` and `>` and on nothing else. There is no
// first-character rule at all - `"0"` and `":a"` are legal attribute names and
// illegal ELEMENT names, which is exactly the pair productions.js draws.
//
// THE SECOND COPY OF THIS RULE is `is_valid_attribute_name` in
// bindings/document.cpp, which createAttribute and createAttributeNS answer
// to. That file's comment records that the two disagreed and that reconciling
// them was this one's to do; they agree now. Two translation units' worth of a
// four-line rule rather than one shared helper because `core/algorithms.hpp`
// is for what three callers share and this has two, both of them bindings.
//
// BYTE-WISE ON PURPOSE, and exact rather than approximate: every character the
// rule names is ASCII, and no byte of a multi-byte UTF-8 sequence is. So no
// decoder, and no dependence on how the VM happens to store a string.
constexpr std::string_view attribute_name_breaks = "\t\n\f\r /=>";

[[nodiscard]] bool valid_attribute_name(std::string_view name) {
    // U+0000 is the one character the tokenizer cannot carry - it becomes
    // U+FFFD, so a name holding one does not read back as itself.
    return !name.empty() && name.find_first_of(attribute_name_breaks) == std::string_view::npos &&
           name.find('\0') == std::string_view::npos;
}

// The prefix and the local part of a qualified name, split at the FIRST colon.
// `a:b:c` is prefix `a` and local `b:c`, which is the DOM's split and not the
// XML QName production's - the two disagree and the DOM is what a page is
// measured against. Deliberately the same answers as `split_qualified` in
// bindings/document.cpp, and the same reason as above for there being two.
struct split_name {
    std::string_view prefix; // empty when there is no colon
    std::string_view local;
    bool has_colon = false;
};

[[nodiscard]] split_name split_attribute_name(std::string_view name) {
    const std::size_t colon = name.find(':');
    if (colon == std::string_view::npos) { return split_name{{}, name, false}; }
    return split_name{name.substr(0, colon), name.substr(colon + 1), true};
}

// The namespaces this file names by URI. Spelled out rather than derived,
// because one wrong character makes a NamespaceError fire on the valid case and
// not on the invalid one, and nothing about the failure says so.
constexpr std::string_view xml_namespace = "http://www.w3.org/XML/1998/namespace";
constexpr std::string_view xmlns_namespace = "http://www.w3.org/2000/xmlns/";
constexpr std::string_view html_namespace = "http://www.w3.org/1999/xhtml";
constexpr std::string_view svg_namespace = "http://www.w3.org/2000/svg";
constexpr std::string_view mathml_namespace = "http://www.w3.org/1998/Math/MathML";

// `data-foo-bar` -> `fooBar`. HTML's dataset mangling in the direction that
// decides which properties EXIST: the supported property names of a
// DOMStringMap are computed from the attributes, never from the key a page
// asks about, which is why `el.dataset['-foo']` is undefined on an element
// carrying `data--foo` - that attribute's name is `Foo`.
//
// A `-` followed by an ASCII LOWERCASE letter becomes that letter uppercased;
// everything else is carried across untouched, including a `-` at the end and a
// `-` in front of anything that is not a lowercase letter. False for a name
// that is not a dataset attribute at all: one without the prefix, or one
// carrying an ASCII uppercase letter - which no attribute of an HTML element
// can have and one of an SVG element can.
[[nodiscard]] bool dataset_name_of(std::string_view attribute_name, std::string & out) {
    if (!attribute_name.starts_with("data-")) { return false; }
    const std::string_view rest = attribute_name.substr(5);
    out.clear();
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] >= 'A' && rest[i] <= 'Z') { return false; }
        if (rest[i] == '-' && i + 1 < rest.size() && rest[i + 1] >= 'a' && rest[i + 1] <= 'z') {
            out.push_back(static_cast<char>(rest[i + 1] - 'a' + 'A'));
            ++i;
            continue;
        }
        out.push_back(rest[i]);
    }
    return true;
}

// Why a write can be refused. Two DIFFERENT exceptions, and the corpus checks
// both by name: a key naming an attribute that could never map back to it is a
// SyntaxError, and one whose attribute name could not be written into a start
// tag is an InvalidCharacterError.
enum class dataset_fault : std::uint8_t {
    none,
    syntax,
    character
};

// ...and `fooBar` -> `data-foo-bar`, which is the other direction and NOT the
// inverse. That is the whole reason both exist: `data--foo` reads back as
// `Foo`, so `-foo` names nothing on the way in, and letting it name
// `data--foo` on the way out would make one attribute answer to two keys.
[[nodiscard]] dataset_fault dataset_attribute_of(std::string_view idl, std::string & out) {
    out = "data-";
    for (std::size_t i = 0; i < idl.size(); ++i) {
        if (idl[i] == '-' && i + 1 < idl.size() && idl[i + 1] >= 'a' && idl[i + 1] <= 'z') {
            return dataset_fault::syntax;
        }
        if (idl[i] >= 'A' && idl[i] <= 'Z') {
            out.push_back('-');
            out.push_back(static_cast<char>(idl[i] - 'A' + 'a'));
            continue;
        }
        out.push_back(idl[i]);
    }
    return valid_attribute_name(out) ? dataset_fault::none : dataset_fault::character;
}

// A NULLABLE DOMString argument. `null` and `undefined` are both the null
// namespace, and so is the empty string - `attributes.html`'s "null and the
// empty string should result in a null namespace" is that sentence as a test.
[[nodiscard]] std::string namespace_argument(context & cx, std::span<value> args, std::size_t i) {
    const value given = arg(args, i);
    return given.is_nullish() ? std::string{} : cx.to_string(given);
}

// DOES THE REFLECTION TABLE ALREADY ANSWER `width` FOR THIS TAG? Declared here
// and defined with the table itself, which is the only place that knows. The
// wrapper installs an OWN `width`/`height` accessor pair on an element carrying
// either attribute, and an own property shadows a prototype one - so a row for
// `<td width>` would be dead on a parsed <td> and live on a created one, which
// is two answers to one question. See the note where it is called.
[[nodiscard]] bool interface_reflects_size(std::string_view tag);

} // namespace

// DOM 4.2.3, "ensure pre-insertion validity". Every one of these checks stands
// in front of a `assert_throws_dom` in `dom/nodes/Node-insertBefore.html`,
// `Node-appendChild.html` and `Node-removeChild.html`, and the FIRST one is the
// reason this is not merely conformance work: appending a node to its own
// descendant built a cycle, and every tree walk in this file has a depth cap
// precisely because nothing stopped one being made.
bool dom_bindings::pre_insert_valid(context & cx, node_id parent, node_id child, value node_arg,
                                    value ref_arg) {
    if (!child) {
        // Not a Node at all. WebIDL reports a failed conversion as a TypeError
        // rather than a DOMException, which is the one case in these steps that
        // is not a DOMException.
        (void)node_arg;
        cx.throw_error("TypeError", "the argument is not a Node");
        return false;
    }
    if (!parent) {
        cx.throw_error("TypeError", "the receiver is not a Node");
        return false;
    }
    const auto txn = doc_->read();
    // 1. "If parent is not a Document, DocumentFragment, or Element node, throw
    //    a HierarchyRequestError." A text node has no children to insert into.
    const node_kind parent_kind = txn.kind(parent).value_or(node_kind::element);
    if (parent_kind != node_kind::document && parent_kind != node_kind::document_fragment &&
        parent_kind != node_kind::element) {
        throw_dom_exception(cx, "HierarchyRequestError", "the parent cannot have children");
        return false;
    }
    // 2. "If node is a host-including inclusive ancestor of parent, throw a
    //    HierarchyRequestError." The cycle case, and the one with teeth.
    for (node_id at = parent; at; at = txn.parent(at)) {
        if (at != child) { continue; }
        throw_dom_exception(cx, "HierarchyRequestError",
                            "the node is an ancestor of the parent it would go into");
        return false;
    }
    // 3. "If child is non-null and its parent is not parent, throw a
    //    NotFoundError."
    if (!ref_arg.is_nullish()) {
        const node_id before = handle_of(ref_arg);
        if (!before) {
            cx.throw_error("TypeError", "the reference node is not a Node");
            return false;
        }
        if (txn.parent(before) != parent) {
            throw_dom_exception(cx, "NotFoundError",
                                "the reference node is not a child of the parent");
            return false;
        }
    }
    // 4. "If node is not a DocumentFragment, DocumentType, Element, or
    //    CharacterData node, throw a HierarchyRequestError." A Document is the
    //    one this engine can produce and must refuse.
    if (txn.kind(child).value_or(node_kind::element) == node_kind::document) {
        throw_dom_exception(cx, "HierarchyRequestError", "a Document cannot be inserted");
        return false;
    }
    return true;
}

// --- ATTRIBUTES AS NODES: Attr, and the NamedNodeMap over them --------------

// A QUALIFIED NAME, AS THIS ELEMENT WOULD HAVE STORED IT. "If the element is in
// the HTML namespace and its node document is an HTML document, set
// qualifiedName to qualifiedName in ASCII lowercase" - DOM 4.9, and it is the
// whole difference between `getAttribute` and `getAttributeNS`, which is
// case-SENSITIVE and has no such rule.
//
// IT USED TO BE UNCONDITIONAL, and that was wrong in a way an SVG page could
// see: `svg.setAttribute("viewBox", ...)` interned `viewbox`, which is a name
// the rasteriser and the style engine never look for - so the whole of
// `<svg>`'s capitalised attribute surface was unreachable from script, in the
// one namespace the tokenizer goes out of its way to preserve the case of.
// `attributes.html`'s "Only lowercase attributes are returned on HTML
// elements" is the other half of the same rule.
atom dom_bindings::attribute_key(const read_txn & txn, node_id id,
                                 std::string_view qualified) const {
    return txn.element_ns(id) == node_ns::html ? atoms_->intern_lower(qualified)
                                               : atoms_->intern(qualified);
}

// ONE Attr, AND IT IS LIVE IN BOTH DIRECTIONS. `attr.value` reads the element's
// attribute at the moment it is asked and `attr.value = "x"` writes through to
// it - which is `attributes.html`'s "Attribute values should not be parsed",
// two lines of which write an Attr and then read `el.getAttribute`. Three data
// properties would have been three strings captured when the object was made.
//
// IT IS KEYED ON (namespace, local name), never on a position: DOM 4.9 says
// that pair is what an attribute IS, and it survives every mutation short of
// removing this attribute. An index does not - a `removeAttribute` moves every
// attribute after it down one.
//
// `value`, `nodeValue` and `textContent` are ONE string behind three
// spellings, because on an Attr that is what they are. They are exactly what
// `dom/nodes/attributes.js`'s `attr_is` reads, and it reads all nine of these
// properties on every case of three files.
//
// AN EMPTY `owner` MEANS DETACHED, and that is the whole of the second half of
// this function. An Attr that has been REMOVED still exists - `removeNamedItem`
// and `removeAttributeNode` both HAND IT BACK, which is how a page moves an
// attribute from one element to another - and DOM 4.9.2 leaves its value, name
// and namespace exactly as they were at the moment of removal while setting its
// ownerElement to null. Reading through to the element it used to name answers
// "" for every one of them, because the element no longer has the attribute:
//
//     var gone = e.attributes.removeNamedItem('a');
//     gone.value                                  // "1" in a browser, "" here
//
// So a detached Attr carries its OWN value as three ordinary data properties -
// which also gives `gone.value = "x"` the right meaning, a write to a node that
// is not in any element rather than a write to an element that has moved on.
value dom_bindings::attribute_object(context & cx, node_id owner, const attribute & held) {
    const std::string qualified{atoms_->text(held.name)};
    const std::string ns{atoms_->text(held.ns)};
    const std::string local{attribute_local_name(*atoms_, held)};
    const std::string prefix{attribute_prefix(*atoms_, held)};
    const atom name = held.name;
    const atom uri = held.ns;

    auto * attr = static_cast<script::object_object *>(cx.make_object().as_heap());
    for (const char * spelling : {"value", "nodeValue", "textContent"}) {
        const std::string property{spelling};
        if (!owner) {
            attr->set(property, cx.string(held.value));
            continue;
        }
        attr->define_accessor(
            property,
            value::object(cx.allocate<script::native_object>(
                property,
                [this, owner, ns, local](context & c, std::span<value>) {
                    const auto txn = doc_->read();
                    const attribute * found = txn.find_attribute_ns(owner, ns, local);
                    return c.string(found == nullptr ? std::string{} : found->value);
                })),
            value::object(cx.allocate<script::native_object>(
                property, [this, owner, name, uri](context & c, std::span<value> a) {
                    (void)doc_->set_attribute_ns(owner, uri, name, arg_string(c, a, 0));
                    mutated();
                    return value::undefined();
                })));
    }
    attr->set("name", cx.string(qualified));
    attr->set("nodeName", cx.string(qualified));
    attr->set("localName", cx.string(local));
    attr->set("prefix", prefix.empty() ? value::null() : cx.string(prefix));
    attr->set("namespaceURI", ns.empty() ? value::null() : cx.string(ns));
    attr->set("nodeType", value::number(2));
    // TRUE for every Attr since DOM4 deleted the other answer, and `attr_is`
    // asserts it on every case it runs.
    attr->set("specified", value::boolean(true));
    // THE WRAPPER THAT ALREADY EXISTS, which is how `attributes_are`'s
    // `assert_equals(el.attributes[i].ownerElement, el)` can be an identity
    // comparison at all. `wrap` is the fallback rather than the path: it
    // REFRESHES the whole element, and going through it once per attribute
    // would re-measure the box for an answer already in hand.
    const value already = value_of_wrapper(owner);
    attr->set("ownerElement", already.is_object() ? already : wrap(cx, owner));
    if (const value proto = interface_prototype("Attr"); proto.is_object()) {
        attr->prototype = proto;
    }
    return value::object(attr);
}

// `element.attributes`, REFILLED IN PLACE rather than rebuilt. The map keeps
// its identity - a page may hold on to one, and `el.attributes ===
// el.attributes` is true in a browser - so this rewrites its indexed
// properties and its `length`.
//
// AN ARRAY-LIKE OBJECT RATHER THAN A PROXY, and that is a measured choice
// rather than a shortcut. `for (const a of el.attributes)` is how p5's XML
// module walks one and `[].concat(...el.attributes)` is Bootstrap's spelling;
// both go through `context::iterable_values`, which materialises any object
// carrying a numeric `length` and indexed properties and yields NOTHING AT ALL
// for a proxy (vm/call.cpp, and bytecode_opcodes.def's note on `iterable`). A
// proxy would have been live and uniterable, which is the worse half of each.
void dom_bindings::refresh_attribute_map(context & cx, script::object_object & map, node_id id) {
    // COPIED OUT BEFORE ANYTHING ELSE RUNS. `attribute_object` calls `wrap`,
    // which opens a read_txn of its own, and a read nested inside another read
    // is a shape nothing else in these bindings has.
    std::vector<attribute> held;
    {
        const auto txn = doc_->read();
        const std::span<const attribute> current = txn.attributes(id);
        held.assign(current.begin(), current.end());
    }
    for (std::size_t i = 0; i < held.size(); ++i) {
        map.set(std::to_string(i), attribute_object(cx, id, held[i]));
    }
    // THE INDICES THAT WENT AWAY. A removed attribute leaves its old index
    // behind, and an index past `length` that still answers is how
    // `assert_array_equals` reports a length it was never given.
    for (std::size_t i = held.size(); map.find(std::to_string(i)) != nullptr; ++i) {
        (void)map.erase(std::to_string(i));
    }
    map.set("length", value::number(static_cast<double>(held.size())));
    // THE NAMED PROPERTIES. `element.attributes.x` is the attribute called `x`
    // - a NamedNodeMap is a legacy platform object with a named property getter
    // and `attributes-namednodemap.html` is five subtests of exactly this. The
    // indexed half was here from the start and this half was not, so a page
    // could reach an attribute by position and not by name.
    //
    // NEVER OVER A METHOD OR OVER `length`, which is the rest of that file:
    // `setAttributeNS("foo", "setNamedItem", v)` must leave
    // `attributes.setNamedItem` a function and `setAttributeNS("foo", "length",
    // v)` must leave `attributes.length` the count. A named property that
    // shadowed either would be an attribute a page can write breaking the map
    // it was written into.
    {
        // "Is this key an array index" is object_object's own question - the
        // one that decides property ORDER - so it is asked with its function
        // rather than with a second spelling that could disagree.
        const auto is_index = [](std::string_view key) {
            std::uint32_t at = 0;
            return script::object_object::array_index_key(key, at);
        };
        std::vector<std::string> stale;
        for (const auto & [key, current] : map.props) {
            if (key == "length" || is_index(key)) { continue; }
            // A METHOD STAYS. Everything else on this object is a named
            // property this function put there on an earlier refresh, and it
            // goes: an attribute that has been removed must stop answering.
            if (current.is_callable()) { continue; }
            stale.push_back(key);
        }
        for (const std::string & key : stale) { (void)map.erase(key); }
        for (std::size_t i = 0; i < held.size(); ++i) {
            const std::string qualified{atoms_->text(held[i].name)};
            if (qualified == "length" || is_index(qualified)) { continue; }
            // THE SAME Attr OBJECT THE INDEX HOLDS, not a second one. Sharing
            // is both cheaper - a wrapper per attribute per read rather than
            // two - and RIGHT: `el.attributes[0] === el.attributes.x` is true
            // in a browser, an Attr being one node under two ways of reaching
            // it. It is read back out of the map rather than kept in a C++
            // local because the map is what roots it.
            //
            // ALREADY TAKEN means leave it alone, which covers both the methods
            // and the case DOM's named getter is actually about: two attributes
            // may share a qualified name in different namespaces, and the FIRST
            // is the one the name answers with.
            if (map.find(qualified) != nullptr) { continue; }
            const value * indexed = map.find(std::to_string(i));
            if (indexed == nullptr) { continue; }
            map.set(qualified, *indexed);
        }
    }
    if (!map.prototype.is_object()) {
        // LATE, because the interfaces are built lazily: the first two element
        // wrappers exist before `EventTarget` does. See ensure_dom_interfaces.
        if (const value proto = interface_prototype("NamedNodeMap"); proto.is_object()) {
            map.prototype = proto;
        }
    }
}

// "VALIDATE AND EXTRACT", DOM 4.9, shared by `setAttributeNS` and the two
// `setNamedItemNS` spellings. Answers false HAVING ALREADY THROWN, which is the
// shape `pre_insert_valid` above uses and for the same reason.
//
// THE ORDER OF THE TWO HALVES IS PART OF THE ANSWER. The SHAPE of the name is
// decided before the namespace is looked at, so `setAttributeNS(XMLNS, "", v)`
// is an InvalidCharacterError and not the NamespaceError its namespace would
// otherwise earn. Both orderings throw; only one throws what the suite asserts.
//
// A prefix is checked for being non-empty and writable and NOTHING ELSE - it is
// the LOCAL name that has to be a name, and the prefix is only ever a label in
// front of it. Deliberately the same rule, in the same order, as
// createAttributeNS in bindings/document.cpp.
bool dom_bindings::validate_and_extract(context & cx, std::string_view where,
                                        const std::string & ns, const std::string & qualified) {
    const split_name split = split_attribute_name(qualified);
    const bool prefix_writable =
        !split.prefix.empty() &&
        split.prefix.find_first_of(attribute_name_breaks) == std::string_view::npos;
    if ((split.has_colon && !prefix_writable) || !valid_attribute_name(split.local)) {
        throw_dom_exception(cx, "InvalidCharacterError",
                            std::string{where} + ": '" + qualified +
                                "' is not a qualified attribute name");
        return false;
    }
    const auto fail = [&](const std::string & why) {
        throw_dom_exception(cx, "NamespaceError", std::string{where} + ": " + why);
        return false;
    };
    if (split.has_colon && ns.empty()) { return fail("a prefix needs a namespace"); }
    if (split.prefix == "xml" && ns != xml_namespace) {
        return fail("the xml prefix belongs to the XML namespace");
    }
    if ((qualified == "xmlns" || split.prefix == "xmlns") && ns != xmlns_namespace) {
        return fail("xmlns belongs to the XMLNS namespace");
    }
    if (ns == xmlns_namespace && qualified != "xmlns" && split.prefix != "xmlns") {
        return fail("the XMLNS namespace is only for xmlns");
    }
    return true;
}

void dom_bindings::install_element_views(context & cx, script::object_object & obj, node_id id) {
    // `<style>.sheet` and `<link>.sheet` - the LinkStyle mixin. Here rather than
    // in bindings/stylesheets.cpp for the same reason `style` is here: it is a
    // view onto ONE element and it has to be installed as its wrapper is made.
    install_sheet_property(cx, obj, id);

    // --- element.attributes
    //
    // THE MAP IS BUILT ONCE and refilled by the accessor, so it keeps its
    // identity across reads while its contents are read out of the document
    // every time. See refresh_attribute_map for why it is an array-like object
    // and not a proxy.
    auto * map = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto map_method = [&](std::string name, script::native_fn fn) {
        map->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // ONE ATTRIBUTE, BY WHICHEVER OF THE TWO QUESTIONS WAS ASKED, copied out of
    // the read before anything that could open another one runs.
    const auto found_by_name = [this, id](std::string_view qualified) {
        const auto txn = doc_->read();
        const attribute * held = txn.find_attribute(id, attribute_key(txn, id, qualified));
        return held == nullptr ? std::optional<attribute>{} : std::optional<attribute>{*held};
    };
    const auto found_by_pair = [this, id](std::string_view ns, std::string_view local) {
        const auto txn = doc_->read();
        const attribute * held = txn.find_attribute_ns(id, ns, local);
        return held == nullptr ? std::optional<attribute>{} : std::optional<attribute>{*held};
    };
    map_method("item", [this, id](context & c, std::span<value> a) {
        std::vector<attribute> held;
        {
            const auto txn = doc_->read();
            const std::span<const attribute> current = txn.attributes(id);
            held.assign(current.begin(), current.end());
        }
        const double at = arg_number(a, 0);
        if (!(at >= 0) || at >= static_cast<double>(held.size())) { return value::null(); }
        return attribute_object(c, id, held[static_cast<std::size_t>(at)]);
    });
    map_method("getNamedItem", [this, id, found_by_name](context & c, std::span<value> a) {
        const std::optional<attribute> held = found_by_name(arg_string(c, a, 0));
        return held ? attribute_object(c, id, *held) : value::null();
    });
    map_method("getNamedItemNS", [this, id, found_by_pair](context & c, std::span<value> a) {
        const std::optional<attribute> held =
            found_by_pair(namespace_argument(c, a, 0), arg_string(c, a, 1));
        return held ? attribute_object(c, id, *held) : value::null();
    });
    // `setNamedItem` and `setNamedItemNS` ARE THE SAME OPERATION - DOM 4.9.2
    // defines both as "set an attribute", which is keyed on the (namespace,
    // local name) pair whichever spelling was used. What an Attr carries is
    // read off the OBJECT rather than off a C++ type, so an Attr from
    // `document.createAttribute` - which bindings/document.cpp makes and which
    // is not one of these - works here too.
    const auto set_named = [this, id, found_by_pair](context & c, std::span<value> a) {
        const value given = arg(a, 0);
        if (!given.is_object()) {
            c.throw_error("TypeError", "setNamedItem: the argument is not an Attr");
            return value::null();
        }
        const value ns_property = c.lookup_property(given, "namespaceURI");
        const std::string ns = ns_property.is_nullish() ? std::string{} : c.to_string(ns_property);
        const std::string qualified = c.to_string(c.lookup_property(given, "name"));
        const value text = c.lookup_property(given, "value");
        const split_name split = split_attribute_name(qualified);
        const std::string_view local = ns.empty() ? std::string_view{qualified} : split.local;
        // THE ONE IT REPLACES IS THE RETURN VALUE, and it has to be read before
        // the write: "return oldAttr" is how a page takes an attribute off one
        // element and puts it on another.
        const std::optional<attribute> replaced = found_by_pair(ns, local);
        (void)doc_->set_attribute_ns(id, atoms_->intern(ns), atoms_->intern(qualified),
                                     text.is_undefined() ? std::string{} : c.to_string(text));
        mutated();
        return replaced ? attribute_object(c, id, *replaced) : value::null();
    };
    map_method("setNamedItem", set_named);
    map_method("setNamedItemNS", set_named);
    // ...and removing one THROWS when there is nothing to remove, which is the
    // half that is easy to miss: "if attr is null, throw a NotFoundError".
    map_method("removeNamedItem", [this, id, found_by_name](context & c, std::span<value> a) {
        const std::string qualified = arg_string(c, a, 0);
        const std::optional<attribute> held = found_by_name(qualified);
        if (!held) {
            throw_dom_exception(c, "NotFoundError",
                                "removeNamedItem: no attribute called '" + qualified + "'");
            return value::null();
        }
        // DETACHED, and made AFTER the removal so it cannot be read live: the
        // Attr this hands back keeps the value it had, and no element.
        (void)doc_->remove_attribute(id, held->name);
        mutated();
        return attribute_object(c, node_id{}, *held);
    });
    map_method("removeNamedItemNS", [this, id, found_by_pair](context & c, std::span<value> a) {
        const std::string ns = namespace_argument(c, a, 0);
        const std::string local = arg_string(c, a, 1);
        const std::optional<attribute> held = found_by_pair(ns, local);
        if (!held) {
            throw_dom_exception(c, "NotFoundError",
                                "removeNamedItemNS: no attribute called '" + local + "'");
            return value::null();
        }
        (void)doc_->remove_attribute_ns(id, ns, local);
        mutated();
        return attribute_object(c, node_id{}, *held);
    });
    {
        // THE MAP IS ROOTED THROUGH THE GETTER. A C++ lambda's captures are
        // invisible to a precise collector, so the raw pointer this closes over
        // would not keep the object alive - `retained` is the channel that
        // does, and the getter itself is reachable from the wrapper's accessor
        // table. See native_object::retained.
        const value map_value = value::object(map);
        auto * getter = cx.allocate<script::native_object>(
            "attributes", [this, map, id](context & c, std::span<value>) {
                refresh_attribute_map(c, *map, id);
                return value::object(map);
            });
        getter->retained.push_back(map_value);
        obj.define_accessor("attributes", value::object(getter), value::undefined());
    }

    // --- element.style
    //
    // A PROXY, because a style object has no fixed set of properties: a page
    // may write any CSS property and the write has to reach the document. The
    // proxy's target holds the declarations and the `set` trap re-serialises it
    // into the element's `style` attribute - which the style engine already
    // parses, so there is no second representation to keep in step.
    //
    // BOTH traps canonicalise the name, because `backgroundColor` and
    // `background-color` are two spellings of ONE property. Storing them as
    // written put both in the attribute and made a read miss a write.
    auto * held = static_cast<script::object_object *>(cx.make_object().as_heap());
    // AND IT IS RE-SEEDED, not seeded once. The store was filled from the
    // `style` attribute at wrapper construction and never again, so
    // `el.setAttribute("style", "color: red")` - which writes the attribute
    // directly and never touches this proxy - left `el.style.color` reading the
    // empty store. `css-style-attr-decl-block.html` names the defect outright
    // ("Changes to style attribute should reflect on CSS declaration block")
    // and `serialize-values.html` is 697 subtests of it: it does createElement,
    // setAttribute("style", …) and then reads the IDL attribute back.
    //
    // The last text SEEN rather than the document version, because a write
    // through this proxy sets the attribute itself and must not then re-seed
    // from what it just wrote - `seen` is updated to the serialisation instead,
    // so a write costs nothing and only a change from OUTSIDE re-reads.
    const auto seen = std::make_shared<std::string>();
    const auto reseed = [this, id, seen](context & c, script::object_object & store) {
        std::string now;
        {
            const auto txn = doc_->read();
            now = std::string{txn.attribute_value(id, atoms_->intern("style"))};
        }
        if (now == *seen) { return; }
        *seen = now;
        // The METHODS stay: `setProperty` and its four siblings live on this
        // same object, and erasing everything would take them with it.
        std::vector<std::string> declared;
        for (const auto & [key, v] : store.props) {
            if (is_declaration(v)) { declared.push_back(key); }
        }
        for (const std::string & key : declared) { store.erase(key); }
        seed_declarations(store, c, now);
    };
    // The write side of the same bookkeeping: after this proxy has written the
    // attribute, what is in it is what we put there.
    const auto wrote = [this, id, seen](context & c, script::object_object & store) {
        *seen = style_attribute(store, c);
        (void)doc_->set_attribute(id, atoms_->intern("style"), *seen);
    };
    reseed(cx, *held);
    const value target = value::object(held);
    auto * handler = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto trap = [&](std::string name, script::native_fn fn) {
        handler->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // `length`, `cssText` and the indexed properties are COMPUTED here rather
    // than stored. Storing them would put `length: 5` in the element's style
    // attribute - the store IS the declaration list, and anything in it that is
    // not a declaration has to be filtered back out by every reader.
    trap("get", [reseed](context & c, std::span<value> args) {
        if (args.size() < 2 || !args[0].is_object()) { return value::undefined(); }
        auto * store = static_cast<script::object_object *>(args[0].as_heap());
        reseed(c, *store);
        const std::string name = c.to_string(args[1]);
        if (name == "length") {
            double count = 0;
            for (const auto & [key, v] : store->props) {
                if (is_declaration(v)) { count += 1; }
            }
            return value::number(count);
        }
        if (name == "cssText") { return c.string(css_text_of(*store, c)); }
        // AN INDEX NAMES A PROPERTY, not a value: CSSOM §6.7.1 makes the
        // indexed properties of a CSSStyleDeclaration its property NAMES, in
        // declaration order, which is what `[...el.style]` and every
        // `for (const p of el.style)` in the corpus iterates.
        if (!name.empty() && name.find_first_not_of("0123456789") == std::string::npos) {
            // COUNTED DOWN rather than converted. `std::stoull` throws on an
            // index a page can write in one keystroke (`el.style[1e30]` arrives
            // here as twenty digits), and an uncaught std::out_of_range out of a
            // native is a terminate() rather than a TypeError.
            std::size_t want = 0;
            for (const char digit : name) {
                if (want > store->props.size()) { return value::undefined(); }
                want = want * 10 + static_cast<std::size_t>(digit - '0');
            }
            for (const auto & [key, v] : store->props) {
                if (!is_declaration(v)) { continue; }
                if (want-- == 0) { return c.string(key); }
            }
            return value::undefined();
        }
        // The raw name first: that is where setProperty and getPropertyValue
        // live, and canonicalising them turns them into `set-property`.
        if (const value * found = store->find(name)) {
            // A METHOD is handed back as it is; a DECLARATION loses its
            // priority, because `el.style.width` is a value and never
            // "100px !important".
            if (found->is_callable()) { return *found; }
            return c.string(std::string{declared_value(c.to_string(*found))});
        }
        const std::string css = css_name_of(name);
        const value * found = store->find(css);
        if (found == nullptr) {
            // A SUPPORTED PROPERTY THAT IS NOT SET IS "", NOT undefined.
            // CSSOM 6.7.2 gives every property in the IDL a getter that
            // returns the empty string when the declaration block has none,
            // and `serialize-values.html` reads exactly that for the ones it
            // could not set. `undefined` is reserved for a name that is not a
            // property at all - `el.style.toString`, `el.style.constructor` -
            // because answering "" there would break every ordinary lookup.
            if (style::css::find_property(css) != nullptr) { return c.string(""); }
            return value::undefined();
        }
        return c.string(std::string{declared_value(c.to_string(*found))});
    });
    trap("set", [this, reseed, wrote](context & c, std::span<value> args) {
        if (args.size() < 3 || !args[0].is_object()) { return value::boolean(false); }
        auto * store = static_cast<script::object_object *>(args[0].as_heap());
        reseed(c, *store);
        const std::string name = c.to_string(args[1]);
        if (name == "cssText") {
            // A WHOLE-BLOCK REPLACEMENT, not a merge: `el.style.cssText = "…"`
            // drops every declaration the element had. Erasing in place would
            // leave the methods behind, which is exactly what has to survive.
            std::vector<std::string> declared;
            for (const auto & [key, v] : store->props) {
                if (is_declaration(v)) { declared.push_back(key); }
            }
            for (const std::string & key : declared) { store->erase(key); }
            seed_declarations(*store, c, c.to_string(args[2]));
        } else {
            // The IDL spelling, canonicalised, and the value through the
            // grammar. A refusal is silent - CSSOM says an unparseable value
            // leaves the declaration alone, and a throw here would break every
            // page that sets a property this engine has not implemented.
            (void)store_declaration(*store, c, css_name_of(name), c.to_string(args[2]), false,
                                    false);
        }
        wrote(c, *store);
        mutated();
        return value::boolean(true);
    });
    const value style_view =
        value::object(cx.allocate<script::proxy_object>(target, value::object(handler)));

    // `setProperty` / `getPropertyValue` / `removeProperty` take the CSS
    // spelling rather than the IDL one, so they are the only way to reach a
    // custom property (`--x`) - which no identifier can name.
    const auto declaration_method = [&](std::string name, script::native_fn fn) {
        held->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // A NON-CUSTOM PROPERTY NAME IS LOWERCASED, a custom one is not: `--X` and
    // `--x` are two different properties and `COLOR` and `color` are one.
    const auto asked_name = [](context & c, std::span<value> args) {
        const std::string given = arg_string(c, args, 0);
        return given.starts_with("--") ? given : ascii_lower_copy(given);
    };
    declaration_method(
        "setProperty", [this, held, asked_name, reseed, wrote](context & c, std::span<value> args) {
            // "If priority is not the empty string and is not an ASCII
            // case-insensitive match for 'important', return." - CSSOM 6.7.2.
            // The VALUE may not carry one; the third argument is the only way
            // a page can ask for it.
            const std::string priority = args.size() > 2 ? c.to_string(args[2]) : std::string{};
            if (!priority.empty() && !ascii_iequals(priority, "important")) {
                return value::undefined();
            }
            reseed(c, *held);
            (void)store_declaration(*held, c, asked_name(c, args),
                                    args.size() > 1 ? c.to_string(args[1]) : std::string{}, false,
                                    !priority.empty());
            wrote(c, *held);
            mutated();
            return value::undefined();
        });
    // ...and it ANSWERS with the value it removed, which is what CSSOM says and
    // what a page toggling a property reads to put it back.
    declaration_method("removeProperty", [this, held, asked_name, reseed,
                                          wrote](context & c, std::span<value> args) {
        reseed(c, *held);
        const std::string name = asked_name(c, args);
        const value * found = held->find(name);
        const std::string was =
            found == nullptr ? std::string{} : std::string{declared_value(c.to_string(*found))};
        held->erase(name);
        wrote(c, *held);
        mutated();
        return c.string(was);
    });
    declaration_method("getPropertyValue",
                       [held, asked_name, reseed](context & c, std::span<value> args) {
                           reseed(c, *held);
                           const value * found = held->find(asked_name(c, args));
                           if (found == nullptr) { return c.string(""); }
                           return c.string(std::string{declared_value(c.to_string(*found))});
                       });
    declaration_method("getPropertyPriority",
                       [held, asked_name, reseed](context & c, std::span<value> args) {
                           reseed(c, *held);
                           const value * found = held->find(asked_name(c, args));
                           if (found == nullptr) { return c.string(""); }
                           return c.string(std::string{declared_priority(c.to_string(*found))});
                       });
    declaration_method("item", [held, reseed](context & c, std::span<value> args) {
        reseed(c, *held);
        double want = args.empty() ? 0 : context::to_number(args[0]);
        if (!(want >= 0)) { return c.string(""); }
        for (const auto & [key, v] : held->props) {
            if (!is_declaration(v)) { continue; }
            if (want < 1) { return c.string(key); }
            want -= 1;
        }
        return c.string("");
    });
    obj.set("style", style_view);

    // --- the reflected attributes
    //
    // NOT HERE ANY MORE, and that is the point. `id`, `className`, `href`,
    // `download`, `target`, `rel`, `alt`, `title`, `name`, `placeholder`,
    // `type` and `htmlFor` used to be twelve accessors installed on EVERY
    // wrapper, which was wrong in both directions: `div.href` existed and
    // `input.maxLength` did not, and none of the twelve knew its own type - so
    // `details.open` was a string and `td.colSpan` was nothing at all.
    //
    // They live on the INTERFACE PROTOTYPES now, out of one table, which is
    // what the specification means by reflection being defined per interface.
    // See install_dom_interfaces at the bottom of this file. What is still
    // installed per element below it is the handful that CANNOT be a table row:
    // a control's `value` and `checked`, which track what the user typed rather
    // than an attribute, an <img>'s `src`, which has to start a load, and a
    // <canvas>'s `width` and `height`, which resize a drawing buffer.

    // `innerHTML` and `textContent` are ACCESSORS over the tree, not properties
    // on the wrapper. As properties, assigning markup stored a string, built no
    // nodes, rendered nothing and reported nothing - and reading one back gave
    // whatever the page last wrote rather than what the DOM actually holds.
    const auto tree_property = [&](std::string property, script::native_fn read,
                                   script::native_fn write) {
        obj.define_accessor(
            property, value::object(cx.allocate<script::native_object>(property, std::move(read))),
            value::object(cx.allocate<script::native_object>(property, std::move(write))));
    };
    tree_property(
        "innerHTML", [this, id](context & c, std::span<value>) { return c.string(inner_html(id)); },
        [this, id](context & c, std::span<value> a) {
            set_inner_html(id, arg_string(c, a, 0));
            return value::undefined();
        });
    // `data` AND `nodeValue` - the text a Text or Comment node holds, which is
    // the one thing those two nodes are FOR. `childNodes` has handed them out
    // all along and there was no way to read what was in one: `.data` was
    // undefined, `.nodeValue` was undefined, and the only spelling that worked
    // was textContent, which is the same answer by accident and a different
    // question. Both are accessors, both write through, and on an element they
    // are null - which is what the DOM says and is not the same as absent.
    for (const char * spelling : {"data", "nodeValue"}) {
        tree_property(
            spelling,
            [this, id](context & c, std::span<value>) {
                const auto txn = doc_->read();
                const node_kind kind = txn.kind(id).value_or(node_kind::element);
                if (kind != node_kind::text && kind != node_kind::comment) { return value::null(); }
                return c.string(std::string{txn.text(id)});
            },
            [this, id](context & c, std::span<value> a) {
                const auto kind = doc_->read().kind(id).value_or(node_kind::element);
                if (kind == node_kind::text || kind == node_kind::comment) {
                    // NULL IS THE EMPTY STRING, not "null". `data` is
                    // [LegacyNullToEmptyString] and `nodeValue` is a nullable
                    // DOMString whose null means "no value"; both land on "",
                    // and ToString would have written the four letters instead.
                    // `CharacterData-data.html` asserts `.data = null` leaves a
                    // node of length 0 - and the very next case asserts
                    // `.data = undefined` writes "undefined", so this is a test
                    // for null ALONE and not for nullish.
                    const value given = arg(a, 0);
                    (void)doc_->set_text(id, given.is_null() ? std::string{} : arg_string(c, a, 0));
                    mutated();
                }
                return value::undefined();
            });
    }
    tree_property(
        "textContent",
        [this, id](context & c, std::span<value>) { return c.string(text_content(id)); },
        [this, id](context & c, std::span<value> a) {
            // Text, never markup: that is the whole point of the property, and
            // the reason a page reaches for it instead of innerHTML.
            set_text(id, arg_string(c, a, 0));
            return value::undefined();
        });

    // `value` and `checked` ARE ACCESSORS, on a control.
    //
    // They were data properties written by refresh_control on whatever tick it
    // next ran. A page that creates a control and reads it back in the same
    // statement - `createInput('hello').value()`, which is p5's own DOM library
    // - therefore read the property as it was before the value existed. The
    // header's note that "the VM has no property accessors" was true when it
    // was written and is not any more.
    //
    // refresh_control still runs: it writes BACK a property assignment into the
    // control, which is how `input.value = ''` clears a field. These make the
    // READ live, which is the half that could not be done before.
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(id).value_or(atom{}));
        const std::string_view type = txn.attribute_value(id, atoms_->intern("type"));
        if (control_kind_of(tag, type) != control_kind::none) {
            obj.define_accessor("value",
                                value::object(cx.allocate<script::native_object>(
                                    "value",
                                    [this, id](context & c, std::span<value>) {
                                        const auto read = doc_->read();
                                        return c.string(forms_->state_of(read, *atoms_, id).value);
                                    })),
                                value::object(cx.allocate<script::native_object>(
                                    "value", [this, id](context & c, std::span<value> a) {
                                        const auto read = doc_->read();
                                        control_state & control =
                                            forms_->state_of(read, *atoms_, id);
                                        control.value = arg_string(c, a, 0);
                                        control.caret = control.value.size();
                                        control.selection = control.caret;
                                        // An assignment DIRTIES the control, so the `value`
                                        // attribute stops being the answer - otherwise setting
                                        // it to "" would be undone by the next read.
                                        control.value_edited = true;
                                        // The browser has to learn a control changed, or the
                                        // paint is stale until something else marks it.
                                        wrote_to_control_ = true;
                                        mutated();
                                        return value::undefined();
                                    })));
            obj.define_accessor("checked",
                                value::object(cx.allocate<script::native_object>(
                                    "checked",
                                    [this, id](context &, std::span<value>) {
                                        const auto read = doc_->read();
                                        return value::boolean(
                                            forms_->state_of(read, *atoms_, id).checked);
                                    })),
                                value::object(cx.allocate<script::native_object>(
                                    "checked", [this, id](context &, std::span<value> a) {
                                        const auto read = doc_->read();
                                        forms_->state_of(read, *atoms_, id).checked =
                                            !a.empty() && context::truthy(a[0]);
                                        wrote_to_control_ = true;
                                        mutated();
                                        return value::undefined();
                                    })));
        }
    }

    // `parentNode` and `children` are ACCESSORS because the tree moves. A
    // wrapper built when an element was detached and refreshed later would hand
    // back the parent it had at wrapping time, which for an element p5 creates
    // and then appends is null forever.
    const auto navigate = [&](std::string property, script::native_fn fn) {
        obj.define_accessor(
            property, value::object(cx.allocate<script::native_object>(property, std::move(fn))),
            value::undefined());
    };
    navigate("parentNode", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        return wrap(c, txn.parent(id));
    });
    // `parentElement` IS NOT `parentNode`. It is null when the parent is not an
    // element, which is exactly the case a tree-walking page tests to know it
    // has reached the top: `<html>`'s parent is the DOCUMENT, and answering
    // with it made the walk run one level past the root.
    navigate("parentElement", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        const node_id parent = txn.parent(id);
        if (!parent || txn.kind(parent).value_or(node_kind::element) != node_kind::element) {
            return value::null();
        }
        return wrap(c, parent);
    });
    // `element.shadowRoot` - THE ROOT, OR NULL, AND THE MODE DECIDES WHICH.
    // A closed root is not hidden from the engine, only from the page: it is
    // still in `shadow_roots_`, `getRootNode()` on a node inside it still
    // answers with it, and only this one accessor refuses to hand it over.
    // That is the whole of what `mode: "closed"` means.
    navigate("shadowRoot", [this, id](context & c, std::span<value>) {
        const node_id root = shadow_root_of(id);
        const shadow_tree * tree = shadow_tree_of(root);
        if (tree == nullptr || !tree->open) { return value::null(); }
        return wrap(c, root);
    });
    // `isConnected` - "shadow-including root is a document", DOM 4.4, and the
    // reason it is here rather than a data property is that it is exactly the
    // question `getRootNode({composed: true})` answers. A node inside a shadow
    // tree whose host is in the document IS connected, which is what
    // `Node-isConnected-shadow-dom.html` is a file about.
    navigate("isConnected", [this, id](context & c, std::span<value>) {
        (void)c;
        const auto txn = doc_->read();
        const node_id top = root_of_tree(txn, id, true);
        return value::boolean(txn.kind(top).value_or(node_kind::element) == node_kind::document);
    });
    // --- ParentNode and NonDocumentTypeChildNode -----------------------------
    //
    // THE ELEMENT-ONLY HALF OF THE TREE, which this wrapper had none of. Every
    // one of these is a one-assertion test file in `dom/nodes`, and there are
    // eight of them: Element-firstElementChild, -lastElementChild,
    // -nextElementSibling, -previousElementSibling, -childElementCount and
    // three -childElementCount-dynamic-* variants, each reporting `undefined`
    // where a node or a count belongs.
    //
    // ACCESSORS, like `parentNode` above and for the same reason: the three
    // dynamic tests add and remove children and read the count again, so a
    // value captured at wrapping time is wrong by construction.
    const auto element_children = [](const read_txn & txn, node_id parent) {
        std::vector<node_id> out;
        for (const node_id child : txn.children(parent)) {
            if (txn.kind(child).value_or(node_kind::text) == node_kind::element) {
                out.push_back(child);
            }
        }
        return out;
    };
    navigate("firstElementChild", [this, id, element_children](context & c, std::span<value>) {
        const auto txn = doc_->read();
        const std::vector<node_id> kids = element_children(txn, id);
        return kids.empty() ? value::null() : wrap(c, kids.front());
    });
    navigate("lastElementChild", [this, id, element_children](context & c, std::span<value>) {
        const auto txn = doc_->read();
        const std::vector<node_id> kids = element_children(txn, id);
        return kids.empty() ? value::null() : wrap(c, kids.back());
    });
    navigate("childElementCount", [this, id, element_children](context & c, std::span<value>) {
        (void)c;
        const auto txn = doc_->read();
        return value::number(static_cast<double>(element_children(txn, id).size()));
    });
    // A SIBLING WALK NEEDS THE PARENT, because the tree is stored as a child
    // list rather than as sibling links: the element's position among its
    // parent's children is the only place the answer lives. A node with no
    // parent has no siblings, which is null rather than an empty walk.
    const auto sibling = [this, element_children](context & c, node_id self, bool forward,
                                                  bool elements_only) {
        const auto txn = doc_->read();
        const node_id parent = txn.parent(self);
        if (!parent) { return value::null(); }
        const std::vector<node_id> kids =
            elements_only
                ? element_children(txn, parent)
                : std::vector<node_id>{txn.children(parent).begin(), txn.children(parent).end()};
        for (std::size_t i = 0; i < kids.size(); ++i) {
            if (kids[i] != self) { continue; }
            if (forward) { return i + 1 < kids.size() ? wrap(c, kids[i + 1]) : value::null(); }
            return i > 0 ? wrap(c, kids[i - 1]) : value::null();
        }
        // NOT AMONG ITS PARENT'S ELEMENT CHILDREN: a text node asked for its
        // previousElementSibling. Walk the full child list to find where it
        // sits and then scan outward for an element.
        if (!elements_only) { return value::null(); }
        const std::span<const node_id> all = txn.children(parent);
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (all[i] != self) { continue; }
            for (std::size_t step = 1; step <= all.size(); ++step) {
                const std::size_t at = forward ? i + step : i - step;
                if (forward ? at >= all.size() : step > i) { break; }
                if (txn.kind(all[at]).value_or(node_kind::text) == node_kind::element) {
                    return wrap(c, all[at]);
                }
            }
            return value::null();
        }
        return value::null();
    };
    navigate("firstChild", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        const std::span<const node_id> kids = txn.children(id);
        return kids.empty() ? value::null() : wrap(c, kids.front());
    });
    navigate("lastChild", [this, id](context & c, std::span<value>) {
        const auto txn = doc_->read();
        const std::span<const node_id> kids = txn.children(id);
        return kids.empty() ? value::null() : wrap(c, kids.back());
    });
    navigate("nextSibling",
             [id, sibling](context & c, std::span<value>) { return sibling(c, id, true, false); });
    navigate("previousSibling",
             [id, sibling](context & c, std::span<value>) { return sibling(c, id, false, false); });
    navigate("nextElementSibling",
             [id, sibling](context & c, std::span<value>) { return sibling(c, id, true, true); });
    navigate("previousElementSibling",
             [id, sibling](context & c, std::span<value>) { return sibling(c, id, false, true); });
    // `childNodes` is EVERY child, text nodes included; `children` is the
    // elements only. Both exist because they answer different questions, and a
    // page that wants the text nodes has no other way to reach them.
    navigate("childNodes", [this, id](context & c, std::span<value>) {
        value list = c.make_array();
        auto * items = static_cast<script::array_object *>(list.as_heap());
        const auto txn = doc_->read();
        for (const node_id child : txn.children(id)) { items->items.push_back(wrap(c, child)); }
        return list;
    });
    // AN HTMLCollection, LIVE - not an Array. `children` is the one of these
    // navigations the DOM gives an interface to, and `ParentNode-children.html`
    // checks liveness by appending and then asks what the thing IS.
    navigate("children", [this, id](context & c, std::span<value>) {
        return make_live_collection(c, [this, id] {
            const auto txn = doc_->read();
            std::vector<node_id> found;
            for (const node_id child : txn.children(id)) {
                if (txn.tag(child).has_value()) { found.push_back(child); }
            }
            return found;
        });
    });

    // `width` and `height` are numbers, and on a <canvas> they are the size of
    // its PIXEL BUFFER rather than of its laid-out box - `canvas.width / 2` is
    // the first line of most canvas pages. Assigning one resizes the surface,
    // which is what the spec means by a canvas being reset by the assignment.
    const auto reflect_size = [&](std::string property, double fallback) {
        obj.define_accessor(
            property,
            value::object(cx.allocate<script::native_object>(
                property,
                [this, id, property, fallback](context &, std::span<value>) {
                    const auto txn = doc_->read();
                    const std::string_view text = txn.attribute_value(id, atoms_->intern(property));
                    double parsed = 0;
                    bool any = false;
                    for (const char c : text) {
                        if (c < '0' || c > '9') { break; }
                        parsed = parsed * 10 + (c - '0');
                        any = true;
                    }
                    return value::number(any ? parsed : fallback);
                })),
            value::object(cx.allocate<script::native_object>(
                property, [this, id, property](context &, std::span<value> a) {
                    const double want = arg_number(a, 0);
                    (void)doc_->set_attribute(id, atoms_->intern(property),
                                              std::to_string(static_cast<long long>(want)));
                    // The SURFACE follows, or the canvas keeps drawing into a
                    // buffer of the size it was created at and everything past
                    // that edge is silently discarded.
                    if (canvases_ != nullptr) {
                        const auto txn = doc_->read();
                        const auto number = [&](std::string_view name, int missing) {
                            const std::string_view text =
                                txn.attribute_value(id, atoms_->intern(name));
                            int out = 0;
                            bool any = false;
                            for (const char c : text) {
                                if (c < '0' || c > '9') { break; }
                                out = out * 10 + (c - '0');
                                any = true;
                            }
                            return any ? out : missing;
                        };
                        const int w = number("width", 300);
                        const int h = number("height", 150);
                        canvases_->resize(id, w, h);
                        // And the WebGL context over the same canvas, which held
                        // a pointer INTO the buffer that resize just replaced.
                        resize_webgl_context(id, w, h);
                    }
                    mutated();
                    return value::undefined();
                })));
    };
    {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_->text(txn.tag(id).value_or(atom{}));
        if (tag == "canvas") {
            // The HTML defaults, which a page that omits the attributes relies on.
            reflect_size("width", 300);
            reflect_size("height", 150);
        } else if (tag == "img") {
            install_image_views(cx, obj, id);
        } else if (tag == "input" && txn.attribute_value(id, atoms_->intern("type")) == "file") {
            // AN EMPTY FileList, and it has to EXIST. There is no user here to
            // choose a file, so this is always empty - but `event.target.files`
            // is what every change handler iterates, and undefined there is a
            // TypeError on the first line of the handler rather than a quiet
            // nothing-was-chosen.
            const value files = cx.make_array();
            static_cast<script::array_object *>(files.as_heap())->items.clear();
            obj.set("files", files);
        } else if ((txn.has_attribute(id, atoms_->intern("width")) ||
                    txn.has_attribute(id, atoms_->intern("height"))) &&
                   !interface_reflects_size(tag)) {
            // AND NOT WHERE THE TABLE HAS A ROW. `<td width=50>`, `<marquee
            // width=50>` and `<iframe width=50>` reflect a DOMString - "50",
            // not 50 - and `<input width=50>` an unsigned long with HTML's
            // integer rules rather than the digit loop above. This branch is
            // what is left: an element whose interface says nothing about
            // width, `<div width=50>` and `<svg width=50>` among them, where a
            // number is better than nothing at all.
            reflect_size("width", 0);
            reflect_size("height", 0);
        }
    }

    // --- element.classList
    //
    // Every operation reads the attribute, edits the token list and writes it
    // back, so nothing is cached and a class added by the parser, by
    // setAttribute or by the style engine is seen by all of them.
    auto * list = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto tokens_now = [this, id] {
        const auto txn = doc_->read();
        return class_tokens(txn.attribute_value(id, atoms_->intern("class")));
    };
    const auto write_tokens = [this, id](const std::vector<std::string> & tokens) {
        std::string text;
        for (const std::string & token : tokens) {
            if (!text.empty()) { text += ' '; }
            text += token;
        }
        (void)doc_->set_attribute(id, atoms_->intern("class"), text);
        mutated();
    };
    const auto list_method = [&](std::string name, script::native_fn fn) {
        list->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    list_method("add", [tokens_now, write_tokens](context & c, std::span<value> args) {
        std::vector<std::string> tokens = tokens_now();
        for (const value & v : args) {
            const std::string token = c.to_string(v);
            if (token.empty()) { continue; }
            if (std::find(tokens.begin(), tokens.end(), token) == tokens.end()) {
                tokens.push_back(token);
            }
        }
        write_tokens(tokens);
        return value::undefined();
    });
    list_method("remove", [tokens_now, write_tokens](context & c, std::span<value> args) {
        std::vector<std::string> tokens = tokens_now();
        for (const value & v : args) {
            const std::string token = c.to_string(v);
            std::erase(tokens, token);
        }
        write_tokens(tokens);
        return value::undefined();
    });
    list_method("contains", [tokens_now](context & c, std::span<value> args) {
        const std::vector<std::string> tokens = tokens_now();
        return value::boolean(std::find(tokens.begin(), tokens.end(), arg_string(c, args, 0)) !=
                              tokens.end());
    });
    list_method("toggle", [tokens_now, write_tokens](context & c, std::span<value> args) {
        std::vector<std::string> tokens = tokens_now();
        const std::string token = arg_string(c, args, 0);
        // The two-argument form FORCES a state rather than flipping it -
        // `classList.toggle("on", isOn)` is the idiom, and treating the second
        // argument as absent turns it into a flip that is right half the time.
        const bool present = std::find(tokens.begin(), tokens.end(), token) != tokens.end();
        const bool want = args.size() > 1 ? context::truthy(args[1]) : !present;
        if (want && !present) { tokens.push_back(token); }
        if (!want && present) { std::erase(tokens, token); }
        write_tokens(tokens);
        return value::boolean(want);
    });
    list_method("item", [tokens_now](context & c, std::span<value> args) {
        const std::vector<std::string> tokens = tokens_now();
        const auto i = static_cast<std::ptrdiff_t>(
            context::to_number(args.empty() ? value::undefined() : args[0]));
        if (i < 0 || static_cast<std::size_t>(i) >= tokens.size()) { return value::null(); }
        return c.string(tokens[static_cast<std::size_t>(i)]);
    });
    // An ACCESSOR, not a number: the count changes whenever the attribute does,
    // and a data property would report whatever it was when the element was
    // first wrapped.
    list->define_accessor("length",
                          value::object(cx.allocate<script::native_object>(
                              "length",
                              [tokens_now](context &, std::span<value>) {
                                  return value::number(static_cast<double>(tokens_now().size()));
                              })),
                          value::undefined());
    obj.set("classList", value::object(list));

    // --- element.dataset
    //
    // A live DOMStringMap over this element's `data-*` attributes, and it did
    // not exist at all: `el.dataset.foo` was a TypeError on the first line of
    // every page that uses the ordinary way of hanging state off an element.
    //
    // A PROXY, because the set of properties IS the set of attributes and a
    // page may write a key this element has never carried. What that costs is
    // said here rather than left to be discovered: this VM implements the
    // `get`, `set` and `has` traps and no others, so
    //
    //   * `for (const k in el.dataset)` enumerates NOTHING - `op::own_keys`
    //     yields an empty array for a proxy;
    //   * `delete el.dataset.foo` is a silent no-op - `op::delete_prop` skips
    //     anything that is not exactly a plain object;
    //   * `el.dataset instanceof DOMStringMap` is false - `instance_of` walks
    //     an object_object's prototype and a proxy has none of its own.
    //
    // All three are deviations in `lib/Script` and that is where they are
    // fixable. The alternative shape - a plain object refilled on every read,
    // as `attributes` above is - trades those three for a `set` that never
    // reaches the document at all, which is the worse half of the trade: a
    // write that silently does nothing is a wrong answer, and an enumeration
    // that finds nothing is a missing one.
    //
    // NOT ON EVERY ELEMENT. `dataset` belongs to HTMLElement, SVGElement and
    // MathMLElement, and `document.createElementNS("test", "test").dataset` is
    // `undefined` - which `dataset.html` asserts by name, and which is the only
    // reason this is conditional rather than unconditional.
    {
        const auto txn = doc_->read();
        const std::string ns = namespace_of(id);
        const bool wanted = txn.kind(id).value_or(node_kind::text) == node_kind::element &&
                            (ns == html_namespace || ns == svg_namespace || ns == mathml_namespace);
        if (wanted) { install_dataset(cx, obj, id); }
    }
}

void dom_bindings::install_dataset(context & cx, script::object_object & obj, node_id id) {
    // THE STORE'S PROTOTYPE IS DELIBERATELY LEFT ALONE. Every miss falls
    // through to `lookup_property` on it, and a plain object reaches the
    // builtin Object.prototype tables that way - which is what
    // `dataset-prototype.html`'s "Properties on Object.prototype should shine
    // through" is asking. Hanging `DOMStringMap.prototype` (whose own
    // prototype link is null) in front of it would cut that off and buy
    // nothing, `instanceof` on a proxy being false either way.
    auto * store = static_cast<script::object_object *>(cx.make_object().as_heap());
    auto * handler = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto trap = [&](std::string name, script::native_fn fn) {
        handler->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // ONE ATTRIBUTE, FOUND THE WAY THE SPECIFICATION FINDS IT: by computing
    // every supported property name from the attribute list and comparing, not
    // by mangling the key and looking that up. The two disagree exactly where
    // `dataset-delete.html` and `dataset-get.html` say they must.
    const auto value_of = [this, id](std::string_view key) -> std::optional<std::string> {
        const auto txn = doc_->read();
        std::string name;
        for (const attribute & held : txn.attributes(id)) {
            // NULL NAMESPACE ONLY: `xlink:data-x` is not a dataset attribute
            // however its local name reads.
            if (held.ns) { continue; }
            if (dataset_name_of(atoms_->text(held.name), name) && name == key) {
                return held.value;
            }
        }
        return std::nullopt;
    };
    trap("get", [value_of](context & c, std::span<value> args) {
        if (args.size() < 2) { return value::undefined(); }
        const std::string key = c.to_string(args[1]);
        if (const std::optional<std::string> found = value_of(key)) { return c.string(*found); }
        return c.lookup_property(args[0], key);
    });
    trap("has", [value_of](context & c, std::span<value> args) {
        if (args.size() < 2) { return value::boolean(false); }
        const std::string key = c.to_string(args[1]);
        if (value_of(key)) { return value::boolean(true); }
        return value::boolean(!c.lookup_property(args[0], key).is_undefined());
    });
    trap("set", [this, id](context & c, std::span<value> args) {
        if (args.size() < 3) { return value::boolean(false); }
        const std::string key = c.to_string(args[1]);
        std::string name;
        switch (dataset_attribute_of(key, name)) {
        case dataset_fault::syntax:
            // "If name contains a U+002D followed by an ASCII lower alpha,
            // throw a SyntaxError" - because that key is not one this map could
            // ever hand back, `data--foo` reading as `Foo` and not as `-foo`.
            throw_dom_exception(c, "SyntaxError",
                                "dataset: '" + key + "' is not a name a data- attribute can have");
            return value::boolean(false);
        case dataset_fault::character:
            throw_dom_exception(c, "InvalidCharacterError",
                                "dataset: '" + name + "' is not a valid attribute name");
            return value::boolean(false);
        case dataset_fault::none: break;
        }
        // ALREADY LOWERCASE by construction, so this interns as written rather
        // than folding: the only characters the mangle can emit above 'z' are
        // the ones it copied, and folding them would be folding the author's.
        (void)doc_->set_attribute(id, atoms_->intern(name), c.to_string(args[2]));
        mutated();
        return value::boolean(true);
    });
    obj.set("dataset", value::object(cx.allocate<script::proxy_object>(value::object(store),
                                                                       value::object(handler))));
}

rect dom_bindings::box_of(node_id id) const {
    if (fragments_ == nullptr) { return rect{}; }
    const auto find = [&](auto && self, const layout::fragment & f, float dx,
                          float dy) -> std::optional<rect> {
        const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
        if (f.source == id) { return box; }
        for (const auto & child : f.children) {
            if (const std::optional<rect> hit = self(self, child, box.x, box.y)) { return hit; }
        }
        return std::nullopt;
    };
    return find(find, *fragments_, 0, 0).value_or(rect{});
}

void dom_bindings::install_element_methods(context & cx, script::object_object & obj) {
    const auto method = [&](std::string name, script::native_fn fn) {
        obj.set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };

    // THE `on...` HANDLER PROPERTIES, PRESENT AND NULL.
    //
    // A browser gives every element one of these per event, defaulting to null,
    // and libraries FEATURE-DETECT with `'onwheel' in element`. Assignment
    // already worked here - `el.onclick = fn` made a property - but `in`
    // answered false, because the property did not exist until something wrote
    // it. That is not a distinction a page can be expected to know about.
    //
    // IT COST A ZOOM. Babylon picks which wheel event to listen for with
    //
    //     "onwheel" in document.createElement("div") ? "wheel"
    //       : document.onmousewheel !== undefined ? "mousewheel" : "DOMMouseScroll"
    //
    // so it fell all the way through to DOMMouseScroll - a Firefox-only name
    // nothing here dispatches - and its ArcRotateCamera could be dragged and
    // not zoomed. Every listener was attached, every event was sent, and the
    // two sets had different names: the same shape as the pointerdown/mousedown
    // fault this file already records.
    //
    // EXACTLY THE EVENTS THIS ENGINE CAN DISPATCH, and no more. A handler
    // property for an event that never fires is a detection that answers yes
    // and a page that then waits forever - which is worse than answering no.
    for (const char * handler :
         {"onclick", "onwheel", "onmousedown", "onmouseup", "onmousemove", "oncontextmenu",
          "onpointerdown", "onpointerup", "onpointermove", "onkeydown", "onkeyup", "oninput",
          "onchange", "onsubmit", "ontoggle", "onfocus", "onblur", "onload", "onerror"}) {
        if (obj.find(handler) == nullptr) { obj.set(handler, value::null()); }
    }

    // `element.click()` - CLICKING WITHOUT A MOUSE.
    //
    // It was absent, and that is how p5's save() reaches the outside world:
    // downloadFile makes an <a href download>, calls click() on it, and revokes
    // the URL on the next line. So the whole export path was one missing method
    // wide, and the failure was that nothing happened - no error, no file.
    //
    // Both halves, in the right order: the event first, through the ordinary
    // capture-and-bubble dispatch, and the DEFAULT ACTION after it unless a
    // listener called preventDefault. A click() that only dispatched would leave
    // `link.click()` doing nothing and `checkbox.click()` not checking anything.
    method("click", [this](context & c, std::span<value> args) {
        (void)args;
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        if (!dispatch_event("click", id, make_event(c, "click", id)) && on_activate_) {
            on_activate_(id);
        }
        return value::undefined();
    });

    method("setAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        const std::string name = arg_string(c, args, 0);
        if (!valid_attribute_name(name)) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "setAttribute: '" + name + "' is not a valid attribute name");
            return value::undefined();
        }
        if (!id) { return value::undefined(); }
        const std::string text = arg_string(c, args, 1);
        const auto txn = doc_->read();
        // THE FIRST ATTRIBUTE WITH THIS QUALIFIED NAME, whatever its namespace,
        // and only its VALUE changes - the DOM layer's set_attribute is what
        // means "Setting the same attribute with another prefix should not
        // change the prefix", which is a subtest by name.
        (void)doc_->set_attribute(id, attribute_key(txn, id, name), text);
        mutated();
        return value::undefined();
    });
    // THE NAMESPACED HALF OF THE ATTRIBUTE API. Every one of these matches on
    // the PAIR (namespace, local name), in which the prefix takes no part, and
    // none of them folds case: an element may hold `x` in no namespace and `x`
    // in two others at once, and each of the three lookups has a different
    // right answer. `Element-removeAttribute.html`'s two subtests are that
    // sentence, in both orders.
    method("setAttributeNS", [this](context & c, std::span<value> args) {
        const std::string ns = namespace_argument(c, args, 0);
        // A DOMString rather than a nullable one, so `null` here really is the
        // four characters "null" and an omitted argument is "undefined".
        const std::string qualified =
            args.size() > 1 ? c.to_string(args[1]) : std::string{"undefined"};
        if (!validate_and_extract(c, "setAttributeNS", ns, qualified)) {
            return value::undefined();
        }
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        // INTERNED AS WRITTEN. The qualified name IS the attribute's name and
        // folding it would lose the case `setAttributeNS("", "ALIGN", ...)`
        // deliberately keeps - see the note on attribute_key.
        (void)doc_->set_attribute_ns(id, atoms_->intern(ns), atoms_->intern(qualified),
                                     arg_string(c, args, 2));
        mutated();
        return value::undefined();
    });
    method("getAttributeNS", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::null(); }
        const std::string ns = namespace_argument(c, args, 0);
        const auto txn = doc_->read();
        const attribute * held = txn.find_attribute_ns(id, ns, arg_string(c, args, 1));
        return held == nullptr ? value::null() : c.string(held->value);
    });
    method("hasAttributeNS", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const std::string ns = namespace_argument(c, args, 0);
        return value::boolean(doc_->read().has_attribute_ns(id, ns, arg_string(c, args, 1)));
    });
    method("removeAttributeNS", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const std::string ns = namespace_argument(c, args, 0);
        // A LOCAL NAME, not a qualified one: `removeAttributeNS(XML, "a:bb")`
        // removes NOTHING, which is the whole of Element-removeAttributeNS.html.
        (void)doc_->remove_attribute_ns(id, ns, arg_string(c, args, 1));
        mutated();
        return value::undefined();
    });
    // `hasAttributes()` - "does this element have any at all", which is a
    // different question from `attributes.length !== 0` only in that a page can
    // ask it without materialising the map.
    method("hasAttributes", [this](context & c, std::span<value>) {
        (void)c;
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        return value::boolean(!doc_->read().attributes(id).empty());
    });
    // THE Attr SPELLINGS OF THE SAME FOUR LOOKUPS. `getAttributeNodeNS` is what
    // `Attr-prefix.html` reaches for on every one of its six cases, because the
    // prefix and the namespace are the two things only an Attr can report.
    method("getAttributeNode", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::null(); }
        std::optional<attribute> held;
        {
            const auto txn = doc_->read();
            const attribute * found =
                txn.find_attribute(id, attribute_key(txn, id, arg_string(c, args, 0)));
            if (found != nullptr) { held = *found; }
        }
        return held ? attribute_object(c, id, *held) : value::null();
    });
    method("getAttributeNodeNS", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::null(); }
        const std::string ns = namespace_argument(c, args, 0);
        std::optional<attribute> held;
        {
            const auto txn = doc_->read();
            const attribute * found = txn.find_attribute_ns(id, ns, arg_string(c, args, 1));
            if (found != nullptr) { held = *found; }
        }
        return held ? attribute_object(c, id, *held) : value::null();
    });
    // `setAttributeNode` and `removeAttributeNode` are `setNamedItem` and
    // `removeNamedItem` under other names - DOM 4.9 defines each pair in terms
    // of the same "set an attribute" and "remove an attribute" - so they are
    // FORWARDED rather than written twice. Two implementations of one operation
    // is two chances for the returned old Attr to differ.
    const auto through_map = [this](std::string on_map) {
        return [this, on_map](context & c, std::span<value> args) {
            const node_id id = receiver(c);
            if (!id) { return value::null(); }
            const value map = c.lookup_property(c.current_this(), "attributes");
            const value fn = c.lookup_property(map, on_map);
            if (!fn.is_callable()) { return value::null(); }
            return c.call(fn, args, map);
        };
    };
    method("setAttributeNode", through_map("setNamedItem"));
    method("setAttributeNodeNS", through_map("setNamedItemNS"));
    method("removeAttributeNode", [this](context & c, std::span<value> args) {
        // NOT through the map: `removeAttributeNode` takes the Attr ITSELF and
        // throws a NotFoundError when it is not this element's, where
        // `removeNamedItem` takes a name.
        const node_id id = receiver(c);
        const value given = arg(args, 0);
        if (!id || !given.is_object()) {
            throw_dom_exception(c, "NotFoundError",
                                "removeAttributeNode: the argument is not an attribute of this "
                                "element");
            return value::null();
        }
        const value ns_property = c.lookup_property(given, "namespaceURI");
        const std::string ns = ns_property.is_nullish() ? std::string{} : c.to_string(ns_property);
        const std::string local = c.to_string(c.lookup_property(given, "localName"));
        std::optional<attribute> held;
        {
            const auto txn = doc_->read();
            const attribute * found = txn.find_attribute_ns(id, ns, local);
            if (found != nullptr) { held = *found; }
        }
        if (!held) {
            throw_dom_exception(c, "NotFoundError",
                                "removeAttributeNode: '" + local +
                                    "' is not an attribute of this "
                                    "element");
            return value::null();
        }
        (void)doc_->remove_attribute_ns(id, ns, local);
        mutated();
        // THE ARGUMENT IS THE ANSWER, and it is the ARGUMENT that has to be
        // detached: this is the one removal that hands back an object the page
        // already holds rather than one made here, so the live accessors on it
        // are still reading through to an element that no longer has the
        // attribute. Frozen in place, for the reason attribute_object gives.
        auto * detaching = static_cast<script::object_object *>(given.as_heap());
        for (const char * spelling : {"value", "nodeValue", "textContent"}) {
            (void)detaching->erase_accessor(spelling);
            detaching->set(spelling, c.string(held->value));
        }
        detaching->set("ownerElement", value::null());
        return given;
    });
    // `toggleAttribute(name, force)` - the boolean-attribute spelling, and it
    // ANSWERS whether the attribute is present afterwards, which is what a page
    // toggling one reads. Without it the only way to flip `disabled` was a
    // hasAttribute/removeAttribute/setAttribute dance that reads the tree twice.
    method("toggleAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        const std::string name = arg_string(c, args, 0);
        if (!valid_attribute_name(name)) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "toggleAttribute: '" + name + "' is not a valid attribute name");
            return value::boolean(false);
        }
        if (!id) { return value::boolean(false); }
        const atom key = attribute_key(doc_->read(), id, name);
        const bool present = doc_->read().has_attribute(id, key);
        // `force` is TRISTATE: absent means "flip", a present `false` means
        // "remove whether or not it is there". `args.size()` is the only thing
        // that can tell the first from the second.
        const bool want = args.size() > 1 ? context::truthy(args[1]) : !present;
        if (want == present) { return value::boolean(present); }
        if (want) {
            (void)doc_->set_attribute(id, key, "");
        } else {
            (void)doc_->remove_attribute(id, key);
        }
        mutated();
        return value::boolean(want);
    });
    // `getAttributeNames()` - the QUALIFIED names, in order, which is the one
    // answer `element.attributes` cannot give in a single string comparison.
    method("getAttributeNames", [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        if (!id) { return out; }
        const auto txn = doc_->read();
        for (const attribute & held : txn.attributes(id)) {
            items->items.push_back(c.string(std::string{atoms_->text(held.name)}));
        }
        return out;
    });
    method("getAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::null(); }
        const auto txn = doc_->read();
        // PRESENT-BUT-EMPTY is not absent. `<details open>`, `<input
        // disabled>` and `<option selected>` all have an empty value, and
        // returning null for them made every boolean attribute unreadable
        // from script - the one shape of attribute that is only ever tested
        // for presence.
        const atom name = attribute_key(txn, id, arg_string(c, args, 0));
        const attribute * held = txn.find_attribute(id, name);
        if (held == nullptr) { return value::null(); }
        return c.string(held->value);
    });
    // THE OTHER TWO HALVES OF THE ATTRIBUTE API. `setAttribute` and
    // `getAttribute` were here and these were not, so an attribute could be
    // written and read and never taken away: `el.removeAttribute('class')` was a
    // TypeError, and `el.hasAttribute('disabled')` - the correct way to ask
    // about a boolean attribute - did not exist at all, leaving `getAttribute()
    // !== null` as the only spelling and undefined behaviour for the page that
    // did not know it.
    method("removeAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const auto txn = doc_->read();
        (void)doc_->remove_attribute(id, attribute_key(txn, id, arg_string(c, args, 0)));
        mutated();
        return value::undefined();
    });
    method("hasAttribute", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const auto txn = doc_->read();
        return value::boolean(
            txn.has_attribute(id, attribute_key(txn, id, arg_string(c, args, 0))));
    });
    method("setText", [this](context & c, std::span<value> args) {
        set_text(id_or_nothing(c), arg_string(c, args, 0));
        return value::undefined();
    });
    method("getText", [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        return id ? c.string(text_of(id)) : c.string(std::string{});
    });
    method("addClass", [this](context & c, std::span<value> args) {
        edit_classes(receiver(c), arg_string(c, args, 0), true);
        return value::undefined();
    });
    method("removeClass", [this](context & c, std::span<value> args) {
        edit_classes(receiver(c), arg_string(c, args, 0), false);
        return value::undefined();
    });
    method("hasClass", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const auto txn = doc_->read();
        const std::string want = arg_string(c, args, 0);
        for (const std::string_view cls : split(txn.attribute_value(id, atoms_->intern("class")))) {
            if (cls == want) { return value::boolean(true); }
        }
        return value::boolean(false);
    });
    // "INSERT ADJACENT", DOM 4.9 - ONE ALGORITHM FOR THREE METHODS, which is
    // the point of it. `insertAdjacentHTML` did this by hand and the other two
    // did not exist, and doing it by hand got `afterend` wrong: it APPENDED to
    // the parent rather than placing the node after this element, so a
    // `beforebegin` and an `afterend` on the same element landed in the wrong
    // order whenever the element had a later sibling.
    //
    // Answers (parent, before), or nothing. There are two kinds of nothing and
    // the caller does not have to tell them apart: an unrecognised position is a
    // SyntaxError and `beforebegin`/`afterend` on the DOCUMENT ELEMENT is a
    // HierarchyRequestError, both already thrown by the time this returns; an
    // element with no parent at all is the "return null" the specification
    // gives, and throws nothing.
    //
    // THE DOCUMENT ELEMENT'S PARENT IS THE DOCUMENT in the DOM and is EMPTY
    // here - this tree builder has no Document node, see install_document_as_node
    // - so the one place the two models differ has to be named rather than
    // inferred. A second element or a text node beside `<html>` would be a
    // second child of the Document, which is what pre-insertion refuses.
    const auto adjacent_place =
        [this](context & c, node_id self,
               const std::string & given) -> std::optional<std::pair<node_id, node_id>> {
        std::string where = given;
        ascii_lower_in_place(where);
        const auto txn = doc_->read();
        if (where == "afterbegin") {
            const std::span<const node_id> kids = txn.children(self);
            return std::pair{self, kids.empty() ? node_id{} : kids.front()};
        }
        if (where == "beforeend") { return std::pair{self, node_id{}}; }
        const bool before = where == "beforebegin";
        if (!before && where != "afterend") {
            throw_dom_exception(c, "SyntaxError",
                                "insertAdjacent: '" + given +
                                    "' is not one of beforebegin, afterbegin, beforeend "
                                    "or afterend");
            return std::nullopt;
        }
        const node_id parent = txn.parent(self);
        if (!parent) {
            if (self == txn.root()) {
                throw_dom_exception(c, "HierarchyRequestError",
                                    "insertAdjacent: the document element cannot have a sibling");
            }
            return std::nullopt;
        }
        if (before) { return std::pair{parent, self}; }
        const std::span<const node_id> siblings = txn.children(parent);
        node_id next;
        for (std::size_t i = 0; i + 1 < siblings.size(); ++i) {
            if (siblings[i] == self) { next = siblings[i + 1]; }
        }
        return std::pair{parent, next};
    };
    // `insertAdjacentHTML(position, markup)` - a fragment parse at one of four
    // places relative to this element. The parser and the copy are the same
    // ones innerHTML uses; only where the nodes land differs.
    method("insertAdjacentHTML", [this, adjacent_place](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self || atoms_ == nullptr) { return value::undefined(); }
        const std::string where = arg_string(c, args, 0);
        const std::string markup = arg_string(c, args, 1);
        const std::optional<std::pair<node_id, node_id>> place = adjacent_place(c, self, where);
        if (!place) { return value::undefined(); }

        // Parsed into a scratch document, as innerHTML does and for the same
        // reason: tree_builder::parse replaces the root it is handed.
        document scratch{*atoms_};
        (void)parse_html(scratch, markup);
        const auto from = scratch.read();
        node_id body{};
        const auto find_body = [&](auto && walk, node_id at) -> void {
            if (!body && from.tag(at).value_or(atom{}) == atoms_->intern_lower("body")) {
                body = at;
            }
            for (const node_id child : from.children(at)) { walk(walk, child); }
        };
        find_body(find_body, from.root());
        if (!body) { return value::undefined(); }

        // IN ORDER, because each node goes before the SAME reference rather
        // than before the one just added. copy_subtree appends to the parent it
        // is given, so the move is a no-op when the reference is empty.
        for (const node_id child : from.children(body)) {
            const node_id made = copy_subtree(from, child, place->first);
            if (place->second) { (void)doc_->insert_before(place->first, made, place->second); }
        }
        mutated();
        return value::undefined();
    });
    // ...and the two spellings that take a NODE rather than markup, both of
    // which were missing. `insertAdjacentElement` ANSWERS with the element it
    // inserted - or null, which is how a page learns the position was one the
    // element has no room for.
    method("insertAdjacentElement", [this, adjacent_place](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self) { return value::null(); }
        const std::optional<std::pair<node_id, node_id>> place =
            adjacent_place(c, self, arg_string(c, args, 0));
        if (!place) { return value::null(); }
        const node_id child = handle_of(arg(args, 1));
        if (!pre_insert_valid(c, place->first, child, arg(args, 1), value::null())) {
            return value::null();
        }
        (void)insert_node(place->first, child, place->second);
        return arg(args, 1);
    });
    method("insertAdjacentText", [this, adjacent_place](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self) { return value::undefined(); }
        const std::string text = arg_string(c, args, 1);
        const std::optional<std::pair<node_id, node_id>> place =
            adjacent_place(c, self, arg_string(c, args, 0));
        if (!place) { return value::undefined(); }
        (void)insert_node(place->first, doc_->create_text(text), place->second);
        return value::undefined();
    });

    // TREE NAVIGATION. appendChild and removeChild could already change the
    // tree; nothing could WALK it, so an element could not reach its own
    // parent. `this.elt.parentNode.removeChild(this.elt)` is the ordinary way
    // to take an element out of the page - it is how p5.js discards the default
    // canvas when a sketch calls createCanvas - and with parentNode undefined
    // the removal threw inside a callback and the discarded canvas stayed in
    // the document, laid out and painted, underneath the real one.
    method("remove", [this](context & c, std::span<value>) {
        const node_id self = receiver(c);
        if (self) {
            (void)doc_->remove_child(self);
            mutated();
        }
        return value::undefined();
    });
    method("insertBefore", [this](context & c, std::span<value> args) {
        const node_id parent = receiver(c);
        const node_id child = handle_of(arg(args, 0));
        const node_id before = handle_of(arg(args, 1));
        // A null reference node means "at the end", which is what makes
        // `insertBefore(node, null)` a documented spelling of appendChild -
        // insert_node reads an empty handle the same way.
        if (!pre_insert_valid(c, parent, child, arg(args, 0), arg(args, 1))) {
            return value::undefined();
        }
        (void)insert_node(parent, child, before);
        return arg(args, 0);
    });
    // `moveBefore` IS `insertBefore` THAT DOES NOT REMOVE FIRST. The DOM says a
    // move preserves state an insertion would destroy - an <iframe>'s document,
    // a playing <video>, a focused control, a running animation - and this
    // engine has none of those, so what is left of the operation is exactly the
    // insertion. The DIFFERENCE that IS observable here is the validity checks,
    // which are stricter than insertBefore's: the node must already have a
    // parent, and both nodes must be in the same document.
    //
    // Named rather than aliased, because `moveBefore === insertBefore` would be
    // a lie a page can test for, and because when state preservation does
    // arrive it arrives here.
    method("moveBefore", [this](context & c, std::span<value> args) {
        const node_id parent = receiver(c);
        const node_id child = handle_of(arg(args, 0));
        if (!pre_insert_valid(c, parent, child, arg(args, 0), arg(args, 1))) {
            return value::undefined();
        }
        {
            const auto txn = doc_->read();
            // "If node's parent is null, then throw a HierarchyRequestError" -
            // a move has to move something from somewhere.
            if (!txn.parent(child)) {
                throw_dom_exception(c, "HierarchyRequestError",
                                    "moveBefore: the node being moved has no parent");
                return value::undefined();
            }
        }
        (void)insert_node(parent, child, handle_of(arg(args, 1)));
        return arg(args, 0);
    });
    // WHERE THE ELEMENT IS ON SCREEN. A page turns a pointer event's viewport
    // coordinates into coordinates within an element by subtracting this - p5's
    // getMouseInfo does exactly that to compute mouseX/mouseY - so without it
    // every mouse listener throws on its first event. The listeners were
    // installed and the events were dispatched; the conversion in between is
    // what was missing, and it made the whole input surface look absent.
    // `querySelector` ON AN ELEMENT, searching its own subtree. The document
    // had both and an element had neither, so the ordinary "find something
    // inside this" - which is what a library does with a container it owns -
    // threw. p5.js's describe() builds an offscreen tree and queries it.
    method("querySelector", [this](context & c, std::span<value> args) {
        const std::vector<node_id> found = query(arg_string(c, args, 0), receiver(c));
        return found.empty() ? value::null() : wrap(c, found.front());
    });
    method("querySelectorAll", [this](context & c, std::span<value> args) {
        const std::vector<node_id> found = query(arg_string(c, args, 0), receiver(c));
        value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        for (const node_id node : found) { items->items.push_back(wrap(c, node)); }
        return out;
    });
    // `element.getElementsByTagName(tag)` - the DOCUMENT had one and an element
    // did not, so a page that scoped its search to a subtree found the method
    // missing. p5's XML module walks a parsed document with exactly this.
    method("getElementsByTagName", [this](context & c, std::span<value> args) {
        const node_id from = receiver(c);
        const std::string want = arg_string(c, args, 0);
        value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        if (!from) { return out; }
        const auto txn = doc_->read();
        // `*` is every descendant, which is what a page uses to count a subtree.
        const atom tag = want == "*" ? atom{} : atoms_->intern_lower(want);
        const auto walk = [&](auto && self, node_id at, bool include) -> void {
            if (include && (want == "*" || txn.tag(at).value_or(atom{}) == tag)) {
                items->items.push_back(wrap(c, at));
            }
            for (const node_id child : txn.children(at)) { self(self, child, true); }
        };
        // DESCENDANTS ONLY - the element itself is not one of its own results.
        walk(walk, from, false);
        return out;
    });
    // `element.getElementsByTagNameNS(namespace, localName)`, the same battery
    // the document has answered all along. `Document-Element-getElementsBy...
    // TagNameNS.js` runs its whole table against BOTH, so half of every case in
    // it was a TypeError on a missing method rather than a comparison. "*"
    // means any on either half, and - as with getElementsByTagName - the
    // element is not one of its own results, which the file asserts by name.
    method("getElementsByTagNameNS", [this](context & c, std::span<value> args) {
        const node_id from = receiver(c);
        const std::string ns = namespace_argument(c, args, 0);
        const std::string local = arg_string(c, args, 1);
        return make_live_collection(c, [this, from, ns, local] {
            std::vector<node_id> found;
            if (!from) { return found; }
            const auto txn = doc_->read();
            const auto walk = [&](auto && self, node_id at, bool include) -> void {
                if (include && txn.tag(at).has_value()) {
                    // THE LOCAL NAME, not the qualified one: a prefix takes no
                    // part in this match any more than it does in getAttributeNS.
                    const std::string_view name = atoms_->text(txn.tag(at).value_or(atom{}));
                    const std::size_t colon = name.find(':');
                    const std::string_view own =
                        colon == std::string_view::npos ? name : name.substr(colon + 1);
                    if ((local == "*" || own == local) && (ns == "*" || namespace_of(at) == ns)) {
                        found.push_back(at);
                    }
                }
                for (const node_id child : txn.children(at)) { self(self, child, true); }
            };
            walk(walk, from, false);
            return found;
        });
    });
    // `element.getElementsByClassName(names)`, scoped to this subtree and LIVE
    // for the same reason the document's is - see make_live_collection. The
    // element is not one of its own results.
    method("getElementsByClassName", [this](context & c, std::span<value> args) {
        const node_id from = receiver(c);
        const std::vector<std::string> tokens = ordered_set(arg_string(c, args, 0));
        return make_live_collection(c, [this, from, tokens] {
            return from ? all_by_class(from, tokens) : std::vector<node_id>{};
        });
    });
    // THE ParentNode AND ChildNode INSERTION METHODS, none of which existed.
    //
    // They are what modern code writes instead of appendChild/insertBefore, and
    // they differ in two ways that matter: they take ANY NUMBER of arguments,
    // and a STRING argument becomes a Text node - so `el.append("x", node)` is
    // one call where the old spelling is three lines and a createTextNode. WPT
    // reaches for them constantly, and so does every library written since 2016.
    const auto parent_of = [this](node_id id) { return doc_->read().parent(id); };
    method("append", [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        for (const value & one : args) { (void)insert_node(self, node_from(c, one), node_id{}); }
        return value::undefined();
    });
    method("prepend", [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self) { return value::undefined(); }
        // BEFORE THE FIRST CHILD, and the arguments keep their order because
        // each is inserted before the SAME reference node rather than before
        // the one just added.
        node_id first;
        {
            const auto txn = doc_->read();
            const auto children = txn.children(self);
            if (!children.empty()) { first = children.front(); }
        }
        for (const value & one : args) { (void)insert_node(self, node_from(c, one), first); }
        return value::undefined();
    });
    // `replaceChildren` - the third of the ParentNode mixin, and the one an
    // element did not have. `document` has had it all along
    // (bindings/document.cpp); an element and a ShadowRoot are where a page
    // actually calls it, because "empty this and put those in it" is what
    // rebuilding a list IS.
    method("replaceChildren", [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self) { return value::undefined(); }
        // COPIED BEFORE REMOVING: children() is a view onto the live child list
        // and each removal republishes it.
        std::vector<node_id> existing;
        {
            const auto txn = doc_->read();
            for (const node_id child : txn.children(self)) { existing.push_back(child); }
        }
        for (const node_id child : existing) { (void)doc_->remove_child(child); }
        for (const value & one : args) { (void)insert_node(self, node_from(c, one), node_id{}); }
        mutated();
        return value::undefined();
    });
    // --- shadow DOM: the two things an ELEMENT gains --------------------------
    //
    // `attachShadow` is on every wrapper rather than on Element.prototype for
    // the reason every other method in this function is: this engine's methods
    // are own properties of the wrapper. It refuses a receiver that is not an
    // element, which is what keeps `shadowRoot.attachShadow` from building a
    // second tree under a fragment.
    method("attachShadow", [this](context & c, std::span<value> args) {
        return attach_shadow(c, receiver(c), args);
    });
    // `getRootNode(options)`, DOM 4.4. On every node, which is what
    // `rootNode.html` asks of an element, a text node and a fragment in turn.
    method("getRootNode", [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self) { return value::undefined(); }
        // `{composed: true}` KEEPS GOING through each shadow host; the default
        // stops at the ShadowRoot, which is the whole point of the boundary.
        bool composed = false;
        if (const value options = arg(args, 0); options.is_object()) {
            composed = context::truthy(c.lookup_property(options, "composed"));
        }
        const auto txn = doc_->read();
        const node_id top = root_of_tree(txn, self, composed);
        // THE DOCUMENT IS NOT A WRAPPER. `document` is one object built by
        // install_document, and `node.getRootNode() === document` is the
        // assertion in four of `rootNode.html`'s five cases - so answering with
        // a wrapper for the document node would fail every one of them.
        if (txn.kind(top).value_or(node_kind::element) == node_kind::document) { return document_; }
        return wrap(c, top);
    });
    method("before", [this, parent_of](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id parent = parent_of(self);
        for (const value & one : args) { (void)insert_node(parent, node_from(c, one), self); }
        return value::undefined();
    });
    method("after", [this, parent_of](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id parent = parent_of(self);
        if (!parent) { return value::undefined(); }
        // The reference is the NEXT sibling, and an empty one means "at the
        // end" - which is exactly what insert_node does with an empty handle.
        node_id next;
        {
            const auto txn = doc_->read();
            const auto children = txn.children(parent);
            for (std::size_t i = 0; i + 1 < children.size(); ++i) {
                if (children[i] == self) { next = children[i + 1]; }
            }
        }
        for (const value & one : args) { (void)insert_node(parent, node_from(c, one), next); }
        return value::undefined();
    });
    method("replaceWith", [this, parent_of](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id parent = parent_of(self);
        if (!parent) { return value::undefined(); }
        for (const value & one : args) { (void)insert_node(parent, node_from(c, one), self); }
        (void)doc_->remove_child(self);
        mutated();
        return value::undefined();
    });
    method("replaceChild", [this](context & c, std::span<value> args) {
        const node_id parent = receiver(c);
        const node_id fresh = handle_of(arg(args, 0));
        const node_id stale = handle_of(arg(args, 1));
        if (!parent || !fresh || !stale) { return arg(args, 1); }
        (void)insert_node(parent, fresh, stale);
        (void)doc_->remove_child(stale);
        mutated();
        return arg(args, 1);
    });
    // `cloneNode(deep)` - a DETACHED copy, and without it there is no way at all
    // to duplicate a template, which is how a page builds a list from one row.
    method("cloneNode", [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self) { return value::null(); }
        const bool deep = !args.empty() && context::truthy(args[0]);
        const auto txn = doc_->read();
        return wrap(c, clone_node(txn, self, deep));
    });
    // `contains` INCLUDES THE NODE ITSELF, which is the part that is easy to get
    // wrong: `el.contains(el)` is true in every browser.
    method("contains", [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id other = handle_of(arg(args, 0));
        if (!self || !other) { return value::boolean(false); }
        return value::boolean(doc_->read().is_ancestor_of(self, other));
    });
    method("getBoundingClientRect", [this](context & c, std::span<value>) {
        const rect box = box_of(receiver(c));
        auto * out = static_cast<script::object_object *>(c.make_object().as_heap());
        const auto set = [&](const char * name, float v) {
            out->set(name, value::number(static_cast<double>(v)));
        };
        set("x", box.x);
        set("y", box.y);
        set("left", box.x);
        set("top", box.y);
        set("width", box.width);
        set("height", box.height);
        // right and bottom are DERIVED, and pages read them directly rather
        // than adding the width themselves.
        set("right", box.x + box.width);
        set("bottom", box.y + box.height);
        return value::object(out);
    });
    method("appendChild", [this](context & c, std::span<value> args) {
        // THROUGH insert_node, which is where a DocumentFragment is flattened:
        // appending one must move its children and leave the fragment behind.
        const node_id parent = receiver(c);
        const node_id child = handle_of(arg(args, 0));
        if (!pre_insert_valid(c, parent, child, arg(args, 0), value::null())) {
            return value::undefined();
        }
        (void)insert_node(parent, child, node_id{});
        return arg(args, 0);
    });
    // "If child's parent is not this, then throw a NotFoundError" - DOM
    // §4.2.3. Removing a node from an element that is not its parent used to
    // succeed and remove it from wherever it actually was, which is a silent
    // corruption of the caller's tree rather than a refused operation.
    method("removeChild", [this](context & c, std::span<value> args) {
        const node_id child = handle_of(arg(args, 0));
        const node_id parent = receiver(c);
        if (!child) {
            c.throw_error("TypeError", "removeChild: the argument is not a Node");
            return value::undefined();
        }
        {
            const auto txn = doc_->read();
            if (txn.parent(child) != parent) {
                throw_dom_exception(c, "NotFoundError",
                                    "removeChild: the node is not a child of this one");
                return value::undefined();
            }
        }
        (void)doc_->remove_child(child);
        mutated();
        return arg(args, 0);
    });
    // `matches` and `closest`, DEFINED IN TERMS OF THE SAME MATCHER
    // `querySelectorAll` uses, so neither can be right about a selector the
    // other is wrong about. That matcher is now `style::engine::select` - the
    // one the cascade runs - rather than the hand-rolled compound matcher
    // `query()` used to be, which gave up on any selector containing a space.
    //
    // BOTH ARE STILL O(document) PER CALL, because they ask `query()` for every
    // match in the tree and then look for this element in the answer. That is
    // the shape to fix next: matching ONE element needs the traversal cursor
    // for its ancestor chain and nothing else.
    method("matches", [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self) { return value::boolean(false); }
        const std::vector<node_id> found = query(arg_string(c, args, 0), node_id{});
        return value::boolean(std::find(found.begin(), found.end(), self) != found.end());
    });
    method("closest", [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self) { return value::null(); }
        const std::vector<node_id> found = query(arg_string(c, args, 0), node_id{});
        const auto txn = doc_->read();
        // INCLUSIVE, and upward: the element itself is the first candidate.
        for (node_id at = self; at; at = txn.parent(at)) {
            if (std::find(found.begin(), found.end(), at) != found.end()) { return wrap(c, at); }
        }
        return value::null();
    });
    method("addEventListener", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (id) { add_listener(make_listener(c, path_step{id, listen_on::node}, args)); }
        return value::undefined();
    });
    // The other half. The WINDOW could remove a listener and an element could
    // not, so a page that tidied up after itself - which p5's Element does when
    // it is removed - threw instead. (The comment here used to say the document
    // could too. It could not, and that was found the same way, one corpus
    // later: see install_document.)
    // `element.dispatchEvent(event)` - the third of the three EventTarget
    // methods, and the one that was missing. addEventListener and
    // removeEventListener were here; nothing a page constructed could ever be
    // sent anywhere, so a page could only ever RECEIVE events the engine made.
    //
    // It returns whether the event was NOT cancelled, which is the opposite of
    // what the internal dispatch reports and is how a caller learns that a
    // listener refused the default action.
    method("dispatchEvent", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        const value event = arg(args, 0);
        if (!id || !event.is_object()) { return value::boolean(true); }
        return value::boolean(!dispatch_to(event, path_step{id, listen_on::node}));
    });
    method("removeEventListener", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        const std::string type = arg_string(c, args, 0);
        const value callback = arg(args, 1);
        // THE CAPTURE FLAG IS PART OF THE IDENTITY - (type, callback, capture)
        // is what the DOM says a listener IS, and only `add_listener` enforced
        // it. So `removeEventListener(t, f)` took a CAPTURING listener away and
        // `removeEventListener(t, f, true)` failed to, which is both subtests of
        // `EventListenerOptions-capture.html`. Reading the third argument is
        // also how a page detects that the option is supported at all.
        //
        // ...and `l.on` was not checked either, so an element wrapper whose id
        // failed to resolve matched `l.target == node_id{}` and could remove the
        // DOCUMENT's and the WINDOW's listeners.
        const value options = arg(args, 2);
        const bool capture = options.is_object()
                                 ? context::truthy(c.lookup_property(options, "capture"))
                                 : context::truthy(options);
        std::erase_if(listeners_, [&](const listener & l) {
            return l.on == listen_on::node && l.target == id && l.type == type &&
                   l.capture == capture && l.callback.bits() == callback.bits();
        });
        return value::undefined();
    });

    // --- form controls -------------------------------------------------
    method("getValue", [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        if (!id) { return c.string(std::string{}); }
        const auto txn = doc_->read();
        return c.string(forms_->state_of(txn, *atoms_, id).value);
    });
    method("setValue", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const auto txn = doc_->read();
        control_state & control = forms_->state_of(txn, *atoms_, id);
        control.value = arg_string(c, args, 0);
        control.caret = control.value.size();
        control.selection = control.caret;
        control.value_edited = true;
        mutated();
        return value::undefined();
    });
    method("isChecked", [this](context & c, std::span<value>) {
        const node_id id = receiver(c);
        if (!id) { return value::boolean(false); }
        const auto txn = doc_->read();
        return value::boolean(forms_->state_of(txn, *atoms_, id).checked);
    });
    method("setChecked", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        if (!id) { return value::undefined(); }
        const auto txn = doc_->read();
        forms_->state_of(txn, *atoms_, id).checked = context::truthy(arg(args, 0));
        mutated();
        return value::undefined();
    });
    method("focus", [this](context & c, std::span<value>) {
        if (const node_id id = receiver(c); id && on_focus_) { on_focus_(id); }
        return value::undefined();
    });
    method("blur", [this](context &, std::span<value>) {
        if (on_focus_) { on_focus_(node_id{}); }
        return value::undefined();
    });

    // --- canvas --------------------------------------------------------
    method("getContext", [this](context & c, std::span<value> args) {
        const node_id id = receiver(c);
        const std::string kind = arg_string(c, args, 0);
        // "2d" and "webgl". `webgl2` IS NOT IMPLEMENTED AND RETURNS NULL, and
        // getting that right took two wrong answers.
        //
        // It first threw for the whole WebGL family, on the grounds that a null
        // was the silent-wrong-answer shape: p5 would take it, fall back to its
        // 2D renderer, and a WEBGL sketch would draw nothing 3D while reporting
        // nothing. A blank canvas and a clear conscience.
        //
        // When a real context arrived, `webgl2` kept throwing - and the comment
        // here claimed the throw was what made p5 fall back to `webgl`. That was
        // backwards, and measurably so. p5's RendererGL asks for `webgl2` first
        // and relies on `getContext(...) || getContext('webgl')`, so it needs a
        // FALSY VALUE to fall through. It catches nothing, so the throw escaped
        // the constructor, escaped createCanvas, and left the sketch on the
        // Renderer2D it already had - which is precisely the outcome the throw
        // was supposed to prevent.
        //
        // Null is also simply what the specification says: an unsupported
        // context id returns null, and feature detection is BUILT on that. It is
        // a documented "not supported" signal rather than a plausible wrong
        // answer, which is the distinction the loud-failure rule turns on.
        if (kind == "webgl" || kind == "experimental-webgl") {
            if (!id) { return value::null(); }
            return webgl_context_object(c, id, 1);
        }
        // `webgl2` RETURNS A CONTEXT NOW (2026-08-02). Everything above is the
        // history of it returning null, and every word of it was right at the
        // time; what changed is that the language and the two capabilities
        // behind it exist - see docs/history/webgl2.md stages 1 to 3.
        //
        // p5's RendererGL asks for this FIRST, so from here on every p5 WEBGL
        // sketch takes a path it has never taken in this engine.
        // examples/pages/p5-webgl.html's golden is the tripwire: the same
        // sketch must produce the same pixels through either context, and if
        // that image moves, this path is wrong rather than new.
        if (kind == "webgl2") {
            if (!id) { return value::null(); }
            return webgl_context_object(c, id, 2);
        }
        if (!id || kind != "2d") { return value::null(); }
        return canvas_context_object(c, id);
    });

    // `canvas.toDataURL()` and `canvas.toBlob()` - READING A CANVAS BACK OUT.
    //
    // Both mean PNG: that is what p5's save() asks for, and encode_png writes one
    // with no compression library (see shell/image/images.hpp). A `type` argument
    // naming anything else still gets PNG rather than a lie about the format -
    // the data URL says image/png, so a page that reads it back is not misled.
    const auto canvas_bytes = [this](context & c) -> std::vector<std::byte> {
        const node_id id = receiver(c);
        if (!id || canvases_ == nullptr) { return {}; }
        // context_for, not pixels_of: a canvas nobody asked getContext of has no
        // surface yet, and a browser still gives you a transparent PNG of the
        // right size rather than nothing. An empty answer here would look like a
        // broken encoder.
        const auto number = [&](std::string_view name, int fallback) {
            const auto txn = doc_->read();
            const std::string_view text = txn.attribute_value(id, atoms_->intern(name));
            int out = 0;
            bool any = false;
            for (const char digit : text) {
                if (digit < '0' || digit > '9') { break; }
                out = out * 10 + (digit - '0');
                any = true;
            }
            return any ? out : fallback;
        };
        (void)canvases_->context_for(id, number("width", 300), number("height", 150));
        const std::shared_ptr<const paint::bitmap> pixels = canvases_->pixels_of(id);
        return pixels ? encode_png(*pixels) : std::vector<std::byte>{};
    };
    method("toDataURL", [canvas_bytes](context & c, std::span<value>) {
        const std::vector<std::byte> png = canvas_bytes(c);
        std::string binary;
        binary.reserve(png.size());
        for (const std::byte b : png) { binary += static_cast<char>(b); }
        // Through the standard library's own btoa, so ONE base64 encoder decides
        // what this means here.
        const value encoder = c.global("btoa");
        if (!encoder.is_callable()) { return c.string("data:image/png;base64,"); }
        const value text = c.string(binary);
        const value args[1] = {text};
        return c.string("data:image/png;base64," + c.to_string(c.call(encoder, args)));
    });
    method("toBlob", [this, canvas_bytes](context & c, std::span<value> args) {
        const value callback = arg(args, 0);
        if (!callback.is_callable()) { return value::undefined(); }
        std::vector<std::byte> png = canvas_bytes(c);
        auto * blob = static_cast<script::object_object *>(c.make_object().as_heap());
        value bytes = c.make_array();
        auto * out = static_cast<script::array_object *>(bytes.as_heap());
        out->elements = script::element_kind::u8;
        out->items.reserve(png.size());
        for (const std::byte b : png) {
            out->items.push_back(value::number(static_cast<double>(static_cast<unsigned char>(b))));
        }
        blob->set("size", value::number(static_cast<double>(png.size())));
        blob->set("type", c.string("image/png"));
        blob->set("__bytes", bytes);
        if (blob_prototype_.is_object()) {
            blob->prototype = blob_prototype_; // so `x instanceof Blob` is true
        }
        // QUEUED, not called: toBlob is asynchronous, and a page that wraps it in
        // a promise - which is what p5's p5.Image.toBlob does - depends on the
        // callback landing after the call returns.
        const value blob_value = value::object(blob);
        c.queue_microtask(callback, std::vector<value>{blob_value});
        return value::undefined();
    });
}

std::shared_ptr<const paint::bitmap> dom_bindings::image_argument(value v) {
    if (images_ == nullptr) { return nullptr; }
    if (v.is_number()) { return images_->at(static_cast<int>(context::to_number(v))); }
    if (!v.is_object()) { return nullptr; }
    auto * wrapper = static_cast<script::object_object *>(v.as_heap());
    const value * handle = wrapper->find(handle_property);
    if (handle == nullptr || assets_ == nullptr) { return nullptr; }
    const node_id id = unpack(static_cast<std::uint64_t>(context::to_number(*handle)));
    const auto txn = doc_->read();
    // A CANVAS IS AN IMAGE SOURCE. `drawImage(otherCanvas, ...)` is how a page
    // composites one surface onto another, and it is what p5's `image(g, ...)`
    // does with a createGraphics - so an offscreen buffer drew nothing at all,
    // silently, because a canvas has no `src` to load and this returned null.
    if (canvases_ != nullptr && atoms_->text(txn.tag(id).value_or(atom{})) == "canvas") {
        return canvases_->pixels_of(id);
    }
    const std::string_view src = txn.attribute_value(id, atoms_->intern("src"));
    return src.empty() ? nullptr : images_->load(*assets_, src);
}

// The wrapper for a node IF ONE EXISTS. Deliberately does not create one: this
// is on the dispatch path for every event, and making a wrapper per node per
// event would build the whole document's worth of them for a mousemove.
value dom_bindings::value_of_wrapper(node_id id) const {
    if (!id) { return value::undefined(); }
    const auto it = wrappers_.find(pack(id));
    return it == wrappers_.end() || it->second == nullptr ? value::undefined()
                                                          : value::object(it->second);
}

// --- REFLECTION, AND THE INTERFACE OBJECTS THE ACCESSORS LIVE ON ------------
//
// "Reflecting content attributes in IDL attributes" is HTML section 2.6, and it
// is one paragraph per TYPE and a table per element - which is exactly the shape
// it has here. Before this there were twelve names (`id`, `className`, `href`,
// `download`, `target`, `rel`, `alt`, `title`, `name`, `placeholder`, `type`,
// `htmlFor`) installed as string accessors on EVERY wrapper, so `div.href`
// existed, `input.maxLength` did not, `details.open` was a string rather than a
// boolean, and `td.colSpan` was undefined. `html/dom` counted the cost:
// 2,411 subtests reading `undefined` where a string belongs, 665 where a
// boolean does and 576 where a number does.
//
// TWO THINGS CHANGED TOGETHER, and they are one change. The rules are a table,
// and the table's first column is an INTERFACE - so the accessors go on
// `HTMLInputElement.prototype` rather than on each input, which is both what the
// specification says and the only way ~270 of them are affordable. Building
// those prototypes is the other half of the work, and it is what
// `el instanceof HTMLBodyElement` and `eventTarget.constructor.name` ask for.
namespace {

// WHICH RULE OF SECTION 2.6 A ROW FOLLOWS. Every one of these is a separate
// paragraph in the specification with its own parse, its own default and, for
// three of them, its own way of throwing.
enum class reflect_type : std::uint8_t {
    dom_string,             // 2.6.1: the value, or "" when absent
    url,                    // 2.6.2: resolved against the document's address
    boolean,                // 2.6.4: PRESENCE, not value
    signed_long,            // 2.6.6: rules for parsing integers
    unsigned_long,          // 2.6.8: rules for parsing non-negative integers
    limited_long,           // 2.6.7: non-negative; setting a negative throws
    limited_unsigned_long,  // 2.6.9: greater than zero; setting zero throws
    unsigned_long_fallback, // 2.6.10: as above, but a bad set writes the default
    clamped_unsigned_long,  // 2.6.11: parsed then clamped into [low, high]
    enumerated,             // 2.6.5: limited to only known values
    // A NULLABLE DOMString: `null` when the attribute is absent rather than "",
    // and setting `null` or `undefined` REMOVES it rather than writing the four
    // or nine characters. It is the shape every `aria-*` property and `role`
    // have, and it is the one thing `dom_string` above cannot say.
    nullable_dom_string,
    // 2.6.5 AND NULLABLE AT ONCE, which is `crossOrigin` and nothing else here:
    // limited to known keywords, but the MISSING value default is `null` rather
    // than a keyword, so `typeof img.crossOrigin` is "object" on an element
    // that has no `crossorigin` attribute and a string on one that has. An
    // INVALID value is still a keyword - `crossorigin=x` is "anonymous" - so
    // the two defaults genuinely differ in type and neither `enumerated` nor
    // `nullable_dom_string` can spell it.
    nullable_enumerated
};

// ONE REFLECTED IDL ATTRIBUTE. The four columns the plan asked for - interface,
// IDL name, content attribute, type - plus the defaults, which are
// per-attribute rather than per-type: `input.size` defaults to 20,
// `textarea.rows` to 2 and `td.colSpan` to 1, and there is nowhere else to put
// that.
//
// THE CASE MAPPING IS DATA, NOT A TRANSFORMATION. `htmlFor` is `for`,
// `acceptCharset` is `accept-charset`, `httpEquiv` is `http-equiv`,
// `defaultValue` is `value` and `defaultMuted` is `muted`: no rule relates the
// two spellings, so the row carries both.
struct reflected_attribute {
    std::string_view interface;
    std::string_view idl;
    std::string_view content;
    reflect_type type = reflect_type::dom_string;
    // The missing value default for the numeric types, and the clamp range for
    // `clamped_unsigned_long`.
    long long fallback = 0;
    long long low = 0;
    long long high = 0;
    // SPACE-SEPARATED, for `enumerated`. None of HTML's keywords contains a
    // space - the longest is `application/x-www-form-urlencoded` - so one
    // string_view holds the whole set and the table stays one line per row.
    //
    // The empty-string keyword `referrerPolicy` has is NOT listed, and does not
    // need to be: its invalid value default is "" as well, so a value matching
    // no keyword answers "" whether or not "" is one of them. That is only true
    // where the two coincide - `input.formMethod` has "" for its MISSING value
    // default and "get" for its invalid one, and `formmethod=""` is "get".
    std::string_view keywords;
    std::string_view missing; // the missing value default
    std::string_view invalid; // the invalid value default
};

constexpr reflected_attribute text_attr(std::string_view iface, std::string_view idl,
                                        std::string_view content = {}) {
    return {iface, idl, content.empty() ? idl : content, reflect_type::dom_string, 0, 0, 0, {},
            {},    {}};
}
// The ARIA shape, and the only place a nullable DOMString appears: the content
// attribute is always the IDL name in another spelling, so it is spelled out
// rather than derived - `ariaAutoComplete` is `aria-autocomplete` and
// `ariaBrailleRoleDescription` is `aria-brailleroledescription`, and no rule
// relates the two.
constexpr reflected_attribute aria_attr(std::string_view idl, std::string_view content) {
    return {"Element", idl, content, reflect_type::nullable_dom_string, 0, 0, 0, {}, {}, {}};
}
constexpr reflected_attribute url_attr(std::string_view iface, std::string_view idl,
                                       std::string_view content = {}) {
    return {iface, idl, content.empty() ? idl : content, reflect_type::url, 0, 0, 0, {}, {}, {}};
}
constexpr reflected_attribute bool_attr(std::string_view iface, std::string_view idl,
                                        std::string_view content = {}) {
    return {iface, idl, content.empty() ? idl : content, reflect_type::boolean, 0, 0, 0, {},
            {},    {}};
}
constexpr reflected_attribute long_attr(std::string_view iface, std::string_view idl,
                                        long long fallback = 0, std::string_view content = {}) {
    return {
        iface, idl, content.empty() ? idl : content, reflect_type::signed_long, fallback, 0, 0, {},
        {},    {}};
}
constexpr reflected_attribute ulong_attr(std::string_view iface, std::string_view idl,
                                         long long fallback = 0, std::string_view content = {}) {
    return {iface,
            idl,
            content.empty() ? idl : content,
            reflect_type::unsigned_long,
            fallback,
            0,
            0,
            {},
            {},
            {}};
}
constexpr reflected_attribute limited_long_attr(std::string_view iface, std::string_view idl,
                                                std::string_view content = {}) {
    return {iface, idl, content.empty() ? idl : content, reflect_type::limited_long, -1, 0, 0, {},
            {},    {}};
}
constexpr reflected_attribute limited_ulong_attr(std::string_view iface, std::string_view idl,
                                                 long long fallback,
                                                 std::string_view content = {}) {
    return {iface,
            idl,
            content.empty() ? idl : content,
            reflect_type::limited_unsigned_long,
            fallback,
            0,
            0,
            {},
            {},
            {}};
}
constexpr reflected_attribute fallback_ulong_attr(std::string_view iface, std::string_view idl,
                                                  long long fallback,
                                                  std::string_view content = {}) {
    return {iface,
            idl,
            content.empty() ? idl : content,
            reflect_type::unsigned_long_fallback,
            fallback,
            0,
            0,
            {},
            {},
            {}};
}
constexpr reflected_attribute clamped_attr(std::string_view iface, std::string_view idl,
                                           long long fallback, long long low, long long high,
                                           std::string_view content = {}) {
    return {iface,
            idl,
            content.empty() ? idl : content,
            reflect_type::clamped_unsigned_long,
            fallback,
            low,
            high,
            {},
            {},
            {}};
}
constexpr reflected_attribute enum_attr(std::string_view iface, std::string_view idl,
                                        std::string_view keywords, std::string_view missing,
                                        std::string_view invalid, std::string_view content = {}) {
    return {iface,
            idl,
            content.empty() ? idl : content,
            reflect_type::enumerated,
            0,
            0,
            0,
            keywords,
            missing,
            invalid};
}
// The nullable spelling of the same rule. There is no `missing` column because
// the missing value default IS null - that is what makes the type - and the
// invalid value default is always a keyword.
constexpr reflected_attribute nullable_enum_attr(std::string_view iface, std::string_view idl,
                                                 std::string_view keywords,
                                                 std::string_view invalid,
                                                 std::string_view content = {}) {
    return {iface,
            idl,
            content.empty() ? idl : content,
            reflect_type::nullable_enumerated,
            0,
            0,
            0,
            keywords,
            {},
            invalid};
}

// The keyword sets that appear on more than one interface, named once so the
// table cannot spell one of them differently from the other.
constexpr std::string_view referrer_keywords =
    "no-referrer no-referrer-when-downgrade same-origin origin strict-origin "
    "origin-when-cross-origin strict-origin-when-cross-origin unsafe-url";
// CORS, which four interfaces share and which is the one non-tentative user of
// the nullable enumerated type: absent is `null`, `anonymous` and
// `use-credentials` are the keywords, and anything else - including the empty
// string, which is what `<img crossorigin>` parses to - is `anonymous`.
constexpr std::string_view cors_keywords = "anonymous use-credentials";
constexpr std::string_view enctype_keywords =
    "application/x-www-form-urlencoded multipart/form-data text/plain";
constexpr std::string_view default_enctype = "application/x-www-form-urlencoded";

// THE TABLE. Built from what the corpus tests: `html/dom/elements-*.js` in the
// WPT checkout enumerate, per element, every reflected attribute and its type,
// and the eight `reflection-*.html` files run that product.
//
// WHAT IS DELIBERATELY NOT HERE, and why:
//   * `width` and `height` on every element. The engine already answers those
//     three different ways - the canvas's drawing buffer, an <img>'s decoded
//     size, and a generic numeric accessor for any element carrying either
//     attribute - all as OWN accessors on the wrapper, which shadow anything on
//     a prototype. A row here would be dead code that reads as if it worked.
//   * `value` on a control and `src` on an <img>, for the same reason: both
//     have live own accessors, and a control's value is not its attribute.
//   * `relList`, `sandbox`, `output.htmlFor`, `link.sizes` - the token lists.
//     `classList` exists as its own object; the rest need a real DOMTokenList,
//     which is an object type rather than a table row.
//   * `document.dir`, which is on the document object rather than on an element
//     interface.
//   * `meter`'s six doubles and `progress.max`: `limited double` is a type
//     nothing else uses and the elements have no behaviour behind it here.
constexpr reflected_attribute reflection_table[] = {
    // --- Element: on everything, including SVG and a page-invented namespace
    text_attr("Element", "id"),
    text_attr("Element", "className", "class"),
    text_attr("Element", "slot"),

    // --- ARIA, WHICH IS ALSO ON EVERYTHING and is the same six lines of rule
    // --- applied forty-one more times.
    //
    // `role` and every `aria-*` content attribute reflect as an IDL attribute
    // on Element (ARIA 1.3 §9, "Reflection"), and they are NULLABLE where every
    // row above is not: an absent one reads `null` rather than "", and writing
    // `null` or `undefined` REMOVES it. `aria-attribute-reflection.html` runs
    // `testNullable` on every one of them, so a row that answered "" would fail
    // its own subtest twice over.
    //
    // THE ELEMENT-VALUED ONES ARE NOT HERE, on purpose. `ariaLabelledByElements`
    // and its five siblings reflect an IDREF list as an array of ELEMENTS
    // rather than as a string, which needs an explicit-set store on the element
    // and a live lookup per read - a different mechanism, not a different row,
    // and `aria-element-reflection*.html` is what measures it. The strings are
    // a table and the table is what is affordable.
    aria_attr("role", "role"),
    aria_attr("ariaAtomic", "aria-atomic"),
    aria_attr("ariaAutoComplete", "aria-autocomplete"),
    aria_attr("ariaBrailleLabel", "aria-braillelabel"),
    aria_attr("ariaBrailleRoleDescription", "aria-brailleroledescription"),
    aria_attr("ariaBusy", "aria-busy"),
    aria_attr("ariaChecked", "aria-checked"),
    aria_attr("ariaColCount", "aria-colcount"),
    aria_attr("ariaColIndex", "aria-colindex"),
    aria_attr("ariaColIndexText", "aria-colindextext"),
    aria_attr("ariaColSpan", "aria-colspan"),
    aria_attr("ariaCurrent", "aria-current"),
    aria_attr("ariaDescription", "aria-description"),
    aria_attr("ariaDisabled", "aria-disabled"),
    aria_attr("ariaExpanded", "aria-expanded"),
    aria_attr("ariaHasPopup", "aria-haspopup"),
    aria_attr("ariaHidden", "aria-hidden"),
    aria_attr("ariaInvalid", "aria-invalid"),
    aria_attr("ariaKeyShortcuts", "aria-keyshortcuts"),
    aria_attr("ariaLabel", "aria-label"),
    aria_attr("ariaLevel", "aria-level"),
    aria_attr("ariaLive", "aria-live"),
    aria_attr("ariaModal", "aria-modal"),
    aria_attr("ariaMultiLine", "aria-multiline"),
    aria_attr("ariaMultiSelectable", "aria-multiselectable"),
    aria_attr("ariaOrientation", "aria-orientation"),
    aria_attr("ariaPlaceholder", "aria-placeholder"),
    aria_attr("ariaPosInSet", "aria-posinset"),
    aria_attr("ariaPressed", "aria-pressed"),
    aria_attr("ariaReadOnly", "aria-readonly"),
    aria_attr("ariaRelevant", "aria-relevant"),
    aria_attr("ariaRequired", "aria-required"),
    aria_attr("ariaRoleDescription", "aria-roledescription"),
    aria_attr("ariaRowCount", "aria-rowcount"),
    aria_attr("ariaRowIndex", "aria-rowindex"),
    aria_attr("ariaRowIndexText", "aria-rowindextext"),
    aria_attr("ariaRowSpan", "aria-rowspan"),
    aria_attr("ariaSelected", "aria-selected"),
    aria_attr("ariaSetSize", "aria-setsize"),
    aria_attr("ariaSort", "aria-sort"),
    aria_attr("ariaValueMax", "aria-valuemax"),
    aria_attr("ariaValueMin", "aria-valuemin"),
    aria_attr("ariaValueNow", "aria-valuenow"),
    aria_attr("ariaValueText", "aria-valuetext"),

    // --- HTMLElement: the global attributes, which the corpus tests once per
    // --- element and which are therefore worth more than any other rows here.
    text_attr("HTMLElement", "title"),
    text_attr("HTMLElement", "lang"),
    text_attr("HTMLElement", "accessKey", "accesskey"),
    text_attr("HTMLElement", "nonce"),
    enum_attr("HTMLElement", "dir", "ltr rtl auto", "", ""),
    enum_attr("HTMLElement", "enterKeyHint", "enter done go next previous search send", "", "",
              "enterkeyhint"),
    enum_attr("HTMLElement", "inputMode", "none text tel url email numeric decimal search", "", "",
              "inputmode"),
    bool_attr("HTMLElement", "autofocus"),
    bool_attr("HTMLElement", "hidden"),
    // The specification's default here is "0 or -1 depending on whether the
    // element is focusable", which is a SHOULD and which the corpus explicitly
    // declines to test. 0 is what a focusable element reports.
    long_attr("HTMLElement", "tabIndex", 0, "tabindex"),

    // --- text-level semantics
    text_attr("HTMLAnchorElement", "target"),
    text_attr("HTMLAnchorElement", "download"),
    text_attr("HTMLAnchorElement", "ping"),
    text_attr("HTMLAnchorElement", "rel"),
    text_attr("HTMLAnchorElement", "hreflang"),
    text_attr("HTMLAnchorElement", "type"),
    text_attr("HTMLAnchorElement", "coords"),
    text_attr("HTMLAnchorElement", "charset"),
    text_attr("HTMLAnchorElement", "name"),
    text_attr("HTMLAnchorElement", "rev"),
    text_attr("HTMLAnchorElement", "shape"),
    enum_attr("HTMLAnchorElement", "referrerPolicy", referrer_keywords, "", "", "referrerpolicy"),
    url_attr("HTMLAnchorElement", "href"),
    url_attr("HTMLQuoteElement", "cite"),
    text_attr("HTMLDataElement", "value"),
    text_attr("HTMLTimeElement", "dateTime", "datetime"),
    text_attr("HTMLBRElement", "clear"),

    // --- grouping content
    text_attr("HTMLParagraphElement", "align"),
    text_attr("HTMLHRElement", "align"),
    text_attr("HTMLHRElement", "color"),
    text_attr("HTMLHRElement", "size"),
    bool_attr("HTMLHRElement", "noShade", "noshade"),
    bool_attr("HTMLOListElement", "reversed"),
    bool_attr("HTMLOListElement", "compact"),
    long_attr("HTMLOListElement", "start", 1),
    text_attr("HTMLOListElement", "type"),
    bool_attr("HTMLUListElement", "compact"),
    text_attr("HTMLUListElement", "type"),
    long_attr("HTMLLIElement", "value"),
    text_attr("HTMLLIElement", "type"),
    bool_attr("HTMLDListElement", "compact"),
    text_attr("HTMLDivElement", "align"),
    text_attr("HTMLHeadingElement", "align"),
    bool_attr("HTMLMenuElement", "compact"),

    // --- sections
    text_attr("HTMLBodyElement", "text"),
    text_attr("HTMLBodyElement", "link"),
    text_attr("HTMLBodyElement", "vLink", "vlink"),
    text_attr("HTMLBodyElement", "aLink", "alink"),
    text_attr("HTMLBodyElement", "bgColor", "bgcolor"),
    text_attr("HTMLBodyElement", "background"),
    text_attr("HTMLHtmlElement", "version"),

    // --- metadata
    text_attr("HTMLBaseElement", "target"),
    url_attr("HTMLBaseElement", "href"),
    url_attr("HTMLLinkElement", "href"),
    nullable_enum_attr("HTMLLinkElement", "crossOrigin", cors_keywords, "anonymous", "crossorigin"),
    text_attr("HTMLLinkElement", "rel"),
    text_attr("HTMLLinkElement", "media"),
    text_attr("HTMLLinkElement", "integrity"),
    text_attr("HTMLLinkElement", "hreflang"),
    text_attr("HTMLLinkElement", "type"),
    text_attr("HTMLLinkElement", "charset"),
    text_attr("HTMLLinkElement", "rev"),
    text_attr("HTMLLinkElement", "target"),
    enum_attr("HTMLLinkElement", "as",
              "fetch audio document embed font image manifest object report script sharedworker "
              "style track video worker xslt",
              "", ""),
    enum_attr("HTMLLinkElement", "referrerPolicy", referrer_keywords, "", "", "referrerpolicy"),
    text_attr("HTMLMetaElement", "name"),
    text_attr("HTMLMetaElement", "httpEquiv", "http-equiv"),
    text_attr("HTMLMetaElement", "content"),
    text_attr("HTMLMetaElement", "media"),
    text_attr("HTMLMetaElement", "scheme"),
    text_attr("HTMLStyleElement", "media"),
    text_attr("HTMLStyleElement", "type"),

    // --- scripting, edits, interactive
    url_attr("HTMLScriptElement", "src"),
    nullable_enum_attr("HTMLScriptElement", "crossOrigin", cors_keywords, "anonymous",
                       "crossorigin"),
    text_attr("HTMLScriptElement", "type"),
    text_attr("HTMLScriptElement", "charset"),
    text_attr("HTMLScriptElement", "integrity"),
    text_attr("HTMLScriptElement", "event"),
    text_attr("HTMLScriptElement", "htmlFor", "for"),
    bool_attr("HTMLScriptElement", "noModule", "nomodule"),
    bool_attr("HTMLScriptElement", "defer"),
    url_attr("HTMLModElement", "cite"),
    text_attr("HTMLModElement", "dateTime", "datetime"),
    bool_attr("HTMLDetailsElement", "open"),
    bool_attr("HTMLDialogElement", "open"),
    text_attr("HTMLSlotElement", "name"),

    // --- embedded content
    text_attr("HTMLImageElement", "alt"),
    nullable_enum_attr("HTMLImageElement", "crossOrigin", cors_keywords, "anonymous",
                       "crossorigin"),
    text_attr("HTMLImageElement", "srcset"),
    text_attr("HTMLImageElement", "useMap", "usemap"),
    text_attr("HTMLImageElement", "name"),
    text_attr("HTMLImageElement", "align"),
    text_attr("HTMLImageElement", "border"),
    bool_attr("HTMLImageElement", "isMap", "ismap"),
    ulong_attr("HTMLImageElement", "hspace"),
    ulong_attr("HTMLImageElement", "vspace"),
    url_attr("HTMLImageElement", "lowsrc"),
    url_attr("HTMLImageElement", "longDesc", "longdesc"),
    enum_attr("HTMLImageElement", "referrerPolicy", referrer_keywords, "", "", "referrerpolicy"),
    enum_attr("HTMLImageElement", "decoding", "async sync auto", "auto", "auto"),
    url_attr("HTMLIFrameElement", "src"),
    text_attr("HTMLIFrameElement", "srcdoc"),
    text_attr("HTMLIFrameElement", "name"),
    text_attr("HTMLIFrameElement", "align"),
    text_attr("HTMLIFrameElement", "scrolling"),
    text_attr("HTMLIFrameElement", "frameBorder", "frameborder"),
    text_attr("HTMLIFrameElement", "marginHeight", "marginheight"),
    text_attr("HTMLIFrameElement", "marginWidth", "marginwidth"),
    text_attr("HTMLIFrameElement", "width"),
    text_attr("HTMLIFrameElement", "height"),
    url_attr("HTMLIFrameElement", "longDesc", "longdesc"),
    bool_attr("HTMLIFrameElement", "allowFullscreen", "allowfullscreen"),
    enum_attr("HTMLIFrameElement", "referrerPolicy", referrer_keywords, "", "", "referrerpolicy"),
    url_attr("HTMLEmbedElement", "src"),
    text_attr("HTMLEmbedElement", "type"),
    text_attr("HTMLEmbedElement", "align"),
    text_attr("HTMLEmbedElement", "name"),
    text_attr("HTMLEmbedElement", "width"),
    text_attr("HTMLEmbedElement", "height"),
    text_attr("HTMLObjectElement", "type"),
    text_attr("HTMLObjectElement", "name"),
    text_attr("HTMLObjectElement", "useMap", "usemap"),
    text_attr("HTMLObjectElement", "align"),
    text_attr("HTMLObjectElement", "archive"),
    text_attr("HTMLObjectElement", "code"),
    text_attr("HTMLObjectElement", "standby"),
    text_attr("HTMLObjectElement", "codeType", "codetype"),
    text_attr("HTMLObjectElement", "border"),
    bool_attr("HTMLObjectElement", "declare"),
    ulong_attr("HTMLObjectElement", "hspace"),
    ulong_attr("HTMLObjectElement", "vspace"),
    url_attr("HTMLObjectElement", "codeBase", "codebase"),
    text_attr("HTMLObjectElement", "width"),
    text_attr("HTMLObjectElement", "height"),
    text_attr("HTMLParamElement", "name"),
    text_attr("HTMLParamElement", "value"),
    text_attr("HTMLParamElement", "type"),
    text_attr("HTMLParamElement", "valueType", "valuetype"),
    url_attr("HTMLMediaElement", "src"),
    nullable_enum_attr("HTMLMediaElement", "crossOrigin", cors_keywords, "anonymous",
                       "crossorigin"),
    bool_attr("HTMLMediaElement", "autoplay"),
    bool_attr("HTMLMediaElement", "loop"),
    bool_attr("HTMLMediaElement", "controls"),
    bool_attr("HTMLMediaElement", "defaultMuted", "muted"),
    enum_attr("HTMLMediaElement", "preload", "none metadata auto", "auto", "auto"),
    enum_attr("HTMLMediaElement", "loading", "lazy eager", "eager", "eager"),
    url_attr("HTMLVideoElement", "poster"),
    bool_attr("HTMLVideoElement", "playsInline", "playsinline"),
    ulong_attr("HTMLVideoElement", "width"),
    ulong_attr("HTMLVideoElement", "height"),
    url_attr("HTMLSourceElement", "src"),
    text_attr("HTMLSourceElement", "type"),
    text_attr("HTMLSourceElement", "srcset"),
    text_attr("HTMLSourceElement", "sizes"),
    text_attr("HTMLSourceElement", "media"),
    url_attr("HTMLTrackElement", "src"),
    text_attr("HTMLTrackElement", "srclang"),
    text_attr("HTMLTrackElement", "label"),
    bool_attr("HTMLTrackElement", "default"),
    enum_attr("HTMLTrackElement", "kind", "subtitles captions descriptions chapters metadata",
              "subtitles", "metadata"),
    text_attr("HTMLMapElement", "name"),
    text_attr("HTMLAreaElement", "alt"),
    text_attr("HTMLAreaElement", "coords"),
    text_attr("HTMLAreaElement", "shape"),
    text_attr("HTMLAreaElement", "target"),
    text_attr("HTMLAreaElement", "download"),
    text_attr("HTMLAreaElement", "ping"),
    text_attr("HTMLAreaElement", "rel"),
    text_attr("HTMLAreaElement", "hreflang"),
    text_attr("HTMLAreaElement", "type"),
    bool_attr("HTMLAreaElement", "noHref", "nohref"),
    enum_attr("HTMLAreaElement", "referrerPolicy", referrer_keywords, "", "", "referrerpolicy"),
    url_attr("HTMLAreaElement", "href"),

    // --- tabular data
    text_attr("HTMLTableElement", "align"),
    text_attr("HTMLTableElement", "border"),
    text_attr("HTMLTableElement", "frame"),
    text_attr("HTMLTableElement", "rules"),
    text_attr("HTMLTableElement", "summary"),
    text_attr("HTMLTableElement", "bgColor", "bgcolor"),
    text_attr("HTMLTableElement", "cellPadding", "cellpadding"),
    text_attr("HTMLTableElement", "cellSpacing", "cellspacing"),
    text_attr("HTMLTableElement", "width"),
    text_attr("HTMLTableCaptionElement", "align"),
    text_attr("HTMLTableColElement", "align"),
    text_attr("HTMLTableColElement", "ch", "char"),
    text_attr("HTMLTableColElement", "chOff", "charoff"),
    text_attr("HTMLTableColElement", "vAlign", "valign"),
    clamped_attr("HTMLTableColElement", "span", 1, 1, 1000),
    text_attr("HTMLTableColElement", "width"),
    text_attr("HTMLTableSectionElement", "align"),
    text_attr("HTMLTableSectionElement", "ch", "char"),
    text_attr("HTMLTableSectionElement", "chOff", "charoff"),
    text_attr("HTMLTableSectionElement", "vAlign", "valign"),
    text_attr("HTMLTableRowElement", "align"),
    text_attr("HTMLTableRowElement", "ch", "char"),
    text_attr("HTMLTableRowElement", "chOff", "charoff"),
    text_attr("HTMLTableRowElement", "vAlign", "valign"),
    text_attr("HTMLTableRowElement", "bgColor", "bgcolor"),
    text_attr("HTMLTableCellElement", "headers"),
    text_attr("HTMLTableCellElement", "abbr"),
    text_attr("HTMLTableCellElement", "align"),
    text_attr("HTMLTableCellElement", "axis"),
    text_attr("HTMLTableCellElement", "ch", "char"),
    text_attr("HTMLTableCellElement", "chOff", "charoff"),
    text_attr("HTMLTableCellElement", "vAlign", "valign"),
    text_attr("HTMLTableCellElement", "bgColor", "bgcolor"),
    bool_attr("HTMLTableCellElement", "noWrap", "nowrap"),
    text_attr("HTMLTableCellElement", "width"),
    text_attr("HTMLTableCellElement", "height"),
    clamped_attr("HTMLTableCellElement", "colSpan", 1, 1, 1000, "colspan"),
    clamped_attr("HTMLTableCellElement", "rowSpan", 1, 0, 65534, "rowspan"),
    enum_attr("HTMLTableCellElement", "scope", "row col rowgroup colgroup", "", ""),

    // --- forms
    text_attr("HTMLFormElement", "acceptCharset", "accept-charset"),
    text_attr("HTMLFormElement", "name"),
    text_attr("HTMLFormElement", "target"),
    bool_attr("HTMLFormElement", "noValidate", "novalidate"),
    url_attr("HTMLFormElement", "action"),
    enum_attr("HTMLFormElement", "autocomplete", "on off", "on", "on"),
    enum_attr("HTMLFormElement", "enctype", enctype_keywords, default_enctype, default_enctype),
    enum_attr("HTMLFormElement", "encoding", enctype_keywords, default_enctype, default_enctype,
              "enctype"),
    enum_attr("HTMLFormElement", "method", "get post dialog", "get", "get"),
    bool_attr("HTMLFieldSetElement", "disabled"),
    text_attr("HTMLFieldSetElement", "name"),
    text_attr("HTMLLegendElement", "align"),
    text_attr("HTMLLabelElement", "htmlFor", "for"),
    text_attr("HTMLInputElement", "accept"),
    text_attr("HTMLInputElement", "alt"),
    text_attr("HTMLInputElement", "dirName", "dirname"),
    text_attr("HTMLInputElement", "formTarget", "formtarget"),
    text_attr("HTMLInputElement", "max"),
    text_attr("HTMLInputElement", "min"),
    text_attr("HTMLInputElement", "name"),
    text_attr("HTMLInputElement", "pattern"),
    text_attr("HTMLInputElement", "placeholder"),
    text_attr("HTMLInputElement", "step"),
    text_attr("HTMLInputElement", "align"),
    text_attr("HTMLInputElement", "useMap", "usemap"),
    text_attr("HTMLInputElement", "autocomplete"),
    ulong_attr("HTMLInputElement", "width"),
    ulong_attr("HTMLInputElement", "height"),
    text_attr("HTMLInputElement", "defaultValue", "value"),
    bool_attr("HTMLInputElement", "defaultChecked", "checked"),
    bool_attr("HTMLInputElement", "disabled"),
    bool_attr("HTMLInputElement", "multiple"),
    bool_attr("HTMLInputElement", "readOnly", "readonly"),
    bool_attr("HTMLInputElement", "required"),
    bool_attr("HTMLInputElement", "formNoValidate", "formnovalidate"),
    limited_long_attr("HTMLInputElement", "maxLength", "maxlength"),
    limited_long_attr("HTMLInputElement", "minLength", "minlength"),
    limited_ulong_attr("HTMLInputElement", "size", 20),
    url_attr("HTMLInputElement", "src"),
    url_attr("HTMLInputElement", "formAction", "formaction"),
    enum_attr("HTMLInputElement", "formEnctype", enctype_keywords, "", default_enctype,
              "formenctype"),
    enum_attr("HTMLInputElement", "formMethod", "get post", "", "get", "formmethod"),
    enum_attr("HTMLInputElement", "type",
              "hidden text search tel url email password date month week time datetime-local "
              "number range color checkbox radio file submit image reset button",
              "text", "text"),
    text_attr("HTMLButtonElement", "name"),
    text_attr("HTMLButtonElement", "formTarget", "formtarget"),
    bool_attr("HTMLButtonElement", "disabled"),
    bool_attr("HTMLButtonElement", "formNoValidate", "formnovalidate"),
    url_attr("HTMLButtonElement", "formAction", "formaction"),
    enum_attr("HTMLButtonElement", "formEnctype", enctype_keywords, "", default_enctype,
              "formenctype"),
    enum_attr("HTMLButtonElement", "formMethod", "get post dialog", "", "get", "formmethod"),
    enum_attr("HTMLButtonElement", "type", "submit reset button", "submit", "submit"),
    text_attr("HTMLSelectElement", "name"),
    bool_attr("HTMLSelectElement", "disabled"),
    bool_attr("HTMLSelectElement", "multiple"),
    bool_attr("HTMLSelectElement", "required"),
    ulong_attr("HTMLSelectElement", "size", 0),
    text_attr("HTMLOptGroupElement", "label"),
    bool_attr("HTMLOptGroupElement", "disabled"),
    bool_attr("HTMLOptionElement", "disabled"),
    bool_attr("HTMLOptionElement", "defaultSelected", "selected"),
    text_attr("HTMLTextAreaElement", "dirName", "dirname"),
    text_attr("HTMLTextAreaElement", "name"),
    text_attr("HTMLTextAreaElement", "placeholder"),
    text_attr("HTMLTextAreaElement", "wrap"),
    bool_attr("HTMLTextAreaElement", "disabled"),
    bool_attr("HTMLTextAreaElement", "readOnly", "readonly"),
    bool_attr("HTMLTextAreaElement", "required"),
    limited_long_attr("HTMLTextAreaElement", "maxLength", "maxlength"),
    limited_long_attr("HTMLTextAreaElement", "minLength", "minlength"),
    fallback_ulong_attr("HTMLTextAreaElement", "cols", 20),
    fallback_ulong_attr("HTMLTextAreaElement", "rows", 2),
    text_attr("HTMLOutputElement", "name"),

    // --- obsolete, and still measured: the corpus has a whole file of them
    text_attr("HTMLFrameSetElement", "cols"),
    text_attr("HTMLFrameSetElement", "rows"),
    text_attr("HTMLFrameElement", "name"),
    text_attr("HTMLFrameElement", "scrolling"),
    text_attr("HTMLFrameElement", "frameBorder", "frameborder"),
    text_attr("HTMLFrameElement", "marginHeight", "marginheight"),
    text_attr("HTMLFrameElement", "marginWidth", "marginwidth"),
    bool_attr("HTMLFrameElement", "noResize", "noresize"),
    url_attr("HTMLFrameElement", "src"),
    url_attr("HTMLFrameElement", "longDesc", "longdesc"),
    bool_attr("HTMLDirectoryElement", "compact"),
    text_attr("HTMLFontElement", "color"),
    text_attr("HTMLFontElement", "face"),
    text_attr("HTMLFontElement", "size"),
    text_attr("HTMLMarqueeElement", "bgColor", "bgcolor"),
    ulong_attr("HTMLMarqueeElement", "hspace"),
    ulong_attr("HTMLMarqueeElement", "vspace"),
    ulong_attr("HTMLMarqueeElement", "scrollAmount", 6, "scrollamount"),
    ulong_attr("HTMLMarqueeElement", "scrollDelay", 85, "scrolldelay"),
    bool_attr("HTMLMarqueeElement", "trueSpeed", "truespeed"),
    text_attr("HTMLMarqueeElement", "width"),
    text_attr("HTMLMarqueeElement", "height"),
    enum_attr("HTMLMarqueeElement", "behavior", "scroll slide alternate", "scroll", "scroll"),
    enum_attr("HTMLMarqueeElement", "direction", "up right down left", "left", "left"),
};

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

// Is `want` one of the space-separated tokens of `list`? The table's keyword
// sets and its tag lists are both encoded that way.
[[nodiscard]] constexpr bool lists_token(std::string_view list, std::string_view want) {
    if (want.empty()) { return false; }
    std::size_t at = 0;
    while (at <= list.size()) {
        const std::size_t end = list.find(' ', at);
        const std::size_t stop = end == std::string_view::npos ? list.size() : end;
        if (list.substr(at, stop - at) == want) { return true; }
        if (end == std::string_view::npos) { return false; }
        at = end + 1;
    }
    return false;
}

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

// ...and the question the wrapper asks before it installs its own pair. The
// INHERITED rows count: `width` is on HTMLMediaElement, so a <video> has one
// even though no row names HTMLVideoElement. Walked rather than cached because
// it is asked only of an element that carries a width or height attribute.
[[nodiscard]] bool interface_reflects_size(std::string_view tag) {
    constexpr std::size_t count = std::size(interface_table);
    for (std::size_t at = interface_for_tag(tag); at < count;
         at = interface_index(interface_table[at].parent)) {
        for (const reflected_attribute & row : reflection_table) {
            if (row.idl == "width" && interface_index(row.interface) == at) { return true; }
        }
    }
    return false;
}

// THE RULES FOR PARSING INTEGERS, HTML 2.4.4.1, which the numeric reflection
// types are all defined in terms of. Answers false when there is no integer
// there at all, which is what makes the attribute's default apply.
//
// The whitespace set is HTML's five characters and NOT JavaScript's -
// `core/algorithms.hpp` takes the set as a parameter for exactly this reason.
// The corpus tests a vertical tab in front of a digit among twenty other
// spacings and expects it to FAIL, because a vertical tab is not HTML
// whitespace and `\v7` is therefore not an integer.
[[nodiscard]] bool parse_html_integer(std::string_view text, long long & out) {
    std::size_t at = 0;
    while (at < text.size() && html_whitespace.find(text[at]) != std::string_view::npos) { ++at; }
    long long sign = 1;
    if (at < text.size() && (text[at] == '-' || text[at] == '+')) {
        sign = text[at] == '-' ? -1 : 1;
        ++at;
    }
    if (at >= text.size() || text[at] < '0' || text[at] > '9') { return false; }
    long long digits = 0;
    bool too_big = false;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
        // CAPPED RATHER THAN WRAPPED. A page can write a hundred digits in an
        // attribute, and signed overflow is undefined behaviour rather than a
        // large number. Anything past 2^32 is out of range for every type here.
        if (digits > 4294967296LL) {
            too_big = true;
        } else {
            digits = digits * 10 + (text[at] - '0');
        }
        ++at;
    }
    if (too_big) { return false; }
    out = sign * digits;
    return true;
}

// ToUint32 and ToInt32 (ECMA-262 7.1.6/7.1.7), which is what WebIDL's
// `unsigned long` and `long` do to whatever a page assigns. `el.tabIndex = 1e30`
// wraps; it does not clamp and it does not throw.
[[nodiscard]] long long to_uint32(double x) {
    if (!std::isfinite(x)) { return 0; }
    double wrapped = std::fmod(std::trunc(x), 4294967296.0);
    if (wrapped < 0) { wrapped += 4294967296.0; }
    return static_cast<long long>(wrapped);
}

[[nodiscard]] long long to_int32(double x) {
    const long long unsigned_value = to_uint32(x);
    return unsigned_value >= 2147483648LL ? unsigned_value - 4294967296LL : unsigned_value;
}

constexpr long long max_int32 = 2147483647;

// --- UTF-16 CODE UNITS OVER UTF-8 BYTES -------------------------------------
//
// EVERY OFFSET IN `CharacterData` IS A UTF-16 CODE UNIT and this engine stores
// UTF-8, so the two are the same number only for ASCII. `CharacterData-*.html`
// tests exactly that difference and tests it twice: once on CJK, where a code
// unit is three bytes, and once on U+1F320, where ONE character is two code
// units and four bytes. A byte offset passes the whole English half of the
// corpus and is wrong for every page that is not in English.
//
// The width of a UTF-8 sequence from its lead byte. A continuation byte or an
// invalid lead counts as one, which keeps this total on any bytes at all - the
// document's text comes from a tokenizer that does not promise well-formedness.
[[nodiscard]] std::size_t utf8_width(unsigned char lead) {
    if (lead < 0x80u) { return 1; }
    if ((lead & 0xE0u) == 0xC0u) { return 2; }
    if ((lead & 0xF0u) == 0xE0u) { return 3; }
    if ((lead & 0xF8u) == 0xF0u) { return 4; }
    return 1;
}

[[nodiscard]] std::size_t utf16_length(std::string_view text) {
    std::size_t units = 0;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t width = utf8_width(static_cast<unsigned char>(text[at]));
        // A character outside the BMP is a SURROGATE PAIR: two code units for
        // the four bytes UTF-8 spends on it, and the only place these two
        // counts diverge.
        units += width == 4 ? 2u : 1u;
        at += width;
    }
    return units;
}

// The byte offset a code-unit offset names. An offset past the end is the end.
//
// AN OFFSET THAT FALLS BETWEEN THE TWO HALVES OF A SURROGATE PAIR resolves to
// the boundary BEFORE it, and that is a deviation said out loud: the DOM lets a
// page split a pair and keep the halves, because a JavaScript string is a
// sequence of code units and a lone surrogate is one of them. UTF-8 cannot hold
// a lone surrogate, so `CharacterData-surrogates.html` - which asserts
// `substringData(1, 8)` yields "\uDF20 test \uD83C" - cannot pass here whatever
// this function does. Rounding down at least keeps the text WELL-FORMED, which
// is the property every other reader of the document relies on.
[[nodiscard]] std::size_t utf16_to_byte(std::string_view text, std::size_t want) {
    std::size_t units = 0;
    std::size_t at = 0;
    while (at < text.size() && units < want) {
        const std::size_t width = utf8_width(static_cast<unsigned char>(text[at]));
        const std::size_t cost = width == 4 ? 2u : 1u;
        if (units + cost > want) { break; }
        units += cost;
        at += width;
    }
    return at;
}

} // namespace

// One reflected attribute's getter: read the content attribute, apply the rule
// its type names. A receiver that does not resolve to an element - a wrapper for
// something removed from the document - answers the type's default rather than
// throwing, which is what every other native in this file does.
value dom_bindings::reflected_get(context & cx, const void * row_ptr) {
    const auto & row = *static_cast<const reflected_attribute *>(row_ptr);
    const node_id id = receiver(cx);
    const auto txn = doc_->read();
    const atom name = atoms_->intern(row.content);
    const bool present = id && txn.has_attribute(id, name);
    const std::string_view raw = present ? txn.attribute_value(id, name) : std::string_view{};
    switch (row.type) {
    case reflect_type::dom_string: return cx.string(std::string{raw});
    // NULL, NOT "", and the difference is the whole of `testNullable`: an
    // absent `aria-label` has no value rather than an empty one, and a page
    // that branches on `el.ariaLabel === null` is asking whether the author
    // wrote one.
    case reflect_type::nullable_dom_string:
        return present ? cx.string(std::string{raw}) : value::null();
    case reflect_type::boolean: return value::boolean(present);
    case reflect_type::url: {
        // "If the content attribute is absent, return the empty string.
        // Otherwise parse it relative to the element's node document and return
        // the resulting URL string. If parsing fails, the value of the content
        // attribute must be returned instead."
        //
        // WHAT THIS ENGINE RESOLVES AGAINST is `location_href_` - the address
        // the browser pushed in through observe_location, which is what
        // `document.URL` and `document.baseURI` already report. It is NOT the
        // `<base href>` element: nothing here reads one, so a page carrying a
        // <base> resolves against the document's own address instead. That is a
        // real difference from a browser and it is the honest one to have -
        // inventing a base URL would be worse than using the document's.
        if (!present) { return cx.string(""); }
        if (location_href_.empty()) { return cx.string(std::string{raw}); }
        const std::string resolved = resolve(location_href_, raw);
        return cx.string(resolved.empty() ? std::string{raw} : resolved);
    }
    case reflect_type::enumerated:
    case reflect_type::nullable_enumerated: {
        const bool nullable = row.type == reflect_type::nullable_enumerated;
        if (!present) { return nullable ? value::null() : cx.string(std::string{row.missing}); }
        // ASCII-INSENSITIVE AND NOTHING WIDER, which is the whole of the
        // corpus's interest in this line: `TRUE` is the keyword `true` and
        // U+212A KELVIN SIGN is not the letter `k`. Every keyword in the table
        // is lower case already, so the folded value IS the canonical spelling.
        const std::string folded = ascii_lower_copy(raw);
        if (lists_token(row.keywords, folded)) { return cx.string(folded); }
        // AN EMPTY VALUE IS AN INVALID ONE. It reads as if it were a state of
        // its own - `<input type="">` - and it is not: the rule is "if the
        // value matches none of the keywords, the invalid value default", and
        // an attribute that is present but empty matches none. Answering ""
        // here made `<input type="">` report "" where "text" belongs, and
        // `<track kind="">` "" where "metadata" does. The rows whose invalid
        // value default is "" - `dir`, `referrerPolicy`, `scope` - are
        // unaffected, which is why this looked right for so long.
        return cx.string(std::string{row.invalid});
    }
    default: break;
    }
    long long parsed = 0;
    const bool ok = present && parse_html_integer(raw, parsed);
    long long answer = row.fallback;
    if (ok) {
        switch (row.type) {
        case reflect_type::signed_long:
            if (parsed >= -2147483648LL && parsed <= max_int32) { answer = parsed; }
            break;
        case reflect_type::unsigned_long:
        case reflect_type::limited_long:
            if (parsed >= 0 && parsed <= max_int32) { answer = parsed; }
            break;
        case reflect_type::limited_unsigned_long:
        case reflect_type::unsigned_long_fallback:
            if (parsed >= 1 && parsed <= max_int32) { answer = parsed; }
            break;
        case reflect_type::clamped_unsigned_long:
            // "If it succeeds but the value is less than min, min must be
            // returned; if greater than max, max." Only a FAILED parse falls
            // back to the default, so `<td colspan=0>` is 1 and
            // `<td colspan=x>` is 1 for two different reasons.
            answer = parsed < row.low ? row.low : (parsed > row.high ? row.high : parsed);
            break;
        default: break;
        }
    }
    return value::number(static_cast<double>(answer));
}

// ...and its setter, which is where the two types that THROW live. Answers
// undefined always: an IDL setter has no return value, and the exception is the
// only channel it has.
value dom_bindings::reflected_set(context & cx, const void * row_ptr, std::span<value> args) {
    const auto & row = *static_cast<const reflected_attribute *>(row_ptr);
    const node_id id = receiver(cx);
    if (!id) { return value::undefined(); }
    const atom name = atoms_->intern(row.content);
    const auto write = [&](std::string text) {
        (void)doc_->set_attribute(id, name, text);
        mutated();
    };
    switch (row.type) {
    case reflect_type::boolean:
        // "The content attribute must be removed if the IDL attribute is set to
        // false, and must be set to the empty string if it is set to true."
        if (!args.empty() && context::truthy(args[0])) {
            write("");
        } else {
            (void)doc_->remove_attribute(id, name);
            mutated();
        }
        return value::undefined();
    case reflect_type::nullable_dom_string:
    case reflect_type::nullable_enumerated:
        // "If the given value is null, remove the content attribute" - so
        // `el.ariaLabel = null` is a removal and not the four characters
        // "null", which is what the ToString below would have written.
        // `undefined` is the same state, which `testNullable` checks by name.
        //
        // A nullable ENUMERATED attribute writes what it is given, exactly as
        // the non-nullable one does: `img.crossOrigin = "ANONYMOUS"` stores
        // those nine capitals and the GETTER is what folds them.
        if (args.empty() || args[0].is_nullish()) {
            (void)doc_->remove_attribute(id, name);
            mutated();
            return value::undefined();
        }
        write(arg_string(cx, args, 0));
        return value::undefined();
    case reflect_type::dom_string:
    case reflect_type::url:
    case reflect_type::enumerated:
        // All three write the ToString of the value verbatim. An enumerated
        // attribute does NOT canonicalise on the way in - the getter is where
        // the keyword table applies - and a URL is stored as given and resolved
        // on the way out.
        write(arg_string(cx, args, 0));
        return value::undefined();
    default: break;
    }
    const double given = arg_number(args, 0);
    long long number =
        row.type == reflect_type::signed_long || row.type == reflect_type::limited_long
            ? to_int32(given)
            : to_uint32(given);
    switch (row.type) {
    case reflect_type::limited_long:
        // "On setting, if the value is negative, the user agent must fire an
        // INDEX_SIZE_ERR exception."
        if (number < 0) {
            throw_dom_exception(cx, "IndexSizeError",
                                std::string{row.idl} + " cannot be set to a negative number");
            return value::undefined();
        }
        break;
    case reflect_type::limited_unsigned_long:
        if (number == 0) {
            throw_dom_exception(cx, "IndexSizeError",
                                std::string{row.idl} + " cannot be set to zero");
            return value::undefined();
        }
        if (number > max_int32) { number = row.fallback; }
        break;
    case reflect_type::unsigned_long_fallback:
        if (number < 1 || number > max_int32) { number = row.fallback; }
        break;
    case reflect_type::unsigned_long:
    case reflect_type::clamped_unsigned_long:
        // A clamped attribute "behaves the same as a regular reflected unsigned
        // integer" on setting: the clamp is a GETTING rule only.
        if (number > max_int32) { number = row.fallback; }
        break;
    default: break;
    }
    write(std::to_string(number));
    return value::undefined();
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
    for (const reflected_attribute & row : reflection_table) {
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

// `isEqualNode`: THE DOM'S STRUCTURAL COMPARISON, DOM 4.4 "equals".
//
// Not identity - that is `isSameNode` - and not a serialisation comparison
// either, which is what a first attempt reaches for and which gets three things
// wrong that this file's own corpus tests by name:
//
//   * ATTRIBUTES ARE AN UNORDERED SET, compared by (namespace, local name,
//     value). Two elements carrying the same attributes in different ORDER are
//     equal, and `innerHTML` would have said no.
//   * A PREFIX TAKES NO PART in comparing an attribute. `setAttributeNS(ns,
//     "prefix:local", v)` and `setAttributeNS(ns, "prefix2:local", v)` are the
//     same attribute and the elements holding them ARE equal - which is the one
//     subtest that a (qualified name, value) comparison fails.
//   * ...but it DOES take part in comparing an ELEMENT, whose qualified name is
//     compared whole. `prefix:localName` and `prefix2:localName` are different
//     elements. The two rules are opposite on purpose and the file asserts both.
//
// Then the children, PAIRWISE AND IN ORDER, which is the recursion.
bool dom_bindings::nodes_are_equal(const read_txn & txn, node_id left, node_id right) const {
    if (!left || !right) { return false; }
    if (left == right) { return true; }
    const node_kind kind = txn.kind(left).value_or(node_kind::element);
    if (kind != txn.kind(right).value_or(node_kind::element)) { return false; }
    switch (kind) {
    case node_kind::element: {
        if (txn.tag(left).value_or(atom{}) != txn.tag(right).value_or(atom{})) { return false; }
        if (namespace_of(left) != namespace_of(right)) { return false; }
        const std::span<const attribute> held = txn.attributes(left);
        if (held.size() != txn.attributes(right).size()) { return false; }
        for (const attribute & one : held) {
            const attribute * match = txn.find_attribute_ns(right, atoms_->text(one.ns),
                                                            attribute_local_name(*atoms_, one));
            if (match == nullptr || match->value != one.value) { return false; }
        }
        break;
    }
    case node_kind::text:
    case node_kind::comment:
        if (txn.text(left) != txn.text(right)) { return false; }
        break;
    // A Document and a DocumentFragment have nothing of their own to compare;
    // they are their children, which is what the walk below does.
    case node_kind::document:
    case node_kind::document_fragment: break;
    }
    const std::span<const node_id> mine = txn.children(left);
    const std::span<const node_id> theirs = txn.children(right);
    if (mine.size() != theirs.size()) { return false; }
    for (std::size_t i = 0; i < mine.size(); ++i) {
        if (!nodes_are_equal(txn, mine[i], theirs[i])) { return false; }
    }
    return true;
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

// --- CharacterData, AND THE Text THAT IS ONE --------------------------------
//
// FOUR NODE TYPES SHARE ONE STRING, and the DOM gives them one interface to
// edit it with: `data`, `length`, and the five methods that are all one
// operation - "replace data", DOM 4.10.2 - with arguments filled in. Written
// once here for that reason: five separate implementations is five chances to
// disagree about which argument throws and which one clamps, and the corpus
// tests that distinction on every one of them.
//
// ON THE PROTOTYPE, not on every wrapper. `install_element_methods` runs per
// node and would have made one `substringData` closure per text node in the
// document; these are one per page, which is what the specification means by
// the operations belonging to an interface. `data` and `nodeValue` stay
// per-wrapper because they close over the node - see install_element_views.
//
// WHAT IS NOT HERE: `ProcessingInstruction` and `CDATASection`. Both are in the
// interface table because a page may name them, and neither is a `node_kind`
// this DOM can produce, so nothing can be an instance of one. When they arrive
// they inherit these methods by being in the chain and nothing here changes.
void dom_bindings::install_character_data(context & cx) {
    const value character_data = interface_prototype("CharacterData");
    const value text_interface = interface_prototype("Text");
    if (!character_data.is_object() || !text_interface.is_object()) { return; }
    auto * proto = static_cast<script::object_object *>(character_data.as_heap());
    auto * text_proto = static_cast<script::object_object *>(text_interface.as_heap());

    const auto native = [&cx](const std::string & name, script::native_fn fn) {
        return value::object(cx.allocate<script::native_object>(name, std::move(fn)));
    };
    // NOT ENUMERABLE, which is what { writable, configurable } spells: an IDL
    // operation is a built-in, and `Body-FrameSet-Event-Handlers.html` counts
    // what a `for...in` over a node reports against the IDL.
    const auto method = [&native](script::object_object & on, const std::string & name,
                                  script::native_fn fn) {
        on.set(name, native(name, std::move(fn)));
        on.set_attrs(name, script::attr_builtin);
    };

    // THE RECEIVER'S TEXT. False when `this` is not a character data node - a
    // wrapper for a node that has since been collected, or one of these methods
    // taken off the prototype and called on an element. Every other native in
    // this file answers the type's default rather than throwing in that case
    // and these do the same: the text is empty and the write is skipped, so
    // nothing is corrupted and nothing throws a LANGUAGE error where the page
    // was told to expect a DOMException.
    const auto data_of = [this](context & c, node_id & id, std::string & text) {
        id = receiver(c);
        if (!id) { return false; }
        const auto txn = doc_->read();
        const node_kind kind = txn.kind(id).value_or(node_kind::element);
        if (kind != node_kind::text && kind != node_kind::comment) { return false; }
        text = std::string{txn.text(id)};
        return true;
    };

    // "REPLACE DATA", DOM 4.10.2. appendData, insertData, deleteData and
    // replaceData are ALL this with arguments filled in, exactly as the
    // specification defines them.
    //
    // THE TWO ARGUMENTS ARE NOT TREATED ALIKE, and that asymmetry is the whole
    // of "with invalid offset" and "with clamped count":
    //
    //   offset  ToUint32'd and then COMPARED. `-1` is 4294967295 and therefore
    //           past the end and therefore an IndexSizeError; `-0x100000000 + 2`
    //           is 2 and is fine; `"test"` is NaN and therefore 0 and is fine.
    //   count   ToUint32'd and then CLAMPED to what is left. `-1` deletes to
    //           the end rather than throwing, and 20 on a four-character node
    //           deletes four.
    const auto replace_data = [this](context & c, node_id id, const std::string & text,
                                     std::string_view where, double offset_arg, double count_arg,
                                     const std::string & with) {
        const auto length = static_cast<unsigned long long>(utf16_length(text));
        const auto offset = static_cast<unsigned long long>(to_uint32(offset_arg));
        if (offset > length) {
            throw_dom_exception(c, "IndexSizeError",
                                std::string{where} + ": offset " + std::to_string(offset) +
                                    " is past the end of " + std::to_string(length) +
                                    " code units");
            return false;
        }
        auto count = static_cast<unsigned long long>(to_uint32(count_arg));
        if (count > length - offset) { count = length - offset; }
        std::string made{text.substr(0, utf16_to_byte(text, static_cast<std::size_t>(offset)))};
        made += with;
        made += text.substr(utf16_to_byte(text, static_cast<std::size_t>(offset + count)));
        (void)doc_->set_text(id, made);
        mutated();
        return true;
    };

    // `length` IS IN CODE UNITS and so is every offset below it. See
    // utf16_length: for ASCII it is the byte count and for nothing else.
    proto->define_accessor("length",
                           native("length",
                                  [data_of](context & c, std::span<value>) {
                                      node_id id;
                                      std::string text;
                                      (void)data_of(c, id, text);
                                      return value::number(static_cast<double>(utf16_length(text)));
                                  }),
                           value::undefined());

    method(*proto, "substringData", [this, data_of](context & c, std::span<value> a) {
        // TWO REQUIRED ARGUMENTS, and the arity TypeError is a subtest by name:
        // `substringData(0)` throws where `substringData(0, 0)` answers "".
        if (a.size() < 2) {
            c.throw_error("TypeError", "substringData needs an offset and a count");
            return value::undefined();
        }
        // CONVERTED FIRST, THEN THE NODE IS READ. WebIDL converts a call's
        // arguments before the operation runs, and a page can tell: a `toString`
        // on an argument may edit the very node this is about to measure.
        const auto offset = static_cast<unsigned long long>(to_uint32(c.to_number_value(a[0])));
        auto count = static_cast<unsigned long long>(to_uint32(c.to_number_value(a[1])));
        node_id id;
        std::string text;
        (void)data_of(c, id, text);
        const auto length = static_cast<unsigned long long>(utf16_length(text));
        if (offset > length) {
            throw_dom_exception(c, "IndexSizeError",
                                "substringData: offset " + std::to_string(offset) +
                                    " is past the end of " + std::to_string(length) +
                                    " code units");
            return value::undefined();
        }
        if (count > length - offset) { count = length - offset; }
        const std::size_t start = utf16_to_byte(text, static_cast<std::size_t>(offset));
        const std::size_t stop = utf16_to_byte(text, static_cast<std::size_t>(offset + count));
        return c.string(text.substr(start, stop - start));
    });

    method(*proto, "appendData", [data_of, replace_data](context & c, std::span<value> a) {
        if (a.empty()) {
            c.throw_error("TypeError", "appendData needs the data to append");
            return value::undefined();
        }
        const std::string with = c.to_string(a[0]);
        node_id id;
        std::string text;
        if (!data_of(c, id, text)) { return value::undefined(); }
        // AT THE END, WHICH CANNOT THROW: the offset IS the length.
        (void)replace_data(c, id, text, "appendData", static_cast<double>(utf16_length(text)), 0.0,
                           with);
        return value::undefined();
    });

    method(*proto, "insertData", [data_of, replace_data](context & c, std::span<value> a) {
        if (a.size() < 2) {
            c.throw_error("TypeError", "insertData needs an offset and the data to insert");
            return value::undefined();
        }
        // IN ARGUMENT ORDER, INTO NAMED LOCALS, AND BEFORE THE NODE IS READ.
        // WebIDL converts a call's arguments left to right and a page can SEE
        // that order - the corpus asserts it with a `toString` that records
        // when it ran - while the order C++ evaluates a call's own arguments in
        // is unspecified. Reading the node afterwards matters for the same
        // reason: a `toString` may have edited it.
        const double offset = c.to_number_value(a[0]);
        const std::string with = c.to_string(a[1]);
        node_id id;
        std::string text;
        if (!data_of(c, id, text)) { return value::undefined(); }
        (void)replace_data(c, id, text, "insertData", offset, 0.0, with);
        return value::undefined();
    });

    method(*proto, "deleteData", [data_of, replace_data](context & c, std::span<value> a) {
        if (a.size() < 2) {
            c.throw_error("TypeError", "deleteData needs an offset and a count");
            return value::undefined();
        }
        const double offset = c.to_number_value(a[0]);
        const double count = c.to_number_value(a[1]);
        node_id id;
        std::string text;
        if (!data_of(c, id, text)) { return value::undefined(); }
        (void)replace_data(c, id, text, "deleteData", offset, count, std::string{});
        return value::undefined();
    });

    method(*proto, "replaceData", [data_of, replace_data](context & c, std::span<value> a) {
        if (a.size() < 3) {
            c.throw_error("TypeError", "replaceData needs an offset, a count and the data");
            return value::undefined();
        }
        const double offset = c.to_number_value(a[0]);
        const double count = c.to_number_value(a[1]);
        const std::string with = c.to_string(a[2]);
        node_id id;
        std::string text;
        if (!data_of(c, id, text)) { return value::undefined(); }
        (void)replace_data(c, id, text, "replaceData", offset, count, with);
        return value::undefined();
    });

    // --- Text, WHICH IS CharacterData PLUS TWO -----------------------------

    // `splitText(offset)`: this node keeps the head, a NEW node takes the tail
    // and goes straight after it. The new node has NO PARENT when this one has
    // none - "Split root" asserts exactly that - which is why the insertion is
    // conditional rather than the obvious appendChild.
    method(*text_proto, "splitText", [this, data_of](context & c, std::span<value> a) {
        const auto offset =
            static_cast<unsigned long long>(to_uint32(c.to_number_value(arg(a, 0))));
        node_id id;
        std::string text;
        if (!data_of(c, id, text)) { return value::null(); }
        const auto length = static_cast<unsigned long long>(utf16_length(text));
        if (offset > length) {
            throw_dom_exception(c, "IndexSizeError",
                                "splitText: offset " + std::to_string(offset) +
                                    " is past the end of " + std::to_string(length) +
                                    " code units");
            return value::null();
        }
        const std::size_t at = utf16_to_byte(text, static_cast<std::size_t>(offset));
        const node_id made = doc_->create_text(text.substr(at));
        (void)doc_->set_text(id, text.substr(0, at));
        node_id parent;
        node_id next;
        {
            const auto txn = doc_->read();
            parent = txn.parent(id);
            if (parent) {
                const std::span<const node_id> kids = txn.children(parent);
                for (std::size_t i = 0; i + 1 < kids.size(); ++i) {
                    if (kids[i] == id) { next = kids[i + 1]; }
                }
            }
        }
        if (parent) { (void)insert_node(parent, made, next); }
        mutated();
        return wrap(c, made);
    });

    // `wholeText`: the CONTIGUOUS RUN of Text siblings this node is in,
    // concatenated. An element between two text nodes ends the run, which is
    // the only thing `Text-wholeText.html` is really asking - it puts an <a>
    // in the middle of three text nodes and re-reads all three.
    text_proto->define_accessor(
        "wholeText",
        native("wholeText",
               [this, data_of](context & c, std::span<value>) {
                   node_id id;
                   std::string text;
                   if (!data_of(c, id, text)) { return c.string(std::string{}); }
                   const auto txn = doc_->read();
                   const node_id parent = txn.parent(id);
                   if (!parent) { return c.string(text); }
                   const std::span<const node_id> kids = txn.children(parent);
                   std::size_t at = 0;
                   while (at < kids.size() && kids[at] != id) { ++at; }
                   if (at >= kids.size()) { return c.string(text); }
                   const auto is_text = [&txn](node_id one) {
                       return txn.kind(one).value_or(node_kind::element) == node_kind::text;
                   };
                   std::size_t first = at;
                   while (first > 0 && is_text(kids[first - 1])) { --first; }
                   std::size_t last = at;
                   while (last + 1 < kids.size() && is_text(kids[last + 1])) { ++last; }
                   std::string whole;
                   for (std::size_t i = first; i <= last; ++i) { whole += txn.text(kids[i]); }
                   return c.string(whole);
               }),
        value::undefined());
}

// ===================== the shadow DOM =====================================
//
// DOM 4.8, far enough that a test which merely USES a shadow tree can run.
//
// A SHADOW ROOT IS A DocumentFragment PLUS TWO FACTS, which is the whole reason
// this lives in the bindings and not in lib/DOM: `node_kind` already has a
// document_fragment - a parentless bag of nodes - and that is exactly the shape
// the specification gives a shadow root. What a fragment does not carry is its
// HOST and its MODE, and neither belongs on `node`: it is the most replicated
// object in the engine and every field on it is paid for by every document that
// has never heard of shadow DOM. They live in two maps on dom_bindings instead,
// keyed on pack(node_id) exactly as `wrappers_`, `namespaces_` and `mirrors_`
// already are.
//
// WHAT THIS DELIBERATELY DOES NOT DO IS RENDER. The fragment is detached, so the
// cascade, layout and paint never reach it: an element inside a shadow root has
// no box, no computed style and no pixels, and `getComputedStyle` on one answers
// as it does for any detached element. That is a real gap and it is named here
// rather than left to be discovered - flattening a shadow tree into the box tree
// is slot assignment and the flat tree, which is a rung of its own. Every test
// this was built for asserts about the TREE, about events, or about
// getComputedStyle on a LIGHT-DOM element.
//
// EVENT RETARGETING IS ALSO NOT HERE. An event dispatched inside a shadow tree
// is not re-targeted at the host as it crosses the boundary, so
// `shadow-relatedTarget.html` and the composed-path half of `event-global.html`
// still report what the engine dispatched rather than what the boundary should
// hide. That lives in bindings/events.cpp.

namespace {

// "VALID SHADOW HOST NAME", DOM 4.8. Sixteen HTML elements, and the list is
// exhaustive on purpose: `attachShadow` on anything else is a NotSupportedError
// rather than a shadow tree nobody can see.
constexpr std::string_view shadow_host_names = "article aside blockquote body div footer h1 h2 h3 "
                                               "h4 h5 h6 header main nav p section span";

// ...plus ANY VALID CUSTOM ELEMENT NAME, which is the half no table can carry:
// `<my-widget>` is a legal host and there is no list of the ones a page will
// invent. HTML's production is a lowercase ASCII letter, then anything that is
// not an ASCII uppercase letter, with at least one hyphen - and eight reserved
// spellings that satisfy it and name SVG or MathML elements that already exist.
[[nodiscard]] bool valid_custom_element_name(std::string_view name) {
    if (name.size() < 2 || name.front() < 'a' || name.front() > 'z') { return false; }
    if (name.find('-') == std::string_view::npos) { return false; }
    for (const char c : name) {
        if (c >= 'A' && c <= 'Z') { return false; }
    }
    for (const std::string_view taken :
         {"annotation-xml", "color-profile", "font-face", "font-face-src", "font-face-uri",
          "font-face-format", "font-face-name", "missing-glyph"}) {
        if (name == taken) { return false; }
    }
    return true;
}

// A SELECTOR MATCHER OVER A DETACHED SUBTREE - and yes, that is a SECOND one in
// an engine whose point is that a selector cannot mean one thing in a stylesheet
// and another in a script. So here is exactly why, and exactly what deletes it.
//
// `dom_bindings::query` runs `style::engine::select`, which walks from
// `txn.root()`. A shadow root is a DETACHED fragment: nothing below it is
// reachable from the document node, so `select` returns an empty list however it
// is scoped. `style::engine::element_matches` looks like the way round that and
// is not - it anchors depth 0 of its cursor on the children of `txn.root()`, so
// for a chain whose top element is not a child of the document node it measures
// a completely different element. `shadowRoot.querySelector('.x')` came back
// having tested `<html>`'s first element child.
//
// THE FIX THAT WOULD RETIRE THIS is two lines in lib/Style/engine.cpp, which is
// not this workstream's file: `element_matches` should take
// `txn.parent(chain[0])` rather than `txn.root()` for depth 0, and `select`
// should walk FROM a scope root instead of always from the document. Both are
// wrong for an ordinary detached element today - `document.createElement('div')
// .matches('div')` is answered about `<html>` - so that fix is owed with or
// without shadow DOM.
//
// Until then: this runs over the SAME `compiled_selector`, from the same parser,
// as the cascade does, so the two cannot disagree about what a selector MEANS.
// They disagree only about where each is able to look.
class subtree_matcher {
public:
    subtree_matcher(const read_txn & txn, atom_table & atoms)
        : txn_(txn), atoms_(atoms), id_(atoms.intern("id")), class_(atoms.intern("class")),
          disabled_(atoms.intern("disabled")), checked_(atoms.intern("checked")),
          href_(atoms.intern("href")) {}

    [[nodiscard]] bool matches(node_id node, const style::compiled_selector & sel) const {
        return !sel.parts.empty() && walk(node, sel);
    }

private:
    [[nodiscard]] bool is_element(node_id node) const {
        return txn_.kind(node).value_or(node_kind::text) == node_kind::element;
    }
    // THE NEAREST ELEMENT ANCESTOR, not the parent. Only elements occupy a depth
    // in the cascade's traversal, so a text or fragment in between is skipped
    // here too - or `>` would mean two different things in the two matchers.
    [[nodiscard]] node_id element_parent(node_id from) const {
        for (node_id at = txn_.parent(from); at; at = txn_.parent(at)) {
            if (is_element(at)) { return at; }
        }
        return node_id{};
    }
    // A SIBLING WALK NEEDS THE PARENT: the tree is stored as a child list and
    // there is no previous-sibling link to follow.
    [[nodiscard]] node_id previous_element(node_id from) const {
        const node_id parent = txn_.parent(from);
        if (!parent) { return node_id{}; }
        node_id last{};
        for (const node_id child : txn_.children(parent)) {
            if (child == from) { return last; }
            if (is_element(child)) { last = child; }
        }
        return node_id{};
    }

    // Right to left, which is the order `compiled_selector::parts` is stored in
    // and the order style::engine::matches_from walks them.
    [[nodiscard]] bool walk(node_id node, const style::compiled_selector & sel) const {
        if (!compound_holds(node, sel.parts.front())) { return false; }
        node_id here = node;
        for (std::size_t i = 1; i < sel.parts.size(); ++i) {
            const style::compound & want = sel.parts[i];
            switch (sel.links[i - 1]) {
            case style::combinator::child:
                here = element_parent(here);
                if (!here || !compound_holds(here, want)) { return false; }
                break;
            case style::combinator::descendant: {
                node_id up = element_parent(here);
                for (; up; up = element_parent(up)) {
                    if (compound_holds(up, want)) { break; }
                }
                if (!up) { return false; }
                here = up;
                break;
            }
            case style::combinator::next_sibling:
                here = previous_element(here);
                if (!here || !compound_holds(here, want)) { return false; }
                break;
            case style::combinator::subsequent_sibling: {
                node_id prev = previous_element(here);
                for (; prev; prev = previous_element(prev)) {
                    if (compound_holds(prev, want)) { break; }
                }
                if (!prev) { return false; }
                here = prev;
                break;
            }
            case style::combinator::none: return false; // only ever the rightmost
            }
        }
        return true;
    }

    // WHERE AN ELEMENT SITS AMONG ITS SIBLINGS, one-based, counting elements
    // only - the four numbers `:nth-child` and the `-of-type` family need.
    struct place {
        std::uint32_t index = 0;
        std::uint32_t count = 0;
        std::uint32_t type_index = 0;
        std::uint32_t type_count = 0;
    };
    [[nodiscard]] place place_of(node_id node) const {
        const node_id parent = txn_.parent(node);
        // No parent at all: it is the only element where it is, which is what
        // `:only-child` answers about a freshly created element in a browser.
        if (!parent) { return place{1, 1, 1, 1}; }
        const atom mine = txn_.tag(node).value_or(atom{});
        place out;
        for (const node_id child : txn_.children(parent)) {
            if (!is_element(child)) { continue; }
            ++out.count;
            if (txn_.tag(child).value_or(atom{}) == mine) { ++out.type_count; }
            if (child == node) {
                out.index = out.count;
                out.type_index = out.type_count;
            }
        }
        return out;
    }

    // `:empty` - no element children and no text at all. Whitespace COUNTS as
    // text here, which is what the selector means and what surprises authors.
    [[nodiscard]] bool is_empty(node_id node) const {
        for (const node_id child : txn_.children(node)) {
            const node_kind kind = txn_.kind(child).value_or(node_kind::text);
            if (kind == node_kind::element) { return false; }
            if (kind == node_kind::text && !txn_.text(child).empty()) { return false; }
        }
        return true;
    }
    [[nodiscard]] bool can_be_disabled(node_id node) const {
        return lists_token("button input select textarea optgroup option fieldset",
                           atoms_.text(txn_.tag(node).value_or(atom{})));
    }
    [[nodiscard]] bool is_link(node_id node) const {
        return lists_token("a area link", atoms_.text(txn_.tag(node).value_or(atom{}))) &&
               txn_.has_attribute(node, href_);
    }

    [[nodiscard]] bool structural_holds(node_id node, std::uint32_t want) const {
        // `:root` is the DOCUMENT ELEMENT, and a shadow tree has none - its top
        // is a DocumentFragment. `:visited` is always false, for the privacy
        // reason style/selector.hpp records.
        if ((want & (style::structural_root | style::structural_visited)) != 0) { return false; }
        if ((want & style::structural_empty) != 0 && !is_empty(node)) { return false; }
        constexpr std::uint32_t positional =
            style::structural_first_child | style::structural_last_child |
            style::structural_only_child | style::structural_first_of_type |
            style::structural_last_of_type | style::structural_only_of_type;
        if ((want & positional) != 0) {
            const place at = place_of(node);
            if ((want & style::structural_first_child) != 0 && at.index != 1) { return false; }
            if ((want & style::structural_last_child) != 0 && at.index != at.count) {
                return false;
            }
            if ((want & style::structural_only_child) != 0 && at.count != 1) { return false; }
            if ((want & style::structural_first_of_type) != 0 && at.type_index != 1) {
                return false;
            }
            if ((want & style::structural_last_of_type) != 0 && at.type_index != at.type_count) {
                return false;
            }
            if ((want & style::structural_only_of_type) != 0 && at.type_count != 1) {
                return false;
            }
        }
        const bool off = txn_.has_attribute(node, disabled_);
        if ((want & style::structural_disabled) != 0 && !(can_be_disabled(node) && off)) {
            return false;
        }
        // `:enabled` is NOT the negation of `:disabled` - it is false of a
        // <div> rather than true.
        if ((want & style::structural_enabled) != 0 && (!can_be_disabled(node) || off)) {
            return false;
        }
        if ((want & style::structural_checked) != 0 && !txn_.has_attribute(node, checked_)) {
            return false;
        }
        if ((want & style::structural_link) != 0 && !is_link(node)) { return false; }
        return true;
    }

    // One `[name op value]`, exactly as style::engine spells it.
    [[nodiscard]] static bool attribute_holds(std::string_view have,
                                              const style::attribute_match & want) {
        const auto same = [&](std::string_view a, std::string_view b) {
            return want.case_insensitive ? ascii_iequals(a, b) : a == b;
        };
        switch (want.op) {
        case style::attr_op::present: return true; // the caller established it exists
        case style::attr_op::exact: return same(have, want.value);
        case style::attr_op::includes: {
            if (want.value.empty()) { return false; }
            std::size_t at = 0;
            while (at < have.size()) {
                const std::size_t start = have.find_first_not_of(html_whitespace, at);
                if (start == std::string_view::npos) { break; }
                std::size_t end = have.find_first_of(html_whitespace, start);
                if (end == std::string_view::npos) { end = have.size(); }
                if (same(have.substr(start, end - start), want.value)) { return true; }
                at = end;
            }
            return false;
        }
        case style::attr_op::dash:
            if (same(have, want.value)) { return true; }
            return have.size() > want.value.size() && have[want.value.size()] == '-' &&
                   same(have.substr(0, want.value.size()), want.value);
        case style::attr_op::prefix:
            return !want.value.empty() && have.size() >= want.value.size() &&
                   same(have.substr(0, want.value.size()), want.value);
        case style::attr_op::suffix:
            return !want.value.empty() && have.size() >= want.value.size() &&
                   same(have.substr(have.size() - want.value.size()), want.value);
        case style::attr_op::substring: {
            if (want.value.empty() || want.value.size() > have.size()) { return false; }
            if (!want.case_insensitive) { return have.find(want.value) != std::string_view::npos; }
            for (std::size_t at = 0; at + want.value.size() <= have.size(); ++at) {
                if (ascii_iequals(have.substr(at, want.value.size()), want.value)) { return true; }
            }
            return false;
        }
        }
        return false;
    }

    // `An+B`, for n = 0, 1, 2, ... and a one-based index.
    [[nodiscard]] static bool nth_holds(std::int32_t a, std::int32_t b, std::uint32_t index_u) {
        const auto index = static_cast<std::int32_t>(index_u);
        if (index <= 0) { return false; }
        if (a == 0) { return index == b; }
        const std::int32_t offset = index - b;
        if (offset % a != 0) { return false; }
        return offset / a >= 0;
    }

    [[nodiscard]] bool pseudo_holds(node_id node, const style::pseudo_ref & want) const {
        switch (want.kind) {
        case style::pseudo_kind::nth_child: return nth_holds(want.a, want.b, place_of(node).index);
        case style::pseudo_kind::nth_last_child: {
            const place at = place_of(node);
            return nth_holds(want.a, want.b, at.count + 1 - at.index);
        }
        case style::pseudo_kind::nth_of_type:
            return nth_holds(want.a, want.b, place_of(node).type_index);
        case style::pseudo_kind::nth_last_of_type: {
            const place at = place_of(node);
            return nth_holds(want.a, want.b, at.type_count + 1 - at.type_index);
        }
        case style::pseudo_kind::not_:
        case style::pseudo_kind::is_:
        case style::pseudo_kind::where_: {
            // A nested selector's SUBJECT is this element, so each argument runs
            // from the same node - combinators of its own included.
            bool any = false;
            for (const style::compiled_selector & one : want.args) {
                if (matches(node, one)) {
                    any = true;
                    break;
                }
            }
            return want.kind == style::pseudo_kind::not_ ? !any : any;
        }
        }
        return false;
    }

    [[nodiscard]] bool compound_holds(node_id node, const style::compound & c) const {
        if (c.never_matches || !is_element(node)) { return false; }
        // A NAME FOLDS ONLY AGAINST AN HTML ELEMENT - Selectors 4 6.1, and the
        // reason this tokenizer's preserved `viewBox` is reachable at all.
        const bool folds = txn_.element_ns(node) == node_ns::html;
        if (c.tag && (folds ? c.tag : c.tag_exact) != txn_.tag(node).value_or(atom{})) {
            return false;
        }
        // The id and the classes compared as TEXT rather than as atoms. The
        // cascade compares interned integers because its traversal has already
        // interned them; interning here would grow the atom table on a READ, and
        // a read must not be able to.
        if (c.id && atoms_.text(c.id) != txn_.attribute_value(node, id_)) { return false; }
        if (!c.classes.empty()) {
            const std::string_view have = txn_.attribute_value(node, class_);
            for (const atom want : c.classes) {
                if (!lists_class(have, atoms_.text(want))) { return false; }
            }
        }
        // `:hover`, `:active` and `:focus`. NOTHING in a detached tree is in one
        // of them - it is not rendered and never receives input - so a compound
        // requiring one matches nothing rather than everything.
        if (c.states != 0) { return false; }
        if (c.structural != 0 && !structural_holds(node, c.structural)) { return false; }
        for (const style::attribute_match & want : c.attributes) {
            const atom name = folds ? want.name : want.name_exact;
            if (!txn_.has_attribute(node, name)) { return false; }
            if (want.op == style::attr_op::present) { continue; }
            if (!attribute_holds(txn_.attribute_value(node, name), want)) { return false; }
        }
        // LAST OF ALL, because a nested selector list runs the matcher again.
        for (const style::pseudo_ref & want : c.pseudos) {
            if (!pseudo_holds(node, want)) { return false; }
        }
        return true;
    }

    // A whitespace-separated class list. `lists_token` above splits on a single
    // space and a `class` attribute may hold a tab or a newline.
    [[nodiscard]] static bool lists_class(std::string_view list, std::string_view want) {
        if (want.empty()) { return false; }
        std::size_t at = 0;
        while (at < list.size()) {
            const std::size_t start = list.find_first_not_of(html_whitespace, at);
            if (start == std::string_view::npos) { break; }
            std::size_t end = list.find_first_of(html_whitespace, start);
            if (end == std::string_view::npos) { end = list.size(); }
            if (list.substr(start, end - start) == want) { return true; }
            at = end;
        }
        return false;
    }

    const read_txn & txn_;
    atom_table & atoms_;
    atom id_;
    atom class_;
    atom disabled_;
    atom checked_;
    atom href_;
};

} // namespace

node_id dom_bindings::shadow_root_of(node_id host) const {
    if (!host) { return node_id{}; }
    const auto it = shadow_roots_.find(pack(host));
    return it == shadow_roots_.end() ? node_id{} : it->second;
}

const dom_bindings::shadow_tree * dom_bindings::shadow_tree_of(node_id root) const {
    if (!root) { return nullptr; }
    const auto it = shadow_hosts_.find(pack(root));
    return it == shadow_hosts_.end() ? nullptr : &it->second;
}

std::vector<node_id> dom_bindings::select_in_subtree(std::string_view selector, node_id root,
                                                     bool first_only, bool * invalid) {
    bool bad = false;
    const style::css::stylesheet parsed = style::css::parse_selector_text(selector, *atoms_, bad);
    if (invalid != nullptr) { *invalid = bad; }
    std::vector<node_id> found;
    if (parsed.selectors.empty() || !root) { return found; }
    const auto txn = doc_->read();
    const subtree_matcher matcher{txn, *atoms_};
    // DESCENDANTS ONLY and in tree order: the root itself is never one of its
    // own results, exactly as `element.querySelectorAll` has it.
    const auto walk = [&](auto && self, node_id at) -> bool {
        for (const node_id child : txn.children(at)) {
            if (txn.kind(child).value_or(node_kind::text) == node_kind::element) {
                for (const style::compiled_selector & one : parsed.selectors) {
                    if (!matcher.matches(child, one)) { continue; }
                    found.push_back(child);
                    if (first_only) { return false; }
                    break;
                }
            }
            if (!self(self, child)) { return false; }
        }
        return true;
    };
    (void)walk(walk, root);
    return found;
}

// "SHADOW-INCLUDING ROOT", DOM 4.4. Up until there is no parent, and then -
// with `composed` - across the one edge a parent pointer cannot express: from a
// shadow root to its host, and on up the light tree that host sits in.
node_id dom_bindings::root_of_tree(const read_txn & txn, node_id from, bool composed) const {
    node_id at = from;
    // A DEPTH CAP, for the reason every other walk in this file has one: a cycle
    // is refused by pre_insert_valid, and a walk that trusts that and is wrong
    // hangs the page rather than answering badly.
    for (std::size_t step = 0; at && step < 4096; ++step) {
        if (const node_id up = txn.parent(at)) {
            at = up;
            continue;
        }
        if (!composed) { break; }
        const shadow_tree * tree = shadow_tree_of(at);
        if (tree == nullptr || !tree->host) { break; }
        at = tree->host;
    }
    return at;
}

// `element.attachShadow(init)`, DOM 4.8.
//
// The ORDER of the three refusals is the specification's and is observable:
// `mode` is a required member of a required dictionary, so WebIDL's argument
// conversion runs - and throws a plain TypeError - before one thing about the
// element is looked at. Only then may the element be the wrong element
// (NotSupportedError), and only then can it already have a shadow root.
value dom_bindings::attach_shadow(context & cx, node_id host, std::span<value> args) {
    const value init = arg(args, 0);
    std::string mode;
    if (init.is_object()) {
        const value given = cx.lookup_property(init, "mode");
        if (!given.is_undefined()) { mode = cx.to_string(given); }
    }
    if (mode != "open" && mode != "closed") {
        cx.throw_error("TypeError",
                       "attachShadow: `mode` is required and must be \"open\" or \"closed\"");
        return value::undefined();
    }
    if (!host) {
        cx.throw_error("TypeError", "attachShadow: the receiver is not an Element");
        return value::undefined();
    }
    {
        const auto txn = doc_->read();
        const std::string tag{atoms_->text(txn.tag(host).value_or(atom{}))};
        // AN HTML ELEMENT WITH ONE OF SIXTEEN NAMES, or a custom element name.
        // An <svg> is not a host, a <span> in some page-invented namespace is
        // not one either, and neither is a <table>.
        const bool can_host =
            txn.kind(host).value_or(node_kind::text) == node_kind::element &&
            txn.element_ns(host) == node_ns::html &&
            (lists_token(shadow_host_names, tag) || valid_custom_element_name(tag));
        if (!can_host) {
            throw_dom_exception(cx, "NotSupportedError",
                                "attachShadow: <" + tag + "> cannot host a shadow root");
            return value::undefined();
        }
    }
    if (shadow_root_of(host)) {
        throw_dom_exception(cx, "NotSupportedError",
                            "attachShadow: this element already hosts a shadow root");
        return value::undefined();
    }
    const node_id root = doc_->create_fragment();
    if (!root) {
        cx.throw_error("TypeError", "attachShadow: the document refused a fragment");
        return value::undefined();
    }
    shadow_roots_.emplace(pack(host), root);
    shadow_hosts_.emplace(pack(root), shadow_tree{host, mode == "open"});
    // The wrapper is made AFTER the maps are written, because prototype_for_node
    // asks them which of DocumentFragment and ShadowRoot this fragment is.
    return wrap(cx, root);
}

// THE MEMBERS A ShadowRoot HAS THAT A PLAIN DocumentFragment DOES NOT.
//
// Everything else it needs it already has: `wrap` gives every node
// install_element_methods and install_element_views, so `innerHTML`,
// `appendChild`, `append`, `replaceChildren`, `childNodes`, `children`,
// `firstChild` and `textContent` are the same code an element uses and work on a
// fragment unchanged. Only these five are different, and two of them are
// different because they have to search a tree the selector engine cannot reach.
void dom_bindings::install_shadow_root_members(context & cx, script::object_object & obj,
                                               node_id root) {
    const shadow_tree * tree = shadow_tree_of(root);
    if (tree == nullptr) { return; }
    const node_id host = tree->host;
    const bool open = tree->open;
    // `mode` and `host` are READ-ONLY, and accessors rather than data properties
    // for the reason `parentNode` is: the host may be moved or removed and the
    // answer has to follow it.
    obj.define_accessor(
        "mode",
        value::object(cx.allocate<script::native_object>(
            "mode",
            [open](context & c, std::span<value>) { return c.string(open ? "open" : "closed"); })),
        value::undefined());
    obj.define_accessor(
        "host",
        value::object(cx.allocate<script::native_object>(
            "host", [this, host](context & c, std::span<value>) { return wrap(c, host); })),
        value::undefined());
    const auto method = [&](std::string name, script::native_fn fn) {
        obj.set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // THESE TWO REPLACE the ones install_element_methods put on this wrapper a
    // moment ago. That is the whole reason this runs after it: the general pair
    // asks `query()`, which walks from the document root and can never see a
    // detached fragment, so on a ShadowRoot they answered null and [] for every
    // selector. See subtree_matcher.
    method("querySelector", [this, root](context & c, std::span<value> args) {
        bool invalid = false;
        const std::string selector = arg_string(c, args, 0);
        const std::vector<node_id> found = select_in_subtree(selector, root, true, &invalid);
        if (invalid) {
            throw_dom_exception(c, "SyntaxError", "'" + selector + "' is not a valid selector");
            return value::undefined();
        }
        return found.empty() ? value::null() : wrap(c, found.front());
    });
    method("querySelectorAll", [this, root](context & c, std::span<value> args) {
        bool invalid = false;
        const std::string selector = arg_string(c, args, 0);
        const std::vector<node_id> found = select_in_subtree(selector, root, false, &invalid);
        if (invalid) {
            throw_dom_exception(c, "SyntaxError", "'" + selector + "' is not a valid selector");
            return value::undefined();
        }
        value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        for (const node_id node : found) { items->items.push_back(wrap(c, node)); }
        return out;
    });
    // `getElementById` ON THE SHADOW ROOT, which is a DocumentFragment method
    // rather than an Element one - an id inside a shadow tree is scoped to that
    // tree, and `document.getElementById` must NOT find it.
    method("getElementById", [this, root](context & c, std::span<value> args) {
        const std::string want = arg_string(c, args, 0);
        if (want.empty()) { return value::null(); }
        const auto txn = doc_->read();
        const atom id_name = atoms_->intern("id");
        node_id found{};
        const auto walk = [&](auto && self, node_id at) -> void {
            for (const node_id child : txn.children(at)) {
                if (found) { return; }
                if (txn.attribute_value(child, id_name) == want) {
                    found = child;
                    return;
                }
                self(self, child);
            }
        };
        walk(walk, root);
        return found ? wrap(c, found) : value::null();
    });
}

} // namespace ctbrowser::shell
