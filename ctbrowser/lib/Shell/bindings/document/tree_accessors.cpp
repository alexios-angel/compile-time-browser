// dom_bindings - the HTML tree accessors: document.title, document.body and
// the eight collections.
//
// One of eight files carved out of a 3,071-line bindings/document.cpp on
// 2026-09-08 - which was itself one of six carved out of bindings.cpp on
// 2026-08-09. All are member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp; the name productions every one of
// them needs are in internal.hpp beside this. Nothing about the public header
// changed.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// ============================================================================
// THE HTML TREE ACCESSORS
// ============================================================================
//
// `document.title`, `document.body` and the eight collections HTML 3.1.5 hangs
// off the Document. All of them are ACCESSORS and none of them is a property
// refreshed on the tick, for one reason: every test in this group writes and
// then reads back in the same statement. `refresh_document` runs when the
// wrappers are pushed, which is at least a frame later, so a data property
// answers a read with the value from before the write that provoked it - and
// `document.title = "x"; assert_equals(document.title, "x")` is the entire
// shape of html/dom's nine title tests.
void dom_bindings::install_tree_accessors(context & cx, script::object_object & doc) {
    const auto accessor = [&](std::string name, script::native_fn read, script::native_fn write) {
        doc.define_accessor(
            name, value::object(cx.allocate<script::native_object>(name, std::move(read))),
            write == nullptr
                ? value::undefined()
                : value::object(cx.allocate<script::native_object>(name, std::move(write))));
    };

    // `document.documentElement` and `document.body`, BOTH RE-READ.
    //
    // They were written once at install, on the grounds that the node never
    // changes - and the node does not, but the WRAPPER's contents do.
    // `refresh_element` pushes `ownerDocument` onto a wrapper every time
    // `wrap()` is called, and the document object those two were wrapped
    // against did not exist yet at install time, so `document.body
    // .ownerDocument === document` was false for the life of the page. Calling
    // `wrap` on each read refreshes it, and it is also the only way `body` can
    // follow a page that replaces it.
    //
    // THE BODY IS NOT `find_by_tag("body")`. HTML says it is the first child of
    // the DOCUMENT ELEMENT that is a `body` or a `frameset`, which is why
    // `Document.body.html` builds a `<body>` inside a `<div>` and expects
    // `document.body` not to be it.
    // `documentElement` IS THE ROOT, not `find_by_tag("html")`. For a parsed
    // page the two are the same node - this tree builder makes `<html>` the
    // root - but `createDocument(null, "foo")` has a root called `foo` and no
    // `<html>` anywhere, and by tag name that document had no document element
    // at all. The root of a document that was never parsed is the Document
    // node itself, which is not an element, and that is what makes
    // `createDocument(null, "").documentElement === null` true.
    accessor(
        "documentElement",
        [this](context & c, std::span<value>) {
            const auto txn = doc_->read();
            const node_id root = txn.root();
            if (txn.kind(root).value_or(node_kind::document) != node_kind::element) {
                return value::null();
            }
            return wrap(c, root);
        },
        nullptr);
    accessor(
        "body", [this](context & c, std::span<value>) { return wrap(c, body_element()); },
        [this](context & c, std::span<value> a) {
            const node_id fresh = handle_of(arg(a, 0));
            std::string local;
            if (fresh) {
                const auto txn = doc_->read();
                const std::string_view qualified = atoms_->text(txn.tag(fresh).value_or(atom{}));
                const std::size_t colon = qualified.find(':');
                local = colon == std::string_view::npos ? std::string{qualified}
                                                        : std::string{qualified.substr(colon + 1)};
            }
            // "IF THE NEW VALUE IS NOT A body OR frameset ELEMENT, THROW A
            // HierarchyRequestError" - and null is not one either, which is what
            // `document.body = null` is for.
            if (local != "body" && local != "frameset") {
                throw_dom_exception(c, "HierarchyRequestError",
                                    "document.body must be a body or frameset element");
                return value::undefined();
            }
            const node_id existing = body_element();
            if (existing == fresh) { return value::undefined(); }
            const node_id root = first_html_element("html");
            if (!root) { return value::undefined(); }
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

    // `document.title`, HTML 4.2.2, both halves.
    //
    // THE GETTER STRIPS AND COLLAPSES and the setter does not: the DOM keeps
    // the bytes the page wrote and the IDL attribute reports them normalised,
    // so `document.title = "two  spaces"` reads back "two spaces" while the
    // text node still holds two.
    //
    // THE SETTER CAN DO NOTHING AT ALL, and that is not a failure. With no
    // title element and no head there is nowhere to put one - HTML says return
    // - so a page that removes its head and then assigns a title has a document
    // whose title is the empty string. `document.title-01.html` asserts exactly
    // that before it goes on to prove that a `<title>` appended to the BODY is
    // found.
    accessor(
        "title",
        [this](context & c, std::span<value>) {
            const node_id title = title_element();
            return c.string(title ? strip_and_collapse(text_content(title)) : std::string{});
        },
        [this](context & c, std::span<value> a) {
            const std::string wanted = arg_string(c, a, 0);
            node_id title = title_element();
            if (!title) {
                const auto txn = doc_->read();
                const node_id root = txn.root();
                const bool svg_root = txn.element_ns(root) == node_ns::svg;
                // AN SVG ROOT AND A ROOT THAT IS NEITHER BOTH DO NOTHING
                // HERE, for two different reasons. HTML says a non-HTML,
                // non-SVG root makes the setter return - an XML document's
                // title is not settable at all. An SVG root should get a new
                // SVG `<title>` prepended, and does not yet: this engine can
                // only be handed an SVG-rooted document by `createDocument`,
                // which is absent, so there is no way to reach the branch and
                // no way to test one written blind.
                if (svg_root || txn.element_ns(root) != node_ns::html) {
                    return value::undefined();
                }
                const node_id head = first_html_element("head");
                if (!head) { return value::undefined(); }
                const node_id made = doc_->create_element(atoms_->intern_lower("title"));
                if (!made) { return value::undefined(); }
                (void)doc_->append_child(head, made);
                title = made;
            }
            set_text(title, wanted);
            return value::undefined();
        });

    // THE EIGHT COLLECTIONS. Each is one predicate over the HTML elements of
    // the document, live for the same reason `getElementsByTagName` is - a page
    // appends a form and reads `document.forms.length` again in the next
    // statement.
    //
    // `links` and `anchors` are the two that are NOT simply a tag: a link is an
    // `<a>` or an `<area>` THAT HAS AN href, and an anchor is an `<a>` that has
    // a `name`. `document.links.html` builds both kinds and counts.
    const auto collection = [&](std::string name, std::function<std::vector<node_id>()> members) {
        doc.define_accessor(name,
                            value::object(cx.allocate<script::native_object>(
                                name,
                                [this, members](context & c, std::span<value>) {
                                    return make_live_collection(c, members);
                                })),
                            value::undefined());
    };
    const auto tagged = [this](std::string_view local) {
        return [this, local] { return all_html_elements(local); };
    };
    collection("images", tagged("img"));
    collection("forms", tagged("form"));
    collection("scripts", tagged("script"));
    // `embeds` and `plugins` are THE SAME COLLECTION under two names, which is
    // what the specification says and what document.embeds-document.plugins-01
    // asserts by comparing their lengths after an insertion.
    collection("embeds", tagged("embed"));
    collection("plugins", tagged("embed"));
    collection("links", [this] {
        // ONE WALK, not two concatenated: `document.links` is in document order
        // and an `<area>` inside a `<map>` can precede an `<a>` that follows
        // it. Two tag walks appended would put every `<a>` first, and a
        // collection out of order fails `assert_array_equals` before it fails
        // anything else.
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
    // `applets` IS ALWAYS EMPTY. HTML kept the property and removed the
    // element, so an empty HTMLCollection is the whole specification for it.
    collection("applets", [] { return std::vector<node_id>{}; });
}

} // namespace ctbrowser::shell
