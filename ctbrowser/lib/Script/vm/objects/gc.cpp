//===- ctbrowser/lib/Script/vm/objects/gc.cpp - Mark-sweep collector ------===//
//
// The script engine's mark-sweep garbage collector: the per-kind edge table
// (trace_object), the precise roots, and the sweep. These are members of
// `context`, declared in include/ctbrowser/script/vm.hpp.
//
//===----------------------------------------------------------------------===//

#include <cstddef>

#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

/// Marks `o` and everything reachable from it. The mark phase is a loop, not a
/// recursion: `push_mark` greys and `trace_object` blackens, and neither calls
/// the other, so C++ stack use does not grow with the longest chain in the
/// object graph (see mark_worklist_ in vm.hpp).
///
/// The drain lives here, next to trace_object, so the engine has exactly one
/// copy of the per-kind edge table.
void context::mark_object(heap_object * o) {
    push_mark(o);
    while (!mark_worklist_.empty()) {
        heap_object * grey = mark_worklist_.back();
        mark_worklist_.pop_back();
        trace_object(grey);
    }
}

/// Pushes every value `o` references onto the mark worklist.
void context::trace_object(heap_object * o) {
    // Every edge goes through push_mark, never mark_object: mark_object drains
    // the worklist, so calling it here would bring back the recursion.
    const auto edge = [this](value v) {
        if (v.is_heap()) { push_mark(v.as_heap()); }
    };
    switch (o->kind) {
    case heap_kind::array: {
        auto * arr = static_cast<array_object *>(o);
        for (const value & v : arr->items) { edge(v); }
        // The sparse elements too: one reachable only through `sparse` can
        // still be read by index.
        for (const auto & [index, v] : arr->sparse) { edge(v); }
        edge(arr->viewed);
        edge(arr->prototype);
        edge(arr->index);
        edge(arr->input);
        edge(arr->groups);
        // The named table is the array's own, not a heap object: its edges
        // are traced here rather than by pushing it (a mark bit nobody
        // sweeps would stay set and hide its edges from the next cycle).
        if (arr->named) {
            for (const auto & [name, v] : arr->named->props) { edge(v); }
            for (const accessor_entry & entry : arr->named->accessors.entries) {
                edge(entry.getter);
                edge(entry.setter);
            }
        }
        break;
    }
    case heap_kind::object: {
        auto * obj = static_cast<object_object *>(o);
        for (const auto & [name, v] : obj->props) { edge(v); }
        for (const accessor_entry & entry : obj->accessors.entries) {
            edge(entry.getter);
            edge(entry.setter);
        }
        edge(obj->prototype);
        break;
    }
    case heap_kind::cell: edge(static_cast<cell_object *>(o)->slot); break;
    case heap_kind::function: {
        auto * closure = static_cast<closure_object *>(o);
        // A closure owns its upvalue cells; missing them would free a
        // captured variable while the closure is still reachable.
        for (const value & up : closure->upvalues) { edge(up); }
        // ...and its own properties, which is where a class keeps its statics
        // and its prototype.
        for (const auto & [name, v] : closure->props) { edge(v); }
        for (const accessor_entry & entry : closure->accessors.entries) {
            edge(entry.getter);
            edge(entry.setter);
        }
        // ...and an arrow's captured `this`, which nothing else can reach.
        edge(closure->captured_this);
        edge(closure->proto_link);
        break;
    }
    case heap_kind::native: {
        auto * fn = static_cast<native_object *>(o);
        for (const auto & [name, v] : fn->props) { edge(v); }
        for (const accessor_entry & entry : fn->accessors.entries) {
            edge(entry.getter);
            edge(entry.setter);
        }
        // ...and the values its C++ lambda captured. A capture lives inside a
        // std::function's erased storage, which the collector cannot walk, so
        // natives list those values in native_object::retained.
        for (const value & v : fn->retained) { edge(v); }
        // ...and its own [[Prototype]]: `TypeError.__proto__` is `Error`, and
        // a constructor reachable only through another must stay alive.
        edge(fn->proto_link);
        break;
    }
    case heap_kind::proxy: {
        auto * proxy = static_cast<proxy_object *>(o);
        edge(proxy->target);
        edge(proxy->handler);
        edge(proxy->private_elements);
        break;
    }
    case heap_kind::coroutine: {
        // A suspended frame is a root like any other. Its saved registers live
        // outside the register file the collector walks, and they are the
        // only reference to the waiting function's locals.
        auto * saved = static_cast<coroutine_object *>(o);
        for (const value & v : saved->window) { edge(v); }
        edge(saved->receiver);
        edge(saved->promise);
        push_mark(saved->closure);
        // An async generator's queued requests - each a promise a caller
        // holds and the value it sent - and the generator object itself.
        edge(saved->self);
        edge(saved->delegate);
        for (const coroutine_object::async_request & waiting : saved->queue) {
            edge(waiting.sent);
            edge(waiting.promise);
        }
        break;
    }
    default: break; // Strings and symbols own no values.
    }
}

