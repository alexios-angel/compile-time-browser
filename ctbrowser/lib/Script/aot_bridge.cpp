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
// EIGHT ROWS. Four of them are what a non-throwing call needs - ct_aot_enter,
// ct_aot_leave, ct_aot_check and ct_aot_return_value - and the fifth,
// ct_aot_call, is what Phase 3 needs: without it a compiled body cannot call
// anything, so three of the six transitions the plan requires could only be
// demonstrated by a test reaching around the ABI it is supposed to be testing.
// The sixth, ct_aot_slots, is what Phase 4 needs: a body had a register span
// reserved for it and no way to address it, so the only place it could keep a
// live value was a C++ local - which the collector does not trace. The seventh,
// ct_aot_binary_op_static, is Phase 5's first extracted SEMANTIC helper - the
// first row here that computes a JavaScript answer rather than managing a
// frame, and it computes it by calling the very function the interpreter calls.
// ct_aot_binary_op is the eighth and its re-entering twin: the seven operations
// that can run a page's own valueOf and toString, and therefore the first rows
// whose non-ok statuses are actually reachable.
//
// Each body is a transcription of its row's DELEGATES TO column, and where a
// row could not be satisfied that is recorded rather than worked around - see
// ct_aot_catch_land in aot_helpers.def, which this rung found unimplementable
// as written and did not implement.
//
// WHAT IS NOT HERE, deliberately: the throwing tier. ct_aot_handler_push,
// ct_aot_throw and ct_aot_catch_land are the rows a compiled `try` needs, and
// one of them cannot be written against the runtime as it stands. Executing
// three rows and reporting the fourth as broken is worth more than four bodies
// one of which quietly does something other than its row says.
#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/core/containers.hpp>
#include <ctbrowser/script/vm.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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
    // HOW MANY SLOTS THE BODY ASKED FOR. Recorded so the span can be checked
    // rather than assumed: without it `ct_aot_slots` hands back a base and
    // nothing anywhere knows how far it is legal to walk.
    std::uint32_t slot_count = 0;
};

// THE NUMBER IN THE HEADER, CHECKED AGAINST THE TYPE IT SIZES. A compiled body
// allocates CT_AOT_FRAME_BYTES and hands the room over; if this handle ever
// outgrows it, generated code would be writing past its own stack slot, and
// nothing at the call site could see that.
static_assert(sizeof(aot_frame_storage) <= CT_AOT_FRAME_BYTES,
              "CT_AOT_FRAME_BYTES must hold the frame handle it sizes");
static_assert(alignof(aot_frame_storage) <= alignof(std::max_align_t),
              "and a body's plain byte array must be able to align it");

// AN INTERNED PROPERTY NAME, and the record OWNS its text.
//
// The row is emphatic that this is NOT `prehashed_name`, which is
// {string_view, hash} - a NON-OWNING view, so a pool built out of one dangles
// the moment whoever supplied the characters goes away. A compiled body's names
// come from an image that may be a memory-mapped file or a string literal in
// generated C++; neither is something to hold a view into forever.
//
// GLOBAL AND IMMORTAL, which the row also says, and it is safe here for a
// reason that does NOT transfer to ct_aot_ic: a name record contains no heap
// pointer and is never GC-traced, so it is context-independent by construction.
// An inline cache holds receivers and is not.
//
// THE HASH COMES FROM hash_name AND FROM NOWHERE ELSE. containers.hpp says
// outright that using anything else is "a lookup that silently never matches",
// which is why the handle is produced at image init and never baked into the
// image: a hash computed by the compiler is a hash computed by a different
// build of a different library.
struct aot_name_record {
    std::string text;
    std::size_t hash;
};

