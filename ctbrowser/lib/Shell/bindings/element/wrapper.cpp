//===- wrapper.cpp - Node wrapper construction ------------------*- C++ -*-===//
//
// The script object that stands for a DOM node: creating it (wrap), keeping
// its data properties in step with the node (refresh_element), finding the
// node behind it (receiver, unpack), image sources, and DOM node equality.
//
//===----------------------------------------------------------------------===//

#include "control_methods/helpers.hpp"
#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

/// The script object for node `id`, made on first request and the same
/// object on every later one; null for a null id.
value dom_bindings::wrap(context & cx, node_id id) {
    if (!id) { return value::null(); }
    // The Document node is `document`. A doctype, a comment or a PI beside the
    // document element has the Document node as its parent (see
    // document::document_node), and `parentNode` on one must be the document
    // object the page holds, not a second wrapper over the same node.
    if (id == doc_->document_node() && document_.is_object_like()) { return document_; }
    // Build the interfaces if they can be built yet (see
    // ensure_dom_interfaces). A no-op after the first success; it is here
    // because wrapping is the earliest thing a page can do that needs them.
    ensure_dom_interfaces(cx);
    if (const auto it = wrappers_.find(id.key()); it != wrappers_.end()) {
        refresh_element(cx, *it->second, id);
        return exposed_wrapper(*it->second);
    }
    // A node whose wrapper was adopted into another document is still that
    // object; the other document refreshes it. See adopted_away_.
    if (const auto it = adopted_away_.find(id.key()); it != adopted_away_.end()) {
        return exposed_wrapper(*it->second);
    }
    auto * obj = cx.allocate<script::object_object>();
    value wrapper = value::object(obj);
    obj->set(std::string{handle_property}, value::number(static_cast<double>(id.key())));
    // The node's interface prototype: `HTMLCanvasElement.prototype` for a
    // canvas, `Text.prototype` for a text node, `HTMLElement.prototype` for a
    // tag with no interface of its own, each chaining down to
    // `EventTarget.prototype`. `instanceof` answers from it and the reflected
    // IDL attributes live on it, so an element with no prototype has neither
    // `id` nor `className`.
    {
        const auto txn = doc_->read();
        const value proto = prototype_for_node(txn, id);
        if (proto.is_object()) { obj->prototype = proto; }
    }
    // The methods are on those prototypes (see define_operation); only the
    // accessors that close over the node are the wrapper's own.
    install_element_views(cx, *obj, id);
    {
        const auto txn = doc_->read();
        // After the views: a ShadowRoot's members come on top of them.
        const node_kind kind = txn.kind(id).value_or(node_kind::element);
        if (kind == node_kind::document_fragment && shadow_tree_of(id) != nullptr) {
            install_shadow_root_members(cx, *obj, id);
        }
        // A CDATASection and a ProcessingInstruction are CharacterData: `data`
        // and `nodeValue` are their text, both ways. install_element_views
        // wires those two for Text and Comment only, so they are re-defined
        // here for the two kinds it does not know. `target`, which only a PI
        // has, is read-only.
        if (kind == node_kind::cdata_section || kind == node_kind::processing_instruction) {
            for (const char * spelling : {"data", "nodeValue"}) {
                obj->define_accessor(spelling,
                                     value::object(cx.allocate<script::native_object>(
                                         spelling,
                                         [this, id](context & c, std::span<value>) {
                                             return c.string(std::string{doc_->read().text(id)});
                                         })),
                                     value::object(cx.allocate<script::native_object>(
                                         spelling, [this, id](context & c, std::span<value> a) {
                                             const value given = arg(a, 0);
                                             (void)doc_->set_text(id, given.is_null()
                                                                          ? std::string{}
                                                                          : arg_string(c, a, 0));
                                             mutated();
                                             return value::undefined();
                                         })));
            }
        }
        if (kind == node_kind::processing_instruction) {
            obj->define_accessor("target",
                                 value::object(cx.allocate<script::native_object>(
                                     "target",
                                     [this, id](context & c, std::span<value>) {
                                         return c.string(
                                             std::string{atoms_->text(doc_->read().name(id))});
                                     })),
                                 value::undefined());
        }
        // `template.content`, HTML 4.12.3: the DocumentFragment the parser put
        // the element's children into (see document::template_content), or,
        // for a template a script made, one created the first time it is asked
        // for, because every template element has one. A read-only accessor
        // returning the same fragment on every read.
        if (txn.element_ns(id) == node_ns::html &&
            atoms_->text(txn.tag(id).value_or(atom{})) == "template") {
            obj->define_accessor("content",
                                 value::object(cx.allocate<script::native_object>(
                                     "content",
                                     [this, id](context & c, std::span<value>) {
                                         node_id contents = doc_->template_content(id);
                                         if (!contents) {
                                             contents = doc_->create_fragment();
                                             doc_->set_template_content(id, contents);
                                         }
                                         return wrap(c, contents);
                                     })),
                                 value::undefined());
        }
    }
    refresh_element(cx, *obj, id);
    wrappers_.emplace(id.key(), obj);
    // A form and a select are legacy platform objects (WebIDL 3.9): the page
    // holds a proxy over this object (control_methods/legacy_objects.cpp).
    // The proxy is stored on the object so every later wrap() hands out the
    // same one.
    if (const value legacy = control_helpers{this}.legacy_object(cx, *obj, id);
        !legacy.is_undefined()) {
        obj->define(legacy_object_key, legacy, script::attr_none);
        return legacy;
    }
    return wrapper;
}