/// Marks `v` and everything reachable from it, if `v` is a heap value.
void context::mark(value v) {
    if (v.is_heap()) { mark_object(v.as_heap()); }
}

/// Runs a full mark-sweep collection and returns the number of objects freed.
std::size_t context::collect() {
    // Counted because a stress mode that silently stopped collecting would
    // give the same answers as one that works; tests that force GC assert
    // this number.
    ++collections_;
    string_bytes_ = 0;
    // The allocation ceiling counts allocations since the last collection, not
    // over the whole lifetime. It exists to stop a loop that allocates without
    // reaching a safepoint; a page that collects normally must never hit it.
    allocations_ = 0;

    // Under GC stress only, check every call frame before marking: a frame whose
    // register base lies past the end of `registers_` would read and write
    // outside the register file, and that corruption surfaces far from its cause.
    // No test reaches this (nothing outside the class can corrupt the frame
    // stack); it costs one comparison per frame in a mode that already collects
    // the whole heap.
    if (gc_stress_) {
        for (const call_frame & f : frames_) {
            if (f.proto == nullptr || f.base > registers_.size()) {
                raise("a call frame's register span is outside the register file");
                break;
            }
        }
    }

    // Precise roots. The single root inventory is `each_root` in vm.hpp, which
    // the type recorder's escape analysis (context::adjudicate) walks too. It
    // covers the whole register file and every frame.
    mark_roots(registers_.size());
    // Also mark what a running native function has allocated and what its script
    // callees returned to it (see context::native_scope and callout_scope). Each
    // pin range runs from its newest object down to, but not including, its
    // bottom: the bottom was already live before the range opened, so the normal
    // mark decides whether it survives. sweep() moves a bottom along if it frees
    // it. ctcompile/test/Analysis/Escape/Cycle.cpp depends on this exclusion.
    for (const pin_range & range : pins_) {
        for (heap_object * o = range.top != nullptr ? range.top : heap_;
             o != nullptr && o != range.bottom; o = o->next) {
            mark_object(o);
        }
    }
    for (const value v : pinned_results_) { mark(v); }
    // Everything that survives is marked now; what the host holds weakly and
    // nothing marked goes from its list before the sweep frees it.
    if (weak_prune_) {
        weak_prune_([](value v) { return !v.is_heap() || v.as_heap()->marked; });
    }
    return sweep();
}

/// Marks every precise root (see each_root) below `register_limit`.
void context::mark_roots(std::size_t register_limit) {
    each_root(register_limit, frames_.size(), [this](root_label, value v) { mark(v); });
}

/// Frees every unmarked object, clears the mark on every survivor, and returns
/// the number freed.
std::size_t context::sweep() {
    std::size_t freed = 0;
    std::size_t live_string_bytes = 0;
    // The pin ranges' bottoms come in list order, newest range first, so
    // one index walks them alongside the heap: `pending` ranges have a
    // bottom not yet passed.
    std::size_t pending = pins_.size();
    heap_object ** link = &heap_;
    while (*link != nullptr) {
        heap_object * o = *link;
        if (o->marked) {
            o->marked = false; // Clear for the next cycle.
            if (o->kind == heap_kind::string) {
                live_string_bytes += static_cast<const string_object *>(o)->text.size();
            }
            while (pending > 0 && pins_[pending - 1].bottom == o) { --pending; }
            link = &o->next;
        } else {
            *link = o->next;
            // A pin range's bottom (see collect) moves to the next older
            // object when the one it named is freed: everything between was
            // freed too, so the range is unchanged.
            for (std::size_t k = pending; k > 0 && pins_[k - 1].bottom == o; --k) {
                pins_[k - 1].bottom = o->next;
            }
            // The type recorder hears about every free, so it flags the record
            // dead and forgets the address before `delete` lets it be reused;
            // no record is ever read through a stale pointer.
            if (recorder_ != nullptr) [[unlikely]] { note_freed(o); }
            delete o;
            ++freed;
            --live_objects_;
        }
    }
    // Trigger the next collection once new string bytes equal the bytes that
    // survived, so string memory peaks at about twice its live size (the same
    // policy as for object counts).
    string_budget_ = std::max(minimum_string_budget, live_string_bytes);
    return freed;
}

/// Clears every mark bit without freeing anything. The escape analysis marks a
/// bounded set to observe reachability and then calls this, leaving the heap
/// exactly as a collection that freed nothing would.
void context::unmark_all() {
    for (heap_object * o = heap_; o != nullptr; o = o->next) { o->marked = false; }
}

/// Frees every object on the heap, marked or not (context teardown).
void context::sweep_all() {
    while (heap_ != nullptr) {
        heap_object * next = heap_->next;
        if (recorder_ != nullptr) [[unlikely]] { note_freed(heap_); }
        delete heap_;
        heap_ = next;
    }
    live_objects_ = 0;
}

} // namespace ctbrowser::script
