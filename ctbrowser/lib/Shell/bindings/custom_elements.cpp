// dom_bindings - customElements: the CustomElementRegistry, the HTMLElement
// constructor a class extends, and the reactions.
//
// WHY THIS IS A SCAN, like the mutation observer beside it. The specification
// enqueues a reaction from inside every mutating step of the DOM; this engine
// has `mutated()`, which says THAT the document changed and not what. So a
// definition is applied by walking the tree: an element a definition covers
// and nothing has tracked yet is upgraded, and a tracked one is diffed against
// what it was - connected or not, under which parent, with which observed
// attributes. The reactions run before the native that mutated returns, which
// is what [CEReactions] promises a page.
//
// WHAT `super()` MEANS HERE. The VM does not rebind `this` to what a NATIVE
// base constructor returns (context::rebind_receiver), so the specification's
// "the HTMLElement constructor returns the element being upgraded" cannot be
// spelled that way. Instead the RECEIVER becomes the element: `new X()`
// arrives with a fresh object whose prototype is X.prototype, and the
// constructor turns that object into the wrapper of a new node. An UPGRADE
// calls the class body with the existing wrapper as `this`, so the identity
// every reference to the element already holds is kept, and `super()` then
// finds a receiver that is on the definition's construction stack: it takes
// the prototype and marks the entry constructed, exactly as HTML 4.13.4 step
// 7 does, and a second `super()` meets the marker and throws.
//
// WHAT A NATIVE CANNOT SEE: new.target. The VM carries it only into a JS
// frame, so `Reflect.construct(HTMLElement, [], NewTarget)` - the receiver
// made from HTMLElement.prototype, NewTarget invisible - is refused where a
// browser would build NewTarget's element. `new C()` and `super()` are what
// pages write, and both carry the class on the receiver's prototype.
//
// ponytail: the scan is O(tree) per DOM write once anything is defined, and
// a candidate parsed into a DETACHED subtree is upgraded when it connects or
// when the native that made it says so (upgrade_created_subtree). Both hold
// until a page shows the cost.
// ponytail: a customized built-in's "is value" is kept (`is_values_`) for what
// createElement, the HTML element constructor, the parser's construction and
// a clone make; a candidate the parser made before its definition existed
// still answers from its `is` ATTRIBUTE, and the serialiser and
// `:defined` read the attribute for every element.

#include <ctbrowser/shell/bindings.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser::shell {
namespace {

constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

// HTML 4.13.1, "valid custom element name" AS IT IS TODAY - not
// PotentialCustomElementName, which whatwg/html#7991 replaced.
//
// The old production was a fixed list of XML name characters; a name is now
// any VALID ELEMENT LOCAL NAME that has no ASCII upper alpha in it, has a
// hyphen, and is not one of the eight SVG/MathML names. That is much wider:
// `a!-element`, `a\x01-element` and `_-element` are all names a page may
// define, and PotentialCustomElementName refused every one of them.
//
// A valid element local name is the tag names the HTML and the XML parsers
// can both produce, which is the specification's one regular expression:
//   ^(?:[A-Za-z][^NUL TAB LF FF CR SPACE / >]*
//     | [:_ or U+0080 and up][A-Za-z0-9-.:_ or U+0080 and up]*)$
// The first branch is what the HTML tokenizer will read back as a tag name -
// it bans only the characters that would END one - and the second is the XML
// Name production's shape.
[[nodiscard]] bool html_tag_name_char(char32_t c) {
    return c != 0 && c != 0x9 && c != 0xA && c != 0xC && c != 0xD && c != 0x20 && c != '/' &&
           c != '>';
}

[[nodiscard]] bool xml_name_char(char32_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '.' || c == ':' || c == '_' || (c >= 0x80 && c <= 0x10FFFF);
}

[[nodiscard]] bool is_valid_element_local_name(const std::vector<char32_t> & points) {
    if (points.empty()) { return false; }
    const char32_t first = points.front();
    if ((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z')) {
        bool ok = true;
        for (std::size_t i = 1; i < points.size(); ++i) {
            ok = ok && html_tag_name_char(points[i]);
        }
        if (ok) { return true; }
    }
    if (first != ':' && first != '_' && (first < 0x80 || first > 0x10FFFF)) { return false; }
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (!xml_name_char(points[i])) { return false; }
    }
    return true;
}

[[nodiscard]] bool is_valid_custom_element_name(std::string_view name) {
    if (name.empty()) { return false; }
    std::vector<char32_t> points;
    for (std::size_t at = 0; at < name.size();) { points.push_back(decode_utf8(name, at)); }
    // THE FIRST CHARACTER IS AN ASCII LOWER ALPHA. It is a requirement of its
    // own, beside the local-name production: `_-element`, `:-element` and
    // `\u{10000}-element` are all valid element local names and none of them
    // is a valid CUSTOM element name. MEASURED - five subtests of
    // valid-custom-element-names.html say so by code point.
    if (points.front() < 'a' || points.front() > 'z') { return false; }
    bool hyphen = false;
    for (const char32_t c : points) {
        if (c >= 'A' && c <= 'Z') { return false; }
        hyphen = hyphen || c == '-';
    }
    if (!hyphen || !is_valid_element_local_name(points)) { return false; }
    for (const std::string_view reserved :
         {"annotation-xml", "color-profile", "font-face", "font-face-src", "font-face-uri",
          "font-face-format", "font-face-name", "missing-glyph"}) {
        if (name == reserved) { return false; }
    }
    return true;
}

// The text a reported exception carries. `name` and `message` are read as
// data rather than through `toString`, which a page's own thrown object may
// have made throw - see events/dispatch.cpp's twin.
[[nodiscard]] std::string describe(context & cx, value thrown) {
    if (!thrown.is_object()) { return cx.to_string(thrown); }
    const value name = cx.lookup_property(thrown, "name");
    const value message = cx.lookup_property(thrown, "message");
    if (name.is_undefined() && message.is_undefined()) { return "an exception"; }
    std::string text = name.is_undefined() ? std::string{"Error"} : cx.to_string(name);
    const std::string body = message.is_undefined() ? std::string{} : cx.to_string(message);
    if (!body.empty()) { text += ": " + body; }
    return text;
}

// WebIDL `sequence<DOMString>` from an iterable: every conversion step can
// throw - Symbol.iterator not callable, `next` throwing - and each of those
// leaves the VM's throw pending for the native to rethrow. False when it did.
[[nodiscard]] bool string_sequence(context & cx, value from, std::vector<std::string> & out) {
    if (!from.is_object_like()) {
        cx.throw_error("TypeError", "The provided value cannot be converted to a sequence.");
        return false;
    }
    // GetMethod(V, @@iterator): a method that is present and not callable -
    // `{[Symbol.iterator]: 1}` - is a TypeError, not an empty list.
    const value method = cx.lookup_property(from, CTBROWSER_SYMBOL_KEY("iterator"));
    if (cx.throw_pending()) { return false; }
    if (!method.is_callable()) {
        cx.throw_error("TypeError", "The provided value cannot be converted to a sequence.");
        return false;
    }
    const value items = cx.iterable_values(from);
    if (cx.throw_pending()) { return false; }
    if (!items.is_array()) {
        cx.throw_error("TypeError", "The provided value cannot be converted to a sequence.");
        return false;
    }
    for (const value & entry : static_cast<script::array_object *>(items.as_heap())->items) {
        out.push_back(cx.to_string(entry));
        if (cx.throw_pending()) { return false; }
    }
    return true;
}

} // namespace

