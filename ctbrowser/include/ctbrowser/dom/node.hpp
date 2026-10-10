//===- node.hpp - The DOM node -------------------------------- -*- C++ -*-===//
//
// The DOM node, its kinds, element namespaces, attributes and the tree depth
// cap.
//
// A generation-tagged handle (node_id) stops a reader resolving a freed node.
// Children, attributes and text are plain members mutated in place, so a span
// or string_view a read_txn handed out over a node is invalidated by the next
// write to that node; a caller that walks a list while writing copies it first.
//
// Single-threaded: the DOM is read and written only on the frame thread, so
// nodes need no atomics or locks.
//
// What this deliberately does not store: layout rects, text-line caches, widget
// state, selection ranges, caret positions, blink phase. Those are outputs of
// style and layout and belong to the box tree, which is not the DOM tree.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <boost/container/small_vector.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <ctbrowser/core/core.hpp>

namespace ctbrowser {

struct node_tag {};
/// A handle to a node in a document's slab.
using node_id = handle<node_tag>;

/// The deepest a tree may go: no node sits max_dom_depth or more levels below
/// the top of its tree, counting a shadow root and a <template>'s contents as
/// one level below their element. This is Blink's parser cap
/// (kMaximumHTMLParserDOMTreeDepth); the HTML tree builder flattens past it and
/// document::append_child / insert_before refuse to exceed it, so every
/// recursive tree walk downstream of the DOM is bounded by it.
///
/// Past the cap, per caller (no browser caps a script's tree, so no spec says
/// what happens): the HTML parser flattens, the XML parser fails the document
/// ("elements nested too deeply"), appendChild / insertBefore / attachShadow
/// throw HierarchyRequestError (bindings/element/pre_insert.cpp), and every
/// internal insertion that discards append_child / insert_before's result
/// (innerHTML's and textContent's moves, innerText, cloning, slot edits)
/// silently drops the node that would not fit and continues with the rest.
inline constexpr std::size_t max_dom_depth = 512;

/// What a node is; the DOM's nodeType, restricted to what the engine models.
enum class node_kind : std::uint8_t {
    document,
    element,
    text,
    comment,
    /// A DocumentFragment: a parentless set of nodes that a script fills and
    /// then inserts, at which point its children move and the fragment itself
    /// does not. It never reaches style or layout (insertion flattens it).
    document_fragment,
    // The next three kinds are never drawn: none is laid out, styled, matched
    // by a selector or counted as an element, so every switch downstream
    // treats them like a comment (unittests/unit/dom/special_nodes checks
    // this).

    /// A DocumentType: `tag` is its name and `text` its public and system
    /// identifiers (see document::create_document_type). It lives under the
    /// Document node, ahead of the document element.
    document_type,
    /// A ProcessingInstruction, `<?target data?>`: `tag` is the target, `text`
    /// the data, and the attribute list holds its attribute map (DOM 4.13; see
    /// document::update_pi_attributes). Produced by the XML parser,
    /// `createProcessingInstruction` and the HTML tokenizer's processing
    /// instruction states; `<?xml ...?>` in HTML is still a bogus comment.
    processing_instruction,
    /// A CDATASection, `<![CDATA[ data ]]>`: a Text node by inheritance, so
    /// everything that reads text through `text()` reads it too (a `<script>`
    /// written the XML way still runs); only `nodeType` and the serialiser tell
    /// the two apart. XML documents only; `createCDATASection` on an HTML
    /// document is a NotSupportedError.
    cdata_section
};

/// Whether `k` is a Text node in the DOM's sense: Text or its one subclass,
/// CDATASection. Use this instead of `kind == text` so CDATA is never missed.
[[nodiscard]] constexpr bool is_text_kind(node_kind k) noexcept {
    return k == node_kind::text || k == node_kind::cdata_section;
}

/// Which vocabulary an element belongs to. HTML and SVG share a document but
/// not a vocabulary: an SVG <title> is a tooltip and an HTML one is the window
/// title, an SVG <a> is not a link element, and `<text>` means nothing in HTML.
/// Both intern to the same atom, so this is the only record of the
/// difference.
///
/// It costs no space: `node` is kind(1) + tag(4) with three bytes of padding
/// between them, and this occupies that padding.
enum class node_ns : std::uint8_t {
    html,
    svg,
    /// Neither HTML nor SVG: what `document.createElementNS` makes for any
    /// other URI, what the XML parser makes of a page's own vocabulary, and
    /// what the HTML parser makes of MathML (`<math>` and everything under
    /// it). The exact URI is recorded by the document
    /// (document::element_namespace) and, for createElementNS, by the element
    /// wrapper, because `node` is the most replicated object in the engine and
    /// has no room for it. A `mathml` enumerator would cost no space but would
    /// need a matching case in ctcompile's document comparator, which switches
    /// on this enum. What matters to consumers is `element_ns == html`, which
    /// gates script execution, <style> collection and the tagName case fold;
    /// an element in a page-invented namespace must fail all three.
    other
};

/// An attribute. DOM §4.9 defines one as a namespace, a namespace prefix, a
/// local name and a value; this stores two atoms and a string, because the
/// prefix and local name are derivable (attribute_local_name, attribute_prefix).
///
/// `name` is the qualified name: `prefix:local` with a prefix, `local`
/// without. It is the whole name because every reader compares it (the style
/// engine, the layout engine, `getAttribute`), and that must stay one integer
/// compare.
///
/// `ns` is the interned namespace URI. The empty atom is the null namespace,
/// which every attribute in an ordinary HTML document has; `xlink:href` and
/// `xmlns:xlink` inside `<svg>` are the only namespaced ones a parsed page
/// normally carries. An atom rather than a std::string (32 bytes on every
/// attribute for a rare case), and rather than an enum because
/// `setAttributeNS` accepts any URI a page can spell.
///
/// `ns` costs no space: atom(4) + four bytes of padding + std::string(32) left
/// room for it. The static_assert below catches a field that would grow the
/// struct.
struct attribute {
    atom name;
    atom ns;
    std::string value; ///< "" for a boolean attribute, per HTML.

