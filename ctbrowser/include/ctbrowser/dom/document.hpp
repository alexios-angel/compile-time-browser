#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <boost/container/small_vector.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/dom/node.hpp>

// The live document.
//
// SINGLE-THREADED, and the contract that follows from it: reads take no
// locks, and a write INVALIDATES every span and string_view a read_txn
// handed out over the node it wrote. children(), attributes(), text() and
// find_attribute() point into an immutable block; a write to that node
// builds a new block, swaps it in and deletes the old one on the spot. A
// caller that walks a child or attribute list and writes in the body copies
// the list to a vector first; a write to a DIFFERENT node touches nothing
// the reader holds.
//
// The stripe locks on per-node writes and the one document-wide mutex on
// structural writes (append, remove, reparent) are what the original
// concurrent design left behind (git history, audit CTB-01: the DOM was
// built for lock-free readers under epoch reclamation, and no engine thread
// ever read it off the frame thread). Uncontended, they cost one atomic
// each and keep the cycle check trivially correct; they are not a promise
// that a second thread may write.
//
// WHAT ATOMICITY YOU GET. Each node's publication is atomic: a reader sees a
// node's old children or its new children, never a mix. A multi-node write is
// NOT atomic as a unit - a reader can observe the child appended to its new
// parent slightly before it observes the removal from the old one. read_txn
// is named for what it does: per-node coherent reads, not an isolating
// snapshot.

namespace ctbrowser {

enum class dom_error : std::uint8_t {
    no_such_node,           // the handle is stale or was never valid
    not_an_element,         // attributes and children need an element
    would_cycle,            // reparenting a node beneath its own descendant
    is_root,                // the root has no parent to detach from
    invalid_attribute_name, // the qualified name cannot round-trip through HTML
};

class document;

// A read view. Free to make; it holds nothing alive. What it hands out is
// valid until the next write to the node it came from - see the contract
// above.
class read_txn {
public:
    explicit read_txn(const document & doc) noexcept;

    read_txn(const read_txn &) = delete;
    read_txn & operator=(const read_txn &) = delete;

    [[nodiscard]] bool contains(node_id id) const noexcept;
    [[nodiscard]] std::expected<node_kind, dom_error> kind(node_id) const noexcept;
    [[nodiscard]] std::expected<atom, dom_error> tag(node_id) const noexcept;
    // THE NAME OF A NODE THAT HAS ONE: an element's tag, a doctype's name, a
    // processing instruction's target; the empty atom for the rest. Separate
    // from `tag()` because "tag().has_value()" is how half the engine asks "is
    // this an element", and a doctype answering it would be counted as one.
    [[nodiscard]] atom name(node_id) const noexcept;
    // A DocumentType's two identifiers; empty for anything else.
    [[nodiscard]] std::string_view public_id(node_id) const noexcept;
    [[nodiscard]] std::string_view system_id(node_id) const noexcept;
    // Which vocabulary the tag belongs to. An SVG <title> and an HTML <title>
    // intern to the SAME atom, so this is the only way to tell a tooltip from
    // the window title.
    [[nodiscard]] node_ns element_ns(node_id) const noexcept;
    // Does a colon in the tag introduce a prefix? See node::prefixed. The two
    // halves of the qualified name follow from it: a prefixed `a:b` is (`a`,
    // `b`) and anything else is (none, the whole tag).
    [[nodiscard]] bool prefixed(node_id) const noexcept;
    [[nodiscard]] std::string_view local_name(node_id) const noexcept;
    [[nodiscard]] std::string_view prefix(node_id) const noexcept;
    [[nodiscard]] node_id parent(node_id) const noexcept;

    // The returned span points into an IMMUTABLE block, valid until the next
    // write to THAT node - copy it before a loop that writes.
    [[nodiscard]] std::span<const node_id> children(node_id) const noexcept;
    [[nodiscard]] std::span<const attribute> attributes(node_id) const noexcept;
    [[nodiscard]] std::string_view text(node_id) const noexcept;

