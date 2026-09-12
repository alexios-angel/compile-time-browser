#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

#include <ctbrowser/core/handle.hpp>

// Stable, generation-tagged storage.
//
// Storage is chunked and the chunk directory is a fixed array, never
// reallocated, so a slot's address is stable for the life of the slab and a
// handle is an index plus a generation. Generations encode liveness in their
// parity - odd is live, even is free - so "is this slot occupied" and "is this
// handle current" are the same check. Generation 0 means never used, which is
// what makes a zeroed handle null.
//
// erase() destroys the object and recycles the slot at once; the generation
// bump is what stops a stale handle resolving to whatever takes the slot next.
//
// SINGLE-THREADED. It was built for concurrent readers under epoch-based
// reclamation (git history, audit CTB-01), which no engine thread ever used;
// the atomics are what that left behind and cost nothing uncontended. A
// pointer from get() is valid until the next erase() of that handle.
//
// A template, so it stays in the header: there is no fixed set of T to
// instantiate it for in one place.

namespace ctbrowser {

template <typename T, typename Tag> class slab {
public:
    using handle_type = handle<Tag>;

    static constexpr std::uint32_t chunk_bits = 12;
    static constexpr std::uint32_t chunk_size = 1u << chunk_bits;
    static constexpr std::uint32_t chunk_mask = chunk_size - 1;
    static constexpr std::size_t max_chunks = 1024; // 4M slots

    slab() = default;
    slab(const slab &) = delete;
    slab & operator=(const slab &) = delete;

    ~slab() {
        // Live slots (odd generation) still hold a constructed T.
        for (std::uint32_t s = 0; s < capacity_.load(std::memory_order_relaxed); ++s) {
            entry * e = locate(s);
            if (e != nullptr && is_live(e->generation.load(std::memory_order_relaxed))) {
                std::destroy_at(value_of(e));
            }
        }
        for (std::atomic<entry *> & c : directory_) { delete[] c.load(std::memory_order_relaxed); }
    }

    // --- reader side ---------------------------------------------------------

    [[nodiscard]] T * get(handle_type h) const noexcept {
        if (!h) { return nullptr; }
        entry * e = locate(h.slot);
        if (e == nullptr) { return nullptr; }
        // The generation check is the whole safety argument: a slot recycled
        // since this handle was made has a different generation, so a stale
        // handle resolves to nullptr instead of to somebody else's object.
        if (e->generation.load(std::memory_order_acquire) != h.generation) { return nullptr; }
        return value_of(e);
    }

    // --- writer side -----------------------------------------------------------

    template <typename... Args> [[nodiscard]] handle_type insert(Args &&... args) {
        const std::uint32_t slot = claim_slot();
        entry * e = locate(slot);
        std::construct_at(value_of(e), std::forward<Args>(args)...);
        // Publishing the generation is what makes the object visible; the
        // construction above must not be reordered after it.
        const std::uint32_t generation = e->generation.load(std::memory_order_relaxed) + 1;
        e->generation.store(generation, std::memory_order_release);
        ++live_;
        return handle_type{slot, generation};
    }

    // Destroy the object and recycle the slot. Every outstanding handle to it
    // stops resolving here.
    bool erase(handle_type h) {
        if (!h) { return false; }
        entry * e = locate(h.slot);
        if (e == nullptr) { return false; }
        if (e->generation.load(std::memory_order_relaxed) != h.generation) { return false; }
        e->generation.store(h.generation + 1, std::memory_order_release); // even == dead
        std::destroy_at(value_of(e));
        e->next_free = free_head_;
        free_head_ = h.slot + 1; // biased by one so 0 can mean "empty"
        --live_;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return live_; }

private:
    struct entry {
        std::atomic<std::uint32_t> generation{0}; // odd = live, even = free, 0 = never used
        std::uint32_t next_free = 0;              // biased by one; 0 = end of list
        alignas(T) std::byte storage[sizeof(T)]{};
    };

    [[nodiscard]] static constexpr bool is_live(std::uint32_t generation) noexcept {
        return (generation & 1u) != 0u;
    }
    [[nodiscard]] static T * value_of(entry * e) noexcept {
        return std::launder(reinterpret_cast<T *>(e->storage));
    }

    [[nodiscard]] entry * locate(std::uint32_t slot) const noexcept {
        const std::size_t chunk = slot >> chunk_bits;
        if (chunk >= max_chunks) { return nullptr; }
        entry * c = directory_[chunk].load(std::memory_order_acquire);
        if (c == nullptr) { return nullptr; }
        return c + (slot & chunk_mask);
    }

    [[nodiscard]] std::uint32_t claim_slot() {
        if (free_head_ != 0) {
            const std::uint32_t slot = free_head_ - 1;
            free_head_ = locate(slot)->next_free;
            return slot;
        }
        const std::uint32_t slot = capacity_.load(std::memory_order_relaxed);
        const std::size_t chunk = slot >> chunk_bits;
        if (directory_[chunk].load(std::memory_order_relaxed) == nullptr) {
            // Published with release so a reader that sees the pointer also
            // sees zero-initialized generations behind it.
            directory_[chunk].store(new entry[chunk_size], std::memory_order_release);
        }
        capacity_.store(slot + 1, std::memory_order_release);
        return slot;
    }

    mutable std::array<std::atomic<entry *>, max_chunks> directory_{};
    std::atomic<std::uint32_t> capacity_{0};
    std::uint32_t free_head_ = 0; // biased by one
    std::size_t live_ = 0;
};

} // namespace ctbrowser
