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
// the first thing every test asserts. The page's is made here and retained by
// the `getSelection` function object, which is a global and therefore a root
// - the arrangement install_custom_elements uses for its registry. A FRAME'S
// is its own (getSelection.html), made the first time its window or document
// asks and kept on its window object; the methods are the page's, and each
// works in the bindings of the document the selection carries in its
// `__document` slot.
//
// THE USER'S SELECTION IS THIS ONE: a press on text collapses it and a drag
// extends it, through these same methods (events/input.cpp, select_by_user).
//
// ponytail: no `selectstart` event, no selectionchange for a boundary a Range
// method or a tree edit moves, and no `modify()`.

#include <ctbrowser/shell/bindings.hpp>

#include <algorithm>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace ctbrowser::shell {
namespace {

// The Range this selection holds, and which end the focus is at.
constexpr std::string_view range_slot = "__range";
constexpr std::string_view backwards_slot = "__backwards";
// The document's "has scheduled selectionchange event" flag, kept on the one
// Selection of the window.
constexpr std::string_view scheduled_slot = "__selectionchangeScheduled";
// THE ANCHOR AND THE FOCUS THEMSELVES, which are not always the range's ends:
// a selection whose two points are in different trees of the document - one
// inside a shadow root - has a live range collapsed at one of them (DOM's
// "set the start or end" across roots), and getComposedRanges() and
// `isCollapsed` answer from these.
constexpr std::string_view anchor_node_slot = "__anchorNode";
constexpr std::string_view anchor_offset_slot = "__anchorOffset";
constexpr std::string_view focus_node_slot = "__focusNode";
constexpr std::string_view focus_offset_slot = "__focusOffset";
// ...and whether they are such a pair. Otherwise the range's own ends are the
// answer, read live - a range the page added may since have been moved.
constexpr std::string_view across_slot = "__across";
// The document the selection is of - the page's, or a frame's.
constexpr std::string_view document_slot = "__document";
// Where a frame's window keeps its selection once one is asked for.
constexpr std::string_view window_selection_key = CTBROWSER_SYMBOL_KEY("sym:ctbrowser:selection");

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
//
// AND ONLY WHILE IT IS STILL THIS DOCUMENT'S. The range is live and the page
// holds it: `getRangeAt(0).selectNode(nodeInAFragment)` moves it out of the
// document, and removing a shadow host takes a range inside its tree out with
// it. A selection's range is in its document, so the moment one is found
// outside - an end that is neither the document nor a connected node of it -
// the selection lets it go (move-selection-range-into-different-root and
// selection-range-in-shadow-after-the-shadow-removed, both tentative).
[[nodiscard]] value range_of(context & c) {
    script::object_object * self = self_object(c);
    if (self == nullptr) { return value::undefined(); }
    const value range = held_slot(self, range_slot);
    if (!range.is_object()) { return value::undefined(); }
    const value mine = held_slot(self, document_slot);
    const value here = mine.is_object_like()      ? mine
                       : c.has_global("document") ? c.global("document")
                                                  : value::undefined();
    for (const char * end : {"startContainer", "endContainer"}) {
        const value node = c.lookup_property(range, end);
        if (node.is_heap() && node.bits() == here.bits()) { continue; }
        if (!node.is_object_like() || !context::truthy(c.lookup_property(node, "isConnected")) ||
            c.lookup_property(node, "ownerDocument").bits() != here.bits()) {
            self->set(std::string{range_slot}, value::undefined());
            return value::undefined();
        }
    }
    return range;
}

// DIRECTIONLESS is the third direction (the Definition): what a selection is
// that nobody made by indicating one point and then another - a click, and
// the collapse() a click comes to - kept as an undefined `__backwards`, so
// the anchor is the start as it is for a forwards one.
void store_range(context & c, value range, bool backwards, bool directionless = false) {
    if (script::object_object * self = self_object(c)) {
        self->set(std::string{range_slot}, range);
        self->set(std::string{backwards_slot},
                  directionless ? value::undefined() : value::boolean(backwards));
        const bool has = range.is_object();
        const auto end = [&](const char * name) {
            return has ? c.lookup_property(range, name) : value::undefined();
        };
        self->set(std::string{anchor_node_slot},
                  end(backwards ? "endContainer" : "startContainer"));
        self->set(std::string{anchor_offset_slot}, end(backwards ? "endOffset" : "startOffset"));
        self->set(std::string{focus_node_slot}, end(backwards ? "startContainer" : "endContainer"));
        self->set(std::string{focus_offset_slot}, end(backwards ? "startOffset" : "endOffset"));
        self->set(std::string{across_slot}, value::boolean(false));
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
    // "The anchor and the focus are the same", of the selection's RANGE - so a
    // selection across a shadow boundary, whose range is collapsed at one of
    // its points, is collapsed; only its composed range is not
    // (Selection-isCollapsed.html, tentative).
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
        script::object_object * self = self_object(c);
        if (!range.is_object() || self == nullptr ||
            held_slot(self, backwards_slot).is_undefined()) {
            return c.string("none");
        }
        return c.string(is_backwards(c) ? "backward" : "forward");
    });

