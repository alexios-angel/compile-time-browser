#include <ctbrowser/core/epoch.hpp>

#include <algorithm>
#include <limits>

namespace ctbrowser {
namespace {

// Function-local statics rather than namespace-scope ones: these are reached
// from a thread_local constructor, and function-local initialisation is the
// form with a defined order relative to that.
[[nodiscard]] std::mutex & registry_mutex() {
    static std::mutex m;
    return m;
}
[[nodiscard]] std::vector<std::size_t> & registry_free_list() {
    static std::vector<std::size_t> f;
    return f;
}

} // namespace

std::size_t thread_registry::index() {
    thread_local slot_lease lease;
    return lease.index;
}

thread_registry::slot_lease::slot_lease() : index(acquire_slot()) {}
thread_registry::slot_lease::~slot_lease() {
    release_slot(index);
}

std::size_t thread_registry::acquire_slot() {
    const std::lock_guard lock{registry_mutex()};
    if (!registry_free_list().empty()) {
        const std::size_t i = registry_free_list().back();
        registry_free_list().pop_back();
        return i;
    }
    static std::size_t next = 0;
    return next < max_threads ? next++ : max_threads - 1;
}

void thread_registry::release_slot(std::size_t i) {
    const std::lock_guard lock{registry_mutex()};
    registry_free_list().push_back(i);
}

epoch_domain::guard::guard(epoch_domain & domain) noexcept
    : domain_(&domain), slot_(thread_registry::index()) {
    // Publish BEFORE reading anything. A reclaiming writer that misses
    // this store could free a node this reader is about to touch, so
    // the store must not sink below the reads that follow it.
    domain_->participants_[slot_].announced.store(domain_->epoch_.load(std::memory_order_relaxed),
                                                  std::memory_order_seq_cst);
}

epoch_domain::guard::~guard() {
    domain_->participants_[slot_].announced.store(inactive, std::memory_order_seq_cst);
}

void epoch_domain::retire(void * object, void (*destroy)(void *)) {
    const std::lock_guard lock{retired_mutex_};
    retired_.push_back({epoch_.load(std::memory_order_relaxed), object, destroy});
}

std::uint64_t epoch_domain::oldest_active() const noexcept {
    std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
    for (const participant & p : participants_) {
        const std::uint64_t announced = p.announced.load(std::memory_order_seq_cst);
        if (announced != inactive) { oldest = std::min(oldest, announced); }
    }
    return oldest;
}

std::size_t epoch_domain::reclaim() {
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

std::size_t epoch_domain::pending() const {
    const std::lock_guard lock{retired_mutex_};
    return retired_.size();
}

epoch_domain::~epoch_domain() {
    for (const entry & e : retired_) { e.destroy(e.object); }
}

} // namespace ctbrowser
