//===- tree_ops.cpp - DOM tree operations -----------------------*- C++ -*-===//
//
// dom_bindings - tree operations: clone, insert, adopt, innerHTML and
// outerHTML (parse and serialise), textContent, and the lookups by id,
// selector and tag.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {
// Sentinel passed as clone_node's `fallback` to mean "no fallback registry".
// nullptr means "use this document's registry" when the source is from another
// document, so it cannot also mean "none" in the recursion.
binding_detail::custom_element_registry no_fallback_registry;
} // namespace

/// Appends a copy of \p node (and its subtree, including template contents)
/// from another document's tree \p from under \p parent in this document, and
/// returns the copy. Used to move parsed fragments out of a scratch document.
node_id dom_bindings::copy_subtree(const read_txn & from, node_id node, node_id parent) {
    node_id made;
    // Handle every node kind the parser produces, so comments and PIs are not
    // copied as tagless elements.
    switch (from.kind(node).value_or(node_kind::element)) {
    case node_kind::text: made = doc_->create_text(from.text(node)); break;
    case node_kind::comment: made = doc_->create_comment(from.text(node)); break;
    case node_kind::cdata_section: made = doc_->create_cdata_section(from.text(node)); break;
    case node_kind::processing_instruction:
        made = doc_->create_processing_instruction(from.name(node), from.text(node));
        break;
    case node_kind::document_type:
        made =
            doc_->create_document_type(from.name(node), from.public_id(node), from.system_id(node));
        break;
    case node_kind::document:
    case node_kind::document_fragment:
    case node_kind::element:
        made = doc_->create_element(from.tag(node).value_or(atom{}), from.element_ns(node),
                                    from.prefixed(node));
        // The document-side namespace of an `other` element (MathML), and
        // each attribute whole, namespace included (see clone_node).
        if (const atom uri = from.element_namespace(node)) {
            doc_->set_element_namespace(made, uri);
        }
        for (const attribute & a : from.attributes(node)) { (void)doc_->set_attribute(made, a); }
        // A null registry the fragment parser gave it travels with it; the
        // rest of the registries are assign_parsed_registries' to decide.
        if (from.null_custom_element_registry(node)) {
            doc_->set_null_custom_element_registry(made);
        }
        break;
    }
    (void)doc_->append_child(parent, made);
    for (const node_id child : from.children(node)) { copy_subtree(from, child, made); }
    // A <template>'s contents travel with it (HTML 4.12.3): the parser put
    // them in a fragment beside the element, not under it, so without this a
    // `div.innerHTML = "<template>..."` would give a template with an empty
    // `.content`.
    if (const node_id contents = from.template_content(node)) {
        const node_id copied = doc_->create_fragment();
        doc_->set_template_content(made, copied);
        for (const node_id child : from.children(contents)) { copy_subtree(from, child, copied); }
    }
    return made;
}

