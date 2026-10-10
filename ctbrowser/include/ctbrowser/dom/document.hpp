//===- document.hpp - The live document ----------------------- -*- C++ -*-===//
//
// The live document: node storage, creation, structural and per-node writes,
// and read_txn, the read view.
//
// Single-threaded, with this contract: a write may invalidate every span and
// string_view a read_txn handed out over the node it wrote. children(),
// attributes(), text() and find_attribute() point into the node's own
// containers, which a write mutates in place. A caller that walks a child or
// attribute list and writes in the loop body copies the list first; a write to
// a different node touches nothing the reader holds.
//
// read_txn is a plain view; the name is kept from an earlier multi-threaded
// design.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/dom/node.hpp>

namespace ctbrowser {

/// Why a DOM read or write failed.
enum class dom_error : std::uint8_t {
    no_such_node,           ///< The handle is stale or was never valid.
    not_an_element,         ///< Attributes and children need an element.
    would_cycle,            ///< Reparenting a node beneath its own descendant.
    is_root,                ///< The root has no parent to detach from.
    invalid_attribute_name, ///< The qualified name cannot round-trip through HTML.
    invalid_shadow_host,    ///< Only supported HTML names may host a shadow tree.
    shadow_root_exists,     ///< An element can host only one shadow tree.
    too_deep,               ///< The tree would reach max_dom_depth (node.hpp).
};

class document;

/// A read view of a document. Free to make; it holds nothing alive. What it
/// hands out is valid until the next write to the node it came from (see the
/// file comment).
class read_txn {
public:
    explicit read_txn(const document & doc) noexcept;

    read_txn(const read_txn &) = delete;
    read_txn & operator=(const read_txn &) = delete;

    [[nodiscard]] bool contains(node_id id) const noexcept;
    [[nodiscard]] std::expected<node_kind, dom_error> kind(node_id) const noexcept;
    [[nodiscard]] std::expected<atom, dom_error> tag(node_id) const noexcept;
    /// The name of a node that has one: an element's tag, a doctype's name, a
    /// processing instruction's target; the empty atom for the rest. Separate
    /// from `tag()` because much of the engine uses "tag().has_value()" to ask
    /// "is this an element", and a doctype must not answer yes.
    [[nodiscard]] atom name(node_id) const noexcept;
    /// A DocumentType's two identifiers; empty for anything else.
    [[nodiscard]] std::string_view public_id(node_id) const noexcept;
    [[nodiscard]] std::string_view system_id(node_id) const noexcept;
    /// Which vocabulary the tag belongs to. An SVG <title> and an HTML <title>
    /// intern to the same atom, so this is the only way to tell a tooltip from
    /// the window title.
    [[nodiscard]] node_ns element_ns(node_id) const noexcept;
    /// Whether a colon in the tag introduces a prefix (see node::prefixed).
    /// local_name() and prefix() follow from it: a prefixed `a:b` is (`b`,
    /// `a`); anything else is (the whole tag, empty).
    [[nodiscard]] bool prefixed(node_id) const noexcept;
    [[nodiscard]] std::string_view local_name(node_id) const noexcept;
    [[nodiscard]] std::string_view prefix(node_id) const noexcept;
    [[nodiscard]] node_id parent(node_id) const noexcept;

    /// The returned span points into the node's own list and is valid until the
    /// next write to that node; copy it before a loop that writes.
    [[nodiscard]] std::span<const node_id> children(node_id) const noexcept;
    [[nodiscard]] std::span<const attribute> attributes(node_id) const noexcept;
    [[nodiscard]] std::string_view text(node_id) const noexcept;

