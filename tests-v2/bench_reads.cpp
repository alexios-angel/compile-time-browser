// The measurement the plan asks for BEFORE the engine is built on this model.
//
// The thread-safe DOM was chosen over the main-thread-confined one that
// shipping engines use, and the stated risk was throughput: a DOM read is the
// single hottest operation in an engine, so if the safety machinery costs too
// much per read, that has to be known now - while the snapshot boundary is
// still the only thing depending on it - rather than after style, layout and
// raster are all sitting on top.
//
// Three numbers, same work in each:
//
//   baseline   direct vector indexing. No handle, no generation check, no
//              epoch. This is the speed limit.
//   guarded    the real read path: epoch guard + chunked lookup + generation
//              compare. The gap against baseline IS the safety tax.
//   scaling    the same guarded path from N threads at once. Readers share no
//              cache lines, so this should scale close to linearly; if it does
//              not, the announce protocol is the suspect.
//
// Not a ctest gate - numbers move with the machine. Run it, read it, decide.

import ctbrowser.core;

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

using namespace ctbrowser;
using clock_type = std::chrono::steady_clock;

namespace {

struct payload_tag {};
using payload_id = handle<payload_tag>;

struct payload {
	std::uint64_t value = 0;
};

constexpr std::size_t object_count = 100'000;
constexpr std::size_t reads_per_thread = 20'000'000;

[[nodiscard]] double million_reads_per_second(std::size_t reads, clock_type::duration elapsed) {
	const double seconds = std::chrono::duration<double>(elapsed).count();
	return seconds > 0 ? static_cast<double>(reads) / seconds / 1e6 : 0.0;
}

// A cheap deterministic index walk. Random enough to defeat prefetching, and
// identical across all three measurements so they compare like for like.
[[nodiscard]] inline std::size_t next_index(std::uint64_t & state, std::size_t modulus) {
	state ^= state << 13;
	state ^= state >> 7;
	state ^= state << 17;
	return static_cast<std::size_t>(state % modulus);
}

} // namespace

