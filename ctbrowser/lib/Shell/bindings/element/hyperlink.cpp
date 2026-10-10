//===- hyperlink.cpp - HTMLHyperlinkElementUtils --------------------------===//
//
// HTMLHyperlinkElementUtils (HTML 4.6.3), the mixin `<a>` and `<area>` share:
// `href`, `origin`, `protocol`, `username`, `password`, `host`, `hostname`,
// `port`, `pathname`, `search` and `hash`, each a getter and a setter over
// shell/net/url.hpp's `url_record`.
//
// One parser, one set of setter steps. Every getter re-derives the element's
// url ("reinitialize url": parse the `href` content attribute against the
// document base URL, or null when there is no attribute or it does not
// parse), and every setter runs the URL Standard's §6.1 setter steps through
// `set_url_part` (the same function `URL.prototype` uses) and writes the
// serialisation back to the content attribute, so links and `new URL()` agree.
//
// The document base URL is `<base href>` (HTML 4.2.3), not the document's own
// address: the first `<base>` element in tree order that has an href, parsed
// against the fallback base URL (the document's address); with no such
// element it is the fallback itself.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

/// One HTML 4.6.3 getter/setter pair, in terms of the record: when the url is
/// null the getter answers `when_null`, otherwise the URL Standard's §6.1
/// getter.
struct hyperlink_attribute {
    const char * name;
    std::string (*get)(const url_record &);
    const char * when_null;      // What the getter answers with no url.
    std::optional<url_part> set; // nullopt: readonly (`origin`).
};

constexpr hyperlink_attribute hyperlink_attributes[] = {
    // `origin` is the one readonly member of the mixin.
    {"origin", [](const url_record & u) { return u.origin(); }, "", std::nullopt},
    // ":" and not "": pages (and WPT url/a-element.html) read this to decide
    // that an input failed to parse.
    {"protocol", [](const url_record & u) { return u.protocol(); }, ":", url_part::protocol},
    {"username", [](const url_record & u) { return u.username; }, "", url_part::username},
    {"password", [](const url_record & u) { return u.password; }, "", url_part::password},
    {"host", [](const url_record & u) { return u.host_and_port(); }, "", url_part::host},
    {"hostname", [](const url_record & u) { return u.hostname(); }, "", url_part::hostname},
    {"port", [](const url_record & u) { return u.port_text(); }, "", url_part::port},
    {"pathname", [](const url_record & u) { return u.pathname(); }, "", url_part::pathname},
    {"search", [](const url_record & u) { return u.search(); }, "", url_part::search},
    {"hash", [](const url_record & u) { return u.hash(); }, "", url_part::hash},
};

/// The first HTML <base> in tree order that carries `attribute`. The namespace
/// is checked because an SVG <base> interns to the same atom as HTML's but is
/// not a document base element.
[[nodiscard]] node_id first_base_with(const read_txn & txn, atom attribute) {
    const auto walk = [&](const auto & self, node_id id) -> node_id {
        if (txn.element_ns(id) == node_ns::html && txn.local_name(id) == "base" &&
            txn.has_attribute(id, attribute)) {
            return id;
        }
        for (const node_id child : txn.children(id)) {
            if (const node_id found = self(self, child)) { return found; }
        }
        return node_id{};
    };
    return walk(walk, txn.document_node());
}

} // namespace

/// HTML 4.2.3 "document base URL". It walks the tree on every access on
/// purpose: a <base> can be inserted, removed or have its href rewritten at any
/// moment, so a cached answer would go stale. Pages carry a handful of
/// elements before <body>, and the walk stops at the first <base href>.
// ponytail: linear scan of the tree; cache on the document's version counter
// if a page with a large <head> ever measures.
std::string detail::document_base_url(const read_txn & txn, atom_table & atoms,
                                      const std::string & address) {
    const atom href = atoms.intern("href");
    const node_id found = first_base_with(txn, href);
    if (!found) { return address; }
    const std::string raw{txn.attribute_value(found, href)};
    if (address.empty()) { return raw; }
    // The frozen base URL, HTML 4.2.3: the attribute parsed against the fallback
    // base URL, which is the document's own address.
    const std::string resolved = resolve(address, raw);
    return resolved.empty() ? raw : resolved;
}