// --- lookups ------------------------------------------------------------------

std::size_t dom_bindings::custom_definition_for(const read_txn & txn, node_id id,
                                                bool connected) const {
    // HTML 4.13.3 "look up a custom element definition" in THE ELEMENT'S OWN
    // registry (DOM 4.9 "create an element" and "try to upgrade"): null -
    // a document with no browsing context, a null-registry subtree - finds
    // nothing, and only the HTML namespace has custom elements.
    const custom_element_registry * registry = registry_of_node(txn, id, connected);
    if (registry == nullptr || txn.element_ns(id) != node_ns::html) { return npos; }
    const std::string_view tag = atoms_->text(txn.tag(id).value_or(atom{}));
    const std::vector<custom_element_definition> & defs = primary().custom_definitions_;
    for (std::size_t i = 0; i < defs.size(); ++i) {
        const custom_element_definition & def = defs[i];
        if (def.registry != registry || def.local_name != tag) { continue; }
        if (def.name == def.local_name) { return i; }
        // A customized built-in is named by its IS VALUE - kept from its
        // creation - or, for one nothing recorded (the parser's), its `is`
        // attribute.
        const auto held = is_values_.find(id.key());
        const std::string_view is = held != is_values_.end()
                                        ? std::string_view{held->second}
                                        : txn.attribute_value(id, atoms_->intern("is"));
        if (is == def.name) { return i; }
    }
    return npos;
}

std::size_t dom_bindings::custom_definition_of(context & cx, value receiver) {
    const std::vector<custom_element_definition> & defs = primary().custom_definitions_;
    dom_bindings * owner = owner_of(receiver);
    if (const node_id held = owner == nullptr ? node_id{} : owner->handle_of(receiver)) {
        // AN UPGRADE IN PROGRESS: the receiver is the element on top of some
        // definition's construction stack.
        for (std::size_t i = 0; i < defs.size(); ++i) {
            const auto & stack = defs[i].construction_stack;
            if (!stack.empty() && stack.back().element == held &&
                (defs[i].registry->document == owner || defs[i].registry->document == nullptr)) {
                return i;
            }
        }
        return npos;
    }
    // `new C()`: the receiver was made from C.prototype, and C is the class
    // the definition holds - EXACTLY that prototype, not one on its chain,
    // because a subclass nobody defined is not a defined element (HTML
    // 4.13.4 step 3 looks the definition up by the constructor itself).
    //
    // IN WHICH REGISTRY is HTML 3.2.3's step 2: the one the "active custom
    // element constructor map" names for C - createElement put it there for
    // the length of the construction - and otherwise the current global's
    // document's, a GLOBAL registry. A class defined only in a scoped
    // registry is not constructible with `new` (registries/Construct.html).
    const value proto = cx.get_prototype(receiver);
    const custom_element_registry * wanted = nullptr;
    for (const auto & [key, registry, document] : primary().active_constructors_) {
        if (key == proto.bits()) { wanted = registry; }
    }
    for (std::size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].prototype.bits() != proto.bits()) { continue; }
        if (wanted != nullptr ? defs[i].registry == wanted
                              : defs[i].registry->document != nullptr) {
            return i;
        }
    }
    return npos;
}

// --- which registry a node has ------------------------------------------------
//
// DOM 4.9 gives every element, shadow root and document a CUSTOM ELEMENT
// REGISTRY - null, the document's global one, or a scoped one a page made
// with `new CustomElementRegistry()` - fixed when the node is created and
// changed afterwards only by initialize() and an adoption. Kept here as the
// EXCEPTIONS to a default, because almost every node has the default and a
// table entry per element would be the whole page twice over:
//
//   * an explicit entry (`node_registries_`, and `shadow_registries_` for a
//     shadow root, keyed by its host) for whatever creation, cloning,
//     initialize() or an adoption said that the default does not;
//   * the PARSER'S null registries, which the document records
//     (document::null_custom_element_registry);
//   * TEMPLATE CONTENTS: their node document is the template's "inert"
//     document, which has no registry, so everything under a contents
//     fragment - and a clone taken out of one, until it is inserted
//     somewhere (`inert_roots_`) - has none. This engine keeps template
//     contents in the page's own document, so that document is emulated
//     by where the node is rather than by which slab holds it;
//   * and otherwise the node document's registry: the global one of a
//     document with a browsing context, null for one a page made - unless
//     initialize() gave that document a scoped one.
//
// ponytail: a clone of template contents marks its root in `inert_roots_`
// and nothing sweeps the entry once the clone is inserted; a page that
// clones a template a million times keeps a million small entries.

dom_bindings::custom_element_registry * dom_bindings::document_registry() const noexcept {
    return registry_ != nullptr ? registry_ : scoped_document_registry_;
}

bool dom_bindings::may_upgrade() const noexcept {
    return document_registry() != nullptr || !node_registries_.empty() ||
           !shadow_registries_.empty();
}

dom_bindings::custom_element_registry * dom_bindings::registry_of_node(const read_txn & txn,
                                                                       node_id id,
                                                                       bool connected) const {
    const auto inert = [&](node_id at) {
        if (connected) { return false; }
        const node_id top = txn.root_of_tree(at, true);
        return static_cast<bool>(doc_->template_of(top)) || inert_roots_.contains(top.key());
    };
    const node_kind kind = txn.kind(id).value_or(node_kind::text);
    if (kind == node_kind::document) { return document_registry(); }
    if (kind == node_kind::document_fragment) {
        const shadow_tree * tree = shadow_tree_of(id);
        if (tree == nullptr) { return nullptr; }
        if (const auto held = shadow_registries_.find(tree->host.key());
            held != shadow_registries_.end()) {
            return held->second;
        }
        return tree->null_registry || inert(tree->host) ? nullptr : document_registry();
    }
    if (kind != node_kind::element) { return nullptr; }
    if (const auto held = node_registries_.find(id.key()); held != node_registries_.end()) {
        return held->second;
    }
    if (doc_->null_custom_element_registry(id) || inert(id)) { return nullptr; }
    return document_registry();
}

void dom_bindings::set_node_registry(const read_txn & txn, node_id id,
                                     custom_element_registry * registry) {
    // A SCOPED registry learns the document now holding a node of it: its
    // "scoped document set", which define() upgrades in this order.
    if (registry != nullptr && registry->document == nullptr &&
        std::ranges::find(registry->documents, this) == registry->documents.end()) {
        registry->documents.push_back(this);
    }
    // An entry only where the default would say something else - see above.
    const node_kind kind = txn.kind(id).value_or(node_kind::text);
    if (kind == node_kind::document) {
        if (registry_ == nullptr) { scoped_document_registry_ = registry; }
        return;
    }
    if (kind == node_kind::document_fragment) {
        const shadow_tree * tree = shadow_tree_of(id);
        if (tree == nullptr) { return; }
        shadow_registries_.erase(tree->host.key());
        if (registry_of_node(txn, id) != registry) {
            shadow_registries_.emplace(tree->host.key(), registry);
        }
        return;
    }
    if (kind != node_kind::element) { return; }
    node_registries_.erase(id.key());
    if (registry_of_node(txn, id) != registry) { node_registries_.emplace(id.key(), registry); }
}

