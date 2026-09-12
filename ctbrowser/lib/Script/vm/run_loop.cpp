// ctbrowser.script context - the interpreter loop itself. All members of
// `context`, declared in include/ctbrowser/script/vm.hpp.

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/number_format.hpp>
#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

// --- INSTRUCTION DISPATCH: computed goto, or a switch ------------------------
//
// The dispatch loop is 15% of a Phaser frame and 77% of benchmarks/bench_script.
// A `switch` compiles to ONE indirect branch that every opcode shares;
// replicating the jump into every handler gives the predictor one site per
// opcode, each of which can learn the PAIRS this bytecode actually emits.
// docs/history/computed-goto.md has the measurements.
//
// GNU ONLY, and the switch is the fallback that keeps this portable - both
// paths must stay live. OFF BY DEFAULT, BECAUSE IT MEASURED SLOWER (the table
// in docs/performance.md). Opt in with -DCTBROWSER_COMPUTED_GOTO to measure
// it on your own hardware: the result is a property of the branch predictor.
#if defined(__GNUC__) && defined(CTBROWSER_COMPUTED_GOTO)
#define VM_COMPUTED_GOTO 1
#else
#define VM_COMPUTED_GOTO 0
#endif

// --- THE TYPE ORACLE'S HOOK ---------------------------------------------------
//
// ONE LINE, AND IT IS `if constexpr`. The loop below is a template on `Record`
// and is instantiated twice; in the instantiation a build without a recorder
// runs, this expands to nothing at all - see `run_loop_impl`.
//
// PLACED AFTER THE FETCH, so `record_step` sees the instruction ABOUT to run
// and `vm_frame->ip` already pointing past it. What it actually records is the
// PREVIOUS instruction's destination: a def is not readable until the handler
// that made it returned, and `op::call`'s handler does not return until the
// callee does. Everything that means - the deferred flush, the interning, the
// parameter sweep - is in type_record.cpp, out of this translation unit.
//
// Compiled out entirely with -DCTBROWSER_SCRIPT_RECORD_TYPES=0, on the same
// terms as CTBROWSER_SCRIPT_DEBUG_NAMES: a private definition, because
// `context::recorder_` exists in every build and only this call does not.
#ifndef CTBROWSER_SCRIPT_RECORD_TYPES
#define CTBROWSER_SCRIPT_RECORD_TYPES 1
#endif
#if CTBROWSER_SCRIPT_RECORD_TYPES
#define VM_RECORD_STEP()                                                                           \
    do {                                                                                           \
        if constexpr (Record) { record_step(in); }                                                 \
    } while (0)
// THE FRAME POP IN `ret`, WITH THE ESCAPE ORACLE'S HOOK BETWEEN THE POP AND THE
// RESULT WRITE - FrameEnds.def row `ret`. The hook needs
// the frame's base and serial from BEFORE the pop and must run AFTER it, so
// that the ending frame's own per-frame roots are gone and `frames_.back()`
// is the caller; and it must run before the return value lands in the
// caller's register, because at that moment the value is in flight in a C++
// local and the hook roots it itself (through `temporaries_`) - which is how
// `return {}` reads `escaped via temporaries` rather than as confined. Same
// `if constexpr (Record)` as VM_RECORD_STEP, so the shipped instantiation is
// the one line it always was.
#define VM_POP_FRAME(carried_)                                                                     \
    do {                                                                                           \
        if constexpr (Record) {                                                                    \
            const call_frame popped = *vm_frame;                                                   \
            frames_.pop_back();                                                                    \
            record_frame_pop(popped, carried_);                                                    \
        } else {                                                                                   \
            frames_.pop_back();                                                                    \
        }                                                                                          \
    } while (0)
#else
#define VM_RECORD_STEP() ((void)0)
#define VM_POP_FRAME(carried_) frames_.pop_back()
#endif

#if VM_COMPUTED_GOTO
// GNU extensions, suppressed HERE and nowhere else: the address-of-label and
// indirect-goto forms, and the C99 array designators that index the table by
// opcode rather than by position.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-label-as-value"
#pragma clang diagnostic ignored "-Wc99-designator"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#define VM_CASE(name) VM_LABEL_##name:
#define VM_DISPATCH_BEGIN goto * vm_table[static_cast<std::size_t>(in.code)];
#define VM_DISPATCH_END
// RE-DERIVES THE FRAME, exactly as the loop head does. It is tempting to keep
// `frame`, `fn` and `base` live across a dispatch and skip this - that is what
// a textbook computed-goto interpreter does - but 12 handlers here push, pop or
// unwind `frames_`, which is a std::vector: any of them can reallocate it and
// leave a cached reference dangling. Re-deriving keeps this change to DISPATCH
// alone, so it cannot alter behaviour, and the win being chased is the branch
// prediction rather than the loads.
#define VM_NEXT                                                                                    \
    do {                                                                                           \
        if (frames_.size() <= stop_depth || failed_) { goto vm_done; }                             \
        vm_frame = &frames_.back();                                                                \
        vm_proto = vm_frame->proto;                                                                \
        if (vm_frame->ip >= vm_proto->code.size()) { goto vm_done; }                               \
        in = vm_proto->code[vm_frame->ip++];                                                       \
        base = vm_frame->base;                                                                     \
        VM_RECORD_STEP();                                                                          \
        goto * vm_table[static_cast<std::size_t>(in.code)];                                        \
    } while (0)