// `owner` is the bindings the source node belongs to: this one when null,
// which is every call but `importNode`'s. It matters twice: the namespace an
// element was created in is in the owner's `namespaces_` and not the tree, and
// a `<template>`'s contents are in the owner's document and not under the
// element.
//
// Custom element registries (DOM 4.4 "clone a single node" step 2): an
// element's copy has the source's registry, or `fallback` when that is null
// (importNode's), and a global one becomes this document's; a clonable shadow
// root's copy likewise, without the fallback. A clone of template contents
// belongs to the inert document and has none; its root is marked, not every
// element (custom_elements.cpp, "which registry a node has").
node_id dom_bindings::clone_node(const read_txn & from, node_id source, bool deep,
                                 const dom_bindings * owner, custom_element_registry * fallback) {
    const dom_bindings & src = owner == nullptr ? *this : *owner;
    const bool same_document = &src == this;
    // importNode's fallback is the importing document's registry, and so is
    // an adoption's starting point (node_from corrects it after); the
    // recursion's "none at all" is `no_fallback_registry`.
    if (fallback == &no_fallback_registry) {
        fallback = nullptr;
    } else if (!same_document && fallback == nullptr) {
        fallback = document_registry();
    }
    // A clone of template contents belongs to the inert document when it is
    // cloneNode's (which has no fallback); importNode's copy is the importing
    // document's.
    const bool inert_source = [&] {
        if (!same_document || fallback != nullptr) { return false; }
        const node_id top = from.root_of_tree(source, true);
        return static_cast<bool>(src.doc_->template_of(top)) ||
               src.inert_roots_.contains(top.key());
    }();
    // Nothing to copy when every registry involved is a default: one document,
    // no scoped registry in the realm, and no exception recorded in it.
    const bool defaults_only = [&] {
        if (!same_document || !node_registries_.empty() || !shadow_registries_.empty() ||
            scoped_document_registry_ != nullptr || doc_->has_null_custom_element_registries()) {
            return false;
        }
        for (const auto & registry : primary().registries_) {
            if (registry->document == nullptr) { return false; }
        }
        return true;
    }();
    const auto effective_global = [this](custom_element_registry * registry) {
        custom_element_registry * mine = document_registry();
        return registry != nullptr && registry->document != nullptr && mine != nullptr &&
                       mine->document != nullptr
                   ? mine
                   : nullptr;
    };
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
    // A document node is copied as an element with the same tag: for a parsed
    // page this tree builder makes `<html>` the root, with nothing above it.
    case node_kind::document:
    case node_kind::element:
        made = doc_->create_element(from.tag(source).value_or(atom{}), from.element_ns(source),
                                    from.prefixed(source));
        // Copy its namespace too, which is not stored on the node: a clone of
        // an element createElementNS made must report the same namespaceURI,
        // and `element_ns` alone would give the wrong one.
        if (const auto it = src.namespaces_.find(source.key()); it != src.namespaces_.end()) {
            namespaces_.emplace(made.key(), it->second);
        }
        if (const atom uri = from.element_namespace(source)) {
            doc_->set_element_namespace(made, uri);
        }
        // Copy each attribute whole, namespace included, so a cloned
        // `xlink:href` keeps its namespace (Node-cloneNode-svg.html reads the
        // namespaceURI off the clone's attributes).
        for (const attribute & held : from.attributes(source)) {
            (void)doc_->set_attribute(made, held);
        }
        if (custom_element_registry * registry =
                defaults_only ? nullptr : src.registry_of_node(from, source);
            !defaults_only && (registry != nullptr || fallback != nullptr || !inert_source)) {
            if (registry == nullptr) { registry = fallback; }
            if (registry != nullptr && registry->document != nullptr) {
                registry = effective_global(registry);
            }
            set_node_registry(doc_->read(), made, registry);
        }
        // DOM 4.4 "clone a single node": the copy is created with the
        // source's is value (see custom_elements.cpp).
        if (const auto held = src.is_values_.find(source.key()); held != src.is_values_.end()) {
            is_values_.insert_or_assign(made.key(), held->second);
        }
        // HTML 4.12.1's cloning steps: the copy's "already started" flag is
        // the source's, so a script that never ran clones into one that will.
        if (std::ranges::find(src.unstarted_scripts_, source) != src.unstarted_scripts_.end()) {
            note_unstarted_script(made);
        }
        // The cloning steps of HTML 4.10.5 and 4.10.11: a control's value,
        // dirty value flag, checkedness and dirty checkedness go with it
        // (the-input-element/cloning-steps.html, clone.html).
        if (src.forms_ != nullptr && forms_ != nullptr) {
            if (src.forms_ == forms_) {
                forms_->clone_state(source, made);
            } else if (const control_state * held = src.forms_->find(source)) {
                const auto txn = doc_->read();
                control_state & copy = forms_->state_of(txn, *atoms_, made);
                copy = *held;
                copy.caret = 0;
                copy.selection = 0;
            }
        }
        break;
    }
    // A copy that lands under another copy is not a root: only the outermost
    // keeps the inert mark (see the note above).
    const auto adopt_copy = [&](node_id parent, node_id copy) {
        (void)doc_->append_child(parent, copy);
        if (inert_source) { inert_roots_.erase(copy.key()); }
    };
    if (deep) {
        for (const node_id child : from.children(source)) {
            // The same fallback all the way down: "none" stays none rather
            // than turning into the document's at the next level.
            adopt_copy(made, clone_node(from, child, true, owner,
                                        fallback == nullptr ? &no_fallback_registry : fallback));
        }
        // HTML 4.12.3, the cloning steps for a template: its contents clone
        // with it, into the copy's own contents fragment. They are not
        // children of the element, so the loop above never sees them, and
        // they belong to the inert document, so no fallback registry.
        if (const node_id contents = src.doc_->template_content(source)) {
            const node_id copied = doc_->create_fragment();
            doc_->set_template_content(made, copied);
            for (const node_id child : from.children(contents)) {
                adopt_copy(copied, clone_node(from, child, true, owner, &no_fallback_registry));
            }
        }
    }
    // DOM 4.4 "clone a node" step 6: a clonable shadow root goes with its
    // host, whole; even a shallow copy of the host gets every node of the
    // shadow tree (shadow-root-clonable.html). The metadata is copied out
    // first, because attaching to a host in the same document may move it.
    // Its registry is the source root's (a global one becomes this
    // document's), and its children are cloned without the fallback (step
    // 6.4's note).
    if (const node_id root = src.doc_->shadow_root_of(source)) {
        const document::shadow_tree * held = src.doc_->shadow_tree_of(root);
        // An adoption moves the shadow tree whatever its `clonable`: it is
        // the same host, in another document (node_from's copy is the move).
        if (held != nullptr && (held->clonable || adopting_)) {
            const document::shadow_tree how = *held;
            custom_element_registry * registry =
                defaults_only ? nullptr : src.registry_of_node(from, root);
            if (const auto mine = doc_->attach_shadow(made, how)) {
                if (!defaults_only) {
                    if (registry != nullptr && registry->document != nullptr) {
                        registry = effective_global(registry);
                    }
                    set_node_registry(doc_->read(), *mine, registry);
                }
                for (const node_id child : from.children(root)) {
                    adopt_copy(*mine, clone_node(from, child, true, owner, &no_fallback_registry));
                }
            }
        }
    }
    if (inert_source) { inert_roots_.emplace(made.key(), true); }
    return made;
}