bool dom_bindings::registry_option(context & cx, value dictionary, custom_element_registry *& out,
                                   bool & given) {
    // A dictionary member of type `CustomElementRegistry?`: absent leaves
    // `out` alone, null is the null registry, and anything that is not a
    // CustomElementRegistry fails the conversion.
    given = false;
    if (!dictionary.is_object_like()) { return true; }
    const value member = cx.lookup_property(dictionary, "customElementRegistry");
    if (cx.throw_pending()) { return false; }
    if (member.is_undefined()) { return true; }
    given = true;
    if (member.is_null()) {
        out = nullptr;
        return true;
    }
    const auto & objects = primary().registry_objects_;
    const auto found = objects.find(member.bits());
    if (found == objects.end()) {
        cx.throw_error("TypeError", "The provided value is not of type 'CustomElementRegistry'.");
        return false;
    }
    out = found->second;
    return true;
}

bool dom_bindings::shadow_registry_option(context & cx, node_id host, value init) {
    // DOM 4.8 attachShadow() steps 1-3, ahead of the host checks: the node
    // document's registry unless `init` names one, and a GLOBAL registry
    // that is not the node document's is a NotSupportedError.
    custom_element_registry * registry = document_registry();
    bool given = false;
    if (!registry_option(cx, init, registry, given)) { return false; }
    if (registry != nullptr && registry->document != nullptr && registry != document_registry()) {
        throw_dom_exception(cx, "NotSupportedError",
                            "attachShadow: the customElementRegistry is another document's "
                            "global registry");
        return false;
    }
    // A host that already has a root keeps that root's registry: attachShadow
    // either claims a declarative root, which keeps its own, or refuses.
    if (!host || shadow_root_of(host)) { return true; }
    shadow_registries_.erase(host.key());
    if (given) {
        const auto txn = doc_->read();
        const node_id top = txn.root_of_tree(host, true);
        const bool inert =
            static_cast<bool>(doc_->template_of(top)) || inert_roots_.contains(top.key());
        if (registry != (inert ? nullptr : document_registry())) {
            shadow_registries_.emplace(host.key(), registry);
        }
        if (registry != nullptr && registry->document == nullptr &&
            std::ranges::find(registry->documents, this) == registry->documents.end()) {
            registry->documents.push_back(this);
        }
    }
    return true;
}

// THE REGISTRIES A PARSED FRAGMENT'S ELEMENTS GET, once it is copied in: each
// element's is its intended parent's (HTML 13.2.6.1) - the fragment's
// `inherited` for the top level, which is the context's - except where the
// parser recorded a null one (a `customelementregistry` start tag, and what
// is under it); a declarative shadow root's is the document's, or null with
// `shadowrootcustomelementregistry`, and its children inherit that.
void dom_bindings::assign_parsed_registries(const read_txn & txn, node_id at,
                                            custom_element_registry * inherited) {
    if (txn.kind(at).value_or(node_kind::text) != node_kind::element) { return; }
    custom_element_registry * mine = doc_->null_custom_element_registry(at) ? nullptr : inherited;
    set_node_registry(txn, at, mine);
    if (const node_id root = shadow_root_of(at)) {
        const shadow_tree * tree = shadow_tree_of(root);
        custom_element_registry * shadow =
            tree != nullptr && tree->null_registry ? nullptr : document_registry();
        for (const node_id child : txn.children(root)) {
            assign_parsed_registries(txn, child, shadow);
        }
    }
    for (const node_id child : txn.children(at)) { assign_parsed_registries(txn, child, mine); }
}

// DOM 4.5 "adopt", the registry half, for `at` and what is under it once it
// is this document's: a shadow root that is null (and not kept null) or
// global takes this document's effective global registry, and so does an
// element that is null or global - from its PARENT'S registry when it has a
// parent that is not a plain fragment and it was null, from the document's
// otherwise. A scoped registry travels unchanged.
void dom_bindings::adopt_registries(const read_txn & txn, node_id at,
                                    custom_element_registry * parent, bool counted) {
    const auto effective_global = [](custom_element_registry * registry) {
        return registry != nullptr && registry->document != nullptr ? registry : nullptr;
    };
    // `parent` is the registry of the parent, and `counted` whether that
    // parent counts - an element or a shadow root does; no parent, or a plain
    // fragment, sends the element to the document's registry instead.
    const node_kind kind = txn.kind(at).value_or(node_kind::text);
    custom_element_registry * mine = registry_of_node(txn, at);
    if (kind == node_kind::element) {
        if (mine == nullptr || mine->document != nullptr) {
            mine = effective_global(mine != nullptr || !counted ? document_registry() : parent);
            set_node_registry(txn, at, mine);
        }
        if (const node_id root = shadow_root_of(at)) {
            const shadow_tree * tree = shadow_tree_of(root);
            custom_element_registry * shadow = registry_of_node(txn, root);
            if (tree != nullptr && ((shadow == nullptr && !tree->null_registry) ||
                                    (shadow != nullptr && shadow->document != nullptr))) {
                shadow = effective_global(document_registry());
                set_node_registry(txn, root, shadow);
            }
            for (const node_id child : txn.children(root)) {
                adopt_registries(txn, child, shadow, true);
            }
        }
    }
    for (const node_id child : txn.children(at)) {
        adopt_registries(txn, child, mine, kind == node_kind::element);
    }
}

void dom_bindings::sync_custom_element_roots() {
    dom_bindings & top = primary();
    if (top.custom_elements_interface_ == nullptr) { return; }
    std::vector<value> & roots = top.custom_elements_interface_->retained;
    roots.clear();
    for (const custom_element_definition & def : top.custom_definitions_) {
        for (const value & held :
             {def.constructor, def.prototype, def.connected, def.disconnected, def.adopted,
              def.attribute_changed, def.connected_move, def.form_associated_callback,
              def.form_reset, def.form_disabled, def.form_state_restore}) {
            roots.push_back(held);
        }
    }
    for (const auto & reg : top.registries_) {
        roots.push_back(reg->object);
        for (const auto & [name, promise] : reg->when_defined) { roots.push_back(promise); }
    }
    roots.push_back(top.construct_fence_);
    roots.push_back(top.custom_elements_registry_prototype_);
}

dom_bindings::custom_element_registry & dom_bindings::registry_of(value receiver) {
    dom_bindings & top = primary();
    const auto found = top.registry_objects_.find(receiver.bits());
    return found == top.registry_objects_.end() ? *top.registry_ : *found->second;
}

dom_bindings::custom_element_registry & dom_bindings::make_registry(dom_bindings * document,
                                                                    value object) {
    dom_bindings & top = primary();
    auto & made = *top.registries_.emplace_back(std::make_unique<custom_element_registry>());
    made.document = document;
    made.object = object;
    top.registry_objects_.emplace(object.bits(), &made);
    sync_custom_element_roots();
    return made;
}

value dom_bindings::custom_elements_registry(context & cx) {
    if (registry_ != nullptr) { return registry_->object; }
    auto * registry = cx.allocate<script::object_object>();
    registry->prototype = primary().custom_elements_registry_prototype_;
    // Only the page and a frame ask for one: a document a page made has no
    // window to hang it on. Having a registry is having a browsing context.
    registry_ = &make_registry(this, value::object(registry));
    // `:defined` asks the document what the tree cannot say (dom/document.hpp).
    // A weak token for the reason watch_shadow_disabled gives.
    if (doc_ != nullptr) {
        doc_->set_custom_state_hook(
            [this, alive = std::weak_ptr<int>{lifetime_}](node_id element) -> std::optional<bool> {
                if (alive.expired()) { return std::nullopt; }
                const auto found = custom_elements_.find(element.key());
                if (found == custom_elements_.end()) { return std::nullopt; }
                return found->second.state == custom_element_state::status::custom;
            });
    }
    return registry_->object;
}

