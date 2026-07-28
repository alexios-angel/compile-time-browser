module;
#include <boost/lockfree/spsc_queue.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <ctbrowser/core/core.hpp>

export module ctbrowser.raster:pipeline;

import ctbrowser.paint;
import :backend;
import :compositor;
import :tile;

// The frame pipeline: a compositor thread that owns the device, fed by raster
// workers over lock-free channels.
//
// WHY A DEDICATED THREAD. A GPU device is not thread-safe and a swapchain is
// less so; every graphics API in use expects submission from one thread. Making
// that thread explicit is better than hoping the frame path happens to stay
// single-threaded, and it is what lets the document thread carry on while a
// frame is in flight.
//
// WHY IT NEEDS NO MUTEX. Tiles finish on pool workers and have to reach the
// compositor. Each worker gets its OWN queue, so every queue has exactly one
// producer and one consumer - single-producer by construction rather than by
// convention. boost::lockfree::spsc_queue is then correct with no lock at all,
// and the fan-in is the compositor draining N of them in turn.
//
// THE DIVISION OF LABOUR, which the backend contract depends on:
//
//   compositor thread   begin_frame, reserve_tiles, composite, end_frame
//                       - every call that may touch the device
//   raster workers      raster() only, which must be CPU-side work into
//                       storage reserve_tiles already allocated
//
// A backend that touches its device from raster() breaks this, and no amount of
// locking inside the backend would fix it - the swapchain still has one thread
// it may be used from.

export namespace ctbrowser::raster {

using ctbrowser::paint::layer_tree;

// What a frame needs. Copied rather than referenced because the document thread
// moves on the moment it has submitted, and the layer tree it built is its own.
struct frame_request {
    layer_tree layers;
    rect viewport;
    int extent = default_tile_extent;
};

template <RasterBackend B> class compositor_thread {
public:
    // `pool` is the raster pool. Its worker count decides how many channels
    // exist, because that is how many producers there can be.
    compositor_thread(B & backend, scheduler & pool)
        : backend_(&backend), pool_(&pool), channels_(pool.producer_slots()) {
        for (auto & c : channels_) { c = std::make_unique<channel>(); }
        worker_ = std::jthread{[this](const std::stop_token & stop) { run(stop); }};
    }

    ~compositor_thread() {
        worker_.request_stop();
        {
            const std::lock_guard lock{mutex_};
            wake_.notify_all();
        }
    }

    compositor_thread(const compositor_thread &) = delete;
    compositor_thread & operator=(const compositor_thread &) = delete;

    // Document thread. Hands over a frame and returns immediately; the number
    // returned is what wait_for() takes.
    std::uint64_t submit(frame_request request) {
        const std::lock_guard lock{mutex_};
        pending_ = std::move(request);
        has_pending_ = true;
        const std::uint64_t n = ++submitted_;
        wake_.notify_all();
        return n;
    }

    // Block until frame `n` has been composited. Tests need this; a real event
    // loop would instead do something else and check back.
    void wait_for(std::uint64_t n) {
        std::unique_lock lock{mutex_};
        done_.wait(lock, [&] { return presented_ >= n || stopped_; });
    }

    [[nodiscard]] std::uint64_t frames_presented() const {
        const std::lock_guard lock{mutex_};
        return presented_;
    }
    [[nodiscard]] std::size_t tiles_published() const {
        return published_.load(std::memory_order_relaxed);
    }
    // The error from the last frame that failed, if any. A frame that fails on
    // the compositor thread has nobody to return to, so it is recorded here.
    [[nodiscard]] std::optional<gpu_error> last_error() const {
        const std::lock_guard lock{mutex_};
        return last_error_;
    }

private:
    // Bounded on purpose. An unbounded queue would let a fast rasterizer grow
    // memory without limit when the compositor stalls; a bounded one makes the
    // producer notice, and the producer noticing is what backpressure IS.
    static constexpr std::size_t channel_capacity = 1024;
    using queue_type =
        boost::lockfree::spsc_queue<tile_id, boost::lockfree::capacity<channel_capacity>>;
    struct channel {
        queue_type queue;
    };

    void run(const std::stop_token & stop) {
        while (!stop.stop_requested()) {
            frame_request request;
            {
                std::unique_lock lock{mutex_};
                wake_.wait(lock, [&] { return has_pending_ || stop.stop_requested(); });
                if (stop.stop_requested()) { break; }
                request = std::move(pending_);
                has_pending_ = false;
            }
            const auto result = run_frame(request);
            {
                const std::lock_guard lock{mutex_};
                if (!result) { last_error_ = result.error(); }
                ++presented_;
            }
            done_.notify_all();
        }
        {
            const std::lock_guard lock{mutex_};
            stopped_ = true;
        }
        done_.notify_all();
    }

    [[nodiscard]] std::expected<void, gpu_error> run_frame(const frame_request & request) {
        const auto token = backend_->begin_frame();
        if (!token) { return std::unexpected(token.error()); }

        std::vector<tile> tiles;
        std::vector<const paint::display_list *> lists;
        visible_tiles(request.layers, request.viewport, request.extent, tiles, lists);

        // Device-touching, so it stays on this thread.
        if (const auto reserved = backend_->reserve_tiles(tiles); !reserved) {
            (void)backend_->end_frame();
            return std::unexpected(reserved.error());
        }

        std::vector<std::size_t> stale;
        stale.reserve(tiles.size());
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            if (backend_->needs_raster(tiles[i].id)) { stale.push_back(i); }
        }

        // Raster on the pool. This thread deliberately does NOT help: it stays
        // free to drain the channels and hand each finished tile to the backend
        // while the rest of the page is still being drawn. parallel_for would
        // have made the caller a worker, which is exactly the wrong shape here -
        // a compositor busy rastering is a compositor not compositing.
        std::atomic<bool> raster_failed{false};
        std::atomic<gpu_error> first_failure{gpu_error::device_lost};
        std::atomic<std::size_t> finished{0};
        for (std::size_t k = 0; k < stale.size(); ++k) {
            const std::size_t i = stale[k];
            pool_->submit([this, i, &tiles, &lists, &raster_failed, &first_failure, &finished] {
                if (const auto r = backend_->raster(tiles[i].id, *lists[i]); !r) {
                    if (!raster_failed.exchange(true, std::memory_order_relaxed)) {
                        first_failure.store(r.error(), std::memory_order_relaxed);
                    }
                } else {
                    publish(tiles[i].id);
                }
                // LAST, and it has to be last. The frame is finished when this
                // counter is full, so anything after it would run against a
                // compositor_thread the caller is already allowed to destroy -
                // and against this function's stack, which is already gone.
                finished.fetch_add(1, std::memory_order_release);
            });
        }

        // Wait on the TASKS, not on the completions. Draining the channels tells
        // you a tile's id arrived, which happens BEFORE its task returns; using
        // that as the exit condition let a worker still be inside publish() when
        // the compositor was torn down. TSan found exactly that.
        std::size_t seen = 0;
        while (finished.load(std::memory_order_acquire) < stale.size()) {
            if (!drain_once(seen)) { std::this_thread::yield(); }
        }
        drain_once(seen); // whatever landed between the last drain and the exit

        if (raster_failed.load(std::memory_order_relaxed)) {
            (void)backend_->end_frame();
            return std::unexpected(first_failure.load(std::memory_order_relaxed));
        }

        if (const auto c = backend_->composite(request.layers.layers); !c) {
            (void)backend_->end_frame();
            return std::unexpected(c.error());
        }
        return backend_->end_frame();
    }