/// Inserts \p child under \p parent before \p before (appends when null). A
/// DocumentFragment inserts its children instead. Runs mutated() once.
bool dom_bindings::insert_node(node_id parent, node_id child, node_id before) {
    if (!parent || !child) { return false; }
    bool fragment = false;
    std::vector<node_id> moving;
    {
        const auto txn = doc_->read();
        fragment = txn.kind(child).value_or(node_kind::element) == node_kind::document_fragment;
        if (fragment) {
            // Copy the list first: `children()` is a view onto the live child
            // list and every move rewrites it, so walking it while inserting
            // would read freed memory from the second child on.
            for (const node_id held : txn.children(child)) { moving.push_back(held); }
        }
    }
    if (!fragment) { moving.push_back(child); }
    if (!moving_) {
        const auto txn = doc_->read();
        for (const node_id one : moving) {
            if (txn.parent(one) || one == txn.root()) { moved_by_mutation_.push_back(one); }
        }
    }
    for (const node_id one : moving) {
        if (before) {
            (void)doc_->insert_before(parent, one, before);
        } else {
            (void)doc_->append_child(parent, one);
        }
    }
    // moveBefore's node, for the custom element scan this mutation runs: every
    // shadow-including descendant of it moved, custom or not.
    if (moving_) { move_root_ = child; }
    mutated();
    move_root_ = node_id{};
    return true;
}