    // THE TWO LOOKUPS ARE NOT THE SAME QUESTION, and DOM §4.9 means them not to
    // be. `getAttribute(qualifiedName)` matches on the QUALIFIED name
    // irrespective of namespace and answers the FIRST such attribute in order;
    // `getAttributeNS(ns, localName)` matches on the PAIR, in which the prefix
    // takes no part. An element may hold `x` in no namespace and `x` in two
    // others at once, and each of the three lookups has a different right
    // answer - which is what `dom/nodes/Element-removeAttribute.html`'s two
    // subtests are about.
    [[nodiscard]] std::string_view attribute_value(node_id, atom name) const noexcept;
    [[nodiscard]] bool has_attribute(node_id, atom name) const noexcept;
    // The attribute itself, so a caller that wants its namespace or its value
    // AND its presence does not pay for the walk twice. The pointer is into the
    // same immutable block `attributes` returns a span over, and is valid for
    // exactly as long: until the next write to that node.
    [[nodiscard]] const attribute * find_attribute(node_id, atom name) const noexcept;
    // `ns` and `local` are TEXT rather than atoms on purpose: a read must not be
    // able to grow the atom table, and `getAttributeNS` is handed whatever URI a
    // page can spell. The empty string is the null namespace.
    [[nodiscard]] const attribute * find_attribute_ns(node_id, std::string_view ns,
                                                      std::string_view local) const noexcept;
    [[nodiscard]] bool has_attribute_ns(node_id, std::string_view ns,
                                        std::string_view local) const noexcept;

    [[nodiscard]] node_id root() const noexcept;
    [[nodiscard]] node_id document_node() const noexcept;
    [[nodiscard]] std::uint64_t version() const noexcept;

    // self first, then ancestors
    [[nodiscard]] bool is_ancestor_of(node_id ancestor, node_id descendant) const noexcept;

private:
    const document * doc_;
};

class document {
public:
    explicit document(atom_table & atoms);
    ~document();

    document(const document &) = delete;
    document & operator=(const document &) = delete;

