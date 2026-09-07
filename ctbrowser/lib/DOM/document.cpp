#include <ctbrowser/dom/document.hpp>

// document: the function bodies.
// The header says what these compute; this says how.

namespace ctbrowser {

namespace {

// The three namespaces "adjust foreign attributes" can put an attribute in.
// Spelled out rather than derived, for the reason bindings/document.cpp spells
// its four out: one wrong character makes the lookup miss on the valid case and
// nothing about the failure says so.
constexpr std::string_view xlink_namespace = "http://www.w3.org/1999/xlink";
constexpr std::string_view xml_namespace = "http://www.w3.org/XML/1998/namespace";
constexpr std::string_view xmlns_namespace = "http://www.w3.org/2000/xmlns/";

// Does this attribute answer to (namespace, local name)? THAT PAIR IS AN
// ATTRIBUTE'S IDENTITY in the DOM, and the prefix is deliberately no part of
// it: `foo:bar` and `quux:bar` in the same namespace are ONE attribute.
[[nodiscard]] bool attribute_is(const atom_table & atoms, const attribute & a, std::string_view ns,
                                std::string_view local) {
    return atoms.text(a.ns) == ns && attribute_local_name(atoms, a) == local;
}

} // namespace

read_txn::read_txn(const document & doc) noexcept : doc_(&doc), guard_(doc.domain_) {}

bool read_txn::contains(node_id id) const noexcept {
    return doc_->find(id) != nullptr;
}

std::expected<node_kind, dom_error> read_txn::kind(node_id id) const noexcept {
    const node * n = doc_->find(id);
    if (n == nullptr) { return std::unexpected{dom_error::no_such_node}; }
    return n->kind;
}

std::expected<atom, dom_error> read_txn::tag(node_id id) const noexcept {
    const node * n = doc_->find(id);
    if (n == nullptr) { return std::unexpected{dom_error::no_such_node}; }
    if (n->kind != node_kind::element) { return std::unexpected{dom_error::not_an_element}; }
    return n->tag;
}

node_ns read_txn::element_ns(node_id id) const noexcept {
    const node * n = doc_->find(id);
    // A missing node and a text node are both HTML as far as anyone asking
    // this can tell - there is no third answer that a caller could use.
    return n != nullptr ? n->ns : node_ns::html;
}

node_id read_txn::parent(node_id id) const noexcept {
    const node * n = doc_->find(id);
    return n != nullptr ? n->parent.load(std::memory_order_acquire) : node_id{};
}

std::span<const node_id> read_txn::children(node_id id) const noexcept {
    const node * n = doc_->find(id);
    if (n == nullptr) { return {}; }
    const child_list * cl = n->children.load(std::memory_order_acquire);
    return std::span<const node_id>{cl->items.data(), cl->items.size()};
}

std::span<const attribute> read_txn::attributes(node_id id) const noexcept {
    const node * n = doc_->find(id);
    if (n == nullptr) { return {}; }
    const attr_list * al = n->attributes.load(std::memory_order_acquire);
    return std::span<const attribute>{al->items.data(), al->items.size()};
}

std::string_view read_txn::text(node_id id) const noexcept {
    const node * n = doc_->find(id);
    if (n == nullptr) { return {}; }
    return n->text.load(std::memory_order_acquire)->value;
}

const attribute * read_txn::find_attribute(node_id id, atom name) const noexcept {
    // THE FIRST, not any: an element may hold two attributes with this
    // qualified name in different namespaces, and every one of getAttribute,
    // setAttribute and removeAttribute is defined on the first of them.
    for (const attribute & a : attributes(id)) {
        if (a.name == name) { return &a; }
    }
    return nullptr;
}

const attribute * read_txn::find_attribute_ns(node_id id, std::string_view ns,
                                              std::string_view local) const noexcept {
    const atom_table & atoms = doc_->atoms();
    for (const attribute & a : attributes(id)) {
        if (attribute_is(atoms, a, ns, local)) { return &a; }
    }
    return nullptr;
}

std::string_view read_txn::attribute_value(node_id id, atom name) const noexcept {
    const attribute * found = find_attribute(id, name);
    return found == nullptr ? std::string_view{} : std::string_view{found->value};
}

bool read_txn::has_attribute(node_id id, atom name) const noexcept {
    return find_attribute(id, name) != nullptr;
}

bool read_txn::has_attribute_ns(node_id id, std::string_view ns,
                                std::string_view local) const noexcept {
    return find_attribute_ns(id, ns, local) != nullptr;
}

node_id read_txn::root() const noexcept {
    return doc_->root_;
}

std::uint64_t read_txn::version() const noexcept {
    return doc_->version();
}

bool read_txn::is_ancestor_of(node_id ancestor, node_id descendant) const noexcept {
    for (node_id at = descendant; at; at = parent(at)) {
        if (at == ancestor) { return true; }
    }
    return false;
}

document::document(atom_table & atoms) : atoms_(&atoms) {
    root_ = nodes_.insert(node_kind::document);
}

document::~document() = default;

node_id document::create_element(atom tag, node_ns ns) {
    return nodes_.insert(node_kind::element, tag, ns);
}

node_id document::create_text(std::string_view value) {
    const node_id id = nodes_.insert(node_kind::text);
    node * n = find(id);
    n->text.store(new text_block{std::string{value}}, std::memory_order_release);
    return id;
}

node_id document::create_comment(std::string_view value) {
    const node_id id = nodes_.insert(node_kind::comment);
    node * n = find(id);
    n->text.store(new text_block{std::string{value}}, std::memory_order_release);
    return id;
}

node_id document::create_fragment() {
    return nodes_.insert(node_kind::document_fragment);
}

void document::detach_locked(node * child_node, node_id child) {
    const node_id old_parent = child_node->parent.load(std::memory_order_acquire);
    if (!old_parent) { return; }
    node * parent_node = find(old_parent);
    if (parent_node == nullptr) { return; }
    const child_list * stale = parent_node->children.load(std::memory_order_acquire);
    auto * fresh = new child_list{stale->items};
    // std::erase/erase_if only overload for std containers, not Boost's
    const auto gone = std::ranges::remove(fresh->items, child);
    fresh->items.erase(gone.begin(), gone.end());
    publish(parent_node->children, static_cast<const child_list *>(fresh));
    child_node->parent.store(node_id{}, std::memory_order_release);
}

std::expected<void, dom_error> document::append_child(node_id parent, node_id child) {
    const std::lock_guard structure{structure_};
    node * parent_node = find(parent);
    node * child_node = find(child);
    if (parent_node == nullptr || child_node == nullptr) {
        return std::unexpected{dom_error::no_such_node};
    }
    // Cycle check. Trivially correct BECAUSE shape changes are serialized:
    // no other thread can be rewriting this ancestor path while we walk it.
    for (node_id at = parent; at; at = find(at)->parent.load(std::memory_order_acquire)) {
        if (at == child) { return std::unexpected{dom_error::would_cycle}; }
    }
    detach_locked(child_node, child);

    const child_list * stale = parent_node->children.load(std::memory_order_acquire);
    auto * fresh = new child_list{stale->items};
    fresh->items.push_back(child);
    child_node->parent.store(parent, std::memory_order_release);
    publish(parent_node->children, static_cast<const child_list *>(fresh));
    bump_version();
    return {};
}

std::expected<void, dom_error> document::insert_before(node_id parent, node_id child,
                                                       node_id before) {
    const std::lock_guard structure{structure_};
    node * parent_node = find(parent);
    node * child_node = find(child);
    if (parent_node == nullptr || child_node == nullptr) {
        return std::unexpected{dom_error::no_such_node};
    }
    for (node_id at = parent; at; at = find(at)->parent.load(std::memory_order_acquire)) {
        if (at == child) { return std::unexpected{dom_error::would_cycle}; }
    }
    detach_locked(child_node, child);

    const child_list * stale = parent_node->children.load(std::memory_order_acquire);
    auto * fresh = new child_list{stale->items};
    const auto at = std::ranges::find(fresh->items, before);
    fresh->items.insert(at, child); // end() when `before` is absent: append
    child_node->parent.store(parent, std::memory_order_release);
    publish(parent_node->children, static_cast<const child_list *>(fresh));
    bump_version();
    return {};
}

std::expected<void, dom_error> document::remove_child(node_id child) {
    const std::lock_guard structure{structure_};
    node * child_node = find(child);
    if (child_node == nullptr) { return std::unexpected{dom_error::no_such_node}; }
    if (child == root_) { return std::unexpected{dom_error::is_root}; }
    detach_locked(child_node, child);
    bump_version();
    return {};
}

std::expected<void, dom_error> document::set_attribute(node_id id, atom name,
                                                       std::string_view value) {
    const std::lock_guard lock{stripe_of(id)};
    node * n = find(id);
    if (n == nullptr) { return std::unexpected{dom_error::no_such_node}; }
    if (n->kind != node_kind::element) { return std::unexpected{dom_error::not_an_element}; }

    const attr_list * stale = n->attributes.load(std::memory_order_acquire);
    auto * fresh = new attr_list{stale->items};
    const auto at =
        std::ranges::find_if(fresh->items, [name](const attribute & a) { return a.name == name; });
    if (at != fresh->items.end()) {
        // ONLY THE VALUE. The attribute found may be in a namespace and may
        // carry a prefix; neither is what a `setAttribute` was asked to change.
        at->value = std::string{value};
    } else {
        fresh->items.push_back(attribute{name, std::string{value}});
    }
    publish(n->attributes, static_cast<const attr_list *>(fresh));
    bump_version();
    return {};
}

std::expected<void, dom_error> document::set_attribute_ns(node_id id, atom ns, atom name,
                                                          std::string_view value) {
    const std::lock_guard lock{stripe_of(id)};
    node * n = find(id);
    if (n == nullptr) { return std::unexpected{dom_error::no_such_node}; }
    if (n->kind != node_kind::element) { return std::unexpected{dom_error::not_an_element}; }

    const attribute wanted{name, ns, std::string{}};
    const std::string_view local = attribute_local_name(*atoms_, wanted);
    const std::string_view uri = atoms_->text(ns);

    const attr_list * stale = n->attributes.load(std::memory_order_acquire);
    auto * fresh = new attr_list{stale->items};
    const auto at = std::ranges::find_if(
        fresh->items, [&](const attribute & a) { return attribute_is(*atoms_, a, uri, local); });
    if (at != fresh->items.end()) {
        at->value = std::string{value};
    } else {
        fresh->items.push_back(attribute{name, ns, std::string{value}});
    }
    publish(n->attributes, static_cast<const attr_list *>(fresh));
    bump_version();
    return {};
}

std::expected<void, dom_error> document::set_attribute(node_id id, const attribute & held) {
    return set_attribute_ns(id, held.ns, held.name, held.value);
}

std::expected<void, dom_error> document::remove_attribute(node_id id, atom name) {
    const std::lock_guard lock{stripe_of(id)};
    node * n = find(id);
    if (n == nullptr) { return std::unexpected{dom_error::no_such_node}; }
    const attr_list * stale = n->attributes.load(std::memory_order_acquire);
    const auto gone =
        std::ranges::find_if(stale->items, [name](const attribute & a) { return a.name == name; });
    // NOTHING TO DO, and saying so rather than republishing an identical block:
    // a `removeAttribute` for an attribute that is not there must not dirty the
    // document, or every one of them costs a restyle.
    if (gone == stale->items.end()) { return {}; }
    auto * fresh = new attr_list{stale->items};
    // THE FIRST ONE ONLY. `removeAttribute("x")` on an element holding `x` in
    // two namespaces removes one of them and the OTHER becomes the answer to
    // `getAttribute("x")` - which is what Element-removeAttribute.html's two
    // subtests check, in both orders.
    fresh->items.erase(fresh->items.begin() + std::distance(stale->items.begin(), gone));
    publish(n->attributes, static_cast<const attr_list *>(fresh));
    bump_version();
    return {};
}

std::expected<void, dom_error> document::remove_attribute_ns(node_id id, std::string_view ns,
                                                             std::string_view local) {
    const std::lock_guard lock{stripe_of(id)};
    node * n = find(id);
    if (n == nullptr) { return std::unexpected{dom_error::no_such_node}; }
    const attr_list * stale = n->attributes.load(std::memory_order_acquire);
    const auto gone = std::ranges::find_if(
        stale->items, [&](const attribute & a) { return attribute_is(*atoms_, a, ns, local); });
    if (gone == stale->items.end()) { return {}; }
    auto * fresh = new attr_list{stale->items};
    fresh->items.erase(fresh->items.begin() + std::distance(stale->items.begin(), gone));
    publish(n->attributes, static_cast<const attr_list *>(fresh));
    bump_version();
    return {};
}

std::expected<void, dom_error> document::set_text(node_id id, std::string_view value) {
    const std::lock_guard lock{stripe_of(id)};
    node * n = find(id);
    if (n == nullptr) { return std::unexpected{dom_error::no_such_node}; }
    publish(n->text, static_cast<const text_block *>(new text_block{std::string{value}}));
    bump_version();
    return {};
}

std::size_t document::collect() {
    const std::size_t payloads = domain_.reclaim();
    return payloads + nodes_.collect();
}

void document::builder::append(node_id parent, node_id child) {
    node * parent_node = doc_->find(parent);
    node * child_node = doc_->find(child);
    if (parent_node == nullptr || child_node == nullptr) { return; }
    const child_list * current = parent_node->children.load(std::memory_order_relaxed);
    if (current == &empty_children) {
        auto * fresh = new child_list{};
        fresh->items.push_back(child);
        parent_node->children.store(fresh, std::memory_order_relaxed);
    } else {
        // Safe ONLY because nothing can be reading this document yet - that is
        // the builder's entire contract, and why appending is O(1) here and
        // O(n) through the published path.
        const_cast<child_list *>(current)->items.push_back(child);
    }
    child_node->parent.store(parent, std::memory_order_relaxed);
}

void document::builder::reparent(node_id child, node_id new_parent) {
    node * child_node = doc_->find(child);
    if (child_node == nullptr) { return; }
    const node_id old_parent = child_node->parent.load(std::memory_order_relaxed);
    if (node * previous = doc_->find(old_parent); previous != nullptr) {
        const child_list * current = previous->children.load(std::memory_order_relaxed);
        if (current != &empty_children) {
            auto & items = const_cast<child_list *>(current)->items;
            items.erase(std::remove(items.begin(), items.end(), child), items.end());
        }
    }
    append(new_parent, child);
}

void document::builder::insert_before(node_id parent, node_id child, node_id before) {
    node * parent_node = doc_->find(parent);
    node * child_node = doc_->find(child);
    if (parent_node == nullptr || child_node == nullptr) { return; }
    const child_list * current = parent_node->children.load(std::memory_order_relaxed);
    if (current == &empty_children) {
        append(parent, child);
        return;
    }
    auto & items = const_cast<child_list *>(current)->items;
    const auto at = std::find(items.begin(), items.end(), before);
    if (at == items.end()) {
        append(parent, child);
        return;
    }
    items.insert(at, child);
    child_node->parent.store(parent, std::memory_order_relaxed);
}

atom document::foreign_namespace_of(node_ns element_ns, atom name) const {
    if (element_ns != node_ns::svg) { return atom{}; }
    const std::string_view text = atoms_->text(name);
    // `xmlns` ALONE is the one unprefixed name in the table, and the colon test
    // is what keeps every ordinary SVG attribute - `d`, `viewBox`, `fill` - to
    // a single find() before it returns.
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos) {
        return text == "xmlns" ? atoms_->intern(xmlns_namespace) : atom{};
    }
    const std::string_view prefix = text.substr(0, colon);
    if (prefix == "xlink") { return atoms_->intern(xlink_namespace); }
    if (prefix == "xml") { return atoms_->intern(xml_namespace); }
    if (prefix == "xmlns") { return atoms_->intern(xmlns_namespace); }
    return atom{};
}

void document::builder::set_attribute(node_id id, atom name, std::string_view value) {
    node * n = doc_->find(id);
    if (n == nullptr) { return; }
    attribute made{name, doc_->foreign_namespace_of(n->ns, name), std::string{value}};
    const attr_list * current = n->attributes.load(std::memory_order_relaxed);
    if (current == &empty_attributes) {
        auto * fresh = new attr_list{};
        fresh->items.push_back(std::move(made));
        n->attributes.store(fresh, std::memory_order_relaxed);
    } else {
        const_cast<attr_list *>(current)->items.push_back(std::move(made));
    }
}

} // namespace ctbrowser