    /// The two lookups answer different questions, as DOM §4.9 intends.
    /// `getAttribute(qualifiedName)` matches the qualified name irrespective of
    /// namespace and answers the first such attribute in order;
    /// `getAttributeNS(ns, localName)` matches the (namespace, local name) pair,
    /// ignoring the prefix. An element may hold `x` in no namespace and `x` in
    /// two others at once, and each lookup has a different right answer (see
    /// WPT dom/nodes/Element-removeAttribute.html).
    [[nodiscard]] std::string_view attribute_value(node_id, atom name) const noexcept;
    [[nodiscard]] bool has_attribute(node_id, atom name) const noexcept;
    /// The attribute itself, so a caller that wants its namespace or value and
    /// its presence walks the list once. The pointer is into the same list
    /// `attributes` returns and is valid until the next write to that node.
    [[nodiscard]] const attribute * find_attribute(node_id, atom name) const noexcept;
    /// `ns` and `local` are text rather than atoms on purpose: a read must not
    /// grow the atom table, and `getAttributeNS` is given whatever URI a page
    /// can spell. The empty string is the null namespace.
    [[nodiscard]] const attribute * find_attribute_ns(node_id, std::string_view ns,
                                                      std::string_view local) const noexcept;
    [[nodiscard]] bool has_attribute_ns(node_id, std::string_view ns,
                                        std::string_view local) const noexcept;
    /// An `other` element's recorded namespace URI; see document::element_namespace.
    [[nodiscard]] atom element_namespace(node_id) const noexcept;
    /// A <template>'s contents fragment; see document::template_content.
    [[nodiscard]] node_id template_content(node_id) const noexcept;
    /// See document::null_custom_element_registry.
    [[nodiscard]] bool null_custom_element_registry(node_id) const noexcept;
    /// See document::custom_element_defined; used by `:defined`.
    [[nodiscard]] std::optional<bool> custom_element_defined(node_id) const;
    /// See document::parser_form.
    [[nodiscard]] node_id parser_form(node_id) const noexcept;

    [[nodiscard]] node_id root() const noexcept;
    [[nodiscard]] node_id document_node() const noexcept;
    [[nodiscard]] std::uint64_t version() const noexcept;

    /// True when `ancestor` is `descendant` itself or one of its ancestors.
    [[nodiscard]] bool is_ancestor_of(node_id ancestor, node_id descendant) const noexcept;
    /// The top of `from`'s tree, continuing through shadow hosts when
    /// `composed`. Like parent(), this stops at the root element rather than
    /// the Document node.
    [[nodiscard]] node_id root_of_tree(node_id from, bool composed = false) const;

private:
    const document * doc_;
};

/// A DOM document: owns every node (in a slab, addressed by node_id) and all
/// per-document state the parsers and the shell record.
class document {
public:
    explicit document(atom_table & atoms);
    ~document();

    document(const document &) = delete;
    document & operator=(const document &) = delete;

    [[nodiscard]] read_txn read() const { return read_txn{*this}; }
    /// The document element (`<html>` on a parsed page), not the Document node
    /// above it: every walk in the style, layout and paint engines starts here
    /// and expects an element. On a document with no element yet
    /// (`new Document()`) it is the Document node itself.
    [[nodiscard]] node_id root() const noexcept { return root_; }
    /// The Document node, whose children are `document.childNodes`: the
    /// doctype, the document element, and any comment or processing
    /// instruction beside them, in document order.
    ///
    /// The document element is in that list but its parent is empty, on
    /// purpose: every engine walks `parent()` to the top and treats the node
    /// with no parent as the root element. The other children do carry the
    /// Document node as their parent (nothing lays them out, and a doctype must
    /// know it is connected). So "is this a child of the Document" is asked of
    /// the child list, never of a parent pointer; see is_document_child.
    [[nodiscard]] node_id document_node() const noexcept { return document_node_; }
    [[nodiscard]] atom_table & atoms() const noexcept { return *atoms_; }
    [[nodiscard]] std::uint64_t version() const noexcept { return version_; }
    /// Records a change that did not bump the version. The parse path
    /// (`builder`, below) bumps none, which is right for a parse nothing
    /// watches but wrong for one a script reads back in the same turn (a
    /// parser-blocking script, `document.write`). A custom element upgrade,
    /// which can make an element a form control, does not bump it either. The
    /// code making such a change calls this so live collections cached by
    /// version see it.
    void note_unversioned_change() noexcept { bump_version(); }
    [[nodiscard]] std::size_t node_count() const noexcept { return nodes_.size(); }

    // --- Creation: the new node is detached until it is appended ----------

