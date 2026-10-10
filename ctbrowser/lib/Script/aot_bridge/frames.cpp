//===- frames.cpp - AOT helpers for the frame protocol --------------------===//
//
// The AOT runtime helpers for the frame protocol: ct_aot_enter and
// ct_aot_leave, the frame's own reads (this, callee, new.target, home, the
// register and argument spans, arguments and rest), exceptions, ct_aot_check
// and ct_aot_call. Each extern "C" function implements one row (one
// CT_AOT_HELPER entry) of aot_helpers.def; the other helpers are in
// operators.cpp, properties.cpp and calls.cpp, and internal.hpp explains how
// they reach context's private state.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::aot {

extern "C" {

/// ct_aot_enter, as its row in aot_helpers.def specifies: push a call_frame
/// carrying the image's real function_proto, reserve reg_count slots in
/// context::registers_ (which each_root traces), count the frame against the
/// interpreter's 512-frame stack limit, and consume and clear
/// pending_new_target_ exactly as context::invoke (vm/call/invoke.cpp) does.
/// Returns null on stack exhaustion; the caller propagates the RangeError
/// without leaving a frame that was never entered.
ct_aot_frame * ct_aot_enter(ct_aot_ctx * c, const ct_aot_site * site, std::uint32_t reg_count,
                            std::uint64_t receiver, void * storage) {
    context & cx = aot_bridge::ctx_of(c);
    const auto * proto = reinterpret_cast<const function_proto *>(site);

    // Check the stack limit first, against frames_ rather than a separate
    // counter. A compiled body pushes a real frame onto the same vector
    // context::invoke checks, so a second counter would only have to be kept
    // equal to frames_.size(). 512 matches the interpreter's limit in
    // vm/call/invoke.cpp.
    if (cx.frames_.size() > 512) {
        cx.throw_error("RangeError", "Maximum call stack size exceeded");
        return nullptr;
    }

    auto * held = new (storage) aot_frame_storage{};
    held->ctx = &cx;
    held->register_base = cx.registers_.size();
    held->handler_base = cx.handlers_.size();

    // Reserve the register span before pushing the frame: vm.hpp writes
    // registers_[base + slot] unconditionally when a handler catches, so the
    // span must exist before any handler can fire. The extra 8 slots are the
    // interpreter's own margin from context::invoke.
    cx.registers_.resize(held->register_base + reg_count + 8u, value::undefined());

    held->frame_index = cx.frames_.size();
    held->slot_count = reg_count;
    context::call_frame entered{};
    entered.proto = proto;
    entered.base = held->register_base;
    entered.handler_base = held->handler_base;
    // The receiver becomes a GC root here: each_root (vm.hpp) marks
    // call_frame::receiver for every live frame. For `new` this matters,
    // because op::construct's fresh instance is otherwise only a C++ local
    // until the body returns.
    entered.receiver = value::from_bits(receiver);
    entered.new_target = cx.pending_new_target_;
    // The argument count. Shared members such as make_arguments_object loop to
    // call_frame::argc, so leaving it unset would give empty `arguments` and
    // rest arrays.
    //
    // Truncated to uint16_t exactly as context::call does: a call with more
    // than 65535 arguments already miscounts in the interpreter, and compiled
    // code must match the interpreter rather than be more correct than it.
    entered.argc = static_cast<std::uint16_t>(cx.pending_argc_);
    // The closure, which is the only way a compiled body reaches what it
    // captured: the entry ABI delivers `site`, the shared function_proto, and
    // upvalues live on the closure instance.
    //
    // A non-closure becomes nullptr rather than being cast. A top-level entry
    // passes undefined, and "no closure" is not the same as "a closure with no
    // upvalues": ct_aot_upvalue_cell and ct_aot_callee both distinguish them,
    // exactly as VM_CASE(get_upvalue) and VM_CASE(load_callee) do.
    entered.closure = cx.pending_closure_.is_kind(heap_kind::function)
                          ? static_cast<closure_object *>(cx.pending_closure_.as_heap())
                          : nullptr;
    // And its realm, as the interpreter's frame would have it.
    entered.realm = cx.realm_for(entered.closure);
    // Pushed before pending_new_target_ is cleared, not after. Between copying
    // it into a C++ local and clearing the root, the constructor a super() is
    // handing on would be reachable from nothing, which is the window aot.hpp
    // warns about. Nothing collects in that gap today; ordering it correctly
    // costs a line and removes the question.
    cx.frames_.push_back(entered);
    // The arriving argument window, consumed and cleared like the two roots
    // around it. enter_compiled_body set it, as the last place that knows where
    // the caller put the arguments.
    held->argv_base = cx.pending_argv_base_;
    held->argv_count = cx.pending_argc_;
    cx.pending_argv_base_ = 0;
    cx.pending_argc_ = 0;
    cx.pending_new_target_ = value::undefined();
    // Cleared after the push: until then the frame does not hold the closure,
    // and clearing first would leave it reachable from nothing. Once the frame
    // is pushed, each_root (vm.hpp) traces it through call_frame::closure.
    cx.pending_closure_ = value::undefined();
    return reinterpret_cast<aot::ct_aot_frame *>(held);
}

/// ct_aot_this: `this` as the frame sees it, through context::effective_this,
/// the same function VM_CASE(load_this) runs. The raw call_frame::receiver
/// would be wrong for an arrow function: `() => this` inside a method must see
/// the method's object, not the arrow frame's own (undefined) receiver.
std::uint64_t ct_aot_this(ct_aot_frame * f) {
    const aot_frame_storage & held = aot_bridge::frame_of(f);
    const context & cx = *held.ctx;
    if (held.frame_index >= cx.frames_.size()) { return value::undefined().bits(); }
    const auto & record = cx.frames_[held.frame_index];
    const value receiver = context::effective_this(record);
    // OrdinaryCallBindThis (10.2.1.2), as in the VM's load_this handler.
    if (!record.proto->is_strict && !record.proto->is_arrow && receiver.is_nullish()) {
        return record.realm->global_this.bits();
    }
    return receiver.bits();
}

/// ct_aot_callee. VM_CASE(load_callee) is
/// `closure != nullptr ? value::object(closure) : undefined`, and this is that.
/// It is also the only way a compiled body can reach its own upvalues, since
/// they live on the closure instance and `site` is the shared function_proto.
std::uint64_t ct_aot_callee(ct_aot_frame * f) {
    const context::call_frame * record = aot_bridge::frame_record(f);
    if (record == nullptr || record->closure == nullptr) { return value::undefined().bits(); }
    return value::object(record->closure).bits();
}

/// ct_aot_pass_new_target. Takes no operand because it reads this frame's own
/// new.target: an explicit parameter would let a caller hand over a stale or
/// cached one, which ct_aot_new_target's row forbids.
///
/// It must immediately precede the ct_aot_call it serves. That is a lowering
/// invariant the ABI cannot enforce: the flag is consumed by the next
/// JavaScript closure frame push, whatever that turns out to be.
void ct_aot_pass_new_target(ct_aot_frame * f) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    const context::call_frame * record = aot_bridge::frame_record(f);
    cx.pass_new_target(record == nullptr ? value::undefined() : record->new_target);
}

