// The test the thread-safe DOM was chosen for.
//
// Many readers traverse the tree with no locks while writers concurrently
// append, remove, reparent and rewrite attributes. Four invariants are checked
// rather than assumed, and every one of them is a bug class that a locked-DOM
// design would not have:
//
//   1. NO CYCLES. A reparent that puts a node beneath its own descendant makes
//      every traversal in the engine loop forever. Readers walk to the root on
//      every visit with a bounded step count, so a cycle shows up as a hang
//      turned into a failure.
//   2. PARENT/CHILD AGREE. If a child appears in a parent's list, the child's
//      parent pointer must name that parent. RCU publishes them separately, so
//      this is exactly where a missing release/acquire would show.
//   3. NO TORN PAYLOAD. Attribute values are self-describing; a value that
//      does not match its own key means a reader observed a half-published
//      block.
//   4. NO USE-AFTER-FREE. Readers dereference spans into payload blocks that
//      writers are concurrently replacing. Only the epoch domain stands
//      between that and a dangling pointer - which is what ASan is here for.
//
// A clean run WITHOUT sanitizers proves very little. Run the tsan and asan
// presets.

#include <ctbrowser/core/core.hpp>
import ctbrowser.dom;

#include "check.hpp"
#include <atomic>
#include <cstdint>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace ctbrowser;

