//===- ctbrowser/lib/Script/vm/run_loop.cpp - Bytecode interpreter loop ---===//
//
// The bytecode interpreter loop, context::run_loop, with the small opcode
// handlers inline. The larger handlers (call, construct, await) are in
// run_loop/*.cpp. Members of `context`, declared in
// include/ctbrowser/script/vm.hpp.
//
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

// --- Instruction dispatch: computed goto, or a switch ------------------------
//
// The dispatch loop is 15% of a Phaser frame and 77% of benchmarks/bench_script.
// A `switch` compiles to one indirect branch that every opcode shares;
// replicating the jump into every handler gives the predictor one site per
// opcode, each of which can learn the opcode pairs the bytecode emits.
// docs/history/computed-goto.md has the measurements.
//
// Computed goto is GNU-only, so the switch is the portable fallback and both
// paths must keep working. It is off by default because it measured slower
// (the table in docs/performance.md). Opt in with -DCTBROWSER_COMPUTED_GOTO to
// measure it on your own hardware: the result depends on the branch predictor.
#if defined(__GNUC__) && defined(CTBROWSER_COMPUTED_GOTO)
#define VM_COMPUTED_GOTO 1
#else
#define VM_COMPUTED_GOTO 0
#endif

// --- Type-recording hook ----------------------------------------------------
//
// When a type recorder is installed (recorder_, see type_record.cpp), each
// step records the value types the previous instruction produced, for
// ctcompile's type analysis. It is `if constexpr`, so the Record = false
// instantiation of run_loop_impl contains nothing.
//
// It runs after the fetch, so `record_step` sees the instruction about to run
// and `vm_frame->ip` already pointing past it. It records the previous
// instruction's destination because a result is not readable until the
// handler that made it has returned, and `op::call`'s handler does not return
// until the callee does. The deferred flush, the interning and the parameter
// sweep that this implies are in type_record.cpp.
#define VM_RECORD_STEP()                                                                           \
    do {                                                                                           \
        if constexpr (Record) { record_step(in); }                                                 \
    } while (0)
// Pops the frame in `ret`. When recording (Record = true), calls
// record_frame_pop between the pop and the write of the return value: after
// the pop so the ending frame's roots are gone and frames_.back() is the
// caller, and before the write because the value is then only in a C++ local,
// so the hook roots it itself (through temporaries_). That is why `return {}`
// is recorded as escaping via temporaries rather than as confined. The exit
// is listed in ctcompile/include/ctcompile/JavaScript/FrameEnds.def. With
// Record = false this is a plain frames_.pop_back().
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

#if VM_COMPUTED_GOTO
// GNU extensions, with their warnings suppressed only here: the address-of-label
// and indirect-goto forms, and the C99 array designators that index the table
// by opcode rather than by position.
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
// Re-derives the frame exactly as the loop head does, instead of caching
// `vm_frame`, `vm_proto` and `base` across dispatches: handlers such as call,
// construct, ret, await, yield and throw push, pop or unwind frames_ (a
// std::vector), and a reallocation would leave a cached pointer dangling.
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

