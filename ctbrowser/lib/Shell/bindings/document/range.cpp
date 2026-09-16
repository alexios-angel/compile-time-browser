// dom_bindings - DOM 5, Range and StaticRange: `document.createRange()`,
// `new Range()`, `new StaticRange(init)`, and every algorithm of DOM 5.5.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// ============================================================================
// RANGE
// ============================================================================
//
// A range is two boundary points - (node, offset) twice - kept as hidden own
// properties of an ordinary object, with the IDL attributes as accessors on
// the prototype over them. The nodes are HANDLES: every read below opens a
// read_txn on the document that owns the node, which is what makes
// `compareBoundaryPoints` over the corpus's 5,000-point tables cheap enough
// to finish. A boundary node may belong to ANY document of the realm - the
// page's, an iframe's, one `createDocument` made - so the owner rides beside
// the id, and an Attr (a Node with no handle, of length 0 and rooted at
// itself) is carried as the object it is.
//
// WRITES GO THROUGH THE NODES' OWN SCRIPT SURFACE - `insertBefore`,
// `removeChild`, `cloneNode`, `replaceData`, `splitText` - because that is
// where pre-insertion validity, adoption, fragment flattening and the
// mutation records already live; a second copy here would be a second
// chance to disagree with them.
//
// ponytail: NOT LIVE TO OTHER MUTATIONS. DOM keeps every range's boundary
// points up to date as the tree changes; a range here follows only its OWN
// edits (the `splitText` insertNode does, the collapse extract does). The
// live-range list needs a hook where document::remove_child, insert_before
// and set_text run, which this file cannot reach - see the report.
// Range-mutations-*.html is what measures it.

namespace {

constexpr unsigned type_element = 1;
constexpr unsigned type_attr = 2;
constexpr unsigned type_text = 3;
constexpr unsigned type_cdata = 4;
constexpr unsigned type_pi = 7;
constexpr unsigned type_comment = 8;
constexpr unsigned type_document = 9;
constexpr unsigned type_doctype = 10;
constexpr unsigned type_fragment = 11;

constexpr std::string_view start_node_slot = "__startContainer";
constexpr std::string_view start_offset_slot = "__startOffset";
constexpr std::string_view end_node_slot = "__endContainer";
constexpr std::string_view end_offset_slot = "__endOffset";

// A NODE OF THE REALM, as a range sees it: the bindings that own it and its
// id - or an Attr object, which has neither.
struct spot {
    dom_bindings * owner = nullptr;
    node_id id;
    value attr = value::undefined();
    [[nodiscard]] bool none() const { return owner == nullptr && attr.is_undefined(); }
    [[nodiscard]] bool is_attr() const { return !attr.is_undefined(); }
    [[nodiscard]] bool operator==(const spot & other) const {
        if (is_attr() || other.is_attr()) {
            return is_attr() && other.is_attr() && attr.bits() == other.attr.bits();
        }
        return owner == other.owner && id == other.id;
    }
};
const spot no_spot{};

struct point {
    spot node;
    double offset = 0;
};

// UTF-16 CODE UNITS OVER UTF-8 BYTES: every CharacterData offset is one, and
// `length` counts them, so a boundary inside "🌠" is at 1 of 2.
[[nodiscard]] std::size_t units_length(std::string_view text) {
    std::size_t n = 0;
    for (std::size_t at = 0; at < text.size();) { n += decode_utf8(text, at) >= 0x10000 ? 2 : 1; }
    return n;
}

[[nodiscard]] script::object_object * self_object(context & c) {
    const value self = c.current_this();
    if (!self.is_object()) {
        c.throw_error("TypeError", "Illegal invocation");
        return nullptr;
    }
    return static_cast<script::object_object *>(self.as_heap());
}

[[nodiscard]] value slot(script::object_object * self, std::string_view name) {
    const value * held = self->find(name);
    return held == nullptr ? value::undefined() : *held;
}

} // namespace