    /// `prefixed`: whether a colon in `tag` is a prefix (see node::prefixed).
    [[nodiscard]] node_id create_element(atom tag, node_ns ns = node_ns::html,
                                         bool prefixed = false);
    /// DOM 4.9 "create an element" step 6.1.3.9: a custom element's namespace
    /// prefix is set after its constructor returns, so `this.prefix` is null
    /// inside it. `qualified` is the prefixed tag the element takes on.
    void set_element_prefix(node_id element, atom qualified);
    [[nodiscard]] node_id create_text(std::string_view value);
    [[nodiscard]] node_id create_comment(std::string_view value);
    /// A DocumentFragment. It stays detached: inserting one moves its
    /// children, never the fragment.
    [[nodiscard]] node_id create_fragment();
    /// A DocumentType. The name is an atom because `nodeName` compares it and
    /// the HTML parser lowercases it; the two identifiers are stored in the
    /// node's text and read back through read_txn::public_id and system_id.
    [[nodiscard]] node_id create_document_type(atom name, std::string_view public_id,
                                               std::string_view system_id);
    /// A ProcessingInstruction: the target is the name, the data is the text.
    [[nodiscard]] node_id create_processing_instruction(atom target, std::string_view data);
    /// A CDATASection: a text node that remembers it was one.
    [[nodiscard]] node_id create_cdata_section(std::string_view value);

    // --- Structural writes ---------------------------------------------------

    std::expected<void, dom_error> append_child(node_id parent, node_id child);
    /// Makes `id` the document element. `root()` becomes it, and the Document
    /// node's child list takes it in the previous element's slot or, when there
    /// was none, before `before` or at the end. This is the only way an element
    /// becomes a child of the Document: `append_child(document_node(), el)`
    /// would give it a parent pointer, which the engine's walks must not see
    /// (see document_node).
    void set_document_element(node_id id, node_id before = node_id{});
    /// The reverse of set_document_element: the document element leaves the
    /// Document node's child list and `root()` is the Document node again.
    /// `remove_child` refuses the root because the engine's walks start there;
    /// this is for documents nothing lays out (ones a page made), which DOM
    /// allows. No-op without an element.
    void remove_document_element();
    std::expected<void, dom_error> insert_before(node_id parent, node_id child, node_id before);
    std::expected<void, dom_error> remove_child(node_id child);

    /// The depth cap (max_dom_depth). `up` is the parent, or for a parentless
    /// shadow root or template contents, its host element. `depth` counts `up`
    /// steps to the top and `height` the longest path below `id` (0 for a
    /// leaf, crossing into shadow roots and template contents); both stop at
    /// max_dom_depth, which also bounds a cycle through a template.
    /// append_child and insert_before refuse when `too_deep` holds.
    [[nodiscard]] node_id up(node_id id) const;
    [[nodiscard]] std::size_t depth(node_id id) const;
    [[nodiscard]] std::size_t height(node_id id) const;
    [[nodiscard]] bool too_deep(node_id parent, node_id child) const {
        return depth(parent) + 1 + height(child) >= max_dom_depth;
    }

    // --- Per-node writes -----------------------------------------------------

    /// "Set an attribute value", DOM §4.9.1, in its two forms. Both change an
    /// existing attribute rather than adding a second one; they differ in which
    /// one they find. The qualified form takes the first attribute with that
    /// name whatever its namespace; the namespaced form takes the one with that
    /// (namespace, local name) whatever its prefix. Neither rewrites a prefix
    /// (WPT: "Setting the same attribute with another prefix should not change
    /// the prefix").
    std::expected<void, dom_error> set_attribute(node_id, atom name, std::string_view value);
    /// `name` is the qualified name and `ns` the interned namespace URI; the
    /// local name is derived from the pair (see attribute_local_name).
    std::expected<void, dom_error> set_attribute_ns(node_id, atom ns, atom name,
                                                    std::string_view value);
    /// One whole attribute, namespace included, as copying an element needs: a
    /// clone whose `xlink:href` lost its namespace would be a different
    /// attribute.
    std::expected<void, dom_error> set_attribute(node_id, const attribute & held);
    /// Removes the first attribute with this qualified name, irrespective of
    /// namespace, and only the first, as `removeAttribute` specifies.
    std::expected<void, dom_error> remove_attribute(node_id, atom name);
    std::expected<void, dom_error> remove_attribute_ns(node_id, std::string_view ns,
                                                       std::string_view local);
    /// `edit`, when given, describes this write as a "replace data" (DOM 4.10.2):
    /// offset, count and the replacement's length, in UTF-16 code units, which
    /// live ranges read through the write log below. Without it the write
    /// replaces the whole data.
    /// A "replace data" edit, in UTF-16 code units.
    struct data_edit {
        std::uint32_t offset = 0;
        std::uint32_t count = 0;
        std::uint32_t added = 0;
    };
    std::expected<void, dom_error> set_text(node_id, std::string_view value,
                                            std::optional<data_edit> edit = std::nullopt);