/// The `target` of the first HTML <base> that has one, or "" when none does.
std::string detail::document_base_target(const read_txn & txn, atom_table & atoms) {
    const atom target = atoms.intern("target");
    const node_id found = first_base_with(txn, target);
    return found ? std::string{txn.attribute_value(found, target)} : std::string{};
}

void dom_bindings::install_hyperlink_utils(context & cx) {
    const atom href_name = atoms_->intern("href");

    // The receiver's own document's bindings. These accessors live on the
    // prototypes every document shares, and an <a> in a frame's document is
    // read through the frame's bindings (its tree, its address, its <base>),
    // not the page's.
    const auto owner = [this](context & c) -> dom_bindings & {
        dom_bindings * found = owner_of(c.current_this());
        return found == nullptr ? *this : *found;
    };

    // "Reinitialize url": null when there is no href attribute, and null again
    // when what it holds does not parse - against the document base URL, whose
    // fallback is a srcdoc document's container's (fallback_base_url).
    const auto url_of = [href_name](dom_bindings & b, node_id id) -> std::optional<url_record> {
        std::string raw;
        {
            const auto txn = b.doc_->read();
            if (!id || !txn.has_attribute(id, href_name)) { return std::nullopt; }
            raw = std::string{txn.attribute_value(id, href_name)};
        }
        const std::string fallback = b.fallback_base_url();
        const std::string base = document_base_url(b.doc_->read(), *b.atoms_, fallback);
        if (base.empty()) { return parse_url(raw); }
        const std::optional<url_record> parsed_base = parse_url(base);
        return parse_url(raw, parsed_base ? &*parsed_base : nullptr);
    };
    const auto href_attribute = [href_name](dom_bindings & b,
                                            node_id id) -> std::optional<std::string> {
        const auto txn = b.doc_->read();
        if (!id || !txn.has_attribute(id, href_name)) { return std::nullopt; }
        return std::string{txn.attribute_value(id, href_name)};
    };

    for (const char * which : {"HTMLAnchorElement", "HTMLAreaElement"}) {
        const value iface = interface_prototype(which);
        if (!iface.is_object()) { continue; }
        auto * proto = static_cast<script::object_object *>(iface.as_heap());

        // `href`, which is not one of the rows: its getter falls back to the
        // content attribute when the url is null, and its setter writes the
        // attribute unparsed. (It replaces the reflected `url` row installed
        // earlier, whose base URL is the document's address rather than
        // <base>'s.)
        define_getter(
            cx, *proto, "href",
            [owner, url_of, href_attribute](context & c, std::span<value>) {
                dom_bindings & b = owner(c);
                const node_id id = b.receiver(c);
                if (const std::optional<url_record> url = url_of(b, id)) {
                    return c.string(url->serialize());
                }
                return c.string(href_attribute(b, id).value_or(std::string{}));
            },
            [owner, href_name](context & c, std::span<value> a) {
                dom_bindings & b = owner(c);
                const node_id id = b.receiver(c);
                if (!id) { return value::undefined(); }
                (void)b.doc_->set_attribute(id, href_name, to_usv_string(c.to_string(arg(a, 0))));
                b.mutated();
                return value::undefined();
            });

        for (const hyperlink_attribute & row : hyperlink_attributes) {
            script::native_fn setter;
            if (row.set) {
                const url_part part = *row.set;
                setter = [owner, url_of, part, href_name](context & c, std::span<value> a) {
                    dom_bindings & b = owner(c);
                    const node_id id = b.receiver(c);
                    std::optional<url_record> url = url_of(b, id);
                    // "If url is null, return" - the shape every setter of the
                    // mixin has. Nothing is written and nothing throws.
                    if (!url) { return value::undefined(); }
                    (void)set_url_part(*url, part, to_usv_string(c.to_string(arg(a, 0))));
                    // "Update href": the content attribute takes the record's
                    // serialisation, which is what makes the next read agree.
                    (void)b.doc_->set_attribute(id, href_name, url->serialize());
                    b.mutated();
                    return value::undefined();
                };
            }
            define_getter(
                cx, *proto, row.name,
                [owner, url_of, row](context & c, std::span<value>) {
                    dom_bindings & b = owner(c);
                    const std::optional<url_record> url = url_of(b, b.receiver(c));
                    return c.string(url ? row.get(*url) : std::string{row.when_null});
                },
                std::move(setter));
        }
    }
}

} // namespace ctbrowser::shell
