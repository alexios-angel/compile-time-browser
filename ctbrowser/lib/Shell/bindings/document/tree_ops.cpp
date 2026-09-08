// dom_bindings - tree operations: clone, insert, innerHTML, textContent, and
// the lookups by id, selector and tag.
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

node_id dom_bindings::copy_subtree(const read_txn & from, node_id node, node_id parent) {
    node_id made;
    if (from.kind(node).value_or(node_kind::element) == node_kind::text) {
        made = doc_->create_text(from.text(node));
    } else {
        made = doc_->create_element(from.tag(node).value_or(atom{}), from.element_ns(node));
        for (const attribute & a : from.attributes(node)) {
            (void)doc_->set_attribute(made, a.name, a.value);
        }
    }
    (void)doc_->append_child(parent, made);
    for (const node_id child : from.children(node)) { copy_subtree(from, child, made); }
    return made;
}

node_id dom_bindings::clone_node(const read_txn & from, node_id source, bool deep) {
    node_id made;
    switch (from.kind(source).value_or(node_kind::element)) {
    case node_kind::text: made = doc_->create_text(from.text(source)); break;
    case node_kind::comment: made = doc_->create_comment(from.text(source)); break;
    case node_kind::document_fragment: made = doc_->create_fragment(); break;
    // A document has no clone that means anything here - there is one document -
    // so it is treated as the element it actually is: this tree builder makes
    // `<html>` the root and nothing sits above it.
    case node_kind::document:
    case node_kind::element:
        made = doc_->create_element(from.tag(source).value_or(atom{}), from.element_ns(source));
        // AND ITS NAMESPACE, which is not on the node: a clone of an element
        // createElementNS made must report the same namespaceURI, and reading
        // it off `element_ns` alone would answer for the wrong one.
        if (const auto it = namespaces_.find(pack(source)); it != namespaces_.end()) {
            namespaces_.emplace(pack(made), it->second);
        }
        for (const attribute & held : from.attributes(source)) {
            (void)doc_->set_attribute(made, held.name, held.value);
        }
        break;
    }
    if (deep) {
        for (const node_id child : from.children(source)) {
            (void)doc_->append_child(made, clone_node(from, child, true));
        }
    }
    return made;
}

bool dom_bindings::insert_node(node_id parent, node_id child, node_id before) {
    if (!parent || !child) { return false; }
    bool fragment = false;
    std::vector<node_id> moving;
    {
        const auto txn = doc_->read();
        fragment = txn.kind(child).value_or(node_kind::element) == node_kind::document_fragment;
        if (fragment) {
            // COPIED FIRST. `children()` is a view onto the live child list and
            // every move rewrites it, so walking it while inserting is a
            // use-after-free waiting for the second child.
            for (const node_id held : txn.children(child)) { moving.push_back(held); }
        }
    }
    if (!fragment) { moving.push_back(child); }
    for (const node_id one : moving) {
        if (before) {
            (void)doc_->insert_before(parent, one, before);
        } else {
            (void)doc_->append_child(parent, one);
        }
    }
    mutated();
    return true;
}

node_id dom_bindings::node_from(context & cx, value v) {
    if (const node_id held = handle_of(v)) { return held; }
    return doc_->create_text(cx.to_string(v));
}

// PARSE THE MARKUP, do not store it.
//
// A fragment goes through the same WHATWG tokenizer and tree builder the page
// did - the alternative is a second, worse parser for the commonest way a page
// builds content. `tree_builder::parse` replaces the document's root, so it
// runs against a SCRATCH document; that document shares this one's atom table,
// so copying across needs no name remapping.
void dom_bindings::set_inner_html(node_id target, std::string_view markup) {
    if (!target || atoms_ == nullptr) { return; }
    {
        const auto txn = doc_->read();
        const std::span<const node_id> kids = txn.children(target);
        const std::vector<node_id> existing{kids.begin(), kids.end()};
        for (const node_id child : existing) { (void)doc_->remove_child(child); }
    }
    document scratch{*atoms_};
    (void)parse_html(scratch, markup);
    const auto from = scratch.read();
    // The builder always makes html/body; the fragment's nodes are body's
    // children. Anything that landed in head - a <style>, a <title> - is not
    // what `el.innerHTML = ...` means and is left behind.
    node_id body{};
    const auto find_body = [&](auto && self, node_id at) -> void {
        if (!body && from.tag(at).value_or(atom{}) == atoms_->intern_lower("body")) { body = at; }
        for (const node_id child : from.children(at)) { self(self, child); }
    };
    find_body(find_body, from.root());
    if (!body) { return; }
    for (const node_id child : from.children(body)) { copy_subtree(from, child, target); }
    mutated();
}