    /// A <template>'s contents (HTML 4.12.3): the DocumentFragment its children
    /// are parsed into, which is not a child of the element, so document
    /// queries do not see them, scripts inside do not run, and nothing lays
    /// them out. Stored here rather than on `node`, the most replicated object
    /// in the engine, because a page has only a few templates. Empty when the
    /// element has none.
    [[nodiscard]] node_id template_content(node_id element) const;
    void set_template_content(node_id element, node_id fragment);
    /// The reverse: the <template> whose contents `fragment` is (its host,
    /// which an insertion into it uses as the context element). Empty for any
    /// other fragment.
    [[nodiscard]] node_id template_of(node_id fragment) const;

    /// The namespace URI of an `other` element, one `node_ns` does not name.
    /// `node` has no room for a URI (see node_ns), and a page holds few of
    /// these (the HTML parser's MathML, an XML document's own vocabulary), so
    /// they are kept here by element for the parsers to record and
    /// `namespaceURI` to read. The empty atom when nothing was recorded; an
    /// HTML or SVG element's URI follows from its `element_ns`.
    [[nodiscard]] atom element_namespace(node_id element) const;
    void set_element_namespace(node_id element, atom uri);

    /// Elements the parser created with a null custom element registry (DOM
    /// 4.9; HTML 13.2.6.1 "create an element for a token" uses the intended
    /// parent's registry). It is null when the element's own start tag has a
    /// `customelementregistry` attribute, or when the intended parent's
    /// registry is null (a declarative shadow root written with
    /// `shadowrootcustomelementregistry`, or an element already in this set).
    /// Everything else the parser makes has the document's registry, which the
    /// shell tracks. Kept here, like the namespaces, because a page has few.
    void set_null_custom_element_registry(node_id element);
    [[nodiscard]] bool null_custom_element_registry(node_id element) const;
    [[nodiscard]] bool has_null_custom_element_registries() const noexcept {
        return !null_registries_.empty();
    }

    /// The form the parser associated a control with (HTML 13.2.6.1 "create an
    /// element for a token", and 4.10.17.3's "parser inserted flag"). A
    /// form-associated element the tree builder made while its form element
    /// pointer was set belongs to that form even when it is not inside it, as
    /// `<table><form><tr><td><input>` builds (a form in a table is popped as
    /// soon as it is inserted). Only those are recorded; a control inside its
    /// form finds it through its ancestors. The association ends when a
    /// removal leaves the two in different trees or the element's `form`
    /// attribute changes (the form owner is reset then); an insertion never
    /// ends it. Empty when nothing was recorded. The form owner is this, when
    /// set, before the walk up to the nearest <form>.
    /// ponytail: a control parsed inside its form and then moved out of it
    /// within the same tree should keep that form per the specification, but
    /// here falls back to its ancestors; recording every control would fix it,
    /// at the cost of a pair per control for every detach to check.
    void set_parser_form(node_id element, node_id form);
    [[nodiscard]] node_id parser_form(node_id element) const noexcept;

