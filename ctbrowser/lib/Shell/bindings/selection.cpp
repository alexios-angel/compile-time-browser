// dom_bindings - the Selection API (https://w3c.github.io/selection-api/):
// `window.getSelection()`, `document.getSelection()` and the Selection
// interface over them.
//
// A SELECTION IS A RANGE AND A DIRECTION, and nothing else. The specification
// says a selection has "a range, which may be null", an anchor and a focus
// derived from that range, and a direction that says which end is which. So
// this holds a Range OBJECT - the one document/range.cpp installs - in a
// hidden slot, and every operation that moves a boundary goes through that
// object's OWN script surface (`setStart`, `setEnd`, `selectNodeContents`,
// `deleteContents`, `comparePoint`). That is where DOM 5.5's validity checks
// already live: `collapse` on a doctype is an InvalidNodeTypeError and an
// offset past the end an IndexSizeError because `setStart` says so, not
// because this file says so a second time and disagrees with it.
//
// `addRange(r)` stores the caller's own Range, so `getRangeAt(0) === r` - and
// a later `r.setStart(...)` moves the selection, which is what the API means
// by the range being live.
//
// THE OBJECT IS PER WINDOW AND STABLE: `getSelection() === getSelection()` is
// the first thing every test asserts. The one instance is retained by the
// `getSelection` function object, which is a global and therefore a root -
// the arrangement install_custom_elements uses for its registry.
//
// ponytail: no `selectionchange`/`selectstart` events, no `modify()` and no
// user selection. Nothing here is driven by a click; the engine's own visual
// selection lives in shell/browser/selection.cpp and the two are not yet one
// selection.

#include <ctbrowser/shell/bindings.hpp>

#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace ctbrowser::shell {
namespace {

// The Range this selection holds, and which end the focus is at.
constexpr std::string_view range_slot = "__range";
constexpr std::string_view backwards_slot = "__backwards";

[[nodiscard]] script::object_object * self_object(context & c) {
    const value self = c.current_this();
    if (!self.is_object()) {
        c.throw_error("TypeError", "Illegal invocation");
        return nullptr;
    }
    return static_cast<script::object_object *>(self.as_heap());
}

[[nodiscard]] value held_slot(script::object_object * self, std::string_view name) {
    const value * held = self->find(name);
    return held == nullptr ? value::undefined() : *held;
}

// The selection's range, or undefined - the slot is undefined until something
// sets it, so `null` never leaks out of here.
[[nodiscard]] value range_of(context & c) {
    script::object_object * self = self_object(c);
    if (self == nullptr) { return value::undefined(); }
    const value range = held_slot(self, range_slot);
    return range.is_object() ? range : value::undefined();
}

void store_range(context & c, value range, bool backwards) {
    if (script::object_object * self = self_object(c)) {
        self->set(std::string{range_slot}, range);
        self->set(std::string{backwards_slot}, value::boolean(backwards));
    }
}

[[nodiscard]] bool is_backwards(context & c) {
    script::object_object * self = self_object(c);
    return self != nullptr && context::truthy(held_slot(self, backwards_slot));
}

// One method of the Range, called the way a page would call it - so what it
// refuses, it refuses with the exception a page would see.
value call_on(context & c, value target, const std::string & name, std::vector<value> args) {
    const value fn = c.lookup_property(target, name);
    return fn.is_callable() ? c.call(fn, args, target) : value::undefined();
}

} // namespace

