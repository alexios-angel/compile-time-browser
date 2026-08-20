#include <ctcompile/HTML/DocumentComparator.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>

namespace ctcompile::html {

using ctbrowser::document;
using ctbrowser::node_id;
using ctbrowser::node_kind;
using ctbrowser::node_ns;
using ctbrowser::read_txn;

namespace {

[[nodiscard]] std::string_view kind_name(node_kind k) {
    switch (k) {
    case node_kind::document: return "#document";
    case node_kind::element: return "element";
    case node_kind::text: return "#text";
    case node_kind::comment: return "#comment";
    }
    return "?";
}

// A tag as TEXT, through the document's own table. See the header for why an
// atom id cannot be compared across documents.
[[nodiscard]] std::string tag_text(const document & doc, const read_txn & read, node_id id) {
    const auto tag = read.tag(id);
    if (!tag) { return {}; }
    return std::string{doc.atoms().text(*tag)};
}

[[nodiscard]] std::string_view ns_name(node_ns ns) {
    return ns == node_ns::svg ? "svg" : "html";
}

struct walker {
    const document & expected;
    const document & actual;
    read_txn a;
    read_txn b;
    std::optional<difference> found;
    // EVERY NODE ONCE, AND NEVER MORE. `document::builder::append` performs no
    // cycle check and no detach - unlike `document::append_child`, which does
    // both - so a blueprint loader written against the builder can append an
    // already-parented node and produce a node reachable from two parents, or
    // a cycle. Against a cycle a children-only walk does not report a
    // difference: it recurses until the stack dies, which is an acceptance
    // test that CRASHES INSTEAD OF FAILING. These two turn both into ordinary
    // reported differences.
    std::unordered_set<std::uint64_t> seen_expected;
    std::unordered_set<std::uint64_t> seen_actual;

    // A CONSTRUCTOR RATHER THAN AGGREGATE INITIALISATION, because the engine's
    // warning set includes -Wmissing-field-initializers and `found` has no
    // business being named at the call site. The two read transactions are
    // built directly from the prvalues - read_txn is deliberately non-copyable,
    // and guaranteed elision is what makes that work here.
    walker(const document & e, const document & c)
        : expected(e), actual(c), a(e.read()), b(c.read()) {}

    [[nodiscard]] bool differ(std::string where, std::string what) {
        found = difference{std::move(where), std::move(what)};
        return false;
    }