/// ct_aot_new_target. VM_CASE(load_new_target), one field read. It is correct
/// only because ct_aot_enter copies pending_new_target_ into the frame, and
/// both context::construct_new and ct_aot_pass_new_target set it, so a compiled
/// constructor sees new.target whether it was reached from compiled code or
/// from the interpreter. Babel's _classCallCheck tests new.target, so nearly
/// every transpiled class depends on this.
std::uint64_t ct_aot_new_target(ct_aot_frame * f) {
    const context::call_frame * record = aot_bridge::frame_record(f);
    return record == nullptr ? value::undefined().bits() : record->new_target.bits();
}

/// ct_aot_home. VM_CASE(load_home), which is what `super` resolves against.
///
/// The value is copied out and no pointer crosses the ABI: find() answers a
/// pointer into closure_object::props, and any later set() on that closure
/// invalidates it.
std::uint64_t ct_aot_home(ct_aot_frame * f) {
    const context::call_frame * record = aot_bridge::frame_record(f);
    if (record == nullptr || record->closure == nullptr) { return value::undefined().bits(); }
    if (value * found = record->closure->find("__home")) { return found->bits(); }
    return value::undefined().bits();
}

/// ct_aot_upvalue_cell. The guarded fetch from VM_CASE(get_upvalue), minus the
/// cell read, which is ct_aot_cell_get's job.
///
/// The split keeps the pair exact. get_upvalue answers undefined for a missing
/// closure, an out-of-range index, or a slot that is not a cell; this covers
/// the first two and cell_get covers the third by no-oping on a non-cell.
/// Composed, the two guards are the one guard the interpreter writes inline,
/// so `cell_get(upvalue_cell(fr, i))` is get_upvalue.
std::uint64_t ct_aot_upvalue_cell(ct_aot_frame * f, std::uint32_t index) {
    const context::call_frame * record = aot_bridge::frame_record(f);
    if (record == nullptr || record->closure == nullptr) { return value::undefined().bits(); }
    if (index >= record->closure->upvalues.size()) { return value::undefined().bits(); }
    return record->closure->upvalues[index].bits();
}