// --- the HTML element constructor ---------------------------------------------

value dom_bindings::construct_html_element(context & c, value self,
                                           std::string_view interface_name) {
    if (!self.is_object_like()) {
        c.throw_error("TypeError", "Failed to construct '" + std::string{interface_name} +
                                       "': please use the 'new' operator.");
        return value::undefined();
    }
    const bool is_element = owner_of(self) != nullptr;
    // HTML 4.13.4 step 2: NewTarget equal to the active function object - the
    // receiver made from THIS interface's own prototype - is a TypeError
    // whether or not somebody defined the interface itself as an element.
    if (!is_element) {
        const value own = c.lookup_property(c.global(std::string{interface_name}), "prototype");
        if (own.bits() == c.get_prototype(self).bits()) {
            c.throw_error("TypeError", "Illegal constructor");
            return value::undefined();
        }
    }
    const std::size_t index = custom_definition_of(c, self);
    if (index == npos) {
        c.throw_error("TypeError", "Illegal constructor");
        return value::undefined();
    }
    custom_element_definition & def = primary().custom_definitions_[index];
    // Steps 5-6: an autonomous definition constructs through HTMLElement and
    // nothing else; a customized built-in through the interface its `extends`
    // tag has - `class extends HTMLButtonElement` with `{extends: "p"}` is a
    // TypeError at `super()`.
    const std::string_view wanted =
        def.name == def.local_name ? "HTMLElement" : interface_name_for_tag(def.local_name);
    if (wanted != interface_name) {
        c.throw_error("TypeError", "Illegal constructor: the custom element definition does not "
                                   "extend " +
                                       std::string{interface_name});
        return value::undefined();
    }
    // Step 7: AN UPGRADE. The element is on the construction stack; `super()`
    // gives it the class's prototype and marks it constructed. A second
    // `super()` on the same element is the "already constructed marker".
    //
    // AND WHILE ONE IS RUNNING, `new C()` from inside the constructor is the
    // same step: the stack is not empty, so the element on top is what the
    // constructor gets - or the marker, which is the TypeError. (The nested
    // constructor's `this` stays the object `new` made, since a native's
    // return cannot rebind it; what the outer `super()` then meets is the
    // marker, and that is the TypeError the page sees reported.)
    if (is_element || !def.construction_stack.empty()) {
        custom_element_definition::construction & top = def.construction_stack.back();
        if (top.constructed) {
            c.throw_error("TypeError", "The HTML element constructor was called twice for one "
                                       "upgrade: the top of the construction stack is already "
                                       "constructed");
            return value::undefined();
        }
        top.constructed = true;
        dom_bindings & upgrading =
            def.registry->document == nullptr ? primary() : *def.registry->document;
        const value element = is_element ? self : upgrading.wrap(c, top.element);
        if (element.is_object()) {
            static_cast<script::object_object *>(element.as_heap())->prototype = def.prototype;
        }
        return element;
    }
    // Steps 8-13: a NEW element, custom from the start, IN THE DOCUMENT WHOSE
    // REGISTRY HOLDS THE DEFINITION - a frame's, when the class was defined
    // through `frame.contentWindow.customElements` - and for a scoped one the
    // document whose createElement is running it, else the page's.
    dom_bindings * creating = nullptr;
    custom_element_registry * registry = def.registry;
    for (const auto & [key, active, document] : primary().active_constructors_) {
        if (key == def.prototype.bits() && active == def.registry) { creating = document; }
    }
    dom_bindings & owner = def.registry->document != nullptr ? *def.registry->document
                           : creating != nullptr             ? *creating
                                                             : primary();
    const node_id made = owner.doc_->create_element(atoms_->intern(def.local_name));
    if (def.local_name != def.name) {
        (void)owner.doc_->set_attribute(made, atoms_->intern("is"), def.name);
        owner.is_values_.insert_or_assign(made.key(), def.name);
    }
    owner.set_node_registry(owner.doc_->read(), made, registry);
    // THE RECEIVER BECOMES THE WRAPPER - what wrap() does to a fresh object,
    // done to the instance `new` made, whose prototype is the author's class.
    auto * obj = static_cast<script::object_object *>(self.as_heap());
    obj->set(std::string{handle_property}, value::number(static_cast<double>(made.key())));
    owner.install_element_views(c, *obj, made);
    owner.refresh_element(c, *obj, made);
    owner.wrappers_.emplace(made.key(), obj);
    custom_element_state state;
    state.definition = index;
    owner.watch_loose(made.key(), state);
    owner.custom_elements_.emplace(made.key(), std::move(state));
    // A customized built-in <script> is a script nothing has started, exactly
    // as createElement("script") notes one: its text runs when it connects,
    // and before its connectedCallback (the reactions follow the
    // post-connection steps).
    if (def.local_name == "script") { owner.note_unstarted_script(made); }
    return self;
}

// --- creation -------------------------------------------------------------------

value dom_bindings::construct_fenced(context & cx, value constructor, bool & threw,
                                     value & thrown) {
    threw = false;
    thrown = value::undefined();
    value & fence = primary().construct_fence_;
    if (!fence.is_callable()) {
        // `new C()` from JavaScript, where a throw has a `catch` to land in:
        // from C++ a throw crossing construct() unwinds to whatever page code
        // is above the native, which is the page's `try`, not this one's.
        script::program compiled = script::compiler::compile(
            "return (function (C) { try { return [true, new C()]; } catch (e) { return [false, "
            "e]; } });\n");
        if (compiled.ok) { fence = cx.run_nested(cx.own_program(std::move(compiled))); }
        sync_custom_element_roots();
    }
    if (!fence.is_callable()) {
        const value made = cx.construct(constructor, {});
        threw = cx.throw_pending();
        return made;
    }
    const value passed[1] = {constructor};
    const value answer = cx.call(fence, passed);
    if (!answer.is_array()) {
        threw = true;
        return value::undefined();
    }
    const auto & items = static_cast<script::array_object *>(answer.as_heap())->items;
    if (items.size() < 2 || !context::truthy(items[0])) {
        threw = true;
        thrown = items.size() >= 2 ? items[1] : value::undefined();
        return value::undefined();
    }
    return items[1];
}

// "Report the exception" for a constructor or a lifecycle callback, at the
// global of THAT callback's realm (HTML 4.13.5 step 8, 4.13.6 "invoke custom
// element reactions": "report exception for callback's ... associated
// realm's global object") - a class made in a frame reports to the frame,
// whichever window's registry defined it.
void dom_bindings::report_custom_element_exception(context & cx, value thrown,
                                                   std::string_view where, value callback) {
    const context::rooted keep{cx, thrown};
    dom_bindings & global = global_of(callback, primary());
    const std::string fault = std::string{where} + ": uncaught " + describe(cx, thrown);
    const bool handled = global.dispatch_error_value(fault, thrown);
    if (!handled && callback_error_.empty()) { callback_error_ = fault; }
}

dom_bindings & dom_bindings::global_of(value callback, dom_bindings & otherwise) {
    if (callback.is_kind(script::heap_kind::function)) {
        const auto * fn = static_cast<const script::closure_object *>(callback.as_heap());
        if (fn->realm != nullptr && fn->realm->global_this.is_heap()) {
            return bindings_of(fn->realm->global_this);
        }
    }
    return otherwise;
}