// The node a value names: ours as it is, another document's adopted into this
// one, and anything else as a Text node holding its string, which is what
// "convert nodes into a node" says.
//
// Adoption, DOM 4.5 "adopt": the node leaves its parent, and it and every
// descendant become this document's. A node is a slot in one document's slab,
// so the move is a deep clone into this slab, the original's removal from its
// tree, and (what makes it an adoption rather than an import) the page's
// wrapper objects rebound to the copies: same JavaScript object, new handle,
// and the methods and views re-installed by these bindings so that no closure
// on it still reaches into the tree it left. What does not travel: event
// listeners and custom-element state, both keyed by the old handle in the old
// document's tables. ponytail: a shadow root is refused rather than moved, as
// importNode refuses it.
node_id dom_bindings::node_from(context & cx, value v, bool whole_fragment) {
    if (const node_id held = handle_of(v)) { return held; }
    dom_bindings * owner = owner_of(v);
    if (owner == nullptr || owner == this || is_a_document(v)) {
        return doc_->create_text(cx.to_string(v));
    }
    const node_id source = owner->handle_of(v);
    if (!source) { return node_id{}; }
    // A fragment stays where it was: inserting one moves its children, and
    // the fragment itself is never adopted. `df.ownerDocument` is still the
    // document that made it, now empty (DOM 4.2.3 "insert" steps 1-2), and a
    // shadow root stays with its host the same way (adoption.window.js,
    // "appendChild() and DocumentFragment" / "...and ShadowRoot"). The
    // children arrive in a fragment of this document so the caller's
    // flattening still applies.
    //
    // The old document's live ranges move first, as "remove" (which adopt
    // runs) moves them: a boundary inside what is leaving goes to (parent,
    // index) now, while the wrappers still name the old document's nodes;
    // once rebound below, that document no longer finds them
    // (Range-adopt-test.html). The parent's own offsets are updated by the
    // removal itself, as for any removal.
    {
        const auto txn = owner->doc_->read();
        std::vector<node_id> leaving;
        if (!whole_fragment &&
            txn.kind(source).value_or(node_kind::element) == node_kind::document_fragment) {
            const std::span<const node_id> kids = txn.children(source);
            leaving.assign(kids.begin(), kids.end());
        } else {
            leaving.push_back(source);
        }
        for (const node_id gone : leaving) {
            const node_id parent = dom_parent(txn, gone);
            if (!parent) { continue; }
            const std::span<const node_id> row = txn.children(parent);
            const auto index = static_cast<double>(std::ranges::find(row, gone) - row.begin());
            owner->each_live_boundary([&](const live_boundary & at) {
                for (node_id up = at.node; up && txn.contains(up); up = dom_parent(txn, up)) {
                    if (up == gone) {
                        owner->move_live_boundary(at, parent, index);
                        return;
                    }
                }
            });
        }
    }
    node_id made;
    std::vector<node_id> moved;
    {
        const auto from = owner->doc_->read();
        const bool fragment = !whole_fragment && from.kind(source).value_or(node_kind::element) ==
                                                     node_kind::document_fragment;
        const bool was_adopting = std::exchange(adopting_, true);
        made = fragment ? doc_->create_fragment()
                        : clone_node(from, source, true, owner, &no_fallback_registry);
        const auto rebind = [&](auto && self, node_id old, node_id fresh) -> void {
            if (const auto it = owner->wrappers_.find(old.key()); it != owner->wrappers_.end()) {
                script::object_object * obj = it->second;
                owner->wrappers_.erase(it);
                owner->adopted_away_[old.key()] = obj;
                obj->set(std::string{handle_property},
                         value::number(static_cast<double>(fresh.key())));
                wrappers_.emplace(fresh.key(), obj);
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
            // ...and its shadow tree, which went with it: the ShadowRoot a
            // page holds and every node in it are the moved ones too.
            const node_id old_root = owner->doc_->shadow_root_of(old);
            const node_id new_root = doc_->shadow_root_of(fresh);
            if (old_root && new_root) { self(self, old_root, new_root); }
        };
        if (fragment) {
            for (const node_id child : from.children(source)) {
                const node_id copy = clone_node(from, child, true, owner, &no_fallback_registry);
                (void)doc_->append_child(made, copy);
                rebind(rebind, child, copy);
                moved.push_back(child);
            }
        } else {
            rebind(rebind, source, made);
            moved.push_back(source);
        }
        adopting_ = was_adopting;
        // Adopt the registries as well, which an adoption changes and a clone
        // does not (custom_elements.cpp, adopt_registries): what was null or
        // global takes this document's, from its parent where it had one.
        const auto mine = doc_->read();
        if (fragment) {
            for (const node_id child : mine.children(made)) {
                adopt_registries(mine, child, nullptr, false);
            }
        } else {
            adopt_registries(mine, made, nullptr, false);
        }
    }
    for (const node_id gone : moved) { (void)owner->doc_->remove_child(gone); }
    owner->mutated();
    return made;
}

// Parse the markup; do not store it.
//
// A fragment goes through the same WHATWG tokenizer and tree builder as the
// page, rather than a second, weaker parser. `tree_builder::parse` replaces
// the document's root, so it runs against a scratch document; that document
// shares this one's atom table, so copying across needs no name remapping.
//
// It parses in the element's own context (HTML 13.2.9):
// `table.innerHTML = "<tr>"` is a row inside a tbody and
// `svg.innerHTML = "<circle/>"` an SVG circle. Parsed as a <body>'s children,
// the first would be nothing at all ("in body" ignores a <tr>) and the second
// an HTML unknown element.
//
// In an XML document it is the XML fragment parsing algorithm (HTML 13.4):
// the markup wrapped in an element that declares every namespace in scope at
// the context element (nearest declaration wins, and the context's own
// namespace is the default when nothing declares one), then parsed as XML.
// `context_ns` is that namespace URI. An ill-formed fragment returns the empty
// handle, and the caller throws SyntaxError and leaves the element as it was.
namespace {
[[nodiscard]] node_id parse_fragment_for(const document & doc, document & scratch, node_id context,
                                         std::string_view markup, std::string_view context_ns,
                                         bool scripting = true) {
    const auto txn = doc.read();
    const bool element = txn.kind(context).value_or(node_kind::element) == node_kind::element;
    if (!doc.xml()) {
        return parse_html_fragment(
            scratch, markup, element ? txn.local_name(context) : std::string_view{"body"},
            element ? txn.element_ns(context) : node_ns::html, doc.scripting() && scripting);
    }
    const atom_table & atoms = doc.atoms();
    const auto quoted = [](std::string_view value) {
        std::string out;
        for (const char c : value) {
            out += c == '&' ? "&amp;" : c == '"' ? "&quot;" : c == '<' ? "&lt;" : std::string{c};
        }
        return out;
    };
    std::string wrapper = "<x";
    std::vector<std::string_view> declared;
    bool has_default = false;
    for (node_id at = element ? context : node_id{}; at; at = txn.parent(at)) {
        for (const attribute & a : txn.attributes(at)) {
            // Match by name: the XML parser keeps an `xmlns` declaration as
            // the attribute it was written as, in no namespace.
            const std::string_view name = atoms.text(a.name);
            if (name != "xmlns" && !name.starts_with("xmlns:")) { continue; }
            if (std::ranges::find(declared, name) != declared.end()) { continue; }
            declared.push_back(name);
            has_default = has_default || name == "xmlns";
            wrapper += " " + std::string{name} + "=\"" + quoted(a.value) + "\"";
        }
    }
    if (!has_default && !context_ns.empty()) { wrapper += " xmlns=\"" + quoted(context_ns) + "\""; }
    wrapper += ">";
    wrapper += markup;
    wrapper += "</x>";
    const xml_parse_result read = parse_xml(scratch, wrapper);
    return read.error.empty() ? read.tree.root : node_id{};
}
} // namespace

namespace {
[[nodiscard]] bool is_html_template(const document & doc, node_id id) {
    const auto txn = doc.read();
    return txn.kind(id).value_or(node_kind::text) == node_kind::element &&
           txn.element_ns(id) == node_ns::html && txn.local_name(id) == "template";
}
} // namespace

/// Replaces the children of \p target (a template's contents, for a template)
/// with \p markup parsed in \p target's context; the innerHTML setter.
void dom_bindings::set_inner_html(node_id target, std::string_view markup) {
    if (!target || atoms_ == nullptr) { return; }
    // `template.innerHTML = markup` replaces the template's contents, not its
    // (always empty) child list: HTML 4.12.3, DOM Parsing's innerHTML setter.
    node_id into = target;
    if (is_html_template(*doc_, target)) {
        into = doc_->template_content(target);
        if (!into) {
            into = doc_->create_fragment();
            doc_->set_template_content(target, into);
        }
    }
    // A shadow root's context is its host (the ShadowRoot innerHTML setter):
    // the host's element name, and in XML the namespace declarations in scope
    // there (innerHTML-setter.xhtml's `<bar:span/>`).
    node_id context = target;
    if (const shadow_tree * tree = shadow_tree_of(target)) { context = tree->host; }
    document scratch{*atoms_};
    // A template's contents belong to its inert document, which has no
    // browsing context and so no scripting: a `<noscript>` there is markup.
    const node_id body =
        parse_fragment_for(*doc_, scratch, context, markup, namespace_of(context), into == target);
    if (!body) {
        // The XML fragment parsing algorithm (HTML 13.4) on markup that is
        // not well-formed: a SyntaxError, and the element is left as it was.
        if (doc_->xml() && cx_ != nullptr) {
            throw_dom_exception(*cx_, "SyntaxError",
                                "Failed to set 'innerHTML': the markup is not well-formed XML");
        }
        return;
    }
    {
        const auto txn = doc_->read();
        const std::span<const node_id> kids = txn.children(into);
        const std::vector<node_id> existing{kids.begin(), kids.end()};
        for (const node_id child : existing) { (void)doc_->remove_child(child); }
    }
    const auto from = scratch.read();
    for (const node_id child : from.children(body)) { copy_subtree(from, child, into); }
    {
        // Give what the parser made the context's registry (HTML 13.4: the
        // fragment's root is created with it, and each element with its
        // intended parent's). A template's target is its contents, a fragment,
        // whose registry is null (the innerHTML setter's step 2).
        const auto txn = doc_->read();
        custom_element_registry * inherited = registry_of_node(txn, target);
        if (into != target) { inherited = nullptr; }
        if (scratch.has_null_custom_element_registries() || inherited != document_registry()) {
            for (const node_id child : txn.children(into)) {
                assign_parsed_registries(txn, child, inherited);
            }
        }
    }
    upgrade_created_subtree(into); // [CEReactions] - see custom_elements.cpp
    mutated();
}

// Reading back produces markup from a serialiser rather than the original
// text: the DOM is the truth, and a page that appended a node after setting
// innerHTML expects to see it.
//
// HTML 13.3, "serializing HTML fragments": escape `&`, U+00A0, `<` and `>`
// everywhere, plus `"` in attribute values. Children of raw-text elements are
// written unescaped (see serializes_raw).
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
        } else if (c == '<') {
            out += "&lt;";
        } else if (c == '>') {
            out += "&gt;";
        } else {
            out += c;
        }
    }
    return out;
}
// Whether an element's children are written unescaped. `<noscript>`'s are only
// while scripting is enabled, since only then did the parser read them as text.
[[nodiscard]] bool serializes_raw(std::string_view tag, bool scripting) {
    for (const std::string_view raw :
         {"style", "script", "xmp", "iframe", "noembed", "noframes", "plaintext"}) {
        if (tag == raw) { return true; }
    }
    return tag == "noscript" && scripting;
}
// "Serializes as void": the void elements and four obsolete ones that never
// had children either. Their children are not written, and innerHTML on one
// is the empty string.
[[nodiscard]] bool serializes_as_void(std::string_view tag) {
    return ctbrowser::html::is_void_element(tag) || tag == "basefont" || tag == "bgsound" ||
           tag == "frame" || tag == "keygen";
}
// An attribute's serialised name, HTML 13.3: by namespace, not by the prefix
// it was set with. `setAttributeNS(XML, "abc:foo")` comes out as `xml:foo`,
// an `xmlns`-namespace attribute as `xmlns` or `xmlns:local`, an XLink one as
// `xlink:local`, and anything else by its qualified name.
[[nodiscard]] std::string attribute_serialized_name(const atom_table & atoms, const attribute & a) {
    const std::string_view ns = atoms.text(a.ns);
    const std::string_view local = attribute_local_name(atoms, a);
    if (ns.empty()) { return std::string{local}; }
    if (ns == xml_namespace) { return "xml:" + std::string{local}; }
    if (ns == xmlns_namespace) {
        return local == "xmlns" ? std::string{"xmlns"} : "xmlns:" + std::string{local};
    }
    if (ns == "http://www.w3.org/1999/xlink") { return "xlink:" + std::string{local}; }
    return std::string{atoms.text(a.name)};
}
} // namespace

