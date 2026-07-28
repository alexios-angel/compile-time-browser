module;
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>
#include <vector>

export module ctbrowser.core:slab;

import :epoch;
import :handle;

// Stable, generation-tagged storage with LOCK-FREE reads.
//
// Storage is chunked and the chunk directory is a fixed array of atomic
// pointers, never reallocated. That is stricter than the reference stability a
// stable_vector would give: a reader indexing by slot must see a stable
// slot -> address mapping, and a container that reallocates its own internal
// index array cannot promise that even when element addresses are stable.
// So this is hand-rolled rather than built on boost::container::stable_vector,
// which the plan had pencilled in.
//
// Removal is split, per the epoch design:
//
//   erase()   bumps the generation, so every existing handle stops resolving
//             IMMEDIATELY and no new reader can reach the object;
//   collect() destroys the object and recycles the slot, once the epoch domain
//             proves no reader from before the erase is still running.
//
// Generations encode liveness in their parity - odd is live, even is free -
// so "is this slot occupied" and "is this handle current" are the same check.
// Generation 0 means never used, which is what makes a zeroed handle null.
//
// CONCURRENCY CONTRACT: many concurrent readers, ONE writer at a time.
// Structural operations (insert/erase/collect) must be serialized by the
// caller. In the engine that serialization is the DOM's write transaction;
// the slab does not lock, so it does not pay for locking on the read path.

export namespace ctbrowser {

template <typename T, typename Tag, std::uint32_t ChunkBits = 12> class slab {
public:
	using handle_type = handle<Tag>;

	static constexpr std::uint32_t chunk_size = 1u << ChunkBits;
	static constexpr std::uint32_t chunk_mask = chunk_size - 1;
	static constexpr std::size_t max_chunks = 1024; // 4M slots at the default chunk size

	explicit slab(epoch_domain & domain) noexcept : domain_(&domain) {}

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
		// So do slots that were ERASED but never collected: erase() only marks
		// them dead, and destruction is deferred to collect(). They read as
		// even-generation and are therefore invisible to the loop above, so
		// without this they leak whatever T owned.
		for (const deferred & d : pending_) {
			if (entry * e = locate(d.slot)) { std::destroy_at(value_of(e)); }
		}
		for (std::atomic<entry *> & c : directory_) { delete[] c.load(std::memory_order_relaxed); }
	}

	// --- reader side: lock-free, safe to call from any thread inside an
	// epoch_domain::guard --------------------------------------------------

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

	[[nodiscard]] bool contains(handle_type h) const noexcept { return get(h) != nullptr; }

	// --- writer side: caller-serialized ------------------------------------

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

	// Logical removal: immediate. Physical destruction: deferred to collect().
	bool erase(handle_type h) {
		if (!h) { return false; }
		entry * e = locate(h.slot);
		if (e == nullptr) { return false; }
		if (e->generation.load(std::memory_order_relaxed) != h.generation) { return false; }
		// Even generation == dead. Every outstanding handle stops resolving here.
		e->generation.store(h.generation + 1, std::memory_order_release);
		--live_;
		pending_.push_back({domain_->epoch(), h.slot});
		return true;
	}

	// Destroy and recycle everything no reader can still be looking at.
	std::size_t collect() {
		domain_->advance();
		const std::uint64_t oldest = domain_->oldest_active();
		std::size_t reclaimed = 0;
		std::size_t keep = 0;
		for (std::size_t i = 0; i < pending_.size(); ++i) {
			const deferred d = pending_[i];
			if (d.retired_at >= oldest) {
				pending_[keep++] = d; // a reader from before the erase is still running
				continue;
			}
			entry * e = locate(d.slot);
			std::destroy_at(value_of(e));
			e->next_free = free_head_;
			free_head_ = d.slot + 1; // biased by one so 0 can mean "empty"
			++reclaimed;
		}
		pending_.resize(keep);
		return reclaimed;
	}

	[[nodiscard]] std::size_t size() const noexcept { return live_; }
	[[nodiscard]] std::size_t pending() const noexcept { return pending_.size(); }
	[[nodiscard]] std::uint32_t capacity() const noexcept {
		return capacity_.load(std::memory_order_relaxed);
	}

private:
	struct entry {
		std::atomic<std::uint32_t> generation{0}; // odd = live, even = free, 0 = never used
		std::uint32_t next_free = 0;              // biased by one; 0 = end of list
		alignas(T) std::byte storage[sizeof(T)]{};
	};
	struct deferred {
		std::uint64_t retired_at;
		std::uint32_t slot;
	};

	[[nodiscard]] static constexpr bool is_live(std::uint32_t generation) noexcept {
		return (generation & 1u) != 0u;
	}
	[[nodiscard]] static T * value_of(entry * e) noexcept {
		return std::launder(reinterpret_cast<T *>(e->storage));
	}

	[[nodiscard]] entry * locate(std::uint32_t slot) const noexcept {
		const std::size_t chunk = slot >> ChunkBits;
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
		const std::size_t chunk = slot >> ChunkBits;
		if (directory_[chunk].load(std::memory_order_relaxed) == nullptr) {
			// Published with release so a reader that sees the pointer also
			// sees zero-initialized generations behind it.
			directory_[chunk].store(new entry[chunk_size], std::memory_order_release);
		}
		capacity_.store(slot + 1, std::memory_order_release);
		return slot;
	}

	epoch_domain * domain_;
	mutable std::array<std::atomic<entry *>, max_chunks> directory_{};
	std::atomic<std::uint32_t> capacity_{0};
	std::uint32_t free_head_ = 0; // biased by one
	std::size_t live_ = 0;
	std::vector<deferred> pending_;
};

} // namespace ctbrowser