int main() {
	epoch_domain domain;
	slab<payload, payload_tag> objects{domain};
	std::vector<payload_id> ids;
	std::vector<payload> flat;
	ids.reserve(object_count);
	flat.reserve(object_count);
	for (std::size_t i = 0; i < object_count; ++i) {
		ids.push_back(objects.insert(payload{i}));
		flat.push_back(payload{i});
	}

	std::printf("ctbrowser.core read throughput  (%zu objects, %zu reads/thread)\n\n",
	            object_count, reads_per_thread);

	// --- baseline: no safety machinery at all -----------------------------
	std::uint64_t checksum_baseline = 0;
	const auto t0 = clock_type::now();
	{
		std::uint64_t state = 0x9E3779B97F4A7C15ull;
		for (std::size_t i = 0; i < reads_per_thread; ++i) {
			checksum_baseline += flat[next_index(state, object_count)].value;
		}
	}
	const auto baseline = clock_type::now() - t0;
	const double baseline_rate = million_reads_per_second(reads_per_thread, baseline);
	std::printf("  baseline (raw vector index)      %8.1f M reads/s\n", baseline_rate);

	// --- diagnostic: handle indirection WITHOUT any safety ----------------
	// Isolates the two costs the guarded number conflates. This walks the same
	// two arrays (handles, then objects) so it pays the same cache footprint,
	// but does no epoch announce, no generation compare and no chunk lookup.
	// If this lands near `guarded`, the cost is indirection and there is
	// nothing to tighten; if it lands near `baseline`, the safety machinery is
	// the problem and the model needs revisiting NOW.
	std::uint64_t checksum_indirect = 0;
	const auto t_ind = clock_type::now();
	{
		std::uint64_t state = 0x9E3779B97F4A7C15ull;
		for (std::size_t i = 0; i < reads_per_thread; ++i) {
			const payload_id id = ids[next_index(state, object_count)];
			checksum_indirect += flat[id.slot].value;
		}
	}
	const auto indirect = clock_type::now() - t_ind;
	const double indirect_rate = million_reads_per_second(reads_per_thread, indirect);
	std::printf("  handle indirection, no safety    %8.1f M reads/s   %.2fx of baseline\n",
	            indirect_rate, indirect_rate / baseline_rate);

	// --- guarded: the real read path, one thread --------------------------
	std::uint64_t checksum_guarded = 0;
	const auto t1 = clock_type::now();
	{
		const auto guard = domain.pin();
		std::uint64_t state = 0x9E3779B97F4A7C15ull;
		for (std::size_t i = 0; i < reads_per_thread; ++i) {
			const payload * p = objects.get(ids[next_index(state, object_count)]);
			if (p != nullptr) { checksum_guarded += p->value; }
		}
	}
	const auto guarded = clock_type::now() - t1;
	const double guarded_rate = million_reads_per_second(reads_per_thread, guarded);
	std::printf("  guarded  (epoch + generation)    %8.1f M reads/s   %.2fx of baseline\n",
	            guarded_rate, guarded_rate / baseline_rate);

	// --- sequential: what tree traversal actually looks like --------------
	// The arms above walk a RANDOM index, which is the worst case for this
	// design and not what an engine does. Style, layout and paint each walk
	// the tree in document order, and nodes are allocated in document order,
	// so slot order and traversal order largely coincide. If the safety tax
	// mostly disappears here, the random-walk number is a worst case rather
	// than the number to design against.
	std::uint64_t checksum_seq_flat = 0;
	const auto t_sf = clock_type::now();
	for (std::size_t r = 0; r < reads_per_thread / object_count; ++r) {
		for (std::size_t i = 0; i < object_count; ++i) { checksum_seq_flat += flat[i].value; }
	}
	const auto seq_flat = clock_type::now() - t_sf;
	const double seq_flat_rate = million_reads_per_second(reads_per_thread, seq_flat);

	std::uint64_t checksum_seq_guarded = 0;
	const auto t_sg = clock_type::now();
	{
		const auto guard = domain.pin();
		for (std::size_t r = 0; r < reads_per_thread / object_count; ++r) {
			for (std::size_t i = 0; i < object_count; ++i) {
				const payload * p = objects.get(ids[i]);
				if (p != nullptr) { checksum_seq_guarded += p->value; }
			}
		}
	}
	const auto seq_guarded = clock_type::now() - t_sg;
	const double seq_guarded_rate = million_reads_per_second(reads_per_thread, seq_guarded);
	// NOTE the sequential baseline is not a fair denominator: a flat summation
	// over a contiguous array auto-vectorizes into a SIMD reduction, which the
	// guarded loop (branch + atomic load per element) cannot. Quoting a ratio
	// against it would say more about the optimizer than about this design.
	// The comparison that means something is guarded-vs-guarded: the same read
	// path under the two access patterns.
	std::printf("\n  sequential baseline              %8.1f M reads/s  (SIMD; not a fair ratio)\n",
	            seq_flat_rate);
	std::printf("  sequential guarded               %8.1f M reads/s   %.2fx the RANDOM guarded\n",
	            seq_guarded_rate, seq_guarded_rate / guarded_rate);

	// --- scaling: the same path from N threads ----------------------------
	const unsigned hw = std::thread::hardware_concurrency();
	const unsigned threads = hw > 1 ? hw : 2;
	std::atomic<std::uint64_t> total{0};
	const auto t2 = clock_type::now();
	{
		std::vector<std::jthread> readers;
		readers.reserve(threads);
		for (unsigned t = 0; t < threads; ++t) {
			readers.emplace_back([&, t] {
				const auto guard = domain.pin();
				std::uint64_t local = 0;
				std::uint64_t state = 0x9E3779B97F4A7C15ull + t;
				for (std::size_t i = 0; i < reads_per_thread; ++i) {
					const payload * p = objects.get(ids[next_index(state, object_count)]);
					if (p != nullptr) { local += p->value; }
				}
				total.fetch_add(local, std::memory_order_relaxed);
			});
		}
	}
	const auto scaled = clock_type::now() - t2;
	const std::size_t scaled_reads = reads_per_thread * threads;
	const double scaled_rate = million_reads_per_second(scaled_reads, scaled);
	std::printf("  guarded x%-2u threads              %8.1f M reads/s   %.2fx of 1 thread\n",
	            threads, scaled_rate, scaled_rate / guarded_rate);

	// Keep every loop above observable so none of them can be optimized out -
	// and the three single-threaded walks use the same seed and the same index
	// sequence, so their checksums MUST agree. If they ever diverge, the
	// benchmark is measuring different work in each arm and its ratios are
	// meaningless, so this is checked rather than assumed.
	std::printf("\n  (checksums %llu %llu %llu / threaded %llu)\n",
	            static_cast<unsigned long long>(checksum_baseline),
	            static_cast<unsigned long long>(checksum_indirect),
	            static_cast<unsigned long long>(checksum_guarded),
	            static_cast<unsigned long long>(total.load()));
	const bool consistent =
	    checksum_baseline == checksum_guarded && checksum_baseline == checksum_indirect &&
	    checksum_seq_flat == checksum_seq_guarded;
	if (!consistent) { std::printf("  MISMATCH: the arms did not do the same work\n"); }
	return consistent ? 0 : 1;
}