std::string dom_bindings::inner_html(node_id target) const {
    return serialize_html(target, false);
}

std::string dom_bindings::outer_html(node_id target) const {
    return serialize_html(target, true);
}

// `outerHTML = markup`: the markup parsed as the parent's children would be,
// then swapped in for the element. A parentless element is left alone, and a
// Document parent is a NoModificationAllowedError (there is no context to
// parse in). One `mutated()` for the swap, so an observer sees a single
// childList record naming both the removed element and what replaced it
// (MutationObserver-inner-outer.html).
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
    const node_id body = parse_fragment_for(*doc_, scratch, parent, markup, namespace_of(parent));
    if (!body) { return; }
    const auto from = scratch.read();
    const node_id fragment = doc_->create_fragment();
    for (const node_id child : from.children(body)) { copy_subtree(from, child, fragment); }
    std::vector<node_id> moving;
    {
        const auto txn = doc_->read();
        for (const node_id held : txn.children(fragment)) { moving.push_back(held); }
        // The parent is the context, and its registry the fragment's (see
        // set_inner_html).
        custom_element_registry * inherited = registry_of_node(txn, parent);
        if (scratch.has_null_custom_element_registries() || inherited != document_registry()) {
            for (const node_id held : moving) { assign_parsed_registries(txn, held, inherited); }
        }
    }
    for (const node_id one : moving) { (void)doc_->insert_before(parent, one, target); }
    (void)doc_->remove_child(target);
    mutated();
}

