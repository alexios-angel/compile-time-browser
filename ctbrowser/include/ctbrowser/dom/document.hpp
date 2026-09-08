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
#include <vector>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/dom/node.hpp>

// The live document.
//
// LOCKING POLICY, and why it is what it is.
//
// Reads take no locks. Ever. That is the whole point of the RCU payloads in
// :node, and it is the operation an engine does hundreds of millions of times
// per second.
//
// Writes split into two classes, because they have genuinely different
// hazards:
//
//   PER-NODE writes (attributes, text) touch exactly one node. They take that
//   node's stripe lock, so unrelated nodes are mutated fully in parallel.
//
//   STRUCTURAL writes (append, remove, reparent) take ONE document-wide
//   mutex. This is a deliberate simplification and it deserves justification
//   rather than an apology: reparenting concurrently is the classic hard
//   problem in concurrent trees, because preventing a cycle means reasoning
//   about a whole ancestor path that other threads are simultaneously
//   rewriting, and a fine-grained protocol that gets it right is subtle
//   enough to be a research result. Serializing shape changes makes
//   cycle-freedom trivially provable, costs nothing on the read path, and
//   costs nothing on attribute writes either. Structural mutation is rare
//   next to both. If profiling ever shows this mutex mattering, the fix is
//   hand-over-hand path locking - but there is no evidence for that cost yet,
//   and shipping a subtly wrong tree protocol to avoid a mutex nobody is
//   contending on would be a bad trade.
//
// WHAT ATOMICITY YOU GET. Each node's publication is atomic: a reader sees a
// node's old children or its new children, never a mix. A multi-node write is
// NOT atomic as a unit - a reader can observe the child appended to its new
// parent slightly before it observes the removal from the old one. Cross-node
// consistency is what an isolating snapshot is for, and that lands in stage 3
// with the style engine that needs it. Calling this a "snapshot" now would
// promise isolation it does not have, so read_txn is named for what it does:
// it pins the epoch (nothing is destroyed underneath you) and gives per-node
// coherent reads.

namespace ctbrowser {

enum class dom_error : std::uint8_t {
    no_such_node,   // the handle is stale or was never valid
    not_an_element, // attributes and children need an element
    would_cycle,    // reparenting a node beneath its own descendant
    is_root,        // the root has no parent to detach from
};

class document;

// A pinned read view. While one exists, no node and no payload block it can
// reach will be destroyed. Cheap to make - one relaxed load and one store.
class read_txn {
public:
    explicit read_txn(const document & doc) noexcept;

    read_txn(const read_txn &) = delete;
    read_txn & operator=(const read_txn &) = delete;

    [[nodiscard]] bool contains(node_id id) const noexcept;
    [[nodiscard]] std::expected<node_kind, dom_error> kind(node_id) const noexcept;
    [[nodiscard]] std::expected<atom, dom_error> tag(node_id) const noexcept;
    // Which vocabulary the tag belongs to. An SVG <title> and an HTML <title>
    // intern to the SAME atom, so this is the only way to tell a tooltip from
    // the window title.
    [[nodiscard]] node_ns element_ns(node_id) const noexcept;
    [[nodiscard]] node_id parent(node_id) const noexcept;

    // The returned span points into an IMMUTABLE block held alive by this
    // read_txn. It stays valid for the transaction's lifetime, and not one
    // instant longer.
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
    // exactly as long.
    [[nodiscard]] const attribute * find_attribute(node_id, atom name) const noexcept;
    // `ns` and `local` are TEXT rather than atoms on purpose: a read must not be
    // able to grow the atom table, and `getAttributeNS` is handed whatever URI a
    // page can spell. The empty string is the null namespace.
    [[nodiscard]] const attribute * find_attribute_ns(node_id, std::string_view ns,
                                                      std::string_view local) const noexcept;
    [[nodiscard]] bool has_attribute_ns(node_id, std::string_view ns,
                                        std::string_view local) const noexcept;

    [[nodiscard]] node_id root() const noexcept;
    [[nodiscard]] std::uint64_t version() const noexcept;

    // self first, then ancestors
    [[nodiscard]] bool is_ancestor_of(node_id ancestor, node_id descendant) const noexcept;

private:
    const document * doc_;
    epoch_domain::guard guard_;
};

class document {
public:
    explicit document(atom_table & atoms);
    ~document();

    document(const document &) = delete;
    document & operator=(const document &) = delete;