    /// A shadow root's metadata. Shadow roots are detached fragments; closed
    /// mode affects exposure to script, not these native tree queries.
    struct shadow_tree {
        node_id host;
        bool open = true;
        /// The rest of `attachShadow(init)`. All are readable back from the
        /// ShadowRoot, and clonable/serializable decide what a clone and a
        /// serialisation contain. `manual_slots` is `slotAssignment: "manual"`:
        /// nothing is assigned to a slot by name and the page calls
        /// `slot.assign()` itself.
        bool delegates_focus = false;
        bool manual_slots = false;
        bool clonable = false;
        bool serializable = false;
        /// The parser attached this root from `<template shadowrootmode>` and
        /// nothing has claimed it yet. `attachShadow` on such a host returns
        /// it emptied instead of refusing, exactly once (the flag is then
        /// cleared).
        bool declarative = false;
        /// `<template shadowrootcustomelementregistry>`: the root's custom
        /// element registry is null and stays null (DOM 4.8 "keep custom
        /// element registry null"); adoption does not give it the new
        /// document's, only CustomElementRegistry.initialize() does.
        bool null_registry = false;
    };
    /// Attaches a shadow root to `host`. `how.host` is ignored.
    [[nodiscard]] std::expected<node_id, dom_error> attach_shadow(node_id host, shadow_tree how);
    [[nodiscard]] std::expected<node_id, dom_error> attach_shadow(node_id host, bool open) {
        return attach_shadow(host, shadow_tree{.host = host, .open = open});
    }
    [[nodiscard]] node_id shadow_root_of(node_id host) const;
    /// Borrowed until the next attach_shadow; the document owns the metadata.
    [[nodiscard]] const shadow_tree * shadow_tree_of(node_id root) const;
    /// Sets shadow_tree::declarative for `root`.
    void set_shadow_declarative(node_id root, bool declarative);
    /// A snapshot, for callers that walk trees and may attach another root.
    [[nodiscard]] std::vector<node_id> shadow_roots() const;
    [[nodiscard]] bool has_shadow_roots() const noexcept { return !shadow_roots_.empty(); }

    // --- The parse path -----------------------------------------------------

    /// The parsers' append: no detach, no cycle check, no version bump, because
    /// every node a parser appends is fresh and nothing observes the document
    /// until it returns. ctcompile's comparator tests also use it to build
    /// deliberately malformed trees, so do not add checks here.
    class builder {
    public:
        explicit builder(document & doc) noexcept : doc_(&doc) {}
        /// Appends `child` to `parent`. Nodes are made by the document's own
        /// create_*, and set_document_element roots them.
        void append(node_id parent, node_id child);
        /// Appends an attribute without looking for a duplicate: the tokenizer
        /// has already deduplicated a start tag's attributes, and this runs
        /// once per attribute of every element.
        ///
        /// This is also where "adjust foreign attributes" happens (see
        /// foreign_namespace_of): it gets the qualified name and its element,
        /// which that step needs, and every parsed attribute passes through.
        void set_attribute(node_id, atom name, std::string_view value);
        /// Moves a node to a new parent, keeping its subtree. The HTML tree
        /// builder's adoption agency algorithm (which turns `<b>1<p>2</b>3`
        /// into what a browser shows) moves already-inserted nodes.
        void reparent(node_id child, node_id new_parent);
        /// Inserts `child` before the sibling `before`. Foster parenting needs
        /// it: content inside a <table> but outside a cell goes immediately
        /// before the table.
        void insert_before(node_id parent, node_id child, node_id before);

    private:
        document * doc_;
    };
    [[nodiscard]] builder build() noexcept { return builder{*this}; }

    /// Quirks mode. `<!DOCTYPE html>` puts the document in standards mode,
    /// and `document.compatMode` reports this flag. True by default, because a
    /// document with no doctype never reaches the tree builder's doctype case
    /// and is in quirks mode by definition.
    ///
    /// Deliberately not wired into the selector engine's case folding: whether
    /// quirks mode changes matching is a separate, render-visible decision;
    /// this flag only reports.
    [[nodiscard]] bool quirks() const noexcept { return quirks_; }
    void set_quirks(bool on) noexcept { quirks_ = on; }

    /// Whether the document was parsed as XML. This is not the same as an
    /// element's vocabulary: `node_ns` says an element is XHTML; this says the
    /// bytes were XML.
    ///
    /// Read by: `nodeName` (uppercases an HTML element's tag only in an HTML
    /// document), `document.contentType` (`application/xhtml+xml` rather than
    /// `text/html`), `compatMode` (always `CSS1Compat`, since XML has no quirks
    /// mode) and `createCDATASection` (allowed only here). dom/xml.hpp's parser
    /// sets it.
    [[nodiscard]] bool xml() const noexcept { return xml_; }
    void set_xml(bool on) noexcept { xml_ = on; }

