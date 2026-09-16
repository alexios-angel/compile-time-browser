// dom_bindings - DOM 6, traversal: `createNodeIterator`, `createTreeWalker`,
// the NodeIterator and TreeWalker prototypes, and the NodeFilter constants.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

// NodeFilter's three answers, DOM 6.3.
constexpr unsigned filter_accept = 1;
constexpr unsigned filter_reject = 2;
constexpr unsigned filter_skip = 3;

// A PLACE A WALKER CAN STAND: a node of ONE of the realm's documents. The
// walkers are made by `document.createTreeWalker` but a root may be any
// document's node - common.js's `foreignDoc` and `xmlDoc` are both walked -
// so the owner rides beside the id. A Document is its own document node
// (`wrap` hands that back as the page's `document`), and `none` is "no such
// node", which is what every traversal answers when it runs out. Nothing here
// holds a read_txn across a step: each primitive opens its own, because a
// filter is script and may edit the tree between two.
struct spot {
    dom_bindings * owner = nullptr;
    node_id id;
    [[nodiscard]] bool none() const { return owner == nullptr || !id; }
    [[nodiscard]] bool operator==(const spot &) const = default;
};
constexpr spot no_spot{};

// THE STATE IS HIDDEN OWN PROPERTIES of the walker - root, whatToShow, filter
// and the position - which is what roots them for the collector; the IDL
// attributes are accessors on the prototype over them, read-only as the
// interface says (TreeWalker-basic.html asserts each descriptor). The active
// flag is one too, so a filter that calls back into its own walker is an
// InvalidStateError rather than a recursion.
constexpr std::string_view root_slot = "__root";
constexpr std::string_view show_slot = "__whatToShow";
constexpr std::string_view filter_slot = "__filter";
constexpr std::string_view current_slot = "__currentNode";
constexpr std::string_view reference_slot = "__referenceNode";
constexpr std::string_view before_slot = "__pointerBeforeReferenceNode";
constexpr std::string_view active_slot = "__active";

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