    // Each operation's `length` is its count of required arguments, from the
    // Selection API's IDL (WebIDL 3.7.6) - `deleteFromDocument.length` is 0,
    // which deleteFromDocument.html asserts first.
    const auto method = [&](const char * name, script::native_fn fn) {
        static constexpr std::pair<std::string_view, int> required[] = {
            {"getRangeAt", 1},       {"addRange", 1},          {"removeRange", 1},
            {"collapse", 1},         {"setPosition", 1},       {"extend", 1},
            {"setBaseAndExtent", 4}, {"selectAllChildren", 1}, {"containsNode", 1}};
        int length = 0;
        for (const auto & [named, count] : required) {
            if (named == name) { length = count; }
        }
        auto * made = cx.allocate<script::native_object>(name, std::move(fn));
        made->define("length", value::number(length), script::attr_configurable);
        proto->set(name, value::object(made));
        proto->set_attrs(name, script::attr_builtin);
    };

    // THE BINDINGS OF THE SELECTION'S DOCUMENT - the page's, or the frame's
    // whose selection `this` is - which every method below works in.
    const auto home = [this](context & c) -> dom_bindings & {
        dom_bindings & top = primary();
        const value self = c.current_this();
        if (!self.is_object()) { return top; }
        const value doc =
            held_slot(static_cast<script::object_object *>(self.as_heap()), document_slot);
        if (!doc.is_object_like() || doc.bits() == top.document_.bits()) { return top; }
        for (const auto & made : top.secondary_documents_) {
            if (made->document_.bits() == doc.bits()) { return *made; }
        }
        return top;
    };

    // --- what a node has to be for the selection to point at it ---------------
    //
    // "If node's root is not the document associated with this, abort these
    // steps" - ABORT, not throw, and it is why a selection cannot be moved
    // into a detached fragment or into another document's tree.
    const auto rooted_here = [](dom_bindings & d, value v) {
        if (d.is_the_document(v)) { return true; }
        if (d.owner_of(v) != &d) { return false; }
        const node_id id = d.handle_of(v);
        if (!id) { return false; }
        const auto txn = d.doc_->read();
        const node_id top = d.root_of_tree(txn, id, true);
        return top == txn.root() ||
               txn.kind(top).value_or(node_kind::element) == node_kind::document;
    };

    // EVERY CHANGE OF THE RANGE GOES THROUGH HERE, and schedules a
    // `selectionchange` (Selection API 5): a new range, none, or one a method
    // moved. ONE task per burst - the flag is set until the task runs - and
    // the task CLEARS it before it fires, so a listener that moves the
    // selection again gets one more event, in a later task. The event bubbles
    // and cannot be cancelled, and it is fired at the document.
    // A SELECTION PUT INSIDE AN EDITING HOST FOCUSES THAT HOST, as every
    // browser does (contenteditable/collapse.html moves it from host to host,
    // and into one nested in another). The host is the outermost element of
    // the run of editable ancestors the focus sits in - `contenteditable`
    // true, empty or plaintext-only, inherited through an invalid value and
    // stopped by `false`.
    const auto focus_editing_host = [](context & c, dom_bindings & d, value range, bool backwards) {
        const node_id focus =
            d.handle_of(c.lookup_property(range, backwards ? "startContainer" : "endContainer"));
        if (!focus) { return; }
        node_id host;
        {
            const auto txn = d.doc_->read();
            const atom attr = d.atoms_->intern("contenteditable");
            const auto editable = [&](node_id element) {
                for (node_id at = element; at; at = txn.parent(at)) {
                    if (!txn.tag(at) || !txn.has_attribute(at, attr)) { continue; }
                    const std::string state = ascii_lower_copy(txn.attribute_value(at, attr));
                    if (state.empty() || state == "true" || state == "plaintext-only") {
                        return true;
                    }
                    if (state == "false") { return false; }
                }
                return false;
            };
            for (node_id at = txn.tag(focus) ? focus : txn.parent(focus);
                 at && txn.tag(at) && editable(at); at = txn.parent(at)) {
                host = at;
            }
        }
        if (!host) { return; }
        const value element = d.wrap(c, host);
        if (c.lookup_property(d.document_, "activeElement").bits() == element.bits()) { return; }
        (void)call_on(c, element, "focus", {});
    };

    const auto store = [this, home, focus_editing_host](context & c, value range, bool backwards,
                                                        bool directionless = false) {
        script::object_object * self = self_object(c);
        if (self == nullptr) { return; }
        dom_bindings * at = &home(c);
        const value before = held_slot(self, range_slot);
        store_range(c, range, backwards, directionless);
        if (range.is_object()) { focus_editing_host(c, *at, range, backwards); }
        if (!before.is_object() && !range.is_object()) { return; }
        if (context::truthy(held_slot(self, scheduled_slot))) { return; }
        self->set(std::string{scheduled_slot}, value::boolean(true));
        // ON THE PAGE'S QUEUE, which is the one the tick runs - a frame's
        // own timers are not - and guarded, because a frame can be removed
        // before the task comes round.
        (void)primary().add_timer(
            native(c, "selectionchange",
                   [this, at, self](context & inner, std::span<value>) {
                       self->set(std::string{scheduled_slot}, value::boolean(false));
                       dom_bindings & top = primary();
                       if (at != &top &&
                           std::ranges::none_of(top.secondary_documents_, [at](const auto & held) {
                               return held.get() == at;
                           })) {
                           return value::undefined();
                       }
                       const value event = at->make_event(inner, "selectionchange", node_id{});
                       static_cast<script::object_object *>(event.as_heap())
                           ->set("cancelable", value::boolean(false));
                       (void)at->dispatch_event("selectionchange", node_id{}, event);
                       return value::undefined();
                   }),
            0, false);
    };

