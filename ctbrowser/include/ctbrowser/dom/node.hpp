#pragma once
#include <atomic>
#include <boost/container/small_vector.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>

// The DOM node, and the reason reads take no locks.
//
// A generation-tagged handle stops a reader resolving a FREED node. It does
// nothing about a reader iterating a child vector while a writer pushes onto
// it - that is a plain data race on the vector, and no amount of handle
// checking fixes it. So the mutable parts of a node are not mutable:
//
//   children, attributes and text are IMMUTABLE blocks behind atomic
//   pointers. A writer builds a whole new block and publishes the pointer
//   with one release store; a reader loads it with one acquire load and then
//   reads a block nobody will ever touch again. The old block is handed to
//   the epoch domain, which destroys it once no reader can still be holding
//   it.
//
// That is RCU, and it is why a reader needs no lock: it never observes a
// half-updated anything. It sees the old block or the new one.
//
// What this deliberately does NOT store, and the previous engine's node did: layout rects,
// text-line caches, widget state, selection ranges, caret positions, blink
// phase. Those are outputs of style and layout, and keeping them on the node
// is what made the previous engine's layout unable to run concurrently. They belong to the box
// tree, which is not the DOM tree.

namespace ctbrowser {

struct node_tag {};
using node_id = handle<node_tag>;

enum class node_kind : std::uint8_t {
    document,
    element,
    text,
    comment,
    // A DocumentFragment: a parentless bag of nodes that a script fills and
    // then inserts, at which point its CHILDREN move and the fragment itself
    // does not. It never reaches style or layout - insertion flattens it - so
    // nothing downstream has to learn about it; what it buys is the one idiom
    // that makes building a list cheap, `frag.append(a, b, c); ul.append(frag)`.
    document_fragment
};

// Which language an element is written in. HTML and SVG share a document but
// not a vocabulary: an SVG <title> is a tooltip and an HTML one is the window
// title, an SVG <a> is not a link element, and `<text>` means nothing in HTML
// at all. Without this they are the same atom and the difference is
// unrecoverable.
//
// FREE, in the literal sense: `node` is kind(1) + tag(4) with three bytes of
// padding between them, so this occupies padding that already existed.
// unittests/unit/dom_basics asserts sizeof(node) did not move.
enum class node_ns : std::uint8_t {
    html,
    svg,
    // NEITHER, which `document.createElementNS` can ask for and the parser
    // never produces. The exact URI is not here - it lives beside the element
    // wrapper, because putting a fourth field on `node` would take it from 40
    // bytes to 48 and this is the most replicated object in the engine. What
    // the enumerator buys is the distinction every consumer actually tests for:
    // `element_ns == html` gates script execution, <style> collection and the
    // tagName case fold, and an element in some page-invented namespace must
    // fail all three.
    other
};

// AN ATTRIBUTE HAS A NAMESPACE. DOM §4.9 says one is four things - a namespace,
// a namespace prefix, a local name and a value - and this is two atoms and a
// string, because the other two are DERIVABLE and a fourth field is not free.
//
// `name` is the QUALIFIED name: `prefix:local` when there is a prefix and
// `local` when there is not. It stays first and stays the whole name because it
// is what every existing reader compares - the style engine, the layout engine,
// `getAttribute` - and that comparison must stay one integer compare.
//
// `ns` is the namespace URI, INTERNED. The empty atom is the null namespace,
// which is what every attribute in an ordinary HTML document has: `xlink:href`
// and `xmlns:xlink` inside `<svg>` are the only ones a parsed page normally
// carries. It is an atom for the reason `node_ns` beside it is an enumerator
// rather than a URI - a `std::string` here is 32 bytes on EVERY attribute of
// EVERY element to describe a case that arises twice per document - and it is
// an atom rather than a three-valued enum because `setAttributeNS` may be
// handed any URI a page can spell.
//
// AND IT COSTS NOTHING AT ALL, which is the same trick and the same measurement
// `node_ns` records: `attribute` was atom(4) + four bytes of padding +
// std::string(32), so `ns` lands in padding that already existed and the
// `small_vector<attribute, 2>` on every element is the size it always was. The
// static_assert below is what stops a third atom being added without anybody
// noticing that it is no longer free.
struct attribute {
    atom name;
    atom ns;
    std::string value; // "" for a boolean attribute, per HTML

