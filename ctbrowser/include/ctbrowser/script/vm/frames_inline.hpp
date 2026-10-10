#pragma once
//===- frames_inline.hpp - Inline frame and root members --------*- C++ -*-===//
//
// Inline definitions of `context`'s frame-level machinery: the effective
// `this`, failure and throw-site recording, unwinding to a handler, the root
// inventory the collector and the escape oracle share, and marking.
//
//===----------------------------------------------------------------------===//

#include "../vm.hpp"

#include <algorithm>

namespace ctbrowser::script {

/// The `this` a frame actually sees. For an ordinary function that is its own
/// receiver; for an arrow it is the one captured where the arrow was written,
/// because an arrow never gets a receiver of its own. Both `load_this` and the
/// closure builder go through here, which is what makes an arrow nested inside
/// an arrow inside a method still resolve correctly.
[[nodiscard]] inline auto context::effective_this(const call_frame & f) -> value {
    value receiver = f.receiver_cell.is_undefined() ? f.receiver : f.receiver_cell;
    if (f.closure != nullptr && f.closure->proto != nullptr && f.closure->proto->is_arrow) {
        receiver = f.closure->captured_this;
    }
    return receiver.is_kind(heap_kind::cell) ? static_cast<cell_object *>(receiver.as_heap())->slot
                                             : receiver;
}

/// Marks the context failed with an uncatchable error, appending the stack it
/// happened on. Only the first failure is kept.
inline auto context::raise(std::string message) -> void {
    if (failed_) { return; }
    failed_ = true;
    error_ = std::move(message) + current_stack();
}

/// Records the instruction a throw leaves from, before any frame is discarded.
/// The innermost frame's `ip` is already past it (the loop's fetch increments
/// first), and a native pushes no frame, so a throw from inside one names its
/// call site. A frame whose proto keeps no offset table (a compiled body, or
/// a build without CTBROWSER_SCRIPT_DEBUG_NAMES) leaves no site rather than a
/// caller's.
///
/// A rethrow from a finally or an iterator close (function_proto::rethrows)
/// is the caught throw continuing, and keeps that throw's site:
/// `try { null.x } finally { log() }` is reported at `null.x`, not at the
/// finally. That holds only while the value is still the one the site was
/// recorded for; a throw caught inside the finally replaced the site, and
/// then there is none to keep.
inline auto context::note_throw_site() noexcept -> void {
    const throw_site caught = throw_site_;
    throw_site_ = {};
    if (frames_.empty()) { return; }
    const call_frame & f = frames_.back();
    const function_proto * fp = f.proto;
    if (fp == nullptr || fp->aot_entry != nullptr || f.ip == 0 || f.ip > fp->code_offsets.size()) {
        return;
    }
    if (std::ranges::binary_search(fp->rethrows, static_cast<std::uint32_t>(f.ip - 1))) {
        if (thrown_.bits() == throw_site_value_) { throw_site_ = caught; }
        return;
    }
    const std::uint32_t offset = fp->code_offsets[f.ip - 1];
    if (offset == function_proto::no_offset) { return; }
    throw_site_.source =
        f.closure != nullptr && f.closure->owner != nullptr ? f.closure->owner : program_;
    throw_site_.offset = offset;
    throw_site_value_ = thrown_.bits();
}

/// Finds the innermost live handler and jumps to it, discarding every call
/// frame between here and the one that owns it. Returns false when nothing
/// caught the throw, which makes it an uncaught exception.
[[nodiscard]] inline auto context::unwind_to_handler() -> bool {
    ++unwinds_;
    note_throw_site();
    while (!handlers_.empty()) {
        const handler h = handlers_.back();
        handlers_.pop_back();
        if (h.fence) {
            // A C++ caller catches here; see call_fenced. Every frame the
            // callee pushed goes, but the caller's registers stay (a native
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
        if (h.frame >= frames_.size()) { continue; } // Its frame already returned.
        // The escape oracle (type_record.cpp) sees the frames before they are
        // discarded; this is the `unwind` row of
        // ctcompile/include/ctcompile/JavaScript/FrameEnds.def. `thrown_` is
        // still set here (it is cleared below, after the landing write), so a
        // value in flight is a root and an object thrown out of a frame reads
        // as escaped `via thrown` rather than as confined. Only frames above
        // the handler's are ending; a throw caught in its own frame discards
        // none and records nothing.
        if (recorder_ != nullptr && h.frame + 1 < frames_.size()) [[unlikely]] {
            record_frames_unwound(h.frame + 1);
        }
        frames_.resize(h.frame + 1);
        call_frame & target = frames_.back();
        target.ip = h.address;
        if (registers_.size() < h.reg_top) { registers_.resize(h.reg_top, value::undefined()); }
        registers_[target.base + h.slot] = thrown_;
        // Record which slot that was. The handler carrying it has already been
        // popped above, so this is the last moment anything knows.
        target.landed_slot = h.slot;
        thrown_ = value::undefined();
        return true;
    }
    return false;
}

/// The single root inventory: every root the collector walks, in
/// CTBROWSER_ROOT_LABELS order and with that label on each, handed to a
/// visitor. collect() marks through it; the escape oracle (type_record.cpp)
/// walks the same inventory with the ending frame's dead register window
/// excluded, so the oracle cannot miss a root the collector knows. A template
/// rather than a virtual so collect()'s visitor inlines.
///
/// `register_limit` bounds the flat register file: slots at or above it are
/// not visited. `frame_limit` bounds the per-frame roots: frames at or above
/// that index are not visited. collect() passes the whole of both. The oracle
/// passes the ending frame's base and index: an inline callee sits inside its
/// caller's register extent (VM_CASE(call) places it at base + a + 1) and
/// `ret` never shrinks registers_, so without the bound every object an ending
/// frame still held in a register would read as reachable, and "reachable in
/// the VM" is not "reachable in the program".
template <class Visit>
inline auto context::each_root(std::size_t register_limit, std::size_t frame_limit, Visit && visit)
    -> void {
    // A script can replace every visible alias of its global object. The
    // realm still owns it between turns, and the next script receives it.
    // Every realm, not only the current one: a frame's closures outlive the
    // call that made them, and the page may call into one at any time.
    for (const auto & realm : realms_) {
        visit(root_label::globals, realm->global_this);
        for (const auto & [name, v] : realm->globals) { visit(root_label::globals, v); }
        for (const auto & [name, v] : realm->platform_prototypes) { visit(root_label::globals, v); }
        // The prototype tables hold every builtin method. Nothing else
        // references them, so without this the standard library would be
        // collected on the first GC. The same holds for the NativeError
        // prototypes: an engine-raised error is put on one, and a page that
        // never mentions `RangeError` holds no other reference to its table.
        const realm_intrinsics & intrinsics = *realm->intrinsics;
        for (object_object * table : intrinsics.prototypes) {
            if (table != nullptr) { visit(root_label::prototypes, value::object(table)); }
        }
        for (const auto & [kind, table] : intrinsics.error_prototypes) {
            (void)kind;
            if (table != nullptr) { visit(root_label::prototypes, value::object(table)); }
        }
        visit(root_label::prototypes, intrinsics.throw_type_error);
    }
    for (const auto & [name, v] : realm_template_) { visit(root_label::globals, v); }
    for (const auto & [name, v] : realm_template_platform_prototypes_) {
        visit(root_label::globals, v);
    }
    const std::size_t top = std::min(register_limit, registers_.size());
    for (std::size_t i = 0; i < top; ++i) { visit(root_label::registers, registers_[i]); }
    // The receiver of a native call in progress. It is held in a C++ local,
    // not in a register, so nothing else would keep the object the method is
    // running on alive.
    visit(root_label::current_this, current_this_);
    // The constructor a super() call is in the middle of handing on. It
    // lives only in this slot between the two instructions, which is
    // exactly the window a collection can fall in.
    visit(root_label::pending_new_target, pending_new_target_);
    visit(root_label::pending_closure, pending_closure_);
    // And the closure each live frame is executing. A function called from C++
    // via call() is likewise referenced only from a C++ local; without this
    // its upvalues could be freed while its body is still running.
    const std::size_t frames = std::min(frame_limit, frames_.size());
    for (std::size_t k = 0; k < frames; ++k) {
        const call_frame & f = frames_[k];
        if (f.closure != nullptr) { visit(root_label::frame_closure, value::object(f.closure)); }
        visit(root_label::frame_receiver, f.receiver);
        visit(root_label::frame_receiver, f.receiver_cell);
        visit(root_label::frame_arguments, f.arguments_object);
        visit(root_label::frame_async_promise, f.async_promise);
        visit(root_label::frame_new_target, f.new_target);
    }
    // A queued job and its arguments. Nothing else refers to them between the
    // moment they are queued and the moment they run, which is exactly the
    // window a collection can fall in.
    for (const microtask & job : microtasks_) {
        visit(root_label::microtasks, job.fn);
        for (const value & arg : job.args) { visit(root_label::microtasks, arg); }
    }
    visit(root_label::microtasks, await_job_); // The native every settled await queues.
    // Every module's export cells. They live in `modules_` and in no register
    // once the module has finished evaluating, so without this a collection
    // between two modules would free the bindings the second one is about to
    // import, while a closure inside the first still refers to the same cell.
    for (auto & [specifier, mod] : modules_) {
        for (auto & [name, cell] : mod.exports) { visit(root_label::module_exports, cell); }
        visit(root_label::module_namespace, mod.namespace_object);
        // The deferred namespace is a cache too: unvisited, a page that dropped
        // it would free it, and the next `import defer` would get the freed
        // object back.
        visit(root_label::module_namespace, mod.deferred_namespace_object);
    }
    // A thrown value in flight is reachable from nothing else.
    visit(root_label::thrown, thrown_);
    visit(root_label::thrown, fence_thrown_);  // Caught by a C++ fence, not yet consumed.
    visit(root_label::thrown, pending_throw_); // Parked by `call`, not yet rethrown.
    // And what a C++ scope is holding across a call. `construct` allocates the
    // instance and then runs field initialisers and the constructor body with
    // it in a local; without this the object being constructed would be freed
    // by any collection inside either. See context::rooted.
    for (const value & v : temporaries_) { visit(root_label::temporaries, v); }
    // The per-function string cache. These are live `value`s held by the
    // context itself and referenced from nowhere else; a sweep without them
    // would free a string literal that a running loop is about to read again.
    for (auto & [proto, cache] : string_cache_) {
        for (auto & [index, v] : cache) { visit(root_label::string_cache, v); }
    }
    // The BigInt literal cache is a root for exactly the same reason: the
    // context is the only thing holding those values, so a sweep without
    // this frees a literal a running loop is about to read again.
    for (auto & [proto, cache] : bigint_cache_) {
        for (auto & [index, v] : cache) { visit(root_label::bigint_cache, v); }
    }
    // And whatever the embedder holds: every DOM listener, timer callback and
    // element wrapper lives in the bindings, not in any VM structure.
    if (external_roots_) {
        external_roots_([&](value v) { visit(root_label::external, v); });
    }
}

/// Greys one object: sets its mark bit and queues it. Never walks edges, so it
/// cannot recurse.
inline auto context::push_mark(heap_object * o) -> void {
    if (o == nullptr || o->marked) { return; }
    o->marked = true;
    mark_worklist_.push_back(o);
}

} // namespace ctbrowser::script
