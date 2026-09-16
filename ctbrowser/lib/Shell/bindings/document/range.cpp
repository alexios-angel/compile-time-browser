// dom_bindings - DOM 5, Range: `document.createRange()` and `new Range()`.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// ============================================================================
// RANGE
// ============================================================================
//
// A range is four own properties on an ordinary object - startContainer,
// startOffset, endContainer, endOffset - and every algorithm below is written
// over the NODES' OWN SCRIPT SURFACE: `parentNode`, `childNodes`, `nodeType`,
// `splitText`, `replaceData`, `removeChild`, `insertBefore`. That is what lets
// one implementation serve a wrapper and the handle-less document object
// alike, and any document in the realm. CharacterData offsets are UTF-16 code
// units because `length`, `substringData` and `replaceData` count them.
//
// ponytail: NOT A LIVE RANGE. DOM keeps every range's boundary points up to
// date as the tree changes; this one does not, and `deleteContents` is
// `extractContents` with the fragment dropped - the tree and the mutation
// records an observer sees are the same. moveBefore/live-range-updates.html
// is what a live range would answer.

namespace {

constexpr double text_node = 3;
constexpr double cdata_node = 4;
constexpr double pi_node = 7;
constexpr double comment_node = 8;
constexpr double document_node = 9;
constexpr double doctype_node = 10;
constexpr double fragment_node = 11;

[[nodiscard]] bool same(value a, value b) {
    return a.is_object_like() && b.is_object_like() && a.bits() == b.bits();
}

struct point {
    value node;
    double offset = 0;
};

// The node's script surface, one call each.
struct nodes {
    context & c;
    [[nodiscard]] value get(value node, const char * name) const {
        return c.lookup_property(node, name);
    }
    [[nodiscard]] value call(value node, const char * name, std::vector<value> args) const {
        const value fn = get(node, name);
        return fn.is_callable() ? c.call(fn, args, node) : value::undefined();
    }
    [[nodiscard]] double type(value node) const {
        return context::to_number(get(node, "nodeType"));
    }
    [[nodiscard]] bool is_node(value node) const {
        return node.is_object_like() && get(node, "nodeType").is_number();
    }
    [[nodiscard]] bool character_data(value node) const {
        const double t = type(node);
        return t == text_node || t == cdata_node || t == pi_node || t == comment_node;
    }
    [[nodiscard]] bool text_like(value node) const {
        const double t = type(node);
        return t == text_node || t == cdata_node;
    }
    [[nodiscard]] value parent(value node) const { return get(node, "parentNode"); }
    [[nodiscard]] std::vector<value> children(value node) const {
        std::vector<value> out;
        const value list = get(node, "childNodes");
        if (!list.is_object_like()) { return out; }
        const double n = context::to_number(get(list, "length"));
        for (double i = 0; i < n; ++i) {
            out.push_back(c.lookup_property(list, std::to_string(static_cast<long long>(i))));
        }
        return out;
    }
    // "length" of a node, DOM 4.2: code units for character data, children
    // otherwise, 0 for a doctype.
    [[nodiscard]] double length(value node) const {
        if (character_data(node)) { return context::to_number(get(node, "length")); }
        if (type(node) == doctype_node) { return 0; }
        return static_cast<double>(children(node).size());
    }
    [[nodiscard]] double index(value node) const {
        const value up = parent(node);
        if (!up.is_object_like()) { return 0; }
        const std::vector<value> kids = children(up);
        for (std::size_t i = 0; i < kids.size(); ++i) {
            if (same(kids[i], node)) { return static_cast<double>(i); }
        }
        return 0;
    }
    [[nodiscard]] value root(value node) const {
        value at = node;
        for (value up = parent(at); up.is_object_like(); up = parent(at)) { at = up; }
        return at;
    }
    [[nodiscard]] bool inclusive_ancestor(value ancestor, value node) const {
        for (value at = node; at.is_object_like(); at = parent(at)) {
            if (same(at, ancestor)) { return true; }
        }
        return false;
    }
    // DOM 5.2 "position of a boundary point": -1 before, 0 equal, 1 after.
    [[nodiscard]] int position(point a, point b) const {
        if (same(a.node, b.node)) {
            return a.offset < b.offset ? -1 : (a.offset > b.offset ? 1 : 0);
        }
        const double bits = context::to_number(call(b.node, "compareDocumentPosition", {a.node}));
        constexpr double following = 0x04;
        if ((static_cast<unsigned>(bits) & static_cast<unsigned>(following)) != 0) {
            // a follows b: the answer is the reverse of b's position against a.
            return -position(b, a);
        }
        if (inclusive_ancestor(a.node, b.node)) {
            value child = b.node;
            while (!same(parent(child), a.node)) { child = parent(child); }
            if (index(child) < a.offset) { return 1; }
        }
        return -1;
    }
    [[nodiscard]] std::string data(value node, double offset, double count) const {
        return c.to_string(
            call(node, "substringData", {value::number(offset), value::number(count)}));
    }
    void replace_data(value node, double offset, double count, const std::string & with) const {
        (void)call(node, "replaceData",
                   {value::number(offset), value::number(count), c.string(with)});
    }
    [[nodiscard]] value clone(value node) const {
        return call(node, "cloneNode", {value::boolean(false)});
    }
    void append(value parent, value child) const { (void)call(parent, "appendChild", {child}); }
    void remove(value node) const {
        if (const value up = parent(node); up.is_object_like()) {
            (void)call(up, "removeChild", {node});
        }
    }
    [[nodiscard]] value owner_document(value node) const {
        return type(node) == document_node ? node : get(node, "ownerDocument");
    }
};

} // namespace