void dom_bindings::install_selection(context & cx) {
    auto * proto = cx.allocate<script::object_object>();
    const value proto_value = value::object(proto);

    // --- the attributes ------------------------------------------------------
    //
    // The anchor is where the selection STARTED and the focus where it ended,
    // so a BACKWARDS selection reads its range's end as the anchor. With no
    // range the nodes are null, the offsets 0 and `isCollapsed` is TRUE -
    // which is the specification's answer, not an accident of there being
    // nothing to ask.
    for (const auto & [name, at_start, at_end] :
         std::initializer_list<std::tuple<const char *, const char *, const char *>>{
             {"anchorNode", "startContainer", "endContainer"},
             {"anchorOffset", "startOffset", "endOffset"},
             {"focusNode", "endContainer", "startContainer"},
             {"focusOffset", "endOffset", "startOffset"}}) {
        const std::string forwards{at_start};
        const std::string backwards{at_end};
        const bool offset = std::string_view{name}.ends_with("Offset");
        define_getter(cx, *proto, name,
                      [forwards, backwards, offset](context & c, std::span<value>) {
                          const value range = range_of(c);
                          if (!range.is_object()) {
                              return offset ? value::number(0) : value::null();
                          }
                          return c.lookup_property(range, is_backwards(c) ? backwards : forwards);
                      });
    }
    define_getter(cx, *proto, "isCollapsed", [](context & c, std::span<value>) {
        const value range = range_of(c);
        if (!range.is_object()) { return value::boolean(true); }
        return value::boolean(context::truthy(c.lookup_property(range, "collapsed")));
    });
    define_getter(cx, *proto, "rangeCount", [](context & c, std::span<value>) {
        return value::number(range_of(c).is_object() ? 1 : 0);
    });
    // "None" with no range, "Caret" when it is collapsed, "Range" otherwise.
    define_getter(cx, *proto, "type", [](context & c, std::span<value>) {
        const value range = range_of(c);
        if (!range.is_object()) { return c.string("None"); }
        return c.string(context::truthy(c.lookup_property(range, "collapsed")) ? "Caret" : "Range");
    });
    define_getter(cx, *proto, "direction", [](context & c, std::span<value>) {
        const value range = range_of(c);
        if (!range.is_object()) { return c.string("none"); }
        return c.string(is_backwards(c) ? "backward" : "forward");
    });

    const auto method = [&](const char * name, script::native_fn fn) {
        set_method(cx, *proto, name, std::move(fn), script::attr_builtin);
    };

    // --- what a node has to be for the selection to point at it ---------------
    //
    // "If node's root is not the document associated with this, abort these
    // steps" - ABORT, not throw, and it is why a selection cannot be moved
    // into a detached fragment or into another document's tree.
    const auto rooted_here = [this](value v) {
        if (is_the_document(v)) { return true; }
        if (owner_of(v) != this) { return false; }
        const node_id id = handle_of(v);
        if (!id) { return false; }
        const auto txn = doc_->read();
        const node_id top = root_of_tree(txn, id, true);
        return top == txn.root() ||
               txn.kind(top).value_or(node_kind::element) == node_kind::document;
    };

    // A fresh Range of this document collapsed at (node, offset). `setStart`
    // is where a doctype and an offset past the end are refused.
    const auto range_at = [this](context & c, value node, value offset) -> value {
        const value made = create_range(c);
        (void)call_on(c, made, "setStart", {node, offset});
        if (c.throw_pending()) { return value::undefined(); }
        (void)call_on(c, made, "setEnd", {node, offset});
        return c.throw_pending() ? value::undefined() : made;
    };

    // --- the methods ----------------------------------------------------------

    method("getRangeAt", [this](context & c, std::span<value> a) {
        const value range = range_of(c);
        if (!range.is_object() || arg_number(a, 0) != 0) {
            throw_dom_exception(c, "IndexSizeError", "getRangeAt: there is no range at that index");
            return value::undefined();
        }
        return range;
    });
    // THE CALLER'S OWN Range OBJECT, KEPT: `getRangeAt(0) === range`. A second
    // range is ignored - this engine, like every shipping one, holds one.
    method("addRange", [rooted_here](context & c, std::span<value> a) {
        const value range = arg(a, 0);
        if (!range.is_object()) {
            c.throw_error("TypeError", "Failed to execute 'addRange' on 'Selection': parameter 1 "
                                       "is not of type 'Range'.");
            return value::undefined();
        }
        if (!rooted_here(c.lookup_property(range, "startContainer"))) { return value::undefined(); }
        if (range_of(c).is_object()) { return value::undefined(); }
        store_range(c, range, false);
        return value::undefined();
    });
    method("removeRange", [this](context & c, std::span<value> a) {
        const value range = arg(a, 0);
        if (!range.is_object()) {
            c.throw_error("TypeError", "Failed to execute 'removeRange' on 'Selection': parameter "
                                       "1 is not of type 'Range'.");
            return value::undefined();
        }
        if (range_of(c).bits() != range.bits()) {
            throw_dom_exception(c, "NotFoundError",
                                "removeRange: that range is not in the selection");
            return value::undefined();
        }
        store_range(c, value::undefined(), false);
        return value::undefined();
    });
    for (const char * name : {"removeAllRanges", "empty"}) {
        method(name, [](context & c, std::span<value>) {
            store_range(c, value::undefined(), false);
            return value::undefined();
        });
    }
    // `collapse(null)` is `removeAllRanges()` - the one place in this API
    // where null is a value an argument may have rather than a missing one.
    for (const char * name : {"collapse", "setPosition"}) {
        method(name, [range_at, rooted_here](context & c, std::span<value> a) {
            const value node = arg(a, 0);
            if (node.is_null() || node.is_undefined()) {
                store_range(c, value::undefined(), false);
                return value::undefined();
            }
            if (!rooted_here(node)) { return value::undefined(); }
            const value made = range_at(c, node, value::number(arg_number(a, 1)));
            if (made.is_object()) { store_range(c, made, false); }
            return value::undefined();
        });
    }
    for (const bool to_start : {true, false}) {
        method(to_start ? "collapseToStart" : "collapseToEnd",
               [this, to_start](context & c, std::span<value>) {
                   const value range = range_of(c);
                   if (!range.is_object()) {
                       throw_dom_exception(c, "InvalidStateError",
                                           "collapseToStart: there is no range to collapse");
                       return value::undefined();
                   }
                   const value made = call_on(c, range, "cloneRange", {});
                   if (!made.is_object()) { return value::undefined(); }
                   (void)call_on(c, made, "collapse", {value::boolean(to_start)});
                   store_range(c, made, false);
                   return value::undefined();
               });
    }
    // `extend(node, offset)`: the ANCHOR stays where it is and the focus moves
    // to the new point, so the range grows in whichever direction that point
    // lies - and the direction follows it.
    method("extend", [this, rooted_here](context & c, std::span<value> a) {
        const value range = range_of(c);
        if (!range.is_object()) {
            throw_dom_exception(c, "InvalidStateError", "extend: there is no range to extend");
            return value::undefined();
        }
        const value node = arg(a, 0);
        if (!rooted_here(node)) { return value::undefined(); }
        const value offset = value::number(arg_number(a, 1));
        const bool backwards = is_backwards(c);
        const value anchor_node =
            c.lookup_property(range, backwards ? "endContainer" : "startContainer");
        const value anchor_offset =
            c.lookup_property(range, backwards ? "endOffset" : "startOffset");
        const value made = create_range(c);
        (void)call_on(c, made, "setStart", {anchor_node, anchor_offset});
        if (c.throw_pending()) { return value::undefined(); }
        (void)call_on(c, made, "setEnd", {anchor_node, anchor_offset});
        if (c.throw_pending()) { return value::undefined(); }
        // Collapsed at the anchor, so comparePoint says which side the new
        // focus is on: -1 before it, 0 or 1 at or after it.
        const double side = context::to_number(call_on(c, made, "comparePoint", {node, offset}));
        if (c.throw_pending()) { return value::undefined(); }
        (void)call_on(c, made, side < 0 ? "setStart" : "setEnd", {node, offset});
        if (c.throw_pending()) { return value::undefined(); }
        store_range(c, made, side < 0);
        return value::undefined();
    });
    method("setBaseAndExtent", [this, rooted_here](context & c, std::span<value> a) {
        const value anchor_node = arg(a, 0);
        const value anchor_offset = value::number(arg_number(a, 1));
        const value focus_node = arg(a, 2);
        const value focus_offset = value::number(arg_number(a, 3));
        if (!rooted_here(anchor_node) || !rooted_here(focus_node)) { return value::undefined(); }
        const value made = create_range(c);
        (void)call_on(c, made, "setStart", {anchor_node, anchor_offset});
        if (c.throw_pending()) { return value::undefined(); }
        (void)call_on(c, made, "setEnd", {anchor_node, anchor_offset});
        if (c.throw_pending()) { return value::undefined(); }
        const double side =
            context::to_number(call_on(c, made, "comparePoint", {focus_node, focus_offset}));
        if (c.throw_pending()) { return value::undefined(); }
        (void)call_on(c, made, side < 0 ? "setStart" : "setEnd", {focus_node, focus_offset});
        if (c.throw_pending()) { return value::undefined(); }
        store_range(c, made, side < 0);
        return value::undefined();
    });
    method("selectAllChildren", [this, rooted_here](context & c, std::span<value> a) {
        const value node = arg(a, 0);
        if (!node.is_object()) {
            c.throw_error("TypeError", "Failed to execute 'selectAllChildren' on 'Selection': "
                                       "parameter 1 is not of type 'Node'.");
            return value::undefined();
        }
        if (!rooted_here(node)) { return value::undefined(); }
        const value made = create_range(c);
        (void)call_on(c, made, "selectNodeContents", {node});
        if (c.throw_pending()) { return value::undefined(); }
        store_range(c, made, false);
        return value::undefined();
    });
    method("deleteFromDocument", [](context & c, std::span<value>) {
        const value range = range_of(c);
        if (range.is_object()) { (void)call_on(c, range, "deleteContents", {}); }
        return value::undefined();
    });
    // `containsNode(node, allowPartial)`: partial containment is exactly
    // `intersectsNode`; full containment is the node's own range inside this
    // one at BOTH ends - START_TO_START and END_TO_END, the two constants
    // compareBoundaryPoints numbers 0 and 2.
    method("containsNode", [this](context & c, std::span<value> a) {
        const value range = range_of(c);
        const value node = arg(a, 0);
        if (!range.is_object() || !node.is_object() || owner_of(node) != this) {
            return value::boolean(false);
        }
        if (context::truthy(arg(a, 1))) {
            return value::boolean(context::truthy(call_on(c, range, "intersectsNode", {node})));
        }
        const node_id id = handle_of(node);
        if (!id || !dom_parent(doc_->read(), id)) { return value::boolean(false); }
        const value about = create_range(c);
        (void)call_on(c, about, "selectNode", {node});
        if (c.throw_pending()) { return value::boolean(false); }
        const double starts = context::to_number(
            call_on(c, range, "compareBoundaryPoints", {value::number(0), about}));
        const double ends = context::to_number(
            call_on(c, range, "compareBoundaryPoints", {value::number(2), about}));
        return value::boolean(starts <= 0 && ends >= 0);
    });
    method("toString", [](context & c, std::span<value>) {
        const value range = range_of(c);
        if (!range.is_object()) { return c.string(""); }
        return c.string(c.to_string(call_on(c, range, "toString", {})));
    });
    // `getComposedRanges()`: the selection as StaticRanges, with the boundary
    // points rescoped out of any shadow tree the caller did NOT pass. No
    // selection this engine makes is inside a shadow tree yet, so what comes
    // back is the range's own two points.
    method("getComposedRanges", [](context & c, std::span<value>) {
        const value made = c.make_array();
        const value range = range_of(c);
        const value ctor = c.global("StaticRange");
        if (!range.is_object() || !ctor.is_callable()) { return made; }
        const value init = value::object(c.allocate<script::object_object>());
        for (const char * name : {"startContainer", "startOffset", "endContainer", "endOffset"}) {
            c.store_property(init, name, c.lookup_property(range, name));
        }
        const value one = c.construct(ctor, std::span<const value>{&init, 1});
        if (one.is_object()) {
            static_cast<script::array_object *>(made.as_heap())->items.push_back(one);
        }
        return made;
    });
    proto->define("@@toStringTag", cx.string("Selection"), script::attr_configurable);

    auto * ctor =
        cx.allocate<script::native_object>("Selection", [](context & c, std::span<value>) {
            c.throw_error("TypeError", "Illegal constructor");
            return value::undefined();
        });
    ctor->define("prototype", proto_value, script::attr_none);
    proto->define("constructor", value::object(ctor), script::attr_builtin);
    cx.define_global("Selection", value::object(ctor));

    // THE ONE SELECTION OF THIS WINDOW. Retained by the function that hands it
    // out, which is a global and therefore a root.
    auto * selection = cx.allocate<script::object_object>();
    selection->prototype = proto_value;
    const value selection_value = value::object(selection);
    auto * getter = cx.allocate<script::native_object>(
        "getSelection", [selection_value](context &, std::span<value>) { return selection_value; });
    getter->retained.push_back(selection_value);
    cx.define_global("getSelection", value::object(getter));

    // `document.getSelection()` - THE SAME OBJECT, and null for a document with
    // no browsing context, which is what a null `defaultView` says.
    //
    // ON THE PAGE'S OWN DOCUMENT IT IS AN OWN PROPERTY, and that is not
    // belt and braces: `document` is a PROXY, and a method reached through it
    // does not arrive with the document as its receiver - the prototype method
    // below therefore read `defaultView` off the wrong thing and answered null,
    // so `getSelection() === document.getSelection()` was false. This one
    // answers with the selection and asks nothing.
    if (auto * doc = document_object()) {
        auto * own = cx.allocate<script::native_object>(
            "getSelection",
            [selection_value](context &, std::span<value>) { return selection_value; });
        own->retained.push_back(selection_value);
        doc->set("getSelection", value::object(own));
        doc->set_attrs("getSelection", script::attr_builtin);
    }
    // AND ON Document.prototype for every OTHER document of the realm - one
    // from createHTMLDocument or DOMParser - which have no browsing context
    // and answer null.
    if (const value document_proto = interface_prototype("Document"); document_proto.is_object()) {
        auto * on = static_cast<script::object_object *>(document_proto.as_heap());
        // RETAINED HERE TOO. A captured `value` is invisible to the collector,
        // and a page may delete the `getSelection` global - which would
        // otherwise leave this method holding a swept object.
        auto * method = cx.allocate<script::native_object>(
            "getSelection", [selection_value](context & c, std::span<value>) {
                const value view = c.lookup_property(c.current_this(), "defaultView");
                return view.is_object() ? selection_value : value::null();
            });
        method->retained.push_back(selection_value);
        on->set("getSelection", value::object(method));
        on->set_attrs("getSelection", script::attr_builtin);
    }
}

} // namespace ctbrowser::shell