namespace {

// A DEQUE FOR THE RECORDS, because the handles are pointers a compiled body
// keeps for the life of the process and a vector would move them. The index's
// keys are views INTO those records, which is only sound because their
// addresses are stable.
//
// SINGLE-THREADED, like everything else in this VM. Script runs on one thread;
// if that ever changes this needs a lock, and so does most of the engine.
std::deque<aot_name_record> & name_records() {
    static std::deque<aot_name_record> records;
    return records;
}

std::unordered_map<std::string_view, const aot_name_record *> & name_index() {
    static std::unordered_map<std::string_view, const aot_name_record *> index;
    return index;
}

} // namespace

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
        cx.pending_new_target_ = value::undefined();
        // CLEARED AFTER THE PUSH for the reason above it: between copying the
        // handoff into the frame and clearing the root, the value is reachable
        // from both - and clearing first would leave a window where it is
        // reachable from neither. call_frame::closure is GC root 4, so once the
        // frame is pushed the closure is traced.
        cx.pending_closure_ = value::undefined();
        return reinterpret_cast<aot::ct_aot_frame *>(held);
    }

    // ct_aot_binary_op_static. Phase 5's first extracted semantic helper: the
    // seven non-re-entering binary operations, delegating to the SAME
    // context::binary_op_static the interpreter now calls.
    //
    // That identity is the point of the phase. A helper that reimplemented
    // `add` would agree with the interpreter on the day it was written and
    // drift afterwards, and the difference would surface as a Phase 12A oracle
    // mismatch on some page - not as a build error.
    static std::int32_t binary_op_static(aot::ct_aot_frame * f, std::uint32_t op_kind,
                                         std::uint64_t lhs, std::uint64_t rhs,
                                         std::uint64_t * out) {
        aot_frame_storage & held = frame_of(f);
        context & cx = *held.ctx;
        // AN OP KIND OUT OF AN IMAGE IS UNTRUSTED INPUT. The interpreter only
        // ever passes the seven; a compiled body's operand came from a file.
        // binary_op_static returns undefined for anything else, and checking
        // the range here as well means the cast below cannot name an
        // enumerator that does not exist.
        if (op_kind >= opcode_count) {
            *out = value::undefined().bits();
            return static_cast<std::int32_t>(aot::ct_aot_status::ok);
        }
        const value produced = cx.binary_op_static(static_cast<op>(op_kind), value::from_bits(lhs),
                                                   value::from_bits(rhs));
        const std::int32_t status = check(f);
        if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
        return status;
    }

    // ct_aot_binary_op. Phase 5's second extracted semantic helper: the seven
    // that CAN run page JavaScript, through the same context::binary_op the
    // interpreter now calls.
    //
    // The status test is not decoration here. Any of these can run a user
    // valueOf or toString, which can throw, and can call something that
    // exhausts the stack - so unlike the static family this one genuinely
    // reaches its non-ok arms.
    static std::int32_t binary_op(aot::ct_aot_frame * f, std::uint32_t op_kind, std::uint64_t lhs,
                                  std::uint64_t rhs, std::uint64_t * out) {
        aot_frame_storage & held = frame_of(f);
        context & cx = *held.ctx;
        if (op_kind >= opcode_count) {
            *out = value::undefined().bits();
            return static_cast<std::int32_t>(aot::ct_aot_status::ok);
        }
        const value produced =
            cx.binary_op(static_cast<op>(op_kind), value::from_bits(lhs), value::from_bits(rhs));
        const std::int32_t status = check(f);
        if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
        return status;
    }

    // ---- THE ROWS THAT NEEDED NO EXTRACTION AT ALL ----------------------
    //
    // Phase 5 is mostly about lifting semantics out of the interpreter's
    // handlers. These are the rows where the lift was already done - the
    // runtime already had the function, and what was missing was the ABI shim
    // in front of it.
    //
    // BATCHED, and the plan's one-helper-per-commit rule is about EXTRACTIONS
    // rather than about this: an extraction can change the VM's behaviour and
    // the discipline exists to keep that diagnosable. A shim adds a symbol and
    // touches no handler, so it cannot.

    // ct_aot_failed. The uncatchable-failure poll, one load of one bool, so
    // that it can sit on back edges rather than after every allocation.
    static std::uint32_t failed(aot::ct_aot_frame * f) {
        return frame_of(f).ctx->failed() ? 1u : 0u;
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
    static std::uint64_t this_value(aot::ct_aot_frame * f) {
        const aot_frame_storage & held = frame_of(f);
        const context & cx = *held.ctx;
        if (held.frame_index >= cx.frames_.size()) { return value::undefined().bits(); }
        return context::effective_this(cx.frames_[held.frame_index]).bits();
    }

    static std::uint64_t cell_new(aot::ct_aot_frame * f, std::uint64_t init) {
        context & cx = *frame_of(f).ctx;
        return value::object(cx.allocate<cell_object>(value::from_bits(init))).bits();
    }

    static std::uint64_t new_object(aot::ct_aot_frame * f) {
        return frame_of(f).ctx->make_object().bits();
    }

    // `reserve_hint` is a HINT: the row keeps it so a backend can size a
    // literal's backing store, and an array that ignores it is merely slower.
    static std::uint64_t new_array(aot::ct_aot_frame * f, std::uint32_t reserve_hint) {
        context & cx = *frame_of(f).ctx;
        const value made = cx.make_array();
        if (reserve_hint != 0) {
            static_cast<array_object *>(made.as_heap())->items.reserve(reserve_hint);
        }
        return made.bits();
    }

    static std::int32_t loose_equals(aot::ct_aot_frame * f, std::uint64_t a, std::uint64_t b,
                                     std::uint32_t * out) {
        context & cx = *frame_of(f).ctx;
        const bool equal = cx.loose_equals(value::from_bits(a), value::from_bits(b));
        *out = equal ? 1u : 0u;
        return check(f);
    }

    // ct_aot_compare. The four relational opcodes are constant comparisons
    // against the ordering this writes, so the NUMBERS matter and are taken
    // from the row rather than from std::partial_ordering's representation.
    static std::int32_t compare(aot::ct_aot_frame * f, std::uint64_t lhs, std::uint64_t rhs,
                                std::int32_t * out_ordering) {
        context & cx = *frame_of(f).ctx;
        const std::partial_ordering ord =
            cx.compare_relational(value::from_bits(lhs), value::from_bits(rhs));
        // UNORDERED FIRST. A NaN on either side is unordered with everything
        // INCLUDING ITSELF, so testing `ord == less` first would be right and
        // testing `!(ord > 0)` for `<=` would not - which is exactly the
        // mistake that makes `NaN <= NaN` true.
        *out_ordering = static_cast<std::int32_t>(
            ord == std::partial_ordering::unordered ? aot::ct_aot_ordering::unordered
            : ord == std::partial_ordering::less    ? aot::ct_aot_ordering::less
            : ord == std::partial_ordering::greater ? aot::ct_aot_ordering::greater
                                                    : aot::ct_aot_ordering::equivalent);
        return check(f);
    }

    static std::int32_t to_number(aot::ct_aot_frame * f, std::uint64_t v, double * out) {
        context & cx = *frame_of(f).ctx;
        *out = cx.to_number_value(value::from_bits(v));
        return check(f);
    }

    // ct_aot_iterable_values. VM_CASE(iterable) is one line -
    // `reg(in.a) = iterable_values(reg(in.b))` - and iterable_values is
    // ALREADY a named member, so there is nothing to extract and no way for
    // the two tiers to drift.
    //
    // DELEGATED WHOLESALE, INCLUDING THE ROW'S CORRECTION (1). That correction
    // describes a real defect - the array-like arm calls lookup_property up to
    // 2^24 times with no failed_ test, so a throw part-way through still runs
    // millions of lookups - but it is the INTERPRETER's defect, and re-testing
    // here and not there would make the compiled tier fail EARLIER than the
    // interpreted one on a program that can observe the difference. Fixing it
    // is a VM change with its own before/after test, in one place, for both
    // tiers.
    static std::int32_t iterable_values(aot::ct_aot_frame * f, std::uint64_t source,
                                        std::uint64_t * out) {
        context & cx = *frame_of(f).ctx;
        const value produced = cx.iterable_values(value::from_bits(source));
        const std::int32_t status = check(f);
        if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
        return status;
    }

    // THE call_frame THIS HANDLE NAMES.
    //
    // BY INDEX AND GUARDED, for the reason aot_frame_storage keeps an index
    // rather than a pointer: frames_ is a vector, a nested call reallocates it,
    // and after an unwind it can be SHORTER than this frame's index - the same
    // state ct_aot_check reports as CT_AOT_UNWOUND. A helper reached in that
    // window must answer, not dereference.
    static context::call_frame * frame_record(aot::ct_aot_frame * f) {
        aot_frame_storage & held = frame_of(f);
        context & cx = *held.ctx;
        return held.frame_index < cx.frames_.size() ? &cx.frames_[held.frame_index] : nullptr;
    }

    // ct_aot_construct. op::construct's own dispatch through the shared
    // context::construct_new.
    //
    // NOT context::construct WHOLESALE, which the row is emphatic about and is
    // right: construct() tests !is_callable() FIRST and raise()s, which no
    // try/catch can see, while the opcode allocates, runs the field
    // initialisers and only then throws a CATCHABLE TypeError. Delegating
    // wholesale turns `try { new obj() } catch` into an engine fault.
    //
    // `site` IS THIS FRAME'S OWN function_proto, which is what names the
    // function in that TypeError. There is no origin scan to go with it -
    // callee_origin walks emitted bytecode from an ip and a register index, and
    // an AOT frame has neither - so describe_callee renders the callee as "the
    // value".
    static std::int32_t construct(aot::ct_aot_frame * f, std::uint64_t callee,
                                  const std::uint64_t * argv, std::uint32_t argc,
                                  const aot::ct_aot_site * site, std::uint64_t * out) {
        aot_frame_storage & held = frame_of(f);
        context & cx = *held.ctx;
        // COPIED OUT BEFORE THE CALL, for ct_aot_call's reason: argv points
        // into registers_ for a compiled body, and the call resizes it.
        std::vector<value> args;
        args.reserve(argc);
        for (std::uint32_t i = 0; i < argc; ++i) { args.push_back(value::from_bits(argv[i])); }

        const function_proto & from = *reinterpret_cast<const function_proto *>(site);
        const value produced = cx.construct_new(value::from_bits(callee), args, from);
        const std::int32_t status = check(f);
        if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
        return status;
    }

    // ct_aot_has_property, ct_aot_instance_of, ct_aot_delete_index - the three
    // shared members lifted out of run_loop so `key in obj` cannot mean one
    // thing compiled and another interpreted.
    //
    // THEIR TIERS DIFFER AND THE SIGNATURES SAY SO. has_property and
    // delete_index answer an int32_t status, so a caller tests. instance_of
    // returns its BOOLEAN - it is raise tier, so on failure the uint32_t is
    // meaningless and a caller polls ct_aot_failed at a back edge instead.
    static std::int32_t has_property(aot::ct_aot_frame * f, std::uint64_t target, std::uint64_t key,
                                     std::uint32_t * out) {
        context & cx = *frame_of(f).ctx;
        const bool present = cx.has_property(value::from_bits(target), value::from_bits(key));
        const std::int32_t status = check(f);
        if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) {
            *out = present ? 1u : 0u;
        }
        return status;
    }

    static std::uint32_t instance_of(aot::ct_aot_frame * f, std::uint64_t target,
                                     std::uint64_t ctor) {
        context & cx = *frame_of(f).ctx;
        return cx.instance_of(value::from_bits(target), value::from_bits(ctor)) ? 1u : 0u;
    }

    static std::int32_t delete_index(aot::ct_aot_frame * f, std::uint64_t target,
                                     std::uint64_t key) {
        context & cx = *frame_of(f).ctx;
        cx.delete_index(value::from_bits(target), value::from_bits(key));
        return check(f);
    }

    // ct_aot_append. VM_CASE(append) through the shared context::array_append.
    //
    // (0, 0, 0): the push_back can grow a std::vector, which is malloc rather
    // than allocate() - no GC object is created, so there is no safepoint and
    // no ceiling to raise against.
    static void append(aot::ct_aot_frame * f, std::uint64_t array, std::uint64_t v) {
        context & cx = *frame_of(f).ctx;
        cx.array_append(value::from_bits(array), value::from_bits(v));
    }

    // ct_aot_new_string. The same context::interned_string the interpreter
    // now calls, memo and all.
    //
    // THE MEMO IS NOT AN OPTIMISATION HERE. `allocations_` counts TOTAL
    // allocations for the process lifetime and is never reset, so the
    // 40,000,000 ceiling is a lifetime budget: interpreted, a string literal
    // in a per-pixel loop allocates ONE object for the whole run, while the
    // same loop compiled without the memo allocates one per iteration and
    // reaches the ceiling in about a second. That is a divergence in the raise
    // tier, on a program the interpreter runs forever.
    //
    // RAISE TIER ONLY, so there is no status: the sole failure is allocate()'s
    // ceiling, which sets failed_ without entering unwind_to_handler and is
    // invisible to a JS try. The returned string is well-formed even after it.
    static std::uint64_t new_string(aot::ct_aot_frame * f, const aot::ct_aot_site * site,
                                    std::uint32_t slot, const char * utf8, std::uint32_t len) {
        context & cx = *frame_of(f).ctx;
        return cx
            .interned_string(static_cast<const void *>(site), slot, std::string_view{utf8, len})
            .bits();
    }

    // ct_aot_set_index. `target[key] = v` through the same
    // context::store_index the interpreter now calls.
    //
    // NO OUT-PARAMETER, and that is the row rather than an omission: the
    // bytecode performs the write and evaluates the expression separately,
    // exactly as delete does. `site` is where Phase 26 attaches an inline
    // cache; it is taken and ignored, for the same reason ct_aot_get_index's
    // is - the signature is what two code generators are written against.
    static std::int32_t set_index(aot::ct_aot_frame * f, std::uint64_t obj, std::uint64_t key,
                                  std::uint64_t v) {
        context & cx = *frame_of(f).ctx;
        cx.store_index(value::from_bits(obj), value::from_bits(key), value::from_bits(v));
        return check(f);
    }

    // ct_aot_negate. `-x` through the same context::negate_value the
    // interpreter now calls.
    //
    // NOT ct_aot_to_number PLUS AN fneg, which is the lowering the shape
    // invites. The BigInt arm ALLOCATES a fresh bigint_object, so the answer
    // may be an unrooted heap value and the caller must park it - which is why
    // this row's out-parameter is a uint64_t VALUE while ct_aot_to_number's is
    // a double.
    //
    // NO OPERAND RANGE CHECK, unlike the two binary rows: there is no op_kind
    // out of an untrusted image here, only a value, and every bit pattern is a
    // legal one.
    static std::int32_t negate(aot::ct_aot_frame * f, std::uint64_t v, std::uint64_t * out) {
        context & cx = *frame_of(f).ctx;
        const value produced = cx.negate_value(value::from_bits(v));
        const std::int32_t status = check(f);
        if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
        return status;
    }

    // ct_aot_bit_not. The same shape, and the same reason for the value
    // out-parameter: `~1n` allocates.
    //
    // ITS may_throw IS THE CEILING, NOT A CATCHABLE THROW. The row says so:
    // `~1n` computes and `~obj` is -1, so neither arm has a JS-catchable
    // throw - an AOT backend needs a FAULT edge here, not an exception edge,
    // and only on the BigInt arm.
    static std::int32_t bit_not(aot::ct_aot_frame * f, std::uint64_t v, std::uint64_t * out) {
        context & cx = *frame_of(f).ctx;
        const value produced = cx.bit_not_value(value::from_bits(v));
        const std::int32_t status = check(f);
        if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
        return status;
    }

    // ct_aot_make_closure. The shared context::make_closure, with the ABI's
    // parallel array.
    //
    // A NON-CLOSURE ENCLOSING VALUE BECOMES nullptr, not a cast: the top level
    // has no enclosing closure and passes undefined, and make_closure reads
    // `enclosing->owner` to choose the program.
    //
    // RAISE TIER ONLY, from three sources rather than one - the allocation
    // ceiling, no program with no enclosing closure, and a function index or
    // upvalue count that does not match. make_closure raises for all three and
    // the caller polls ct_aot_failed; there is no status to return, because the
    // row answers with a value.
    static std::uint64_t make_closure(aot::ct_aot_frame * f, std::uint64_t enclosing_closure,
                                      std::uint32_t function_index,
                                      const std::uint64_t * local_upvalues,
                                      std::uint32_t upvalue_count, std::uint64_t enclosing_this) {
        context & cx = *frame_of(f).ctx;
        const value enclosing = value::from_bits(enclosing_closure);
        closure_object * parent = enclosing.is_kind(heap_kind::function)
                                      ? static_cast<closure_object *>(enclosing.as_heap())
                                      : nullptr;
        return cx
            .make_closure(parent, function_index,
                          context::upvalue_source{local_upvalues, upvalue_count, nullptr},
                          value::from_bits(enclosing_this))
            .bits();
    }

    // ct_aot_callee. VM_CASE(load_callee) is
    // `closure != nullptr ? value::object(closure) : undefined`, and this is
    // that - which is also the only way a compiled body can reach its own
    // upvalues, since they live on the closure INSTANCE and `site` is the
    // shared function_proto.
    static std::uint64_t callee(aot::ct_aot_frame * f) {
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
    static void pass_new_target(aot::ct_aot_frame * f) {
        context & cx = *frame_of(f).ctx;
        const context::call_frame * record = frame_record(f);
        cx.pass_new_target(record == nullptr ? value::undefined() : record->new_target);
    }

    // ct_aot_get_proto and ct_aot_set_proto, through the shared members the
    // rows themselves named. Both all-zero: the link is a plain field, so
    // nothing allocates, nothing throws and no accessor can run.
    //
    // BY VALUE IN, RESULT OUT, which removes an aliasing question the
    // interpreter has: get_proto is emitted as `{get_proto, dst, dst}` right
    // after load_home, so a and b are the same register there.
    static std::uint64_t get_proto(aot::ct_aot_frame * f, std::uint64_t target) {
        context & cx = *frame_of(f).ctx;
        return cx.get_prototype(value::from_bits(target)).bits();
    }

    static void set_proto(aot::ct_aot_frame * f, std::uint64_t target, std::uint64_t proto) {
        context & cx = *frame_of(f).ctx;
        cx.set_prototype(value::from_bits(target), value::from_bits(proto));
    }

    // ct_aot_new_bigint_literal. A BigInt literal, parsed once per site.
    //
    // RAISE TIER: it answers the VALUE rather than a status, and the row is
    // explicit that a literal the lexer accepted but that is not an integer
    // does not throw - 1.5n substitutes 0n. The only failure is the allocation
    // ceiling, so a well-formed value comes back on every path.
    //
    // THE SITE IS THE CALLER'S MARKER, not a function_proto, which is why the
    // cache key is a void pointer. A compiled body numbers its slots in walk
    // order and the interpreter numbers them by constant-pool index; sharing a
    // key would let one read a slot the other filled with a different literal.
    static std::uint64_t new_bigint_literal(aot::ct_aot_frame * f, const aot::ct_aot_site * site,
                                            std::uint32_t slot, const char * text,
                                            std::uint32_t len) {
        context & cx = *frame_of(f).ctx;
        return cx
            .interned_bigint_literal(static_cast<const void *>(site), slot,
                                     std::string_view{text, len})
            .bits();
    }

    // ct_aot_delete_prop. `delete o.k`, the named form.
    //
    // (0, 0, 0): erasing from a hash map is malloc's business, not the
    // collector's, and a NAME cannot run a to_string the way delete_index's
    // value key can - which is the entire reason the two are separate opcodes.
    static void delete_prop(aot::ct_aot_frame * f, std::uint64_t target,
                            const aot_name_record * name) {
        context & cx = *frame_of(f).ctx;
        cx.delete_named(value::from_bits(target), name->text);
    }

    // ct_aot_own_keys. for-in, which compiles to a for-of over this array.
    //
    // RAISE TIER: it returns the ARRAY rather than a status, so on failure the
    // value is meaningless and a caller polls ct_aot_failed at a back edge. It
    // allocates - make_array plus one string per key - which is why it is a
    // safepoint even though it cannot run user code.
    static std::uint64_t own_keys(aot::ct_aot_frame * f, std::uint64_t source) {
        context & cx = *frame_of(f).ctx;
        return cx.own_keys(value::from_bits(source)).bits();
    }

    // ct_aot_define_accessor. `get x()` and `set x(v)`, both opcodes.
    //
    // (0, 0, 0) AND THE ROW SPELLS OUT WHY may_reenter IS FALSE HERE even
    // though every later read or write of that property WILL re-enter:
    // installing an accessor only STORES the function, it never invokes it.
    // The cost moves to the access site, which is where the barrier and the
    // cache both belong.
    static void define_accessor(aot::ct_aot_frame * f, std::uint64_t target,
                                const aot_name_record * name, std::uint64_t getter,
                                std::uint64_t setter) {
        context & cx = *frame_of(f).ctx;
        cx.define_accessor(value::from_bits(target), name->text, value::from_bits(getter),
                           value::from_bits(setter));
    }

    // ct_aot_copy_props. Object spread, through the shared member.
    //
    // (0, 0, 0) AND THAT IS VERIFIED RATHER THAN COPIED. object_object::set
    // grows a std::vector and a hash map - malloc, not allocate() - so no GC
    // object is created, nothing can throw and no accessor runs. Taking
    // ct_aot_append's TRAITS because the shape looks alike would overstate the
    // row; it is the lowering shape that is alike, not the effects.
    static void copy_props(aot::ct_aot_frame * f, std::uint64_t target, std::uint64_t source) {
        context & cx = *frame_of(f).ctx;
        cx.copy_own_properties(value::from_bits(target), value::from_bits(source));
    }

    // ct_aot_call_spread and ct_aot_construct_spread - the two halves of
    // VM_CASE(apply), through the shared members.
    //
    // NO ARGUMENT WINDOW AND NO argc, which is the whole difference from
    // ct_aot_call. The arguments arrived as an ARRAY because their count was
    // not known until the spread ran, so the array is one already-rooted value
    // and there is nothing to copy out and nothing to park a run of.
    //
    // THE site IS IGNORED BY BOTH, exactly as ct_aot_call ignores its own: the
    // messages these paths raise are the interpreter's, and neither names the
    // enclosing function. It is taken so the ABI does not have to change if one
    // of them ever does.
    static std::int32_t call_spread(aot::ct_aot_frame * f, std::uint64_t callee,
                                    std::uint64_t arg_array, std::uint64_t receiver,
                                    const aot::ct_aot_site * site, std::uint64_t * out) {
        (void)site;
        context & cx = *frame_of(f).ctx;
        const value produced = cx.call_spread(value::from_bits(callee), value::from_bits(arg_array),
                                              value::from_bits(receiver));
        const std::int32_t status = check(f);
        if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
        return status;
    }

    static std::int32_t construct_spread(aot::ct_aot_frame * f, std::uint64_t callee,
                                         std::uint64_t arg_array, const aot::ct_aot_site * site,
                                         std::uint64_t * out) {
        (void)site;
        context & cx = *frame_of(f).ctx;
        const value produced =
            cx.construct_spread(value::from_bits(callee), value::from_bits(arg_array));
        const std::int32_t status = check(f);
        if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
        return status;
    }

    // ---- THE FOUR MODULE ROWS -------------------------------------------
    //
    // Each delegates to the context member VM_CASE(load_import),
    // VM_CASE(bind_export), VM_CASE(load_namespace) and VM_CASE(dyn_import)
    // now call, so the two tiers cannot resolve a specifier, raise a message
    // or adopt a cell differently.
    //
    // A std::string PER CALL, and it is not a shortcut. modules_,
    // module_record::resolved and module_record::exports are plain
    // flat_map<std::string, ...>, and core/containers.hpp says at length that
    // `flat_map<std::string, V>::find` takes the KEY TYPE - there is no
    // heterogeneous overload without string_hash, which these three maps do not
    // use. Building the key is what the interpreter does too; it simply has one
    // already, because its names came out of the constant pool. Every one of
    // these opcodes runs once per binding in a module prologue, never in a
    // loop, which is why the allocation is not worth a wider map.

    // ct_aot_module_import_cell. RAISE TIER: it answers the CELL rather than a
    // status, so on either miss the value is undefined and a caller polls
    // ct_aot_failed at a back edge. Not a safepoint - it allocates nothing.
    static std::uint64_t module_import_cell(aot::ct_aot_frame * f, const char * specifier,
                                            std::uint32_t specifier_len, const char * export_name,
                                            std::uint32_t export_name_len) {
        context & cx = *frame_of(f).ctx;
        return cx
            .module_import_cell(std::string{specifier, specifier_len},
                                std::string{export_name, export_name_len})
            .bits();
    }

    // ct_aot_module_export_cell. THE CALLER SEEDS *out, which is what makes the
    // conditional write expressible without a status the enum does not have.
    //
    // The row asked for CT_AOT_NO_WRITE when there is no module being
    // evaluated; ct_aot_status has four members and none of them is that. So
    // the lowering initialises the out-slot with the destination register's
    // current value and this hands the same value straight back on that arm -
    // the register ends up holding what it held, which is what the interpreter
    // leaving it alone means. See the row.
    static std::int32_t module_export_cell(aot::ct_aot_frame * f, const char * name,
                                           std::uint32_t name_len, std::uint64_t * out) {
        context & cx = *frame_of(f).ctx;
        const value published =
            cx.module_export_cell(std::string{name, name_len}, value::from_bits(*out));
        const std::int32_t status = check(f);
        if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) {
            *out = published.bits();
        }
        return status;
    }

    // ct_aot_module_namespace. RAISE TIER like the import row, and a SAFEPOINT
    // unlike it: context::module_namespace allocates the namespace object and
    // one native getter per export. Safe under a real collector because the
    // half-built object is stored into module_record::namespace_object - a GC
    // root - before the accessor loop runs.
    static std::uint64_t module_namespace(aot::ct_aot_frame * f, const char * specifier,
                                          std::uint32_t specifier_len) {
        context & cx = *frame_of(f).ctx;
        return cx.module_namespace_for(std::string{specifier, specifier_len}).bits();
    }

    // ct_aot_dynamic_import. The heaviest safepoint in the table: the
    // specifier's toString and then a whole module graph, both user JavaScript.
    //
    // THE REFERRER COMES OFF THE FRAME, exactly as the row says and as the
    // handler does. A literal could not be right: function_proto::module is
    // stamped by the LOADER after compilation, so the compiler would be baking
    // its guess at what the loader will call the file.
    //
    // THE REFERRER IS COPIED BEFORE THE CALL. frames_ is a vector and the
    // loader pushes frames, so a reference into frames_[i].proto->module taken
    // before it would dangle - and `module` is a std::string on a proto the
    // loader may also be assigning to.
    static std::int32_t dynamic_import(aot::ct_aot_frame * f, std::uint64_t specifier,
                                       std::uint64_t * out) {
        context & cx = *frame_of(f).ctx;
        const context::call_frame * record = frame_record(f);
        const std::string referrer =
            (record == nullptr || record->proto == nullptr) ? std::string{} : record->proto->module;
        const value produced = cx.dynamic_import(value::from_bits(specifier), referrer);
        const std::int32_t status = check(f);
        if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
        return status;
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
    static std::uint64_t new_target(aot::ct_aot_frame * f) {
        const context::call_frame * record = frame_record(f);
        return record == nullptr ? value::undefined().bits() : record->new_target.bits();
    }

    // ct_aot_home. VM_CASE(load_home), which is what `super` resolves against.
    //
    // THE VALUE IS COPIED OUT AND NO POINTER CROSSES THE ABI: find() answers a
    // pointer into closure_object::props, and any later set() on that closure
    // invalidates it.
    static std::uint64_t home(aot::ct_aot_frame * f) {
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
    static std::uint64_t upvalue_cell(aot::ct_aot_frame * f, std::uint32_t index) {
        const context::call_frame * record = frame_record(f);
        if (record == nullptr || record->closure == nullptr) { return value::undefined().bits(); }
        if (index >= record->closure->upvalues.size()) { return value::undefined().bits(); }
        return record->closure->upvalues[index].bits();
    }

    // ct_aot_cell_get. VM_CASE(cell_get) verbatim.
    //
    // NO FRAME AND NO FAILURE: the row is (0, 0, 0) and takes no handle at all,
    // and its FAILURE line calls the silence a semantic guarantee - "a non-cell
    // argument yields undefined silently", which is what lets the pair above
    // compose without a second test.
    static std::uint64_t cell_get(std::uint64_t cell) {
        const value held = value::from_bits(cell);
        return held.is_kind(heap_kind::cell)
                   ? static_cast<cell_object *>(held.as_heap())->slot.bits()
                   : value::undefined().bits();
    }

    // ct_aot_cell_set. VM_CASE(cell_set) verbatim, and silent on a non-cell for
    // the same reason.
    static void cell_set(std::uint64_t cell, std::uint64_t v) {
        const value held = value::from_bits(cell);
        if (held.is_kind(heap_kind::cell)) {
            static_cast<cell_object *>(held.as_heap())->slot = value::from_bits(v);
        }
    }

    // ct_aot_global_get. THE ABSENCE IS LOAD-BEARING and the row says so: an
    // undeclared global does NOT throw a ReferenceError in this runtime, it
    // reads `undefined`. context::global already has exactly that behaviour -
    // `globals_.find(name)`, undefined when absent - which is the same two
    // lines VM_CASE(get_global) runs (run_loop.cpp:226-231), so the two tiers
    // cannot drift.
    //
    // NO FRAME IS TOUCHED beyond finding the context, and nothing here can
    // throw, allocate or re-enter - which is why the row is (0, 0, 0) and why
    // the compiled form is a single call with no status and no edge.
    static std::uint64_t global_get(aot::ct_aot_frame * f, const char * name,
                                    std::uint32_t name_len) {
        const context & cx = *frame_of(f).ctx;
        return cx.global(std::string_view{name, name_len}).bits();
    }

    // ct_aot_global_set. VM_CASE(set_global) is
    // `globals_[names[in.bx()]] = reg(in.a)` (run_loop.cpp:235), and
    // context::define_global is that assignment - so a global created by
    // compiled code and one created by the interpreter are the same entry.
    //
    // THE NAME IS COPIED because the map owns its keys and the caller's
    // characters are a `const char *` pointing into a generated translation
    // unit's rodata - which outlives everything here, but the map's contract is
    // ownership and borrowing from it would be a second rule to remember.
    static void global_set(aot::ct_aot_frame * f, const char * name, std::uint32_t name_len,
                           std::uint64_t v) {
        context & cx = *frame_of(f).ctx;
        cx.define_global(std::string{name, name_len}, value::from_bits(v));
    }

    static std::int32_t get_index(aot::ct_aot_frame * f, std::uint64_t obj, std::uint64_t key,
                                  std::uint64_t * out) {
        context & cx = *frame_of(f).ctx;
        const value produced = cx.lookup_index(value::from_bits(obj), value::from_bits(key));
        const std::int32_t status = check(f);
        if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
        return status;
    }

    // ct_aot_intern_name. Idempotent: the same characters always give the same
    // pointer, which is what lets a backend compare names by address.
    static const aot_name_record * intern(const char * utf8, std::uint32_t len) {
        const std::string_view text{utf8, len};
        auto & index = name_index();
        if (const auto found = index.find(text); found != index.end()) { return found->second; }
        auto & records = name_records();
        records.push_back(aot_name_record{std::string{text}, hash_name(text)});
        const aot_name_record * record = &records.back();
        // KEYED BY A VIEW INTO THE RECORD'S OWN STRING, not by the caller's
        // characters - which are a `const char *` the caller is free to free.
        index.emplace(std::string_view{record->text}, record);
        return record;
    }

    static std::int32_t get_prop(aot::ct_aot_frame * f, std::uint64_t obj,
                                 const aot_name_record * name, std::uint64_t * out) {
        context & cx = *frame_of(f).ctx;
        // THROUGH THE INTERPRETER'S OWN lookup_property, hashing the name again
        // as it does. The row's payoff - reusing the interned hash across the
        // prototype chain - needs lookup_property to take a prehashed_name, and
        // that is a refactor of a long function with two dozen `name == "..."`
        // arms. It is an OPTIMISATION, worth its own change, and doing it here
        // would mean an extraction and a new ABI row in one step.
        const value produced = cx.lookup_property(value::from_bits(obj), name->text);
        const std::int32_t status = check(f);
        if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
        return status;
    }

    static std::int32_t set_prop(aot::ct_aot_frame * f, std::uint64_t obj,
                                 const aot_name_record * name, std::uint64_t v) {
        context & cx = *frame_of(f).ctx;
        cx.store_property(value::from_bits(obj), name->text, value::from_bits(v));
        return check(f);
    }

    // ct_aot_slots. The row: the frame's own register span, which is where a
    // compiled body puts a value it needs to survive a safepoint.
    //
    // VALID UNTIL THE NEXT SAFEPOINT AND NOT ONE INSTRUCTION LONGER. registers_
    // is a std::vector and a nested call resizes it, so this recomputes the
    // base every time - exactly as the interpreter's own reg() does, and for
    // the same reason.
    static std::uint64_t * slots(aot::ct_aot_frame * f) {
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
    static void leave(aot::ct_aot_frame * f) {
        aot_frame_storage & held = frame_of(f);
        context & cx = *held.ctx;
        // NOT frames_.back(). A nested call made while `failed_` is set can
        // leave a foreign frame on top, and popping it would destroy someone
        // else's. Truncating to this frame's own index is the same operation
        // when nothing went wrong and the correct one when something did.
        if (cx.frames_.size() > held.frame_index) { cx.frames_.resize(held.frame_index); }
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
    static void handler_push(aot::ct_aot_frame * f, std::uint32_t pad, std::uint32_t slot) {
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
    static void handler_pop(aot::ct_aot_frame * f) {
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
    static std::int32_t throw_value(aot::ct_aot_frame * f, std::uint64_t thrown) {
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
    static std::uint32_t catch_land(aot::ct_aot_frame * f, std::uint64_t * out_thrown) {
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
        // AND CAUGHT, which this could not detect until Phase 6. The row's test
        // is CT_AOT_PAD_BIT in call_frame::ip, and nothing could set it while
        // ct_aot_catch_land was unimplementable; both work now, and the bit is
        // put there by unwind_to_handler assigning `ip` from a handler whose
        // address is the body's pad id.
        if ((cx.frames_[held.frame_index].ip & CT_AOT_PAD_BIT) != 0) {
            return static_cast<std::int32_t>(aot::ct_aot_status::caught);
        }
        return static_cast<std::int32_t>(aot::ct_aot_status::ok);
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
    static std::int32_t call(aot::ct_aot_frame * f, std::uint64_t callee, std::uint64_t receiver,
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
};

} // namespace ctbrowser::script

namespace ctbrowser::aot {

extern "C" {

ct_aot_frame * ct_aot_enter(ct_aot_ctx * ctx, const ct_aot_site * site, std::uint32_t reg_count,
                            std::uint64_t receiver, void * storage) {
    return script::aot_bridge::enter(ctx, site, reg_count, receiver, storage);
}

std::int32_t ct_aot_binary_op(ct_aot_frame * fr, std::uint32_t op_kind, std::uint64_t lhs,
                              std::uint64_t rhs, std::uint64_t * out) {
    return script::aot_bridge::binary_op(fr, op_kind, lhs, rhs, out);
}

std::int32_t ct_aot_binary_op_static(ct_aot_frame * fr, std::uint32_t op_kind, std::uint64_t lhs,
                                     std::uint64_t rhs, std::uint64_t * out) {
    return script::aot_bridge::binary_op_static(fr, op_kind, lhs, rhs, out);
}

// THE HANDLE IS AN OPAQUE POINTER on the ABI's side and a record on this one,
// which is what `struct ct_aot_name` being declared and never defined is for.
const ct_aot_name * ct_aot_intern_name(const char * utf8, std::uint32_t len) {
    return reinterpret_cast<const ct_aot_name *>(script::aot_bridge::intern(utf8, len));
}

std::int32_t ct_aot_get_prop(ct_aot_frame * fr, std::uint64_t obj, const ct_aot_name * name,
                             ct_aot_ic * site, std::uint64_t * out) {
    (void)site; // Phase 26 attaches an inline cache here without an ABI break
    return script::aot_bridge::get_prop(
        fr, obj, reinterpret_cast<const script::aot_name_record *>(name), out);
}

std::int32_t ct_aot_set_prop(ct_aot_frame * fr, std::uint64_t obj, const ct_aot_name * name,
                             std::uint64_t v, ct_aot_ic * site) {
    (void)site;
    return script::aot_bridge::set_prop(fr, obj,
                                        reinterpret_cast<const script::aot_name_record *>(name), v);
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

std::uint32_t ct_aot_failed(ct_aot_frame * fr) {
    return script::aot_bridge::failed(fr);
}

std::uint64_t ct_aot_this(ct_aot_frame * fr) {
    return script::aot_bridge::this_value(fr);
}

std::uint64_t ct_aot_cell_new(ct_aot_frame * fr, std::uint64_t init) {
    return script::aot_bridge::cell_new(fr, init);
}

std::uint64_t ct_aot_new_object(ct_aot_frame * fr) {
    return script::aot_bridge::new_object(fr);
}

std::uint64_t ct_aot_new_array(ct_aot_frame * fr, std::uint32_t reserve_hint) {
    return script::aot_bridge::new_array(fr, reserve_hint);
}

// PURE, AND THEREFORE HANDLE-FREE. These four take no frame because they
// cannot throw, cannot re-enter and cannot collect - which is what lets a
// backend fold them away entirely when it has proven the operand's type.
std::uint32_t ct_aot_truthy(std::uint64_t v) {
    return script::context::truthy(script::value::from_bits(v)) ? 1u : 0u;
}

std::uint32_t ct_aot_strict_equals(std::uint64_t a, std::uint64_t b) {
    return script::value::from_bits(a).strict_equals(script::value::from_bits(b)) ? 1u : 0u;
}

std::int32_t ct_aot_to_int32(std::uint64_t v) {
    return script::context::to_int32(script::value::from_bits(v));
}

std::uint32_t ct_aot_to_uint32(std::uint64_t v) {
    return script::context::to_uint32(script::value::from_bits(v));
}

double ct_aot_to_number_primitive(std::uint64_t v) {
    return script::context::to_number(script::value::from_bits(v));
}

double ct_aot_exponentiate(double base, double exponent) {
    return script::context::exponentiate(base, exponent);
}

// THE RETURN SLOT CARRIES A LENGTH, and *out_text points at static storage:
// context::type_of returns a string_view over string literals, which is what
// lets `typeof x === 'function'` be a length compare and a memcmp with no
// allocation at all.
std::uint32_t ct_aot_type_of_name(std::uint64_t v, const char ** out_text) {
    const std::string_view text = script::context::type_of(script::value::from_bits(v));
    *out_text = text.data();
    return static_cast<std::uint32_t>(text.size());
}

std::int32_t ct_aot_loose_equals(ct_aot_frame * fr, std::uint64_t a, std::uint64_t b,
                                 std::uint32_t * out) {
    return script::aot_bridge::loose_equals(fr, a, b, out);
}

std::int32_t ct_aot_compare(ct_aot_frame * fr, std::uint64_t lhs, std::uint64_t rhs,
                            std::int32_t * out_ordering) {
    return script::aot_bridge::compare(fr, lhs, rhs, out_ordering);
}

std::int32_t ct_aot_to_number(ct_aot_frame * fr, std::uint64_t v, double * out) {
    return script::aot_bridge::to_number(fr, v, out);
}

// The `site` parameter is where Phase 26 attaches an inline cache without an
// ABI break. Taken and ignored, because the signature is the thing two code
// generators are written against and a parameter added later is a break.
std::int32_t ct_aot_construct(ct_aot_frame * fr, std::uint64_t callee, const std::uint64_t * argv,
                              std::uint32_t argc, const ct_aot_site * site, std::uint64_t * out) {
    return script::aot_bridge::construct(fr, callee, argv, argc, site, out);
}

void ct_aot_append(ct_aot_frame * fr, std::uint64_t array, std::uint64_t v) {
    script::aot_bridge::append(fr, array, v);
}

std::uint64_t ct_aot_new_string(ct_aot_frame * fr, const ct_aot_site * site, std::uint32_t slot,
                                const char * utf8, std::uint32_t len) {
    return script::aot_bridge::new_string(fr, site, slot, utf8, len);
}

std::int32_t ct_aot_set_index(ct_aot_frame * fr, std::uint64_t obj, std::uint64_t key,
                              std::uint64_t v, ct_aot_ic * site) {
    (void)site;
    return script::aot_bridge::set_index(fr, obj, key, v);
}

std::int32_t ct_aot_negate(ct_aot_frame * fr, std::uint64_t v, std::uint64_t * out) {
    return script::aot_bridge::negate(fr, v, out);
}

std::int32_t ct_aot_bit_not(ct_aot_frame * fr, std::uint64_t v, std::uint64_t * out) {
    return script::aot_bridge::bit_not(fr, v, out);
}

std::uint64_t ct_aot_callee(ct_aot_frame * fr) {
    return script::aot_bridge::callee(fr);
}

std::uint64_t ct_aot_make_closure(ct_aot_frame * fr, std::uint64_t enclosing_closure,
                                  std::uint32_t function_index,
                                  const std::uint64_t * local_upvalues, std::uint32_t upvalue_count,
                                  std::uint64_t enclosing_this) {
    return script::aot_bridge::make_closure(fr, enclosing_closure, function_index, local_upvalues,
                                            upvalue_count, enclosing_this);
}

std::uint64_t ct_aot_upvalue_cell(ct_aot_frame * fr, std::uint32_t index) {
    return script::aot_bridge::upvalue_cell(fr, index);
}

std::uint64_t ct_aot_cell_get(std::uint64_t cell) {
    return script::aot_bridge::cell_get(cell);
}

void ct_aot_cell_set(std::uint64_t cell, std::uint64_t v) {
    script::aot_bridge::cell_set(cell, v);
}

std::uint64_t ct_aot_global_get(ct_aot_frame * fr, const char * name, std::uint32_t name_len) {
    return script::aot_bridge::global_get(fr, name, name_len);
}

void ct_aot_global_set(ct_aot_frame * fr, const char * name, std::uint32_t name_len,
                       std::uint64_t v) {
    script::aot_bridge::global_set(fr, name, name_len, v);
}

std::int32_t ct_aot_get_index(ct_aot_frame * fr, std::uint64_t obj, std::uint64_t key,
                              ct_aot_ic * site, std::uint64_t * out) {
    (void)site;
    return script::aot_bridge::get_index(fr, obj, key, out);
}

std::int32_t ct_aot_iterable_values(ct_aot_frame * fr, std::uint64_t source, std::uint64_t * out) {
    return script::aot_bridge::iterable_values(fr, source, out);
}

std::int32_t ct_aot_has_property(ct_aot_frame * fr, std::uint64_t target, std::uint64_t key,
                                 std::uint32_t * out) {
    return script::aot_bridge::has_property(fr, target, key, out);
}

std::uint32_t ct_aot_instance_of(ct_aot_frame * fr, std::uint64_t target, std::uint64_t ctor) {
    return script::aot_bridge::instance_of(fr, target, ctor);
}

std::int32_t ct_aot_delete_index(ct_aot_frame * fr, std::uint64_t target, std::uint64_t key) {
    return script::aot_bridge::delete_index(fr, target, key);
}

std::uint64_t ct_aot_new_bigint_literal(ct_aot_frame * fr, const ct_aot_site * site,
                                        std::uint32_t slot, const char * literal,
                                        std::uint32_t len) {
    return script::aot_bridge::new_bigint_literal(fr, site, slot, literal, len);
}

void ct_aot_delete_prop(ct_aot_frame * fr, std::uint64_t target, const ct_aot_name * name) {
    script::aot_bridge::delete_prop(fr, target,
                                    reinterpret_cast<const script::aot_name_record *>(name));
}

std::uint64_t ct_aot_own_keys(ct_aot_frame * fr, std::uint64_t source) {
    return script::aot_bridge::own_keys(fr, source);
}

std::uint64_t ct_aot_module_import_cell(ct_aot_frame * fr, const char * specifier,
                                        std::uint32_t specifier_len, const char * export_name,
                                        std::uint32_t export_name_len) {
    return script::aot_bridge::module_import_cell(fr, specifier, specifier_len, export_name,
                                                  export_name_len);
}

std::int32_t ct_aot_module_export_cell(ct_aot_frame * fr, const char * name, std::uint32_t name_len,
                                       std::uint64_t * out) {
    return script::aot_bridge::module_export_cell(fr, name, name_len, out);
}

std::uint64_t ct_aot_module_namespace(ct_aot_frame * fr, const char * specifier,
                                      std::uint32_t specifier_len) {
    return script::aot_bridge::module_namespace(fr, specifier, specifier_len);
}

std::int32_t ct_aot_dynamic_import(ct_aot_frame * fr, std::uint64_t specifier,
                                   std::uint64_t * out) {
    return script::aot_bridge::dynamic_import(fr, specifier, out);
}

void ct_aot_define_accessor(ct_aot_frame * fr, std::uint64_t target, const ct_aot_name * name,
                            std::uint64_t getter, std::uint64_t setter) {
    script::aot_bridge::define_accessor(
        fr, target, reinterpret_cast<const script::aot_name_record *>(name), getter, setter);
}

void ct_aot_copy_props(ct_aot_frame * fr, std::uint64_t target, std::uint64_t source) {
    script::aot_bridge::copy_props(fr, target, source);
}

std::int32_t ct_aot_call_spread(ct_aot_frame * fr, std::uint64_t callee, std::uint64_t arg_array,
                                std::uint64_t receiver, const ct_aot_site * site,
                                std::uint64_t * out) {
    return script::aot_bridge::call_spread(fr, callee, arg_array, receiver, site, out);
}

std::int32_t ct_aot_construct_spread(ct_aot_frame * fr, std::uint64_t callee,
                                     std::uint64_t arg_array, const ct_aot_site * site,
                                     std::uint64_t * out) {
    return script::aot_bridge::construct_spread(fr, callee, arg_array, site, out);
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

std::uint64_t ct_aot_get_proto(ct_aot_frame * fr, std::uint64_t target) {
    return script::aot_bridge::get_proto(fr, target);
}

void ct_aot_set_proto(ct_aot_frame * fr, std::uint64_t target, std::uint64_t proto) {
    script::aot_bridge::set_proto(fr, target, proto);
}

std::uint64_t * ct_aot_slots(ct_aot_frame * fr) {
    return script::aot_bridge::slots(fr);
}

void ct_aot_leave(ct_aot_frame * fr) {
    script::aot_bridge::leave(fr);
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
