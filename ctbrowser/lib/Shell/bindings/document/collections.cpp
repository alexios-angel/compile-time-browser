// dom_bindings - the element collections: by tag, class and name, the HTML
// element walks, and the live HTMLCollection they are handed back as.
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

// THE LOCAL NAME AND THE NAMESPACE, which is the pair HTML's own definitions
// are written in and the pair `find_by_tag` below cannot ask about.
//
// It matters twice over in this engine. The tokenizer preserves case inside
// foreign content on purpose - see CLAUDE.md - so an SVG `<title>` and an HTML
// `<title>` can intern to the same atom while an SVG `<clipPath>` and an HTML
// one do not, and `<svg><title>Chart</title></svg>` really did make
// `document.title` answer "Chart".
namespace {

// The five, and not `isspace`: the same set `dom_whitespace` further down
// names, spelled again here because that one is defined after its first use
// and one constant cannot be in two anonymous namespaces at once.
constexpr std::string_view ascii_whitespace = "\t\n\f\r ";

[[nodiscard]] std::string_view local_name_of(std::string_view qualified) {
    const std::size_t colon = qualified.find(':');
    return colon == std::string_view::npos ? qualified : qualified.substr(colon + 1);
}

} // namespace

node_id dom_bindings::first_html_element(std::string_view local) {
    const auto txn = doc_->read();
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (found) { return; }
        if (const auto tagged = txn.tag(at); tagged.has_value() &&
                                             txn.element_ns(at) == node_ns::html &&
                                             local_name_of(atoms_->text(*tagged)) == local) {
            found = at;
            return;
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

std::vector<node_id> dom_bindings::all_html_elements(std::string_view local) {
    const auto txn = doc_->read();
    std::vector<node_id> found;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (const auto tagged = txn.tag(at); tagged.has_value() &&
                                             txn.element_ns(at) == node_ns::html &&
                                             local_name_of(atoms_->text(*tagged)) == local) {
            found.push_back(at);
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

// "The body element", HTML 4.2.3: the FIRST CHILD of the document element that
// is a `body` or a `frameset`. Not the first `<body>` in the document - a
// `<body>` the parser has put inside a `<div>` is not the document's body, and
// `Document.body.html` asserts that by building one.
node_id dom_bindings::body_element() {
    const auto txn = doc_->read();
    const node_id root = txn.root();
    for (const node_id child : txn.children(root)) {
        if (txn.element_ns(child) != node_ns::html) { continue; }
        const auto tagged = txn.tag(child);
        if (!tagged.has_value()) { continue; }
        const std::string_view local = local_name_of(atoms_->text(*tagged));
        if (local == "body" || local == "frameset") { return child; }
    }
    return node_id{};
}

// "The title element", HTML 4.2.2 - and the SVG branch is not a curiosity. A
// document whose root is `<svg>` takes its title from that root's own first
// SVG `<title>` CHILD, not from any HTML title anywhere; every other document
// takes the first HTML title element in tree order, wherever it is. That
// "wherever" is what `document.title-01.html` is about: it removes the head,
// appends a `<title>` to the BODY, and expects the title to be that one.
node_id dom_bindings::title_element() {
    const auto txn = doc_->read();
    const node_id root = txn.root();
    const auto is_svg_root = [&] {
        if (txn.element_ns(root) != node_ns::svg) { return false; }
        const auto tagged = txn.tag(root);
        return tagged.has_value() && local_name_of(atoms_->text(*tagged)) == "svg";
    };
    if (is_svg_root()) {
        for (const node_id child : txn.children(root)) {
            if (txn.element_ns(child) != node_ns::svg) { continue; }
            const auto tagged = txn.tag(child);
            if (tagged.has_value() && local_name_of(atoms_->text(*tagged)) == "title") {
                return child;
            }
        }
        return node_id{};
    }
    // Outside the transaction would be tidier, but `first_html_element` opens
    // one of its own and a read nested inside another read is a shape nothing
    // else in these bindings has - so the walk is repeated here instead.
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (found) { return; }
        if (const auto tagged = txn.tag(at); tagged.has_value() &&
                                             txn.element_ns(at) == node_ns::html &&
                                             local_name_of(atoms_->text(*tagged)) == "title") {
            found = at;
            return;
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, root);
    return found;
}

// Infra's "strip and collapse ASCII whitespace": the five ASCII whitespace
// characters, not `isspace`, and a run of them becomes exactly one space.
// `document.title-03.html` writes "two\t\ttabs" and reads back "two tabs".
std::string dom_bindings::strip_and_collapse(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool pending = false;
    for (const char c : text) {
        if (ascii_whitespace.find(c) != std::string_view::npos) {
            pending = !out.empty();
            continue;
        }
        if (pending) { out.push_back(' '); }
        pending = false;
        out.push_back(c);
    }
    return out;
}

node_id dom_bindings::find_by_tag(std::string_view tag) {
    const auto txn = doc_->read();
    const atom want = atoms_->intern_lower(tag);
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (!found && txn.tag(at).value_or(atom{}) == want) { found = at; }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

namespace {

// ASCII whitespace, as the DOM defines it: TAB, LF, FF, CR and SPACE. Not
// `isspace`, which is locale-dependent and includes vertical tab.
constexpr std::string_view dom_whitespace = "\t\n\f\r ";

// A property key that is a whole non-negative integer and nothing else. "1x" is
// not index 1, and neither is " 1", "+1" or "1.0" - a collection has to say no
// to those or `hasOwnProperty` starts agreeing to keys nobody indexed.
[[nodiscard]] std::optional<std::size_t> whole_index(std::string_view key) {
    if (key.empty() || (key.size() > 1 && key.front() == '0')) { return std::nullopt; }
    std::size_t at = 0;
    const char * first = key.data();
    const char * last = first + key.size();
    const auto [stopped, failed] = std::from_chars(first, last, at);
    if (failed != std::errc{} || stopped != last) { return std::nullopt; }
    return at;
}

} // namespace

std::string dom_bindings::namespace_of(node_id id) const {
    if (const auto it = namespaces_.find(pack(id)); it != namespaces_.end()) { return it->second; }
    switch (doc_->read().element_ns(id)) {
    case node_ns::svg: return std::string{svg_namespace};
    case node_ns::html: return std::string{html_namespace};
    // An `other` element with no recorded URI cannot happen - the only thing
    // that makes one records it - but a stale handle resolves to `html` and
    // then to this, and the null namespace is the honest answer for a node that
    // is not there any more.
    case node_ns::other: break;
    }
    return {};
}

std::vector<std::string> dom_bindings::ordered_set(std::string_view text) {
    std::vector<std::string> out;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t start = text.find_first_not_of(dom_whitespace, at);
        if (start == std::string_view::npos) { break; }
        std::size_t end = text.find_first_of(dom_whitespace, start);
        if (end == std::string_view::npos) { end = text.size(); }
        std::string token{text.substr(start, end - start)};
        // AN ORDERED *SET*: "a a" asks for one class twice, and a duplicate in
        // the wanted list is a match requirement that is already satisfied.
        if (std::ranges::find(out, token) == out.end()) { out.push_back(std::move(token)); }
        at = end;
    }
    return out;
}

std::vector<node_id> dom_bindings::all_by_class(node_id root,
                                                const std::vector<std::string> & tokens) {
    if (tokens.empty()) { return {}; }
    const auto txn = doc_->read();
    const atom class_attribute = atoms_->intern("class");
    std::vector<node_id> found;
    const auto has_every = [&](node_id at) {
        // CASE-SENSITIVE, which is the standards-mode rule. A quirks-mode
        // document matches ASCII-case-insensitively; this engine does not carry
        // the document's mode past the tree builder yet, so the standards answer
        // is the one given - it is the right one for every document with a
        // doctype, which is every document a test suite writes on purpose.
        const std::vector<std::string> held = ordered_set(txn.attribute_value(at, class_attribute));
        return std::ranges::all_of(tokens, [&](const std::string & want) {
            return std::ranges::find(held, want) != held.end();
        });
    };
    const auto walk = [&](auto && self, node_id at, bool include) -> void {
        // ELEMENTS ONLY, and never the element the search started from: a search
        // rooted at an element looks at its DESCENDANTS.
        if (include && txn.tag(at).has_value() && has_every(at)) { found.push_back(at); }
        for (const node_id child : txn.children(at)) { self(self, child, true); }
    };
    // THE DOCUMENT'S ROOT IS THE <html> ELEMENT, not a Document node - this
    // tree builder makes `<html>` and calls set_root with it, and there is no
    // node above it. So a document-wide search must INCLUDE the root, or
    // `document.getElementsByClassName` silently cannot return the one element
    // that is most often given a class. An element-rooted search excludes it.
    walk(walk, root ? root : txn.root(), !root);
    return found;
}

std::vector<node_id> dom_bindings::all_by_name(std::string_view name) {
    const auto txn = doc_->read();
    const atom key = atoms_->intern("name");
    std::vector<node_id> found;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (txn.tag(at).has_value() && txn.element_ns(at) == node_ns::html &&
            txn.has_attribute(at, key) && txn.attribute_value(at, key) == name) {
            found.push_back(at);
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

// THE LIVE COLLECTION. Three things have to be true at once and only a proxy
// gets all three: `length` is recomputed on every read, `collection[i]` is
// recomputed on every read, and `collection.hasOwnProperty(i)` agrees with both
// - which is exactly what `assert_array_equals` checks before it compares a
// single element.
//
// `item` and `namedItem` are ordinary properties of the TARGET rather than
// answers the trap fabricates, so they keep their identity across reads and so
// the trap's fallback - an ordinary lookup on the target - finds them along with
// everything Object.prototype provides.
value dom_bindings::make_live_collection(context & cx,
                                         std::function<std::vector<node_id>()> members,
                                         std::string_view interface_name) {
    auto * target = static_cast<script::object_object *>(cx.make_object().as_heap());
    auto * handler = static_cast<script::object_object *>(cx.make_object().as_heap());
    // `children instanceof HTMLCollection` IS A SUBTEST, and it is the only
    // failing one in `ParentNode-children.html` and `Document-getElementsBy
    // ClassName.html` alike - both files check liveness by appending and
    // reading `length`, which already worked, and then ask what the thing IS.
    // The interface objects exist (install_dom_interfaces); nothing had linked
    // a collection to one.
    // The table is built lazily on the first `wrap()`, and a page can ask for a
    // collection before it has touched a single element - `document.images`
    // reaches here without wrapping anything. Without this the prototype was
    // `undefined` and `instanceof HTMLCollection` was false for the first
    // collection a page made and true for every one after it.
    ensure_dom_interfaces(cx);
    target->prototype = interface_prototype(interface_name);
    // Shared rather than copied into each trap: `members` walks the document, and
    // three copies of the same walk is three chances for them to disagree.
    const auto live = std::make_shared<std::function<std::vector<node_id>()>>(std::move(members));

    const auto native = [&](std::string name, script::native_fn fn) {
        return value::object(cx.allocate<script::native_object>(std::move(name), std::move(fn)));
    };
    target->set("item", native("item", [this, live](context & c, std::span<value> a) {
                    const std::vector<node_id> found = (*live)();
                    const double at = arg_number(a, 0);
                    if (at < 0 || at >= static_cast<double>(found.size())) { return value::null(); }
                    return wrap(c, found[static_cast<std::size_t>(at)]);
                }));
    target->set("namedItem", native("namedItem", [this, live](context & c, std::span<value> a) {
                    const std::string want = arg_string(c, a, 0);
                    const auto txn = doc_->read();
                    const atom id = atoms_->intern("id");
                    const atom name = atoms_->intern("name");
                    for (const node_id at : (*live)()) {
                        if (txn.attribute_value(at, id) == want ||
                            txn.attribute_value(at, name) == want) {
                            return wrap(c, at);
                        }
                    }
                    return value::null();
                }));

    handler->set("get", native("get", [this, live](context & c, std::span<value> args) {
                     if (args.size() < 2) { return value::undefined(); }
                     const std::string key = c.to_string(args[1]);
                     if (key == "length") {
                         return value::number(static_cast<double>((*live)().size()));
                     }
                     if (const std::optional<std::size_t> at = whole_index(key)) {
                         const std::vector<node_id> found = (*live)();
                         return *at < found.size() ? wrap(c, found[*at]) : value::undefined();
                     }
                     return c.lookup_property(args[0], key);
                 }));
    // AN INDEX IS READ-ONLY. `collection[0] = x` must not stick - an
    // HTMLCollection's indexed properties have no setter, so a non-strict
    // assignment is silently ignored and a strict one throws. Without a `set`
    // trap the proxy wrote straight through to the target, and the next read
    // came back with whatever the page had assigned instead of the element the
    // walk finds; `Document-getElementsByTagName.html` checks both modes.
    handler->set("set", native("set", [](context & c, std::span<value> args) {
                     if (args.size() < 3 || !args[0].is_object()) { return value::boolean(false); }
                     const std::string key = c.to_string(args[1]);
                     if (key == "length" || whole_index(key).has_value()) {
                         return value::boolean(false);
                     }
                     c.store_property(args[0], key, args[2]);
                     return value::boolean(true);
                 }));
    handler->set("has", native("has", [live](context & c, std::span<value> args) {
                     if (args.size() < 2) { return value::boolean(false); }
                     const std::string key = c.to_string(args[1]);
                     if (key == "length") { return value::boolean(true); }
                     if (const std::optional<std::size_t> at = whole_index(key)) {
                         return value::boolean(*at < (*live)().size());
                     }
                     return value::boolean(!c.lookup_property(args[0], key).is_undefined());
                 }));
    return value::object(
        cx.allocate<script::proxy_object>(value::object(target), value::object(handler)));
}

} // namespace ctbrowser::shell