// HTML 13.3, "serializing HTML fragments": the children of `target`, or with
// `outer` the node itself, as markup. In an XML document it is the XML
// serialisation instead (DOM Parsing 2.4, the innerHTML/outerHTML getters):
// `<a xmlns="http://www.w3.org/1999/xhtml"></a>`, empty elements self-closed.
std::string dom_bindings::serialize_html(node_id target, bool outer, bool serializable_shadow_roots,
                                         const std::vector<node_id> & shadow_roots) const {
    const auto txn = doc_->read();
    std::string out;
    if (doc_->xml()) {
        if (outer) { return serialize_xml(target, ""); }
        for (const node_id child : txn.children(target)) { out += serialize_xml(child, ""); }
        return out;
    }
    // "Scripting is enabled for the node", which decides how <noscript> is
    // written: false under template contents, whose inert document has none.
    const auto scripting_for = [&](node_id node) {
        return doc_->scripting() && !doc_->template_of(txn.root_of_tree(node, true));
    };
    // Skip the per-element shadow-root lookup entirely when nothing asked for
    // one, so `innerHTML`/`outerHTML` stay byte identical and pay nothing.
    const bool want_shadow = serializable_shadow_roots || !shadow_roots.empty();
    const auto in_list = [&](node_id root) {
        for (const node_id r : shadow_roots) {
            if (r == root) { return true; }
        }
        return false;
    };
    // An element's shadow root, written as `<template shadowrootmode>` plus the
    // root's children, before the host's own children (HTML fragment
    // serialisation's shadow step). `wr` is the child writer of whichever pass
    // is running (target vs. a descendant), so nested shadow trees recurse.
    const auto emit_shadow = [&](node_id node, auto && wr) {
        if (!want_shadow) { return; }
        const node_id root = shadow_root_of(node);
        if (!root) { return; }
        const shadow_tree * how = shadow_tree_of(root);
        if (how == nullptr) { return; }
        if (!(in_list(root) || (serializable_shadow_roots && how->serializable))) { return; }
        out += "<template shadowrootmode=\"";
        out += how->open ? "open" : "closed";
        out += "\"";
        // The specification's order, which is not the attributes' own: mode,
        // delegatesfocus, serializable, slotassignment (only when manual, since
        // named is the default), clonable. gethtml-ordering.html writes the
        // template attributes in every other order and expects this one back.
        if (how->delegates_focus) { out += " shadowrootdelegatesfocus=\"\""; }
        if (how->serializable) { out += " shadowrootserializable=\"\""; }
        if (how->manual_slots) { out += " shadowrootslotassignment=\"manual\""; }
        if (how->clonable) { out += " shadowrootclonable=\"\""; }
        // ...and `shadowrootcustomelementregistry` unless the root's registry
        // is what parsing the markup back would give it: null beside a null
        // document registry, or global beside a global one.
        const custom_element_registry * document_has = document_registry();
        const custom_element_registry * root_has = registry_of_node(txn, root);
        const auto global = [](const custom_element_registry * r) {
            return r != nullptr && r->document != nullptr;
        };
        if (!(document_has == nullptr && root_has == nullptr) &&
            !(global(document_has) && global(root_has))) {
            out += " shadowrootcustomelementregistry=\"\"";
        }
        out += ">";
        for (const node_id child : txn.children(root)) { wr(child); }
        out += "</template>";
    };
    // The children an element serialises: a <template>'s are its contents.
    const auto children_of = [&](node_id node) {
        if (const node_id contents = doc_->template_content(node)) {
            return txn.children(contents);
        }
        return txn.children(node);
    };
    const auto write = [&](auto && self, node_id node, bool raw) -> void {
        switch (txn.kind(node).value_or(node_kind::element)) {
        // A CDATA section in an HTML document is written as the Text node it
        // is by inheritance, escaped, since the `<![CDATA[` form would not
        // parse back as text here (serializing-cdata-in-html-document.html).
        case node_kind::text:
        case node_kind::cdata_section:
            out += raw ? std::string{txn.text(node)} : escape_html(txn.text(node), false);
            return;
        case node_kind::comment:
            out += "<!--";
            out += txn.text(node);
            out += "-->";
            return;
        // HTML 13.3, the fragment serialisation algorithm's two remaining
        // rows: a PI is `<?target data?>`, and a doctype is `<!DOCTYPE name>`
        // with no identifiers.
        case node_kind::processing_instruction:
            out += "<?";
            out += atoms_->text(txn.name(node));
            out += " ";
            out += txn.text(node);
            out += "?>";
            return;
        case node_kind::document_type:
            out += "<!DOCTYPE ";
            out += atoms_->text(txn.name(node));
            out += ">";
            return;
        case node_kind::document:
        case node_kind::document_fragment:
            for (const node_id child : txn.children(node)) { self(self, child, false); }
            return;
        case node_kind::element: break;
        }
        const std::string_view tag = atoms_->text(txn.tag(node).value_or(atom{}));
        const bool html = txn.element_ns(node) == node_ns::html;
        out += "<";
        out += tag;
        for (const attribute & a : txn.attributes(node)) {
            out += " ";
            out += attribute_serialized_name(*atoms_, a);
            out += "=\"";
            out += escape_html(a.value, true);
            out += "\"";
        }
        out += ">";
        if (html && serializes_as_void(tag)) { return; }
        emit_shadow(node, [&](node_id c) { self(self, c, false); });
        const bool raw_below =
            html && serializes_raw(tag, tag != "noscript" || scripting_for(node));
        for (const node_id child : children_of(node)) { self(self, child, raw_below); }
        out += "</";
        out += tag;
        out += ">";
    };
    if (outer) {
        write(write, target, false);
        return out;
    }
    // `innerHTML`/`getHTML` on the target: nothing for an element that
    // serialises as void, raw text under a raw-text one, and its own shadow
    // root before its children, exactly as a descendant host's would inside
    // `write`.
    const bool target_html = txn.kind(target).value_or(node_kind::text) == node_kind::element &&
                             txn.element_ns(target) == node_ns::html;
    const std::string_view target_tag = atoms_->text(txn.tag(target).value_or(atom{}));
    if (target_html && serializes_as_void(target_tag)) { return out; }
    const bool raw = target_html &&
                     serializes_raw(target_tag, target_tag != "noscript" || scripting_for(target));
    emit_shadow(target, [&](node_id c) { write(write, c, false); });
    for (const node_id child : children_of(target)) { write(write, child, raw); }
    return out;
}