/// ct_aot_make_arguments and ct_aot_gather_rest, through the shared members.
///
/// These cannot throw a JavaScript exception, only fail with an uncatchable
/// error (the allocation ceiling; "raise tier" in aot_helpers.def). They return
/// the array rather than a status, so after a failure the value is meaningless
/// and the caller checks ct_aot_failed at its next loop back edge. They
/// allocate, so both are safepoints; neither can run user code.
///
/// An unwound frame still gets a well-formed value. frame_record answers null
/// once frames_ has been truncated below this frame, and these helpers have no
/// status to report that with, so the answer is an empty array, not a
/// dereference.
std::uint64_t ct_aot_make_arguments(ct_aot_frame * f, const std::uint64_t * slots,
                                    std::uint32_t argc) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    context::call_frame * record = aot_bridge::frame_record(f);
    if (record == nullptr) { return cx.make_array().bits(); }
    return cx.make_arguments_object(*record, reinterpret_cast<const value *>(slots), argc).bits();
}

std::uint64_t ct_aot_gather_rest(ct_aot_frame * f, const std::uint64_t * slots, std::uint32_t argc,
                                 std::uint32_t from) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    const context::call_frame * record = aot_bridge::frame_record(f);
    if (record == nullptr) { return cx.make_array().bits(); }
    return cx.gather_rest_values(*record, reinterpret_cast<const value *>(slots), argc, from)
        .bits();
}

/// ct_aot_args and ct_aot_argc. Where the arriving arguments are, and how
/// many. This frame's own slots cannot answer either, because ct_aot_enter puts
/// them above the caller's window.
std::uint64_t * ct_aot_args(ct_aot_frame * f) {
    const aot_frame_storage & held = aot_bridge::frame_of(f);
    context & cx = *held.ctx;
    // The same bound test ct_aot_slots makes, against base + count rather than
    // base alone: an unwound frame truncates registers_ to exactly the base, and
    // a one-past-the-end pointer is worse than a null one.
    if (held.argv_base + held.argv_count > cx.registers_.size()) { return nullptr; }
    return reinterpret_cast<std::uint64_t *>(cx.registers_.data() + held.argv_base);
}

std::uint32_t ct_aot_argc(ct_aot_frame * f) {
    const context::call_frame * record = aot_bridge::frame_record(f);
    return record == nullptr ? 0u : record->argc;
}

/// ct_aot_slots. Returns the frame's own register span, which is where a
/// compiled body keeps a value that must survive a safepoint.
///
/// The pointer is valid only until the next safepoint. registers_ is a
/// std::vector and a nested call resizes it, so this recomputes the base every
/// time, as the interpreter's own reg() does.
std::uint64_t * ct_aot_slots(ct_aot_frame * f) {
    const aot_frame_storage & held = aot_bridge::frame_of(f);
    context & cx = *held.ctx;
    // Check that the whole span still exists. An unwound frame truncates
    // registers_ to exactly register_base, so testing base alone would return
    // a one-past-the-end pointer, and the write that follows would corrupt
    // memory somewhere unrelated.
    if (held.register_base + held.slot_count > cx.registers_.size()) { return nullptr; }
    return reinterpret_cast<std::uint64_t *>(cx.registers_.data() + held.register_base);
}