    /// The document's scripting flag (HTML 13.2.6.4.4): on for a document with
    /// a browsing context, off for one no script will run in (DOMParser's,
    /// createHTMLDocument's). The parser reads it for `<noscript>` (raw text
    /// when on, elements when off), and so must every later fragment parse on
    /// the document, which is why it is stored. `parse_html` records the flag
    /// it was given.
    [[nodiscard]] bool scripting() const noexcept { return scripting_; }
    void set_scripting(bool on) noexcept { scripting_ = on; }

    /// "Allow declarative shadow roots" (HTML 3.1.1): whether the parser makes
    /// a `<template shadowrootmode>` its parent's shadow root. On for a
    /// document a navigation made and for `parseHTMLUnsafe`'s; off (the
    /// specification's default) for DOMParser's, createHTMLDocument's and the
    /// scratch document `innerHTML` parses into. This is the difference between
    /// `innerHTML` and `setHTMLUnsafe`.
    [[nodiscard]] bool allow_declarative_shadow_roots() const noexcept {
        return declarative_shadow_roots_;
    }
    void set_allow_declarative_shadow_roots(bool on) noexcept { declarative_shadow_roots_ = on; }
    /// Answers whether a custom element definition disables shadow roots for
    /// `host` (DOM 4.8 "attach a shadow root" step 3), which the DOM cannot
    /// know itself. The shell that owns the registry installs it; the parser
    /// asks before attaching a declarative shadow root and, on a refusal,
    /// inserts the template as an ordinary one.
    void set_shadow_disabled_hook(std::function<bool(node_id)> hook) {
        shadow_disabled_ = std::move(hook);
    }
    [[nodiscard]] bool shadow_disabled(node_id host) const {
        return shadow_disabled_ && shadow_disabled_(host);
    }
    /// Answers an element's custom element state for `:defined` (HTML 4.13.1:
    /// "defined" means uncustomized or custom). The shell answers for elements
    /// it tracks (true once custom, false while an upgrade is pending or after
    /// one failed) and nullopt for the rest, whose name and `is` decide.
    void set_custom_state_hook(std::function<std::optional<bool>(node_id)> hook) {
        custom_state_ = std::move(hook);
    }
    [[nodiscard]] std::optional<bool> custom_element_defined(node_id element) const {
        return custom_state_ ? custom_state_(element) : std::nullopt;
    }

    /// The name of the document's encoding, `document.characterSet`. The
    /// loader that has the bytes sets it from the BOM or the `<meta charset>`
    /// prescan and decodes by it (dom/encoding.hpp). A document not loaded from
    /// bytes (created, or parsed from a string) is "UTF-8", as the
    /// specifications say. Set before the tree is built, read after.
    [[nodiscard]] const std::string & encoding() const noexcept { return encoding_; }
    void set_encoding(std::string name) { encoding_ = std::move(name); }

private:
    friend class read_txn;

    bool quirks_ = true;
    // False by default; only `parse_xml` sets it.
    bool xml_ = false;
    bool scripting_ = true;
    bool declarative_shadow_roots_ = false;
    std::function<bool(node_id)> shadow_disabled_;             // See set_shadow_disabled_hook.
    std::function<std::optional<bool>(node_id)> custom_state_; // See set_custom_state_hook.
    std::string encoding_ = "UTF-8";

    [[nodiscard]] node * find(node_id id) const noexcept { return nodes_.get(id); }
    void bump_version() noexcept { ++version_; }

    // Detaches `child` from whatever parent it has.
    void detach(node * child_node, node_id child);
    // The parser associations a detach or a `form` attribute write ends.
    void sever_parser_forms();
    void forget_parser_form(node_id element, atom attribute_name);

    // A ProcessingInstruction's attribute map and its data, kept in step both
    // ways (DOM §4.13); see the parser above them in document.cpp.
    void update_pi_attributes(node & n, std::string_view data);
    void update_pi_data(node_id id, node & n);