    [[nodiscard]] read_txn read() const { return read_txn{*this}; }
    [[nodiscard]] node_id root() const noexcept { return root_; }
    [[nodiscard]] atom_table & atoms() const noexcept { return *atoms_; }
    [[nodiscard]] std::uint64_t version() const noexcept {
        return version_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::size_t node_count() const noexcept { return nodes_.size(); }

    // --- creation: the new node is DETACHED until it is appended ----------
    [[nodiscard]] node_id create_element(atom tag, node_ns ns = node_ns::html);
    [[nodiscard]] node_id create_text(std::string_view value);
    [[nodiscard]] node_id create_comment(std::string_view value);
    // A DocumentFragment. Detached like everything else here, and it stays
    // detached: inserting one moves its children, never the fragment.
    [[nodiscard]] node_id create_fragment();

    // --- structural writes (document-wide mutex) ---------------------------
    std::expected<void, dom_error> append_child(node_id parent, node_id child);
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

    // Destroy the storage of nodes removed and no longer observable. Callers
    // drive this (typically once per frame) rather than it happening inside a
    // write, so the cost never lands on an interactive mutation.
    std::size_t collect();

    // --- the parse path -----------------------------------------------------
    // Building a document by repeatedly appending through the RCU path would
    // copy the child list on every append: O(n^2) for one element's children,
    // and the whole point of RCU is publication to readers that do not exist
    // yet. `builder` mutates in place instead. It is legal ONLY while nothing
    // else can see the document, which is exactly the case during parsing.
    class builder {
    public:
        explicit builder(document & doc) noexcept : doc_(&doc) {}
        [[nodiscard]] node_id create_element(atom tag, node_ns ns = node_ns::html) {
            return doc_->create_element(tag, ns);
        }
        [[nodiscard]] node_id create_text(std::string_view v) { return doc_->create_text(v); }
        [[nodiscard]] node_id create_comment(std::string_view v) { return doc_->create_comment(v); }
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
        void set_root(node_id id) noexcept { doc_->root_ = id; }

    private:
        document * doc_;
    };
    [[nodiscard]] builder build() noexcept { return builder{*this}; }

    // QUIRKS MODE, THE ONE BIT THE DOCTYPE TOKEN LEAVES BEHIND. Nothing here
    // renders differently for the doctype's name or its identifiers - the tree
    // builder drops the token and says so - but `<!DOCTYPE html>` is what puts
    // the document in STANDARDS mode, and `document.compatMode` is the answer
    // to that and nothing else.
    //
    // TRUE by default, because a document with no doctype at all never reaches
    // the tree builder's doctype case and is in quirks mode by definition.
    //
    // Deliberately NOT wired into the selector engine's case folding: whether
    // quirks mode changes MATCHING is a separate, render-visible decision and
    // this is a reporting one.
    [[nodiscard]] bool quirks() const noexcept { return quirks_.load(std::memory_order_acquire); }
    void set_quirks(bool on) noexcept { quirks_.store(on, std::memory_order_release); }

private:
    friend class read_txn;

    static constexpr std::size_t stripe_count = 256;

    // Atomic for the same reason `version_` is: the tree builder writes it on
    // one thread and a binding reads it on another, and a torn bool is a data
    // race whatever the hardware does about it in practice.
    std::atomic<bool> quirks_{true};

    [[nodiscard]] node * find(node_id id) const noexcept { return nodes_.get(id); }
    [[nodiscard]] std::mutex & stripe_of(node_id id) const noexcept {
        return stripes_[id.slot % stripe_count];
    }
    void bump_version() noexcept { version_.fetch_add(1, std::memory_order_release); }

    // Publish a replacement payload and retire the old one.
    template <typename Payload>
    void publish(std::atomic<const Payload *> & slot, const Payload * fresh) {
        const Payload * stale = slot.exchange(fresh, std::memory_order_release);
        retire_payload(domain_, stale);
    }

    // detach `child` from whatever parent it has; caller holds structure_
    void detach_locked(node * child_node, node_id child);

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

    atom_table * atoms_;
    mutable epoch_domain domain_;
    mutable slab<node, node_tag> nodes_{domain_};
    mutable std::array<std::mutex, stripe_count> stripes_;
    std::mutex structure_; // serializes tree-SHAPE changes; see the policy note
    node_id root_{};
    std::atomic<std::uint64_t> version_{1};
};

// ===================== read_txn ==========================================

// ===================== document ==========================================

// --- builder: in-place, pre-publication ---------------------------------

} // namespace ctbrowser
