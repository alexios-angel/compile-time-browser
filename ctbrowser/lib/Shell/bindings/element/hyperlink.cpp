// dom_bindings - HTMLHyperlinkElementUtils (HTML 4.6.3), the mixin `<a>` and
// `<area>` share: `href`, `origin`, `protocol`, `username`, `password`, `host`,
// `hostname`, `port`, `pathname`, `search` and `hash`, each a getter AND a
// setter over shell/net/url.hpp's `url_record`.
//
// ONE PARSER, ONE SET OF SETTER STEPS. Every getter here re-derives the
// element's url ("reinitialize url": parse the `href` content attribute against
// the document base URL, or null when there is no attribute or it does not
// parse), and every setter runs the URL Standard's §6.1 setter steps through
// `set_url_part` - the same function `URL.prototype` uses - and writes the
// serialisation back to the content attribute. What preceded this was a second,
// hand-cut implementation over `location_url` with no username, no password, no
// opaque-path rules and a `protocol` setter that took any word at all; it
// disagreed with `new URL()` about ~1,900 of url/a-element.html's subtests.
//
// THE DOCUMENT BASE URL IS `<base href>`, HTML 4.2.3 - not the document's own
// address. url/resources/a-element.js sets `document.getElementById("base").href
// = base` before EVERY one of its cases, so without this the whole file resolves
// against the test's own file: URL. The frozen base URL is the first `<base>`
// element in tree order that has an href, parsed against the fallback base URL
// (the document's address); no such element means the fallback itself.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

// HTML 4.6.3's getters, in terms of the record. Each is "url is null" -> the
// null answer, and otherwise the URL Standard's §6.1 getter.
struct hyperlink_attribute {
    const char * name;
    std::string (*get)(const url_record &);
    const char * when_null;      // what the getter answers with no url
    std::optional<url_part> set; // nullopt: readonly (`origin`)
};

constexpr hyperlink_attribute hyperlink_attributes[] = {
    // `origin` is the one readonly member of the mixin.
    {"origin", [](const url_record & u) { return u.origin(); }, "", std::nullopt},
    // ":" and not "" - `a-element.js` reads exactly this to decide that an
    // input FAILED to parse.
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

} // namespace

void dom_bindings::install_hyperlink_utils(context & cx) {
    const atom href_name = atoms_->intern("href");

    // HTML 4.2.3 "document base URL". A WALK PER ACCESS, and deliberately: a
    // <base> can be inserted, removed or have its href rewritten at any moment
    // (a-element.js rewrites it between every subtest), so a cached answer is a
    // stale one. Pages carry a handful of elements before <body>, and the walk
    // stops at the first <base href>.
    // ponytail: linear scan of the tree; cache on the document's version
    // counter if a page with a large <head> ever measures.
    const auto base_url = [this, href_name]() -> std::string {
        const auto txn = doc_->read();
        const auto first_base = [&](auto & self, node_id id) -> node_id {
            // THE NAMESPACE DECIDES: an SVG <base> is not HTML's, and the two
            // intern to the same atom (the CLAUDE.md invariant).
            if (txn.element_ns(id) == node_ns::html && txn.local_name(id) == "base" &&
                txn.has_attribute(id, href_name)) {
                return id;
            }
            for (const node_id child : txn.children(id)) {
                if (const node_id found = self(self, child)) { return found; }
            }
            return node_id{};
        };
        const node_id found = first_base(first_base, txn.document_node());
        if (!found) { return location_href_; }
        const std::string raw{txn.attribute_value(found, href_name)};
        if (location_href_.empty()) { return raw; }
        // The frozen base URL, 4.2.3: the attribute parsed against the
        // FALLBACK base URL, which is the document's own address.
        const std::string resolved = resolve(location_href_, raw);
        return resolved.empty() ? raw : resolved;
    };

    // "Reinitialize url": null when there is no href attribute, and null again
    // when what it holds does not parse.
    const auto url_of = [this, base_url, href_name](node_id id) -> std::optional<url_record> {
        std::string raw;
        {
            const auto txn = doc_->read();
            if (!id || !txn.has_attribute(id, href_name)) { return std::nullopt; }
            raw = std::string{txn.attribute_value(id, href_name)};
        }
        const std::string base = base_url();
        if (base.empty()) { return parse_url(raw); }
        const std::optional<url_record> parsed_base = parse_url(base);
        return parse_url(raw, parsed_base ? &*parsed_base : nullptr);
    };
    const auto href_attribute = [this, href_name](node_id id) -> std::optional<std::string> {
        const auto txn = doc_->read();
        if (!id || !txn.has_attribute(id, href_name)) { return std::nullopt; }
        return std::string{txn.attribute_value(id, href_name)};
    };

    for (const char * which : {"HTMLAnchorElement", "HTMLAreaElement"}) {
        const value iface = interface_prototype(which);
        if (!iface.is_object()) { continue; }
        auto * proto = static_cast<script::object_object *>(iface.as_heap());

        // `href`, which is NOT one of the rows: its getter falls back to the
        // content attribute when the url is null, and its setter writes the
        // attribute unparsed. (It replaces the reflected `url` row installed
        // earlier, whose base URL is the document's address rather than
        // <base>'s.)
        define_getter(
            cx, *proto, "href",
            [this, url_of, href_attribute](context & c, std::span<value>) {
                const node_id id = receiver(c);
                if (const std::optional<url_record> url = url_of(id)) {
                    return c.string(url->serialize());
                }
                return c.string(href_attribute(id).value_or(std::string{}));
            },
            [this, href_name](context & c, std::span<value> a) {
                const node_id id = receiver(c);
                if (!id) { return value::undefined(); }
                (void)doc_->set_attribute(id, href_name, to_usv_string(c.to_string(arg(a, 0))));
                mutated();
                return value::undefined();
            });

        for (const hyperlink_attribute & row : hyperlink_attributes) {
            script::native_fn setter;
            if (row.set) {
                const url_part part = *row.set;
                setter = [this, url_of, part, href_name](context & c, std::span<value> a) {
                    const node_id id = receiver(c);
                    std::optional<url_record> url = url_of(id);
                    // "If url is null, return" - the shape every setter of the
                    // mixin has. Nothing is written and nothing throws.
                    if (!url) { return value::undefined(); }
                    (void)set_url_part(*url, part, to_usv_string(c.to_string(arg(a, 0))));
                    // "Update href": the content attribute takes the record's
                    // serialisation, which is what makes the next read agree.
                    (void)doc_->set_attribute(id, href_name, url->serialize());
                    mutated();
                    return value::undefined();
                };
            }
            define_getter(
                cx, *proto, row.name,
                [this, url_of, row](context & c, std::span<value>) {
                    const std::optional<url_record> url = url_of(receiver(c));
                    return c.string(url ? row.get(*url) : std::string{row.when_null});
                },
                std::move(setter));
        }
    }
}

} // namespace ctbrowser::shell