    // The HTML parser's "adjust foreign attributes" step: why an SVG
    // `xlink:href` is in the XLink namespace while an HTML `xml:lang` is in
    // none. It applies to foreign content only, hence the element namespace
    // parameter: the same qualified name on a `<div>` stays unprefixed and
    // unnamespaced (WPT dom/nodes/Attr-prefix.html checks both).
    //
    // Exactly the specification's ten names; `xml:base` is not one of them and
    // stays in no namespace (html5lib webkit02.dat). On an HTML element it
    // costs one enum compare and returns.
    [[nodiscard]] atom foreign_namespace_of(node_ns element_ns, atom name) const;

    // --- The write log, for the MutationObserver diff ---------------------
    //
    // A write that changes nothing (`setAttribute("x", theSameValue)`,
    // `appendData("")`, `classList.remove(aMissingToken)`) still queues a
    // mutation record, which a before/after diff cannot see. So while logging
    // is on (`log_writes(true)`), every set_attribute* and set_text notes what
    // it wrote, and `take_writes` drains the notes. Off, it costs one load per
    // write. An attribute that is actually removed is noted too, because the
    // attribute change steps run for a removal.
    //
    // Tree edits are logged for live ranges (DOM 5.5): every child inserted or
    // removed is noted with its parent and its index at that moment, in order
    // (a node moved by insert_before is one `removed` and one `inserted`), and
    // a data write carries its "replace data" arguments.
public:
    /// One logged write; see the write log above.
    struct write_note {
        enum class edit : std::uint8_t {
            attribute,
            data,
            inserted,
            removed
        };
        node_id node;      ///< The element or character data written; for a child edit, the parent.
        atom name;         ///< The attribute's qualified name; unused otherwise.
        bool text = false; ///< `kind == data`.
        /// For an attribute: whether it existed before and whether this write
        /// removed it, which HTML's attribute change steps receive as oldValue
        /// and value.
        bool existed = false;
        bool removal = false;
        edit kind = edit::attribute;
        node_id child;           ///< For inserted/removed: which child.
        std::uint32_t index = 0; ///< For inserted/removed: its index in `node`'s children.
        data_edit data;          ///< For data: the replacement, in code units.
    };
    /// Turns the write log on or off.
    void log_writes(bool on);
    /// Returns and clears the logged writes.
    [[nodiscard]] std::vector<write_note> take_writes();

private:
    void note_write(node_id id, atom name, bool text, bool existed = false, bool removal = false);
    void note_edit(write_note::edit kind, node_id parent, node_id child, std::size_t index);
    bool log_writes_ = false;
    std::vector<write_note> writes_log_;

    atom_table * atoms_;
    mutable slab<node, node_tag> nodes_;
    // (element, contents fragment) pairs; see template_content. A vector
    // because a page holds few and a lookup happens once per `.content` read.
    std::vector<std::pair<node_id, node_id>> template_contents_;
    // (control, form) pairs; see parser_form. A vector for the same reason:
    // most pages have none, which every detach checks first.
    std::vector<std::pair<node_id, node_id>> parser_forms_;
    flat_map<std::uint64_t, atom> element_namespaces_;
    flat_map<std::uint64_t, bool> null_registries_;
    flat_map<std::uint64_t, node_id> shadow_roots_;
    flat_map<std::uint64_t, shadow_tree> shadow_hosts_;
    node_id root_{};
    node_id document_node_{};
    std::uint64_t version_ = 1;
};

/// Whether `id` is a child of the Document node. Asked of the child list rather
/// than a parent pointer, because the document element is in the list with no
/// parent (see document::document_node).
[[nodiscard]] inline bool is_document_child(const read_txn & txn, node_id id) noexcept {
    if (!id) { return false; }
    if (txn.parent(id) == txn.document_node()) { return true; }
    return id == txn.root() && txn.kind(id).value_or(node_kind::document) == node_kind::element;
}

/// The parent as the DOM sees it: the Document node for every child of the
/// document, including the document element (whose own parent is empty).
/// `parentNode`, the sibling walks and anything else a page observes use this;
/// the engine's internal walks use `parent()`.
[[nodiscard]] inline node_id dom_parent(const read_txn & txn, node_id id) noexcept {
    return is_document_child(txn, id) ? txn.document_node() : txn.parent(id);
}

} // namespace ctbrowser
