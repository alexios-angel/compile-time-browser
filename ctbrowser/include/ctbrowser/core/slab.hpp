//===- slab.hpp - Generation-tagged storage ------------------- -*- C++ -*-===//
//
// Stable, generation-tagged storage.
//
// Slots live in a std::deque, whose push_back never moves an existing element,
// so a slot's address is stable for the life of the slab and a handle is an
// index plus a generation. Generation parity encodes liveness (odd is live,
// even is free), so "is this slot occupied" and "is this handle current" are
// the same check. Generation 0 means never used, which makes a zeroed handle
// null.
//
// erase() destroys the object and recycles the slot at once; the generation
// bump stops a stale handle resolving to whatever takes the slot next.
//
// Single-threaded: callers must not use a slab from more than one thread. A
// pointer from get() is valid until the next erase() of that handle.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <new>
#include <utility>

#include <ctbrowser/core/handle.hpp>

namespace ctbrowser {

/// Slot-stable storage for T, addressed by handle<Tag>. Single-threaded.
template <typename T, typename Tag> class slab {
public:
    using handle_type = handle<Tag>;

    slab() = default;
    slab(const slab &) = delete;
    slab & operator=(const slab &) = delete;

    ~slab() {
        // Live slots (odd generation) still hold a constructed T.
        for (entry & e : slots_) {
            if (is_live(e.generation)) { std::destroy_at(value_of(&e)); }
        }
    }

    // --- Lookup --------------------------------------------------------------

    /// The object `h` refers to, or nullptr if `h` is null or stale.
    [[nodiscard]] T * get(handle_type h) const noexcept {
        if (!h || h.slot >= slots_.size()) { return nullptr; }
        entry & e = slots_[h.slot];
        // A slot recycled since this handle was made has a different
        // generation, so a stale handle resolves to nullptr instead of to
        // another object.
        if (e.generation != h.generation) { return nullptr; }
        return value_of(&e);
    }

    // --- Mutation --------------------------------------------------------------

    /// Constructs a T from `args` in a free slot and returns its handle.
    /// Allocation and T's constructor may throw; failed construction leaves the
    /// slot reusable.
    template <typename... Args> [[nodiscard]] handle_type insert(Args &&... args) {
        std::uint32_t slot;
        if (free_head_ != 0) {
            slot = free_head_ - 1;
            free_head_ = slots_[slot].next_free;
        } else {
            slot = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
        }
        entry & e = slots_[slot];
        try {
            std::construct_at(reinterpret_cast<T *>(e.storage), std::forward<Args>(args)...);
        } catch (...) {
            e.next_free = free_head_;
            free_head_ = slot + 1;
            throw;
        }
        ++e.generation;
        ++live_;
        return handle_type{slot, e.generation};
    }

    /// Destroys the object and recycles the slot; every outstanding handle to
    /// it stops resolving. Returns false if `h` was null or stale.
    bool erase(handle_type h) {
        if (!h || h.slot >= slots_.size()) { return false; }
        entry & e = slots_[h.slot];
        if (e.generation != h.generation) { return false; }
        e.generation = h.generation + 1; // Even means dead.
        std::destroy_at(value_of(&e));
        e.next_free = free_head_;
        free_head_ = h.slot + 1; // Biased by one so 0 can mean "empty".
        --live_;
        return true;
    }

    /// The number of live objects.
    [[nodiscard]] std::size_t size() const noexcept { return live_; }

private:
    struct entry {
        std::uint32_t generation = 0; // Odd = live, even = free, 0 = never used.
        std::uint32_t next_free = 0;  // Biased by one; 0 = end of list.
        alignas(T) std::byte storage[sizeof(T)]{};
    };

    [[nodiscard]] static constexpr bool is_live(std::uint32_t generation) noexcept {
        return (generation & 1u) != 0u;
    }
    [[nodiscard]] static T * value_of(entry * e) noexcept {
        return std::launder(reinterpret_cast<T *>(e->storage));
    }

    // Mutable because get() is const and hands out a T* the caller may write
    // through: the slab is storage, not an owner of constness.
    mutable std::deque<entry> slots_;
    std::uint32_t free_head_ = 0; // Biased by one.
    std::size_t live_ = 0;
};

} // namespace ctbrowser
