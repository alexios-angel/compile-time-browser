// THE FIRST EXECUTABLE LINES OF THE AOT ABI.
//
// aot_helpers.def has specified sixty-eight helpers since Phase 2 and not one
// of them had a body. Phase 2's gate, from the master plan, is "VM code calls a
// hand-authored AOT closure through the real runtime ABI" - and until this file
// nothing had ever executed a line of that contract. A table nobody runs is
// prose, however good; the cheapest way to find out whether 1,881 lines are
// right is to make a few of them run before sixty-eight bodies and two code
// generators are written against them.
//
// FOUR ROWS, CHOSEN BECAUSE A NON-THROWING CALL NEEDS EXACTLY THESE FOUR.
// ct_aot_enter, ct_aot_leave, ct_aot_check and ct_aot_return_value. Each body
// is a transcription of its row's DELEGATES TO column, and where a row could
// not be satisfied that is recorded rather than worked around - see
// ct_aot_catch_land in aot_helpers.def, which this rung found unimplementable
// as written and did not implement.
//
// WHAT IS NOT HERE, deliberately: the throwing tier. ct_aot_handler_push,
// ct_aot_throw and ct_aot_catch_land are the rows a compiled `try` needs, and
// one of them cannot be written against the runtime as it stands. Executing
// three rows and reporting the fourth as broken is worth more than four bodies
// one of which quietly does something other than its row says.
#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/vm.hpp>

#include <cstddef>
#include <cstdint>

namespace ctbrowser::script {

// The handle a compiled body carries between helpers. It is what
// CT_AOT_FRAME_BYTES sizes, and it is deliberately NOT a call_frame: the row
// says the storage is caller-allocated so that the layout stays changeable, and
// a compiled body must not need to know how big a call_frame is.
struct aot_frame_storage {
    context * ctx = nullptr;
    // WHICH FRAME THIS IS, as an INDEX rather than a pointer. `frames_` is a
    // vector and a nested call reallocates it, so a pointer would dangle across
    // the very calls this handle exists to survive. It is also what ct_aot_check
    // compares against: "frames_.size() < fr depth => CT_AOT_UNWOUND".
    std::size_t frame_index = 0;
    std::size_t register_base = 0;
    std::size_t handler_base = 0;
};

// THE NUMBER IN THE HEADER, CHECKED AGAINST THE TYPE IT SIZES. A compiled body
// allocates CT_AOT_FRAME_BYTES and hands the room over; if this handle ever
// outgrows it, generated code would be writing past its own stack slot, and
// nothing at the call site could see that.
static_assert(sizeof(aot_frame_storage) <= CT_AOT_FRAME_BYTES,
              "CT_AOT_FRAME_BYTES must hold the frame handle it sizes");
static_assert(alignof(aot_frame_storage) <= alignof(std::max_align_t),
              "and a body's plain byte array must be able to align it");

struct aot_bridge {
    static context & ctx_of(aot::ct_aot_ctx * c) { return *reinterpret_cast<context *>(c); }
    static aot_frame_storage & frame_of(aot::ct_aot_frame * f) {
        return *reinterpret_cast<aot_frame_storage *>(f);
    }

    // ct_aot_enter. The row: push a call_frame carrying the image's real
    // function_proto, reserve reg_count slots in context::registers_ (GC root
    // 2), count the frame against the 512 guard, and consume-and-clear
    // pending_new_target_ exactly as call.cpp does. Returns NULL on the depth
    // raise, and the caller returns FAILED without leaving.
    static aot::ct_aot_frame * enter(aot::ct_aot_ctx * c, const aot::ct_aot_site * site,
                                     std::uint32_t reg_count, void * storage) {
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
        context::call_frame entered{};
        entered.proto = proto;
        entered.base = held->register_base;
        entered.handler_base = held->handler_base;
        // CONSUMED AND CLEARED, exactly as call.cpp does it: a new.target left
        // set would be inherited by the next ordinary call.
        entered.new_target = cx.pending_new_target_;
        cx.pending_new_target_ = value::undefined();
        cx.frames_.push_back(entered);
        return reinterpret_cast<aot::ct_aot_frame *>(held);
    }

    // ct_aot_leave. The row: pop the frame, truncate handlers_ to
    // call_frame::handler_base exactly as op::ret does, release the register
    // span. It must NOT run on the unwound path - the unwinder already
    // destroyed the frame - and is a harmless no-op after a failure.
    static void leave(aot::ct_aot_frame * f) {
        aot_frame_storage & held = frame_of(f);
        context & cx = *held.ctx;
        // NOT frames_.back(). A nested call made while `failed_` is set can
        // leave a foreign frame on top, and popping it would destroy someone
        // else's. Truncating to this frame's own index is the same operation
        // when nothing went wrong and the correct one when something did.
        if (cx.frames_.size() > held.frame_index) { cx.frames_.resize(held.frame_index); }
        if (cx.handlers_.size() > held.handler_base) { cx.handlers_.resize(held.handler_base); }
        if (cx.registers_.size() > held.register_base) {
            cx.registers_.resize(held.register_base);
        }
        held.~aot_frame_storage();
    }

    // ct_aot_check. The row's precedence, in its order and for its reasons:
    // unwound FIRST because the call_frame is destroyed and every later test
    // would dereference it; failed BEFORE caught because the run loop's own head
    // tests failed_ in the same disjunction and leaves regardless of a handler
    // that just fired; then caught; then ok.
    static std::int32_t check(aot::ct_aot_frame * f) {
        const aot_frame_storage & held = frame_of(f);
        const context & cx = *held.ctx;
        if (cx.frames_.size() <= held.frame_index) {
            return static_cast<std::int32_t>(aot::ct_aot_status::unwound);
        }
        if (cx.failed_) { return static_cast<std::int32_t>(aot::ct_aot_status::failed); }
        // CAUGHT IS NOT DETECTABLE HERE YET. The row tests CT_AOT_PAD_BIT in
        // call_frame::ip, and no compiled body can set it because
        // ct_aot_catch_land - the row that would consume it - cannot be written
        // against this runtime at all: unwind_to_handler pops the handler before
        // it writes, and call_frame records no landing slot, so the register the
        // thrown value went into is unknowable by the time a body could ask.
        // See aot_helpers.def. Until that row is fixed, no frame is ever CAUGHT,
        // and saying so here is better than a test that cannot fire.
        return static_cast<std::int32_t>(aot::ct_aot_status::ok);
    }
};

} // namespace ctbrowser::script

namespace ctbrowser::aot {

extern "C" {

ct_aot_frame * ct_aot_enter(ct_aot_ctx * ctx, const ct_aot_site * site, std::uint32_t reg_count,
                            void * storage) {
    return script::aot_bridge::enter(ctx, site, reg_count, storage);
}

void ct_aot_leave(ct_aot_frame * fr) {
    script::aot_bridge::leave(fr);
}

std::int32_t ct_aot_check(ct_aot_frame * fr) {
    return script::aot_bridge::check(fr);
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