    [[nodiscard]] read_txn read() const { return read_txn{*this}; }
    // THE DOCUMENT ELEMENT - `<html>` on a parsed page - and not the Document
    // node above it: every walk in the style, layout and paint engines starts
    // here, and every one of them expects an element. On a document that has
    // no element yet (`new Document()`) it is the Document node itself.
    [[nodiscard]] node_id root() const noexcept { return root_; }
    // THE DOCUMENT NODE, whose children are what `document.childNodes` is: the
    // doctype, the document element, and any comment or processing instruction
    // beside them, in document order. THE DOCUMENT ELEMENT IS IN THAT LIST BUT
    // ITS PARENT IS EMPTY - deliberately. `parent()` is the walk every engine
    // takes to the top of the tree, and each of them stops at "no parent" and
    // reads the node it stopped on as the root element; a Document node at the
    // top would be one more node for every such walk to learn about, for no
    // pixel drawn differently. The other children DO carry the Document node
    // as their parent: nothing lays them out, and a doctype has to know it is
    // connected. So "is this a child of the Document" is asked of the child
    // LIST, never of a parent pointer - see is_document_child.
    [[nodiscard]] node_id document_node() const noexcept { return document_node_; }
    [[nodiscard]] atom_table & atoms() const noexcept { return *atoms_; }
    [[nodiscard]] std::uint64_t version() const noexcept {
        return version_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::size_t node_count() const noexcept { return nodes_.size(); }

    // --- creation: the new node is DETACHED until it is appended ----------
    // `prefixed`: whether a colon in `tag` is a prefix - see node::prefixed.
    [[nodiscard]] node_id create_element(atom tag, node_ns ns = node_ns::html,
                                         bool prefixed = false);
    [[nodiscard]] node_id create_text(std::string_view value);
    [[nodiscard]] node_id create_comment(std::string_view value);
    // A DocumentFragment. Detached like everything else here, and it stays
    // detached: inserting one moves its children, never the fragment.
    [[nodiscard]] node_id create_fragment();
    // A DocumentType. The name is an atom because `nodeName` compares it and
    // the HTML parser lowercases it; the two identifiers ride in the node's
    // text block, read back through read_txn::public_id and system_id.
    [[nodiscard]] node_id create_document_type(atom name, std::string_view public_id,
                                               std::string_view system_id);
    // A ProcessingInstruction: the target is the name, the data is the text.
    [[nodiscard]] node_id create_processing_instruction(atom target, std::string_view data);
    // A CDATASection: a text node that remembers it was one.
    [[nodiscard]] node_id create_cdata_section(std::string_view value);

    // --- structural writes (document-wide mutex) ---------------------------
    std::expected<void, dom_error> append_child(node_id parent, node_id child);
    // MAKE `id` THE DOCUMENT ELEMENT. `root()` becomes it, and the Document
    // node's child list takes it in the previous element's slot, or - when
    // there was none - ahead of `before`, or at the end. It is the one way an
    // element becomes a child of the Document: `append_child(document_node(),
    // el)` would give it a parent pointer, which the engine's walks must not
    // see (document_node says why). `root()` stays the Document node itself
    // while there is no element, which is what `new Document()` is.
    void set_document_element(node_id id, node_id before = node_id{});
    // THE OTHER DIRECTION: the document element leaves the Document node's
    // child list and `root()` is the Document node again, as on a document
    // that never had one. `remove_child` refuses the root because the
    // engine's walks start there; a document nothing lays out - one a page
    // made - may do this, and DOM says it may. No-op without an element.
    void remove_document_element();
    std::expected<void, dom_error> insert_before(node_id parent, node_id child, node_id before);
    std::expected<void, dom_error> remove_child(node_id child);

    // --- per-node writes (striped) -----------------------------------------
    //
    // "SET AN ATTRIBUTE VALUE", DOM §4.9.1, in its two spellings. Both CHANGE
    // an existing attribute rather than adding a second one, and which existing
    // attribute they find is the whole difference between them: the qualified
    // form takes the first attribute with that name whatever its namespace, the
    // namespaced form takes the one with that (namespace, local name) whatever
    // its prefix. Neither ever rewrites a prefix - "Setting the same attribute
    // with another prefix should not change the prefix" is a subtest by name.
    std::expected<void, dom_error> set_attribute(node_id, atom name, std::string_view value);
    // `name` is the QUALIFIED name and `ns` the interned namespace URI; the
    // local name is derived from the pair - see attribute_local_name.
    std::expected<void, dom_error> set_attribute_ns(node_id, atom ns, atom name,
                                                    std::string_view value);
    // One WHOLE attribute, namespace and all, which is what copying an element
    // needs: a clone whose `xlink:href` came back in no namespace is a different
    // attribute from the one it was cloned from.
    std::expected<void, dom_error> set_attribute(node_id, const attribute & held);
    // The FIRST attribute with this qualified name, irrespective of namespace -
    // and only the first, which is what `removeAttribute` means.
    std::expected<void, dom_error> remove_attribute(node_id, atom name);
    std::expected<void, dom_error> remove_attribute_ns(node_id, std::string_view ns,
                                                       std::string_view local);
    std::expected<void, dom_error> set_text(node_id, std::string_view value);

    // A <template>'s CONTENTS, HTML 4.12.3: the DocumentFragment its children
    // are parsed into, which is NOT a child of the element - a document query
    // must not see them, a script inside one must not run, and nothing lays
    // them out. The pairing lives here rather than on `node` because a fourth
    // field on the most replicated object in the engine is not free and there
    // are a handful of templates in a page. Empty when the element has none.
    [[nodiscard]] node_id template_content(node_id element) const;
    void set_template_content(node_id element, node_id fragment);

    // --- the parse path -----------------------------------------------------
    // Building a document by repeatedly appending through the published path
    // would copy the child list on every append: O(n^2) for one element's
    // children, to keep spans valid for readers that do not exist yet.
    // `builder` mutates in place instead. It is legal ONLY while nothing else
    // can see the document, which is exactly the case during parsing.
    class builder {
    public:
        explicit builder(document & doc) noexcept : doc_(&doc) {}
        // Nodes are made by the document's own create_* - detached nodes need
        // no in-place path - and set_document_element roots them.
        void append(node_id parent, node_id child);
        // Appends, without looking for a duplicate: a start tag's attribute list
        // has already been deduplicated by the tokenizer and this runs once per
        // attribute of every element in the document.
        //
        // IT IS ALSO WHERE "ADJUST FOREIGN ATTRIBUTES" HAPPENS - see
        // foreign_namespace_of. The tree builder hands over a qualified name and
        // the element it belongs to, which is exactly what that step needs, and
        // this is the one place every parsed attribute passes through.
        void set_attribute(node_id, atom name, std::string_view value);
        // Move a node to a new parent, keeping its own subtree. The HTML tree
        // builder needs it for the adoption agency algorithm - the one that
        // turns `<b>1<p>2</b>3` into what a browser shows - and that algorithm
        // genuinely MOVES already-inserted nodes. Cheap here for the same reason
        // append is: nothing can be reading the document yet.
        void reparent(node_id child, node_id new_parent);
        // Insert BEFORE a sibling. Foster parenting needs it: content that turns
        // up inside a <table> but outside a cell goes immediately before the
        // table, not after it, and "after" puts it below the whole table on
        // screen.
        void insert_before(node_id parent, node_id child, node_id before);

    private:
        document * doc_;
    };
    [[nodiscard]] builder build() noexcept { return builder{*this}; }

    // QUIRKS MODE, THE ONE BIT OF THE DOCTYPE THAT RENDERS. Nothing here
    // renders differently for the doctype's name or its identifiers - the tree
    // builder keeps them on a DocumentType node for `document.doctype` and that
    // is all - but `<!DOCTYPE html>` is what puts the document in STANDARDS
    // mode, and `document.compatMode` is the answer to that and nothing else.
    //
    // TRUE by default, because a document with no doctype at all never reaches
    // the tree builder's doctype case and is in quirks mode by definition.
    //
    // Deliberately NOT wired into the selector engine's case folding: whether
    // quirks mode changes MATCHING is a separate, render-visible decision and
    // this is a reporting one.
    [[nodiscard]] bool quirks() const noexcept { return quirks_.load(std::memory_order_acquire); }
    void set_quirks(bool on) noexcept { quirks_.store(on, std::memory_order_release); }

    // WHICH LANGUAGE THIS DOCUMENT WAS WRITTEN IN, which is not the same
    // question as which vocabulary an element belongs to. `node_ns` says an
    // element is XHTML; this says the BYTES were XML, and the two differ in
    // every place the HTML parser is permissive and XML is not.
    //
    // What reads it: `nodeName` uppercases an HTML element's tag only in an
    // HTML document, `document.contentType` is `application/xhtml+xml` rather
    // than `text/html`, `compatMode` is always `CSS1Compat` because an XML
    // document has no quirks mode to be in, and `createCDATASection` is only
    // allowed here. See dom/xml.hpp for the parser that sets it.
    [[nodiscard]] bool xml() const noexcept { return xml_.load(std::memory_order_acquire); }
    void set_xml(bool on) noexcept { xml_.store(on, std::memory_order_release); }

private:
    friend class read_txn;

    static constexpr std::size_t stripe_count = 256;

    // Atomic for the same reason `version_` is: the tree builder writes it on
    // one thread and a binding reads it on another, and a torn bool is a data
    // race whatever the hardware does about it in practice.
    std::atomic<bool> quirks_{true};
    // FALSE by default: every document this engine has ever built came from the
    // HTML tree builder, and `parse_xml` is the only thing that sets it.
    std::atomic<bool> xml_{false};

    [[nodiscard]] node * find(node_id id) const noexcept { return nodes_.get(id); }
    [[nodiscard]] std::mutex & stripe_of(node_id id) const noexcept {
        return stripes_[id.slot % stripe_count];
    }
    void bump_version() noexcept { version_.fetch_add(1, std::memory_order_release); }

    // Publish a replacement payload and delete the old one - which is what
    // invalidates every span a read_txn handed out over this node.
    template <typename Payload>
    void publish(std::atomic<const Payload *> & slot, const Payload * fresh) {
        node::destroy_payload(slot.exchange(fresh, std::memory_order_release));
    }

    // detach `child` from whatever parent it has; caller holds structure_
    void detach_locked(node * child_node, node_id child);

    // A ProcessingInstruction's attribute map and its data, kept in step both
    // ways (DOM §4.13) - see the parser above them in document.cpp. Caller
    // holds the node's stripe.
    void update_pi_attributes(node & n, std::string_view data);
    void update_pi_data(node_id id, node & n);

    // "ADJUST FOREIGN ATTRIBUTES", the HTML parser's own step, and the reason
    // an SVG `xlink:href` is in the XLink namespace while an HTML `xml:lang` is
    // in none. It applies to FOREIGN CONTENT ONLY, which is why it takes the
    // element's namespace: the same qualified name written on a `<div>` stays
    // unprefixed and unnamespaced, and `dom/nodes/Attr-prefix.html` asserts
    // both halves against each other.
    //
    // The specification lists ten names; this matches the three PREFIXES those
    // ten use, which differs only for something like `xlink:actuate`'s
    // unlisted neighbours - and putting `xlink:anything` in the XLink namespace
    // is what an author writing it means. Costs one enum compare on every
    // attribute of an HTML document, which is where it returns.
    [[nodiscard]] atom foreign_namespace_of(node_ns element_ns, atom name) const;

    // --- the write log, for the MutationObserver diff ---------------------
    //
    // A write that changes nothing - `setAttribute("x", theSameValue)`,
    // `appendData("")`, `classList.remove(aMissingToken)` - still queues a
    // mutation record, and a diff of before against after cannot see it. So
    // while a reader has asked (`log_writes(true)`), every set_attribute* and
    // set_text notes what it wrote, and `take_writes` drains the notes. Off,
    // it costs one load per write.
public:
    struct write_note {
        node_id node;
        atom name; // the attribute's qualified name; unused for a text write
        bool text = false;
    };
    void log_writes(bool on);
    [[nodiscard]] std::vector<write_note> take_writes();

private:
    void note_write(node_id id, atom name, bool text);
    std::atomic<bool> log_writes_{false};
    std::mutex writes_;
    std::vector<write_note> writes_log_;

    atom_table * atoms_;
    mutable slab<node, node_tag> nodes_;
    mutable std::array<std::mutex, stripe_count> stripes_;
    std::mutex structure_; // serializes tree-SHAPE changes; see the policy note
    // (element, contents fragment) pairs - see template_content. A vector
    // because a page holds a few and a lookup happens once per `.content`
    // read. ONE mutex for the whole vector rather than the element's stripe:
    // two templates are on two stripes, and an emplace_back under either of
    // them would race the other's walk.
    mutable std::mutex templates_;
    std::vector<std::pair<node_id, node_id>> template_contents_;
    node_id root_{};
    node_id document_node_{};
    std::atomic<std::uint64_t> version_{1};
};

// IS THIS NODE A CHILD OF THE DOCUMENT? Asked of the child list rather than a
// parent pointer, because the document element sits in the list with no
// parent - see document::document_node for why.
[[nodiscard]] inline bool is_document_child(const read_txn & txn, node_id id) noexcept {
    if (!id) { return false; }
    if (txn.parent(id) == txn.document_node()) { return true; }
    return id == txn.root() && txn.kind(id).value_or(node_kind::document) == node_kind::element;
}

// THE PARENT AS THE DOM SEES IT: the Document node for every child of the
// document, the document element included - whose own pointer is empty, see
// above. `parentNode`, the sibling walks and anything else a page observes
// asks this; the engine's walks keep asking `parent()`.
[[nodiscard]] inline node_id dom_parent(const read_txn & txn, node_id id) noexcept {
    return is_document_child(txn, id) ? txn.document_node() : txn.parent(id);
}

} // namespace ctbrowser
