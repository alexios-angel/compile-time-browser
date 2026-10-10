//===- ctbrowser/unittests/unit/listener_registry.cpp - Listener tests ----===//
//
// shell::listener_registry, the per-document store of event listeners:
// duplicate detection by (target, type, callback, capture), snapshots taken
// before dispatch, `once` listeners spent on first use, handler-property
// slots, and removal while a dispatch is running.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/shell/listener_registry.hpp>

#include "check.hpp"
#include <memory>
#include <type_traits>

using ctbrowser::shell::event_target_kind;
using ctbrowser::shell::listener_registry;
using ctbrowser::shell::listener_target;

namespace {

/// A move-only payload, so the tests also prove entries are never copied.
using registry = listener_registry<std::unique_ptr<int>>;
constexpr listener_target target{event_target_kind::node, 17};

/// A bubbling "test" listener on \c target with callback \p id, whose payload
/// holds \p payload.
registry::entry listener(std::uint64_t id, int payload = 0) {
    return {.target = target,
            .type = "test",
            .callback_id = id,
            .payload = std::make_unique<int>(payload)};
}

/// A listener's identity is (target, type, callback, capture): a duplicate is
/// rejected and keeps the first registration's payload and flags, while a
/// different capture flag or target is a separate listener.
void test_registration_identity() {
    static_assert(!std::is_copy_constructible_v<registry::entry>);
    registry listeners;
    CHECK(listeners.add(listener(1, 17)));
    auto duplicate = listener(1, 99);
    duplicate.once = true;
    duplicate.passive = true;
    CHECK(!listeners.add(std::move(duplicate)));
    CHECK_EQ(*listeners.entries()[0].payload, 17);
    CHECK(!listeners.entries()[0].once);
    CHECK(!listeners.entries()[0].passive);

    auto capture = listener(1);
    capture.capture = true;
    CHECK(listeners.add(std::move(capture)));
    for (const auto at : {listener_target{event_target_kind::node, 18},
                          listener_target{event_target_kind::document, 0},
                          listener_target{event_target_kind::window, 0},
                          listener_target{event_target_kind::object, 17},
                          listener_target{event_target_kind::object, 18}}) {
        auto distinct = listener(1);
        distinct.target = at;
        CHECK(listeners.add(std::move(distinct)));
        CHECK_EQ(listeners.snapshot(at, "test", false), (std::vector<std::uint64_t>{1}));
    }
    CHECK_EQ(listeners.entries().size(), 7u);
    listeners.remove(target, "test", 1, false);
    CHECK(listeners.snapshot(target, "test", false).empty());
    CHECK_EQ(listeners.snapshot(target, "test", true), (std::vector<std::uint64_t>{1}));
    CHECK_EQ(listeners.entries().size(), 6u);
}

/// A dispatch snapshots callback ids, not entries: removing and re-adding the
/// same id before its turn runs the new entry, and a listener added after the
/// snapshot is not in it.
void test_snapshot_and_same_identity_replacement() {
    registry listeners;
    CHECK(listeners.add(listener(1)));
    CHECK(listeners.add(listener(2, 20)));
    const auto queued = listeners.snapshot(target, "test", false);
    CHECK_EQ(queued, (std::vector<std::uint64_t>{1, 2}));
    CHECK(listeners.begin(target, "test", queued[0], false));
    listeners.remove(target, "test", 2, false);
    CHECK(listeners.add(listener(2, 22)));
    CHECK(listeners.add(listener(3)));
    const auto * replacement = listeners.begin(target, "test", queued[1], false);
    CHECK(replacement != nullptr);
    if (replacement) { CHECK_EQ(*replacement->payload, 22); }
    CHECK_EQ(queued, (std::vector<std::uint64_t>{1, 2}));
    CHECK_EQ(listeners.snapshot(target, "test", false), (std::vector<std::uint64_t>{1, 2, 3}));
    listeners.remove(target, "test", 1, false);
    CHECK(!listeners.begin(target, "test", queued[0], false));
}

/// A `once` listener is spent by begin() and leaves later snapshots, but its
/// payload stays alive until reap_spent(), so a nested dispatch can still use it.
void test_once_and_nested_membership() {
    registry listeners;
    auto once = listener(1, 42);
    once.once = true;
    CHECK(listeners.add(std::move(once)));
    CHECK(listeners.add(listener(2)));
    const auto outer = listeners.snapshot(target, "test", false);
    const auto * running = listeners.begin(target, "test", outer[0], false);
    CHECK(running != nullptr);
    if (!running) { return; }
    const int * payload = running->payload.get();
    CHECK_EQ(listeners.snapshot(target, "test", false), (std::vector<std::uint64_t>{2}));
    CHECK(!listeners.begin(target, "test", 1, false));
    CHECK(listeners.begin(target, "test", 2, false));
    CHECK_EQ(listeners.entries().size(), 2u);
    // The owner retains spent entries through nested dispatch. Borrow the
    // stable owned object, never a vector row, across another registration.
    CHECK(listeners.add(listener(1, 99)));
    CHECK_EQ(*payload, 42);
    const auto * readded = listeners.begin(target, "test", 1, false);
    CHECK(readded != nullptr);
    if (readded) { CHECK_EQ(*readded->payload, 99); }
    CHECK(listeners.begin(target, "test", outer[1], false));
    listeners.reap_spent();
    CHECK_EQ(listeners.entries().size(), 2u);
    CHECK_EQ(listeners.snapshot(target, "test", false), (std::vector<std::uint64_t>{2, 1}));
}

/// A handler-property slot (`onfoo`) ignores `once` and keeps its position
/// until remove_handler(); erase_if() removes by payload, and an entry that
/// arrives already spent is not added.
void test_handler_slots_and_owner_removal() {
    registry listeners;
    auto handler = listener(1);
    handler.handler = true;
    handler.once = true;
    CHECK(listeners.add(std::move(handler)));
    CHECK(listeners.add(listener(2, 2)));
    CHECK(listeners.has_handler(target, "test"));
    CHECK(!listeners.has_handler({event_target_kind::window, 0}, "test"));
    CHECK(listeners.begin(target, "test", 1, false));
    CHECK(listeners.begin(target, "test", 1, false));
    CHECK_EQ(listeners.snapshot(target, "test", false), (std::vector<std::uint64_t>{1, 2}));
    listeners.remove_handler(target, "test");
    CHECK(!listeners.has_handler(target, "test"));
    CHECK_EQ(listeners.snapshot(target, "test", false), (std::vector<std::uint64_t>{2}));
    listeners.erase_if([](const registry::entry & current) { return *current.payload == 2; });
    CHECK(listeners.entries().empty());
    auto spent = listener(3);
    spent.spent = true;
    CHECK(!listeners.add(std::move(spent)));
    CHECK(listeners.entries().empty());
}

} // namespace

int main() {
    test_registration_identity();
    test_snapshot_and_same_identity_replacement();
    test_once_and_nested_membership();
    test_handler_slots_and_owner_removal();
    REPORT("listener_registry");
}
