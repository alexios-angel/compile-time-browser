// ctbrowser.script context - the interpreter loop itself.
//
// One of four files carved out of a 3,232-line vm.cpp on 2026-08-09. All
// members of `context`, declared in include/ctbrowser/script/vm.hpp - so
// they split across translation units with nothing to declare.

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

// The VM's implementation.
//
// `run_loop` alone is 15 KB of object code - the whole instruction dispatch -
// and while it lived in the interface every translation unit that imported the
// module emitted its own copy and optimised it again. The class declaration
// stays in :vm; the bodies live here and are compiled once.

namespace ctbrowser::script {

// THE OPCODE LIST LIVES IN include/ctbrowser/script/bytecode_opcodes.def NOW,
// and this file no longer keeps a second copy of it.
//
// It used to: a private VM_OPCODES(X) macro listing all 93 names, with a
// static_assert tying its length to the enum's. That assert has moved to
// bytecode.hpp beside the table it checks, and the label table below is built
// by including the .def directly. One list, and the compiler reads the same
// one - which is the point of Phase 0's inventory: a compiler's opcode table
// and an interpreter's that can drift present as a MISCOMPILE rather than as a
// build failure.

// --- INSTRUCTION DISPATCH: computed goto, or a switch ------------------------
//
// The dispatch loop is 15% of a Phaser frame (measured - callgrind on
// test/corpus/phaser/phaser_invaders) and 77% of benchmarks/bench_script. A `switch` compiles to
// ONE indirect branch that all 88 opcodes share, so the predictor sees a single
// site with 88 targets and mispredicts constantly. Replicating the jump into
// every handler gives it 88 sites, each of which can learn the PAIRS this
// bytecode actually emits - `less` then `jump_if_false`, `get_prop` then
// `call_method`. docs/history/computed-goto.md has the measurement that justified
// trying it and the numbers it actually produced.
//
// GNU ONLY, and the switch is not a poor relation - it is the fallback that
// keeps this portable, and both paths must stay live. Any compiler without the
// address-of-label extension takes it, as does anyone defining
// CTBROWSER_NO_COMPUTED_GOTO to compare the two.
// OFF BY DEFAULT, BECAUSE IT MEASURED SLOWER - see the table in
// docs/performance.md. Opt in with -DCTBROWSER_COMPUTED_GOTO (or the CMake
// option of the same name) to measure it on your own hardware: the result is a
// property of the branch predictor, not of this code, and a different
// microarchitecture may well answer differently.
#if defined(__GNUC__) && defined(CTBROWSER_COMPUTED_GOTO)
#define VM_COMPUTED_GOTO 1
#else
#define VM_COMPUTED_GOTO 0
#endif

#if VM_COMPUTED_GOTO
// GNU extensions, suppressed HERE and nowhere else: the address-of-label and
// indirect-goto forms, and the C99 array designators that index the table by
// opcode rather than by position. Verified under
// -O2 -pedantic -Wall -Wextra -Werror -Wconversion on clang 24 and gcc 13.
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
        goto * vm_table[static_cast<std::size_t>(in.code)];                                        \
    } while (0)
#else
#define VM_CASE(name) case op::name:
#define VM_DISPATCH_BEGIN switch (in.code) {
#define VM_DISPATCH_END }
#define VM_NEXT break
#endif