// `exact` is createElementNS's (and an XML document's createElement): the
// qualified name kept as written, `prefixed` saying whether its colon is a
// prefix, the local name after it what a definition is looked up by, and `ns`
// the element's namespace - only an HTML element is ever a custom one, but the
// registry option is every element's.
value dom_bindings::create_html_element(context & cx, const std::string & name, value options,
                                        bool exact, bool prefixed, node_ns ns) {
    const atom tag = exact ? atoms_->intern(name) : atoms_->intern_lower(name);
    std::string_view lowered = atoms_->text(tag);
    if (prefixed) { lowered = lowered.substr(lowered.find(':') + 1); }
    // DOM 4.9.2 "flatten element creation options", its members in the
    // dictionary's (lexicographic) order: `customElementRegistry`, then `is`,
    // and the two together are a NotSupportedError. The registry is the
    // document's unless one is given, and a GLOBAL one that is not this
    // document's cannot be.
    std::string is;
    bool has_is = false;
    custom_element_registry * registry = document_registry();
    if (options.is_object_like()) {
        bool given = false;
        if (!registry_option(cx, options, registry, given)) { return value::undefined(); }
        const value named = cx.lookup_property(options, "is");
        if (cx.throw_pending()) { return value::undefined(); }
        if (!named.is_nullish()) {
            is = cx.to_string(named);
            has_is = true;
        }
        if (given && has_is) {
            throw_dom_exception(cx, "NotSupportedError",
                                "createElement: `is` and `customElementRegistry` cannot be given "
                                "together");
            return value::undefined();
        }
    }
    if (registry != nullptr && registry->document != nullptr && registry != document_registry()) {
        throw_dom_exception(cx, "NotSupportedError",
                            "createElement: the customElementRegistry is another document's "
                            "global registry");
        return value::undefined();
    }
    // "Look up a custom element definition" in THAT registry (HTML 4.13.3):
    // a null one - a document with no browsing context - finds nothing.
    dom_bindings & reg = primary();
    std::size_t index = npos;
    if (registry != nullptr && ns == node_ns::html) {
        for (std::size_t i = 0; i < reg.custom_definitions_.size(); ++i) {
            const custom_element_definition & def = reg.custom_definitions_[i];
            if (def.registry != registry || def.local_name != lowered) { continue; }
            if (def.name == def.local_name || def.name == is) {
                index = i;
                break;
            }
        }
    }
    if (index == npos) {
        const node_id made = doc_->create_element(tag, ns, prefixed);
        // A candidate for a definition that may come later: its `is` value.
        if (ns == node_ns::html && !is.empty() && is_valid_custom_element_name(is) &&
            !lowered.contains('-')) {
            (void)doc_->set_attribute(made, atoms_->intern("is"), is);
            is_values_.insert_or_assign(made.key(), is);
        }
        set_node_registry(doc_->read(), made, registry);
        // createElementNS's <script>, noted as createElement's is.
        if (exact && ns == node_ns::html && lowered == "script") { note_unstarted_script(made); }
        return wrap(cx, made);
    }
    // A PREFIXED custom element (`createElementNS(html, "p:x-a")`) is
    // constructed under its local name and takes the prefix only once the
    // constructor has returned - step 6.1.3.9, and the element made instead
    // of one that failed has it too (Document-createElementNS-prefix-timing).
    const value made = construct_custom_element(cx, index, registry, atoms_->intern(lowered));
    if (const node_id id = prefixed ? handle_of(made) : node_id{}; id && made.is_object()) {
        doc_->set_element_prefix(id, tag);
        refresh_element(cx, *static_cast<script::object_object *>(made.as_heap()), id);
    }
    return made;
}

// DOM 4.9 "create an element" with the synchronous custom elements flag set,
// for a definition found in `registry`: createElement's, and the HTML
// parser's (parser_create_custom_element).
//
// SYNCHRONOUSLY, through the author's class: `super()` makes the receiver the
// wrapper of the node, so the instance IS the element - and everything the
// constructor did to it is checked, steps 6.1.3 to 6.1.9.
value dom_bindings::construct_custom_element(context & cx, std::size_t index,
                                             custom_element_registry * registry, atom tag) {
    dom_bindings & reg = primary();
    const std::string_view lowered = atoms_->text(tag);
    const value constructor = reg.custom_definitions_[index].constructor;
    bool threw = false;
    value thrown = value::undefined();
    // The "active custom element constructor map" entry for the length of
    // the construction (DOM 4.9 "create an element" 6.1.2-6.1.4): `super()`
    // reads which registry - and which document - it is constructing for.
    reg.active_constructors_.emplace_back(reg.custom_definitions_[index].prototype.bits(), registry,
                                          this);
    const value made = construct_fenced(cx, constructor, threw, thrown);
    reg.active_constructors_.pop_back();
    const context::rooted keep_made{cx, made};
    // "Clean up after running script": constructed from the PARSER, no script
    // is left on the stack, so the microtasks the constructor queued run now -
    // before the checks below, which they may fail (microtasks-and-
    // constructors.html). From createElement the calling script is still there.
    if (cx.current_stack().empty()) { cx.drain_microtasks(); }
    std::string failure;
    std::string failure_name = "NotSupportedError";
    if (!threw) {
        dom_bindings * home = owner_of(made);
        const node_id result = home == nullptr ? node_id{} : home->handle_of(made);
        const auto txn = (home == nullptr ? *this : *home).doc_->read();
        if (!result || txn.kind(result).value_or(node_kind::text) != node_kind::element) {
            failure_name = "TypeError";
            failure = "the custom element constructor did not return an element";
        } else if (home != this) {
            failure = "the custom element was adopted into another document during "
                      "construction";
        } else if (txn.local_name(result) != lowered) {
            failure = "the custom element constructor returned an element with the wrong "
                      "local name";
        } else if (txn.parent(result)) {
            failure = "the custom element was inserted into a parent during construction";
        } else if (!txn.children(result).empty()) {
            failure = "the custom element gained children during construction";
        } else {
            for (const attribute & held : txn.attributes(result)) {
                if (atoms_->text(held.name) != "is") {
                    failure = "the custom element gained attributes during construction";
                    break;
                }
            }
        }
        if (!failure.empty()) {
            thrown = failure_name == "TypeError" ? cx.make_error("TypeError", failure)
                                                 : make_dom_exception(cx, failure_name, failure);
        }
    }
    if (threw || !failure.empty()) {
        // "Report the exception, and set result to a new HTMLUnknownElement" -
        // a constructor that threw, or never reached HTMLElement, does not
        // make createElement throw. Custom element state "failed".
        report_custom_element_exception(cx, thrown, "custom element constructor", constructor);
        const node_id made_instead = doc_->create_element(tag);
        set_node_registry(doc_->read(), made_instead, registry);
        custom_element_state state;
        state.definition = index;
        state.state = custom_element_state::status::failed;
        custom_elements_.emplace(made_instead.key(), std::move(state));
        // "...a new element that implements the HTMLUnknownElement interface":
        // a customized built-in keeps its own interface, an autonomous one
        // whose name would otherwise be a plain HTMLElement is the unknown one.
        const value instead = wrap(cx, made_instead);
        if (index != npos && reg.custom_definitions_[index].name == lowered &&
            instead.is_object()) {
            const value unknown = interface_prototype("HTMLUnknownElement");
            if (unknown.is_object()) {
                static_cast<script::object_object *>(instead.as_heap())->prototype = unknown;
            }
        }
        return instead;
    }
    // createElement notes a <script> it made as unstarted itself, and the
    // constructor already did: one entry, not two runs.
    if (lowered == "script") { std::erase(unstarted_scripts_, handle_of(made)); }
    set_node_registry(doc_->read(), handle_of(made), registry); // step 6.1.3.9
    return made;
}

