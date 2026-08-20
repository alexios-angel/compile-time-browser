#include <ctcompile/HTML/DocumentComparator.hpp>

#include <cstddef>
#include <string_view>

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
    (void)w.node(expected.root(), actual.root(), "");
    return w.found;
}

} // namespace ctcompile::html
