#include <ctbrowser/dom/document.hpp>

#include <ctbrowser/core/algorithms.hpp>

#include <optional>

// document: the function bodies.
// The header says what these compute; this says how.

namespace ctbrowser {

namespace {

// The three namespaces "adjust foreign attributes" can put an attribute in.
// Spelled out rather than derived, for the reason bindings/document/internal.hpp spells
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

// --- A PROCESSING INSTRUCTION'S ATTRIBUTES ---------------------------------
//
// DOM §4.13 gives a ProcessingInstruction an attribute map beside its data, and
// the two are kept in step in BOTH directions: writing `data` re-parses it into
// the map ("update attributes from data"), and the attribute operations
// re-serialise the map into `data` ("update data from attributes"). The map is
// STORED - on the node's ordinary attribute list, which every kind carries -
// rather than derived on each read, because the two directions are not
// inverses: `setAttribute("$", v)` is legal (a valid attribute local name) and
// writes `$="v"`, which the parser then REFUSES (`$` is no XML Name) - so a map
// derived from the data would lose the attribute the page just set.
//
// THE PARSE is xml-stylesheet §3, "rules for parsing pseudo-attributes from a
// string": `Name S? '=' S? quoted`, separated by S, with the five predefined
// entities and character references decoded and nothing else allowed after an
// `&`; an unquoted value, a `<`, a duplicate name or a stray character is an
// ERROR and the whole result is empty - the spec's "if result is an error,
// return" after clearing the map. Name is checked the way lib/DOM/xml.cpp
// checks it: the ASCII half exactly, everything above 0x7F accepted.

[[nodiscard]] bool pi_name_start(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':' || c >= 0x80;
}
[[nodiscard]] bool pi_name_char(unsigned char c) {
    return pi_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}
// XML's S production: space, tab, CR, LF - not form feed, which HTML's set has.
[[nodiscard]] bool pi_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

[[nodiscard]] std::optional<std::vector<attribute>> parse_pseudo_attributes(atom_table & atoms,
                                                                            std::string_view data) {
    std::vector<attribute> out;
    std::size_t at = 0;
    const auto skip_space = [&] {
        const std::size_t before = at;
        while (at < data.size() && pi_space(data[at])) { ++at; }
        return at != before;
    };
    // `&#...;`, `&#x...;` or one of the five names, with `at` on the `&`.
    const auto reference = [&](std::string & into) {
        const std::size_t end = data.find(';', at);
        if (end == std::string_view::npos) { return false; }
        const std::string_view body = data.substr(at + 1, end - at - 1);
        at = end + 1;
        for (const auto & [name, text] :
             {std::pair{"amp", '&'}, std::pair{"lt", '<'}, std::pair{"gt", '>'},
              std::pair{"quot", '"'}, std::pair{"apos", '\''}}) {
            if (body == name) {
                into += text;
                return true;
            }
        }
        if (body.size() < 2 || body[0] != '#') { return false; }
        const bool hex = body[1] == 'x';
        const std::string_view digits = body.substr(hex ? 2 : 1);
        if (digits.empty()) { return false; }
        std::uint32_t code = 0;
        for (const char d : digits) {
            const int v = hex ? hex_value(d) : (d >= '0' && d <= '9' ? d - '0' : -1);
            if (v < 0 || code > 0x10FFFF) { return false; }
            code = code * (hex ? 16u : 10u) + static_cast<std::uint32_t>(v);
        }
        // XML's Char production - the well-formedness constraint on a CharRef.
        const bool legal = code == 0x9 || code == 0xA || code == 0xD ||
                           (code >= 0x20 && code <= 0xD7FF) || (code >= 0xE000 && code <= 0xFFFD) ||
                           (code >= 0x10000 && code <= 0x10FFFF);
        if (!legal) { return false; }
        append_utf8(into, static_cast<char32_t>(code));
        return true;
    };
    while (true) {
        const bool spaced = skip_space();
        if (at == data.size()) { return out; }
        // PseudoAtts ::= PseudoAtt? (S PseudoAtt)* S? - one S between two.
        if (!out.empty() && !spaced) { return std::nullopt; }
        const std::size_t name_begin = at;
        if (!pi_name_start(static_cast<unsigned char>(data[at]))) { return std::nullopt; }
        while (at < data.size() && pi_name_char(static_cast<unsigned char>(data[at]))) { ++at; }
        const std::string_view name = data.substr(name_begin, at - name_begin);
        (void)skip_space();
        if (at >= data.size() || data[at] != '=') { return std::nullopt; }
        ++at;
        (void)skip_space();
        if (at >= data.size() || (data[at] != '"' && data[at] != '\'')) { return std::nullopt; }
        const char quote = data[at++];
        std::string value;
        while (true) {
            if (at >= data.size()) { return std::nullopt; }
            const char c = data[at];
            if (c == quote) {
                ++at;
                break;
            }
            if (c == '<') { return std::nullopt; }
            if (c == '&') {
                if (!reference(value)) { return std::nullopt; }
                continue;
            }
            value += c;
            ++at;
        }
        const atom key = atoms.intern(name);
        if (std::ranges::any_of(out, [key](const attribute & a) { return a.name == key; })) {
            return std::nullopt;
        }
        out.emplace_back(key, std::move(value));
    }
}

// "Update data from attributes": `name="value"` pairs one space apart, the
// value with `&`, `<`, `>` and `"` as their entities - which is what makes
// every serialisation parse back to the map it came from.
[[nodiscard]] std::string serialize_pseudo_attributes(const atom_table & atoms,
                                                      std::span<const attribute> attributes) {
    std::string data;
    for (const attribute & a : attributes) {
        if (!data.empty()) { data += ' '; }
        data += atoms.text(a.name);
        data += "=\"";
        for (const char c : a.value) {
            switch (c) {
            case '&': data += "&amp;"; break;
            case '<': data += "&lt;"; break;
            case '>': data += "&gt;"; break;
            case '"': data += "&quot;"; break;
            default: data += c; break;
            }
        }
        data += '"';
    }
    return data;
}

} // namespace

read_txn::read_txn(const document & doc) noexcept : doc_(&doc) {}

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

atom read_txn::name(node_id id) const noexcept {
    const node * n = doc_->find(id);
    if (n == nullptr) { return atom{}; }
    switch (n->kind) {
    case node_kind::element:
    case node_kind::document_type:
    case node_kind::processing_instruction: return n->tag;
    case node_kind::document:
    case node_kind::text:
    case node_kind::comment:
    case node_kind::document_fragment:
    case node_kind::cdata_section: return atom{};
    }
    return atom{};
}

// The two identifiers share the doctype's text block, NUL between them - the
// one byte neither can carry, because the tokenizer never emits it and
// `createDocumentType` is handed DOMStrings that a page has no reason to put
// one in. ponytail: two strings in one block; a second text slot on `node`
// would cost every node 8 bytes for the one kind that has two.
std::string_view read_txn::public_id(node_id id) const noexcept {
    const node * n = doc_->find(id);
    if (n == nullptr || n->kind != node_kind::document_type) { return {}; }
    const std::string_view both = n->text.load(std::memory_order_acquire)->value;
    return both.substr(0, both.find('\0'));
}

std::string_view read_txn::system_id(node_id id) const noexcept {
    const node * n = doc_->find(id);
    if (n == nullptr || n->kind != node_kind::document_type) { return {}; }
    const std::string_view both = n->text.load(std::memory_order_acquire)->value;
    const std::size_t nul = both.find('\0');
    return nul == std::string_view::npos ? std::string_view{} : both.substr(nul + 1);
}

node_ns read_txn::element_ns(node_id id) const noexcept {
    const node * n = doc_->find(id);
    // A missing node and a text node are both HTML as far as anyone asking
    // this can tell - there is no third answer that a caller could use.
    return n != nullptr ? n->ns : node_ns::html;
}

bool read_txn::prefixed(node_id id) const noexcept {
    const node * n = doc_->find(id);
    return n != nullptr && n->kind == node_kind::element && n->prefixed;
}

std::string_view read_txn::local_name(node_id id) const noexcept {
    const std::string_view qualified = doc_->atoms().text(tag(id).value_or(atom{}));
    if (!prefixed(id)) { return qualified; }
    const std::size_t colon = qualified.find(':');
    return colon == std::string_view::npos ? qualified : qualified.substr(colon + 1);
}

std::string_view read_txn::prefix(node_id id) const noexcept {
    if (!prefixed(id)) { return {}; }
    const std::string_view qualified = doc_->atoms().text(tag(id).value_or(atom{}));
    const std::size_t colon = qualified.find(':');
    return colon == std::string_view::npos ? std::string_view{} : qualified.substr(0, colon);
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

node_id read_txn::document_node() const noexcept {
    return doc_->document_node_;
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
    document_node_ = nodes_.insert(node_kind::document);
    root_ = document_node_;
}

document::~document() = default;

node_id document::create_element(atom tag, node_ns ns, bool prefixed) {
    return nodes_.insert(node_kind::element, tag, ns, prefixed);
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

node_id document::create_document_type(atom name, std::string_view public_id,
                                       std::string_view system_id) {
    const node_id id = nodes_.insert(node_kind::document_type, name);
    std::string both{public_id};
    both += '\0';
    both += system_id;
    find(id)->text.store(new text_block{std::move(both)}, std::memory_order_release);
    return id;
}

node_id document::create_processing_instruction(atom target, std::string_view data) {
    const node_id id = nodes_.insert(node_kind::processing_instruction, target);
    node * n = find(id);
    n->text.store(new text_block{std::string{data}}, std::memory_order_release);
    update_pi_attributes(*n, data);
    return id;
}

// "Update attributes from data" - the map is cleared first, so a data string
// the parser refuses leaves NO attributes rather than the previous ones.
void document::update_pi_attributes(node & n, std::string_view data) {
    auto * fresh = new attr_list{};
    if (std::optional<std::vector<attribute>> parsed = parse_pseudo_attributes(*atoms_, data)) {
        fresh->items.assign(parsed->begin(), parsed->end());
    }
    publish(n.attributes, static_cast<const attr_list *>(fresh));
}

// "Update data from attributes", after an attribute write on a PI. Straight
// into the text block - NOT through set_text, which would re-parse the data
// and drop an attribute whose name the parser refuses - and noted as a text
// write, because the record a MutationObserver gets for it is characterData.
void document::update_pi_data(node_id id, node & n) {
    if (n.kind != node_kind::processing_instruction) { return; }
    const attr_list * held = n.attributes.load(std::memory_order_acquire);
    publish(n.text, static_cast<const text_block *>(
                        new text_block{serialize_pseudo_attributes(*atoms_, held->items)}));
    note_write(id, atom{}, true);
}

node_id document::create_cdata_section(std::string_view value) {
    const node_id id = nodes_.insert(node_kind::cdata_section);
    find(id)->text.store(new text_block{std::string{value}}, std::memory_order_release);
    return id;
}

void document::set_document_element(node_id id, node_id before) {
    const std::lock_guard structure{structure_};
    const node_id previous = root_;
    root_ = id;
    node * doc_node = find(document_node_);
    if (doc_node == nullptr || id == document_node_ || id == previous) { return; }
    // Wherever it was before, it is a child of the Document now - and one with
    // NO parent pointer, see document_node.
    if (node * fresh_node = find(id); fresh_node != nullptr) { detach_locked(fresh_node, id); }
    const child_list * stale = doc_node->children.load(std::memory_order_acquire);
    auto * fresh = new child_list{stale->items};
    const auto gone = std::ranges::remove(fresh->items, id);
    fresh->items.erase(gone.begin(), gone.end());
    // The old element's slot if it had one; otherwise ahead of `before`, or at
    // the end.
    if (const auto slot = std::ranges::find(fresh->items, previous); slot != fresh->items.end()) {
        *slot = id;
    } else {
        fresh->items.insert(std::ranges::find(fresh->items, before), id);
    }
    publish(doc_node->children, static_cast<const child_list *>(fresh));
    bump_version();
}

void document::remove_document_element() {
    const std::lock_guard structure{structure_};
    if (root_ == document_node_) { return; }
    node * doc_node = find(document_node_);
    if (doc_node == nullptr) { return; }
    const child_list * stale = doc_node->children.load(std::memory_order_acquire);
    auto * fresh = new child_list{stale->items};
    const auto gone = std::ranges::remove(fresh->items, root_);
    fresh->items.erase(gone.begin(), gone.end());
    publish(doc_node->children, static_cast<const child_list *>(fresh));
    root_ = document_node_;
    bump_version();
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
    // "If child is node, set child to node's next sibling" - DOM 4.2.3. A node
    // inserted before ITSELF stays where it is; detaching first and then
    // looking for a reference that is no longer in the list appended it to the
    // end instead, which `Node-insertBefore.html` asserts by name.
    if (before == child) {
        const child_list * siblings = parent_node->children.load(std::memory_order_acquire);
        const auto self = std::ranges::find(siblings->items, child);
        before = self == siblings->items.end() || self + 1 == siblings->items.end() ? node_id{}
                                                                                    : *(self + 1);
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
    // An element, or a ProcessingInstruction - whose attribute map lives on
    // the same list and is written back into its data below.
    if (n->kind != node_kind::element && n->kind != node_kind::processing_instruction) {
        return std::unexpected{dom_error::not_an_element};
    }

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
    update_pi_data(id, *n);
    bump_version();
    note_write(id, name, false);
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
    note_write(id, name, false);
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
    update_pi_data(id, *n);
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
    // The fresh block's own copy of the text goes to the PI parser below, not
    // `value`: a caller may have passed this node's previous text, which the
    // publish just deleted.
    auto * fresh = new text_block{std::string{value}};
    publish(n->text, static_cast<const text_block *>(fresh));
    if (n->kind == node_kind::processing_instruction) { update_pi_attributes(*n, fresh->value); }
    bump_version();
    note_write(id, atom{}, true);
    return {};
}

void document::log_writes(bool on) {
    log_writes_.store(on, std::memory_order_release);
    if (!on) {
        const std::lock_guard lock{writes_};
        writes_log_.clear();
    }
}

std::vector<document::write_note> document::take_writes() {
    const std::lock_guard lock{writes_};
    return std::exchange(writes_log_, {});
}

void document::note_write(node_id id, atom name, bool text) {
    if (!log_writes_.load(std::memory_order_acquire)) { return; }
    const std::lock_guard lock{writes_};
    writes_log_.push_back(write_note{id, name, text});
}

node_id document::template_content(node_id element) const {
    const std::lock_guard lock{templates_};
    for (const auto & [held, fragment] : template_contents_) {
        if (held == element) { return fragment; }
    }
    return node_id{};
}

void document::set_template_content(node_id element, node_id fragment) {
    const std::lock_guard lock{templates_};
    for (auto & [held, current] : template_contents_) {
        if (held == element) {
            current = fragment;
            return;
        }
    }
    template_contents_.emplace_back(element, fragment);
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
    if (element_ns != node_ns::svg && element_ns != node_ns::mathml) { return atom{}; }
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
