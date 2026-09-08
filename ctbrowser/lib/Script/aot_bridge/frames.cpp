// The AOT ABI - the frame protocol: ct_aot_enter and ct_aot_leave, the frame's
// own reads (this, callee, new.target, home, the register and argument
// spans, arguments and rest), the throwing tier, ct_aot_check and ct_aot_call.
//
// One of four files carved out of a 1,586-line aot_bridge.cpp on 2026-09-08.
// `struct aot_bridge` is declared in internal.hpp beside this; each of its
// bodies is defined in the file that also holds the extern "C" row calling
// it, so a wrapper and the member behind it are still one translation unit
// and the call still inlines. The four members every file calls - ctx_of,
// frame_of, frame_record and check - stay in-class in the header for the
// same reason.

#include "internal.hpp"

namespace ctbrowser::script {

// ct_aot_enter. The row: push a call_frame carrying the image's real
// function_proto, reserve reg_count slots in context::registers_ (GC root
// 2), count the frame against the 512 guard, and consume-and-clear
// pending_new_target_ exactly as call.cpp does. Returns NULL on the depth
// raise, and the caller returns FAILED without leaving.
aot::ct_aot_frame * aot_bridge::enter(aot::ct_aot_ctx * c, const aot::ct_aot_site * site,
                                      std::uint32_t reg_count, std::uint64_t receiver,
                                      void * storage) {
    context & cx = ctx_of(c);
    const auto * proto = reinterpret_cast<const function_proto *>(site);

    // THE GUARD FIRST, and against frames_ rather than a separate counter.
    // The row asks for "a new native_depth_ so the 512 guard counts AOT
    // frames"; it does not need one, because this pushes a REAL frame onto
    // the same vector the guard already reads. A second counter would be a
    // second thing to keep in step with frames_.size(), which is exactly
    // what ct_aot_check's precedence is defined against.
    if (cx.frames_.size() > 512) {
        cx.raise("call stack exhausted");
        return nullptr;
    }

    auto * held = new (storage) aot_frame_storage{};
    held->ctx = &cx;
    held->register_base = cx.registers_.size();
    held->handler_base = cx.handlers_.size();

    // THE REGISTER SPAN IS RESERVED BEFORE THE FRAME EXISTS, which the row
    // insists on for a reason it states: vm.hpp writes
    // registers_[base + slot] UNCONDITIONALLY for a winning handler, so the
    // span has to exist before the first handler could fire. The `+ 8` is
    // the interpreter's own margin (call.cpp), copied rather than reasoned
    // about again.
    cx.registers_.resize(held->register_base + reg_count + 8u, value::undefined());

    held->frame_index = cx.frames_.size();
    held->slot_count = reg_count;
    context::call_frame entered{};
    entered.proto = proto;
    entered.base = held->register_base;
    entered.handler_base = held->handler_base;
    // THE RECEIVER IS A ROOT AND THIS IS WHERE IT BECOMES ONE. objects.cpp
    // marks call_frame::receiver for every live frame; a compiled body's
    // `this` was in no frame field and therefore in no root. For `new` that
    // was not survivable by accident: op::construct's fresh instance is a
    // C++ local until the body returns.
    entered.receiver = value::from_bits(receiver);
    entered.new_target = cx.pending_new_target_;
    // AND HOW MANY ARGUMENTS ARRIVED, which this frame reported as ZERO
    // until now - the field simply was never set for a compiled frame. Any
    // shared member that loops to the frame's argc therefore built an empty
    // `arguments` object and an empty rest array in the compiled tier while
    // the interpreted one was correct.
    //
    // TRUNCATED THE SAME WAY context::call TRUNCATES IT. call_frame::argc is
    // a uint16_t and the ABI's argc is 32-bit, so a call with more than
    // 65535 arguments already miscounts in the VM. Reproducing that is
    // right: a compiled tier that was MORE correct here would be a
    // differential failure rather than a fix.
    entered.argc = static_cast<std::uint16_t>(cx.pending_argc_);
    // AND THE CLOSURE, WHICH IS THE ONLY WAY A COMPILED BODY REACHES WHAT
    // IT CAPTURED. The entry ABI delivers `site` - the shared
    // function_proto - and upvalues live on the closure INSTANCE.
    //
    // A NON-CLOSURE BECOMES nullptr RATHER THAN A CAST. A top-level entry
    // passes undefined, and "no closure" is not the same as "a closure with
    // no upvalues": ct_aot_upvalue_cell and ct_aot_callee both distinguish
    // them, exactly as VM_CASE(get_upvalue) and VM_CASE(load_callee) do.
    entered.closure = cx.pending_closure_.is_kind(heap_kind::function)
                          ? static_cast<closure_object *>(cx.pending_closure_.as_heap())
                          : nullptr;
    // PUSHED BEFORE THE CLEAR, not after. Between copying
    // pending_new_target_ into a C++ local and clearing the root, the
    // constructor a super() is handing on is reachable from nothing - which
    // is the precise window GCRoots.def warns about, reproduced here.
    // Nothing collects in that gap today; ordering it correctly costs a
    // line and removes the question.
    cx.frames_.push_back(entered);
    // THE ARRIVING WINDOW, CONSUMED AND CLEARED like the two roots below -
    // set by enter_compiled_body, which is the last place that knows where
    // the caller put them.
    held->argv_base = cx.pending_argv_base_;
    held->argv_count = cx.pending_argc_;
    cx.pending_argv_base_ = 0;
    cx.pending_argc_ = 0;
    cx.pending_new_target_ = value::undefined();
    // CLEARED AFTER THE PUSH for the reason above it: between copying the
    // handoff into the frame and clearing the root, the value is reachable
    // from both - and clearing first would leave a window where it is
    // reachable from neither. call_frame::closure is GC root 4, so once the
    // frame is pushed the closure is traced.
    cx.pending_closure_ = value::undefined();
    return reinterpret_cast<aot::ct_aot_frame *>(held);
}

// ct_aot_this. `this` as the frame sees it.
//
// THROUGH effective_this, WHICH THE ROW ALWAYS SAID and this did not: it
// returned call_frame::receiver directly. For every ordinary function the
// two are the same value, so nothing could tell them apart - an ARROW is
// the only shape that separates them, and until compiled code could build
// one there was no way to write the test. `() => this` inside a method read
// the arrow frame's own receiver, which is undefined for a plain call,
// instead of the method's object.
//
// effective_this is also what VM_CASE(load_this) runs, which is where the
// importer sends op::load_this - so the two tiers now answer the same
// question rather than two similar ones.
std::uint64_t aot_bridge::this_value(aot::ct_aot_frame * f) {
    const aot_frame_storage & held = frame_of(f);
    const context & cx = *held.ctx;
    if (held.frame_index >= cx.frames_.size()) { return value::undefined().bits(); }
    return context::effective_this(cx.frames_[held.frame_index]).bits();
}

// ct_aot_callee. VM_CASE(load_callee) is
// `closure != nullptr ? value::object(closure) : undefined`, and this is
// that - which is also the only way a compiled body can reach its own
// upvalues, since they live on the closure INSTANCE and `site` is the
// shared function_proto.
std::uint64_t aot_bridge::callee(aot::ct_aot_frame * f) {
    const context::call_frame * record = frame_record(f);
    if (record == nullptr || record->closure == nullptr) { return value::undefined().bits(); }
    return value::object(record->closure).bits();
}

// ct_aot_pass_new_target. Zero-operand at the ABI because it reads THIS
// frame's new.target - an explicit parameter would let a caller hand over a
// stale or cached one, which ct_aot_new_target's row forbids outright.
//
// ADJACENCY TO THE ct_aot_call THAT FOLLOWS IS A LOWERING INVARIANT the ABI
// cannot enforce: the flag is consumed by the next JS-closure frame push,
// whatever that turns out to be.
void aot_bridge::pass_new_target(aot::ct_aot_frame * f) {
    context & cx = *frame_of(f).ctx;
    const context::call_frame * record = frame_record(f);
    cx.pass_new_target(record == nullptr ? value::undefined() : record->new_target);
}

// ct_aot_new_target. VM_CASE(load_new_target), which is one field read.
//
// IT WAS BLOCKED BY SOMETHING THAT WAS HALF FIXED. The lowering refused any
// function mentioning new.target, and the reason it gave was that
// ct_aot_enter takes new_target from pending_new_target_ and op::construct
// never set that on the compiled path. context::construct_new set it - so a
// compiled constructor reached from COMPILED code was already right, and a
// compiled constructor reached from the INTERPRETER still saw undefined.
// Only the differential case that read new.target found the second half;
// it went in claiming to cover pass_new_target, and a mutant that deleted
// that lowering entirely left it green.
//
// THAT UNBLOCKS EVERY TRANSPILED CLASS: Babel's _classCallCheck guard is a
// new.target test, so it appears in almost every bundled class.
std::uint64_t aot_bridge::new_target(aot::ct_aot_frame * f) {
    const context::call_frame * record = frame_record(f);
    return record == nullptr ? value::undefined().bits() : record->new_target.bits();
}

// ct_aot_home. VM_CASE(load_home), which is what `super` resolves against.
//
// THE VALUE IS COPIED OUT AND NO POINTER CROSSES THE ABI: find() answers a
// pointer into closure_object::props, and any later set() on that closure
// invalidates it.
std::uint64_t aot_bridge::home(aot::ct_aot_frame * f) {
    const context::call_frame * record = frame_record(f);
    if (record == nullptr || record->closure == nullptr) { return value::undefined().bits(); }
    if (value * found = record->closure->find("__home")) { return found->bits(); }
    return value::undefined().bits();
}

// ct_aot_upvalue_cell. The guarded fetch from VM_CASE(get_upvalue), MINUS
// the cell read - which is ct_aot_cell_get's job.
//
// THE SPLIT IS WHAT MAKES THE PAIR EXACT. get_upvalue answers undefined for
// a missing closure, an out-of-range index, or a slot that is not a cell;
// this covers the first two and cell_get covers the third by no-oping on a
// non-cell. Composed, the two guards are the one guard the interpreter
// writes inline, so `cell_get(upvalue_cell(fr, i))` IS get_upvalue.
std::uint64_t aot_bridge::upvalue_cell(aot::ct_aot_frame * f, std::uint32_t index) {
    const context::call_frame * record = frame_record(f);
    if (record == nullptr || record->closure == nullptr) { return value::undefined().bits(); }
    if (index >= record->closure->upvalues.size()) { return value::undefined().bits(); }
    return record->closure->upvalues[index].bits();
}

// ct_aot_slots. The row: the frame's own register span, which is where a
// compiled body puts a value it needs to survive a safepoint.
//
// VALID UNTIL THE NEXT SAFEPOINT AND NOT ONE INSTRUCTION LONGER. registers_
// is a std::vector and a nested call resizes it, so this recomputes the
// base every time - exactly as the interpreter's own reg() does, and for
// the same reason.
// ct_aot_make_arguments and ct_aot_gather_rest, through the shared members.
//
// RAISE TIER: both answer the ARRAY rather than a status, so on failure the
// value is meaningless and a caller polls ct_aot_failed at a back edge.
// They allocate, so both are safepoints; neither can run user code.
//
// AN UNWOUND FRAME STILL GETS A WELL-FORMED VALUE. frame_record answers
// null once frames_ has been truncated below this frame, and a raise-tier
// helper has no status to report it with - so the honest answer is an empty
// array, not a dereference.
std::uint64_t aot_bridge::make_arguments(aot::ct_aot_frame * f, const std::uint64_t * slots,
                                         std::uint32_t argc) {
    context & cx = *frame_of(f).ctx;
    context::call_frame * record = frame_record(f);
    if (record == nullptr) { return cx.make_array().bits(); }
    return cx.make_arguments_object(*record, reinterpret_cast<const value *>(slots), argc).bits();
}

std::uint64_t aot_bridge::gather_rest(aot::ct_aot_frame * f, const std::uint64_t * slots,
                                      std::uint32_t argc, std::uint32_t from) {
    context & cx = *frame_of(f).ctx;
    const context::call_frame * record = frame_record(f);
    if (record == nullptr) { return cx.make_array().bits(); }
    return cx.gather_rest_values(*record, reinterpret_cast<const value *>(slots), argc, from)
        .bits();
}

// ct_aot_args and ct_aot_argc. Where the arriving arguments are, and how
// many - neither of which this frame's own slots can answer, because
// ct_aot_enter puts them above the caller's window.
std::uint64_t * aot_bridge::args(aot::ct_aot_frame * f) {
    aot_frame_storage & held = frame_of(f);
    context & cx = *held.ctx;
    // THE SAME BOUND TEST ct_aot_slots MAKES, against base + count rather
    // than base alone: an unwound frame truncates registers_ to exactly the
    // base, and a one-past-the-end pointer is worse than a null one.
    if (held.argv_base + held.argv_count > cx.registers_.size()) { return nullptr; }
    return reinterpret_cast<std::uint64_t *>(cx.registers_.data() + held.argv_base);
}

std::uint32_t aot_bridge::argc(aot::ct_aot_frame * f) {
    const context::call_frame * record = frame_record(f);
    return record == nullptr ? 0u : record->argc;
}

std::uint64_t * aot_bridge::slots(aot::ct_aot_frame * f) {
    aot_frame_storage & held = frame_of(f);
    context & cx = *held.ctx;
    // THE WHOLE SPAN IS STILL THERE, checked rather than assumed - and the
    // test is against base + count, not against base alone. An unwound
    // frame truncates registers_ to EXACTLY register_base, so a `>` test
    // passes at the moment the span has ceased to exist and hands back a
    // one-past-the-end pointer; the write that follows is the kind of
    // corruption that surfaces somewhere else entirely.
    if (held.register_base + held.slot_count > cx.registers_.size()) { return nullptr; }
    return reinterpret_cast<std::uint64_t *>(cx.registers_.data() + held.register_base);
}

// ct_aot_leave. The row: pop the frame, truncate handlers_ to
// call_frame::handler_base exactly as op::ret does, release the register
// span. It must NOT run on the unwound path - the unwinder already
// destroyed the frame - and is a harmless no-op after a failure.
void aot_bridge::leave(aot::ct_aot_frame * f, value result, bool observe_return) {
    aot_frame_storage & held = frame_of(f);
    context & cx = *held.ctx;
    // NOT frames_.back(). A nested call made while `failed_` is set can
    // leave a foreign frame on top, and popping it would destroy someone
    // else's. Truncating to this frame's own index is the same operation
    // when nothing went wrong and the correct one when something did.
    // Only a normal return with exactly this frame on top supplies enough
    // evidence. Keep the frame copy and status check off the ordinary path
    // when recording is disabled. Foreign-frame cleanup stays unchecked.
    if (cx.recorder_ != nullptr && observe_return && cx.frames_.size() == held.frame_index + 1 &&
        check(f) == static_cast<std::int32_t>(aot::ct_aot_status::ok)) [[unlikely]] {
        const context::call_frame popped = cx.frames_[held.frame_index];
        cx.frames_.resize(held.frame_index);
        cx.record_frame_pop(popped, result, true);
    } else if (cx.frames_.size() > held.frame_index) {
        cx.frames_.resize(held.frame_index);
    }
    if (cx.handlers_.size() > held.handler_base) { cx.handlers_.resize(held.handler_base); }
    if (cx.registers_.size() > held.register_base) { cx.registers_.resize(held.register_base); }
    held.~aot_frame_storage();
}

// ---- THE THROWING TIER. Phase 6. ------------------------------------
//
// Phase 2 declared ct_aot_catch_land unimplementable as written, having
// tried: it says to read back registers_[base + handler::slot], and
// unwind_to_handler POPS the handler before it writes, so by the time a
// compiled body could ask, which register the value went into is
// unknowable. The row wrote down two possible fixes and took neither,
// because "taking one without a compiled `try` to test it would be
// inventing on no evidence".
//
// There is a compiled `try` now - unittests/unit/aot_throw - and the fix
// taken is neither of the two: call_frame RECORDS the slot at the moment
// unwind_to_handler writes it. The row's second option, adding the slot to
// this helper's parameters, would have changed a signature two code
// generators are written against to avoid two bytes on a frame.

// ct_aot_handler_push. The pad id is the body's own label, carried in
// handler::address with CT_AOT_PAD_BIT set - so unwind_to_handler's
// `target.ip = h.address` hands a compiled body its landing pad using the
// same four steps that resume the interpreter at a catch block. No change
// to the unwinder.
void aot_bridge::handler_push(aot::ct_aot_frame * f, std::uint32_t pad, std::uint32_t slot) {
    aot_frame_storage & held = frame_of(f);
    context & cx = *held.ctx;
    cx.handlers_.push_back(
        context::handler{held.frame_index, static_cast<std::size_t>(pad) | CT_AOT_PAD_BIT,
                         cx.registers_.size(), static_cast<std::uint16_t>(slot)});
}

// ct_aot_handler_pop. Pops the GLOBALLY innermost handler without
// consulting handler_base, exactly as op::pop_handler does - so a
// mis-balanced emission drops a CALLER's catch and nothing reports it.
// Balance is a compiler invariant; `fr` is carried so this can say so.
void aot_bridge::handler_pop(aot::ct_aot_frame * f) {
    aot_frame_storage & held = frame_of(f);
    context & cx = *held.ctx;
    if (cx.handlers_.empty()) { return; }
    // NOT AN ASSERT, because this is also reachable from an image: a body
    // popping a handler it did not push is a broken image rather than a
    // broken runtime, and refusing is better than corrupting a caller's.
    if (cx.handlers_.back().frame != held.frame_index) { return; }
    cx.handlers_.pop_back();
}

// ct_aot_throw. The row: `thrown_ = v; if (!unwind_to_handler()) raise(...)`
// and then ct_aot_check for the status, so this classifies identically to
// every other throwing helper. It never returns OK.
std::int32_t aot_bridge::throw_value(aot::ct_aot_frame * f, std::uint64_t thrown) {
    aot_frame_storage & held = frame_of(f);
    context & cx = *held.ctx;
    cx.thrown_ = value::from_bits(thrown);
    if (!cx.unwind_to_handler()) {
        // NOTHING CAUGHT IT. unwind_to_handler pops as it SEARCHES, so
        // handlers_ is empty and thrown_ is still set - which is what keeps
        // the object alive while its own toString runs inside describe.
        cx.raise("uncaught " + cx.describe_thrown(cx.thrown_));
    }
    return check(f);
}

// ct_aot_catch_land. A pure READ of what unwind_to_handler deposited: the
// pad id from `ip`, and the value from the slot the frame recorded.
std::uint32_t aot_bridge::catch_land(aot::ct_aot_frame * f, std::uint64_t * out_thrown) {
    aot_frame_storage & held = frame_of(f);
    context & cx = *held.ctx;
    if (held.frame_index >= cx.frames_.size()) {
        *out_thrown = value::undefined().bits();
        return 0;
    }
    context::call_frame & frame = cx.frames_[held.frame_index];
    const auto pad = static_cast<std::uint32_t>(frame.ip & ~CT_AOT_PAD_BIT);
    // CLEARED, so a second call is not a second catch. The row asks for
    // exactly this and calls it "a debug assert rather than a second catch".
    frame.ip = 0;
    *out_thrown = cx.registers_[frame.base + frame.landed_slot].bits();
    return pad;
}

// ct_aot_call. The row: route THROUGH context::call rather than around it,
// which gets three things right for free - a generator callee runs nothing,
// a native callee's current_this_ save and restore, and the
// pending_new_target_ handoff a non-spread super() needs.
//
// `key` and `site` are the row's DIAGNOSTIC arguments and are unused here.
// They exist because op::call_computed names its callee with a run-time
// to_string and because callee_origin is a backwards scan over emitted
// bytecode that an AOT frame has no ip for; both matter for the MESSAGE a
// failed call produces, and neither is reachable until the throwing tier
// has a body. Taking them now keeps the signature the one the table
// declares - a helper whose parameters drift from its row is a helper the
// code generators will call wrongly.
std::int32_t aot_bridge::call(aot::ct_aot_frame * f, std::uint64_t callee, std::uint64_t receiver,
                              const std::uint64_t * argv, std::uint32_t argc, std::uint64_t key,
                              const aot::ct_aot_site * site, std::uint64_t * out) {
    (void)key;
    (void)site;
    aot_frame_storage & held = frame_of(f);
    context & cx = *held.ctx;

    // NO SAFEPOINT HERE, and that is a deliberate deletion rather than an
    // omission. The row declares ct_aot_call is_safepoint, and it is one:
    // it delegates to context::call, whose first act IS a safepoint. A
    // second collection immediately before that one satisfies nothing the
    // first does not - it was written, and then removing it did not turn
    // a single test red, which is the whole point of trying.
    //
    // COPIED OUT BEFORE THE CALL. `argv` points into registers_ for a
    // compiled body, and context::call resizes that vector - so the span
    // handed to it must not be the vector's own storage.
    std::vector<value> args;
    args.reserve(argc);
    for (std::uint32_t i = 0; i < argc; ++i) { args.push_back(value::from_bits(argv[i])); }

    const value produced = cx.call(value::from_bits(callee), args, value::from_bits(receiver));

    // THE ROW'S THREE REACHABLE STATES, tested in ct_aot_check's order and
    // for its reasons. *out is written only on OK, because an unwound
    // call's undefined is indistinguishable from a real one.
    const std::int32_t status = check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

} // namespace ctbrowser::script

namespace ctbrowser::aot {

extern "C" {

ct_aot_frame * ct_aot_enter(ct_aot_ctx * ctx, const ct_aot_site * site, std::uint32_t reg_count,
                            std::uint64_t receiver, void * storage) {
    return script::aot_bridge::enter(ctx, site, reg_count, receiver, storage);
}

void ct_aot_handler_push(ct_aot_frame * fr, std::uint32_t pad, std::uint32_t slot) {
    script::aot_bridge::handler_push(fr, pad, slot);
}

void ct_aot_handler_pop(ct_aot_frame * fr) {
    script::aot_bridge::handler_pop(fr);
}

std::int32_t ct_aot_throw(ct_aot_frame * fr, std::uint64_t thrown) {
    return script::aot_bridge::throw_value(fr, thrown);
}

std::uint32_t ct_aot_catch_land(ct_aot_frame * fr, std::uint64_t * out_thrown) {
    return script::aot_bridge::catch_land(fr, out_thrown);
}

std::uint64_t ct_aot_this(ct_aot_frame * fr) {
    return script::aot_bridge::this_value(fr);
}

std::uint64_t ct_aot_callee(ct_aot_frame * fr) {
    return script::aot_bridge::callee(fr);
}

std::uint64_t ct_aot_upvalue_cell(ct_aot_frame * fr, std::uint32_t index) {
    return script::aot_bridge::upvalue_cell(fr, index);
}

std::uint64_t ct_aot_new_target(ct_aot_frame * fr) {
    return script::aot_bridge::new_target(fr);
}

std::uint64_t ct_aot_home(ct_aot_frame * fr) {
    return script::aot_bridge::home(fr);
}

void ct_aot_pass_new_target(ct_aot_frame * fr) {
    script::aot_bridge::pass_new_target(fr);
}

std::uint64_t ct_aot_make_arguments(ct_aot_frame * fr, const std::uint64_t * slots,
                                    std::uint32_t argc) {
    return script::aot_bridge::make_arguments(fr, slots, argc);
}

std::uint64_t ct_aot_gather_rest(ct_aot_frame * fr, const std::uint64_t * slots, std::uint32_t argc,
                                 std::uint32_t from) {
    return script::aot_bridge::gather_rest(fr, slots, argc, from);
}

std::uint64_t * ct_aot_args(ct_aot_frame * fr) {
    return script::aot_bridge::args(fr);
}

std::uint32_t ct_aot_argc(ct_aot_frame * fr) {
    return script::aot_bridge::argc(fr);
}

std::uint64_t * ct_aot_slots(ct_aot_frame * fr) {
    return script::aot_bridge::slots(fr);
}

void ct_aot_leave(ct_aot_frame * fr) {
    script::aot_bridge::leave(fr);
}

void ct_aot_leave_return(ct_aot_frame * fr, std::uint64_t result) {
    script::aot_bridge::leave(fr, script::value::from_bits(result), true);
}

std::int32_t ct_aot_check(ct_aot_frame * fr) {
    return script::aot_bridge::check(fr);
}

std::int32_t ct_aot_call(ct_aot_frame * fr, std::uint64_t callee, std::uint64_t receiver,
                         const std::uint64_t * argv, std::uint32_t argc, std::uint64_t key,
                         const ct_aot_site * site, std::uint64_t * out) {
    return script::aot_bridge::call(fr, callee, receiver, argv, argc, key, site, out);
}

// ct_aot_return_value. The row is emphatic that a two-argument form is a
// miscompile: `constructing` is a run-time property of the frame, and the
// substitution is what makes a class constructor with no explicit return
// evaluate to the instance. is_object_like is the predicate, behind the helper
// so nothing hardcodes something subtler than it looks - an array, a function
// AND a proxy all count.
std::uint64_t ct_aot_return_value(std::uint64_t returned, std::uint64_t receiver,
                                  std::uint32_t constructing) {
    const script::value produced = script::value::from_bits(returned);
    if (constructing != 0u && !produced.is_object_like()) { return receiver; }
    return returned;
}

} // extern "C"

} // namespace ctbrowser::aot