// HTML 13.2.6.1 "create an element for a token", steps 5-11, when a
// definition exists: the throw-on-dynamic-markup-insertion counter up, a
// microtask checkpoint when no script is running, the element constructed
// synchronously in the INTENDED PARENT'S registry, the token's attributes
// appended - after the constructor, so it does not see them - and their
// attributeChangedCallbacks run, then the counter down. Nothing, for the tree
// builder to create an ordinary element, when no definition covers it.
node_id dom_bindings::parser_create_custom_element(
    std::string_view local_name, const std::vector<html::token_attribute> & attributes,
    node_id intended_parent) {
    const std::vector<custom_element_definition> & defs = primary().custom_definitions_;
    if (cx_ == nullptr || defs.empty()) { return {}; }
    // Cheap first: a name nothing defines is the whole of a page's markup.
    std::string_view is;
    for (const html::token_attribute & held : attributes) {
        if (held.name == "is") { is = held.value; }
    }
    const auto covers = [&](const custom_element_definition & def) {
        return def.local_name == local_name && (def.name == def.local_name || def.name == is);
    };
    if (std::ranges::none_of(defs, covers)) { return {}; }
    custom_element_registry * registry = nullptr;
    {
        // A <template> on the stack means its contents - a declarative
        // shadow root's registry, or a plain fragment's none.
        const auto txn = doc_->read();
        node_id at = intended_parent;
        if (at && txn.element_ns(at) == node_ns::html && txn.local_name(at) == "template") {
            at = doc_->template_content(at);
        }
        registry = at ? registry_of_node(txn, at) : document_registry();
    }
    std::size_t index = npos;
    for (std::size_t i = 0; i < defs.size() && registry != nullptr; ++i) {
        if (defs[i].registry == registry && covers(defs[i])) { index = i; }
    }
    if (index == npos) { return {}; }
    context & cx = *cx_;
    ++markup_insertion_counter_;
    if (cx.current_stack().empty()) { cx.drain_microtasks(); }
    const std::size_t from = custom_reactions_.size();
    node_id made;
    if (defs[index].name != defs[index].local_name) {
        // A CUSTOMIZED BUILT-IN is created as the built-in and then UPGRADED,
        // synchronously (DOM 4.9 "create an element" step 5): a constructor
        // that throws leaves the element it already is, prototype and all,
        // and one that appends children appends them.
        made = doc_->create_element(atoms_->intern_lower(local_name));
        is_values_.insert_or_assign(made.key(), std::string{is});
        set_node_registry(doc_->read(), made, registry);
        custom_element_state state;
        state.definition = index;
        state.state = custom_element_state::status::precustomized;
        custom_elements_.emplace(made.key(), std::move(state));
        run_upgrade(cx, index, made, wrap(cx, made));
    } else {
        made = handle_of(
            construct_custom_element(cx, index, registry, atoms_->intern_lower(local_name)));
    }
    if (made) {
        // The parser's own append, which a MutationObserver does not see -
        // all but an `is` a customized built-in already carries.
        document::builder append{*doc_};
        const bool has_is = doc_->read().has_attribute(made, atoms_->intern("is"));
        for (const html::token_attribute & held : attributes) {
            if (has_is && held.name == "is") { continue; }
            append.set_attribute(made, atoms_->intern(held.name), held.value);
        }
        const auto txn = doc_->read();
        flat_map<std::uint64_t, bool> roots_seen;
        walk_custom_elements(txn, made, false, false, roots_seen);
    }
    flush_custom_element_reactions(from);
    --markup_insertion_counter_;
    return made;
}

// ...and its insertion (13.2.6.1 "insert an element at the adjusted insertion
// location"): the element queue that runs its connectedCallback before the
// parser reads its children.
void dom_bindings::parser_inserted_custom_element(node_id element) {
    if (cx_ == nullptr) { return; }
    const std::size_t from = custom_reactions_.size();
    {
        const auto txn = doc_->read();
        const node_id top = root_of_tree(txn, element, true);
        flat_map<std::uint64_t, bool> roots_seen;
        walk_custom_elements(txn, element,
                             top == txn.root() ||
                                 txn.kind(top).value_or(node_kind::element) == node_kind::document,
                             false, roots_seen);
    }
    flush_custom_element_reactions(from);
}

// --- the interfaces ---------------------------------------------------------------