/// The ordinary wrapper behind a legacy platform object's proxy, and only
/// behind ours: a page's `new Proxy(form, {})` is not a Node and is returned
/// unchanged, as is any non-proxy value.
value dom_bindings::unwrap_legacy_proxy(value v) {
    if (!v.is_kind(script::heap_kind::proxy)) { return v; }
    const value target = static_cast<script::proxy_object *>(v.as_heap())->target;
    if (!target.is_object()) { return v; }
    const value * held =
        static_cast<script::object_object *>(target.as_heap())->find(legacy_object_key);
    return held != nullptr && held->bits() == v.bits() ? target : v;
}

/// Decodes a node handle: slot in the high 32 bits, generation in the low 32.
node_id dom_bindings::unpack(std::uint64_t bits) {
    node_id id;
    id.slot = static_cast<std::uint32_t>(bits >> 32);
    id.generation = static_cast<std::uint32_t>(bits & 0xFFFFFFFFu);
    return id;
}

/// The node behind the current `this`, or a null id when it is not a wrapper.
node_id dom_bindings::receiver(context & cx) {
    const value self = unwrap_legacy_proxy(cx.current_this());
    if (!self.is_object()) { return node_id{}; }
    auto * obj = static_cast<script::object_object *>(self.as_heap());
    const value * slot = obj->find(std::string{handle_property});
    if (slot == nullptr) { return node_id{}; }
    return unpack(static_cast<std::uint64_t>(context::to_number(*slot)));
}

/// Rewrites the wrapper's data properties from the node: tagName, nodeName,
/// nodeType, ownerDocument and the qualified-name parts. Everything that can
/// change between refreshes is an accessor instead.
void dom_bindings::refresh_element(context & cx, script::object_object & obj, node_id id) {
    // Here as well as in wrap(), because refresh_wrappers() walks every live
    // wrapper before a dispatch and before a frame, so a page that never asks
    // for a new element still gets its interfaces built, and the wrappers
    // install_document made before EventTarget existed still get linked.
    ensure_dom_interfaces(cx);
    const auto txn = doc_->read();
    // `tagName` is the qualified name, uppercased only for an HTML-namespace
    // element in an HTML document (DOM "HTML-uppercased qualified name"). An
    // XHTML document keeps the case, as WPT Node-nodeName-xhtml.xhtml checks.
    {
        std::string tag_name{atoms_->text(txn.tag(id).value_or(atom{}))};
        if (txn.element_ns(id) == node_ns::html && !doc_->xml()) { ascii_upper_in_place(tag_name); }
        obj.set("tagName", cx.string(tag_name));
        // `nodeName` and `nodeType` per DOM's tables. They differ from
        // `tagName` for non-elements: a text node's tagName is "" but its
        // nodeName is "#text".
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
        // A doctype is named for its name and a PI for its target (DOM 4.4's
        // nodeName table), and neither is case-folded, whatever the document.
        case node_kind::document_type:
            obj.set("nodeName", cx.string(std::string{atoms_->text(txn.name(id))}));
            obj.set("nodeType", value::number(10));
            // The DocumentType interface's three attributes. Data properties
            // rather than accessors because none of the three can change.
            obj.set("name", cx.string(std::string{atoms_->text(txn.name(id))}));
            obj.set("publicId", cx.string(std::string{txn.public_id(id)}));
            obj.set("systemId", cx.string(std::string{txn.system_id(id)}));
            break;
        case node_kind::processing_instruction:
            obj.set("nodeName", cx.string(std::string{atoms_->text(txn.name(id))}));
            obj.set("nodeType", value::number(7));
            break;
        case node_kind::cdata_section:
            obj.set("nodeName", cx.string("#cdata-section"));
            obj.set("nodeType", value::number(4));
            break;
        }
        // `ownerDocument`: null on the Document node itself, otherwise the
        // document these bindings belong to.
        obj.set("ownerDocument", kind == node_kind::document ? value::null() : document_);
        // `localName`, `prefix` and `namespaceURI`: the parts of a qualified
        // name.
        //
        // localName is the tag without the case fold and without the prefix:
        // `createElementNS(ns, "a:b")` has tagName "a:b" and localName "b".
        // prefix is null when the colon is not a prefix separator (see
        // node::prefixed), which covers every element the HTML parser builds
        // and everything `createElement` makes.
        if (kind == node_kind::element) {
            const std::string_view prefix = txn.prefix(id);
            obj.set("localName", cx.string(std::string{txn.local_name(id)}));
            obj.set("prefix", prefix.empty() ? value::null() : cx.string(std::string{prefix}));
            const std::string ns = namespace_of(id);
            obj.set("namespaceURI", ns.empty() ? value::null() : cx.string(ns));
        } else {
            obj.set("localName", value::undefined());
            obj.set("prefix", value::null());
            obj.set("namespaceURI", value::null());
        }
    }
    // `id`, `className`, `width`, `height` and the box metrics (offsetWidth
    // and friends) are not set here: they are accessors (on the interface
    // prototypes or from install_element_views), so writes reach the
    // attributes and reads see current layout.
}

