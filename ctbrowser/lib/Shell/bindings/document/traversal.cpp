// dom_bindings - DOM 6, traversal: `createNodeIterator`, `createTreeWalker`
// and the NodeFilter constants.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

// NodeFilter's three answers, DOM 6.3.
constexpr unsigned filter_accept = 1;
constexpr unsigned filter_reject = 2;
constexpr unsigned filter_skip = 3;

// A PLACE A WALKER CAN STAND. The Document is one of them and has no node in
// the tree - see as_node.cpp for why - so it is a flag beside an empty handle,
// and `none` is "no such node", which is what every traversal answers when it
// runs out. Nothing here holds a read_txn across a step: each primitive opens
// its own, because a filter is script and may edit the tree between two.
struct spot {
    node_id id;
    bool document = false;
    [[nodiscard]] bool none() const { return !id && !document; }
    [[nodiscard]] bool operator==(const spot &) const = default;
};
constexpr spot no_spot{};
constexpr spot the_document{node_id{}, true};

} // namespace

void dom_bindings::install_traversal(context & cx, script::object_object & doc) {
    // --- the primitives, each one read_txn long ------------------------------

    // THE DOCUMENT'S CHILDREN ARE THE DOCUMENT NODE'S LIST - the doctype, the
    // element and whatever sits beside them - and the document element is in
    // that list with no parent pointer, which is why parent_of and sibling_of
    // ask is_document_child rather than parent(). See document::document_node.
    const auto parent_of = [this](spot at) -> spot {
        if (at.document) { return no_spot; }
        const auto txn = doc_->read();
        if (is_document_child(txn, at.id)) { return the_document; }
        if (const node_id parent = txn.parent(at.id)) { return spot{parent}; }
        return no_spot;
    };
    const auto child_of = [this](spot at, bool last) -> spot {
        const auto txn = doc_->read();
        const std::span<const node_id> kids =
            txn.children(at.document ? txn.document_node() : at.id);
        if (kids.empty()) { return no_spot; }
        return spot{last ? kids.back() : kids.front()};
    };
    const auto sibling_of = [this](spot at, bool next) -> spot {
        if (at.document) { return no_spot; }
        const auto txn = doc_->read();
        const node_id parent =
            is_document_child(txn, at.id) ? txn.document_node() : txn.parent(at.id);
        if (!parent) { return no_spot; }
        const std::span<const node_id> kids = txn.children(parent);
        const auto here = std::ranges::find(kids, at.id);
        if (here == kids.end()) { return no_spot; }
        if (next) { return here + 1 == kids.end() ? no_spot : spot{*(here + 1)}; }
        return here == kids.begin() ? no_spot : spot{*(here - 1)};
    };
    const auto node_type = [this](spot at) -> unsigned {
        if (at.document) { return 9; }
        switch (doc_->read().kind(at.id).value_or(node_kind::element)) {
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
    const auto to_value = [this](context & c, spot at) {
        return at.document ? document_ : wrap(c, at.id);
    };
    const auto from_value = [this](value v) -> spot {
        if (is_the_document(v)) { return the_document; }
        if (const node_id id = handle_of(v)) { return spot{id}; }
        return no_spot;
    };
    const auto property = [](context & c, const char * name) {
        return c.lookup_property(c.current_this(), name);
    };
    const auto store = [](context & c, const char * name, value v) {
        static_cast<script::object_object *>(c.current_this().as_heap())->set(name, v);
    };

    // DOM 6.3, "filter": whatToShow first, then the callback - and the ACTIVE
    // flag, which makes a filter that calls back into its own walker an
    // InvalidStateError rather than a recursion. `nullopt` means script threw,
    // and the caller returns without touching the walker.
    //
    // `this` for the callback is the filter itself, whether it is a function
    // or an object with `acceptNode` - "call a user object's operation" says
    // so, and TreeWalker-acceptNode-filter.html reads it.
    const auto filter = [this, node_type, to_value, property](context & c,
                                                              std::shared_ptr<bool> active,
                                                              spot at) -> std::optional<unsigned> {
        const unsigned what = context::to_uint32(property(c, "whatToShow"));
        if (((what >> (node_type(at) - 1)) & 1u) == 0) { return filter_skip; }
        const value fn = property(c, "filter");
        if (fn.is_nullish()) { return filter_accept; }
        if (*active) {
            throw_dom_exception(c, "InvalidStateError", "the filter is already being run");
            return std::nullopt;
        }
        value operation = fn;
        if (!fn.is_callable()) {
            operation = c.lookup_property(fn, "acceptNode");
            if (!operation.is_callable()) {
                c.throw_error("TypeError", "the NodeFilter has no acceptNode to call");
                return std::nullopt;
            }
        }
        *active = true;
        const value node = to_value(c, at);
        const value answer = c.call(operation, std::span<const value>{&node, 1}, fn);
        *active = false;
        if (c.failed()) { return std::nullopt; }
        // An `unsigned short`: ToUint32 and keep sixteen bits.
        return context::to_uint32(answer) & 0xFFFFu;
    };

    // --- TreeWalker, DOM 6.2 -------------------------------------------------

    const auto install_walker = [=](context & c, script::object_object & walker,
                                    std::shared_ptr<bool> active) {
        const auto method = [&](std::string name, script::native_fn fn) {
            walker.set(name, value::object(c.allocate<script::native_object>(name, std::move(fn))));
        };
        const auto current = [=](context & cx2) {
            return from_value(property(cx2, "currentNode"));
        };
        const auto root = [=](context & cx2) { return from_value(property(cx2, "root")); };
        const auto accept = [=](context & cx2, spot node) {
            const value v = to_value(cx2, node);
            store(cx2, "currentNode", v);
            return v;
        };
        method("parentNode", [=](context & cx2, std::span<value>) {
            spot node = current(cx2);
            while (!node.none() && node != root(cx2)) {
                node = parent_of(node);
                if (node.none()) { break; }
                const auto result = filter(cx2, active, node);
                if (!result) { return value::undefined(); }
                if (*result == filter_accept) { return accept(cx2, node); }
            }
            return value::null();
        });
        // "Traverse children": firstChild and lastChild are one algorithm with
        // the direction as its argument.
        const auto traverse_children = [=](context & cx2, bool first) {
            const spot origin = current(cx2);
            const spot top = root(cx2);
            spot node = child_of(origin, !first);
            while (!node.none()) {
                const auto result = filter(cx2, active, node);
                if (!result) { return value::undefined(); }
                if (*result == filter_accept) { return accept(cx2, node); }
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
        method("firstChild",
               [=](context & cx2, std::span<value>) { return traverse_children(cx2, true); });
        method("lastChild",
               [=](context & cx2, std::span<value>) { return traverse_children(cx2, false); });
        // "Traverse siblings", likewise.
        const auto traverse_siblings = [=](context & cx2, bool next) {
            const spot top = root(cx2);
            spot node = current(cx2);
            if (node == top) { return value::null(); }
            while (true) {
                spot sibling = sibling_of(node, next);
                while (!sibling.none()) {
                    node = sibling;
                    const auto result = filter(cx2, active, node);
                    if (!result) { return value::undefined(); }
                    if (*result == filter_accept) { return accept(cx2, node); }
                    sibling = child_of(node, !next);
                    if (*result == filter_reject || sibling.none()) {
                        sibling = sibling_of(node, next);
                    }
                }
                node = parent_of(node);
                if (node.none() || node == top) { return value::null(); }
                const auto result = filter(cx2, active, node);
                if (!result) { return value::undefined(); }
                if (*result == filter_accept) { return value::null(); }
            }
        };
        method("nextSibling",
               [=](context & cx2, std::span<value>) { return traverse_siblings(cx2, true); });
        method("previousSibling",
               [=](context & cx2, std::span<value>) { return traverse_siblings(cx2, false); });
        method("previousNode", [=](context & cx2, std::span<value>) {
            const spot top = root(cx2);
            spot node = current(cx2);
            while (node != top) {
                spot sibling = sibling_of(node, false);
                while (!sibling.none()) {
                    node = sibling;
                    auto result = filter(cx2, active, node);
                    if (!result) { return value::undefined(); }
                    while (*result != filter_reject) {
                        const spot child = child_of(node, true);
                        if (child.none()) { break; }
                        node = child;
                        result = filter(cx2, active, node);
                        if (!result) { return value::undefined(); }
                    }
                    if (*result == filter_accept) { return accept(cx2, node); }
                    sibling = sibling_of(node, false);
                }
                if (node == top) { return value::null(); }
                node = parent_of(node);
                if (node.none()) { return value::null(); }
                const auto result = filter(cx2, active, node);
                if (!result) { return value::undefined(); }
                if (*result == filter_accept) { return accept(cx2, node); }
            }
            return value::null();
        });
        method("nextNode", [=](context & cx2, std::span<value>) {
            const spot top = root(cx2);
            spot node = current(cx2);
            unsigned result = filter_accept;
            while (true) {
                while (result != filter_reject) {
                    const spot child = child_of(node, false);
                    if (child.none()) { break; }
                    node = child;
                    const auto answer = filter(cx2, active, node);
                    if (!answer) { return value::undefined(); }
                    result = *answer;
                    if (result == filter_accept) { return accept(cx2, node); }
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
                // A currentNode a page moved OUTSIDE the root runs up to nothing;
                // the specification's loop would spin here.
                if (temporary.none()) { return value::null(); }
                const auto answer = filter(cx2, active, node);
                if (!answer) { return value::undefined(); }
                result = *answer;
                if (result == filter_accept) { return accept(cx2, node); }
            }
        });
    };

    // --- NodeIterator, DOM 6.1 -----------------------------------------------
    //
    // ponytail: the pre-removing steps - an iterator whose reference node is
    // removed moves to the node before it - are not run; add them from
    // insert_node/remove paths when NodeIterator-removal.html is measured.

    const auto install_iterator = [=](context & c, script::object_object & iterator,
                                      std::shared_ptr<bool> active) {
        const auto method = [&](std::string name, script::native_fn fn) {
            iterator.set(name,
                         value::object(c.allocate<script::native_object>(name, std::move(fn))));
        };
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
        const auto traverse = [=](context & cx2, bool next) {
            const spot top = from_value(property(cx2, "root"));
            spot node = from_value(property(cx2, "referenceNode"));
            bool before = context::truthy(property(cx2, "pointerBeforeReferenceNode"));
            while (true) {
                if (next == before) {
                    before = !next;
                } else {
                    node = next ? following(node, top) : preceding(node, top);
                    if (node.none()) { return value::null(); }
                }
                const auto result = filter(cx2, active, node);
                if (!result) { return value::undefined(); }
                if (*result == filter_accept) { break; }
            }
            const value found = to_value(cx2, node);
            store(cx2, "referenceNode", found);
            store(cx2, "pointerBeforeReferenceNode", value::boolean(before));
            return found;
        };
        method("nextNode", [=](context & cx2, std::span<value>) { return traverse(cx2, true); });
        method("previousNode",
               [=](context & cx2, std::span<value>) { return traverse(cx2, false); });
        // "detach()" does nothing, and the DOM says so in those words.
        method("detach", [](context &, std::span<value>) { return value::undefined(); });
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
        auto * made = static_cast<script::object_object *>(c.make_object().as_heap());
        if (const value proto = interface_prototype(iterator ? "NodeIterator" : "TreeWalker");
            proto.is_object()) {
            made->prototype = proto;
        }
        // THE STATE IS THE OBJECT'S OWN PROPERTIES - root, whatToShow, filter
        // and the position - which is what roots them for the collector and
        // what the IDL exposes anyway. Only the active flag is C++.
        made->set("root", root);
        made->set("whatToShow", value::number(static_cast<double>(what)));
        made->set("filter", fn);
        auto active = std::make_shared<bool>(false);
        if (iterator) {
            made->set("referenceNode", root);
            made->set("pointerBeforeReferenceNode", value::boolean(true));
            install_iterator(c, *made, active);
        } else {
            made->set("currentNode", root);
            install_walker(c, *made, active);
        }
        return value::object(made);
    };
    const auto method = [&](std::string name, script::native_fn fn) {
        doc.set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    method("createTreeWalker",
           [factory](context & c, std::span<value> args) { return factory(c, args, false); });
    method("createNodeIterator",
           [factory](context & c, std::span<value> args) { return factory(c, args, true); });

    // `NodeFilter` is a callback interface with constants and no constructor:
    // one global, defined once, for the realm the primary document is in.
    if (!secondary_) {
        auto * node_filter = static_cast<script::object_object *>(cx.make_object().as_heap());
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
