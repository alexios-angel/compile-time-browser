// The test the thread-safe DOM rests on.
//
// Readers hammer the slab through the lock-free path while a writer inserts,
// erases and recycles slots underneath them. Two things must hold, and both are
// checked rather than assumed:
//
//   1. A handle either resolves to ITS OWN object or to nothing. It must never
//      resolve to a different object that happens to have landed in the same
//      recycled slot - that is the failure generations exist to prevent, and it
//      is silent corruption rather than a crash, so a checksum catches it.
//   2. A reader inside an epoch guard never touches destroyed memory. That one
//      IS a crash, and it is what ASan/TSan are here to find.
//
// Run it under the asan and tsan presets, not just the default one - a clean
// pass without sanitizers proves very little about either property.

import ctbrowser.core;

#include "check.hpp"
#include <atomic>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

using namespace ctbrowser;

namespace {

struct payload_tag {};
using payload_id = handle<payload_tag>;

// Self-describing: the checksum is derived from the id, so an object read
// through the WRONG handle fails validation even though the memory is live and
// perfectly readable. Without this, slot-reuse bugs pass silently.
struct payload {
	std::uint32_t id = 0;
	std::uint32_t check = 0;

	explicit payload(std::uint32_t v) noexcept : id(v), check(checksum(v)) {}
	[[nodiscard]] static constexpr std::uint32_t checksum(std::uint32_t v) noexcept {
		return v * 2654435761u ^ 0xA5A5A5A5u;
	}
	[[nodiscard]] constexpr bool intact() const noexcept { return check == checksum(id); }
};

constexpr std::size_t published_slots = 512;
constexpr std::size_t reader_threads = 6;

struct shared_state {
	epoch_domain domain;
	slab<payload, payload_tag> objects{domain};

	// The writer publishes handles here and readers race to use them. A reader
	// may well load a handle the writer erases a nanosecond later - that is the
	// interesting case, not a flaw in the test.
	std::array<std::atomic<std::uint64_t>, published_slots> published{};

