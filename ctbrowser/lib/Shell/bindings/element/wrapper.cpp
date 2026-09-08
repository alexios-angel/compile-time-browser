// dom_bindings - the element wrapper itself: identity, refresh, the box it
// occupies, equality of nodes, and the wrapper a node already has.
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

} // namespace ctbrowser::shell
