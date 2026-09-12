// dom_bindings - tree operations: clone, insert, innerHTML, textContent, and
// the lookups by id, selector and tag.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

node_id dom_bindings::copy_subtree(const read_txn & from, node_id node, node_id parent) {
    node_id made;
    if (from.kind(node).value_or(node_kind::element) == node_kind::text) {
        made = doc_->create_text(from.text(node));
    } else {
        made = doc_->create_element(from.tag(node).value_or(atom{}), from.element_ns(node),
                                    from.prefixed(node));
        // THE WHOLE ATTRIBUTE, namespace and all - see clone_node.
        for (const attribute & a : from.attributes(node)) { (void)doc_->set_attribute(made, a); }
    }
    (void)doc_->append_child(parent, made);
    for (const node_id child : from.children(node)) { copy_subtree(from, child, made); }
    return made;
}

// `owner` is the bindings the SOURCE node belongs to - this one when null,
// which is every call but `importNode`'s. It matters twice: the namespace an
// element was created in is in the owner's `namespaces_` and not the tree, and
// a `<template>`'s contents are in the owner's document and not under the
// element.
node_id dom_bindings::clone_node(const read_txn & from, node_id source, bool deep,
                                 const dom_bindings * owner) {
    const dom_bindings & src = owner == nullptr ? *this : *owner;
    node_id made;
    switch (from.kind(source).value_or(node_kind::element)) {
    case node_kind::text: made = doc_->create_text(from.text(source)); break;
    case node_kind::comment: made = doc_->create_comment(from.text(source)); break;
    case node_kind::cdata_section: made = doc_->create_cdata_section(from.text(source)); break;
    case node_kind::document_fragment: made = doc_->create_fragment(); break;
    case node_kind::document_type:
        made = doc_->create_document_type(from.name(source), from.public_id(source),
                                          from.system_id(source));
        break;
    case node_kind::processing_instruction:
        made = doc_->create_processing_instruction(from.name(source), from.text(source));
        break;
    // A document has no clone that means anything here - there is one document -
    // so it is treated as the element it actually is: this tree builder makes
    // `<html>` the root and nothing sits above it.
    case node_kind::document:
    case node_kind::element:
        made = doc_->create_element(from.tag(source).value_or(atom{}), from.element_ns(source),
                                    from.prefixed(source));
        // AND ITS NAMESPACE, which is not on the node: a clone of an element
        // createElementNS made must report the same namespaceURI, and reading
        // it off `element_ns` alone would answer for the wrong one.
        if (const auto it = src.namespaces_.find(pack(source)); it != src.namespaces_.end()) {
            namespaces_.emplace(pack(made), it->second);
        }
        // THE WHOLE ATTRIBUTE, namespace and all. Copying `(name, value)` put
        // a cloned `xlink:href` in no namespace, and `Node-cloneNode-svg.html`
        // reads the namespaceURI off the clone's attributes.
        for (const attribute & held : from.attributes(source)) {
            (void)doc_->set_attribute(made, held);
        }
        // HTML 4.12.1's cloning steps: the copy's `already started` is the
        // source's - a script that never ran clones into one that will.
        if (std::ranges::find(src.unstarted_scripts_, source) != src.unstarted_scripts_.end()) {
            note_unstarted_script(made);
        }
        break;
    }
    if (deep) {
        for (const node_id child : from.children(source)) {
            (void)doc_->append_child(made, clone_node(from, child, true, owner));
        }
        // HTML 4.12.3, the cloning steps for a template: its CONTENTS clone
        // with it, into the copy's own contents fragment. They are not
        // children of the element, so the loop above never sees them.
        if (const node_id contents = src.doc_->template_content(source)) {
            const node_id copied = doc_->create_fragment();
            doc_->set_template_content(made, copied);
            for (const node_id child : from.children(contents)) {
                (void)doc_->append_child(copied, clone_node(from, child, true, owner));
            }
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

// THE NODE A VALUE NAMES: ours as it is, another document's ADOPTED into this
// one, and anything else as a Text node holding its string - which is what
// "convert nodes into a node" says.
//
// ADOPTION, DOM 4.5 "adopt": the node leaves its parent, and it and every
// descendant become this document's. A node is a slot in ONE document's slab,
// so the move is a deep clone into this slab, the original's removal from its
// tree, and - the part that makes it an adoption rather than an import - the
// page's wrapper objects REBOUND to the copies: same JavaScript object, new
// handle, and the methods and views re-installed by THESE bindings so that no
// closure on it still reaches into the tree it left. That is exactly the
// hazard the old note on adoptNode named, and re-installing is the answer to
// it. What does not travel: event listeners and custom-element state, both
// keyed by the old handle in the old document's tables. ponytail: a shadow
// root is refused rather than moved, as importNode refuses it.
node_id dom_bindings::node_from(context & cx, value v) {
    if (const node_id held = handle_of(v)) { return held; }
    dom_bindings * owner = owner_of(v);
    if (owner == nullptr || owner == this || is_a_document(v)) {
        return doc_->create_text(cx.to_string(v));
    }
    const node_id source = owner->handle_of(v);
    if (!source || owner->shadow_tree_of(source) != nullptr) { return node_id{}; }
    node_id made;
    {
        const auto from = owner->doc_->read();
        made = clone_node(from, source, true, owner);
        const auto rebind = [&](auto && self, node_id old, node_id fresh) -> void {
            if (const auto it = owner->wrappers_.find(pack(old)); it != owner->wrappers_.end()) {
                script::object_object * obj = it->second;
                owner->wrappers_.erase(it);
                obj->set(std::string{handle_property},
                         value::number(static_cast<double>(pack(fresh))));
                wrappers_.emplace(pack(fresh), obj);
                install_element_methods(cx, *obj);
                install_element_views(cx, *obj, fresh);
                refresh_element(cx, *obj, fresh);
            }
            const std::span<const node_id> olds = from.children(old);
            std::vector<node_id> news;
            {
                const auto mine = doc_->read();
                const std::span<const node_id> made_kids = mine.children(fresh);
                news.assign(made_kids.begin(), made_kids.end());
            }
            for (std::size_t i = 0; i < olds.size() && i < news.size(); ++i) {
                self(self, olds[i], news[i]);
            }
        };
        rebind(rebind, source, made);
    }
    (void)owner->doc_->remove_child(source);
    owner->mutated();
    return made;
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
// HTML 13.2, "serializing HTML fragments". Text is escaped except inside the
// raw-text elements, attribute values escape `&`, `"` and U+00A0, and a comment
// is `<!--data-->` - it used to come out as `<></>`, an empty tag pair.
namespace {
[[nodiscard]] std::string escape_html(std::string_view text, bool attribute) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '&') {
            out += "&amp;";
        } else if (c == '\xc2' && i + 1 < text.size() && text[i + 1] == '\xa0') {
            out += "&nbsp;";
            ++i;
        } else if (attribute && c == '"') {
            out += "&quot;";
        } else if (!attribute && c == '<') {
            out += "&lt;";
        } else if (!attribute && c == '>') {
            out += "&gt;";
        } else {
            out += c;
        }
    }
    return out;
}
[[nodiscard]] bool serializes_raw(std::string_view tag) {
    for (const std::string_view raw :
         {"style", "script", "xmp", "iframe", "noembed", "noframes", "plaintext", "noscript"}) {
        if (tag == raw) { return true; }
    }
    return false;
}
} // namespace

std::string dom_bindings::inner_html(node_id target) const {
    return serialize_html(target, false);
}

std::string dom_bindings::outer_html(node_id target) const {
    return serialize_html(target, true);
}

// `outerHTML = markup`: the markup parsed as the PARENT's children would be,
// then swapped in for the element. A parentless element is left alone, and
// the Document as a parent is a NoModificationAllowedError - there is no
// context to parse in. ONE `mutated()` for the swap, so an observer sees a
// single childList record naming both the removed element and what replaced
// it, which is what MutationObserver-inner-outer.html asserts.
void dom_bindings::set_outer_html(context & cx, node_id target, std::string_view markup) {
    node_id parent;
    {
        const auto txn = doc_->read();
        parent = dom_parent(txn, target);
        if (!parent) { return; }
        if (txn.kind(parent).value_or(node_kind::element) == node_kind::document) {
            throw_dom_exception(cx, "NoModificationAllowedError",
                                "outerHTML: the element's parent is a Document");
            return;
        }
    }
    document scratch{*atoms_};
    (void)parse_html(scratch, markup);
    const auto from = scratch.read();
    const node_id fragment = doc_->create_fragment();
    node_id body{};
    const auto find_body = [&](auto && self, node_id at) -> void {
        if (!body && from.tag(at).value_or(atom{}) == atoms_->intern_lower("body")) { body = at; }
        for (const node_id child : from.children(at)) { self(self, child); }
    };
    find_body(find_body, from.root());
    if (body) {
        for (const node_id child : from.children(body)) { copy_subtree(from, child, fragment); }
    }
    std::vector<node_id> moving;
    {
        const auto txn = doc_->read();
        for (const node_id held : txn.children(fragment)) { moving.push_back(held); }
    }
    for (const node_id one : moving) { (void)doc_->insert_before(parent, one, target); }
    (void)doc_->remove_child(target);
    mutated();
}

// HTML 13.2, "serializing HTML fragments": the children of `target`, or with
// `outer` the node itself, as markup.
std::string dom_bindings::serialize_html(node_id target, bool outer) const {
    const auto txn = doc_->read();
    std::string out;
    const auto write = [&](auto && self, node_id node, bool raw) -> void {
        switch (txn.kind(node).value_or(node_kind::element)) {
        case node_kind::text:
            out += raw ? std::string{txn.text(node)} : escape_html(txn.text(node), false);
            return;
        case node_kind::comment:
            out += "<!--";
            out += txn.text(node);
            out += "-->";
            return;
        // HTML 13.3, the fragment serialisation algorithm's three remaining
        // rows: a PI is its target, a space and its data; a doctype is
        // `<!DOCTYPE name>` and nothing else - the identifiers are not written;
        // a CDATA section is its data between the brackets, unescaped.
        case node_kind::processing_instruction:
            out += "<?";
            out += atoms_->text(txn.name(node));
            out += " ";
            out += txn.text(node);
            out += ">";
            return;
        case node_kind::document_type:
            out += "<!DOCTYPE ";
            out += atoms_->text(txn.name(node));
            out += ">";
            return;
        case node_kind::cdata_section:
            out += "<![CDATA[";
            out += txn.text(node);
            out += "]]>";
            return;
        case node_kind::document:
        case node_kind::document_fragment:
            for (const node_id child : txn.children(node)) { self(self, child, false); }
            return;
        case node_kind::element: break;
        }
        const std::string_view tag = atoms_->text(txn.tag(node).value_or(atom{}));
        out += "<";
        out += tag;
        for (const attribute & a : txn.attributes(node)) {
            out += " ";
            out += atoms_->text(a.name);
            out += "=\"";
            out += escape_html(a.value, true);
            out += "\"";
        }
        out += ">";
        if (ctbrowser::html::is_void_element(tag)) { return; }
        const bool raw_below = txn.element_ns(node) == node_ns::html && serializes_raw(tag);
        for (const node_id child : txn.children(node)) { self(self, child, raw_below); }
        out += "</";
        out += tag;
        out += ">";
    };
    if (outer) {
        write(write, target, false);
        return out;
    }
    for (const node_id child : txn.children(target)) { write(write, child, false); }
    return out;
}

// Every text node under the element, concatenated - which is what
// `textContent` is, and what makes it the safe way to read a label. A Text or
// Comment node's textContent is its own data (DOM 4.4, "the descendant text
// content" applies only to an element or fragment).
std::string dom_bindings::text_content(node_id target) const {
    const auto txn = doc_->read();
    const node_kind kind = txn.kind(target).value_or(node_kind::element);
    if (kind == node_kind::text || kind == node_kind::comment || kind == node_kind::cdata_section ||
        kind == node_kind::processing_instruction) {
        return std::string{txn.text(target)};
    }
    std::string out;
    const auto walk = [&](auto && self, node_id node) -> void {
        // Text and its subclass: a PI's data is not "descendant text content".
        if (is_text_kind(txn.kind(node).value_or(node_kind::element))) { out += txn.text(node); }
        for (const node_id child : txn.children(node)) { self(self, child); }
    };
    for (const node_id child : txn.children(target)) { walk(walk, child); }
    return out;
}

node_id dom_bindings::find_by_id(const std::string & want) {
    // "If elementId is the empty string, return null" - DOM 4.2.6, and an
    // element with `id=""` is not a match for it.
    if (want.empty()) { return node_id{}; }
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
// In an XML document EVERY element matches exactly - the fold is the HTML
// document's, not the HTML namespace's, and `Document-getElementsByTagName-
// xhtml.xhtml` puts an <I> in an XHTML page to say so.
std::vector<node_id> dom_bindings::all_by_tag(std::string_view tag) {
    const auto txn = doc_->read();
    // "*" is every ELEMENT, which is how a page asks for the whole document.
    const bool every = tag == "*";
    const atom folded = every ? atom{} : atoms_->intern_lower(tag);
    std::vector<node_id> found;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (const auto tagged = txn.tag(at); tagged.has_value()) {
            const bool folds = txn.element_ns(at) == node_ns::html && !doc_->xml();
            const bool matched =
                every || (folds ? *tagged == folded : atoms_->text(*tagged) == tag);
            if (matched) { found.push_back(at); }
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

} // namespace ctbrowser::shell
