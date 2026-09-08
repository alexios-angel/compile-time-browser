// ctbrowser.script context - the mark-sweep collector: the edge table, the
// precise roots, and the sweep.
//
// One of six files carved out of a 1,396-line vm/objects.cpp on 2026-09-08 -
// which was itself one of four carved out of a 3,232-line vm.cpp on
// 2026-08-09. All members of `context`, declared in
// include/ctbrowser/script/vm.hpp - so they split across translation units
// with nothing to declare.

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/number_format.hpp>
#include <ctbrowser/script/vm.hpp>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// The VM's implementation.
//
// `run_loop` alone is 15 KB of object code - the whole instruction dispatch -
// and while it lived in the interface every translation unit that imported the
// module emitted its own copy and optimised it again. The class declaration
// stays in :vm; the bodies live here and are compiled once.

namespace ctbrowser::script {

// ===================== gc ================================================

// THE MARK PHASE, AS A LOOP. `push_mark` greys and `trace_object` blackens;
// neither can call the other, so the C++ stack is O(1) in the shape of the
// object graph rather than O(its longest chain). See mark_worklist_ in vm.hpp
// for what that cost was.
//
// The drain is here rather than in the header because trace_object is: one
// copy of the per-kind edge table, and nothing else in the engine may have an
// opinion about what an object points at.
void context::mark_object(heap_object * o) {
    push_mark(o);
    while (!mark_worklist_.empty()) {
        heap_object * grey = mark_worklist_.back();
        mark_worklist_.pop_back();
        trace_object(grey);
    }
}

void context::trace_object(heap_object * o) {
    // EVERY EDGE GOES THROUGH push_mark, NOT THROUGH mark_object. mark_object
    // drains, so calling it from here would put the whole recursion back one
    // level down and the fix would measure as working while doing nothing.
    const auto edge = [this](value v) {
        if (v.is_heap()) { push_mark(v.as_heap()); }
    };
    switch (o->kind) {
    case heap_kind::array: {
        auto * arr = static_cast<array_object *>(o);
        for (const value & v : arr->items) { edge(v); }
        // AND THE SPARSE HALF. An element that is only reachable through
        // `sparse` is reachable, and a collector that walked `items` alone
        // would free it under a page that can still read it by index.
        for (const auto & [index, v] : arr->sparse) { edge(v); }
        edge(arr->viewed);
        edge(arr->index);
        edge(arr->input);
        edge(arr->groups);
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
        // A closure OWNS its upvalue cells. Missing this frees a captured
        // variable while the closure that captured it is still reachable.
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
        // ...AND WHAT ITS C++ LAMBDA CAPTURED. A capture is invisible to a
        // precise collector - it lives inside a std::function's erased
        // storage, which no root walk can reach - so a native that closed over
        // a value held a pointer the sweep was free to free. See
        // native_object::retained.
        for (const value & v : fn->retained) { edge(v); }
        // ...AND ITS OWN [[Prototype]]. `TypeError.__proto__` is `Error`, and a
        // constructor reachable only through another one would otherwise be
        // swept out from under it.
        edge(fn->proto_link);
        break;
    }
    case heap_kind::proxy: {
        auto * proxy = static_cast<proxy_object *>(o);
        edge(proxy->target);
        edge(proxy->handler);
        break;
    }
    case heap_kind::coroutine: {
        // A SUSPENDED FRAME IS A ROOT LIKE ANY OTHER. Its registers are the
        // only reference to everything the function had in hand, and they are
        // out of the register stack the collector normally walks - so without
        // this every local of every waiting function is freed.
        auto * saved = static_cast<coroutine_object *>(o);
        for (const value & v : saved->window) { edge(v); }
        edge(saved->receiver);
        edge(saved->promise);
        push_mark(saved->closure);
        break;
    }
    default: break; // strings and symbols own no values
    }
}

void context::mark(value v) {
    if (v.is_heap()) { mark_object(v.as_heap()); }
}

std::size_t context::collect() {
    // COUNTED, because a stress mode that silently stops collecting looks
    // exactly like one that works: every answer is the same either way. A test
    // that forces GC asserts this number, not the answer.
    ++collections_;

    // AND THE FRAME CHAIN CHECKED, under stress only, which is where the master
    // plan puts it: "have the GC validate the whole frame chain on every
    // collection - reachable, terminated, and with plausible slot counts".
    //
    // A compiled frame's slots live in `registers_` and are addressed as
    // `registers_.data() + base`, so a base past the end of that vector is a
    // body about to read and write somewhere else entirely - the kind of
    // corruption that surfaces a long way from its cause.
    //
    // SAID PLAINLY: no test exercises this. Reaching it needs a corrupted
    // frame stack, which nothing can produce from outside the class, and
    // writing a test that reaches in to corrupt it would be testing the
    // corruption rather than the guard. It is here because it costs one
    // comparison per frame in a mode that already collects the whole heap.
    if (gc_stress_) {
        for (const call_frame & f : frames_) {
            if (f.proto == nullptr || f.base > registers_.size()) {
                raise("a call frame's register span is outside the register file");
                break;
            }
        }
    }

    // Precise roots: everything reachable is reachable from exactly these -
    // and "these" is ONE inventory, `each_root` in vm.hpp, which the escape
    // oracle walks too. The whole register file and every frame: a
    // collection has no dead window.
    mark_roots(registers_.size());
    return sweep();
}

void context::mark_roots(std::size_t register_limit) {
    each_root(register_limit, frames_.size(), [this](root_label, value v) { mark(v); });
}

std::size_t context::sweep() {
    std::size_t freed = 0;
    heap_object ** link = &heap_;
    while (*link != nullptr) {
        heap_object * o = *link;
        if (o->marked) {
            o->marked = false; // clear for the next cycle
            link = &o->next;
        } else {
            *link = o->next;
            // THE ESCAPE ORACLE HEARS ABOUT EVERY FREE, so a record can never
            // be read through a stale pointer: the recorder flags it dead and
            // forgets the address before `delete` reuses it.
            if (recorder_ != nullptr) [[unlikely]] { note_freed(o); }
            delete o;
            ++freed;
            --live_objects_;
        }
    }
    return freed;
}

// THE ESCAPE ORACLE'S EXIT. Everything a bounded mark set, cleared again, and
// nothing freed: the oracle observes and does not collect. collect() itself
// treats the mark bit as transient (sweep clears it on every survivor), so
// this leaves the heap exactly as a collection that freed nothing would.
void context::unmark_all() {
    for (heap_object * o = heap_; o != nullptr; o = o->next) { o->marked = false; }
}

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