    // Raster worker. Lock-free: this worker's queue has no other producer.
    //
    // A full queue means the compositor is behind, and the right answer is to
    // WAIT rather than drop - the compositor is draining continuously, so the
    // wait is bounded, and a dropped completion would leave the frame counting
    // a tile that never arrives. Backpressure is the queue being bounded and
    // the producer noticing; a queue that silently discards has neither.
    void publish(tile_id id) {
        const std::size_t slot = pool_->worker_index();
        channel & c = *channels_[slot < channels_.size() ? slot : channels_.size() - 1];
        // Counted BEFORE the push. Once the id is in the queue the compositor
        // may pop it and move on, so anything this function does afterwards is
        // a write racing the frame's own teardown.
        published_.fetch_add(1, std::memory_order_relaxed);
        while (!c.queue.push(id)) {
            overflowed_.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    }

    // Compositor thread. Hand every arrived tile to the backend, which is where
    // a GPU backend starts its upload.
    bool drain_once(std::size_t & seen) {
        bool any = false;
        for (auto & c : channels_) {
            tile_id id{};
            while (c->queue.pop(id)) {
                backend_->tile_ready(id);
                ++seen;
                any = true;
            }
        }
        return any;
    }

    B * backend_;
    scheduler * pool_;
    std::vector<std::unique_ptr<channel>> channels_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable done_;
    frame_request pending_;
    bool has_pending_ = false;
    bool stopped_ = false;
    std::uint64_t submitted_ = 0;
    std::uint64_t presented_ = 0;
    std::optional<gpu_error> last_error_;

    std::atomic<std::size_t> published_{0};
    std::atomic<std::size_t> drained_{0};
    std::atomic<std::size_t> overflowed_{0};

    std::jthread worker_;
};

} // namespace ctbrowser::raster