    // Returns false as soon as a difference is recorded, so the walk stops at
    // the first one and `found` holds it.
    bool node(node_id ea, node_id eb, const std::string & path) {
        if (!seen_expected.insert(ea.key()).second) {
            return differ(path, "the expected document reaches this node twice - a cycle or a "
                                "node with two parents");
        }
        if (!seen_actual.insert(eb.key()).second) {
            return differ(path, "the actual document reaches this node twice - a cycle or a "
                                "node with two parents");
        }
        // A second bound, because a walk can be finite and still wrong: more
        // visited nodes than the document holds means the tree is not a tree.
        if (seen_expected.size() > expected.node_count() ||
            seen_actual.size() > actual.node_count()) {
            return differ(path, "the walk visited more nodes than the document contains");
        }

        const auto ka = a.kind(ea);
        const auto kb = b.kind(eb);
        if (!ka || !kb) { return differ(path, "a node is unreadable in one of the two documents"); }
        if (*ka != *kb) {
            return differ(
                path,
                std::string{"kind "}.append(kind_name(*ka)).append(" vs ").append(kind_name(*kb)));
        }

        if (*ka == node_kind::element) {
            const std::string ta = tag_text(expected, a, ea);
            const std::string tb = tag_text(actual, b, eb);
            if (ta != tb) { return differ(path, "tag <" + ta + "> vs <" + tb + ">"); }

            // THE NAMESPACE IS PART OF THE IDENTITY, not decoration. An SVG
            // <title> is a tooltip and an HTML <title> is the window's name,
            // and they intern to the SAME atom - so a blueprint that dropped
            // the namespace would rebuild a tree that looks identical here and
            // behaves differently in the style engine and in every DOM walk
            // that asks what an element is.
            if (a.element_ns(ea) != b.element_ns(eb)) {
                return differ(path, std::string{"namespace "}
                                        .append(ns_name(a.element_ns(ea)))
                                        .append(" vs ")
                                        .append(ns_name(b.element_ns(eb))));
            }

            const auto aa = a.attributes(ea);
            const auto ab = b.attributes(eb);
            if (aa.size() != ab.size()) {
                return differ(path, "attribute count " + std::to_string(aa.size()) + " vs " +
                                        std::to_string(ab.size()));
            }
            // IN ORDER. Attribute order is observable from script, so a
            // blueprint has to preserve it; comparing as sets would accept a
            // blueprint that shuffles them.
            for (std::size_t i = 0; i < aa.size(); ++i) {
                const std::string na{expected.atoms().text(aa[i].name)};
                const std::string nb{actual.atoms().text(ab[i].name)};
                if (na != nb) {
                    return differ(path,
                                  "attribute " + std::to_string(i) + " named " + na + " vs " + nb);
                }
                if (aa[i].value != ab[i].value) {
                    return differ(path + "@" + na,
                                  "value \"" + aa[i].value + "\" vs \"" + ab[i].value + "\"");
                }
            }
        } else if (*ka == node_kind::text || *ka == node_kind::comment) {
            if (a.text(ea) != b.text(eb)) {
                return differ(path, "text \"" + std::string{a.text(ea)} + "\" vs \"" +
                                        std::string{b.text(eb)} + "\"");
            }
        }

        const auto ca = a.children(ea);
        const auto cb = b.children(eb);
        if (ca.size() != cb.size()) {
            return differ(path, "child count " + std::to_string(ca.size()) + " vs " +
                                    std::to_string(cb.size()));
        }
        for (std::size_t i = 0; i < ca.size(); ++i) {
            // `node::parent` is a SECOND, independently stored copy of the
            // tree, and it can disagree with children[]: builder::append sets
            // the child's parent but never removes it from a previous parent's
            // list. A comparison that only followed children[] would accept a
            // torn tree, and every consumer that CLIMBS - is_ancestor_of, the
            // shell's hit testing, :root - would then disagree with the one
            // that descends.
            if (a.parent(ca[i]) != ea) {
                return differ(path, "the expected child's parent link does not point back here");
            }
            if (b.parent(cb[i]) != eb) {
                return differ(path, "the actual child's parent link does not point back here");
            }
            const auto kind = a.kind(ca[i]);
            std::string step = "/";
            if (kind && *kind == node_kind::element) {
                step += tag_text(expected, a, ca[i]);
            } else if (kind) {
                step += std::string{kind_name(*kind)};
            }
            step += "[" + std::to_string(i) + "]";
            if (!node(ca[i], cb[i], path + step)) { return false; }
        }
        return true;
    }
};

} // namespace

std::optional<difference> compare(const document & expected, const document & actual) {
    walker w{expected, actual};

    // THE ROOT'S PARENT MUST BE NOTHING. After a parse the root is the <html>
    // ELEMENT - tree_builder::parse calls set_root and the document node the
    // constructor made is left behind - and <html> is never appended to
    // anything, so its parent stays a null handle. style::engine::facts_of
    // computes `is_root` from exactly that, so a blueprint that parents <html>
    // under an element silently loses `:root` and nothing else would say so.
    if (static_cast<bool>(w.a.parent(expected.root())) !=
        static_cast<bool>(w.b.parent(actual.root()))) {
        return difference{"", "one document's root has a parent and the other's does not"};
    }

    (void)w.node(expected.root(), actual.root(), "");
    return w.found;
}

} // namespace ctcompile::html