namespace {

constexpr std::size_t pool_size = 256; // elements shuffled around the tree
constexpr std::size_t reader_threads = 4;
constexpr std::size_t writer_threads = 2;
constexpr int writer_rounds = 20'000;

struct shared {
    atom_table atoms;
    document doc{atoms};
    std::vector<node_id> pool;
    atom attr_key{};
    atom tag{};

    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> visits{0};
    std::atomic<std::uint64_t> cycles{0};     // invariant 1
    std::atomic<std::uint64_t> mismatched{0}; // invariant 2
    std::atomic<std::uint64_t> torn{0};       // invariant 3
    std::atomic<std::uint64_t> reclaimed{0};  // proof that anything was freed at all
};

// Attribute values encode their own node, so a reader can tell whether the
// block it observed belongs to the node it asked about.
[[nodiscard]] std::string expected_value(node_id id) {
    return "v" + std::to_string(id.slot);
}

void reader_loop(shared & st, unsigned seed) {
    std::mt19937 rng{seed};
    std::uniform_int_distribution<std::size_t> pick{0, pool_size - 1};

    while (!st.stop.load(std::memory_order_relaxed)) {
        const auto r = st.doc.read();

        // (1) walk to the root with a hard step bound: a cycle cannot hang us
        const node_id start = st.pool[pick(rng)];
        std::size_t steps = 0;
        for (node_id at = start; at; at = r.parent(at)) {
            if (++steps > pool_size + 8) {
                st.cycles.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }

        // (2) every child's parent pointer must name the parent we found it under
        for (const node_id child : r.children(start)) {
            const node_id back = r.parent(child);
            // The child may have been moved away between the two loads; that is
            // legal and is not a mismatch. A NON-EMPTY parent that is some OTHER
            // node while the child is still listed here would be.
            if (back && back != start) {
                bool still_listed = false;
                for (const node_id c : r.children(start)) {
                    if (c == child) { still_listed = true; }
                }
                if (still_listed) { st.mismatched.fetch_add(1, std::memory_order_relaxed); }
            }
        }

        // (3) an attribute value must match the node it was read from
        const std::string_view value = r.attribute_value(start, st.attr_key);
        if (!value.empty() && value != expected_value(start)) {
            st.torn.fetch_add(1, std::memory_order_relaxed);
        }

        st.visits.fetch_add(1, std::memory_order_relaxed);
    }
}

void writer_loop(shared & st, unsigned seed) {
    std::mt19937 rng{seed};
    std::uniform_int_distribution<std::size_t> pick{0, pool_size - 1};
    std::uniform_int_distribution<int> action{0, 99};

    for (int round = 0; round < writer_rounds && !st.stop.load(std::memory_order_relaxed);
         ++round) {
        const node_id a = st.pool[pick(rng)];
        const node_id b = st.pool[pick(rng)];
        const int roll = action(rng);

        if (roll < 45) {
            // reparent: the operation that can create a cycle. The document is
            // expected to REFUSE the cycling ones, not to crash or corrupt.
            (void)st.doc.append_child(b, a);
        } else if (roll < 65) {
            (void)st.doc.remove_child(a);
        } else if (roll < 90) {
            (void)st.doc.set_attribute(a, st.attr_key, expected_value(a));
        } else {
            (void)st.doc.append_child(st.doc.root(), a);
        }

        // RECLAIM WHILE READERS ARE RUNNING. Without this the test is close to
        // worthless: retired payload blocks would simply pile up, nothing would
        // ever be freed, and ASan could not catch a use-after-free that never
        // happens. Collecting here is what puts destruction in a genuine race
        // with the readers dereferencing those very blocks - which is the one
        // thing the epoch domain exists to make safe.
        if ((round % 64) == 0) {
            const std::size_t freed = st.doc.collect();
            st.reclaimed.fetch_add(freed, std::memory_order_relaxed);
        }
    }
    st.stop.store(true, std::memory_order_relaxed);
}

void test_concurrent_dom() {
    shared st;
    st.attr_key = st.atoms.intern("data-v");
    st.tag = st.atoms.intern("div");
    st.pool.reserve(pool_size);
    for (std::size_t i = 0; i < pool_size; ++i) {
        const node_id id = st.doc.create_element(st.tag);
        (void)st.doc.append_child(st.doc.root(), id);
        (void)st.doc.set_attribute(id, st.attr_key, expected_value(id));
        st.pool.push_back(id);
    }

    {
        std::vector<std::jthread> threads;
        threads.reserve(reader_threads + writer_threads);
        for (std::size_t i = 0; i < reader_threads; ++i) {
            threads.emplace_back([&st, i] { reader_loop(st, static_cast<unsigned>(i) + 100u); });
        }
        for (std::size_t i = 0; i < writer_threads; ++i) {
            threads.emplace_back([&st, i] { writer_loop(st, static_cast<unsigned>(i) + 900u); });
        }
    }
    st.stop.store(true, std::memory_order_relaxed);

    std::printf("  visits=%llu cycles=%llu mismatched=%llu torn=%llu reclaimed=%llu\n",
                static_cast<unsigned long long>(st.visits.load()),
                static_cast<unsigned long long>(st.cycles.load()),
                static_cast<unsigned long long>(st.mismatched.load()),
                static_cast<unsigned long long>(st.torn.load()),
                static_cast<unsigned long long>(st.reclaimed.load()));

    CHECK_EQ(st.cycles.load(), 0u);
    CHECK_EQ(st.mismatched.load(), 0u);
    CHECK_EQ(st.torn.load(), 0u);
    CHECK(st.visits.load() > 1000); // the readers must actually have run
    // If nothing was reclaimed mid-run, destruction never raced a reader and
    // the whole point of the test was missed. Assert the exercise happened.
    CHECK(st.reclaimed.load() > 0);

    // the tree must still be a tree once everything has quiesced
    const auto r = st.doc.read();
    std::size_t reachable = 0;
    const auto walk = [&](auto && self, node_id at, std::size_t depth) -> void {
        CHECK(depth < pool_size + 8);
        ++reachable;
        for (const node_id c : r.children(at)) {
            CHECK(r.parent(c) == at);
            self(self, c, depth + 1);
        }
    };
    walk(walk, r.root(), 0);
    CHECK(reachable <= pool_size + 1); // nothing duplicated into two parents

    while (st.doc.collect() > 0) {} // and everything retired drains
}

} // namespace

int main() {
    test_concurrent_dom();
    REPORT("stress_dom");
}