#else
#define VM_CASE(name) case op::name:
#define VM_DISPATCH_BEGIN switch (in.code) {
#define VM_DISPATCH_END }
#define VM_NEXT break
#endif

// THE ONE PER-INSTRUCTION TEST, AND WHY IT IS NOT ONE. A run-time
// `if (recorder_ != nullptr)` at the loop head measured +0.53% on
// bench_script and +0.48% on phaser_invaders: a predicted not-taken branch is
// not free in a loop that runs a hundred million times. So the whole loop is
// a template on ONE bool, instantiated twice, and `run_loop` below picks; the
// `Record=true` instantiation is ~15 KB of cold code.
template <bool Record> value context::run_loop_impl(std::size_t stop_depth) {
#if VM_COMPUTED_GOTO
    // Indexed BY OPCODE, which is what the array designators buy: the order of
    // this table cannot drift out of step with the enum. A label address is not
    // a constant expression, so it is a function-local `static` rather than
    // constexpr - checked in the emitted assembly to be plain .rodata with no
    // thread-safe-init guard, which on the hottest loop in the engine would
    // have cost an atomic load per dispatch.
#define CT_OPCODE(name, ...) [static_cast<std::size_t>(op::name)] = &&VM_LABEL_##name,
    static void * const vm_table[] = {
#include <ctbrowser/script/bytecode_opcodes.def>
    };
#undef CT_OPCODE
    static_assert(std::size(vm_table) == static_cast<std::size_t>(op::halt) + 1,
                  "bytecode_opcodes.def must list every opcode - a gap here is a null table "
                  "entry and a jump to address zero at run time");
#endif

    // HOISTED, because in computed-goto mode the handlers are jumped to
    // directly and never fall back through the loop head - so anything they
    // read has to survive a goto that bypasses initialisation.
    call_frame * vm_frame = nullptr;
    const function_proto * vm_proto = nullptr;
    instruction in{};
    std::size_t base = 0;
    const auto reg = [&](std::uint16_t r) -> value & { return registers_[base + r]; };

    while (frames_.size() > stop_depth && !failed_) {
        vm_frame = &frames_.back();
        vm_proto = vm_frame->proto;
        if (vm_frame->ip >= vm_proto->code.size()) { break; }
        in = vm_proto->code[vm_frame->ip++];
        base = vm_frame->base;
        VM_RECORD_STEP();

        VM_DISPATCH_BEGIN
        VM_CASE(load_const) do {
            reg(in.a) = vm_proto->constants[in.bx()];
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_bigint) do {
            // MEMOISED UNDER THIS PROTO, which is the interpreter's own key -
            // a compiled body passes a marker address of its own instead,
            // because the two number their slots differently.
            reg(in.a) = interned_bigint_literal(&(*vm_proto), in.bx(), vm_proto->strings[in.bx()]);
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_string) do {
            reg(in.a) = interned_string(&(*vm_proto), in.bx(), vm_proto->strings[in.bx()]);
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_undef) do {
            reg(in.a) = value::undefined();
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_null) do {
            reg(in.a) = value::null();
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_true) do {
            reg(in.a) = value::boolean(true);
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_false) do {
            reg(in.a) = value::boolean(false);
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(move) do {
            reg(in.a) = reg(in.b);
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(get_global) do {
            {
                // The hit path is the one map lookup it always was; a MISS asks
                // the embedder, then the global object, then throws - unless
                // this read is `typeof x`'s operand, which the compiler emits as
                // this instruction immediately followed by type_of on the same
                // register. The peek is on the miss path only.
                const auto it = globals_.find(vm_proto->names[in.bx()]);
                if (it != globals_.end()) {
                    reg(in.a) = it->second;
                    break;
                }
                const bool silent = vm_frame->ip < vm_proto->code.size() &&
                                    vm_proto->code[vm_frame->ip].code == op::type_of &&
                                    vm_proto->code[vm_frame->ip].b == in.a;
                reg(in.a) = global_or_named(vm_proto->names[in.bx()], silent);
                break;
            }
        }
        while (0);
        VM_NEXT;
        VM_CASE(set_global) do {
            globals_[vm_proto->names[in.bx()]] = reg(in.a);
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(add) do {
            reg(in.a) = binary_op_static(op::add, reg(in.b), reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(sub) do {
            reg(in.a) = binary_op(op::sub, reg(in.b), reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(mul) do {
            reg(in.a) = binary_op(op::mul, reg(in.b), reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(div) do {
            reg(in.a) = binary_op(op::div, reg(in.b), reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(mod) do {
            reg(in.a) = binary_op(op::mod, reg(in.b), reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(pow) do {
            reg(in.a) = binary_op(op::pow, reg(in.b), reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(add_generic) do {
            reg(in.a) = binary_op(op::add_generic, reg(in.b), reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(concat) do {
            reg(in.a) = binary_op(op::concat, reg(in.b), reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(negate) do {
            reg(in.a) = negate_value(reg(in.b));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(to_number) do {
            // `+x` IS A CONVERSION, not a move: `+"2"` is the number 2.
            reg(in.a) = value::number(to_number_value(reg(in.b)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(logical_not) do {
            reg(in.a) = value::boolean(!truthy(reg(in.b)));
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(equal) do {
            reg(in.a) = value::boolean(reg(in.b).strict_equals(reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(not_equal) do {
            reg(in.a) = value::boolean(!reg(in.b).strict_equals(reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(loose_equal) do {
            reg(in.a) = value::boolean(loose_equals(reg(in.b), reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(loose_not_equal) do {
            reg(in.a) = value::boolean(!loose_equals(reg(in.b), reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        // `x instanceof C` is true when C.prototype appears anywhere in x's
        // prototype chain - the same chain lookup_property walks.
        VM_CASE(instance_of) do {
            reg(in.a) = value::boolean(instance_of(reg(in.b), reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(has_property) do {
            // b IS THE KEY AND c IS THE TARGET, which is the reverse of the
            // reading order and the reason the member takes them named.
            reg(in.a) = value::boolean(has_property(reg(in.c), reg(in.b)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(bit_and) do {
            reg(in.a) = binary_op_static(op::bit_and, reg(in.b), reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(bit_or) do {
            reg(in.a) = binary_op_static(op::bit_or, reg(in.b), reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(bit_xor) do {
            reg(in.a) = binary_op_static(op::bit_xor, reg(in.b), reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(shl) do {
            reg(in.a) = binary_op_static(op::shl, reg(in.b), reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(shr) do {
            reg(in.a) = binary_op_static(op::shr, reg(in.b), reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(ushr) do {
            // The BigInt arm REFUSES this one: an unsigned shift needs a WIDTH
            // to fill from and a BigInt has none. binary_op_static carries that
            // refusal, which is where it belongs.
            reg(in.a) = binary_op_static(op::ushr, reg(in.b), reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(bit_not) do {
            reg(in.a) = bit_not_value(reg(in.b));
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(copy_props) do {
            // a IS THE TARGET AND b THE SOURCE, and the target is not written
            // back - the object is mutated in place.
            copy_own_properties(reg(in.a), reg(in.b));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(delete_prop) do {
            // a IS THE TARGET and b NAMES THE PROPERTY - nothing is written
            // back, so this produces no value.
            delete_named(reg(in.a), vm_proto->names[in.b]);
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(delete_index) do {
            delete_index(reg(in.a), reg(in.b));
            break;
        }
        while (0);
        VM_NEXT;

        // All four are ONE comparison asked four ways - see
        // context::compare_relational.
        VM_CASE(less) do {
            reg(in.a) = value::boolean(std::is_lt(compare_relational(reg(in.b), reg(in.c))));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(less_equal) do {
            reg(in.a) = value::boolean(std::is_lteq(compare_relational(reg(in.b), reg(in.c))));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(greater) do {
            reg(in.a) = value::boolean(std::is_gt(compare_relational(reg(in.b), reg(in.c))));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(greater_equal) do {
            reg(in.a) = value::boolean(std::is_gteq(compare_relational(reg(in.b), reg(in.c))));
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(jump) do {
            vm_frame->ip =
                static_cast<std::size_t>(static_cast<std::int64_t>(vm_frame->ip) + in.sbx());
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(jump_if_false) do {
            if (!truthy(reg(in.a))) {
                vm_frame->ip =
                    static_cast<std::size_t>(static_cast<std::int64_t>(vm_frame->ip) + in.sbx());
            }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(jump_if_true) do {
            if (truthy(reg(in.a))) {
                vm_frame->ip =
                    static_cast<std::size_t>(static_cast<std::int64_t>(vm_frame->ip) + in.sbx());
            }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(jump_if_not_nullish) do {
            if (!reg(in.a).is_nullish()) {
                vm_frame->ip =
                    static_cast<std::size_t>(static_cast<std::int64_t>(vm_frame->ip) + in.sbx());
            }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(jump_if_defined) do {
            if (!reg(in.a).is_undefined()) {
                vm_frame->ip =
                    static_cast<std::size_t>(static_cast<std::int64_t>(vm_frame->ip) + in.sbx());
            }
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(define_getter)
        VM_CASE(define_setter) do {
            {
                // THE DISCRIMINATOR IS THE OPCODE and it goes no further than
                // this line: the member takes both halves and the caller passes
                // undefined for the one it does not have.
                const bool getter = in.code == op::define_getter;
                define_accessor(reg(in.a), vm_proto->names[in.b],
                                getter ? reg(in.c) : value::undefined(),
                                getter ? value::undefined() : reg(in.c));
            }
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(apply)
        VM_CASE(construct_apply) do {
            // a IS BOTH THE CALLEE AND THE DESTINATION, b is the argument
            // array and c is the receiver.
            const value produced = in.code == op::construct_apply
                                       ? construct_spread(reg(in.a), reg(in.b))
                                       : call_spread(reg(in.a), reg(in.b), reg(in.c));
            // call_spread goes through context::call, whose fence parks a
            // throw; it is thrown here, at the spread call's own site.
            if (rethrow_pending()) { break; }
            reg(in.a) = produced;
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(gather_rest) do {
            reg(in.a) =
                gather_rest_values(*vm_frame, registers_.data() + base, vm_frame->argc, in.b);
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(new_object) do {
            reg(in.a) = make_object();
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(new_array) do {
            reg(in.a) = make_array();
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(append) do {
            array_append(reg(in.a), reg(in.b));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(get_prop) do {
            reg(in.a) = lookup_property(reg(in.b), vm_proto->names[in.c]);
            break;
        }
        while (0);
        VM_NEXT;
        // A SETTER ON THE CHAIN TAKES THE WRITE, and only if none does is an
        // own data property defined - store_property has the whole rule.
        VM_CASE(set_prop) do {
            store_property(reg(in.a), vm_proto->names[in.b], reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(get_index) do {
            reg(in.a) = lookup_index(reg(in.b), reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(set_index) do {
            store_index(reg(in.a), reg(in.b), reg(in.c));
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(closure) do {
            {
                // THE BODY IS context::make_closure, shared with
                // ct_aot_make_closure so the two tiers cannot drift. What stays
                // here is the INTERPRETER's part: its register window, and the
                // effective receiver an arrow captures.
                //
                // BY REGISTER, because this frame has a window and a descriptor
                // marked from_parent_local names a register in it. A compiled
                // caller passes an array indexed in parallel with the
                // descriptors instead; see context::upvalue_source.
                reg(in.a) =
                    make_closure(vm_frame->closure, in.bx(), upvalue_source{nullptr, 0, &reg(0)},
                                 effective_this((*vm_frame)));
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(call)
        VM_CASE(call_method)
        VM_CASE(call_computed)
        VM_CASE(call_receiver) do {
            {
                value callee = reg(in.a);
                value receiver = value::undefined();
                // THE LOOKUP CAN THROW - a getter, a proxy trap, or a nullish
                // receiver - and a throw has already unwound to its handler by
                // the time it returns. Calling `undefined` after that would
                // throw a SECOND TypeError from the landing site.
                const std::size_t unwound = unwinds_;
                if (in.code == op::call_receiver) {
                    // The callee was resolved elsewhere (up the prototype chain, for
                    // `super`) and the receiver is passed explicitly.
                    receiver = reg(in.c);
                } else if (in.code == op::call_method) {
                    receiver = reg(in.a);
                    // Through the SAME lookup as get_prop, so `s.split(...)` and
                    // `var f = s.split; f(...)` find the same function.
                    callee = lookup_property(receiver, vm_proto->names[in.c]);
                } else if (in.code == op::call_computed) {
                    receiver = reg(in.a);
                    callee = lookup_index(receiver, reg(in.c));
                }
                if (unwinds_ != unwound) { break; }
                const std::size_t arg_base = base + in.a + 1;
                if (callee.is_kind(heap_kind::native)) {
                    auto * nat = static_cast<native_object *>(callee.as_heap());
                    // COPIED, not spanned into the register stack. A native may call
                    // back into script - an event listener dispatching another
                    // event - and that grows registers_, which would leave a span
                    // into it dangling. One small vector per native call is the
                    // price of natives being allowed to re-enter the VM at all.
                    std::vector<value> args{
                        registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                        registers_.begin() + static_cast<std::ptrdiff_t>(arg_base + in.b)};
                    const value saved_this = current_this_;
                    current_this_ = receiver;
                    const value produced = [&] {
                        const native_scope pinned{*this};
                        return nat->fn(*this, args);
                    }();
                    current_this_ = saved_this;
                    // A throw the native's `call` parked is thrown HERE, at
                    // its call site - see context::call.
                    if (rethrow_pending()) { break; }
                    reg(in.a) = produced;
                    break;
                }
                if (!callee.is_kind(heap_kind::function)) {
                    {
                        std::string what = describe_callee(
                            (*vm_proto),
                            in.code == op::call_method ? vm_proto->names[in.c]
                            : in.code == op::call_computed
                                ? to_string(reg(in.c))
                                : callee_origin((*vm_proto), vm_frame->ip - 1, in.a),
                            callee);
                        // WHAT IT WAS CALLED ON. "`replace` is undefined" reads the
                        // same whether the method is missing from a real object or
                        // the object itself is undefined, and those are different
                        // bugs in different places.
                        if (in.code == op::call_method || in.code == op::call_computed) {
                            what += ", on " + std::string{type_of(receiver)};
                            if (receiver.is_nullish()) {
                                what += " (" + to_string(receiver) + ")";
                                // WHICH undefined. "`get` is undefined, on
                                // undefined" names the method and says nothing
                                // about the object, and the object is the bug -
                                // `get` is fine, whatever should have had it is
                                // missing. A method call keeps its receiver in the
                                // callee's own register, so the walk that names a
                                // plain call's callee names the receiver too.
                                const std::string from =
                                    callee_origin((*vm_proto), vm_frame->ip - 1, in.a);
                                if (!from.empty()) { what += " from `" + from + "`"; }
                            }
                        }
                        throw_error("TypeError", std::move(what));
                    }
                    break;
                }
                auto * fnobj = static_cast<closure_object *>(callee.as_heap());
                const function_proto & target = *fnobj->proto;
                // CALLING A GENERATOR RUNS NOTHING. It hands back an object over a
                // (*vm_frame) that has not started; the first instruction runs on the
                // first `.next()`.
                if (target.is_generator) {
                    std::vector<value> args{
                        registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                        registers_.begin() + static_cast<std::ptrdiff_t>(arg_base + in.b)};
                    reg(in.a) = make_generator(fnobj, receiver, args);
                    break;
                }
                // The callee's (*vm_frame) starts where its arguments already are, so no
                // copying is needed to pass them.
                const std::size_t new_base = arg_base;
                const std::size_t needed = new_base + target.frame_size + 8u;
                if (registers_.size() < needed) { registers_.resize(needed, value::undefined()); }
                for (std::size_t i = in.b; i < target.param_count; ++i) {
                    registers_[new_base + i] = value::undefined(); // missing args
                }
                // A COMPILED BODY, IF THIS FUNCTION HAS ONE - asked in the one
                // place that asks, so that every other entry into a function
                // gets the same answer. See script/dispatch.hpp.
                //
                // AFTER the argument fill, so `argv` is what the callee's row
                // promises: the window with its missing parameters already
                // undefined. BEFORE the depth guard, because ct_aot_enter owns
                // that guard for a compiled frame.
                if (value produced = value::undefined();
                    enter_compiled(*this, target, callee, registers_.data() + new_base, new_base,
                                   in.b, receiver, /*constructing*/ false, produced)) {
                    reg(in.a) = produced;
                    break;
                }
                if (frames_.size() > 512) {
                    raise("call stack exhausted");
                    break;
                }
                call_frame entered{&target, 0,     new_base, in.a,
                                   in.b,    fnobj, receiver, handlers_.size()};
                entered.new_target = pending_new_target_;
                pending_new_target_ = value::undefined();
                frames_.push_back(entered);
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(ret)
        VM_CASE(ret_undef) do {
            {
                value returned = in.code == op::ret ? reg(in.a) : value::undefined();
                if (vm_frame->constructing && !returned.is_object_like()) {
                    returned = vm_frame->receiver;
                }
                const std::uint16_t slot = vm_frame->result_reg;
                // Handlers this (*vm_frame) installed die with it: a `return` out of a
                // try block must not leave its catch reachable from the caller.
                if (handlers_.size() > vm_frame->handler_base) {
                    handlers_.resize(vm_frame->handler_base);
                }
                VM_POP_FRAME(returned);
                if (frames_.size() <= stop_depth) { return returned; }
                registers_[frames_.back().base + slot] = returned;
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(type_of) do {
            reg(in.a) = string(std::string{type_of(reg(in.b))});
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(load_this) do {
            reg(in.a) = effective_this((*vm_frame));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_new_target) do {
            reg(in.a) = vm_frame->new_target;
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(make_arguments) do {
            // THE POINTER IS COMPUTED AT THE CALL, not hoisted. make_array()
            // inside the member allocates, and allocation can collect - but it
            // does not resize registers_, so the pointer survives. That is a
            // fact about context::allocate rather than an accident, and it is
            // why this is safe.
            reg(in.a) = make_arguments_object(*vm_frame, registers_.data() + base, vm_frame->argc);
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_callee) do {
            reg(in.a) = vm_frame->closure != nullptr ? value::object(vm_frame->closure)
                                                     : value::undefined();
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(iterable) do {
            reg(in.a) = iterable_values(reg(in.b));
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(own_keys) do {
            reg(in.a) = own_keys(reg(in.b));
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(wrap_promise) do {
            // Already a promise (`return somePromise` inside an async function)
            // stays as it is rather than nesting. The test itself lives on
            // `context` because the compiled tier runs the same one - see
            // wrap_in_promise.
            reg(in.a) = wrap_in_promise(reg(in.a));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(await_value) do {
            {
                // A settled promise carries its value in `__value`; anything else
                // awaits to itself. A REJECTED promise throws, which is what makes
                // `try { await f() } catch` work.
                const value awaited = reg(in.b);
                // A PENDING PROMISE SUSPENDS THE FRAME. There is one stack and
                // the event loop is above it, so `await` cannot block: the frame
                // is lifted out, the caller is handed a promise, and the frame
                // comes back when the awaited one settles.
                if (is_pending_promise(awaited) && pending_promise_factory_ && promise_settler_) {
                    if (vm_frame->async_promise.is_undefined()) {
                        vm_frame->async_promise = pending_promise_factory_(*this);
                    }
                    const value promise = vm_frame->async_promise;
                    // AN ASYNC GENERATOR'S FRAME IS ALREADY A COROUTINE - the one
                    // its `.next()` resumes - so the await parks THAT object rather
                    // than making a second one the generator would never see.
                    // `awaiting` keeps the request queue from resuming it until
                    // the awaited promise does.
                    coroutine_object * saved = vm_frame->generator;
                    if (saved != nullptr) {
                        saved->awaiting = true;
                        saved->running = false;
                        saved->handlers.clear();
                    } else {
                        saved = allocate<coroutine_object>();
                    }
                    saved->proto = vm_frame->proto;
                    saved->ip = vm_frame->ip;
                    saved->await_reg = in.a;
                    saved->argc = vm_frame->argc;
                    saved->closure = vm_frame->closure;
                    saved->receiver = vm_frame->receiver;
                    saved->constructing = vm_frame->constructing;
                    saved->promise = promise;
                    saved->window.assign(registers_.begin() + static_cast<std::ptrdiff_t>(base),
                                         registers_.end());
                    // This (*vm_frame)'s handlers travel with it, with reg_top made
                    // RELATIVE - the (*vm_frame) comes back somewhere else in the stack,
                    // and an absolute mark would point at whatever is there then.
                    for (std::size_t i = vm_frame->handler_base; i < handlers_.size(); ++i) {
                        handler moved = handlers_[i];
                        moved.reg_top -= base;
                        saved->handlers.push_back(moved);
                    }
                    handlers_.resize(vm_frame->handler_base);
                    const std::uint16_t slot = vm_frame->result_reg;
                    registers_.resize(base);
                    frames_.pop_back();
                    attach_resume(awaited, value::object(saved));
                    suspended_ = true;
                    if (frames_.size() <= stop_depth) { return promise; }
                    registers_[frames_.back().base + slot] = promise;
                    break;
                }
                reg(in.a) = awaited;
                if (awaited.is_object()) {
                    auto * obj = static_cast<object_object *>(awaited.as_heap());
                    if (value * state = obj->find("__rejected");
                        state != nullptr && truthy(*state)) {
                        thrown_ = obj->find("__value") != nullptr ? *obj->find("__value")
                                                                  : value::undefined();
                        if (!unwind_to_handler()) { raise("uncaught rejection"); }
                        break;
                    }
                    if (value * settled = obj->find("__value")) { reg(in.a) = *settled; }
                }
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(yield_value) do {
            {
                // SUSPEND INTO THE GENERATOR AND HAND THE VALUE OUT. Everything
                // here is the (*vm_frame)-lifting `await` does a few cases up; what
                // differs is only who puts it back, and that a value goes to the
                // caller of `.next()` rather than to a promise.
                coroutine_object * saved = vm_frame->generator;
                if (saved == nullptr) {
                    // The compiler refuses `yield` outside a generator, so this is
                    // unreachable - and cheap insurance against it becoming
                    // reachable, since the alternative is a null dereference.
                    raise("`yield` outside a generator");
                    break;
                }
                const value produced = reg(in.b);
                saved->ip = vm_frame->ip;
                saved->await_reg = in.a;
                saved->receiver = vm_frame->receiver;
                saved->window.assign(registers_.begin() + static_cast<std::ptrdiff_t>(base),
                                     registers_.end());
                // This (*vm_frame)'s handlers travel with it, with reg_top made RELATIVE:
                // the (*vm_frame) comes back somewhere else in the register stack, and an
                // absolute mark would point at whatever is there then.
                saved->handlers.clear();
                for (std::size_t i = vm_frame->handler_base; i < handlers_.size(); ++i) {
                    handler moved = handlers_[i];
                    moved.reg_top -= base;
                    saved->handlers.push_back(moved);
                }
                handlers_.resize(vm_frame->handler_base);
                registers_.resize(base);
                frames_.pop_back();
                yielded_ = true;
                if (frames_.size() <= stop_depth) { return produced; }
                // A generator body is only ever entered by generator_resume, which
                // stops at its own depth - so reaching here would mean a yield ran
                // under some other caller's loop and there is nowhere to put the
                // value.
                raise("a generator yielded outside its own resume");
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(bind_export) do {
            // bx(), NOT b. b is the HIGH half of the pair and c the low one, so
            // a decoder that reads b alone gets name index 0 for every module
            // with fewer than 65,536 names - which is all of them - and exports
            // an unrelated binding under this name.
            //
            // THE DESTINATION IS ALSO A SOURCE, and that is the shape the
            // conditional write needs. Outside a module the interpreter writes
            // nothing at all and the register holds the local being exported;
            // context::module_export_cell answers that same value back, so
            // "does not write" and "writes what was there" are one expression
            // both tiers can spell. See its declaration.
            const value published = module_export_cell(vm_proto->names[in.bx()], reg(in.a));
            reg(in.a) = published;
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(load_import) do {
            // b IS THE EXPORT NAME AND c IS THE SPECIFIER, which is the reverse
            // of the reading order and makes this the only opcode that reads c
            // as a standalone index. bytecode.hpp says the same thing: "a = the
            // cell exported as names[b] by the module at specifier names[c]".
            //
            // A CELL LANDS HERE, not a value, which is what makes the binding
            // live - and the local was marked boxed with NO new_cell for that
            // reason, so boxing this would leave every read seeing a cell
            // containing a cell.
            const value cell = module_import_cell(vm_proto->names[in.c], vm_proto->names[in.b]);
            reg(in.a) = cell;
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(load_namespace) do {
            const value ns = module_namespace_for(vm_proto->names[in.b]);
            reg(in.a) = ns;
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(dyn_import) do {
            // THE REFERRER COMES FROM THE RUNNING FUNCTION, not from
            // current_module_, and the difference is the whole point: a
            // dynamic import is usually called long after its module finished
            // evaluating, from a callback, where current_module_ is null. The
            // proto knows which module it was compiled in - see
            // function_proto::module.
            //
            // THE RESULT INTO A LOCAL FIRST, and it is not a style choice. The
            // loader evaluates the module it fetches, which RE-ENTERS this VM
            // and grows `registers_` - so a reference to reg(in.a) taken before
            // the call points into a freed buffer by the time the value comes
            // back.
            const value loaded = dynamic_import(
                reg(in.b), vm_frame->proto == nullptr ? std::string{} : vm_frame->proto->module);
            // NO PRE-WRITE OF undefined, and the store is skipped on ANY raise:
            // the register keeps whatever it held, which the opcode row calls
            // out and which is ct_aot_dynamic_import's "*out written ONLY on
            // CT_AOT_OK" rule.
            if (!failed_) { reg(in.a) = loaded; }
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(pass_new_target) do {
            // The NEXT (*vm_frame) pushed - the base constructor super() is about to
            // enter - gets this (*vm_frame)'s new.target rather than undefined.
            pass_new_target(vm_frame->new_target);
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(set_proto) do {
            set_prototype(reg(in.a), reg(in.b));
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(get_proto) do {
            // `super` has to start its lookup at the prototype ABOVE the class
            // the running method was written in - not above `this`, which in a
            // three-deep hierarchy is a different object and would call the
            // method again forever. So each method carries its home object.
            reg(in.a) = get_prototype(reg(in.b));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_home) do {
            {
                closure_object * running = frames_.empty() ? nullptr : frames_.back().closure;
                reg(in.a) = value::undefined();
                if (running != nullptr) {
                    if (value * home = running->find("__home")) { reg(in.a) = *home; }
                }
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(construct) do {
            {
                const value callee = reg(in.a);
                // A PROXY GOES THE LONG WAY ROUND. The inline path exists to avoid
                // a nested interpreter loop, and a construct trap needs one - so
                // this hands over to the general form rather than duplicating it.
                if (callee.is_kind(heap_kind::proxy)) {
                    const std::size_t arg_base = base + in.a + 1;
                    std::vector<value> args{
                        registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                        registers_.begin() + static_cast<std::ptrdiff_t>(arg_base + in.b)};
                    reg(in.a) = construct(callee, args);
                    break;
                }
                // A NATIVE GOES THE LONG WAY TOO, for the same reason as a proxy:
                // the inline path exists to avoid a nested interpreter loop, which
                // only a JavaScript body needs, and a second copy of the native
                // case is a second chance to disagree about `new Number(5)`.
                if (callee.is_kind(heap_kind::native)) {
                    const std::size_t arg_base = base + in.a + 1;
                    std::vector<value> args{
                        registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                        registers_.begin() + static_cast<std::ptrdiff_t>(arg_base + in.b)};
                    reg(in.a) = construct(callee, args);
                    break;
                }
                // The instance's prototype comes from the constructor's own
                // `prototype` property, which is what makes a method defined on the
                // class reachable from every instance.
                auto * instance = allocate<object_object>();
                if (callee.is_object()) {
                    if (value * proto =
                            static_cast<object_object *>(callee.as_heap())->find("prototype")) {
                        instance->prototype = *proto;
                    }
                } else if (callee.is_kind(heap_kind::function)) {
                    instance->prototype = ensure_prototype(callee);
                }
                const value self = value::object(instance);
                // ROOTED FOR THE SAME REASON context::construct roots its own:
                // the instance is in a C++ local while field initialisers run
                // user JavaScript, and it stays in one until the frame that
                // carries it as a receiver is pushed. reg(in.a) still holds the
                // CALLEE at this point, so nothing else refers to it.
                const rooted keep_instance{*this, self};
                run_field_initialisers(callee, self);
                const std::size_t arg_base = base + in.a + 1;

                if (!callee.is_kind(heap_kind::function)) {
                    // THE MESSAGE IS SHARED, so a compiled `new` on a
                    // non-constructor cannot spell it differently. The origin
                    // is the backwards scan, which only an interpreted frame
                    // has an ip for.
                    new_callee_type_error(
                        (*vm_proto), callee_origin((*vm_proto), vm_frame->ip - 1, in.a), callee);
                    break;
                }
                auto * fnobj = static_cast<closure_object *>(callee.as_heap());
                const function_proto & target = *fnobj->proto;
                const std::size_t new_base = arg_base;
                const std::size_t needed = new_base + target.frame_size + 8u;
                if (registers_.size() < needed) { registers_.resize(needed, value::undefined()); }
                for (std::size_t i = in.b; i < target.param_count; ++i) {
                    registers_[new_base + i] = value::undefined();
                }
                if (frames_.size() > 512) {
                    raise("call stack exhausted");
                    break;
                }
                // `new` ASKS FOR A COMPILED BODY TOO, and passes `constructing`,
                // which is what makes a constructor returning a primitive
                // evaluate to its receiver (ct_aot_return_value).
                //
                // IT MUST HAND OVER new.target: the interpreted path below sets
                // fresh.new_target directly, but ct_aot_enter can only read it
                // from pending_new_target_.
                //
                // SET AND RESTORED rather than set and cleared: op::construct
                // never consumes the flag on its own path, and ct_aot_enter
                // clears it once the frame is pushed. Restoring keeps the
                // interpreted path's behaviour identical either way.
                const value saved_new_target = pending_new_target_;
                pending_new_target_ = callee;
                if (value produced = value::undefined();
                    enter_compiled(*this, target, callee, registers_.data() + new_base, new_base,
                                   in.b, self, /*constructing*/ true, produced)) {
                    pending_new_target_ = saved_new_target;
                    reg(in.a) = produced.is_object_like() ? produced : self;
                    break;
                }
                pending_new_target_ = saved_new_target;
                call_frame fresh{&target, 0, new_base, in.a, in.b, fnobj, self, handlers_.size()};
                fresh.constructing = true;
                fresh.new_target = callee;
                frames_.push_back(fresh);
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(push_handler) do {
            handlers_.push_back(
                handler{frames_.size() - 1,
                        static_cast<std::size_t>(vm_frame->ip) + static_cast<std::size_t>(in.sbx()),
                        registers_.size(), in.a});
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(pop_handler) do {
            if (!handlers_.empty()) { handlers_.pop_back(); }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(throw_value) do {
            {
                thrown_ = reg(in.a);
                if (!unwind_to_handler()) { raise("uncaught " + describe_thrown(thrown_)); }
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(new_cell) do {
            reg(in.a) = value::object(allocate<cell_object>(reg(in.a)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(cell_get) do {
            reg(in.a) = reg(in.b).is_kind(heap_kind::cell)
                            ? static_cast<cell_object *>(reg(in.b).as_heap())->slot
                            : value::undefined();
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(cell_set) do {
            if (reg(in.a).is_kind(heap_kind::cell)) {
                static_cast<cell_object *>(reg(in.a).as_heap())->slot = reg(in.b);
            }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(get_upvalue) do {
            {
                reg(in.a) = value::undefined();
                if (vm_frame->closure != nullptr && in.b < vm_frame->closure->upvalues.size()) {
                    const value cell = vm_frame->closure->upvalues[in.b];
                    if (cell.is_kind(heap_kind::cell)) {
                        reg(in.a) = static_cast<cell_object *>(cell.as_heap())->slot;
                    }
                }
                break;
            }
        }
        while (0);
        VM_NEXT;
        VM_CASE(set_upvalue) do {
            {
                if (vm_frame->closure != nullptr && in.a < vm_frame->closure->upvalues.size()) {
                    const value cell = vm_frame->closure->upvalues[in.a];
                    if (cell.is_kind(heap_kind::cell)) {
                        static_cast<cell_object *>(cell.as_heap())->slot = reg(in.b);
                    }
                }
                break;
            }
        }
        while (0);
        VM_NEXT;
        VM_CASE(halt) do {
            return value::undefined();
        }
        while (0);
        VM_NEXT;
        VM_DISPATCH_END
    }
#if VM_COMPUTED_GOTO
// Where VM_NEXT goes when the loop is over. In switch mode the while condition
// is the exit and this label would be unused - a -Wunused-label error - hence
// the guard.
vm_done:
#endif
    return value::undefined();
}
#if VM_COMPUTED_GOTO
#if defined(__clang__)
#pragma clang diagnostic pop
#else
#pragma GCC diagnostic pop
#endif
#endif

// AND THE PICK, ONCE PER ENTRY rather than once per instruction. A recorder is
// installed before the context that should record is constructed and never
// changes while a body runs, so there is nothing to re-decide inside the loop.
//
// BOTH INSTANTIATIONS ARE NAMED HERE, which is what makes them exist: they are
// used from this translation unit and nowhere else, so nothing else needs the
// definition and no explicit instantiation is required.
value context::run_loop(std::size_t stop_depth) {
#if CTBROWSER_SCRIPT_RECORD_TYPES
    if (recorder_ != nullptr) { return run_loop_impl<true>(stop_depth); }
#endif
    return run_loop_impl<false>(stop_depth);
}

} // namespace ctbrowser::script