/// The node's client rectangle in viewport coordinates.
rect dom_bindings::box_of(node_id id) const {
    return client_rect_of(id); // viewport coordinates: the scroll offsets above the box come off
}

/// The pixels a canvas image argument names: an image index, an ImageBitmap,
/// a <canvas> wrapper, or an element whose `src` is loaded. Null otherwise.
std::shared_ptr<const paint::bitmap> dom_bindings::image_argument(value v) {
    if (images_ == nullptr) { return nullptr; }
    if (v.is_number()) { return images_->at(static_cast<int>(context::to_number(v))); }
    if (!v.is_object()) { return nullptr; }
    auto * wrapper = static_cast<script::object_object *>(v.as_heap());
    if (const value * bitmap = wrapper->find(image_bitmap_property)) { // an ImageBitmap
        return images_->at(static_cast<int>(context::to_number(*bitmap)));
    }
    const value * handle = wrapper->find(handle_property);
    if (handle == nullptr || assets_ == nullptr) { return nullptr; }
    const node_id id = unpack(static_cast<std::uint64_t>(context::to_number(*handle)));
    const auto txn = doc_->read();
    // A canvas is an image source: `drawImage(otherCanvas, ...)` composites
    // one surface onto another (p5's `image(g, ...)` with a createGraphics
    // buffer does this). It has no `src`, so its pixels are read directly.
    if (canvases_ != nullptr && atoms_->text(txn.tag(id).value_or(atom{})) == "canvas") {
        return canvases_->pixels_of(id);
    }
    const std::string_view src = txn.attribute_value(id, atoms_->intern("src"));
    return src.empty() ? nullptr : images_->load(*assets_, src);
}

/// The wrapper for a node if one exists, else undefined. Deliberately does not
/// create one: this is on the dispatch path for every event, and making a
/// wrapper per node per event would wrap the whole document for a mousemove.
value dom_bindings::value_of_wrapper(node_id id) const {
    if (!id) { return value::undefined(); }
    const auto it = wrappers_.find(id.key());
    return it == wrappers_.end() || it->second == nullptr ? value::undefined()
                                                          : exposed_wrapper(*it->second);
}

/// DOM "equals" (used by `isEqualNode`): a structural comparison, not
/// identity. Attributes are compared as an unordered set of (namespace, local
/// name, value), ignoring the attribute prefix; an element's own qualified
/// name, prefix included, must match. Children are compared pairwise in order.
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
    case node_kind::cdata_section:
        if (txn.text(left) != txn.text(right)) { return false; }
        break;
    // A doctype is its name and two identifiers, a PI its target and data
    // (the rows of DOM 4.4's "equals" table for the two).
    case node_kind::document_type:
        if (txn.name(left) != txn.name(right) || txn.public_id(left) != txn.public_id(right) ||
            txn.system_id(left) != txn.system_id(right)) {
            return false;
        }
        break;
    case node_kind::processing_instruction:
        if (txn.name(left) != txn.name(right) || txn.text(left) != txn.text(right)) {
            return false;
        }
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