/// The interpreter loop: runs frames until the frame stack is back at
/// `stop_depth` and returns the value the last frame returned.
///
/// It is a template on `Record` rather than testing recorder_ per
/// instruction: a run-time `if (recorder_ != nullptr)` at the loop head
/// measured +0.53% on bench_script and +0.48% on phaser_invaders. run_loop
/// below picks the instantiation; the Record = true one is about 15 KB of
/// cold code.
template <bool Record> value context::run_loop_impl(std::size_t stop_depth) {
#if VM_COMPUTED_GOTO
    // Indexed by opcode through the array designators, so the table order
    // cannot drift out of step with the enum. A label address is not a
    // constant expression, so this is a function-local `static` rather than
    // constexpr; the emitted assembly shows plain .rodata with no thread-safe
    // init guard (which would cost an atomic load per dispatch).
#define CT_OPCODE(name, ...) [static_cast<std::size_t>(op::name)] = &&VM_LABEL_##name,
    static void * const vm_table[] = {
#include <ctbrowser/script/bytecode_opcodes.def>
    };
#undef CT_OPCODE
    static_assert(std::size(vm_table) == static_cast<std::size_t>(op::halt) + 1,
                  "bytecode_opcodes.def must list every opcode - a gap here is a null table "
                  "entry and a jump to address zero at run time");
#endif

    // Declared before the loop because in computed-goto mode the handlers are
    // jumped to directly and never pass through the loop head, so anything
    // they read must survive a goto that bypasses initialisation.
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
        // clang-format off
        VM_CASE(load_const) do {
            reg(in.a) = vm_proto->constants[in.bx()];
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_bigint) do {
            // Memoised under this proto, the interpreter's own key. AOT-compiled
            // code passes a marker address of its own instead, because the two
            // number their slots differently.
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

        VM_CASE(get_global)
        VM_CASE(get_global_typeof) do {
            // The running frame's realm: one load, no search, since a frame is
            // never pushed without one (call_frame::realm).
            const string_flat_map<value> & globals = vm_frame->realm->globals;
            // An engine helper never comes from the globals table, where a
            // script's `globalThis['%class_defined']` could have put one.
            if (internal_global(vm_proto->names[in.bx()])) [[unlikely]] {
                reg(in.a) = global_or_named(vm_proto->names[in.bx()], true);
                break;
            }
            const auto & lexical = vm_frame->realm->lexical;
            if (const auto binding = lexical.find(vm_proto->names[in.bx()]);
                binding != lexical.end() && !binding->second) [[unlikely]] {
                reg(in.a) = global_or_named(vm_proto->names[in.bx()],
                                           in.code == op::get_global_typeof);
                break;
            }
            const auto it = globals.find(vm_proto->names[in.bx()]);
            if (it != globals.end()) {
                reg(in.a) = it->second;
                break;
            }
            reg(in.a) = global_or_named(vm_proto->names[in.bx()], in.code == op::get_global_typeof);
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(set_global) do {
            assign_global(*vm_frame->realm, vm_proto->names[in.bx()], reg(in.a),
                          vm_proto->is_strict);
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(init_global) do {
            initialize_global(*vm_frame->realm, vm_proto->names[in.bx()], reg(in.a));
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(add) do {
            // Used only for `++`/`--` and internal counters (compile_binary
            // maps source `+` to add_generic). ToNumeric of an object operand
            // comes first, so `o++` on {valueOf: () => 3} stores 4. The
            // postfix expression's own value is still the object it read: the
            // old value is moved before this runs, so no separate ToNumeric
            // opcode is spent on every loop counter.
            value lhs = numeric_operand(reg(in.b));
            value rhs = numeric_operand(reg(in.c));
            // `1n++`: the step is the integral Number 1 (or -1, or the 0 that
            // reads the old value) beside a BigInt. Since this opcode is never
            // source `+`, the step is converted to a BigInt here.
            if (lhs.is_kind(heap_kind::bigint) != rhs.is_kind(heap_kind::bigint)) {
                value & number = lhs.is_kind(heap_kind::bigint) ? rhs : lhs;
                if (number.is_number()) {
                    if (const std::optional<bigint> as_big =
                            bigint_from_double(number.as_number())) {
                        number = value::object(allocate<bigint_object>(*as_big));
                    }
                }
            }
            reg(in.a) = binary_op_static(op::add, lhs, rhs);
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
            // `+x` is a conversion, not a move: `+"2"` is the number 2.
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
        // prototype chain, the same chain lookup_property walks.
        VM_CASE(instance_of) do {
            reg(in.a) = value::boolean(instance_of(reg(in.b), reg(in.c)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(has_property) do {
            // Operand b is the key and c the target, the reverse of reading
            // order (`b in c`); has_property takes them target first.
            reg(in.a) = value::boolean(has_property(reg(in.c), reg(in.b)));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(bit_and) do {
            if (value l, r; numeric_operands(reg(in.b), reg(in.c), l, r)) {
                reg(in.a) = binary_op_static(op::bit_and, l, r);
            }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(bit_or) do {
            if (value l, r; numeric_operands(reg(in.b), reg(in.c), l, r)) {
                reg(in.a) = binary_op_static(op::bit_or, l, r);
            }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(bit_xor) do {
            if (value l, r; numeric_operands(reg(in.b), reg(in.c), l, r)) {
                reg(in.a) = binary_op_static(op::bit_xor, l, r);
            }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(shl) do {
            // numeric_operands converts left then right and stops if the left
            // throws; two conversions as arguments of one call would run in
            // unspecified order.
            if (value l, r; numeric_operands(reg(in.b), reg(in.c), l, r)) {
                reg(in.a) = binary_op_static(op::shl, l, r);
            }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(shr) do {
            // numeric_operands converts left then right and stops if the left
            // throws; two conversions as arguments of one call would run in
            // unspecified order.
            if (value l, r; numeric_operands(reg(in.b), reg(in.c), l, r)) {
                reg(in.a) = binary_op_static(op::shr, l, r);
            }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(ushr) do {
            // BigInt operands are a TypeError here: an unsigned shift needs a
            // width to fill from and a BigInt has none. binary_op_static
            // raises it.
            if (value l, r; numeric_operands(reg(in.b), reg(in.c), l, r)) {
                reg(in.a) = binary_op_static(op::ushr, l, r);
            }
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(bit_not) do {
            reg(in.a) = bit_not_value(numeric_operand(reg(in.b)));
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(copy_props) do {
            // a is the target and b the source; the target object is mutated
            // in place, not written back.
            copy_own_properties(reg(in.a), reg(in.b));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(delete_prop) do {
            // a is the target and b names the property; nothing is written
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

        // All four are one comparison asked four ways (see
        // context::compare_relational).
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
            // A loop's back edge is a safepoint for strings: a loop that
            // concatenates and calls nothing never reaches `invoke`, so its
            // dead strings would otherwise never be collected. Every value is
            // in a register here. It collects only when the string budget is
            // spent; an object-count collection still waits for a call.
            if (in.sbx() < 0 && string_bytes_ >= string_budget_) [[unlikely]] {
                (void)collect_if_due();
            }
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
                // The opcode says which half is defined; define_accessor takes
                // both, with undefined for the missing one.
                const bool getter = in.code == op::define_getter;
                define_accessor(reg(in.a), vm_proto->names[in.b],
                                getter ? reg(in.c) : value::undefined(),
                                getter ? value::undefined() : reg(in.c));
            }
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(define_data) do {
            define_data_property(reg(in.a), reg(in.b), reg(in.c), true);
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(apply)
        VM_CASE(construct_apply) do {
            // a is both the callee and the destination, b is the argument
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
        // A setter on the chain takes the write; only if none does is an own
        // data property defined. store_property has the whole rule.
        VM_CASE(set_prop) do {
            // Strict code throws where sloppy code drops the write;
            // store_property reports a rejection through store_rejected_
            // (13.15.2 PutValue step 6.b).
            store_rejected_ = false;
            store_property(reg(in.a), vm_proto->names[in.b], reg(in.c));
            if (vm_proto->is_strict) { strict_store_check(vm_proto->names[in.b]); }
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
            store_rejected_ = false;
            store_index(reg(in.a), reg(in.b), reg(in.c));
            if (vm_proto->is_strict && store_rejected_) {
                strict_store_check(to_string(reg(in.b)));
            }
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(closure) do {
            {
                // The work is in context::make_closure, which ahead-of-time compiled code
                // also calls (through ct_aot_make_closure), so the interpreter and compiled
                // code cannot diverge. This handler supplies only the interpreter's part: the
                // register window and the receiver an arrow function captures.
                //
                // Upvalues are passed by register, because a descriptor marked
                // from_parent_local names a register in this frame's window.
                // Compiled code passes an array indexed in parallel with the
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
            execute_call(in, vm_frame, vm_proto, base);
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(ret)
        VM_CASE(ret_undef) do {
            {
                value returned = in.code == op::ret ? reg(in.a) : value::undefined();
                if (vm_frame->constructing && !returned.is_object_like()) {
                    returned = effective_this(*vm_frame);
                }
                const std::uint16_t slot = vm_frame->result_reg;
                // Exception handlers this frame installed die with it: a `return` out of a
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
            // 10.2.1.2 OrdinaryCallBindThis step 5.a: a sloppy function called
            // with no receiver (`f()`, or a native calling back with undefined)
            // sees the global object; strict code and an arrow see what they
            // were given. (A primitive receiver stays unboxed here - ponytail:
            // ToObject lives in builtins/internal.hpp, box it when a test needs it.)
            if (!vm_proto->is_strict && !vm_proto->is_arrow && reg(in.a).is_nullish()) {
                reg(in.a) = vm_frame->realm->global_this;
            }
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
            // The register pointer is computed at the call. make_array() inside
            // the member allocates, and allocation can collect, but it never
            // resizes registers_ (a guarantee of context::allocate), so the
            // pointer stays valid.
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
            reg(in.a) = spread_values(reg(in.b));
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
            // A value that is already a promise (`return somePromise` in an async
            // function) is returned as is, not nested. The check is in
            // context::wrap_in_promise so ahead-of-time compiled code uses the same one.
            reg(in.a) = wrap_in_promise(reg(in.a));
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(await_value) do {
            if (const auto produced = execute_await(in, vm_frame, base, stop_depth)) { return *produced; }
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(yield_value) do {
            {
                // Suspend into the generator and hand the value out. This is the same
                // frame suspension execute_await (run_loop/await.cpp) does; the difference
                // is that the value goes to the caller of `.next()` rather than to a promise.
                coroutine_object * saved = vm_frame->generator;
                if (saved == nullptr) {
                    // The compiler refuses `yield` outside a generator, so this is
                    // unreachable; it is cheap insurance against a null
                    // dereference if that ever changes.
                    raise("`yield` outside a generator");
                    break;
                }
                const value produced = reg(in.b);
                suspend_frame(saved, in.a);
                yielded_ = true;
                if (frames_.size() <= stop_depth) { return produced; }
                // A generator body is only ever entered by generator_resume, which
                // stops at its own depth, so reaching here would mean a yield ran
                // under some other caller's loop and there is nowhere to put the
                // value.
                raise("a generator yielded outside its own resume");
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(bind_export) do {
            // The name index is bx(), not b: b is the high half of the pair and
            // c the low one, so reading b alone would give index 0 for any
            // module with fewer than 65,536 names.
            //
            // The destination register is also the input. Outside a module nothing is
            // exported and module_export_cell returns the value it was given, so
            // "no write" and "write back the same value" are the same expression for
            // both the interpreter and AOT-compiled code.
            const value published = module_export_cell(vm_proto->names[in.bx()], reg(in.a));
            reg(in.a) = published;
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(load_import) do {
            // b is the export name and c the specifier, the reverse of reading
            // order; this is the only opcode that reads c as a standalone index
            // (bytecode.hpp: "a = the cell exported as names[b] by the module
            // at specifier names[c]").
            //
            // A cell lands here, not a value, which keeps the binding live.
            // The local is marked boxed with no new_cell for that reason;
            // boxing it again would make every read see a cell in a cell.
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
            // The referrer comes from the running function, not from
            // current_module_: a dynamic import usually runs long after its
            // module finished evaluating, from a callback, where
            // current_module_ is null. The proto knows which module it was
            // compiled in (function_proto::module).
            //
            // The result goes into a local first: the loader evaluates the
            // module it fetches, which re-enters this VM and can grow
            // registers_, so a reference to reg(in.a) taken before the call
            // could dangle.
            const value loaded = dynamic_import(
                reg(in.b), vm_frame->proto == nullptr ? std::string{} : vm_frame->proto->module);
            // No pre-write of undefined, and the store is skipped on any raise:
            // the register keeps whatever it held, matching
            // ct_aot_dynamic_import's rule that *out is written only on
            // CT_AOT_OK.
            if (!failed_) { reg(in.a) = loaded; }
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(pass_new_target) do {
            // The next frame pushed (the base constructor super() is about to enter)
            // gets this frame's new.target rather than undefined.
            pass_new_target(vm_frame->new_target);
            super_call_ = true; // See refuse_class_call.
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
            // `super` starts its lookup at the prototype above the class the
            // running method was written in, not above `this`, which in a
            // three-deep hierarchy is a different object and would call the
            // method again forever. So each method carries its home object.
            reg(in.a) = super_base(reg(in.b));
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
            execute_construct(in, vm_frame, vm_proto, base);
            break;
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
        // clang-format on
        VM_DISPATCH_END
    }
#if VM_COMPUTED_GOTO
// Where VM_NEXT goes when the loop is over. In switch mode the while condition
// is the exit and this label would be unused (a -Wunused-label error), hence
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

/// Runs the interpreter loop, choosing the recording or plain instantiation
/// once per entry rather than once per instruction. A recorder is installed
/// before the recording context is constructed and never changes while a body
/// runs.
///
/// Naming both instantiations here is what makes them exist; they are used
/// only in this translation unit, so no explicit instantiation is needed.
value context::run_loop(std::size_t stop_depth) {
    if (recorder_ != nullptr) { return run_loop_impl<true>(stop_depth); }
    return run_loop_impl<false>(stop_depth);
}

} // namespace ctbrowser::script
