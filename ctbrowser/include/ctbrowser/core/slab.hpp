#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <new>
#include <utility>

#include <ctbrowser/core/handle.hpp>

// Stable, generation-tagged storage.
//
// Slots live in a std::deque, whose push_back never moves an existing element,
// so a slot's address is stable for the life of the slab and a handle is an
// index plus a generation. Generations encode liveness in their parity - odd
// is live, even is free - so "is this slot occupied" and "is this handle
// current" are the same check. Generation 0 means never used, which is what
// makes a zeroed handle null.
//
// erase() destroys the object and recycles the slot at once; the generation
// bump is what stops a stale handle resolving to whatever takes the slot next.
//
// SINGLE-THREADED. It was built for concurrent readers under epoch-based
// reclamation (git history, audit CTB-01), which no engine thread ever used;
// the hand-rolled chunk directory of atomics that design needed went with it.
// A pointer from get() is valid until the next erase() of that handle.
//
// A template, so it stays in the header: there is no fixed set of T to
// instantiate it for in one place.

namespace ctbrowser {

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

    // --- reader side ---------------------------------------------------------

    [[nodiscard]] T * get(handle_type h) const noexcept {
        if (!h || h.slot >= slots_.size()) { return nullptr; }
        entry & e = slots_[h.slot];
        // The generation check is the whole safety argument: a slot recycled
        // since this handle was made has a different generation, so a stale
        // handle resolves to nullptr instead of to somebody else's object.
        if (e.generation != h.generation) { return nullptr; }
        return value_of(&e);
    }

    // --- writer side -----------------------------------------------------------

    // Allocation and T's constructor may throw. Failed construction leaves the
    // slot reusable.
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

    // Destroy the object and recycle the slot. Every outstanding handle to it
    // stops resolving here.
    bool erase(handle_type h) {
        if (!h || h.slot >= slots_.size()) { return false; }
        entry & e = slots_[h.slot];
        if (e.generation != h.generation) { return false; }
        e.generation = h.generation + 1; // even == dead
        std::destroy_at(value_of(&e));
        e.next_free = free_head_;
        free_head_ = h.slot + 1; // biased by one so 0 can mean "empty"
        --live_;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return live_; }

private:
    struct entry {
        std::uint32_t generation = 0; // odd = live, even = free, 0 = never used
        std::uint32_t next_free = 0;  // biased by one; 0 = end of list
        alignas(T) std::byte storage[sizeof(T)]{};
    };

    [[nodiscard]] static constexpr bool is_live(std::uint32_t generation) noexcept {
        return (generation & 1u) != 0u;
    }
    [[nodiscard]] static T * value_of(entry * e) noexcept {
        return std::launder(reinterpret_cast<T *>(e->storage));
    }

    // mutable because get() is const and hands out a T* the caller may write
    // through: the slab is storage, not an owner of constness.
    mutable std::deque<entry> slots_;
    std::uint32_t free_head_ = 0; // biased by one
    std::size_t live_ = 0;
};

} // namespace ctbrowser
