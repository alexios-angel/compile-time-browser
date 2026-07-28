module;
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <vector>

export module ctbrowser.core:epoch;

// Epoch-based reclamation: the reason DOM reads take no locks.
//
// The problem it solves: a reader walking the tree holds a pointer to a node
// that a writer on another thread may unlink at any moment. Locking every node
// to prevent that is the throughput sink we are avoiding. Instead:
//
//   * Removal is SPLIT. Unlinking is immediate - the moment a writer commits,
//     no NEW reader can reach the node. Destruction is deferred.
//   * A reader announces itself by publishing the current epoch for as long as
//     it is inside the tree.
//   * A retired node is destroyed only once every active reader announced an
//     epoch LATER than the retirement. Such a reader began after the unlink,
//     so it cannot be holding the node. Readers that began earlier have all
//     finished.
//
// Cost on the read path: one relaxed load and two stores per critical section.
// No contention, no cache-line ping-pong between readers (each participant
// gets its own line).
//
// The orderings here are deliberately conservative - seq_cst on the announce
// and on the scan. Getting this subtly wrong produces a use-after-free that
// appears under load once a week, so the tuning to acquire/release plus
// explicit fences is left until the TSan stress suite is established and can
// prove the change.

export namespace ctbrowser {

// A process-wide, reusable thread index. Reusable matters: a naive
// fetch_add-per-thread leaks indices, so a program that churns threads would
// eventually exhaust the participant table.
class thread_registry {
public:
	static constexpr std::size_t max_threads = 256;

	[[nodiscard]] static std::size_t index() {
		thread_local slot_lease lease;
		return lease.index;
	}

private:
	struct slot_lease {
		std::size_t index;
		slot_lease() : index(acquire_slot()) {}
		~slot_lease() { release_slot(index); }
	};

	[[nodiscard]] static std::mutex & mutex() {
		static std::mutex m;
		return m;
	}
	[[nodiscard]] static std::vector<std::size_t> & free_list() {
		static std::vector<std::size_t> f;
		return f;
	}
	[[nodiscard]] static std::size_t acquire_slot() {
		const std::lock_guard lock{mutex()};
		if (!free_list().empty()) {
			const std::size_t i = free_list().back();
			free_list().pop_back();
			return i;
		}
		static std::size_t next = 0;
		return next < max_threads ? next++ : max_threads - 1;
	}
	static void release_slot(std::size_t i) {
		const std::lock_guard lock{mutex()};
		free_list().push_back(i);
	}
};

class epoch_domain {
public:
	static constexpr std::uint64_t inactive = 0;

	// RAII critical section. While one of these is alive on a thread, nothing
	// retired after it began can be destroyed.
	class guard {
	public:
		explicit guard(epoch_domain & domain) noexcept
		    : domain_(&domain), slot_(thread_registry::index()) {
			// Publish BEFORE reading anything. A reclaiming writer that misses
			// this store could free a node this reader is about to touch, so
			// the store must not sink below the reads that follow it.
			domain_->participants_[slot_].announced.store(
			    domain_->epoch_.load(std::memory_order_relaxed), std::memory_order_seq_cst);
		}
		~guard() {
			domain_->participants_[slot_].announced.store(inactive, std::memory_order_seq_cst);
		}
		guard(const guard &) = delete;
		guard & operator=(const guard &) = delete;

	private:
		epoch_domain * domain_;
		std::size_t slot_;
	};

	[[nodiscard]] guard pin() noexcept { return guard{*this}; }
	[[nodiscard]] std::uint64_t epoch() const noexcept {
		return epoch_.load(std::memory_order_relaxed);
	}

	// Hand over an object that has ALREADY been unlinked. Ownership passes to
	// the domain; `destroy` runs once no reader can still reach it.
	void retire(void * object, void (*destroy)(void *)) {
		const std::lock_guard lock{retired_mutex_};
		retired_.push_back({epoch_.load(std::memory_order_relaxed), object, destroy});
	}

	// The epoch of the longest-running reader still inside the tree, or "no
	// readers at all". Anything retired STRICTLY BEFORE this is unreachable.
	//
	// Exposed because containers with their own storage (the slab recycles
	// slots rather than freeing memory) need this answer without duplicating
	// the participant scan.
	[[nodiscard]] std::uint64_t oldest_active() const noexcept {
		std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
		for (const participant & p : participants_) {
			const std::uint64_t announced = p.announced.load(std::memory_order_seq_cst);
			if (announced != inactive) { oldest = std::min(oldest, announced); }
		}
		return oldest;
	}

	// Bump the epoch so that everything retired up to now is strictly older
	// than anything a reader can announce from here on.
	std::uint64_t advance() noexcept { return epoch_.fetch_add(1, std::memory_order_seq_cst) + 1; }

	// Destroy everything now provably unreachable. Called by writers, not
	// readers. Returns how many objects were destroyed.
	std::size_t reclaim() {
		advance();
		const std::uint64_t oldest_reader = oldest_active();

		std::vector<entry> doomed;
		{
			const std::lock_guard lock{retired_mutex_};
			const auto split =
			    std::partition(retired_.begin(), retired_.end(), [oldest_reader](const entry & e) {
				    return e.retired_at >= oldest_reader;
			    });
			doomed.assign(std::make_move_iterator(split), std::make_move_iterator(retired_.end()));
			retired_.erase(split, retired_.end());
		}
		for (const entry & e : doomed) { e.destroy(e.object); }
		return doomed.size();
	}

	// how many objects are retired but not yet destroyed
	[[nodiscard]] std::size_t pending() const {
		const std::lock_guard lock{retired_mutex_};
		return retired_.size();
	}

	// Destroying the domain destroys whatever is still retired. Without this
	// every object retired since the last reclaim() simply leaks, which is
	// easy to miss because the common path does call reclaim() eventually -
	// ASan caught it only once the stress test started reclaiming mid-run.
	// No reader can exist here: the domain is going away, so anything holding
	// a guard into it would already be a lifetime bug.
	~epoch_domain() {
		for (const entry & e : retired_) { e.destroy(e.object); }
	}

	epoch_domain() = default;
	epoch_domain(const epoch_domain &) = delete;
	epoch_domain & operator=(const epoch_domain &) = delete;

private:
	struct entry {
		std::uint64_t retired_at;
		void * object;
		void (*destroy)(void *);
	};
	// one cache line each: readers announcing must not invalidate each other
	struct alignas(64) participant {
		std::atomic<std::uint64_t> announced{inactive};
	};

	std::array<participant, thread_registry::max_threads> participants_{};
	std::atomic<std::uint64_t> epoch_{1};
	mutable std::mutex retired_mutex_;
	std::vector<entry> retired_;
};

} // namespace ctbrowser