// Read back as markup. A serialiser rather than the original text: the DOM is
// the truth, and a page that appended a node after setting innerHTML expects to
// see it.
std::string dom_bindings::inner_html(node_id target) const {
    const auto txn = doc_->read();
    std::string out;
    const auto write = [&](auto && self, node_id node) -> void {
        if (txn.kind(node).value_or(node_kind::element) == node_kind::text) {
            out += txn.text(node);
            return;
        }
        const std::string_view tag = atoms_->text(txn.tag(node).value_or(atom{}));
        out += "<";
        out += tag;
        for (const attribute & a : txn.attributes(node)) {
            out += " ";
            out += atoms_->text(a.name);
            out += "=\"";
            out += a.value;
            out += "\"";
        }
        out += ">";
        if (ctbrowser::html::is_void_element(tag)) { return; }
        for (const node_id child : txn.children(node)) { self(self, child); }
        out += "</";
        out += tag;
        out += ">";
    };
    for (const node_id child : txn.children(target)) { write(write, child); }
    return out;
}

// Every text node under the element, concatenated - which is what
// `textContent` is, and what makes it the safe way to read a label.
std::string dom_bindings::text_content(node_id target) const {
    const auto txn = doc_->read();
    std::string out;
    const auto walk = [&](auto && self, node_id node) -> void {
        if (txn.kind(node).value_or(node_kind::element) == node_kind::text) {
            out += txn.text(node);
        }
        for (const node_id child : txn.children(node)) { self(self, child); }
    };
    for (const node_id child : txn.children(target)) { walk(walk, child); }
    return out;
}

node_id dom_bindings::find_by_id(const std::string & want) {
    const auto txn = doc_->read();
    const atom key = atoms_->intern("id");
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (!found && txn.attribute_value(at, key) == want) { found = at; }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

// `querySelector` / `querySelectorAll`, ON THE REAL SELECTORS ENGINE.
//
// This used to be a hand-rolled compound matcher that gave up on any selector
// containing a space or a `>` - its own comment said "a combinator: not
// supported, matches nothing" - while `lib/Style/css/selector.cpp` and
// `style::engine` had a full one that combinators, attribute selectors, `:not`,
// `:is` and the sibling forms all went through. Two matchers, and the weaker one
// was the one a script reached. `matches` and `closest` are defined in terms of
// this function precisely so that the three cannot disagree, so all three moved
// together.
//
// The selector is PARSED PER CALL. A compiled_selector owns everything it holds -
// atoms and, for an attribute selector, a std::string - so nothing here points
// into the parse, and a selector string is a handful of tokens. Caching it is an
// optimisation with a lifetime question attached and there is no measurement
// asking for one yet.
//
// AN UNSUPPORTED SELECTOR MATCHES NOTHING AND DOES NOT THROW. `parse_selector_text`
// separates that from a syntax error, and the distinction is the whole reason it
// takes an out parameter: `:has(.x)` is valid CSS this engine cannot answer, and a
// SyntaxError for it would be a wrong answer rather than a missing one.
std::vector<node_id> dom_bindings::query(std::string_view selector, node_id within, bool * invalid,
                                         bool first_only) {
    bool bad = false;
    const style::css::stylesheet parsed = style::css::parse_selector_text(selector, *atoms_, bad);
    if (invalid) { *invalid = bad; }
    if (parsed.selectors.empty()) { return {}; }
    const auto txn = doc_->read();
    return selector_engine().select(txn, within, parsed.selectors, first_only);
}

// The engine `query` matches through. The browser's own when it has handed one
// over, so states and atoms are shared; otherwise one of our own, made once.
style::engine & dom_bindings::selector_engine() {
    if (selector_engine_) { return *selector_engine_; }
    if (!own_selector_engine_) { own_selector_engine_ = std::make_unique<style::engine>(*atoms_); }
    return *own_selector_engine_;
}

// "LIST OF ELEMENTS WITH QUALIFIED NAME qualifiedName", DOM 4.5 - and the rule
// has TWO branches in an HTML document, which is the half that was missing.
//
// An HTML-namespace element matches the name ASCII-LOWERCASED; an element in
// any other namespace matches it EXACTLY. That is not a nicety: the tokenizer
// preserves case inside foreign content on purpose, so `<linearGradient>` in an
// `<svg>` interns as written - and lowercasing the search made it unfindable by
// either spelling. `document.getElementsByTagName("linearGradient")` has to
// find it and `("lineargradient")` must not, which is exactly the six "Element
// in non-HTML namespace" subtests of `Document-getElementsByTagName.html`.
std::vector<node_id> dom_bindings::all_by_tag(std::string_view tag) {
    const auto txn = doc_->read();
    // "*" is every ELEMENT, which is how a page asks for the whole document.
    const bool every = tag == "*";
    const atom folded = every ? atom{} : atoms_->intern_lower(tag);
    std::vector<node_id> found;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (const auto tagged = txn.tag(at); tagged.has_value()) {
            const bool matched =
                every || (txn.element_ns(at) == node_ns::html ? *tagged == folded
                                                              : atoms_->text(*tagged) == tag);
            if (matched) { found.push_back(at); }
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

} // namespace ctbrowser::shell