void dom_bindings::install_range(context & cx) {
    auto * proto = cx.allocate<script::object_object>();
    const value proto_value = value::object(proto);
    const auto self_of = [](context & c) -> script::object_object * {
        const value self = c.current_this();
        if (!self.is_object()) {
            c.throw_error("TypeError", "Illegal invocation");
            return nullptr;
        }
        return static_cast<script::object_object *>(self.as_heap());
    };
    const auto start_of = [](context & c, script::object_object * self) {
        return point{c.lookup_property(value::object(self), "startContainer"),
                     context::to_number(c.lookup_property(value::object(self), "startOffset"))};
    };
    const auto end_of = [](context & c, script::object_object * self) {
        return point{c.lookup_property(value::object(self), "endContainer"),
                     context::to_number(c.lookup_property(value::object(self), "endOffset"))};
    };
    const auto set_start = [](script::object_object * self, point p) {
        self->set("startContainer", p.node);
        self->set("startOffset", value::number(p.offset));
    };
    const auto set_end = [](script::object_object * self, point p) {
        self->set("endContainer", p.node);
        self->set("endOffset", value::number(p.offset));
    };
    // DOM 5.5 "set the start or end": a doctype is InvalidNodeTypeError, an
    // offset past the node's length IndexSizeError, and the other boundary
    // follows when it would be on the wrong side or in another tree.
    const auto set_boundary = [this, self_of, start_of, end_of, set_start, set_end](
                                  context & c, value node_value, value offset_value, bool start) {
        script::object_object * self = self_of(c);
        if (self == nullptr) { return; }
        const nodes n{c};
        if (!n.is_node(node_value)) {
            c.throw_error("TypeError", "Range: the argument is not a Node");
            return;
        }
        if (n.type(node_value) == doctype_node) {
            throw_dom_exception(c, "InvalidNodeTypeError",
                                "a Range cannot start or end at a doctype");
            return;
        }
        const double offset = static_cast<double>(context::to_uint32(offset_value));
        if (offset > n.length(node_value)) {
            throw_dom_exception(c, "IndexSizeError", "the offset is past the end of the node");
            return;
        }
        const point bp{node_value, offset};
        if (start) {
            const point end = end_of(c, self);
            if (!same(n.root(node_value), n.root(end.node)) || n.position(bp, end) > 0) {
                set_end(self, bp);
            }
            set_start(self, bp);
        } else {
            const point begin = start_of(c, self);
            if (!same(n.root(node_value), n.root(begin.node)) || n.position(bp, begin) < 0) {
                set_start(self, bp);
            }
            set_end(self, bp);
        }
    };
    set_method(
        cx, *proto, "setStart",
        [set_boundary](context & c, std::span<value> a) {
            set_boundary(c, arg(a, 0), arg(a, 1), true);
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *proto, "setEnd",
        [set_boundary](context & c, std::span<value> a) {
            set_boundary(c, arg(a, 0), arg(a, 1), false);
            return value::undefined();
        },
        script::attr_builtin);
    // The four "before/after a node" setters: the node's parent and index.
    const auto beside = [this, set_boundary](context & c, value node, bool after, bool start) {
        const nodes n{c};
        if (!n.is_node(node)) {
            c.throw_error("TypeError", "Range: the argument is not a Node");
            return;
        }
        const value parent = n.parent(node);
        if (!parent.is_object_like()) {
            throw_dom_exception(c, "InvalidNodeTypeError", "the node has no parent");
            return;
        }
        set_boundary(c, parent, value::number(n.index(node) + (after ? 1 : 0)), start);
    };
    set_method(
        cx, *proto, "setStartBefore",
        [beside](context & c, std::span<value> a) {
            beside(c, arg(a, 0), false, true);
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *proto, "setStartAfter",
        [beside](context & c, std::span<value> a) {
            beside(c, arg(a, 0), true, true);
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *proto, "setEndBefore",
        [beside](context & c, std::span<value> a) {
            beside(c, arg(a, 0), false, false);
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *proto, "setEndAfter",
        [beside](context & c, std::span<value> a) {
            beside(c, arg(a, 0), true, false);
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *proto, "collapse",
        [self_of, start_of, end_of, set_start, set_end](context & c, std::span<value> a) {
            script::object_object * self = self_of(c);
            if (self == nullptr) { return value::undefined(); }
            if (context::truthy(arg(a, 0))) {
                set_end(self, start_of(c, self));
            } else {
                set_start(self, end_of(c, self));
            }
            return value::undefined();
        },
        script::attr_builtin);
    const auto select = [this, self_of, set_start, set_end](context & c, value node,
                                                            bool contents) {
        script::object_object * self = self_of(c);
        if (self == nullptr) { return; }
        const nodes n{c};
        if (!n.is_node(node)) {
            c.throw_error("TypeError", "Range: the argument is not a Node");
            return;
        }
        if (contents) {
            if (n.type(node) == doctype_node) {
                throw_dom_exception(c, "InvalidNodeTypeError", "a Range cannot select a doctype");
                return;
            }
            set_start(self, {node, 0});
            set_end(self, {node, n.length(node)});
            return;
        }
        const value parent = n.parent(node);
        if (!parent.is_object_like()) {
            throw_dom_exception(c, "InvalidNodeTypeError", "the node has no parent");
            return;
        }
        const double index = n.index(node);
        set_start(self, {parent, index});
        set_end(self, {parent, index + 1});
    };
    set_method(
        cx, *proto, "selectNode",
        [select](context & c, std::span<value> a) {
            select(c, arg(a, 0), false);
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *proto, "selectNodeContents",
        [select](context & c, std::span<value> a) {
            select(c, arg(a, 0), true);
            return value::undefined();
        },
        script::attr_builtin);
    define_getter(cx, *proto, "collapsed",
                  [self_of, start_of, end_of](context & c, std::span<value>) {
                      script::object_object * self = self_of(c);
                      if (self == nullptr) { return value::undefined(); }
                      const point s = start_of(c, self);
                      const point e = end_of(c, self);
                      return value::boolean(same(s.node, e.node) && s.offset == e.offset);
                  });
    define_getter(cx, *proto, "commonAncestorContainer",
                  [self_of, start_of, end_of](context & c, std::span<value>) {
                      script::object_object * self = self_of(c);
                      if (self == nullptr) { return value::undefined(); }
                      const nodes n{c};
                      value at = start_of(c, self).node;
                      const value end = end_of(c, self).node;
                      while (at.is_object_like() && !n.inclusive_ancestor(at, end)) {
                          at = n.parent(at);
                      }
                      return at;
                  });
    set_method(
        cx, *proto, "cloneRange",
        [self_of, proto_value](context & c, std::span<value>) {
            script::object_object * self = self_of(c);
            if (self == nullptr) { return value::undefined(); }
            auto * made = c.allocate<script::object_object>();
            made->prototype = proto_value;
            for (const char * name :
                 {"startContainer", "startOffset", "endContainer", "endOffset"}) {
                made->set(name, c.lookup_property(value::object(self), name));
            }
            return value::object(made);
        },
        script::attr_builtin);
    set_method(
        cx, *proto, "detach", [](context &, std::span<value>) { return value::undefined(); },
        script::attr_builtin);

    // DOM 5.5 "contained": (node, 0) after the start and (node, length)
    // before the end, in the range's tree.
    const auto contained = [](const nodes & n, value node, point s, point e) {
        return same(n.root(node), n.root(s.node)) && n.position({node, 0}, s) > 0 &&
               n.position({node, n.length(node)}, e) < 0;
    };
    // ...and "partially contained": an inclusive ancestor of one boundary's
    // node but not the other's.
    const auto partially = [](const nodes & n, value node, point s, point e) {
        return n.inclusive_ancestor(node, s.node) != n.inclusive_ancestor(node, e.node);
    };

    // DOM 5.5 "extract" and "clone the contents", one body: `take` moves the
    // contained nodes and cuts the partial text; `!take` copies both.
    const auto extract = [this, contained, partially](auto && again, context & c, point s, point e,
                                                      bool take, point & fresh) -> value {
        const nodes n{c};
        fresh = s;
        const value fragment = n.call(n.owner_document(s.node), "createDocumentFragment", {});
        if (same(s.node, e.node) && s.offset == e.offset) { return fragment; }
        if (same(s.node, e.node) && n.character_data(s.node)) {
            const value clone = n.clone(s.node);
            c.store_property(clone, "data",
                             c.string(n.data(s.node, s.offset, e.offset - s.offset)));
            n.append(fragment, clone);
            if (take) { n.replace_data(s.node, s.offset, e.offset - s.offset, ""); }
            return fragment;
        }
        value common = s.node;
        while (!n.inclusive_ancestor(common, e.node)) { common = n.parent(common); }
        value first_partial = value::null();
        value last_partial = value::null();
        std::vector<value> contained_children;
        for (const value & child : n.children(common)) {
            if (first_partial.is_null() && !n.inclusive_ancestor(s.node, e.node) &&
                partially(n, child, s, e)) {
                first_partial = child;
            }
            if (!n.inclusive_ancestor(e.node, s.node) && partially(n, child, s, e)) {
                last_partial = child;
            }
            if (contained(n, child, s, e)) {
                if (n.type(child) == doctype_node) {
                    throw_dom_exception(c, "HierarchyRequestError",
                                        "a doctype cannot be extracted");
                    return value::undefined();
                }
                contained_children.push_back(child);
            }
        }
        if (!n.inclusive_ancestor(s.node, e.node)) {
            value reference = s.node;
            while (n.parent(reference).is_object_like() &&
                   !n.inclusive_ancestor(n.parent(reference), e.node)) {
                reference = n.parent(reference);
            }
            fresh = {n.parent(reference), n.index(reference) + 1};
        }
        if (first_partial.is_object_like() && n.character_data(first_partial)) {
            const value clone = n.clone(s.node);
            const double count = n.length(s.node) - s.offset;
            c.store_property(clone, "data", c.string(n.data(s.node, s.offset, count)));
            n.append(fragment, clone);
            if (take) { n.replace_data(s.node, s.offset, count, ""); }
        } else if (first_partial.is_object_like()) {
            const value clone = n.clone(first_partial);
            n.append(fragment, clone);
            point unused;
            const value sub =
                again(again, c, s, point{first_partial, n.length(first_partial)}, take, unused);
            if (sub.is_object_like()) { n.append(clone, sub); }
        }
        for (const value & child : contained_children) {
            n.append(fragment, take ? child : n.call(child, "cloneNode", {value::boolean(true)}));
        }
        if (last_partial.is_object_like() && n.character_data(last_partial)) {
            const value clone = n.clone(e.node);
            c.store_property(clone, "data", c.string(n.data(e.node, 0, e.offset)));
            n.append(fragment, clone);
            if (take) { n.replace_data(e.node, 0, e.offset, ""); }
        } else if (last_partial.is_object_like()) {
            const value clone = n.clone(last_partial);
            n.append(fragment, clone);
            point unused;
            const value sub = again(again, c, point{last_partial, 0}, e, take, unused);
            if (sub.is_object_like()) { n.append(clone, sub); }
        }
        return fragment;
    };
    const auto extract_from = [self_of, start_of, end_of, set_start, set_end,
                               extract](context & c, bool take) -> value {
        script::object_object * self = self_of(c);
        if (self == nullptr) { return value::undefined(); }
        point fresh;
        const value fragment = extract(extract, c, start_of(c, self), end_of(c, self), take, fresh);
        // "Set range's start and end to (new node, new offset)" - extract
        // collapses the range, clone leaves it.
        if (take && fragment.is_object_like()) {
            set_start(self, fresh);
            set_end(self, fresh);
        }
        return fragment;
    };
    set_method(
        cx, *proto, "extractContents",
        [extract_from](context & c, std::span<value>) { return extract_from(c, true); },
        script::attr_builtin);
    set_method(
        cx, *proto, "cloneContents",
        [extract_from](context & c, std::span<value>) { return extract_from(c, false); },
        script::attr_builtin);
    set_method(
        cx, *proto, "deleteContents",
        [extract_from](context & c, std::span<value>) {
            (void)extract_from(c, true);
            return value::undefined();
        },
        script::attr_builtin);

    // DOM 5.5 "insert": split the start text node when the start is inside
    // one, put `node` ahead of what follows the start, and stretch a
    // collapsed range around it.
    const auto insert = [this, self_of, start_of, end_of, set_end](context & c, value node) {
        script::object_object * self = self_of(c);
        if (self == nullptr) { return; }
        const nodes n{c};
        if (!n.is_node(node)) {
            c.throw_error("TypeError", "insertNode: the argument is not a Node");
            return;
        }
        const point s = start_of(c, self);
        const point e = end_of(c, self);
        const double start_type = n.type(s.node);
        if (start_type == pi_node || start_type == comment_node ||
            (n.text_like(s.node) && !n.parent(s.node).is_object_like()) || same(s.node, node)) {
            throw_dom_exception(c, "HierarchyRequestError", "insertNode: nowhere to insert");
            return;
        }
        value reference = value::null();
        if (n.text_like(s.node)) {
            reference = s.node;
        } else {
            const std::vector<value> kids = n.children(s.node);
            if (static_cast<std::size_t>(s.offset) < kids.size()) {
                reference = kids[static_cast<std::size_t>(s.offset)];
            }
        }
        const value parent = reference.is_object_like() ? n.parent(reference) : s.node;
        if (n.text_like(s.node)) {
            reference = n.call(s.node, "splitText", {value::number(s.offset)});
        }
        if (same(node, reference)) {
            reference = n.get(reference, "nextSibling");
            if (!reference.is_object_like()) { reference = value::null(); }
        }
        n.remove(node);
        double new_offset = reference.is_object_like() ? n.index(reference) : n.length(parent);
        new_offset += n.type(node) == fragment_node ? n.length(node) : 1;
        (void)n.call(parent, "insertBefore", {node, reference});
        if (same(s.node, e.node) && s.offset == e.offset) { set_end(self, {parent, new_offset}); }
    };
    set_method(
        cx, *proto, "insertNode",
        [insert](context & c, std::span<value> a) {
            insert(c, arg(a, 0));
            return value::undefined();
        },
        script::attr_builtin);
    set_method(
        cx, *proto, "surroundContents",
        [this, self_of, start_of, end_of, partially, extract_from, insert,
         select](context & c, std::span<value> a) {
            script::object_object * self = self_of(c);
            if (self == nullptr) { return value::undefined(); }
            const nodes n{c};
            const value parent = arg(a, 0);
            if (!n.is_node(parent)) {
                c.throw_error("TypeError", "surroundContents: the argument is not a Node");
                return value::undefined();
            }
            const point s = start_of(c, self);
            const point e = end_of(c, self);
            // A partially contained non-Text node cannot be surrounded.
            for (value at : {s.node, e.node}) {
                for (; at.is_object_like(); at = n.parent(at)) {
                    if (!n.text_like(at) && partially(n, at, s, e)) {
                        throw_dom_exception(c, "InvalidStateError",
                                            "surroundContents: the range splits a non-Text node");
                        return value::undefined();
                    }
                }
            }
            const double type = n.type(parent);
            if (type == document_node || type == doctype_node || type == fragment_node) {
                throw_dom_exception(c, "InvalidNodeTypeError",
                                    "surroundContents: the new parent cannot hold the contents");
                return value::undefined();
            }
            const value fragment = extract_from(c, true);
            if (!fragment.is_object_like()) { return value::undefined(); }
            for (const value & child : n.children(parent)) { n.remove(child); }
            insert(c, parent);
            if (c.throw_pending()) { return value::undefined(); }
            n.append(parent, fragment);
            select(c, parent, false);
            return value::undefined();
        },
        script::attr_builtin);
    // DOM 5.5 "stringification": the text between the two boundary points.
    set_method(
        cx, *proto, "toString",
        [self_of, start_of, end_of, contained](context & c, std::span<value>) {
            script::object_object * self = self_of(c);
            if (self == nullptr) { return value::undefined(); }
            const nodes n{c};
            const point s = start_of(c, self);
            const point e = end_of(c, self);
            if (same(s.node, e.node) && n.text_like(s.node)) {
                return c.string(n.data(s.node, s.offset, e.offset - s.offset));
            }
            std::string out;
            if (n.text_like(s.node)) {
                out += n.data(s.node, s.offset, n.length(s.node) - s.offset);
            }
            const auto walk = [&](auto && self_walk, value at) -> void {
                if (n.text_like(at) && contained(n, at, s, e)) {
                    out += c.to_string(n.get(at, "data"));
                }
                for (const value & child : n.children(at)) { self_walk(self_walk, child); }
            };
            walk(walk, n.root(s.node));
            if (n.text_like(e.node)) { out += n.data(e.node, 0, e.offset); }
            return c.string(out);
        },
        script::attr_builtin);
    proto->define("@@toStringTag", cx.string("Range"), script::attr_configurable);

    // `new Range()`: collapsed at (document, 0) of the realm's document.
    auto * ctor = cx.allocate<script::native_object>(
        "Range", [this, proto_value](context & c, std::span<value>) -> value {
            const value self = c.current_this();
            if (!self.is_object()) {
                c.throw_error("TypeError", "Failed to construct 'Range': please use the 'new' "
                                           "operator.");
                return value::undefined();
            }
            auto * made = static_cast<script::object_object *>(self.as_heap());
            if (!made->prototype.is_object()) { made->prototype = proto_value; }
            made->set("startContainer", document_);
            made->set("startOffset", value::number(0));
            made->set("endContainer", document_);
            made->set("endOffset", value::number(0));
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
}

// `document.createRange()`: a range collapsed at (this document, 0).
value dom_bindings::create_range(context & cx) {
    const value ctor = cx.global("Range");
    auto * made = cx.allocate<script::object_object>();
    if (ctor.is_callable()) {
        const value proto = cx.lookup_property(ctor, "prototype");
        if (proto.is_object()) { made->prototype = proto; }
    }
    made->set("startContainer", document_);
    made->set("startOffset", value::number(0));
    made->set("endContainer", document_);
    made->set("endOffset", value::number(0));
    return value::object(made);
}

} // namespace ctbrowser::shell
