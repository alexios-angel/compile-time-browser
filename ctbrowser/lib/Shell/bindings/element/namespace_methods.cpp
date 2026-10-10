//===- namespace_methods.cpp - Node namespace lookups, normalize ----------===//
//
// dom_bindings::install_node_namespaces: the Node operations
// lookupNamespaceURI, isDefaultNamespace, lookupPrefix (DOM 4.4), normalize
// and contains, on Node.prototype.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_node_namespaces(context & cx) {
    const std::initializer_list<const char *> node = {"Node"};
    // The element whose namespace declarations a lookup on `self` consults
    // (DOM 4.4 "locate a namespace"): the element itself, a character data
    // node's parent element, a document's document element, or nothing.
    const auto namespace_element = [this](context & c, node_id self) {
        // An Attr names its ownerElement, and a detached one names nothing.
        if (!self) { return handle_of(c.lookup_property(c.current_this(), "ownerElement")); }
        const auto txn = doc_->read();
        switch (txn.kind(self).value_or(node_kind::element)) {
        case node_kind::element: return self;
        case node_kind::text:
        case node_kind::comment:
        case node_kind::cdata_section:
        case node_kind::processing_instruction: {
            const node_id parent = txn.parent(self);
            return parent && txn.kind(parent).value_or(node_kind::text) == node_kind::element
                       ? parent
                       : node_id{};
        }
        case node_kind::document:
            for (const node_id child : txn.children(self)) {
                if (txn.kind(child).value_or(node_kind::text) == node_kind::element) {
                    return child;
                }
            }
            return node_id{};
        case node_kind::document_fragment:
        case node_kind::document_type: return node_id{};
        }
        return node_id{};
    };
    define_operation(cx, node, "lookupNamespaceURI", 1,
                     [this, namespace_element](context & c, std::span<value> args) {
                         const value given = arg(args, 0);
                         // "If prefix is the empty string, then set it to null."
                         const std::string prefix =
                             given.is_nullish() ? std::string{} : c.to_string(given);
                         const std::string found = locate_namespace(
                             namespace_element(c, receiver(c)), prefix.empty() ? nullptr : &prefix);
                         return found.empty() ? value::null() : c.string(found);
                     });
    define_operation(cx, node, "isDefaultNamespace", 1,
                     [this, namespace_element](context & c, std::span<value> args) {
                         const value given = arg(args, 0);
                         const std::string want =
                             given.is_nullish() ? std::string{} : c.to_string(given);
                         return value::boolean(
                             locate_namespace(namespace_element(c, receiver(c)), nullptr) == want);
                     });
    define_operation(
        cx, node, "lookupPrefix", 1, [this, namespace_element](context & c, std::span<value> args) {
            const value given = arg(args, 0);
            if (given.is_nullish()) { return value::null(); }
            const std::string found =
                locate_namespace_prefix(namespace_element(c, receiver(c)), c.to_string(given));
            return found.empty() ? value::null() : c.string(found);
        });
    // `normalize()`, DOM 4.4: every empty Text descendant is removed, and every
    // run of contiguous Text siblings is merged into its first member, not a new
    // node, so a page holding that node sees the merged data. An empty first
    // member is removed instead of absorbing the run, which is the order the
    // specification walks in.
    define_operation(cx, node, "normalize", 0, [this](context & c, std::span<value>) {
        const node_id self = receiver(c);
        if (!self) { return value::undefined(); }
        // One run of contiguous Text siblings: its first member, the data it
        // ends up with, and each absorbed sibling with the code-unit length
        // the first member had when it was appended - what the live ranges
        // in it move to (DOM 4.7 steps 7.5-7.6).
        struct absorbed {
            node_id node;
            double index;
            double at;
        };
        struct run {
            node_id node;
            std::string data;
            std::uint32_t units = 0;
            node_id parent;
            std::vector<absorbed> rest;
        };
        std::vector<run> merged;
        std::vector<node_id> removed;
        {
            const auto txn = doc_->read();
            const auto is_text = [&txn](node_id one) {
                return txn.kind(one).value_or(node_kind::element) == node_kind::text;
            };
            const auto units_of = [](std::string_view text) {
                return static_cast<double>(script::utf16::length(text));
            };
            const auto walk = [&](auto && again, node_id at) -> void {
                const std::span<const node_id> kids = txn.children(at);
                for (std::size_t i = 0; i < kids.size();) {
                    if (!is_text(kids[i])) {
                        again(again, kids[i]);
                        ++i;
                        continue;
                    }
                    if (txn.text(kids[i]).empty()) {
                        removed.push_back(kids[i]);
                        ++i;
                        continue;
                    }
                    run one{kids[i], std::string{txn.text(kids[i])}, 0, at, {}};
                    one.units = static_cast<std::uint32_t>(units_of(one.data));
                    double length = one.units;
                    std::size_t j = i + 1;
                    for (; j < kids.size() && is_text(kids[j]); ++j) {
                        one.rest.push_back(absorbed{kids[j], static_cast<double>(j), length});
                        length += units_of(txn.text(kids[j]));
                        one.data += txn.text(kids[j]);
                        removed.push_back(kids[j]);
                    }
                    if (j > i + 1) { merged.push_back(std::move(one)); }
                    i = j;
                }
            };
            walk(walk, self);
        }
        if (merged.empty() && removed.empty()) { return value::undefined(); }
        // One mutation each, as DOM 4.4's normalize has them: the data change
        // is one record and every removal is its own, with the siblings the
        // node had when it was removed (MutationObserver sees each). The data
        // is appended (a replace of nothing at the old length) and the absorbed
        // siblings' range boundaries move into the node before they go.
        for (const run & one : merged) {
            const std::size_t added =
                script::utf16::length(std::string_view{one.data}.substr(one.units));
            (void)doc_->set_text(
                one.node, one.data,
                document::data_edit{one.units, 0, static_cast<std::uint32_t>(added)});
            // The data step's range moves (7.3) settle before the absorbed
            // siblings' boundaries move into the node (7.5-7.8), or a
            // boundary just moved to (node, length + offset) would be pushed
            // along by the append it was placed after.
            mutated();
            for (const absorbed & gone : one.rest) {
                absorb_live_ranges(gone.node, one.parent, gone.index, one.node, gone.at);
            }
        }
        for (const node_id node : removed) {
            (void)doc_->remove_child(node);
            mutated();
        }
        return value::undefined();
    });
    // `contains(other)`: whether `other` is an inclusive descendant, so
    // `el.contains(el)` is true.
    define_operation(cx, node, "contains", 1, [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id other = handle_of(arg(args, 0));
        if (!self || !other) { return value::boolean(false); }
        return value::boolean(doc_->read().is_ancestor_of(self, other));
    });
}

} // namespace ctbrowser::shell