    attribute() = default;
    /// The null namespace, which is what the parser and `setAttribute` produce.
    attribute(atom qualified, std::string v) : name(qualified), value(std::move(v)) {}
    attribute(atom qualified, atom uri, std::string v)
        : name(qualified), ns(uri), value(std::move(v)) {}
};

static_assert(sizeof(attribute) <= sizeof(std::string) + alignof(std::string),
              "the namespace must be FREE: two atoms in the padding ahead of `value`");

/// An attribute's local name and prefix, derived exactly from its qualified
/// name and namespace.
///
/// DOM's "validate and extract" makes a prefix with a null namespace a
/// NamespaceError, so an attribute has a prefix only if it has a namespace:
//
///
///   ns empty     - the whole qualified name is the local name, colons and all.
///                  `el.setAttribute("pre:fix", v)` has local name "pre:fix"
///                  and no prefix, and `xml:lang` written on an HTML element is
///                  one unprefixed attribute called "xml:lang" (WPT
///                  dom/nodes/Attr-prefix.html checks both).
///   ns non-empty - everything before the first colon is the prefix. `a:b:c` is
///                  prefix `a` and local `b:c`, the DOM's split rather than the
///                  XML QName production's.
///
/// The views point into the atom table's stable storage, so they outlive the
/// call.
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

/// One DOM node, stored in place in a document's slab.
struct node {
    node_kind kind = node_kind::element;
    node_ns ns = node_ns::html; ///< Elements only; see node_ns (costs no space).
    /// Elements only, and also stored in padding: whether a colon in `tag`
    /// introduces a prefix. `createElementNS(ns, "a:b")` and an XML-parsed
    /// `<a:b>` have prefix `a` and local name `b`; `createElement("a:b")` and
    /// an HTML-parsed `<a:b>` have no prefix and local name `a:b`. The atom
    /// alone cannot say which, and DOM 4.9 makes both observable.
    bool prefixed = false;
    atom tag; ///< Elements only (and see node_kind for other kinds' uses).

    node_id parent;

    // Small vectors because most elements have a handful of children and one
    // or two attributes, and a heap allocation each would dominate document
    // construction.
    boost::container::small_vector<node_id, 4> children;
    boost::container::small_vector<attribute, 2> attributes;
    std::string text;

    node() = default;
    explicit node(node_kind k, atom t = {}, node_ns n = node_ns::html,
                  bool has_prefix = false) noexcept
        : kind(k), ns(n), prefixed(has_prefix), tag(t) {}

    // The slab stores these in place; nothing copies a node.
    node(const node &) = delete;
    node & operator=(const node &) = delete;

    // A node is the most replicated object in the engine, so assert that the
    // small fields fit in the padding ahead of `tag`; a field that does not is
    // a real cost and should be a deliberate decision.
    static_assert(sizeof(node_kind) + sizeof(node_ns) + sizeof(bool) <=
                      alignof(atom) + sizeof(atom),
                  "kind, ns and prefixed must share the padding ahead of `tag`");
};

} // namespace ctbrowser