void dom_bindings::install_custom_elements(context & cx) {
    // --- HTMLElement, CONSTRUCTIBLE. Defined here, before install_dom_interfaces
    // --- builds the table, which adopts a global that already exists rather
    // --- than replacing it with the throwing stub every other interface gets.
    auto * html_element_proto = cx.allocate<script::object_object>();
    auto * html_element_ctor = cx.allocate<script::native_object>(
        "HTMLElement", [this](context & c, std::span<value>) -> value {
            return construct_html_element(c, c.current_this(), "HTMLElement");
        });
    html_element_ctor->set("prototype", value::object(html_element_proto));
    html_element_proto->define("constructor", value::object(html_element_ctor),
                               script::attr_builtin);
    cx.define_builtin_global("HTMLElement", value::object(html_element_ctor));
    install_element_internals(cx, *html_element_proto);

    // --- CustomElementRegistry.prototype -----------------------------------
    auto * registry_proto = cx.allocate<script::object_object>();
    custom_elements_registry_prototype_ = value::object(registry_proto);
    // (registry, name) -> the definition's index in the primary's vector.
    const auto defined = [this](const custom_element_registry & reg, std::string_view name) {
        for (std::size_t i = 0; i < custom_definitions_.size(); ++i) {
            if (custom_definitions_[i].registry == &reg && custom_definitions_[i].name == name) {
                return i;
            }
        }
        return npos;
    };

    // A `Node` argument - any document's node, or a document itself.
    const auto node_argument = [this](context & c, value given, std::string_view method,
                                      dom_bindings *& owner, node_id & root) {
        owner = owner_of(given);
        root = owner == nullptr ? node_id{} : owner->handle_of(given);
        if (!root && given.is_object_like()) {
            dom_bindings & document = target_owner(given);
            if (document.is_the_document(given)) {
                owner = &document;
                root = document.doc_->document_node();
            }
        }
        if (!root || owner == nullptr) {
            c.throw_error("TypeError", "Failed to execute '" + std::string{method} +
                                           "' on 'CustomElementRegistry': parameter 1 is not "
                                           "of type 'Node'.");
            return false;
        }
        return true;
    };
    // "Try to upgrade" what is under `root` and has `reg` - connected or not -
    // in shadow-including tree order, and run the reactions it queued.
    const auto try_upgrade = [](dom_bindings & owner, node_id root,
                                const custom_element_registry & reg) {
        if (!owner.may_upgrade()) { return; }
        const std::size_t from = owner.custom_reactions_.size();
        {
            const auto txn = owner.doc_->read();
            const node_id top = owner.root_of_tree(txn, root, true);
            ++owner.scan_generation_;
            flat_map<std::uint64_t, bool> roots_seen;
            owner.upgrade_filter_ = &reg;
            owner.walk_custom_elements(txn, root,
                                       top == txn.root() ||
                                           txn.kind(top).value_or(node_kind::element) ==
                                               node_kind::document,
                                       true, roots_seen);
            owner.upgrade_filter_ = nullptr;
        }
        owner.flush_custom_element_reactions(from);
    };

    set_method(
        cx, *registry_proto, "define",
        [this, defined](context & c, std::span<value> args) -> value {
            custom_element_registry & reg = registry_of(c.current_this());
            // HTML 4.13.4 "element definition", in the specification's order:
            // the constructor, the name, the name twice over, the `extends`,
            // the running flag, then the prototype and everything read off it.
            const value ctor = arg(args, 1);
            if (!script::is_constructor(ctor)) {
                c.throw_error("TypeError", "Failed to execute 'define' on 'CustomElementRegistry': "
                                           "parameter 2 is not of type 'Function'.");
                return value::undefined();
            }
            const std::string name = arg_string(c, args, 0);
            if (!is_valid_custom_element_name(name)) {
                throw_dom_exception(c, "SyntaxError",
                                    "'" + name + "' is not a valid custom element name");
                return value::undefined();
            }
            if (defined(reg, name) != npos) {
                throw_dom_exception(c, "NotSupportedError",
                                    "the name '" + name +
                                        "' has already been used with this registry");
                return value::undefined();
            }
            for (const custom_element_definition & def : custom_definitions_) {
                if (def.registry == &reg && def.constructor.bits() == ctor.bits()) {
                    throw_dom_exception(
                        c, "NotSupportedError",
                        "this constructor has already been used with this registry");
                    return value::undefined();
                }
            }
            std::string local_name = name;
            const value options = arg(args, 2);
            if (options.is_object_like()) {
                const value extends = c.lookup_property(options, "extends");
                if (c.throw_pending()) { return value::undefined(); }
                if (!extends.is_undefined() && !extends.is_null()) {
                    // A scoped registry has no customized built-ins: `is`
                    // is looked up in the document's registry alone.
                    if (reg.document == nullptr) {
                        throw_dom_exception(c, "NotSupportedError",
                                            "a scoped CustomElementRegistry cannot define a "
                                            "customized built-in element");
                        return value::undefined();
                    }
                    local_name = c.to_string(extends);
                    if (is_valid_custom_element_name(local_name)) {
                        throw_dom_exception(c, "NotSupportedError",
                                            "'" + local_name +
                                                "' is a custom element name, not a "
                                                "built-in element to extend");
                        return value::undefined();
                    }
                    if (interface_name_for_tag(local_name) == "HTMLUnknownElement") {
                        throw_dom_exception(c, "NotSupportedError",
                                            "'" + local_name +
                                                "' is not a built-in element to extend");
                        return value::undefined();
                    }
                }
            }
            if (reg.running) {
                throw_dom_exception(c, "NotSupportedError",
                                    "customElements.define is already running");
                return value::undefined();
            }
            reg.running = true;
            // Steps 8-9: everything read off the constructor and its
            // prototype, with the flag cleared however it ends.
            custom_element_definition def;
            def.name = name;
            def.local_name = local_name;
            def.registry = &reg;
            def.constructor = ctor;
            const bool read = [&] {
                def.prototype = c.lookup_property(ctor, "prototype");
                if (c.throw_pending()) { return false; }
                if (!def.prototype.is_object_like()) {
                    c.throw_error("TypeError",
                                  "Failed to execute 'define' on 'CustomElementRegistry': "
                                  "The 'prototype' property is not an object");
                    return false;
                }
                const auto callback = [&](const char * which, value & into) {
                    into = c.lookup_property(def.prototype, which);
                    if (c.throw_pending()) { return false; }
                    if (!into.is_undefined() && !into.is_callable()) {
                        c.throw_error("TypeError", std::string{"Failed to execute 'define' on "
                                                               "'CustomElementRegistry': '"} +
                                                       which + "' is not a function");
                        return false;
                    }
                    return true;
                };
                if (!callback("connectedCallback", def.connected) ||
                    !callback("disconnectedCallback", def.disconnected) ||
                    !callback("connectedMoveCallback", def.connected_move) ||
                    !callback("adoptedCallback", def.adopted) ||
                    !callback("attributeChangedCallback", def.attribute_changed)) {
                    return false;
                }
                if (def.attribute_changed.is_callable()) {
                    const value observed = c.lookup_property(ctor, "observedAttributes");
                    if (c.throw_pending()) { return false; }
                    if (!observed.is_undefined() &&
                        !string_sequence(c, observed, def.observed_attributes)) {
                        return false;
                    }
                }
                const value disabled = c.lookup_property(ctor, "disabledFeatures");
                if (c.throw_pending()) { return false; }
                if (!disabled.is_undefined()) {
                    std::vector<std::string> features;
                    if (!string_sequence(c, disabled, features)) { return false; }
                    for (const std::string & feature : features) {
                        def.disable_internals = def.disable_internals || feature == "internals";
                        def.disable_shadow = def.disable_shadow || feature == "shadow";
                    }
                }
                const value form_associated = c.lookup_property(ctor, "formAssociated");
                if (c.throw_pending()) { return false; }
                def.form_associated = context::truthy(form_associated);
                if (def.form_associated) {
                    if (!callback("formAssociatedCallback", def.form_associated_callback) ||
                        !callback("formResetCallback", def.form_reset) ||
                        !callback("formDisabledCallback", def.form_disabled) ||
                        !callback("formStateRestoreCallback", def.form_state_restore)) {
                        return false;
                    }
                }
                return true;
            }();
            reg.running = false;
            if (!read) { return value::undefined(); }
            custom_definitions_.push_back(std::move(def));
            sync_custom_element_roots();
            // The candidates in the registry's document are upgraded, then
            // whenDefined settles. A SCOPED registry's are in every document of
            // its "scoped document set", in the order they joined it (see
            // set_node_registry); each scan upgrades only the elements whose
            // registry it is.
            if (reg.document != nullptr) {
                reg.document->react_custom_elements();
            } else {
                // COPIED: an upgrade may bring another document into the set.
                const std::vector<dom_bindings *> documents = reg.documents;
                for (dom_bindings * each : documents) { each->react_custom_elements(); }
            }
            if (const auto waiting = reg.when_defined.find(name);
                waiting != reg.when_defined.end()) {
                const value promise = waiting->second;
                reg.when_defined.erase(waiting);
                c.settle_promise(promise, ctor, false);
            }
            sync_custom_element_roots();
            return value::undefined();
        },
        script::attr_builtin);

    set_method(
        cx, *registry_proto, "get",
        [this, defined](context & c, std::span<value> args) {
            const std::size_t index =
                defined(registry_of(c.current_this()), arg_string(c, args, 0));
            return index == npos ? value::undefined() : custom_definitions_[index].constructor;
        },
        script::attr_builtin);

    set_method(
        cx, *registry_proto, "getName",
        [this](context & c, std::span<value> args) {
            const value ctor = arg(args, 0);
            if (!script::is_constructor(ctor)) {
                c.throw_error("TypeError", "Failed to execute 'getName' on "
                                           "'CustomElementRegistry': parameter 1 is not of "
                                           "type 'Function'.");
                return value::undefined();
            }
            const custom_element_registry & reg = registry_of(c.current_this());
            for (const custom_element_definition & def : custom_definitions_) {
                if (def.registry == &reg && def.constructor.bits() == ctor.bits()) {
                    return cx_->string(def.name);
                }
            }
            return value::null();
        },
        script::attr_builtin);

    set_method(
        cx, *registry_proto, "whenDefined",
        [this, defined](context & c, std::span<value> args) {
            custom_element_registry & reg = registry_of(c.current_this());
            const std::string name = arg_string(c, args, 0);
            if (!is_valid_custom_element_name(name)) {
                return c.make_promise(
                    make_dom_exception(c, "SyntaxError",
                                       "'" + name + "' is not a valid custom element name"),
                    true);
            }
            if (const std::size_t index = defined(reg, name); index != npos) {
                return c.make_promise(custom_definitions_[index].constructor, false);
            }
            // ONE promise per name, however often it is asked for.
            if (const auto held = reg.when_defined.find(name); held != reg.when_defined.end()) {
                return held->second;
            }
            const value promise = c.make_pending_promise();
            reg.when_defined.emplace(name, promise);
            sync_custom_element_roots();
            return promise;
        },
        script::attr_builtin);

    set_method(
        cx, *registry_proto, "upgrade",
        [this, node_argument, try_upgrade](context & c, std::span<value> args) {
            // "Try to upgrade" every shadow-including inclusive descendant
            // whose registry is THIS one (HTML 4.13.3 upgrade()).
            dom_bindings * owner = nullptr;
            node_id root;
            if (!node_argument(c, arg(args, 0), "upgrade", owner, root)) {
                return value::undefined();
            }
            if (!custom_definitions_.empty()) {
                try_upgrade(*owner, root, registry_of(c.current_this()));
            }
            return value::undefined();
        },
        script::attr_builtin);

    set_method(
        cx, *registry_proto, "initialize",
        [this, node_argument, try_upgrade](context & c, std::span<value> args) {
            // HTML 4.13.3 initialize(root): every inclusive descendant with
            // a null registry gets this one - the root too, when it is a
            // document or a shadow root with none - and what then has this
            // one is tried for an upgrade. A GLOBAL registry may only
            // initialize nodes of its own document, never a document.
            custom_element_registry & reg = registry_of(c.current_this());
            dom_bindings * owner = nullptr;
            node_id root;
            if (!node_argument(c, arg(args, 0), "initialize", owner, root)) {
                return value::undefined();
            }
            const auto txn = owner->doc_->read();
            const bool is_document =
                txn.kind(root).value_or(node_kind::element) == node_kind::document;
            if (reg.document != nullptr && (is_document || owner->document_registry() != &reg)) {
                throw_dom_exception(c, "NotSupportedError",
                                    "initialize: a global CustomElementRegistry cannot initialize "
                                    "a document or another document's nodes");
                return value::undefined();
            }
            if (is_document && owner->document_registry() == nullptr) {
                // The elements a page holds that are NOT in the tree keep the
                // null registry they had: pinned before the default changes.
                for (const auto & [key, wrapper] : owner->wrappers_) {
                    const node_id held = unpack(key);
                    if (!txn.contains(held) ||
                        txn.kind(held).value_or(node_kind::text) != node_kind::element ||
                        owner->node_registries_.contains(key)) {
                        continue;
                    }
                    const node_id top = txn.root_of_tree(held, true);
                    if (top != txn.root() &&
                        txn.kind(top).value_or(node_kind::element) != node_kind::document) {
                        owner->node_registries_.emplace(key, nullptr);
                    }
                }
                owner->scoped_document_registry_ = &reg;
            } else if (owner->shadow_tree_of(root) != nullptr &&
                       owner->registry_of_node(txn, root) == nullptr) {
                owner->set_node_registry(txn, root, &reg);
            }
            const auto walk = [&](auto && self, node_id at) -> void {
                if (txn.kind(at).value_or(node_kind::text) == node_kind::element &&
                    owner->registry_of_node(txn, at) == nullptr) {
                    owner->set_node_registry(txn, at, &reg);
                }
                for (const node_id child : txn.children(at)) { self(self, child); }
            };
            walk(walk, root);
            if (!custom_definitions_.empty()) { try_upgrade(*owner, root, reg); }
            return value::undefined();
        },
        script::attr_builtin);

    // `new CustomElementRegistry()` - a SCOPED registry (HTML 4.13.3): its
    // definitions belong to no document. What looks them up is an element,
    // shadow root or document whose OWN registry it is - given by
    // createElement's and attachShadow's options, by initialize(), and
    // inherited by what the parser and a clone make under them (see "which
    // registry a node has" above).
    auto * registry_ctor = cx.allocate<script::native_object>(
        "CustomElementRegistry", [this](context & c, std::span<value>) {
            const value self = c.current_this();
            if (!self.is_object()) {
                c.throw_error("TypeError", "Failed to construct 'CustomElementRegistry': please "
                                           "use the 'new' operator.");
                return value::undefined();
            }
            (void)make_registry(nullptr, self);
            return self;
        });
    registry_ctor->set("prototype", value::object(registry_proto));
    registry_proto->define("constructor", value::object(registry_ctor), script::attr_builtin);
    cx.define_builtin_global("CustomElementRegistry", value::object(registry_ctor));
    custom_elements_interface_ = registry_ctor;

    // `window.customElements` - a bare global, which the window proxy answers
    // for `window.customElements` and `self.customElements` too.
    cx.define_global("customElements", custom_elements_registry(cx));
}