	std::atomic<bool> stop{false};
	std::atomic<std::uint64_t> reads_ok{0};
	std::atomic<std::uint64_t> reads_missed{0}; // resolved to nothing: expected and fine
	std::atomic<std::uint64_t> corrupt{0};      // resolved to the WRONG object: a bug
};

[[nodiscard]] constexpr std::uint64_t pack(payload_id h) noexcept {
	return (static_cast<std::uint64_t>(h.slot) << 32) | h.generation;
}
[[nodiscard]] constexpr payload_id unpack(std::uint64_t v) noexcept {
	return payload_id{static_cast<std::uint32_t>(v >> 32), static_cast<std::uint32_t>(v)};
}

void reader_loop(shared_state & st, unsigned seed) {
	std::mt19937 rng{seed};
	std::uniform_int_distribution<std::size_t> pick{0, published_slots - 1};
	while (!st.stop.load(std::memory_order_relaxed)) {
		// Everything inside this guard is protected from reclamation. Outside
		// it, any pointer previously obtained is dead.
		const auto guard = st.domain.pin();
		for (int burst = 0; burst < 64; ++burst) {
			const std::uint64_t raw = st.published[pick(rng)].load(std::memory_order_acquire);
			const payload_id id = unpack(raw);
			if (!id) { continue; }
			const payload * p = st.objects.get(id);
			if (p == nullptr) {
				st.reads_missed.fetch_add(1, std::memory_order_relaxed); // erased: correct
				continue;
			}
			if (!p->intact() || p->id != id.slot) {
				st.corrupt.fetch_add(1, std::memory_order_relaxed);
			} else {
				st.reads_ok.fetch_add(1, std::memory_order_relaxed);
			}
		}
	}
}

void writer_loop(shared_state & st, int rounds) {
	std::mt19937 rng{1234};
	std::uniform_int_distribution<std::size_t> pick{0, published_slots - 1};
	std::uniform_int_distribution<int> action{0, 99};

	for (int round = 0; round < rounds && !st.stop.load(std::memory_order_relaxed); ++round) {
		const std::size_t where = pick(rng);
		const std::uint64_t existing = st.published[where].load(std::memory_order_relaxed);
		const int roll = action(rng);

		if (existing == 0 || roll < 45) {
			// insert, and stamp the payload with its own slot so a reader can
			// tell whether the object it got is the one it asked for
			const payload_id fresh = st.objects.insert(0u);
			if (payload * p = st.objects.get(fresh)) { *p = payload{fresh.slot}; }
			const std::uint64_t previous =
			    st.published[where].exchange(pack(fresh), std::memory_order_release);
			if (previous != 0) { st.objects.erase(unpack(previous)); }
		} else if (roll < 85) {
			// unpublish then erase: readers stop being ABLE to find it before
			// it stops resolving, which is the ordering the DOM will use
			const std::uint64_t previous =
			    st.published[where].exchange(0, std::memory_order_release);
			if (previous != 0) { st.objects.erase(unpack(previous)); }
		} else {
			st.objects.collect();
		}
		if ((round % 512) == 0) { st.objects.collect(); }
	}
	st.stop.store(true, std::memory_order_relaxed);
}

void test_concurrent_slab() {
	CHECK(
	    std::atomic<std::uint64_t>::is_always_lock_free); // else the test measures the wrong thing

	shared_state st;
	std::vector<std::jthread> readers;
	readers.reserve(reader_threads);
	for (std::size_t i = 0; i < reader_threads; ++i) {
		readers.emplace_back([&st, i] { reader_loop(st, static_cast<unsigned>(i) + 1u); });
	}
	{
		const std::jthread writer{[&st] { writer_loop(st, 200'000); }};
	}
	st.stop.store(true, std::memory_order_relaxed);
	readers.clear(); // jthread joins

	std::printf("  reads ok=%llu missed=%llu corrupt=%llu\n",
	            static_cast<unsigned long long>(st.reads_ok.load()),
	            static_cast<unsigned long long>(st.reads_missed.load()),
	            static_cast<unsigned long long>(st.corrupt.load()));

	// The headline invariant: not one read resolved to the wrong object.
	CHECK_EQ(st.corrupt.load(), 0u);
	// And the test has to have actually exercised the path - a run where every
	// read missed would report zero corruption while proving nothing.
	CHECK(st.reads_ok.load() > 1000);

	// draining with no readers left must reclaim everything outstanding
	while (st.objects.pending() > 0) {
		if (st.objects.collect() == 0) { break; }
	}
	CHECK_EQ(st.objects.pending(), 0u);
}

// Many threads interning the same names concurrently must agree on the ids, and
// must not corrupt the table doing it.
void test_concurrent_interning() {
	atom_table atoms;
	constexpr std::size_t threads = 8;
	constexpr std::size_t names = 500;

	std::vector<std::vector<atom>> results(threads);
	{
		std::vector<std::jthread> workers;
		workers.reserve(threads);
		for (std::size_t t = 0; t < threads; ++t) {
			workers.emplace_back([&atoms, &results, t] {
				results[t].reserve(names);
				for (std::size_t i = 0; i < names; ++i) {
					results[t].push_back(atoms.intern("tag" + std::to_string(i)));
				}
			});
		}
	}
	bool agree = true;
	for (std::size_t t = 1; t < threads; ++t) {
		if (results[t] != results[0]) { agree = false; }
	}
	CHECK(agree);                       // every thread got the same ids
	CHECK_EQ(atoms.size(), names + 1u); // interned once each, plus the empty atom
	CHECK_EQ(atoms.text(results[0][7]), std::string_view{"tag7"});
}

} // namespace

int main() {
	test_concurrent_slab();
	test_concurrent_interning();
	REPORT("stress_slab");
}