// Every text node under the element, concatenated, which is what
// `textContent` is. A Text or Comment node's textContent is its own data
// (DOM 4.4: "the descendant text content" applies only to an element or
// fragment).
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

/// Returns the first element in tree order whose `id` is \p want, or the
/// empty handle.
node_id dom_bindings::find_by_id(const std::string & want) {
    // "If elementId is the empty string, return null" (DOM 4.2.6), so an
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

// `querySelector` / `querySelectorAll`, matched by the style engine's selector
// matcher. `matches` and `closest` are defined in terms of this function so
// that the three cannot disagree.
//
// The selector is parsed per call. A compiled_selector owns everything it
// holds (atoms and, for an attribute selector, a std::string), so nothing here
// points into the parse, and a selector string is a handful of tokens.
// ponytail: no parse cache; add one, with its lifetime question, when a
// profile asks for it.
//
// An unsupported selector matches nothing and does not throw.
// `parse_selector_text` reports a syntax error separately through its out
// parameter: `:state(foo)` is valid CSS this engine cannot answer, and a
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

// The engine `query` matches through: the browser's own when it has handed one
// over, so states and atoms are shared; otherwise one of our own, made once.
style::engine & dom_bindings::selector_engine() {
    if (selector_engine_) { return *selector_engine_; }
    if (!own_selector_engine_) { own_selector_engine_ = std::make_unique<style::engine>(*atoms_); }
    return *own_selector_engine_;
}

// DOM 4.5 "list of elements with qualified name". In an HTML document, an
// HTML-namespace element matches the ASCII-lowercased name and any other
// element matches exactly (the tokenizer preserves case in foreign content,
// so `<linearGradient>` is found only by that spelling). In an XML document
// every element matches exactly. Tests: Document-getElementsByTagName.html,
// Document-getElementsByTagName-xhtml.xhtml.
std::vector<node_id> dom_bindings::all_by_tag(std::string_view tag) {
    const auto txn = doc_->read();
    // "*" is every element, which is how a page asks for the whole document.
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