/// ct_aot_leave. Pops the frame, truncates handlers_ to
/// call_frame::handler_base exactly as op::ret does, and releases the register
/// span. It must not run on the unwound path, where the unwinder already
/// destroyed the frame, and is a harmless no-op after a failure.
void ct_aot_leave(ct_aot_frame * f) {
    aot_frame_storage & held = aot_bridge::frame_of(f);
    context & cx = *held.ctx;
    // Not frames_.back(): a nested call made while `failed_` is set can leave
    // a foreign frame on top, and popping it would destroy someone else's.
    // Truncating to this frame's own index is the same operation when nothing
    // went wrong and the correct one when something did.
    if (cx.frames_.size() > held.frame_index) { cx.frames_.resize(held.frame_index); }
    if (cx.handlers_.size() > held.handler_base) { cx.handlers_.resize(held.handler_base); }
    if (cx.registers_.size() > held.register_base) { cx.registers_.resize(held.register_base); }
    held.~aot_frame_storage();
}

/// ct_aot_leave_return. ct_aot_leave, after recording the pop for the frame
/// recorder. Only a normal return with exactly this frame on top supplies
/// enough evidence to record. The frame copy and status check stay off the
/// ordinary path when recording is disabled; foreign-frame cleanup is left to
/// ct_aot_leave unrecorded.
void ct_aot_leave_return(ct_aot_frame * f, std::uint64_t result) {
    const aot_frame_storage & held = aot_bridge::frame_of(f);
    context & cx = *held.ctx;
    if (cx.recorder_ != nullptr && cx.frames_.size() == held.frame_index + 1 &&
        aot_bridge::check(f) == static_cast<std::int32_t>(aot::ct_aot_status::ok)) [[unlikely]] {
        const context::call_frame popped = cx.frames_[held.frame_index];
        cx.frames_.resize(held.frame_index);
        cx.record_frame_pop(popped, value::from_bits(result), true);
    }
    ct_aot_leave(f);
}

// ---- Exceptions: try/catch inside compiled bodies ------------------------
//
// unwind_to_handler pops the handler before it writes the thrown value, so a
// compiled body could not find out afterwards which register received it.
// call_frame therefore records that register (landed_slot) when
// unwind_to_handler writes it, and ct_aot_catch_land reads it from there.
// Passing the slot as an extra helper argument would also work, but would
// change a signature both code generators are written against.
// unittests/unit/aot/throw covers this path.

/// ct_aot_handler_push. The pad id is the body's own label, carried in
/// handler::address with CT_AOT_PAD_BIT set, so unwind_to_handler's
/// `target.ip = h.address` hands a compiled body its landing pad using the
/// same steps that resume the interpreter at a catch block. The unwinder
/// needs no change.
void ct_aot_handler_push(ct_aot_frame * f, std::uint32_t pad, std::uint32_t slot) {
    const aot_frame_storage & held = aot_bridge::frame_of(f);
    context & cx = *held.ctx;
    cx.handlers_.push_back(
        context::handler{held.frame_index, static_cast<std::size_t>(pad) | CT_AOT_PAD_BIT,
                         cx.registers_.size(), static_cast<std::uint16_t>(slot)});
}

/// ct_aot_handler_pop. Pops the innermost handler on the whole stack without
/// consulting handler_base, as op::pop_handler does, so a mis-balanced
/// emission would drop a caller's catch. Balance is a compiler invariant; the
/// frame handle is carried so this can at least refuse a foreign handler.
void ct_aot_handler_pop(ct_aot_frame * f) {
    const aot_frame_storage & held = aot_bridge::frame_of(f);
    context & cx = *held.ctx;
    if (cx.handlers_.empty()) { return; }
    // Not an assert, because this is reachable from an image: a body popping a
    // handler it did not push is a broken image rather than a broken runtime,
    // and refusing is better than corrupting a caller's handler.
    if (cx.handlers_.back().frame != held.frame_index) { return; }
    cx.handlers_.pop_back();
}

