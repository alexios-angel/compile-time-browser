// ctbrowser.core: the single-threaded contracts. The concurrent ones live in
// stress_slab.cpp, which runs under TSan.
import ctbrowser.core;

#include "check.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;

struct thing_tag {};
using thing_id = handle<thing_tag>;

namespace {

void test_handle() {
	CHECK(!thing_id{});                       // a zeroed handle is null
	CHECK(static_cast<bool>(thing_id{0, 1})); // slot 0 is a real slot
	CHECK((thing_id{3, 1} == thing_id{3, 1}));
	CHECK((thing_id{3, 1} != thing_id{3, 2})); // same slot, different generation

	// the total order is what makes multi-node locking deadlock-free, so it
	// has to actually be a total order
	std::vector<thing_id> ids{{2, 1}, {1, 5}, {1, 2}, {3, 1}};
	std::ranges::sort(ids, [](thing_id a, thing_id b) { return a.key() < b.key(); });
	CHECK(std::ranges::is_sorted(ids, [](thing_id a, thing_id b) { return a.key() < b.key(); }));
	CHECK_EQ(ids.front().slot, 1u);
}

void test_slab_basics() {
	epoch_domain domain;
	slab<std::string, thing_tag> s{domain};

	CHECK_EQ(s.size(), 0u);
	const thing_id a = s.insert("alpha");
	const thing_id b = s.insert("beta");
	CHECK_EQ(s.size(), 2u);
	CHECK(s.get(a) != nullptr);
	CHECK_EQ(*s.get(a), std::string{"alpha"});
	CHECK_EQ(*s.get(b), std::string{"beta"});
	CHECK(s.get(thing_id{}) == nullptr);

	// erase is IMMEDIATE for readers even though destruction is deferred
	CHECK(s.erase(a));
	CHECK(s.get(a) == nullptr);
	CHECK_EQ(s.size(), 1u);
	CHECK(!s.erase(a)); // and it is not erasable twice
	CHECK_EQ(*s.get(b), std::string{"beta"});
}

// The whole point of generations: a recycled slot must not answer to the
// handle that used to name it.
void test_stale_handle_does_not_resolve() {
	epoch_domain domain;
	slab<std::string, thing_tag> s{domain};

	const thing_id first = s.insert("first");
	const std::uint32_t slot = first.slot;
	CHECK(s.erase(first));
	CHECK_EQ(s.collect(), 1u); // no readers pinned, so it recycles at once

	const thing_id second = s.insert("second");
	CHECK_EQ(second.slot, slot);           // the slot really was reused...
	CHECK(second.generation != first.generation); // ...with a fresh generation
	CHECK(s.get(first) == nullptr);        // so the stale handle is dead
	CHECK_EQ(*s.get(second), std::string{"second"});
}

// A pinned reader must hold off recycling, or its pointer dangles.
void test_pin_defers_recycling() {
	epoch_domain domain;
	slab<std::string, thing_tag> s{domain};

	const thing_id id = s.insert("pinned");
	{
		const auto guard = domain.pin();
		CHECK(s.erase(id));
		CHECK_EQ(s.collect(), 0u); // a reader from before the erase is live
		CHECK_EQ(s.pending(), 1u);
	}
	CHECK_EQ(s.collect(), 1u); // it left; now it recycles
	CHECK_EQ(s.pending(), 0u);
}

void test_slab_grows_past_a_chunk() {
	epoch_domain domain;
	slab<int, thing_tag, 4> s{domain}; // 16 slots per chunk, so this spans several
	std::vector<thing_id> ids;
	for (int i = 0; i < 100; ++i) { ids.push_back(s.insert(i)); }
	CHECK_EQ(s.size(), 100u);
	for (int i = 0; i < 100; ++i) {
		const int * v = s.get(ids[static_cast<std::size_t>(i)]);
		CHECK(v != nullptr && *v == i); // every handle still resolves after growth
	}
}

void test_epoch_retire() {
	epoch_domain domain;
	int destroyed = 0;
	static int * counter = nullptr;
	counter = &destroyed;

	auto * payload = new int{7};
	domain.retire(payload, [](void * p) {
		++(*counter);
		delete static_cast<int *>(p);
	});
	CHECK_EQ(domain.pending(), 1u);
	{
		const auto guard = domain.pin();
		CHECK_EQ(domain.reclaim(), 0u); // pinned: nothing may be destroyed
	}
	CHECK_EQ(domain.reclaim(), 1u);
	CHECK_EQ(destroyed, 1);
	CHECK_EQ(domain.pending(), 0u);
}

void test_atoms() {
	atom_table atoms;
	const atom div = atoms.intern("div");
	const atom same = atoms.intern("div");
	const atom span = atoms.intern("span");

	CHECK(div == same);  // interning is idempotent...
	CHECK(div != span);  // ...and distinct strings stay distinct
	CHECK_EQ(atoms.text(div), std::string_view{"div"});
	CHECK(!atom{});      // the empty atom is falsy
	CHECK_EQ(atoms.text(atom{}), std::string_view{});
	CHECK(atoms.intern_lower("DIV") == div); // HTML names fold
	CHECK(atoms.intern_lower("DiV") == div);

	// the views must survive growth of the table they point into
	std::vector<atom> many;
	for (int i = 0; i < 5000; ++i) { many.push_back(atoms.intern("name" + std::to_string(i))); }
	CHECK_EQ(atoms.text(many[0]), std::string_view{"name0"});
	CHECK_EQ(atoms.text(div), std::string_view{"div"});
}

void test_geometry() {
	constexpr rect a{0, 0, 10, 10};
	constexpr rect b{5, 5, 10, 10};
	static_assert(a.contains(point{5, 5}));
	static_assert(!a.contains(point{10, 5})); // right edge is exclusive
	static_assert(a.intersects(b));
	static_assert(a.intersected(b) == rect{5, 5, 5, 5});
	static_assert(a.united(b) == rect{0, 0, 15, 15});
	static_assert(rect{}.united(a) == a); // empty unites to the other side
	static_assert(!a.intersects(rect{20, 20, 1, 1}));
	static_assert(a.translated(2, 3) == rect{2, 3, 10, 10});

	constexpr color c = color::rgba(0x11, 0x22, 0x33, 0x44);
	static_assert(c.argb == 0x44112233u);
	static_assert(c.red() == 0x11 && c.green() == 0x22 && c.blue() == 0x33 && c.alpha() == 0x44);
	static_assert(color::rgba(0, 0, 0, 255).opaque());
	static_assert(color::rgba(0, 0, 0, 0).transparent());

	constexpr sides s{1, 2, 3, 4};
	static_assert(s.horizontal() == 6 && s.vertical() == 4);
	CHECK(true); // the assertions above are compile-time; this keeps the counter honest
}

void test_scheduler() {
	scheduler pool{4};
	CHECK_EQ(pool.worker_count(), 4u);

	std::vector<int> out(1000, 0);
	pool.parallel_for(out.size(), [&](std::size_t i) { out[i] = static_cast<int>(i) * 2; });
	bool all = true;
	for (std::size_t i = 0; i < out.size(); ++i) {
		if (out[i] != static_cast<int>(i) * 2) { all = false; }
	}
	CHECK(all);

	// nested parallel_for must not deadlock: the caller helps drain the pool,
	// which is the property that makes recursive layout safe
	std::vector<int> nested(64, 0);
	pool.parallel_for(8, [&](std::size_t outer) {
		pool.parallel_for(8, [&](std::size_t inner) { nested[outer * 8 + inner] = 1; });
	});
	CHECK(std::ranges::all_of(nested, [](int v) { return v == 1; }));

	pool.parallel_for(0, [](std::size_t) { CHECK(false); }); // n == 0 runs nothing
}

} // namespace

int main() {
	test_handle();
	test_slab_basics();
	test_stale_handle_does_not_resolve();
	test_pin_defers_recycling();
	test_slab_grows_past_a_chunk();
	test_epoch_retire();
	test_atoms();
	test_geometry();
	test_scheduler();
	REPORT("core_basics");
}
