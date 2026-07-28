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
    [[nodiscard]] node_id parent(node_id) const noexcept;

    // The returned span points into an IMMUTABLE block held alive by this
    // read_txn. It stays valid for the transaction's lifetime, and not one
    // instant longer.
    [[nodiscard]] std::span<const node_id> children(node_id) const noexcept;
    [[nodiscard]] std::span<const attribute> attributes(node_id) const noexcept;
    [[nodiscard]] std::string_view text(node_id) const noexcept;

    [[nodiscard]] std::string_view attribute_value(node_id, atom name) const noexcept;
    [[nodiscard]] bool has_attribute(node_id, atom name) const noexcept;

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
    [[nodiscard]] node_id create_element(atom tag);
    [[nodiscard]] node_id create_text(std::string_view value);
    [[nodiscard]] node_id create_comment(std::string_view value);

    // --- structural writes (document-wide mutex) ---------------------------
    std::expected<void, dom_error> append_child(node_id parent, node_id child);
    std::expected<void, dom_error> insert_before(node_id parent, node_id child, node_id before);
    std::expected<void, dom_error> remove_child(node_id child);

    // --- per-node writes (striped) -----------------------------------------
    std::expected<void, dom_error> set_attribute(node_id, atom name, std::string_view value);
    std::expected<void, dom_error> remove_attribute(node_id, atom name);
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
        [[nodiscard]] node_id create_element(atom tag) { return doc_->create_element(tag); }
        [[nodiscard]] node_id create_text(std::string_view v) { return doc_->create_text(v); }
        [[nodiscard]] node_id create_comment(std::string_view v) { return doc_->create_comment(v); }
        void append(node_id parent, node_id child);
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

private:
    friend class read_txn;

    static constexpr std::size_t stripe_count = 256;

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
