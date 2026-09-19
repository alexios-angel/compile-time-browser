#pragma once

#include "../vm.hpp"

namespace ctbrowser::script {

// The `this` a frame actually sees. For an ordinary function that is its
// own receiver; for an arrow it is the one captured where the arrow was
// written, because an arrow never gets a receiver of its own. Both
// `load_this` and the closure builder go through here, which is what makes
// an arrow nested inside an arrow inside a method still resolve correctly.
[[nodiscard]] inline auto context::effective_this(const call_frame & f) -> value {
    if (f.closure != nullptr && f.closure->proto != nullptr && f.closure->proto->is_arrow) {
        return f.closure->captured_this;
    }
    return f.receiver;
}

// A FAILURE COMES WITH THE STACK IT HAPPENED ON.
inline auto context::raise(std::string message) -> void {
    if (failed_) { return; }
    failed_ = true;
    error_ = std::move(message) + current_stack();
}

// Find the innermost live handler and jump to it, discarding every call
// frame between here and the one that owns it. Returning false means
// nothing caught it, which is an uncaught exception.
[[nodiscard]] inline auto context::unwind_to_handler() -> bool {
    ++unwinds_;
    while (!handlers_.empty()) {
        const handler h = handlers_.back();
        handlers_.pop_back();
        if (h.fence) {
            // A C++ caller catches here - see call_fenced. Every frame the
            // callee pushed goes; the caller's registers stay (a native
            // still on the C++ stack may write its result into a popped
            // frame's slot, as it already could through a page's `try`).
            if (recorder_ != nullptr && h.frame < frames_.size()) [[unlikely]] {
                record_frames_unwound(h.frame);
            }
            if (frames_.size() > h.frame) { frames_.resize(h.frame); }
            fence_thrown_ = thrown_;
            fence_hit_ = true;
            thrown_ = value::undefined();
            return true;
        }
        if (h.frame >= frames_.size()) { continue; } // its frame already returned
        // THE ESCAPE ORACLE SEES THE FRAMES BEFORE THEY GO - FrameEnds.def
        // row `unwind`. Here `thrown_` is still
        // set (it is cleared below, after the landing write), so a value
        // in flight is a root and an object thrown out of a frame reads
        // as escaped `via thrown` rather than as confined. Only frames
        // ABOVE the handler's are ending; a throw caught in its own frame
        // discards none and records nothing.
        if (recorder_ != nullptr && h.frame + 1 < frames_.size()) [[unlikely]] {
            record_frames_unwound(h.frame + 1);
        }
        frames_.resize(h.frame + 1);
        call_frame & target = frames_.back();
        target.ip = h.address;
        if (registers_.size() < h.reg_top) { registers_.resize(h.reg_top, value::undefined()); }
        registers_[target.base + h.slot] = thrown_;
        // AND WHICH SLOT THAT WAS. The handler carrying it has already been
        // popped above, so this is the last moment anything knows.
        target.landed_slot = h.slot;
        thrown_ = value::undefined();
        return true;
    }
    return false;
}

// --- THE ONE ROOT INVENTORY --------------------------------------------
//
// Every root the collector walks, in CTBROWSER_ROOT_LABELS order and with
// that label on each, handed to a visitor. collect() marks through it
// and nothing else about collect() changed when it was split out; the
// escape oracle (type_record.cpp) walks the SAME inventory with the popped
// frame's dead register window excluded, so a root the collector knows
// cannot be one the oracle forgets. A template rather than a virtual, so
// the visitor collect() passes inlines to what the loop was before.
//
// `register_limit` bounds the flat register file: slots at or above it
// are not visited. `frame_limit` bounds the per-frame roots: frames at or
// above that index are not visited. collect() passes the whole of both.
// The oracle passes the ending frame's base and index - an inline callee
// sits INSIDE its caller's register extent (VM_CASE(call) places it at
// base + a + 1) and `ret` never shrinks registers_, so without the bound
// every object an ending frame still held in a register would read as
// reachable, and "reachable in the VM" is not "reachable in the program".
template <class Visit>
inline auto context::each_root(std::size_t register_limit, std::size_t frame_limit, Visit && visit)
    -> void {
    // A script can replace every visible alias of its global object. The
    // realm still owns it between turns, and the next script receives it.
    visit(root_label::globals, global_this_);
    for (const auto & [name, v] : globals_) { visit(root_label::globals, v); }
    const std::size_t top = std::min(register_limit, registers_.size());
    for (std::size_t i = 0; i < top; ++i) { visit(root_label::registers, registers_[i]); }
    // The receiver of a native call in progress. It is held in a C++
    // local, not in a register, so nothing else would keep it alive - and
    // collecting the object a method is running on is about as bad as it
    // gets.
    visit(root_label::current_this, current_this_);
    // The constructor a super() call is in the middle of handing on. It
    // lives only in this slot between the two instructions, which is
    // exactly the window a collection can fall in.
    visit(root_label::pending_new_target, pending_new_target_);
    visit(root_label::pending_closure, pending_closure_);
    // And the closure each live frame is executing. A function called
    // from C++ via call() is likewise only referenced from a C++ local;
    // without this its upvalues can be freed while its body is still
    // running.
    const std::size_t frames = std::min(frame_limit, frames_.size());
    for (std::size_t k = 0; k < frames; ++k) {
        const call_frame & f = frames_[k];
        if (f.closure != nullptr) { visit(root_label::frame_closure, value::object(f.closure)); }
        visit(root_label::frame_receiver, f.receiver);
        visit(root_label::frame_arguments, f.arguments_object);
        visit(root_label::frame_async_promise, f.async_promise);
        visit(root_label::frame_new_target, f.new_target);
    }
    // A QUEUED JOB AND ITS ARGUMENTS. Nothing else refers to them between
    // the moment they are queued and the moment they run, which is
    // precisely the window a collection can fall in.
    for (const microtask & job : microtasks_) {
        visit(root_label::microtasks, job.fn);
        for (const value & arg : job.args) { visit(root_label::microtasks, arg); }
    }
    visit(root_label::microtasks, await_job_); // the one native every settled await queues
    visit(root_label::prototypes, throw_type_error_);
    // EVERY MODULE'S EXPORT CELLS. They live in `modules_` and in no
    // register once the module has finished evaluating, so without this a
    // collection between two modules frees the bindings the second one is
    // about to import - and it frees them while a closure inside the
    // first still refers to the same cell.
    for (auto & [specifier, mod] : modules_) {
        for (auto & [name, cell] : mod.exports) { visit(root_label::module_exports, cell); }
        visit(root_label::module_namespace, mod.namespace_object);
    }
    // A thrown value in flight is reachable from nothing else.
    visit(root_label::thrown, thrown_);
    visit(root_label::thrown, fence_thrown_);  // caught by a C++ fence, not yet consumed
    visit(root_label::thrown, pending_throw_); // parked by `call`, not yet rethrown
    // AND WHAT A C++ SCOPE IS HOLDING ACROSS A CALL. `construct` allocates
    // the instance and then runs field initialisers and the constructor
    // body with it in a local; without this the object being constructed
    // is freed by any collection inside either. See context::rooted.
    for (const value & v : temporaries_) { visit(root_label::temporaries, v); }
    // The prototype tables hold every builtin method. Nothing else
    // references them, so without this the standard library is collected
    // on the first gc.
    for (object_object * table : prototypes_) {
        if (table != nullptr) { visit(root_label::prototypes, value::object(table)); }
    }
    // ...and the six NativeError prototypes, for the same reason: an
    // engine-raised error is put on one, and a page that never mentions
    // `RangeError` holds no other reference to its table.
    for (const auto & [kind, table] : error_prototypes_) {
        (void)kind;
        if (table != nullptr) { visit(root_label::prototypes, value::object(table)); }
    }
    // The per-function string cache. These are live `value`s held by the
    // context itself and referenced from nowhere else - a sweep without
    // them frees a string literal that a running loop is about to read
    // again.
    for (auto & [proto, cache] : string_cache_) {
        for (auto & [index, v] : cache) { visit(root_label::string_cache, v); }
    }
    // The BigInt literal cache is a root for exactly the same reason: the
    // context is the only thing holding those values, so a sweep without
    // this frees a literal a running loop is about to read again.
    for (auto & [proto, cache] : bigint_cache_) {
        for (auto & [index, v] : cache) { visit(root_label::bigint_cache, v); }
    }
    // And whatever the embedder holds: every DOM listener, timer callback
    // and element wrapper lives in the bindings, not in any VM structure.
    if (external_roots_) {
        external_roots_([&](value v) { visit(root_label::external, v); });
    }
}

// Grey one object: set the bit and queue it. Never walks edges, so it
// cannot recurse.
inline auto context::push_mark(heap_object * o) -> void {
    if (o == nullptr || o->marked) { return; }
    o->marked = true;
    mark_worklist_.push_back(o);
}

} // namespace ctbrowser::script
