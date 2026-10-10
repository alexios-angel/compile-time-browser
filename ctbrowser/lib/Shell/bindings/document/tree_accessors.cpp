//===- tree_accessors.cpp - Document tree accessors -------------*- C++ -*-===//
//
// dom_bindings - the HTML tree accessors on Document: documentElement,
// rootElement, body, activeElement, dir, the legacy body colours, title, and
// the eight collections (images, forms, links, ...).
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

/// Installs the HTML tree accessors on \p doc: `document.title`,
/// `document.body` and the other accessors and collections HTML 3.1.5 hangs
/// off the Document. All of them are accessors that read the tree on every
/// access rather than data properties refreshed each tick, because pages
/// write and then read back in the same statement:
/// `document.title = "x"; assert_equals(document.title, "x")` must pass.
void dom_bindings::install_tree_accessors(context & cx, script::object_object & doc) {
    // `document.documentElement` and `document.body` are getters that call
    // `wrap()` on every read: that refreshes the wrapper's `ownerDocument`
    // (which did not exist yet at install time) and lets `body` follow a page
    // that replaces it.
    //
    // The body is not `find_by_tag("body")`: HTML defines it as the first
    // child of the document element that is a `body` or a `frameset`, so a
    // `<body>` inside a `<div>` is not it (Document.body.html).
    // `documentElement` is the root, not `find_by_tag("html")`. For a parsed
    // page the two are the same node (this tree builder makes `<html>` the
    // root), but `createDocument(null, "foo")` has a root called `foo` and no
    // `<html>` anywhere. The root of a document that was never parsed is the
    // Document node itself, which is not an element, so
    // `createDocument(null, "").documentElement === null`.
    define_getter(cx, doc, "documentElement", [this](context & c, std::span<value>) {
        const auto txn = doc_->read();
        const node_id root = txn.root();
        if (txn.kind(root).value_or(node_kind::document) != node_kind::element) {
            return value::null();
        }
        return wrap(c, root);
    });
    // `rootElement` (SVG 2, partial interface Document): the document element
    // when it is an SVG <svg>, else null. This is how a script reaches the root
    // of a parsed .svg (DOMParser-parseFromString-xml-scripting-support-disabled).
    define_getter(cx, doc, "rootElement", [this](context & c, std::span<value>) {
        const auto txn = doc_->read();
        const node_id root = txn.root();
        if (txn.kind(root).value_or(node_kind::document) != node_kind::element ||
            txn.element_ns(root) != node_ns::svg || txn.local_name(root) != "svg") {
            return value::null();
        }
        return wrap(c, root);
    });
    define_getter(
        cx, doc, "body", [this](context & c, std::span<value>) { return wrap(c, body_element()); },
        [this](context & c, std::span<value> a) {
            const node_id fresh = handle_of(arg(a, 0));
            // WebIDL conversion first: anything that is not an element at all
            // (a string, say) fails the HTMLElement? conversion with a
            // TypeError; null and a non-body element pass it and fail HTML's check.
            if (!fresh && !arg(a, 0).is_null()) {
                c.throw_error("TypeError", "document.body must be an HTMLElement or null");
                return value::undefined();
            }
            std::string local;
            if (fresh) {
                const auto txn = doc_->read();
                const std::string_view qualified = atoms_->text(txn.tag(fresh).value_or(atom{}));
                const std::size_t colon = qualified.find(':');
                local = colon == std::string_view::npos ? std::string{qualified}
                                                        : std::string{qualified.substr(colon + 1)};
            }
            // "If the new value is not a body or frameset element, then throw a
            // HierarchyRequestError". Null is not one either, which is what
            // `document.body = null` tests.
            if (local != "body" && local != "frameset") {
                throw_dom_exception(c, "HierarchyRequestError",
                                    "document.body must be a body or frameset element");
                return value::undefined();
            }
            const node_id existing = body_element();
            if (existing == fresh) { return value::undefined(); }
            // "If there is no document element, throw a HierarchyRequestError";
            // otherwise the new body goes on the document element whatever it
            // is called (Document.body.html appends one to a <test> root).
            node_id root;
            {
                const auto txn = doc_->read();
                root = txn.root();
                if (txn.kind(root).value_or(node_kind::document) != node_kind::element) {
                    root = node_id{};
                }
            }
            if (!root) {
                throw_dom_exception(c, "HierarchyRequestError",
                                    "document.body: there is no document element");
                return value::undefined();
            }
            // Insert before the old body and then remove it, rather than the
            // other way round: removing first leaves the document with no body
            // for the length of one statement, and `mutated()` is not the only
            // thing that reads the tree between them.
            if (existing) {
                (void)doc_->insert_before(root, fresh, existing);
                (void)doc_->remove_child(existing);
            } else {
                (void)doc_->append_child(root, fresh);
            }
            mutated();
            return value::undefined();
        });
    // `document.activeElement`, HTML 6.6.3: the focused element retargeted
    // against the document (a focus inside a shadow tree answers with the
    // outermost host, so nothing inside a closed tree leaks out this way),
    // else the body, else the document element. A getter rather than a
    // per-tick property so it is correct during parsing, when
    // shadow-dom/focus/* first asserts it equals document.body.
    define_getter(cx, doc, "activeElement", [this](context & c, std::span<value>) {
        node_id active = focused_ && is_connected(focused_) ? focused_ : node_id{};
        node_id root;
        {
            const auto txn = doc_->read();
            // Bounded walk out through nested shadow hosts; 64 levels is far
            // beyond any real page and stops a malformed host cycle.
            for (int guard = 0; active && guard < 64; ++guard) {
                const shadow_tree * tree = shadow_tree_of(root_of_tree(txn, active, false));
                if (tree == nullptr) { break; }
                active = tree->host;
            }
            if (txn.kind(txn.root()).value_or(node_kind::document) == node_kind::element) {
                root = txn.root();
            }
        }
        if (!active) { active = body_element(); }
        if (!active) { active = root; }
        return active ? wrap(c, active) : value::null();
    });

    // `document.dir`, HTML 3.2.6.4: the html element's `dir`, limited to the
    // three known values on the way out and written verbatim on the way in;
    // "" when there is no html element. Tested by reflection-sections.html's
    // #document.dir rows and document-dir.html.
    const auto html_element = [this]() -> node_id {
        const auto txn = doc_->read();
        const node_id root = txn.root();
        if (txn.element_ns(root) != node_ns::html) { return node_id{}; }
        return txn.local_name(root) == "html" ? root : node_id{};
    };
    define_getter(
        cx, doc, "dir",
        [this, html_element](context & c, std::span<value>) {
            const node_id html = html_element();
            if (!html) { return c.string(""); }
            const std::string folded =
                ascii_lower_copy(doc_->read().attribute_value(html, atoms_->intern("dir")));
            return c.string(folded == "ltr" || folded == "rtl" || folded == "auto" ? folded
                                                                                   : std::string{});
        },
        [this, html_element](context & c, std::span<value> a) {
            if (const node_id html = html_element()) {
                (void)doc_->set_attribute(html, atoms_->intern("dir"), arg_string(c, a, 0));
                mutated();
            }
            return value::undefined();
        });
    // The five legacy body colours, HTML 16.3 (obsolete):
    // [LegacyNullToEmptyString] DOMStrings over the body element's attribute,
    // "" when the body element is a frameset rather than a body.
    for (const auto & [idl, content] :
         {std::pair{"fgColor", "text"}, std::pair{"bgColor", "bgcolor"},
          std::pair{"linkColor", "link"}, std::pair{"vlinkColor", "vlink"},
          std::pair{"alinkColor", "alink"}}) {
        const auto body = [this]() -> node_id {
            const node_id found = body_element();
            if (!found) { return found; }
            return doc_->read().local_name(found) == "body" ? found : node_id{};
        };
        const std::string attribute{content};
        define_getter(
            cx, doc, idl,
            [this, body, attribute](context & c, std::span<value>) {
                const node_id element = body();
                if (!element) { return c.string(""); }
                return c.string(
                    std::string{doc_->read().attribute_value(element, atoms_->intern(attribute))});
            },
            [this, body, attribute](context & c, std::span<value> a) {
                if (const node_id element = body()) {
                    (void)doc_->set_attribute(element, atoms_->intern(attribute),
                                              arg(a, 0).is_null() ? std::string{}
                                                                  : arg_string(c, a, 0));
                    mutated();
                }
                return value::undefined();
            });
    }

    // `document.title`, HTML 4.2.2, both halves.
    //
    // The getter strips and collapses whitespace and the setter does not: the
    // DOM keeps the text the page wrote and the IDL attribute reports it
    // normalised, so `document.title = "two  spaces"` reads back "two spaces"
    // while the text node still holds two (document.title-03.html).
    //
    // The setter can legitimately do nothing. With no title element and no
    // head there is nowhere to put one (HTML says return), so a page that
    // removes its head and then assigns a title has a document whose title is
    // the empty string (document.title-01.html, which then checks that a
    // `<title>` appended to the body is found).
    define_getter(
        cx, doc, "title",
        [this](context & c, std::span<value>) {
            const node_id title = title_element();
            return c.string(title ? collapse_whitespace(text_content(title), html_whitespace)
                                  : std::string{});
        },
        [this](context & c, std::span<value> a) {
            const std::string wanted = arg_string(c, a, 0);
            // HTML 4.2.2: an SVG root gets a new SVG `<title>` as its first
            // child (document.title-09.html); an HTML document gets one
            // appended to its head; any other root makes the setter return
            // before it looks for a title at all, so an XML document's title is
            // not settable even when an `html:title` sits somewhere in it
            // (document.title-not-in-html-svg.html).
            bool svg_root = false;
            {
                const auto txn = doc_->read();
                const node_id root = txn.root();
                svg_root = txn.element_ns(root) == node_ns::svg && txn.local_name(root) == "svg";
                if (!svg_root && txn.element_ns(root) != node_ns::html) {
                    return value::undefined();
                }
            }
            node_id title = title_element();
            if (!title) {
                const auto txn = doc_->read();
                const node_id root = txn.root();
                if (svg_root) {
                    const node_id made =
                        doc_->create_element(atoms_->intern("title"), node_ns::svg);
                    if (!made) { return value::undefined(); }
                    const std::span<const node_id> kids = txn.children(root);
                    (void)doc_->insert_before(root, made, kids.empty() ? node_id{} : kids.front());
                    title = made;
                } else {
                    const node_id head = first_html_element("head");
                    if (!head) { return value::undefined(); }
                    const node_id made = doc_->create_element(atoms_->intern_lower("title"));
                    if (!made) { return value::undefined(); }
                    (void)doc_->append_child(head, made);
                    title = made;
                }
            }
            set_text(title, wanted);
            return value::undefined();
        });

    // The eight collections. Each is one predicate over the HTML elements of
    // the document, live for the same reason `getElementsByTagName` is: a page
    // appends a form and reads `document.forms.length` again in the next
    // statement.
    //
    // `links` and `anchors` are the two that are not simply a tag: a link is
    // an `<a>` or an `<area>` that has an href, and an anchor is an `<a>` that
    // has a `name` (document.links.html builds both kinds and counts).
    // Each collection is [SameObject] (`document.embeds === document.embeds`,
    // document.embeds-document.plugins-01.html), so it is cached in
    // named_collections_ under a key that starts with NUL, which no element
    // name can contain.
    const auto collection = [&](std::string name, std::function<std::vector<node_id>()> members) {
        doc.define_accessor(name,
                            value::object(cx.allocate<script::native_object>(
                                name,
                                [this, name, members](context & c, std::span<value>) {
                                    // `plugins` "must return the same object as
                                    // embeds", HTML 3.1.5.
                                    const std::string key =
                                        '\0' + (name == "plugins" ? std::string{"embeds"} : name);
                                    if (const auto held = named_collections_.find(key);
                                        held != named_collections_.end()) {
                                        return held->second;
                                    }
                                    const value made = make_live_collection(c, members);
                                    named_collections_.emplace(key, made);
                                    return made;
                                })),
                            value::undefined());
    };
    const auto tagged = [this](std::string_view local) {
        return [this, local] { return all_html_elements(local); };
    };
    collection("images", tagged("img"));
    collection("forms", tagged("form"));
    collection("scripts", tagged("script"));
    // `embeds` and `plugins` are the same collection under two names, as the
    // specification says (document.embeds-document.plugins-01 compares their
    // lengths after an insertion).
    collection("embeds", tagged("embed"));
    collection("plugins", tagged("embed"));
    collection("links", [this] {
        // One walk, not two concatenated: `document.links` is in document
        // order, and an `<area>` inside a `<map>` can precede an `<a>`. Two tag
        // walks appended would put every `<a>` first.
        const auto txn = doc_->read();
        const atom href = atoms_->intern("href");
        std::vector<node_id> found;
        const auto walk = [&](auto && self, node_id at) -> void {
            if (const auto tag = txn.tag(at); tag.has_value() &&
                                              txn.element_ns(at) == node_ns::html &&
                                              txn.has_attribute(at, href)) {
                const std::string_view local = atoms_->text(*tag);
                if (local == "a" || local == "area") { found.push_back(at); }
            }
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        walk(walk, txn.root());
        return found;
    });
    collection("anchors", [this] {
        const atom name = atoms_->intern("name");
        std::vector<node_id> found;
        const auto txn = doc_->read();
        for (const node_id at : all_html_elements("a")) {
            if (txn.has_attribute(at, name)) { found.push_back(at); }
        }
        return found;
    });
    // `applets` is always empty. HTML kept the property and removed the
    // element, so an empty HTMLCollection is the whole specification for it.
    collection("applets", [] { return std::vector<node_id>{}; });
}

} // namespace ctbrowser::shell