void dom_bindings::install_traversal(context & cx, script::object_object & doc) {
    // --- the primitives, each one read_txn long ------------------------------

    // THE DOCUMENT'S CHILDREN ARE THE DOCUMENT NODE'S LIST - the doctype, the
    // element and whatever sits beside them - and the document element is in
    // that list with no parent pointer, which is why parent_of and sibling_of
    // ask dom_parent rather than parent(). See document::document_node.
    const auto parent_of = [](spot at) -> spot {
        const auto txn = at.owner->doc_->read();
        return spot{at.owner, dom_parent(txn, at.id)};
    };
    const auto child_of = [](spot at, bool last) -> spot {
        const auto txn = at.owner->doc_->read();
        const std::span<const node_id> kids = txn.children(at.id);
        if (kids.empty()) { return no_spot; }
        return spot{at.owner, last ? kids.back() : kids.front()};
    };
    const auto sibling_of = [](spot at, bool next) -> spot {
        const auto txn = at.owner->doc_->read();
        const node_id parent = dom_parent(txn, at.id);
        if (!parent) { return no_spot; }
        const std::span<const node_id> kids = txn.children(parent);
        const auto here = std::ranges::find(kids, at.id);
        if (here == kids.end()) { return no_spot; }
        if (next) { return here + 1 == kids.end() ? no_spot : spot{at.owner, *(here + 1)}; }
        return here == kids.begin() ? no_spot : spot{at.owner, *(here - 1)};
    };
    const auto node_type = [](spot at) -> unsigned {
        switch (at.owner->doc_->read().kind(at.id).value_or(node_kind::element)) {
        case node_kind::text: return 3;
        case node_kind::cdata_section: return 4;
        case node_kind::processing_instruction: return 7;
        case node_kind::comment: return 8;
        case node_kind::document: return 9;
        case node_kind::document_type: return 10;
        case node_kind::document_fragment: return 11;
        case node_kind::element: break;
        }
        return 1;
    };
    const auto to_value = [](context & c, spot at) { return at.owner->wrap(c, at.id); };
    // ANY DOCUMENT OF THE REALM: the page's, an iframe's, one createDocument
    // made. A Document is the owner's document node.
    const auto from_value = [this](value v) -> spot {
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
        return no_spot;
    };

    // DOM 6.3, "filter": whatToShow first, then the callback - and the ACTIVE
    // flag. `nullopt` means script threw, and the caller returns without
    // touching the walker.
    //
    // `this` for the callback is the filter itself, whether it is a function
    // or an object with `acceptNode` - "call a user object's operation" says
    // so, and TreeWalker-acceptNode-filter.html reads it.
    const auto filter = [this, node_type, to_value](context & c, script::object_object * self,
                                                    spot at) -> std::optional<unsigned> {
        if (context::truthy(slot(self, active_slot))) {
            throw_dom_exception(c, "InvalidStateError", "the filter is already being run");
            return std::nullopt;
        }
        const unsigned what = context::to_uint32(slot(self, show_slot));
        if (((what >> (node_type(at) - 1)) & 1u) == 0) { return filter_skip; }
        const value fn = slot(self, filter_slot);
        if (fn.is_nullish()) { return filter_accept; }
        const auto set_active = [self](bool on) {
            self->set(std::string{active_slot}, value::boolean(on));
        };
        set_active(true);
        value operation = fn;
        if (!fn.is_callable()) {
            const std::size_t unwinds = c.unwinds();
            operation = c.lookup_property(fn, "acceptNode");
            if (c.throw_pending() || c.unwinds() != unwinds) {
                set_active(false);
                return std::nullopt;
            }
            if (!operation.is_callable()) {
                set_active(false);
                c.throw_error("TypeError", "the NodeFilter has no acceptNode to call");
                return std::nullopt;
            }
        }
        const value node = to_value(c, at);
        const value answer = c.call(operation, std::span<const value>{&node, 1}, fn);
        set_active(false);
        if (c.throw_pending()) { return std::nullopt; }
        // An `unsigned short`: ToUint32 and keep sixteen bits.
        return context::to_uint32(answer) & 0xFFFFu;
    };

    // --- the prototypes, installed once, on first use ------------------------
    //
    // ON FIRST USE rather than here: install_document runs before the interface
    // table exists, and the two prototypes are entries in it. The natives read
    // the walker's state off `this`, so one prototype serves every document.
    const auto readonly = [](context & c, script::object_object & proto, const char * name,
                             std::string_view which) {
        define_getter(c, proto, name, [which](context & c2, std::span<value>) {
            script::object_object * self = self_object(c2);
            return self == nullptr ? value::undefined() : slot(self, which);
        });
    };

    // --- TreeWalker, DOM 6.2 -------------------------------------------------

    const auto install_walker = [=](context & c, script::object_object & proto) {
        const auto current = [=](script::object_object * self) {
            return from_value(slot(self, current_slot));
        };
        const auto root = [=](script::object_object * self) {
            return from_value(slot(self, root_slot));
        };
        const auto accept = [=](context & c2, script::object_object * self, spot node) {
            const value v = to_value(c2, node);
            self->set(std::string{current_slot}, v);
            return v;
        };
        readonly(c, proto, "root", root_slot);
        readonly(c, proto, "whatToShow", show_slot);
        readonly(c, proto, "filter", filter_slot);
        define_getter(
            c, proto, "currentNode",
            [](context & c2, std::span<value>) {
                script::object_object * self = self_object(c2);
                return self == nullptr ? value::undefined() : slot(self, current_slot);
            },
            [from_value](context & c2, std::span<value> a) {
                script::object_object * self = self_object(c2);
                if (self == nullptr) { return value::undefined(); }
                if (from_value(arg(a, 0)).none()) {
                    c2.throw_error("TypeError", "Failed to set the 'currentNode' property on "
                                                "'TreeWalker': the value is not of type 'Node'.");
                    return value::undefined();
                }
                self->set(std::string{current_slot}, arg(a, 0));
                return value::undefined();
            });
        set_method(
            c, proto, "parentNode",
            [=](context & c2, std::span<value>) {
                script::object_object * self = self_object(c2);
                if (self == nullptr) { return value::undefined(); }
                const spot top = root(self);
                spot node = current(self);
                while (!node.none() && node != top) {
                    node = parent_of(node);
                    if (node.none()) { break; }
                    const auto result = filter(c2, self, node);
                    if (!result) { return value::undefined(); }
                    if (*result == filter_accept) { return accept(c2, self, node); }
                }
                return value::null();
            },
            script::attr_builtin);
        // "Traverse children": firstChild and lastChild are one algorithm with
        // the direction as its argument.
        const auto traverse_children = [=](context & c2, bool first) {
            script::object_object * self = self_object(c2);
            if (self == nullptr) { return value::undefined(); }
            const spot origin = current(self);
            if (origin.none()) { return value::null(); }
            const spot top = root(self);
            spot node = child_of(origin, !first);
            while (!node.none()) {
                const auto result = filter(c2, self, node);
                if (!result) { return value::undefined(); }
                if (*result == filter_accept) { return accept(c2, self, node); }
                if (*result == filter_skip) {
                    if (const spot child = child_of(node, !first); !child.none()) {
                        node = child;
                        continue;
                    }
                }
                while (!node.none()) {
                    if (const spot next = sibling_of(node, first); !next.none()) {
                        node = next;
                        break;
                    }
                    const spot parent = parent_of(node);
                    if (parent.none() || parent == top || parent == origin) {
                        return value::null();
                    }
                    node = parent;
                }
            }
            return value::null();
        };
        set_method(
            c, proto, "firstChild",
            [=](context & c2, std::span<value>) { return traverse_children(c2, true); },
            script::attr_builtin);
        set_method(
            c, proto, "lastChild",
            [=](context & c2, std::span<value>) { return traverse_children(c2, false); },
            script::attr_builtin);
        // "Traverse siblings", likewise.
        const auto traverse_siblings = [=](context & c2, bool next) {
            script::object_object * self = self_object(c2);
            if (self == nullptr) { return value::undefined(); }
            const spot top = root(self);
            spot node = current(self);
            if (node.none() || node == top) { return value::null(); }
            while (true) {
                spot sibling = sibling_of(node, next);
                while (!sibling.none()) {
                    node = sibling;
                    const auto result = filter(c2, self, node);
                    if (!result) { return value::undefined(); }
                    if (*result == filter_accept) { return accept(c2, self, node); }
                    sibling = child_of(node, !next);
                    if (*result == filter_reject || sibling.none()) {
                        sibling = sibling_of(node, next);
                    }
                }
                node = parent_of(node);
                if (node.none() || node == top) { return value::null(); }
                const auto result = filter(c2, self, node);
                if (!result) { return value::undefined(); }
                if (*result == filter_accept) { return value::null(); }
            }
        };
        set_method(
            c, proto, "nextSibling",
            [=](context & c2, std::span<value>) { return traverse_siblings(c2, true); },
            script::attr_builtin);
        set_method(
            c, proto, "previousSibling",
            [=](context & c2, std::span<value>) { return traverse_siblings(c2, false); },
            script::attr_builtin);
        set_method(
            c, proto, "previousNode",
            [=](context & c2, std::span<value>) {
                script::object_object * self = self_object(c2);
                if (self == nullptr) { return value::undefined(); }
                const spot top = root(self);
                spot node = current(self);
                while (!node.none() && node != top) {
                    spot sibling = sibling_of(node, false);
                    while (!sibling.none()) {
                        node = sibling;
                        auto result = filter(c2, self, node);
                        if (!result) { return value::undefined(); }
                        while (*result != filter_reject) {
                            const spot child = child_of(node, true);
                            if (child.none()) { break; }
                            node = child;
                            result = filter(c2, self, node);
                            if (!result) { return value::undefined(); }
                        }
                        if (*result == filter_accept) { return accept(c2, self, node); }
                        sibling = sibling_of(node, false);
                    }
                    if (node == top) { return value::null(); }
                    node = parent_of(node);
                    if (node.none()) { return value::null(); }
                    const auto result = filter(c2, self, node);
                    if (!result) { return value::undefined(); }
                    if (*result == filter_accept) { return accept(c2, self, node); }
                }
                return value::null();
            },
            script::attr_builtin);
        set_method(
            c, proto, "nextNode",
            [=](context & c2, std::span<value>) {
                script::object_object * self = self_object(c2);
                if (self == nullptr) { return value::undefined(); }
                const spot top = root(self);
                spot node = current(self);
                if (node.none()) { return value::null(); }
                unsigned result = filter_accept;
                while (true) {
                    while (result != filter_reject) {
                        const spot child = child_of(node, false);
                        if (child.none()) { break; }
                        node = child;
                        const auto answer = filter(c2, self, node);
                        if (!answer) { return value::undefined(); }
                        result = *answer;
                        if (result == filter_accept) { return accept(c2, self, node); }
                    }
                    spot temporary = node;
                    while (!temporary.none()) {
                        if (temporary == top) { return value::null(); }
                        if (const spot next = sibling_of(temporary, true); !next.none()) {
                            node = next;
                            break;
                        }
                        temporary = parent_of(temporary);
                    }
                    // A currentNode a page moved OUTSIDE the root runs up to
                    // nothing; the specification's loop would spin here.
                    if (temporary.none()) { return value::null(); }
                    const auto answer = filter(c2, self, node);
                    if (!answer) { return value::undefined(); }
                    result = *answer;
                    if (result == filter_accept) { return accept(c2, self, node); }
                }
            },
            script::attr_builtin);
    };

    // --- NodeIterator, DOM 6.1 -----------------------------------------------
    //
    // ponytail: the pre-removing steps - an iterator whose reference node is
    // removed moves to the node before it - need a hook where the tree is
    // edited (document::remove_child), which this file cannot reach; see the
    // report. NodeIterator-removal.html is what measures them.

    const auto install_iterator = [=](context & c, script::object_object & proto) {
        readonly(c, proto, "root", root_slot);
        readonly(c, proto, "whatToShow", show_slot);
        readonly(c, proto, "filter", filter_slot);
        readonly(c, proto, "referenceNode", reference_slot);
        readonly(c, proto, "pointerBeforeReferenceNode", before_slot);
        // "Following" and "preceding" in tree order, within the root.
        const auto following = [=](spot node, spot top) {
            if (const spot child = child_of(node, false); !child.none()) { return child; }
            for (spot at = node; !at.none() && at != top; at = parent_of(at)) {
                if (const spot next = sibling_of(at, true); !next.none()) { return next; }
            }
            return no_spot;
        };
        const auto preceding = [=](spot node, spot top) {
            if (node == top) { return no_spot; }
            spot at = sibling_of(node, false);
            if (at.none()) { return parent_of(node); }
            for (spot child = child_of(at, true); !child.none(); child = child_of(at, true)) {
                at = child;
            }
            return at;
        };
        const auto traverse = [=](context & c2, bool next) {
            script::object_object * self = self_object(c2);
            if (self == nullptr) { return value::undefined(); }
            const spot top = from_value(slot(self, root_slot));
            spot node = from_value(slot(self, reference_slot));
            if (node.none()) { return value::null(); }
            bool before = context::truthy(slot(self, before_slot));
            while (true) {
                if (next == before) {
                    before = !next;
                } else {
                    node = next ? following(node, top) : preceding(node, top);
                    if (node.none()) { return value::null(); }
                }
                const auto result = filter(c2, self, node);
                if (!result) { return value::undefined(); }
                if (*result == filter_accept) { break; }
            }
            const value found = to_value(c2, node);
            self->set(std::string{reference_slot}, found);
            self->set(std::string{before_slot}, value::boolean(before));
            return found;
        };
        set_method(
            c, proto, "nextNode",
            [=](context & c2, std::span<value>) { return traverse(c2, true); },
            script::attr_builtin);
        set_method(
            c, proto, "previousNode",
            [=](context & c2, std::span<value>) { return traverse(c2, false); },
            script::attr_builtin);
        // "detach()" does nothing, and the DOM says so in those words.
        set_method(
            c, proto, "detach", [](context &, std::span<value>) { return value::undefined(); },
            script::attr_builtin);
    };

    // --- the two factories ---------------------------------------------------

    const auto factory = [this, from_value, install_walker,
                          install_iterator](context & c, std::span<value> args, bool iterator) {
        const value root = arg(args, 0);
        if (from_value(root).none()) {
            c.throw_error("TypeError", std::string{"Failed to execute '"} +
                                           (iterator ? "createNodeIterator" : "createTreeWalker") +
                                           "' on 'Document': parameter 1 is not of type 'Node'.");
            return value::undefined();
        }
        const value show = arg(args, 1);
        const unsigned what = show.is_undefined() ? 0xFFFFFFFFu : context::to_uint32(show);
        const value given = arg(args, 2);
        const value fn = given.is_nullish() ? value::null() : given;
        if (!fn.is_null() && !fn.is_object_like()) {
            c.throw_error("TypeError", "the filter is neither a function nor an object");
            return value::undefined();
        }
        auto * made = c.allocate<script::object_object>();
        const value proto = interface_prototype(iterator ? "NodeIterator" : "TreeWalker");
        if (proto.is_object()) {
            auto * on = static_cast<script::object_object *>(proto.as_heap());
            made->prototype = proto;
            if (on->find("nextNode") == nullptr) {
                if (iterator) {
                    install_iterator(c, *on);
                } else {
                    install_walker(c, *on);
                }
            }
        }
        made->define(std::string{root_slot}, root, script::attr_none);
        made->define(std::string{show_slot}, value::number(static_cast<double>(what)),
                     script::attr_none);
        made->define(std::string{filter_slot}, fn, script::attr_none);
        made->define(std::string{active_slot}, value::boolean(false), script::attr_none);
        if (iterator) {
            made->define(std::string{reference_slot}, root, script::attr_none);
            made->define(std::string{before_slot}, value::boolean(true), script::attr_none);
        } else {
            made->define(std::string{current_slot}, root, script::attr_none);
        }
        return value::object(made);
    };
    set_method(cx, doc, "createTreeWalker",
               [factory](context & c, std::span<value> args) { return factory(c, args, false); });
    set_method(cx, doc, "createNodeIterator",
               [factory](context & c, std::span<value> args) { return factory(c, args, true); });

    // `NodeFilter` is a callback interface with constants and no constructor:
    // one global, defined once, for the realm the primary document is in.
    if (!secondary_) {
        auto * node_filter = cx.allocate<script::object_object>();
        for (const auto & [name, bits] : std::initializer_list<std::pair<const char *, double>>{
                 {"FILTER_ACCEPT", filter_accept},
                 {"FILTER_REJECT", filter_reject},
                 {"FILTER_SKIP", filter_skip},
                 {"SHOW_ALL", 0xFFFFFFFF},
                 {"SHOW_ELEMENT", 0x1},
                 {"SHOW_ATTRIBUTE", 0x2},
                 {"SHOW_TEXT", 0x4},
                 {"SHOW_CDATA_SECTION", 0x8},
                 {"SHOW_ENTITY_REFERENCE", 0x10},
                 {"SHOW_ENTITY", 0x20},
                 {"SHOW_PROCESSING_INSTRUCTION", 0x40},
                 {"SHOW_COMMENT", 0x80},
                 {"SHOW_DOCUMENT", 0x100},
                 {"SHOW_DOCUMENT_TYPE", 0x200},
                 {"SHOW_DOCUMENT_FRAGMENT", 0x400},
                 {"SHOW_NOTATION", 0x800}}) {
            node_filter->set(name, value::number(bits));
        }
        cx.define_global("NodeFilter", value::object(node_filter));
    }
}

} // namespace ctbrowser::shell