void dom_bindings::install_range(context & cx) {
    // --- the node primitives, each read_txn long -----------------------------

    const auto from_value = [this](context & c, value v) -> spot {
        if (v.is_object_like()) {
            dom_bindings * top = primary_ == nullptr ? this : primary_;
            if (top->is_the_document(v)) { return spot{top, top->doc_->document_node()}; }
            for (const auto & made : top->secondary_documents_) {
                if (made->is_the_document(v)) {
                    return spot{made.get(), made->doc_->document_node()};
                }
            }
        }
        if (dom_bindings * owner = owner_of(v)) { return spot{owner, owner->handle_of(v)}; }
        // An Attr: a Node with no handle. `nodeType` is what says so.
        if (v.is_object() && context::to_number(c.lookup_property(v, "nodeType")) == type_attr) {
            return spot{nullptr, node_id{}, v};
        }
        return no_spot;
    };
    const auto to_value = [](context & c, spot at) {
        return at.is_attr() ? at.attr : at.owner->wrap(c, at.id);
    };
    const auto type_of = [](spot at) -> unsigned {
        if (at.is_attr()) { return type_attr; }
        switch (at.owner->doc_->read().kind(at.id).value_or(node_kind::element)) {
        case node_kind::text: return type_text;
        case node_kind::cdata_section: return type_cdata;
        case node_kind::processing_instruction: return type_pi;
        case node_kind::comment: return type_comment;
        case node_kind::document: return type_document;
        case node_kind::document_type: return type_doctype;
        case node_kind::document_fragment: return type_fragment;
        case node_kind::element: break;
        }
        return type_element;
    };
    const auto is_character_data = [type_of](spot at) {
        const unsigned t = type_of(at);
        return t == type_text || t == type_cdata || t == type_pi || t == type_comment;
    };
    const auto is_text = [type_of](spot at) {
        const unsigned t = type_of(at);
        return t == type_text || t == type_cdata;
    };
    const auto text_of = [](spot at) { return std::string{at.owner->doc_->read().text(at.id)}; };
    // "Length" of a node, DOM 4.2: code units for character data, children
    // otherwise, 0 for a doctype and an Attr.
    const auto length_of = [type_of, is_character_data](spot at) -> double {
        if (at.is_attr()) { return 0; }
        if (is_character_data(at)) {
            return static_cast<double>(units_length(at.owner->doc_->read().text(at.id)));
        }
        if (type_of(at) == type_doctype) { return 0; }
        return static_cast<double>(at.owner->doc_->read().children(at.id).size());
    };
    const auto parent_of = [](spot at) -> spot {
        if (at.is_attr()) { return no_spot; }
        const auto txn = at.owner->doc_->read();
        const node_id up = dom_parent(txn, at.id);
        return up ? spot{at.owner, up} : no_spot;
    };
    const auto children_of = [](spot at) {
        std::vector<node_id> out;
        if (at.is_attr()) { return out; }
        const auto txn = at.owner->doc_->read();
        const std::span<const node_id> kids = txn.children(at.id);
        out.assign(kids.begin(), kids.end());
        return out;
    };
    const auto index_of = [parent_of, children_of](spot at) -> double {
        const spot up = parent_of(at);
        if (up.none()) { return 0; }
        const std::vector<node_id> kids = children_of(up);
        const auto here = std::ranges::find(kids, at.id);
        return here == kids.end() ? 0 : static_cast<double>(here - kids.begin());
    };
    const auto root_of = [parent_of](spot at) {
        for (spot up = parent_of(at); !up.none(); up = parent_of(at)) { at = up; }
        return at;
    };
    const auto inclusive_ancestor = [parent_of](spot ancestor, spot node) {
        for (spot at = node; !at.none(); at = parent_of(at)) {
            if (at == ancestor) { return true; }
        }
        return false;
    };
    // TREE ORDER of two nodes of one tree: -1 when `a` precedes `b`, 1 when
    // it follows, 0 when they are the same. An ancestor precedes its
    // descendants. The two chains are walked up to the root and compared
    // from the top.
    const auto compare_order = [parent_of, children_of](spot a, spot b) -> int {
        if (a == b) { return 0; }
        const auto chain = [parent_of](spot at) {
            std::vector<spot> out;
            for (; !at.none(); at = parent_of(at)) { out.push_back(at); }
            std::ranges::reverse(out);
            return out;
        };
        const std::vector<spot> ca = chain(a);
        const std::vector<spot> cb = chain(b);
        std::size_t i = 0;
        while (i < ca.size() && i < cb.size() && ca[i] == cb[i]) { ++i; }
        if (i == ca.size()) { return -1; } // a is an ancestor of b
        if (i == cb.size()) { return 1; }  // b is an ancestor of a
        const std::vector<node_id> kids = children_of(ca[i - 1]);
        const auto ia = std::ranges::find(kids, ca[i].id);
        const auto ib = std::ranges::find(kids, cb[i].id);
        return ia < ib ? -1 : 1;
    };
    // DOM 5.2 "position of a boundary point": -1 before, 0 equal, 1 after.
    const auto position = [compare_order, inclusive_ancestor, parent_of,
                           index_of](auto && self, point a, point b) -> int {
        if (a.node == b.node) { return a.offset < b.offset ? -1 : (a.offset > b.offset ? 1 : 0); }
        if (compare_order(b.node, a.node) < 0) { return -self(self, b, a); }
        if (inclusive_ancestor(a.node, b.node)) {
            spot child = b.node;
            while (!(parent_of(child) == a.node)) { child = parent_of(child); }
            if (index_of(child) < a.offset) { return 1; }
        }
        return -1;
    };
    const auto before = [position](point a, point b) { return position(position, a, b) < 0; };
    const auto after = [position](point a, point b) { return position(position, a, b) > 0; };

    // --- the boundary points of `this` ---------------------------------------

    const auto start_of = [from_value](context & c, script::object_object * self) {
        return point{from_value(c, slot(self, start_node_slot)),
                     context::to_number(slot(self, start_offset_slot))};
    };
    const auto end_of = [from_value](context & c, script::object_object * self) {
        return point{from_value(c, slot(self, end_node_slot)),
                     context::to_number(slot(self, end_offset_slot))};
    };
    const auto set_start = [to_value](context & c, script::object_object * self, point p) {
        self->set(std::string{start_node_slot}, to_value(c, p.node));
        self->set(std::string{start_offset_slot}, value::number(p.offset));
    };
    const auto set_end = [to_value](context & c, script::object_object * self, point p) {
        self->set(std::string{end_node_slot}, to_value(c, p.node));
        self->set(std::string{end_offset_slot}, value::number(p.offset));
    };
    const auto store_points = [](script::object_object & on, value start_node, double start_offset,
                                 value end_node, double end_offset) {
        on.define(std::string{start_node_slot}, start_node, script::attr_none);
        on.define(std::string{start_offset_slot}, value::number(start_offset), script::attr_none);
        on.define(std::string{end_node_slot}, end_node, script::attr_none);
        on.define(std::string{end_offset_slot}, value::number(end_offset), script::attr_none);
    };

    // The AbstractRange attributes, on both prototypes.
    const auto install_abstract = [&](script::object_object & proto) {
        for (const auto & [name, which] :
             std::initializer_list<std::pair<const char *, std::string_view>>{
                 {"startContainer", start_node_slot},
                 {"startOffset", start_offset_slot},
                 {"endContainer", end_node_slot},
                 {"endOffset", end_offset_slot}}) {
            define_getter(cx, proto, name, [which](context & c, std::span<value>) {
                script::object_object * self = self_object(c);
                return self == nullptr ? value::undefined() : slot(self, which);
            });
        }
        define_getter(cx, proto, "collapsed", [](context & c, std::span<value>) {
            script::object_object * self = self_object(c);
            if (self == nullptr) { return value::undefined(); }
            return value::boolean(slot(self, start_node_slot).bits() ==
                                      slot(self, end_node_slot).bits() &&
                                  context::to_number(slot(self, start_offset_slot)) ==
                                      context::to_number(slot(self, end_offset_slot)));
        });
    };

    auto * proto = cx.allocate<script::object_object>();
    const value proto_value = value::object(proto);
    install_abstract(*proto);

    // --- the script surface a write goes through -----------------------------

    const auto call_on = [to_value](context & c, spot at, const char * name,
                                    std::vector<value> args) -> value {
        const value target = to_value(c, at);
        const value fn = c.lookup_property(target, name);
        return fn.is_callable() ? c.call(fn, args, target) : value::undefined();
    };
    const auto clone_of = [call_on](context & c, spot at, bool deep) {
        return call_on(c, at, "cloneNode", {value::boolean(deep)});
    };
    const auto append = [](context & c, value parent, value child) {
        const value fn = c.lookup_property(parent, "appendChild");
        if (fn.is_callable()) { (void)c.call(fn, std::span<const value>{&child, 1}, parent); }
    };
    const auto remove = [call_on](context & c, spot at) { (void)call_on(c, at, "remove", {}); };
    const auto substring = [to_value](context & c, spot at, double offset, double count) {
        const value target = to_value(c, at);
        const value fn = c.lookup_property(target, "substringData");
        if (!fn.is_callable()) { return std::string{}; }
        const value args[2] = {value::number(offset), value::number(count)};
        return c.to_string(c.call(fn, args, target));
    };
    const auto replace_data = [call_on](context & c, spot at, double offset, double count,
                                        std::string with) {
        (void)call_on(c, at, "replaceData",
                      {value::number(offset), value::number(count), c.string(std::move(with))});
    };
    // A shallow clone of a character data node holding `data` - what the
    // partial ends of an extract are made of.
    const auto clone_with_data = [clone_of](context & c, spot at, std::string data) {
        const value clone = clone_of(c, at, false);
        if (clone.is_object_like()) { c.store_property(clone, "data", c.string(std::move(data))); }
        return clone;
    };
    const auto new_fragment = [call_on](context & c, spot at) {
        // "node document" of the node: the owner's document object.
        if (at.is_attr()) {
            const value owner = c.lookup_property(at.attr, "ownerDocument");
            const value fn = c.lookup_property(owner, "createDocumentFragment");
            return fn.is_callable() ? c.call(fn, {}, owner) : value::undefined();
        }
        return call_on(c, spot{at.owner, at.owner->doc_->document_node()}, "createDocumentFragment",
                       {});
    };

    // --- DOM 5.5 "set the start or end" --------------------------------------
    //
    // A doctype is InvalidNodeTypeError, an offset past the node's length
    // IndexSizeError, and the other boundary follows when it would be on the
    // wrong side or in another tree.
    const auto set_boundary = [this, from_value, type_of, length_of, root_of, before, after,
                               start_of, end_of, set_start,
                               set_end](context & c, script::object_object * self, value node_value,
                                        value offset_value, bool start) {
        const spot node = from_value(c, node_value);
        if (node.none()) {
            c.throw_error("TypeError", "Range: the argument is not a Node");
            return;
        }
        if (type_of(node) == type_doctype) {
            throw_dom_exception(c, "InvalidNodeTypeError",
                                "a Range cannot start or end at a doctype");
            return;
        }
        const double offset = static_cast<double>(context::to_uint32(offset_value));
        if (offset > length_of(node)) {
            throw_dom_exception(c, "IndexSizeError", "the offset is past the end of the node");
            return;
        }
        const point bp{node, offset};
        if (start) {
            const point end = end_of(c, self);
            if (!(root_of(node) == root_of(end.node)) || after(bp, end)) { set_end(c, self, bp); }
            set_start(c, self, bp);
        } else {
            const point begin = start_of(c, self);
            if (!(root_of(node) == root_of(begin.node)) || before(bp, begin)) {
                set_start(c, self, bp);
            }
            set_end(c, self, bp);
        }
    };
    const auto method = [&](const char * name, script::native_fn fn) {
        set_method(cx, *proto, name, std::move(fn), script::attr_builtin);
    };
    method("setStart", [set_boundary](context & c, std::span<value> a) {
        if (script::object_object * self = self_object(c)) {
            set_boundary(c, self, arg(a, 0), arg(a, 1), true);
        }
        return value::undefined();
    });
    method("setEnd", [set_boundary](context & c, std::span<value> a) {
        if (script::object_object * self = self_object(c)) {
            set_boundary(c, self, arg(a, 0), arg(a, 1), false);
        }
        return value::undefined();
    });
    // The four "before/after a node" setters: the node's parent and index.
    const auto beside = [this, from_value, parent_of, index_of, to_value,
                         set_boundary](context & c, value node_value, bool after_node, bool start) {
        script::object_object * self = self_object(c);
        if (self == nullptr) { return; }
        const spot node = from_value(c, node_value);
        if (node.none()) {
            c.throw_error("TypeError", "Range: the argument is not a Node");
            return;
        }
        const spot parent = parent_of(node);
        if (parent.none()) {
            throw_dom_exception(c, "InvalidNodeTypeError", "the node has no parent");
            return;
        }
        set_boundary(c, self, to_value(c, parent),
                     value::number(index_of(node) + (after_node ? 1 : 0)), start);
    };
    method("setStartBefore", [beside](context & c, std::span<value> a) {
        beside(c, arg(a, 0), false, true);
        return value::undefined();
    });
    method("setStartAfter", [beside](context & c, std::span<value> a) {
        beside(c, arg(a, 0), true, true);
        return value::undefined();
    });
    method("setEndBefore", [beside](context & c, std::span<value> a) {
        beside(c, arg(a, 0), false, false);
        return value::undefined();
    });
    method("setEndAfter", [beside](context & c, std::span<value> a) {
        beside(c, arg(a, 0), true, false);
        return value::undefined();
    });
    method("collapse", [start_of, end_of, set_start, set_end](context & c, std::span<value> a) {
        script::object_object * self = self_object(c);
        if (self == nullptr) { return value::undefined(); }
        if (context::truthy(arg(a, 0))) {
            set_end(c, self, start_of(c, self));
        } else {
            set_start(c, self, end_of(c, self));
        }
        return value::undefined();
    });
    const auto select = [this, from_value, type_of, length_of, parent_of, index_of, set_start,
                         set_end](context & c, script::object_object * self, value node_value,
                                  bool contents) {
        const spot node = from_value(c, node_value);
        if (node.none()) {
            c.throw_error("TypeError", "Range: the argument is not a Node");
            return;
        }
        if (contents) {
            if (type_of(node) == type_doctype) {
                throw_dom_exception(c, "InvalidNodeTypeError", "a Range cannot select a doctype");
                return;
            }
            set_start(c, self, {node, 0});
            set_end(c, self, {node, length_of(node)});
            return;
        }
        const spot parent = parent_of(node);
        if (parent.none()) {
            throw_dom_exception(c, "InvalidNodeTypeError", "the node has no parent");
            return;
        }
        const double index = index_of(node);
        set_start(c, self, {parent, index});
        set_end(c, self, {parent, index + 1});
    };
    method("selectNode", [select](context & c, std::span<value> a) {
        if (script::object_object * self = self_object(c)) { select(c, self, arg(a, 0), false); }
        return value::undefined();
    });
    method("selectNodeContents", [select](context & c, std::span<value> a) {
        if (script::object_object * self = self_object(c)) { select(c, self, arg(a, 0), true); }
        return value::undefined();
    });
    define_getter(
        cx, *proto, "commonAncestorContainer",
        [start_of, end_of, inclusive_ancestor, parent_of, to_value](context & c, std::span<value>) {
            script::object_object * self = self_object(c);
            if (self == nullptr) { return value::undefined(); }
            spot at = start_of(c, self).node;
            const spot end = end_of(c, self).node;
            while (!at.none() && !inclusive_ancestor(at, end)) { at = parent_of(at); }
            return at.none() ? value::null() : to_value(c, at);
        });
    method("cloneRange", [proto_value, store_points](context & c, std::span<value>) {
        script::object_object * self = self_object(c);
        if (self == nullptr) { return value::undefined(); }
        auto * made = c.allocate<script::object_object>();
        made->prototype = proto_value;
        store_points(*made, slot(self, start_node_slot),
                     context::to_number(slot(self, start_offset_slot)), slot(self, end_node_slot),
                     context::to_number(slot(self, end_offset_slot)));
        return value::object(made);
    });
    method("detach", [](context &, std::span<value>) { return value::undefined(); });

    // --- THE FOUR COMPARISONS, DOM 5.5 -----------------------------------------
    //
    // `isPointInRange` and `comparePoint` share their checks: a doctype is an
    // InvalidNodeTypeError, an offset past the node's length an IndexSizeError,
    // and another tree is `false` for the one and a WrongDocumentError for the
    // other. `intersectsNode` asks about (parent, index) and (parent, index + 1);
    // `compareBoundaryPoints` about this range's boundary against the other's.
    const auto checked_point = [this, from_value, root_of, type_of, length_of, start_of](
                                   context & c, std::span<value> a, script::object_object * self,
                                   bool other_tree_throws, point & out) {
        const spot node = from_value(c, arg(a, 0));
        if (node.none()) {
            c.throw_error("TypeError", "Range: the argument is not a Node");
            return false;
        }
        if (!(root_of(node) == root_of(start_of(c, self).node))) {
            if (other_tree_throws) {
                throw_dom_exception(c, "WrongDocumentError", "the node is in another tree");
            }
            return false;
        }
        if (type_of(node) == type_doctype) {
            throw_dom_exception(c, "InvalidNodeTypeError", "a doctype has no boundary points");
            return false;
        }
        const double offset = static_cast<double>(context::to_uint32(arg(a, 1)));
        if (offset > length_of(node)) {
            throw_dom_exception(c, "IndexSizeError", "the offset is past the end of the node");
            return false;
        }
        out = point{node, offset};
        return true;
    };
    method("isPointInRange",
           [start_of, end_of, checked_point, before, after](context & c, std::span<value> a) {
               script::object_object * self = self_object(c);
               if (self == nullptr) { return value::undefined(); }
               point p;
               if (!checked_point(c, a, self, false, p)) { return value::boolean(false); }
               return value::boolean(!before(p, start_of(c, self)) && !after(p, end_of(c, self)));
           });
    method("comparePoint",
           [start_of, end_of, checked_point, before, after](context & c, std::span<value> a) {
               script::object_object * self = self_object(c);
               if (self == nullptr) { return value::undefined(); }
               point p;
               if (!checked_point(c, a, self, true, p)) { return value::undefined(); }
               if (before(p, start_of(c, self))) { return value::number(-1); }
               if (after(p, end_of(c, self))) { return value::number(1); }
               return value::number(0);
           });
    method("intersectsNode", [from_value, root_of, parent_of, index_of, start_of, end_of, before,
                              after](context & c, std::span<value> a) {
        script::object_object * self = self_object(c);
        if (self == nullptr) { return value::undefined(); }
        const spot node = from_value(c, arg(a, 0));
        if (node.none()) {
            c.throw_error("TypeError", "Range: the argument is not a Node");
            return value::undefined();
        }
        const point s = start_of(c, self);
        if (!(root_of(node) == root_of(s.node))) { return value::boolean(false); }
        const spot up = parent_of(node);
        if (up.none()) { return value::boolean(true); }
        const double offset = index_of(node);
        return value::boolean(before({up, offset}, end_of(c, self)) && after({up, offset + 1}, s));
    });
    method("compareBoundaryPoints", [this, start_of, end_of, root_of,
                                     position](context & c, std::span<value> a) {
        script::object_object * self = self_object(c);
        if (self == nullptr) { return value::undefined(); }
        const double how = context::to_number(arg(a, 0));
        const value other = arg(a, 1);
        if (!(how == 0 || how == 1 || how == 2 || how == 3)) {
            throw_dom_exception(c, "NotSupportedError",
                                "compareBoundaryPoints: `how` is not one of the four");
            return value::undefined();
        }
        if (!other.is_object() ||
            static_cast<script::object_object *>(other.as_heap())->find(start_node_slot) ==
                nullptr) {
            c.throw_error("TypeError", "compareBoundaryPoints: the argument is not a Range");
            return value::undefined();
        }
        auto * source = static_cast<script::object_object *>(other.as_heap());
        const point this_point = how == 0 || how == 3 ? start_of(c, self) : end_of(c, self);
        const point other_point = how == 0 || how == 1 ? start_of(c, source) : end_of(c, source);
        if (!(root_of(this_point.node) == root_of(other_point.node))) {
            throw_dom_exception(c, "WrongDocumentError", "the ranges are in different trees");
            return value::undefined();
        }
        return value::number(position(position, this_point, other_point));
    });

    // DOM 5.5 "contained": (node, 0) after the start and (node, length)
    // before the end, in the range's tree.
    const auto contained = [root_of, after, before, length_of](spot node, point s, point e) {
        return root_of(node) == root_of(s.node) && after({node, 0}, s) &&
               before({node, length_of(node)}, e);
    };
    // ...and "partially contained": an inclusive ancestor of one boundary's
    // node but not the other's.
    const auto partially = [inclusive_ancestor](spot node, point s, point e) {
        return inclusive_ancestor(node, s.node) != inclusive_ancestor(node, e.node);
    };
    // The node and offset an extract or delete collapses to, DOM 5.5.
    const auto collapse_point = [inclusive_ancestor, parent_of, index_of](point s, point e) {
        if (inclusive_ancestor(s.node, e.node)) { return s; }
        spot reference = s.node;
        while (!parent_of(reference).none() && !inclusive_ancestor(parent_of(reference), e.node)) {
            reference = parent_of(reference);
        }
        return point{parent_of(reference), index_of(reference) + 1};
    };

    // DOM 5.5 "extract" and "clone the contents", one body: `take` moves the
    // contained nodes and cuts the partial text; `!take` copies both.
    const auto extract =
        [this, is_character_data, contained, partially, parent_of, inclusive_ancestor, children_of,
         type_of, length_of, new_fragment, clone_with_data, clone_of, substring, replace_data,
         append, to_value](auto && again, context & c, point s, point e, bool take) -> value {
        const value fragment = new_fragment(c, s.node);
        if (!fragment.is_object_like()) { return value::undefined(); }
        if (s.node == e.node && s.offset == e.offset) { return fragment; }
        if (s.node == e.node && is_character_data(s.node)) {
            append(c, fragment,
                   clone_with_data(c, s.node, substring(c, s.node, s.offset, e.offset - s.offset)));
            if (take) { replace_data(c, s.node, s.offset, e.offset - s.offset, ""); }
            return fragment;
        }
        spot common = s.node;
        while (!inclusive_ancestor(common, e.node)) { common = parent_of(common); }
        spot first_partial;
        spot last_partial;
        std::vector<spot> contained_children;
        const bool start_above_end = inclusive_ancestor(s.node, e.node);
        const bool end_above_start = inclusive_ancestor(e.node, s.node);
        for (const node_id id : children_of(common)) {
            const spot child{common.owner, id};
            if (!start_above_end && first_partial.none() && partially(child, s, e)) {
                first_partial = child;
            }
            if (!end_above_start && partially(child, s, e)) { last_partial = child; }
            if (contained(child, s, e)) {
                if (type_of(child) == type_doctype) {
                    throw_dom_exception(c, "HierarchyRequestError",
                                        "a doctype cannot be extracted");
                    return value::undefined();
                }
                contained_children.push_back(child);
            }
        }
        if (!first_partial.none() && is_character_data(first_partial)) {
            const double count = length_of(s.node) - s.offset;
            append(c, fragment, clone_with_data(c, s.node, substring(c, s.node, s.offset, count)));
            if (take) { replace_data(c, s.node, s.offset, count, ""); }
        } else if (!first_partial.none()) {
            const value clone = clone_of(c, first_partial, false);
            append(c, fragment, clone);
            const value sub =
                again(again, c, s, point{first_partial, length_of(first_partial)}, take);
            if (sub.is_object_like()) { append(c, clone, sub); }
        }
        for (const spot & child : contained_children) {
            append(c, fragment, take ? to_value(c, child) : clone_of(c, child, true));
        }
        if (!last_partial.none() && is_character_data(last_partial)) {
            append(c, fragment, clone_with_data(c, e.node, substring(c, e.node, 0, e.offset)));
            if (take) { replace_data(c, e.node, 0, e.offset, ""); }
        } else if (!last_partial.none()) {
            const value clone = clone_of(c, last_partial, false);
            append(c, fragment, clone);
            const value sub = again(again, c, point{last_partial, 0}, e, take);
            if (sub.is_object_like()) { append(c, clone, sub); }
        }
        return fragment;
    };
    const auto extract_from = [start_of, end_of, set_start, set_end, collapse_point, extract](
                                  context & c, script::object_object * self, bool take) -> value {
        const point s = start_of(c, self);
        const point e = end_of(c, self);
        // "Set range's start and end to (new node, new offset)" - extract
        // collapses the range, clone leaves it. The point is computed FIRST:
        // the moves below change the indexes it is made of.
        const point fresh = take ? collapse_point(s, e) : s;
        const value fragment = extract(extract, c, s, e, take);
        if (take && fragment.is_object_like()) {
            set_start(c, self, fresh);
            set_end(c, self, fresh);
        }
        return fragment;
    };
    method("extractContents", [extract_from](context & c, std::span<value>) {
        script::object_object * self = self_object(c);
        return self == nullptr ? value::undefined() : extract_from(c, self, true);
    });
    method("cloneContents", [extract_from](context & c, std::span<value>) {
        script::object_object * self = self_object(c);
        return self == nullptr ? value::undefined() : extract_from(c, self, false);
    });
    // DOM 5.5 "delete the contents": the same walk with nothing kept. The
    // nodes to remove are the contained ones without a contained ancestor -
    // found by descending only through the partially contained.
    method("deleteContents", [start_of, end_of, set_start, set_end, is_character_data, replace_data,
                              length_of, contained, partially, inclusive_ancestor, parent_of,
                              children_of, collapse_point, remove](context & c, std::span<value>) {
        script::object_object * self = self_object(c);
        if (self == nullptr) { return value::undefined(); }
        const point s = start_of(c, self);
        const point e = end_of(c, self);
        if (s.node == e.node && s.offset == e.offset) { return value::undefined(); }
        if (s.node == e.node && is_character_data(s.node)) {
            replace_data(c, s.node, s.offset, e.offset - s.offset, "");
            return value::undefined();
        }
        std::vector<spot> to_remove;
        const auto gather = [&](auto && again, spot at) -> void {
            for (const node_id id : children_of(at)) {
                const spot child{at.owner, id};
                if (contained(child, s, e)) {
                    to_remove.push_back(child);
                } else if (partially(child, s, e)) {
                    again(again, child);
                }
            }
        };
        spot common = s.node;
        while (!inclusive_ancestor(common, e.node)) { common = parent_of(common); }
        gather(gather, common);
        const point fresh = collapse_point(s, e);
        if (is_character_data(s.node)) {
            replace_data(c, s.node, s.offset, length_of(s.node) - s.offset, "");
        }
        for (const spot & node : to_remove) { remove(c, node); }
        if (is_character_data(e.node)) { replace_data(c, e.node, 0, e.offset, ""); }
        set_start(c, self, fresh);
        set_end(c, self, fresh);
        return value::undefined();
    });

    // DOM 5.5 "insert": split the start text node when the start is inside
    // one, put `node` ahead of what follows the start, and stretch a
    // collapsed range around it. THE EDITS MOVE THIS RANGE'S OWN POINTS the
    // way the live-range steps of split, remove and insert would - a boundary
    // past the split goes into the new node, one after a removed or inserted
    // child in its parent shifts by one - so the range ends where a live one
    // does. Other ranges do not follow; see the note at the top.
    const auto insert = [this, from_value, type_of, is_text, parent_of, children_of, index_of,
                         length_of, to_value, call_on, remove, inclusive_ancestor, start_of, end_of,
                         set_start,
                         set_end](context & c, script::object_object * self, value node_value) {
        const spot node = from_value(c, node_value);
        if (node.none()) {
            c.throw_error("TypeError", "insertNode: the argument is not a Node");
            return;
        }
        point s = start_of(c, self);
        point e = end_of(c, self);
        const unsigned start_type = type_of(s.node);
        if (start_type == type_pi || start_type == type_comment ||
            (is_text(s.node) && parent_of(s.node).none()) || s.node == node || node.is_attr() ||
            s.node.is_attr()) {
            throw_dom_exception(c, "HierarchyRequestError", "insertNode: nowhere to insert");
            return;
        }
        const bool collapsed = s.node == e.node && s.offset == e.offset;
        spot reference;
        if (is_text(s.node)) {
            reference = s.node;
        } else {
            const std::vector<node_id> kids = children_of(s.node);
            if (static_cast<std::size_t>(s.offset) < kids.size()) {
                reference = spot{s.node.owner, kids[static_cast<std::size_t>(s.offset)]};
            }
        }
        const spot parent = reference.none() ? s.node : parent_of(reference);
        // The live-range steps of "insert" and "remove", DOM 4.2.3, applied
        // to this range's two points.
        const auto inserted = [&](spot into, double index, double count) {
            for (point * p : {&s, &e}) {
                if (p->node == into && p->offset > index) { p->offset += count; }
            }
        };
        const auto removed = [&](spot from, double index, spot gone) {
            for (point * p : {&s, &e}) {
                if (inclusive_ancestor(gone, p->node)) {
                    *p = point{from, index};
                } else if (p->node == from && p->offset > index) {
                    p->offset -= 1;
                }
            }
        };
        if (is_text(s.node)) {
            const spot text = s.node;
            const double at = s.offset;
            const value made = call_on(c, text, "splitText", {value::number(at)});
            if (c.throw_pending()) { return; }
            reference = from_value(c, made);
            if (reference.none()) { return; }
            for (point * p : {&s, &e}) {
                if (p->node == text && p->offset > at) { *p = point{reference, p->offset - at}; }
            }
            inserted(parent, index_of(text), 1);
        }
        if (node == reference) {
            const value next = c.lookup_property(to_value(c, reference), "nextSibling");
            reference = from_value(c, next);
        }
        if (const spot old_parent = parent_of(node); !old_parent.none()) {
            const double old_index = index_of(node);
            remove(c, node);
            if (c.throw_pending()) { return; }
            removed(old_parent, old_index, node);
        }
        const double at = reference.none() ? length_of(parent) : index_of(reference);
        const double count = type_of(node) == type_fragment ? length_of(node) : 1;
        (void)call_on(c, parent, "insertBefore",
                      {node_value, reference.none() ? value::null() : to_value(c, reference)});
        if (c.throw_pending()) { return; }
        inserted(parent, at, count);
        if (collapsed) { e = point{parent, at + count}; }
        set_start(c, self, s);
        set_end(c, self, e);
    };
    method("insertNode", [insert](context & c, std::span<value> a) {
        if (script::object_object * self = self_object(c)) { insert(c, self, arg(a, 0)); }
        return value::undefined();
    });
    method("surroundContents", [this, from_value, start_of, end_of, is_text, partially, parent_of,
                                type_of, extract_from, children_of, remove, insert, append, select,
                                to_value](context & c, std::span<value> a) {
        script::object_object * self = self_object(c);
        if (self == nullptr) { return value::undefined(); }
        const spot parent = from_value(c, arg(a, 0));
        if (parent.none()) {
            c.throw_error("TypeError", "surroundContents: the argument is not a Node");
            return value::undefined();
        }
        const point s = start_of(c, self);
        const point e = end_of(c, self);
        // A partially contained non-Text node cannot be surrounded. Only an
        // ancestor of a boundary node can be partially contained.
        for (spot at : {s.node, e.node}) {
            for (; !at.none(); at = parent_of(at)) {
                if (!is_text(at) && partially(at, s, e)) {
                    throw_dom_exception(c, "InvalidStateError",
                                        "surroundContents: the range splits a non-Text node");
                    return value::undefined();
                }
            }
        }
        const unsigned type = type_of(parent);
        if (type == type_document || type == type_doctype || type == type_fragment ||
            parent.is_attr()) {
            throw_dom_exception(c, "InvalidNodeTypeError",
                                "surroundContents: the new parent cannot hold the contents");
            return value::undefined();
        }
        const value fragment = extract_from(c, self, true);
        if (!fragment.is_object_like() || c.throw_pending()) { return value::undefined(); }
        for (const node_id id : children_of(parent)) { remove(c, spot{parent.owner, id}); }
        insert(c, self, arg(a, 0));
        if (c.throw_pending()) { return value::undefined(); }
        append(c, to_value(c, parent), fragment);
        select(c, self, arg(a, 0), false);
        return value::undefined();
    });
    // DOM 5.5 "stringification": the text between the two boundary points -
    // the Text nodes only, a comment contributes nothing.
    method("toString", [start_of, end_of, is_text, contained, substring, root_of, children_of,
                        text_of, length_of](context & c, std::span<value>) {
        script::object_object * self = self_object(c);
        if (self == nullptr) { return value::undefined(); }
        const point s = start_of(c, self);
        const point e = end_of(c, self);
        if (s.node == e.node && is_text(s.node)) {
            return c.string(substring(c, s.node, s.offset, e.offset - s.offset));
        }
        std::string out;
        if (is_text(s.node)) {
            out += substring(c, s.node, s.offset, length_of(s.node) - s.offset);
        }
        const auto walk = [&](auto && again, spot at) -> void {
            if (is_text(at) && contained(at, s, e)) { out += text_of(at); }
            for (const node_id id : children_of(at)) { again(again, spot{at.owner, id}); }
        };
        if (const spot top = root_of(s.node); !top.is_attr()) { walk(walk, top); }
        if (is_text(e.node)) { out += substring(c, e.node, 0, e.offset); }
        return c.string(out);
    });
    // DOM Parsing 3.3, `createContextualFragment(markup)`: the markup parsed
    // as the children of an element like the range's start node's element -
    // the node itself, its parent for text and comments, `body` for a
    // document or fragment - handed back as a DocumentFragment.
    // ponytail: the parse is innerHTML's, in body context; a `<tr>` in a
    // table context loses its row the way innerHTML on a <div> would.
    method("createContextualFragment",
           [start_of, type_of, parent_of, call_on](context & c, std::span<value> a) {
               script::object_object * self = self_object(c);
               if (self == nullptr) { return value::undefined(); }
               const std::string markup = arg_string(c, a, 0);
               spot node = start_of(c, self).node;
               while (!node.none() && !node.is_attr() && type_of(node) != type_element &&
                      type_of(node) != type_document && type_of(node) != type_fragment) {
                   node = parent_of(node);
               }
               if (node.none() || node.is_attr()) { return value::null(); }
               const spot document_node{node.owner, node.owner->doc_->document_node()};
               std::string tag = "body";
               if (type_of(node) == type_element) {
                   const auto txn = node.owner->doc_->read();
                   tag = std::string{txn.local_name(node.id)};
                   if (txn.element_ns(node.id) != node_ns::html || tag == "html") { tag = "body"; }
               }
               const value scratch = call_on(c, document_node, "createElement", {c.string(tag)});
               if (!scratch.is_object_like()) { return value::null(); }
               c.store_property(scratch, "innerHTML", c.string(markup));
               const value fragment = call_on(c, document_node, "createDocumentFragment", {});
               while (true) {
                   const value first = c.lookup_property(scratch, "firstChild");
                   if (!first.is_object_like()) { break; }
                   const value append = c.lookup_property(fragment, "appendChild");
                   if (!append.is_callable()) { break; }
                   (void)c.call(append, std::span<const value>{&first, 1}, fragment);
                   if (c.throw_pending()) { break; }
               }
               return fragment;
           });
    proto->define("@@toStringTag", cx.string("Range"), script::attr_configurable);

    // `new Range()`: collapsed at (document, 0) of the realm's document.
    auto * ctor = cx.allocate<script::native_object>(
        "Range", [this, proto_value, store_points](context & c, std::span<value>) -> value {
            const value self = c.current_this();
            if (!self.is_object()) {
                c.throw_error("TypeError", "Failed to construct 'Range': please use the 'new' "
                                           "operator.");
                return value::undefined();
            }
            auto * made = static_cast<script::object_object *>(self.as_heap());
            if (!made->prototype.is_object()) { made->prototype = proto_value; }
            store_points(*made, document_, 0, document_, 0);
            return self;
        });
    ctor->define("prototype", proto_value, script::attr_none);
    proto->define("constructor", value::object(ctor), script::attr_builtin);
    for (const auto & [name, bits] : {std::pair{"START_TO_START", 0}, std::pair{"START_TO_END", 1},
                                      std::pair{"END_TO_END", 2}, std::pair{"END_TO_START", 3}}) {
        ctor->define(name, value::number(bits), script::attr_enumerable);
        proto->define(name, value::number(bits), script::attr_enumerable);
    }
    cx.define_global("Range", value::object(ctor));

    // --- StaticRange, DOM 5.4 --------------------------------------------------
    //
    // `new StaticRange(init)`: the four members are required, a doctype or an
    // Attr is an InvalidNodeTypeError, and nothing else is checked - an offset
    // past the end and an inverted pair are both allowed. It is a snapshot,
    // and stays one.
    auto * static_proto = cx.allocate<script::object_object>();
    const value static_proto_value = value::object(static_proto);
    install_abstract(*static_proto);
    static_proto->define("@@toStringTag", cx.string("StaticRange"), script::attr_configurable);
    auto * static_ctor = cx.allocate<script::native_object>(
        "StaticRange",
        [this, static_proto_value, from_value, type_of, store_points](context & c,
                                                                      std::span<value> a) -> value {
            const value self = c.current_this();
            if (!self.is_object()) {
                c.throw_error("TypeError", "Failed to construct 'StaticRange': please use the "
                                           "'new' operator.");
                return value::undefined();
            }
            const value init = arg(a, 0);
            if (!init.is_object_like()) {
                c.throw_error("TypeError", "Failed to construct 'StaticRange': the init "
                                           "dictionary is required");
                return value::undefined();
            }
            value nodes[2];
            double offsets[2] = {0, 0};
            for (std::size_t i = 0; i < 2; ++i) {
                const value node = dict_member(c, init, i == 0 ? "startContainer" : "endContainer");
                const value offset = dict_member(c, init, i == 0 ? "startOffset" : "endOffset");
                if (node.is_undefined() || offset.is_undefined()) {
                    c.throw_error("TypeError", "Failed to construct 'StaticRange': the four "
                                               "members are required");
                    return value::undefined();
                }
                const spot at = from_value(c, node);
                if (at.none()) {
                    c.throw_error("TypeError", "Failed to construct 'StaticRange': the "
                                               "container is not a Node");
                    return value::undefined();
                }
                if (at.is_attr() || type_of(at) == type_doctype) {
                    throw_dom_exception(c, "InvalidNodeTypeError",
                                        "a StaticRange cannot start or end at a doctype or Attr");
                    return value::undefined();
                }
                nodes[i] = node;
                offsets[i] = static_cast<double>(context::to_uint32(offset));
            }
            auto * made = static_cast<script::object_object *>(self.as_heap());
            if (!made->prototype.is_object()) { made->prototype = static_proto_value; }
            store_points(*made, nodes[0], offsets[0], nodes[1], offsets[1]);
            return self;
        });
    static_ctor->define("prototype", static_proto_value, script::attr_none);
    static_proto->define("constructor", value::object(static_ctor), script::attr_builtin);
    cx.define_global("StaticRange", value::object(static_ctor));
    // UNDER AbstractRange, when the interface table has already been linked:
    // install_dom_interfaces adopts `Range.prototype` off the global and
    // chains it, but only if it runs after this - and the first wrap() of
    // the load can have run it before install_range was reached.
    if (const value abstract = interface_prototype("AbstractRange"); abstract.is_object()) {
        proto->prototype = abstract;
        static_proto->prototype = abstract;
    }
}

// `document.createRange()`: a range collapsed at (this document, 0).
value dom_bindings::create_range(context & cx) {
    const value ctor = cx.global("Range");
    auto * made = cx.allocate<script::object_object>();
    if (ctor.is_callable()) {
        const value proto = cx.lookup_property(ctor, "prototype");
        if (proto.is_object()) { made->prototype = proto; }
    }
    made->define(std::string{start_node_slot}, document_, script::attr_none);
    made->define(std::string{start_offset_slot}, value::number(0), script::attr_none);
    made->define(std::string{end_node_slot}, document_, script::attr_none);
    made->define(std::string{end_offset_slot}, value::number(0), script::attr_none);
    return value::object(made);
}

} // namespace ctbrowser::shell