// `customElementRegistry` on Element, ShadowRoot and Document (DOM 4.9,
// 4.2.? DocumentOrShadowRoot): the node's registry, or null. Installed once
// the interface table exists (install_shadow_dom), on the shared prototypes,
// so the node's OWN document answers - a frame's element reads the frame's.
void dom_bindings::install_registry_accessors(context & cx) {
    const auto getter = [this](context & c, std::span<value>) -> value {
        const value self = c.current_this();
        custom_element_registry * registry = nullptr;
        if (dom_bindings * owner = owner_of(self)) {
            registry = owner->registry_of_node(owner->doc_->read(), owner->handle_of(self));
            return registry == nullptr ? value::null() : registry->object;
        }
        // A DOCUMENT: the proxy a page holds, or - read through it - the object
        // behind it (make_document_proxy), of any document in the realm.
        const auto is = [&self](dom_bindings & d) {
            return d.is_the_document(self) ||
                   (self.is_object() && self.as_heap() == d.document_object());
        };
        dom_bindings & top = primary();
        dom_bindings * document = is(top) ? &top : nullptr;
        for (const auto & made : top.secondary_documents_) {
            if (document == nullptr && is(*made)) { document = made.get(); }
        }
        if (document == nullptr) { return value::undefined(); }
        registry = document->document_registry();
        return registry == nullptr ? value::null() : registry->object;
    };
    for (const char * name : {"Element", "ShadowRoot", "Document"}) {
        if (const value proto = interface_prototype(name); proto.is_object()) {
            static_cast<script::object_object *>(proto.as_heap())
                ->define_accessor("customElementRegistry",
                                  native(cx, "customElementRegistry", getter), value::undefined());
        }
    }
}

// A FORM-ASSOCIATED CUSTOM ELEMENT (HTML 4.13.2): a custom element whose
// definition says `formAssociated` - a form control, so one the browser's
// focus has to ask whether it is disabled.
bool dom_bindings::form_associated(node_id element) const {
    const auto found = custom_elements_.find(element.key());
    if (found == custom_elements_.end() ||
        found->second.state != custom_element_state::status::custom) {
        return false;
    }
    const std::vector<custom_element_definition> & defined = primary().custom_definitions_;
    return found->second.definition < defined.size() &&
           defined[found->second.definition].form_associated;
}

} // namespace ctbrowser::shell