/// ct_aot_throw. The row: `thrown_ = v; if (!unwind_to_handler()) raise(...)`,
/// then ct_aot_check for the status, so this classifies identically to every
/// other throwing helper. It never returns ok.
std::int32_t ct_aot_throw(ct_aot_frame * f, std::uint64_t thrown) {
    const aot_frame_storage & held = aot_bridge::frame_of(f);
    context & cx = *held.ctx;
    cx.thrown_ = value::from_bits(thrown);
    if (!cx.unwind_to_handler()) {
        // Nothing caught it. unwind_to_handler pops as it searches, so
        // handlers_ is empty and thrown_ is still set, which keeps the object
        // alive while its own toString runs inside describe_thrown.
        cx.raise("uncaught " + cx.describe_thrown(cx.thrown_));
    }
    return aot_bridge::check(f);
}

/// ct_aot_catch_land. A pure read of what unwind_to_handler deposited: the
/// pad id from `ip`, and the thrown value from the slot the frame recorded.
std::uint32_t ct_aot_catch_land(ct_aot_frame * f, std::uint64_t * out_thrown) {
    const aot_frame_storage & held = aot_bridge::frame_of(f);
    context & cx = *held.ctx;
    if (held.frame_index >= cx.frames_.size()) {
        *out_thrown = value::undefined().bits();
        return 0;
    }
    context::call_frame & frame = cx.frames_[held.frame_index];
    const auto pad = static_cast<std::uint32_t>(frame.ip & ~CT_AOT_PAD_BIT);
    // Clear the landing pad so that calling this twice does not catch twice.
    frame.ip = 0;
    *out_thrown = cx.registers_[frame.base + frame.landed_slot].bits();
    return pad;
}

/// ct_aot_check. Its body is in-class in internal.hpp so the other helpers'
/// calls to it inline.
std::int32_t ct_aot_check(ct_aot_frame * f) {
    return aot_bridge::check(f);
}

/// ct_aot_call. Routes through context::call rather than around it, which
/// gets three things right for free: a generator callee runs nothing, a native
/// callee's current_this_ is saved and restored, and the pending_new_target_
/// handoff a non-spread super() needs happens.
///
/// `key` and `site` are only for error messages and are unused here:
/// op::call_computed names its callee with a run-time to_string, and
/// callee_origin scans bytecode an AOT frame has no ip for. They stay in the
/// signature so it matches the declaration in aot_helpers.def that the code
/// generators call.
std::int32_t ct_aot_call(ct_aot_frame * f, std::uint64_t callee, std::uint64_t receiver,
                         const std::uint64_t * argv, std::uint32_t argc, std::uint64_t key,
                         const ct_aot_site * site, std::uint64_t * out) {
    (void)key;
    (void)site;
    const aot_frame_storage & held = aot_bridge::frame_of(f);
    context & cx = *held.ctx;

    // No safepoint here: context::call starts with one, so a second one just
    // before it would add nothing, even though the row declares ct_aot_call
    // is_safepoint.
    //
    // Copied out before the call: `argv` points into registers_ for a compiled
    // body, and context::call resizes that vector, so the span handed to it
    // must not be the vector's own storage.
    std::vector<value> args;
    args.reserve(argc);
    for (std::uint32_t i = 0; i < argc; ++i) { args.push_back(value::from_bits(argv[i])); }

    const value produced = cx.call(value::from_bits(callee), args, value::from_bits(receiver));

    // The row's three reachable states, tested in ct_aot_check's order and for
    // its reasons. *out is written only on ok, because an unwound call's
    // undefined is indistinguishable from a real one.
    const std::int32_t status = aot_bridge::check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

/// ct_aot_return_value. The row requires the `constructing` argument (a
/// two-argument form would miscompile): it is a run-time property of the
/// frame, and substituting the receiver is what makes a class constructor
/// with no explicit return evaluate to the instance. is_object_like is the
/// predicate, kept behind the helper because it is subtler than it looks: an
/// array, a function and a proxy all count.
std::uint64_t ct_aot_return_value(std::uint64_t returned, std::uint64_t receiver,
                                  std::uint32_t constructing) {
    const value produced = value::from_bits(returned);
    if (constructing != 0u && !produced.is_object_like()) { return receiver; }
    return returned;
}

} // extern "C"

} // namespace ctbrowser::aot