value context::run_loop(std::size_t stop_depth) {
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

        VM_DISPATCH_BEGIN
        VM_CASE(load_const) do {
            reg(in.a) = vm_proto->constants[in.bx()];
            break;
        }
        while (0);
        VM_NEXT;
        VM_CASE(load_bigint) do {
            {
                // Parsed ONCE per site and cached beside the string cache: the
                // digits never change, and a BigInt in a loop should not re-read
                // them. A literal the lexer accepted but that is not an integer
                // - `1.5n` - has no value to load, and 0n is the honest answer
                // for a program that should have been refused at compile time.
                auto & cache = bigint_cache_[&(*vm_proto)];
                const auto it = cache.find(in.bx());
                if (it != cache.end()) {
                    reg(in.a) = it->second;
                } else {
                    const std::optional<bigint> parsed =
                        bigint_from_literal(vm_proto->strings[in.bx()]);
                    const value v =
                        value::object(allocate<bigint_object>(parsed.value_or(bigint{0})));
                    cache.emplace(in.bx(), v);
                    reg(in.a) = v;
                }
            }
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
                const auto it = globals_.find(vm_proto->names[in.bx()]);
                reg(in.a) = it == globals_.end() ? value::undefined() : it->second;
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
            // `+x` IS A CONVERSION. It compiled to a plain `move` for a long time,
            // so `+"2"` stayed the string "2" - and that is invisible in most of the
            // places it is written, because `+x + "/"` concatenates either way. It
            // shows up where the result is USED as a number: `d[(+y * 8 + +x) * 4]`
            // indexed with a string built by concatenation and read undefined.
            //
            // It was sitting in VM_CASE(negate), after a `break` and describing
            // a different opcode.
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

            // `x instanceof C` is true when C.prototype appears anywhere in x's
            // prototype chain - the same chain lookup_property walks.
        }
        while (0);
        VM_NEXT;
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
            {
                if (!reg(in.a).is_object()) { break; }
                auto * target = static_cast<object_object *>(reg(in.a).as_heap());
                if (reg(in.b).is_object()) {
                    // A copy of the source's entries first: `set` can reallocate
                    // the target's storage, and target and source may be the same
                    // object.
                    const std::vector<std::pair<std::string, value>> entries =
                        static_cast<object_object *>(reg(in.b).as_heap())->props;
                    for (const auto & [name, item] : entries) { target->set(name, item); }
                } else if (reg(in.b).is_array()) {
                    const std::vector<value> items =
                        static_cast<array_object *>(reg(in.b).as_heap())->items;
                    for (std::size_t i = 0; i < items.size(); ++i) {
                        target->set(std::to_string(i), items[i]);
                    }
                }
                break;
            }
        }
        while (0);
        VM_NEXT;
        VM_CASE(delete_prop) do {
            if (reg(in.a).is_object()) {
                (void)static_cast<object_object *>(reg(in.a).as_heap())
                    ->erase(vm_proto->names[in.b]);
            }
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
        // context::compare_relational, which is where strings stopped being
        // coerced to NaN.
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
                const bool getter = in.code == op::define_getter;
                const value g = getter ? reg(in.c) : value::undefined();
                const value st = getter ? value::undefined() : reg(in.c);
                if (reg(in.a).is_object()) {
                    static_cast<object_object *>(reg(in.a).as_heap())
                        ->define_accessor(vm_proto->names[in.b], g, st);
                } else if (reg(in.a).is_kind(heap_kind::function)) {
                    // a `static get` on a class, which IS the constructor closure
                    static_cast<closure_object *>(reg(in.a).as_heap())
                        ->define_accessor(vm_proto->names[in.b], g, st);
                }
            }
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(apply)
        VM_CASE(construct_apply) do {
            {
                // The arguments arrived as an ARRAY because their count was not
                // known until the spread was evaluated. Both go through the same
                // re-entrant call path a native callback uses, which is why one
                // opcode covers plain, method and computed calls: the receiver is
                // just a register the compiler already filled in.
                const value callee = reg(in.a);
                const value argv = reg(in.b);
                std::vector<value> args;
                if (argv.is_array()) { args = static_cast<array_object *>(argv.as_heap())->items; }
                if (in.code == op::construct_apply) {
                    reg(in.a) = construct(callee, args);
                } else if (!callee.is_callable()) {
                    raise("attempted to call a non-function");
                } else {
                    reg(in.a) = call(callee, args, reg(in.c));
                }
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(gather_rest) do {
            {
                // The arguments past the declared parameters. They are still in
                // this (*vm_frame)'s registers - the caller wrote them there and the
                // callee's base IS the argument base - so this reads them in place.
                //
                // UNLESS the body also built an `arguments` object, which happens
                // before this and claims a register an extra argument may be in.
                // Then the (*vm_frame)'s copy is the one that still has them.
                value out = make_array();
                auto * rest = static_cast<array_object *>(out.as_heap());
                if (vm_frame->arguments_object.is_array()) {
                    const auto & held =
                        static_cast<array_object *>(vm_frame->arguments_object.as_heap())->items;
                    for (std::size_t i = in.b; i < held.size(); ++i) {
                        rest->items.push_back(held[i]);
                    }
                } else {
                    for (std::size_t i = in.b; i < vm_frame->argc; ++i) {
                        rest->items.push_back(registers_[base + i]);
                    }
                }
                reg(in.a) = out;
                break;
            }
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
            // A SETTER ON THE CHAIN TAKES THE WRITE, and only if none does is an
            // own data property defined. Getting that backwards is how a setter
            // silently stops running: the write lands on the instance and shadows
            // the accessor from then on. store_property has the whole rule.
        }
        while (0);
        VM_NEXT;
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
                // THE BODY IS context::make_closure NOW, shared with
                // ct_aot_make_closure so the two tiers cannot drift - which is
                // what the ABI row for that helper asks for. What stays here is
                // the part that is the INTERPRETER's: its register window, and
                // the effective receiver an arrow captures.
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
                    const value produced = nat->fn(*this, args);
                    current_this_ = saved_this;
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
                if (value produced = value::undefined(); enter_compiled(
                        *this, target, callee, registers_.data() + new_base, in.b, receiver,
                        /*constructing*/ false, produced)) {
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
                frames_.pop_back();
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
            // `new.target` IS THE CONSTRUCTOR, OR UNDEFINED. The (*vm_frame) already
            // carries both halves - `constructing`, so `new C()` can evaluate to
            // the new object rather than the body's return, and `closure`, which is
            // the function running - so this reads state that was there rather than
            // adding any.
            //
            // The closure is the function this (*vm_frame) is EXECUTING, which for a
            // direct `new C()` is C. The spec's new.target follows the originally
            // invoked constructor through a `super()` chain to the derived-most
            // class; that distinction only shows up in a hierarchy, and the pages
            // that use this - a transpiler's `_classCallCheck`, Babylon's decorator
            // metadata - ask whether it is undefined, not which constructor it is.
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
            {
                // The (*vm_frame) knows how many arguments ARRIVED; the proto only knows
                // how many were declared, and those are different numbers whenever
                // `arguments` is worth reading at all.
                value list = make_array();
                auto * items = static_cast<array_object *>(list.as_heap());
                items->items.reserve(vm_frame->argc);
                for (std::uint16_t i = 0; i < vm_frame->argc; ++i) {
                    items->items.push_back(reg(i));
                }
                // On the (*vm_frame) too: this claims a register that an extra argument
                // may be in, so whatever still needs the raw ones reads them here.
                vm_frame->arguments_object = list;
                reg(in.a) = list;
                break;
            }
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
            {
                // The own property names of an object, as an array. `for (k in o)`
                // compiles to a for-of over this, which keeps one iteration
                // mechanism instead of two.
                value out = make_array();
                auto * keys = static_cast<array_object *>(out.as_heap());
                if (reg(in.b).is_object()) {
                    // In DEFINITION ORDER, data and accessors interleaved - which
                    // is what a page sees from for-in and has to match Object.keys.
                    // for-in enumerates STRING keys only.
                    static_cast<object_object *>(reg(in.b).as_heap())
                        ->each_own_string_key(
                            [&](const std::string & name) { keys->items.push_back(string(name)); });
                } else if (reg(in.b).is_array()) {
                    const std::size_t n =
                        static_cast<array_object *>(reg(in.b).as_heap())->items.size();
                    for (std::size_t i = 0; i < n; ++i) {
                        keys->items.push_back(string(std::to_string(i)));
                    }
                }
                reg(in.a) = out;
                break;
            }
        }
        while (0);
        VM_NEXT;

        VM_CASE(wrap_promise) do {
            // Already a promise (`return somePromise` inside an async function)
            // stays as it is rather than nesting.
            if (!(reg(in.a).is_object() &&
                  static_cast<object_object *>(reg(in.a).as_heap())->find("__value") != nullptr)) {
                reg(in.a) = make_promise(reg(in.a), false);
            }
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
                // A PENDING PROMISE SUSPENDS THE FRAME.
                //
                // There is one stack and the event loop is above it, so `await`
                // cannot block: the (*vm_frame) is lifted out, the caller is handed a
                // promise, and the (*vm_frame) comes back when the awaited one settles.
                //
                // It used to read `__value` off a promise that had none, so `await`
                // on anything genuinely asynchronous evaluated to UNDEFINED and ran
                // the rest of the function immediately - the single largest wrong
                // answer left in this engine, and silent.
                if (is_pending_promise(awaited) && pending_promise_factory_ && promise_settler_) {
                    if (vm_frame->async_promise.is_undefined()) {
                        vm_frame->async_promise = pending_promise_factory_(*this);
                    }
                    const value promise = vm_frame->async_promise;
                    auto * saved = allocate<coroutine_object>();
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
            // ADOPT THE RECORD'S CELL, do not publish this register's. The cell
            // is created before ANY module in the graph runs - see
            // instantiate_module - so by the time this executes it already
            // exists and something in a cycle may already be holding it.
            // Overwriting the record here would hand that importer a box
            // nobody ever writes to again.
            //
            // THE CELL, NOT THE VALUE, either way: an importer holds the box,
            // which is what makes the binding LIVE. Handing over the value
            // passes "an importer sees an export" and fails "an imported
            // binding is live" - the shortcut docs/plans/modules.md names in
            // advance.
            if (current_module_ != nullptr) {
                value & slot = current_module_->exports[vm_proto->names[in.bx()]];
                if (!slot.is_kind(heap_kind::cell)) {
                    slot = value::object(allocate<cell_object>(value::undefined()));
                }
                reg(in.a) = slot;
            }
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(load_import) do {
            // The exporter has been evaluated already - the loader walks the
            // graph depth-first - so its cell is there to be taken.
            reg(in.a) = value::undefined();
            const std::string & written = vm_proto->names[in.c];
            const std::string & what = vm_proto->names[in.b];
            // AS WRITTEN IS NOT AS KEYED: `./dep.js` in one module and in
            // another are two different files. The loader left the translation
            // in the record - see module_record::resolved.
            const std::string & from = [&]() -> const std::string & {
                if (current_module_ == nullptr) { return written; }
                const auto mapped = current_module_->resolved.find(written);
                return mapped == current_module_->resolved.end() ? written : mapped->second;
            }();
            const auto found = modules_.find(from);
            if (found == modules_.end()) {
                raise("module `" + from + "` was not loaded");
                break;
            }
            const auto cell = found->second.exports.find(what);
            if (cell == found->second.exports.end()) {
                raise("`" + from + "` has no export named `" + what + "`");
                break;
            }
            reg(in.a) = cell->second;
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(load_namespace) do {
            reg(in.a) = value::undefined();
            const std::string & written = vm_proto->names[in.b];
            const std::string & from = [&]() -> const std::string & {
                if (current_module_ == nullptr) { return written; }
                const auto mapped = current_module_->resolved.find(written);
                return mapped == current_module_->resolved.end() ? written : mapped->second;
            }();
            const auto found = modules_.find(from);
            if (found == modules_.end()) {
                raise("module `" + from + "` was not loaded");
                break;
            }
            reg(in.a) = module_namespace(found->second);
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
            if (!module_loader_) {
                raise("dynamic import() has no loader installed");
                break;
            }
            const value spec = reg(in.b);
            // THE RESULT INTO A LOCAL FIRST, and it is not a style choice. The
            // loader evaluates the module it fetches, which RE-ENTERS this VM
            // and grows `registers_` - so a reference to reg(in.a) taken before
            // the call points into a freed buffer by the time the value comes
            // back. Written that way it stored the promise into memory nobody
            // owned and `import(...)` read undefined, with no error anywhere.
            const value loaded = module_loader_(
                *this, to_string(spec),
                vm_frame->proto == nullptr ? std::string{} : vm_frame->proto->module);
            reg(in.a) = loaded;
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(pass_new_target) do {
            // The NEXT (*vm_frame) pushed - the base constructor super() is about to
            // enter - gets this (*vm_frame)'s new.target rather than undefined.
            pending_new_target_ = vm_frame->new_target;
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(set_proto) do {
            if (reg(in.a).is_object()) {
                static_cast<object_object *>(reg(in.a).as_heap())->prototype = reg(in.b);
            }
            break;
        }
        while (0);
        VM_NEXT;

        VM_CASE(get_proto) do {
            reg(in.a) = reg(in.b).is_object()
                            ? static_cast<object_object *>(reg(in.b).as_heap())->prototype
                            : value::undefined();
            break;

            // `super` has to start its lookup at the prototype ABOVE the class the
            // running method was written in - not above `this`, which in a
            // three-deep hierarchy is a different object and would call the method
            // again forever. So each method carries its home object.
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
                // A NATIVE GOES THE LONG WAY TOO, for the same reason as a proxy: the
                // inline path exists to avoid a nested interpreter loop, which only a
                // JavaScript body needs. Duplicating the native case here is what let
                // the two disagree - this copy neither set the instance's prototype
                // from a native's `prototype` property nor honoured the conversion
                // flag, so `new Number(5)` was an empty object down this path and a 5
                // down the other, depending only on whether a proxy was involved.
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
                    // THE MESSAGE IS SHARED NOW, so a compiled `new` on a
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
                // AND `new` ASKS TOO, which it never did: this handler pushed a
                // frame of its own, so a constructor with a compiled body was
                // interpreted and nothing said so. It also passes
                // `constructing`, which is not a detail - it is what makes a
                // constructor returning a primitive evaluate to its receiver,
                // and the ABI hands that decision to ct_aot_return_value.
                if (value produced = value::undefined();
                    enter_compiled(*this, target, callee, registers_.data() + new_base, in.b, self,
                                   /*constructing*/ true, produced)) {
                    reg(in.a) = produced.is_object_like() ? produced : self;
                    break;
                }
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

} // namespace ctbrowser::script