    attribute() = default;
    // The null namespace, which is what the parser and `setAttribute` produce.
    attribute(atom qualified, std::string v) : name(qualified), value(std::move(v)) {}
    attribute(atom qualified, atom uri, std::string v)
        : name(qualified), ns(uri), value(std::move(v)) {}
};

static_assert(sizeof(attribute) <= sizeof(std::string) + alignof(std::string),
              "the namespace must be FREE: two atoms in the padding ahead of `value`");

// THE LOCAL NAME AND THE PREFIX, DERIVED RATHER THAN STORED - and derived
// exactly, not approximately.
//
// The rule that makes it exact is one line of DOM's "validate and extract": a
// prefix with a null namespace is a NamespaceError. So an attribute has a
// prefix ONLY IF it has a namespace, and:
//
//   ns empty     - the whole qualified name is the local name, colons and all.
//                  `el.setAttribute("pre:fix", v)` really does have local name
//                  "pre:fix" and no prefix, and `xml:lang` written on an HTML
//                  element really is one unprefixed attribute called
//                  "xml:lang" - `dom/nodes/Attr-prefix.html` asserts both.
//   ns non-empty - everything before the FIRST colon is the prefix. `a:b:c` is
//                  prefix `a` and local `b:c`, which is the DOM's split and not
//                  the XML QName production's; the two disagree and the DOM is
//                  what a page is measured against.
//
// The views point into the atom table's storage, which is a deque of stable
// strings, so they outlive the call.
[[nodiscard]] inline std::string_view attribute_local_name(const atom_table & atoms,
                                                           const attribute & a) {
    const std::string_view qualified = atoms.text(a.name);
    if (!a.ns) { return qualified; }
    const std::size_t colon = qualified.find(':');
    return colon == std::string_view::npos ? qualified : qualified.substr(colon + 1);
}
[[nodiscard]] inline std::string_view attribute_prefix(const atom_table & atoms,
                                                       const attribute & a) {
    if (!a.ns) { return {}; }
    const std::string_view qualified = atoms.text(a.name);
    const std::size_t colon = qualified.find(':');
    return colon == std::string_view::npos ? std::string_view{} : qualified.substr(0, colon);
}

// Immutable once published. Small-vector because the overwhelming majority of
// elements have a handful of children and one or two attributes, and a heap
// allocation each would dominate document construction.
struct child_list {
    boost::container::small_vector<node_id, 4> items;
};
struct attr_list {
    boost::container::small_vector<attribute, 2> items;
};
struct text_block {
    std::string value;
};

// Shared empties, so a leaf element costs no allocation at all. Never
// published into the epoch domain - `retire_payload` skips them.
inline const child_list empty_children{};
inline const attr_list empty_attributes{};
inline const text_block empty_text{};

struct node {
    node_kind kind = node_kind::element;
    node_ns ns = node_ns::html; // elements only; see node_ns - this is free
    atom tag;                   // elements only

    // 8 bytes and lock-free on every target we care about; a reader that
    // races a reparent sees the old parent or the new one, never a mix.
    std::atomic<node_id> parent{node_id{}};

    std::atomic<const child_list *> children{&empty_children};
    std::atomic<const attr_list *> attributes{&empty_attributes};
    std::atomic<const text_block *> text{&empty_text};

    node() = default;
    explicit node(node_kind k, atom t = {}, node_ns n = node_ns::html) noexcept
        : kind(k), ns(n), tag(t) {}

    // The slab stores these in place; atomics make them immovable anyway.
    node(const node &) = delete;
    node & operator=(const node &) = delete;

    // The namespace field claimed padding that was already there - measured, on
    // this target, before and after adding it. A node is the single most
    // replicated object in the engine, so "free" is worth asserting rather than
    // believing: if a future field pushes this over, that is a real cost and
    // should be a decision rather than a surprise.
    static_assert(sizeof(node_kind) + sizeof(node_ns) <= alignof(atom) + sizeof(atom),
                  "kind and ns must share the padding ahead of `tag`");

    ~node() {
        // Only the shared empties survive a document teardown untouched;
        // anything else was allocated by a publish and is ours to free.
        destroy_payload(children.load(std::memory_order_relaxed));
        destroy_payload(attributes.load(std::memory_order_relaxed));
        destroy_payload(text.load(std::memory_order_relaxed));
    }

    static void destroy_payload(const child_list * p) {
        if (p != &empty_children) { delete p; }
    }
    static void destroy_payload(const attr_list * p) {
        if (p != &empty_attributes) { delete p; }
    }
    static void destroy_payload(const text_block * p) {
        if (p != &empty_text) { delete p; }
    }
};

// Hand a replaced payload block to the epoch domain. The shared empties are
// never retired - they outlive every document.
inline void retire_payload(epoch_domain & domain, const child_list * p) {
    if (p == &empty_children) { return; }
    domain.retire(const_cast<child_list *>(p),
                  [](void * q) { delete static_cast<child_list *>(q); });
}
inline void retire_payload(epoch_domain & domain, const attr_list * p) {
    if (p == &empty_attributes) { return; }
    domain.retire(const_cast<attr_list *>(p), [](void * q) { delete static_cast<attr_list *>(q); });
}
inline void retire_payload(epoch_domain & domain, const text_block * p) {
    if (p == &empty_text) { return; }
    domain.retire(const_cast<text_block *>(p),
                  [](void * q) { delete static_cast<text_block *>(q); });
}

} // namespace ctbrowser