    // A fresh Range of this document collapsed at (node, offset). `setStart`
    // is where a doctype and an offset past the end are refused.
    const auto range_at = [home](context & c, value node, value offset) -> value {
        const value made = home(c).create_range(c);
        (void)call_on(c, made, "setStart", {node, offset});
        if (c.throw_pending()) { return value::undefined(); }
        (void)call_on(c, made, "setEnd", {node, offset});
        return c.throw_pending() ? value::undefined() : made;
    };

    // A RANGE, as the argument conversion of addRange/removeRange means it: a
    // live range of the realm - a Selection, a StaticRange or a plain object
    // is a TypeError.
    const auto is_live_range = [this](value v) {
        if (!v.is_object()) { return false; }
        const std::vector<value> & held = primary().live_ranges_;
        return static_cast<script::object_object *>(v.as_heap())->find("__startContainer") !=
                   nullptr &&
               std::ranges::any_of(held, [&](const value & one) { return one.bits() == v.bits(); });
    };

    // --- points in different trees of the document ---------------------------
    //
    // THE SHADOW-INCLUDING ORDER OF TWO BOUNDARY POINTS: each is lifted out of
    // the shadow trees the other is not inside - a point in a shadow tree
    // stands at its host, BETWEEN (parent, index) and (parent, index + 1),
    // which the half offset says - and the two are then compared in one tree
    // as DOM 5.2 compares boundary points. -1, 0 or 1.
    const auto node_of = [](dom_bindings & d, value v) {
        return d.is_the_document(v) ? d.doc_->document_node() : d.handle_of(v);
    };
    const auto compare_composed = [](dom_bindings & d, node_id a, double a_offset, node_id b,
                                     double b_offset) -> int {
        const auto txn = d.doc_->read();
        const auto roots = [&](node_id n) {
            std::vector<node_id> out;
            for (int guard = 0; n && guard < 64; ++guard) {
                const node_id root = d.root_of_tree(txn, n, false);
                out.push_back(root);
                const shadow_tree * tree = d.shadow_tree_of(root);
                if (tree == nullptr) { break; }
                n = tree->host;
            }
            return out;
        };
        const std::vector<node_id> a_roots = roots(a);
        const std::vector<node_id> b_roots = roots(b);
        node_id common;
        for (const node_id one : a_roots) {
            if (std::ranges::find(b_roots, one) != b_roots.end()) {
                common = one;
                break;
            }
        }
        const auto index_of = [&](node_id n) {
            const std::span<const node_id> row = txn.children(dom_parent(txn, n));
            const auto here = std::ranges::find(row, n);
            return static_cast<double>(here - row.begin());
        };
        const auto lift = [&](node_id & n, double & offset) {
            for (int guard = 0; n && guard < 64; ++guard) {
                const node_id root = d.root_of_tree(txn, n, false);
                const shadow_tree * tree = root == common ? nullptr : d.shadow_tree_of(root);
                if (tree == nullptr) { return; }
                offset = index_of(tree->host) + 0.5;
                n = dom_parent(txn, tree->host);
            }
        };
        lift(a, a_offset);
        lift(b, b_offset);
        if (a == b) { return a_offset < b_offset ? -1 : a_offset > b_offset ? 1 : 0; }
        const auto chain = [&](node_id n) {
            std::vector<node_id> up;
            for (; n; n = dom_parent(txn, n)) { up.push_back(n); }
            std::ranges::reverse(up);
            return up;
        };
        const std::vector<node_id> a_up = chain(a);
        const std::vector<node_id> b_up = chain(b);
        std::size_t shared = 0;
        while (shared < a_up.size() && shared < b_up.size() && a_up[shared] == b_up[shared]) {
            ++shared;
        }
        // One contains the other: where the other's branch leaves it.
        if (shared == a_up.size()) { return index_of(b_up[shared]) < a_offset ? 1 : -1; }
        if (shared == b_up.size()) { return index_of(a_up[shared]) < b_offset ? -1 : 1; }
        if (shared == 0) { return 0; }
        return index_of(a_up[shared]) < index_of(b_up[shared]) ? -1 : 1;
    };
    // THE DOCUMENT IS IN ITS OWN TREE: root_of_tree stops at the document
    // element, which has no parent pointer, so a point at (document, n) and
    // one inside the element are told apart unless the two roots are read as
    // the one Document (extend-00.html's `[document, 0]` points).
    const auto same_tree = [](dom_bindings & d, node_id a, node_id b) {
        const auto txn = d.doc_->read();
        const auto top = [&](node_id n) {
            const node_id root = d.root_of_tree(txn, n, false);
            return root == txn.root() ? txn.document_node() : root;
        };
        return top(a) == top(b);
    };
    // A selection whose anchor and focus are in DIFFERENT trees: the live
    // range collapses where DOM's "set the start" then "set the end" leave it
    // - at the later point - and the two points are kept beside it.
    const auto store_across = [store](context & c, value anchor_node, value anchor_offset,
                                      value focus_node, value focus_offset, bool backwards,
                                      value collapsed) {
        store(c, collapsed, backwards);
        if (script::object_object * self = self_object(c)) {
            self->set(std::string{anchor_node_slot}, anchor_node);
            self->set(std::string{anchor_offset_slot}, anchor_offset);
            self->set(std::string{focus_node_slot}, focus_node);
            self->set(std::string{focus_offset_slot}, focus_offset);
            self->set(std::string{across_slot}, value::boolean(true));
        }
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
    method("addRange", [home, rooted_here, store, is_live_range](context & c, std::span<value> a) {
        const value range = arg(a, 0);
        if (!is_live_range(range)) {
            c.throw_error("TypeError", "Failed to execute 'addRange' on 'Selection': parameter 1 "
                                       "is not of type 'Range'.");
            return value::undefined();
        }
        if (!rooted_here(home(c), c.lookup_property(range, "startContainer"))) {
            return value::undefined();
        }
        if (range_of(c).is_object()) { return value::undefined(); }
        store(c, range, false);
        return value::undefined();
    });
    method("removeRange", [this, store, is_live_range](context & c, std::span<value> a) {
        const value range = arg(a, 0);
        if (!is_live_range(range)) {
            c.throw_error("TypeError", "Failed to execute 'removeRange' on 'Selection': parameter "
                                       "1 is not of type 'Range'.");
            return value::undefined();
        }
        if (range_of(c).bits() != range.bits()) {
            throw_dom_exception(c, "NotFoundError",
                                "removeRange: that range is not in the selection");
            return value::undefined();
        }
        store(c, value::undefined(), false);
        return value::undefined();
    });
    for (const char * name : {"removeAllRanges", "empty"}) {
        method(name, [store](context & c, std::span<value>) {
            store(c, value::undefined(), false);
            return value::undefined();
        });
    }
    // `collapse(null)` is `removeAllRanges()` - the one place in this API
    // where null is a value an argument may have rather than a missing one.
    for (const char * name : {"collapse", "setPosition"}) {
        method(name, [home, range_at, rooted_here, store](context & c, std::span<value> a) {
            const value node = arg(a, 0);
            if (node.is_null() || node.is_undefined()) {
                store(c, value::undefined(), false);
                return value::undefined();
            }
            // VALIDATE, THEN ABORT - in that order, which is observable: a
            // detached node with an offset past its end is an IndexSizeError
            // and NOT a silent no-op, because the specification checks the
            // node kind and the offset before it asks where the node is.
            const value made = range_at(c, node, value::number(arg_number(a, 1)));
            if (c.throw_pending() || !rooted_here(home(c), node)) { return value::undefined(); }
            if (made.is_object()) { store(c, made, false, true); }
            return value::undefined();
        });
    }
    for (const bool to_start : {true, false}) {
        method(to_start ? "collapseToStart" : "collapseToEnd",
               [this, to_start, store](context & c, std::span<value>) {
                   const value range = range_of(c);
                   if (!range.is_object()) {
                       throw_dom_exception(c, "InvalidStateError",
                                           "collapseToStart: there is no range to collapse");
                       return value::undefined();
                   }
                   const value made = call_on(c, range, "cloneRange", {});
                   if (!made.is_object()) { return value::undefined(); }
                   (void)call_on(c, made, "collapse", {value::boolean(to_start)});
                   store(c, made, false, true);
                   return value::undefined();
               });
    }
    // `extend(node, offset)`: the ANCHOR stays where it is and the focus moves
    // to the new point, so the range grows in whichever direction that point
    // lies - and the direction follows it.
    method("extend", [this, home, rooted_here, store, range_at, node_of, same_tree,
                      compare_composed, store_across](context & c, std::span<value> a) {
        dom_bindings & d = home(c);
        const value range = range_of(c);
        if (!range.is_object()) {
            throw_dom_exception(c, "InvalidStateError", "extend: there is no range to extend");
            return value::undefined();
        }
        const value node = arg(a, 0);
        if (!rooted_here(d, node)) { return value::undefined(); }
        const value offset = value::number(arg_number(a, 1));
        const bool backwards = is_backwards(c);
        const value anchor_node =
            c.lookup_property(range, backwards ? "endContainer" : "startContainer");
        const value anchor_offset =
            c.lookup_property(range, backwards ? "endOffset" : "startOffset");
        // Step 6: A NEW FOCUS IN ANOTHER TREE than the range - inside a
        // shadow root the anchor is outside of, or the other way round - and
        // the range collapses at the new focus. The two points are kept beside
        // it, as setBaseAndExtent keeps them, for getComposedRanges().
        if (const node_id anchor_id = node_of(d, anchor_node), focus_id = node_of(d, node);
            anchor_id && focus_id && !same_tree(d, anchor_id, focus_id)) {
            const value focus_at = range_at(c, node, offset);
            if (c.throw_pending()) { return value::undefined(); }
            const bool before = compare_composed(d, focus_id, context::to_number(offset), anchor_id,
                                                 context::to_number(anchor_offset)) < 0;
            store_across(c, anchor_node, anchor_offset, node, offset, before, focus_at);
            return value::undefined();
        }
        const value made = d.create_range(c);
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
        store(c, made, side < 0);
        return value::undefined();
    });
    method("setBaseAndExtent", [home, rooted_here, store, range_at, node_of, same_tree,
                                compare_composed, store_across](context & c, std::span<value> a) {
        dom_bindings & d = home(c);
        // FOUR REQUIRED ARGUMENTS: fewer is WebIDL's TypeError, before any of
        // them is converted.
        if (a.size() < 4) {
            c.throw_error("TypeError", "Failed to execute 'setBaseAndExtent' on 'Selection': 4 "
                                       "arguments required.");
            return value::undefined();
        }
        const value anchor_node = arg(a, 0);
        const value anchor_offset = value::number(arg_number(a, 1));
        const value focus_node = arg(a, 2);
        const value focus_offset = value::number(arg_number(a, 3));
        // The offsets are checked against each node BEFORE the root is (steps
        // 1-2) - an IndexSizeError from either comes first - by collapsing a
        // fresh range at each point.
        const value made = range_at(c, anchor_node, anchor_offset);
        if (c.throw_pending()) { return value::undefined(); }
        const value focus_at = range_at(c, focus_node, focus_offset);
        if (c.throw_pending()) { return value::undefined(); }
        if (!rooted_here(d, anchor_node) || !rooted_here(d, focus_node)) {
            return value::undefined();
        }
        if (const node_id anchor_id = node_of(d, anchor_node), focus_id = node_of(d, focus_node);
            anchor_id && focus_id && !same_tree(d, anchor_id, focus_id)) {
            const bool backwards =
                compare_composed(d, focus_id, context::to_number(focus_offset), anchor_id,
                                 context::to_number(anchor_offset)) < 0;
            store_across(c, anchor_node, anchor_offset, focus_node, focus_offset, backwards,
                         backwards ? made : focus_at);
            return value::undefined();
        }
        const double side =
            context::to_number(call_on(c, made, "comparePoint", {focus_node, focus_offset}));
        if (c.throw_pending()) { return value::undefined(); }
        (void)call_on(c, made, side < 0 ? "setStart" : "setEnd", {focus_node, focus_offset});
        if (c.throw_pending()) { return value::undefined(); }
        store(c, made, side < 0);
        return value::undefined();
    });
    // `selectAllChildren(node)`: (node, 0) to (node, THE NUMBER OF CHILDREN) -
    // which is not `selectNodeContents`, and the difference is a text node.
    // Range's "length" is a text node's DATA length, so selectNodeContents on
    // one selects its text; selectAllChildren counts children and selects
    // nothing. selectAllChildren.html asserts `focusOffset ===
    // node.childNodes.length` for every node it has, text nodes included.
    method("selectAllChildren", [home, rooted_here, store](context & c, std::span<value> a) {
        dom_bindings & d = home(c);
        const value node = arg(a, 0);
        // `is_object_like`: a Document is a PROXY, and `is_object()` is false
        // for one - selectAllChildren(document) is a call on a Node.
        if (!node.is_object_like()) {
            c.throw_error("TypeError", "Failed to execute 'selectAllChildren' on 'Selection': "
                                       "parameter 1 is not of type 'Node'.");
            return value::undefined();
        }
        const node_id id = d.is_the_document(node) ? d.doc_->document_node() : d.handle_of(node);
        const auto children = id ? d.doc_->read().children(id).size() : 0u;
        const value made = d.create_range(c);
        // A doctype is an InvalidNodeTypeError, and it comes from `setStart`
        // before anything is asked about where the node is.
        (void)call_on(c, made, "setStart", {node, value::number(0)});
        if (c.throw_pending()) { return value::undefined(); }
        (void)call_on(c, made, "setEnd", {node, value::number(static_cast<double>(children))});
        if (c.throw_pending() || !rooted_here(d, node)) { return value::undefined(); }
        store(c, made, false);
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
    method("containsNode", [home](context & c, std::span<value> a) {
        dom_bindings & d = home(c);
        const value range = range_of(c);
        const value node = arg(a, 0);
        if (!range.is_object() || !node.is_object() || d.owner_of(node) != &d) {
            return value::boolean(false);
        }
        if (context::truthy(arg(a, 1))) {
            return value::boolean(context::truthy(call_on(c, range, "intersectsNode", {node})));
        }
        const node_id id = d.handle_of(node);
        if (!id || !dom_parent(d.doc_->read(), id)) { return value::boolean(false); }
        // NOT IN THE FLAT TREE, NOT IN THE SELECTION: a host's child no slot
        // took, or a slot's fallback content while something is assigned to
        // it, is not rendered - and so not selected, whatever the DOM range
        // spans (selection-at-nodes-not-part-of-flattened-tree.html).
        {
            const auto txn = d.doc_->read();
            for (node_id at = id; at;) {
                const node_id parent = txn.parent(at);
                if (!parent) { break; }
                if (d.shadow_root_of(parent) && !d.assigned_slot_of(at)) {
                    return value::boolean(false);
                }
                if (txn.tag(parent) && txn.element_ns(parent) == node_ns::html &&
                    txn.local_name(parent) == "slot" && !d.assigned_nodes_of(parent).empty()) {
                    return value::boolean(false);
                }
                at = parent;
            }
        }
        const value about = d.create_range(c);
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
    // `getComposedRanges(options)` (Selection API): the selection as a
    // StaticRange from its first point to its last, each RESCOPED out of every
    // shadow tree the caller did not name - a point inside one stands at its
    // host, before it for the start and after it for the end - so a page is
    // told nothing about a tree it was not given. `shadowRoots` must hold
    // ShadowRoots; the older variadic spelling is taken as the same list.
    method("getComposedRanges", [home, node_of](context & c, std::span<value> args) {
        dom_bindings & d = home(c);
        const value made = c.make_array();
        std::vector<node_id> allowed;
        const auto allow = [&](value one) {
            const node_id id = d.handle_of(one);
            if (!id || d.shadow_tree_of(id) == nullptr) {
                c.throw_error("TypeError", "Failed to execute 'getComposedRanges' on "
                                           "'Selection': a member of shadowRoots is not a "
                                           "ShadowRoot.");
                return false;
            }
            allowed.push_back(id);
            return true;
        };
        const value first = arg(args, 0);
        if (d.handle_of(first)) {
            for (const value & one : args) {
                if (!allow(one)) { return value::undefined(); }
            }
        } else if (const value list = dict_member(c, first, "shadowRoots"); list.is_array()) {
            for (const value & one : static_cast<script::array_object *>(list.as_heap())->items) {
                if (!allow(one)) { return value::undefined(); }
            }
        }
        const value range = range_of(c);
        script::object_object * self = self_object(c);
        const value ctor = c.global("StaticRange");
        if (!range.is_object() || self == nullptr || !ctor.is_callable()) { return made; }
        // The range's own ends, unless the selection is across trees - then
        // the anchor and focus, first one first.
        const bool across = context::truthy(held_slot(self, across_slot));
        const bool backwards = is_backwards(c);
        const auto point = [&](bool first, bool offset) {
            if (!across) {
                return c.lookup_property(range, first ? (offset ? "startOffset" : "startContainer")
                                                      : (offset ? "endOffset" : "endContainer"));
            }
            const bool anchor = first != backwards;
            return held_slot(self, anchor ? (offset ? anchor_offset_slot : anchor_node_slot)
                                          : (offset ? focus_offset_slot : focus_node_slot));
        };
        value start_node = point(true, false);
        double start_offset = context::to_number(point(true, true));
        value end_node = point(false, false);
        double end_offset = context::to_number(point(false, true));
        const auto rescope = [&](value & node, double & offset, bool end) {
            const auto txn = d.doc_->read();
            node_id at = node_of(d, node);
            for (int guard = 0; at && guard < 64; ++guard) {
                const node_id root = d.root_of_tree(txn, at, false);
                const shadow_tree * tree = d.shadow_tree_of(root);
                if (tree == nullptr) { return; }
                // Stop at a root that is a shadow-including inclusive ancestor
                // of one the caller named.
                for (const node_id named : allowed) {
                    for (node_id b = named; b;) {
                        if (b == root) { return; }
                        const node_id up = txn.parent(b);
                        const shadow_tree * above = up ? nullptr : d.shadow_tree_of(b);
                        b = up ? up : above == nullptr ? node_id{} : above->host;
                    }
                }
                const node_id parent = dom_parent(txn, tree->host);
                const std::span<const node_id> row = txn.children(parent);
                offset = static_cast<double>(std::ranges::find(row, tree->host) - row.begin()) +
                         (end ? 1 : 0);
                at = parent;
                node = parent == d.doc_->document_node() ? d.document_ : d.wrap(c, parent);
            }
        };
        rescope(start_node, start_offset, false);
        rescope(end_node, end_offset, true);
        const value init = value::object(c.allocate<script::object_object>());
        c.store_property(init, "startContainer", start_node);
        c.store_property(init, "startOffset", value::number(start_offset));
        c.store_property(init, "endContainer", end_node);
        c.store_property(init, "endOffset", value::number(end_offset));
        const value one = c.construct(ctor, std::span<const value>{&init, 1});
        if (one.is_object()) {
            static_cast<script::array_object *>(made.as_heap())->items.push_back(one);
        }
        return made;
    });
    proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("Selection"),
                  script::attr_configurable);

    auto * ctor =
        cx.allocate<script::native_object>("Selection", [](context & c, std::span<value>) {
            c.throw_error("TypeError", "Illegal constructor");
            return value::undefined();
        });
    ctor->define("prototype", proto_value, script::attr_none);
    proto->define("constructor", value::object(ctor), script::attr_builtin);
    cx.define_builtin_global("Selection", value::object(ctor));

    // THE PAGE'S SELECTION, made now, of this document.
    auto * selection = cx.allocate<script::object_object>();
    selection->prototype = proto_value;
    selection->set(std::string{document_slot}, document_);
    const value selection_value = value::object(selection);

    // A FRAME'S, made the first time its window or its document is asked and
    // kept on its window object from then on - so `getSelection() ===
    // document.getSelection()` inside it, and neither is the page's. A
    // document with no browsing context (createHTMLDocument, DOMParser) has
    // none, and answers null.
    const auto selection_of = [this, proto_value, selection_value](context & c,
                                                                   dom_bindings & d) -> value {
        if (&d == &primary()) { return selection_value; }
        if (d.realm_ == nullptr) { return value::null(); }
        script::object_object * window = d.window_object();
        if (window == nullptr) { return value::null(); }
        if (const value * held = window->find(window_selection_key)) { return *held; }
        auto * made = c.allocate<script::object_object>();
        made->prototype = proto_value;
        made->set(std::string{document_slot}, d.document_);
        window->define(std::string{window_selection_key}, value::object(made),
                       script::attr_configurable);
        return value::object(made);
    };
    // The bindings a receiver is the window or the document of: a frame's
    // window view or document when it is one, else the page's.
    const auto bindings_of = [this](context & c, value receiver) -> dom_bindings & {
        dom_bindings & top = primary();
        const auto names = [&](dom_bindings & d) {
            if (!receiver.is_heap()) { return false; }
            if (d.window_view_.is_heap() && d.window_view_.bits() == receiver.bits()) {
                return true;
            }
            if (d.document_.is_heap() && d.document_.bits() == receiver.bits()) { return true; }
            return d.document_object() != nullptr && receiver.is_object() &&
                   receiver.as_heap() == d.document_object();
        };
        for (const auto & made : top.secondary_documents_) {
            if (names(*made)) { return *made; }
        }
        if (names(top)) { return top; }
        // A bare `getSelection()` from a frame's own script: the frame whose
        // realm is running.
        script::realm_record & running = c.current_realm();
        for (const auto & made : top.secondary_documents_) {
            if (made->realm_ == &running) { return *made; }
        }
        return top;
    };

    // `window.getSelection()`. Retained by the function that hands it out,
    // which is a global and therefore a root.
    auto * getter = cx.allocate<script::native_object>(
        "getSelection", [bindings_of, selection_of](context & c, std::span<value>) {
            return selection_of(c, bindings_of(c, c.current_this()));
        });
    getter->retained.push_back(selection_value);
    cx.define_global("getSelection", value::object(getter));

    // `document.getSelection()` - THE SAME OBJECT, and null for a document with
    // no browsing context.
    //
    // ON THE PAGE'S OWN DOCUMENT IT IS AN OWN PROPERTY, and that is not
    // belt and braces: `document` is a PROXY, and a method reached through it
    // does not arrive with the document as its receiver - the prototype method
    // below could not tell the page's document from another, so
    // `getSelection() === document.getSelection()` was false. This one
    // answers with the selection and asks nothing.
    if (auto * doc = document_object()) {
        auto * own = cx.allocate<script::native_object>(
            "getSelection",
            [selection_value](context &, std::span<value>) { return selection_value; });
        own->retained.push_back(selection_value);
        doc->set("getSelection", value::object(own));
        doc->set_attrs("getSelection", script::attr_builtin);
    }
    // AND ON Document.prototype for every OTHER document of the realm: a
    // frame's answers with the frame's selection, and one from
    // createHTMLDocument or DOMParser, which has no browsing context, null.
    if (const value document_proto = interface_prototype("Document"); document_proto.is_object()) {
        auto * on = static_cast<script::object_object *>(document_proto.as_heap());
        // RETAINED HERE TOO. A captured `value` is invisible to the collector,
        // and a page may delete the `getSelection` global - which would
        // otherwise leave this method holding a swept object.
        auto * method = cx.allocate<script::native_object>(
            "getSelection", [this, selection_of](context & c, std::span<value>) {
                const value self = c.current_this();
                dom_bindings & top = primary();
                for (const auto & made : top.secondary_documents_) {
                    if (made->is_the_document(self) ||
                        (self.is_object() && self.as_heap() == made->document_object())) {
                        return selection_of(c, *made);
                    }
                }
                return value::null();
            });
        method->retained.push_back(selection_value);
        on->set("getSelection", value::object(method));
        on->set_attrs("getSelection", script::attr_builtin);
    }
}

// A SELECTION'S RANGE INSIDE A SHADOW TREE WHOSE HOST WAS JUST REMOVED - the
// host itself or an ancestor of it - is no longer the selection's, even when
// the host comes straight back (`host.replaceWith(host)`,
// `parent.replaceChild(host, host)`). The live range steps leave the range
// where it is, since its ends are not inclusive descendants of the removed
// node, and the lazy check in range_of would find it connected again; so it
// is let go here, from the removal's note, as the first two cases of
// selection-range-in-shadow-after-the-shadow-removed let it go on removal
// alone. A range whose end is a PLAIN descendant was moved to the parent by
// the live range steps and stays. The page's Range object is untouched.
void dom_bindings::forget_selection_under(node_id removed) {
    if (cx_ == nullptr || doc_ == nullptr || !removed || !doc_->has_shadow_roots()) { return; }
    context & c = *cx_;
    const value selection = call_on(c, document_, "getSelection", {});
    if (!selection.is_object()) { return; }
    auto * self = static_cast<script::object_object *>(selection.as_heap());
    const value range = held_slot(self, range_slot);
    if (!range.is_object()) { return; }
    bool inside = false;
    for (const char * end : {"startContainer", "endContainer"}) {
        const node_id node = handle_of(c.lookup_property(range, end));
        const auto txn = doc_->read();
        bool crossed = false;
        for (node_id at = node; at && !inside;) {
            if (at == removed) {
                inside = crossed;
                break;
            }
            if (const node_id up = txn.parent(at)) {
                at = up;
                continue;
            }
            const shadow_tree * tree = shadow_tree_of(at);
            if (tree == nullptr) { break; }
            crossed = true;
            at = tree->host;
        }
    }
    if (!inside) { return; }
    for (const std::string_view slot :
         {range_slot, anchor_node_slot, anchor_offset_slot, focus_node_slot, focus_offset_slot}) {
        self->set(std::string{slot}, value::undefined());
    }
    self->set(std::string{across_slot}, value::boolean(false));
}

// AN EDITING HOST THAT GAINS FOCUS GETS A CARET, where every browser puts one
// (selection/contenteditable/initial-selection-on-focus.tentative.html): if
// the document's selection is not already inside the host, it collapses at
// the host's FIRST CARET POSITION - the first text node's first visible
// character, before the first <br>, or, when an element the caret cannot
// enter (contenteditable=false, a replaced or form control) comes first, the
// start of the block it sits in; the host's start when there is none of these.
//
// ponytail: "visible" is read from the markup - leading white space of a text
// node collapses unless an ancestor up to the host is <pre>/<textarea> or says
// `white-space: pre|pre-wrap|break-spaces` in its style attribute - not from
// computed style, which may not have been recomputed since the host's content
// changed.
void dom_bindings::place_caret_on_focus(node_id element) {
    if (cx_ == nullptr || !element) { return; }
    node_id at_node;
    std::size_t at_offset = 0;
    {
        const auto txn = doc_->read();
        if (txn.kind(element).value_or(node_kind::text) != node_kind::element) { return; }
        const atom editable = atoms_->intern("contenteditable");
        const std::string state = ascii_lower_copy(txn.attribute_value(element, editable));
        if (!txn.has_attribute(element, editable) ||
            (!state.empty() && state != "true" && state != "plaintext-only")) {
            return;
        }
        const auto named = [&](node_id id, std::initializer_list<std::string_view> names) {
            if (txn.element_ns(id) != node_ns::html) { return false; }
            const std::string_view local = txn.local_name(id);
            return std::ranges::find(names, local) != names.end();
        };
        const auto atomic = [&](node_id id) {
            return ascii_lower_copy(txn.attribute_value(id, editable)) == "false" ||
                   txn.element_ns(id) != node_ns::html ||
                   named(id, {"input", "img", "hr", "textarea", "select", "button", "video",
                              "audio", "iframe", "object", "embed", "canvas", "meter", "progress"});
        };
        const auto block_of = [&](node_id id) {
            for (node_id up = txn.parent(id); up && up != element; up = txn.parent(up)) {
                if (named(up, {"p",    "div",     "li",      "ul",      "ol",       "h1",
                               "h2",   "h3",      "h4",      "h5",      "h6",       "blockquote",
                               "pre",  "section", "article", "table",   "td",       "th",
                               "dd",   "dt",      "dl",      "address", "header",   "footer",
                               "main", "nav",     "aside",   "figure",  "fieldset", "form"})) {
                    return up;
                }
            }
            return element;
        };
        const auto preserves_space = [&](node_id text) {
            for (node_id up = txn.parent(text); up; up = txn.parent(up)) {
                if (named(up, {"pre", "textarea", "listing", "plaintext"})) { return true; }
                const std::string style =
                    ascii_lower_copy(txn.attribute_value(up, atoms_->intern("style")));
                if (const std::size_t found = style.find("white-space");
                    found != std::string::npos) {
                    const std::string_view rest = std::string_view{style}.substr(found);
                    return rest.find("pre-wrap") != std::string_view::npos ||
                           rest.find("break-spaces") != std::string_view::npos ||
                           (rest.find("pre") != std::string_view::npos &&
                            rest.find("pre-line") == std::string_view::npos);
                }
                if (up == element) { break; }
            }
            return false;
        };
        const auto index_in_parent = [&](node_id id) {
            const std::span<const node_id> row = txn.children(txn.parent(id));
            return static_cast<std::size_t>(std::ranges::find(row, id) - row.begin());
        };
        const auto first = [&](auto && self, node_id at) -> bool {
            for (const node_id child : txn.children(at)) {
                const node_kind kind = txn.kind(child).value_or(node_kind::comment);
                if (is_text_kind(kind)) {
                    const std::string_view data = txn.text(child);
                    std::size_t skip = 0;
                    if (!preserves_space(child)) {
                        while (skip < data.size() &&
                               (data[skip] == ' ' || data[skip] == '\t' || data[skip] == '\n' ||
                                data[skip] == '\r' || data[skip] == '\f')) {
                            ++skip;
                        }
                    }
                    if (skip >= data.size()) { continue; }
                    at_node = child;
                    at_offset = skip; // ASCII white space: a byte is a code unit
                    return true;
                }
                if (kind != node_kind::element) { continue; }
                if (named(child, {"br"})) {
                    at_node = at;
                    at_offset = index_in_parent(child);
                    return true;
                }
                if (atomic(child)) {
                    at_node = block_of(child);
                    at_offset = 0;
                    return true;
                }
                if (self(self, child)) { return true; }
            }
            return false;
        };
        if (!first(first, element)) {
            at_node = element;
            at_offset = 0;
        }
    }
    context & c = *cx_;
    const value selection = call_on(c, document_, "getSelection", {});
    if (!selection.is_object()) { return; }
    // Already inside the host: focus keeps the caret where the page put it.
    if (context::to_number(c.lookup_property(selection, "rangeCount")) > 0) {
        const node_id anchor = handle_of(c.lookup_property(selection, "anchorNode"));
        const auto txn = doc_->read();
        for (node_id up = anchor; up; up = txn.parent(up)) {
            if (up == element) { return; }
        }
    }
    (void)call_on(c, selection, "collapse",
                  {wrap(c, at_node), value::number(static_cast<double>(at_offset))});
}

} // namespace ctbrowser::shell
