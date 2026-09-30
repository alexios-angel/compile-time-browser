#include "Importer.h"
#include "OperatorTables.h"

#include "llvm/ADT/BitVector.h"

#include <ctbrowser/script/type_record.hpp>

#include <initializer_list>
#include <string_view>

namespace ctcompile::js::bytecode_detail {

// WHICH OPCODES LIFT THE FRAME OUT OF THE STACK, from the .def's own column and
// not from a list of two names. `await_value` and `yield_value` are the two
// today; the point of deriving it is that a third would be refused the moment
// it existed rather than falling through to `default:` with a message that
// blames a missing operation.
//
// THE DISTINCTION IS NOT PEDANTRY. Every other refusal in this file means "the
// importer has not learned this yet" and closes when somebody writes a case.
// This one does not: a compiled body is a C++ stack frame and a suspension
// point has to lift it OUT, save its live values, and put it back somewhere
// else later. `coroutine_object` saves a suspended frame by copying its
// REGISTER WINDOW out of the flat register file, and a compiled frame has no
// register window - so there is nothing for a `case` to emit until Phase 14
// decides what a compiled frame suspends INTO. Saying that here is the
// difference between a work item and a design decision.
#define CT_OPCODE(name_, a_kind_, b_kind_, c_kind_, writes_a_, allocates_, may_throw_,             \
                  may_reenter_, is_safepoint_, may_suspend_, resumable_, impl_)                    \
    (may_suspend_) != 0,
constexpr bool opcode_may_suspend[] = {
#include <ctbrowser/script/bytecode_opcodes.def>
};
#undef CT_OPCODE

static_assert(std::size(opcode_may_suspend) == opcode_count,
              "the importer's may_suspend table and `enum class op` disagree");

[[nodiscard]] bool may_suspend(op code) {
    const auto index = static_cast<std::size_t>(code);
    return index < std::size(opcode_may_suspend) && opcode_may_suspend[index];
}

// WHICH OF a, b AND c NAME A REGISTER, from the same .def columns - for the
// globals summary, which follows the global object through registers and reads
// every register operand of an operation it does not list. Whether a is written
// is ctbrowser::script::opcode_writes_a, read out of the same file.
enum class operand {
    reg,
    kidx,
    sidx,
    nidx,
    fidx,
    jump,
    count,
    bx_hi,
    unused
};
struct operand_row {
    operand a, b, c;
};
#define CT_OPCODE(name_, a_kind_, b_kind_, c_kind_, writes_a_, allocates_, may_throw_,             \
                  may_reenter_, is_safepoint_, may_suspend_, resumable_, impl_)                    \
    {operand::a_kind_, operand::b_kind_, operand::c_kind_},
constexpr operand_row opcode_operands[] = {
#include <ctbrowser/script/bytecode_opcodes.def>
};
#undef CT_OPCODE

static_assert(std::size(opcode_operands) == opcode_count,
              "the importer's operand table and `enum class op` disagree");

// WHAT A DROPPED BODY CAN STILL DO TO THE GLOBALS TABLE - Phase 62 1/2-A.
//
// A refused function is not a function that does not run. It runs in the
// interpreter, and every `op::set_global` in it rebinds a name that
// --ctjs-resolve-globals is trying to prove bound exactly once. That pass
// cannot count a store that is not in the module, so it refused EVERY name in
// the program as soon as ONE function was skipped: 101 globals on p5, 72 on
// phaser - and phaser's whole loss came from two functions.
//
// IT DOES NOT HAVE TO GUESS, AND THAT IS THE WHOLE POINT. `op::set_global`
// names its target with `proto.names[in.bx()]`: a static index into the
// function's own name pool, which the compiler decided and which needs no
// lowering, no SSA form and no control flow to read back. The exact set of
// names a body may store survives the refusal even though the body does not,
// so the closed world can refuse those NAMES instead of all of them.
//
// AND THE SUMMARY MUST ANSWER THE SAME QUESTION CLAUSE 5 DOES, from bytecode
// rather than from IR: can this body write the globals table other than
// through a set_global counted above? Three ways, and each is an OPERAND test
// rather than a scan of the pools:
//
//   * `Function` or `eval` read as a global. The program either builds runs a
//     top level of its own. Opaque.
//   * `.constructor` read with a constant key. That is `Function` off any
//     function, and following the value needs the IR this body has none of.
//     Opaque.
//   * the global object read as a global, or as `this`. A write through it
//     binds a global in a spec-conformant engine - but the NAME it binds is
//     `proto.names[in.b]` of the op::set_prop that writes it, which is as
//     static as a set_global's. So those names join `stores` and the body stays
//     bounded. A write through a COMPUTED key is the one that cannot be named,
//     and it is opaque - and so is handing the object on, as an argument, a
//     stored value or a return: the parameter it arrives at is in the IR, the
//     call that fills it is not.
//
// EVERY TEST IS ON AN OPERAND, and two earlier versions of this were not. The
// first asked whether the NAME OR STRING POOLS mentioned `constructor`,
// `window` or `Function` anywhere, which cost p5 all 101 of its globals over a
// body that merely contained the word. The second still refused any
// op::get_index in a body whose pool held the string, on the grounds that a
// computed key MIGHT be it - and that was not merely coarse, it was
// INCONSISTENT: the pass's own `constantKey` does not taint a
// ctjs.get_property with a non-constant key either, so a computed `o[k]` is
// invisible on the IR side. Refusing it here bought no soundness that the
// other half of the same clause was not already giving away.
//
// WHAT NEITHER SIDE COVERS, said once and shared: a `constructor` key computed
// at run time - `o[k]` where `k` becomes "constructor" - is invisible to this
// summary exactly as it is invisible to the pass. That is ONE stated limit of
// the closed world rather than two different ones.

[[nodiscard]] dropped_globals summarise_globals(const function_proto & proto) {
    dropped_globals out;
    const auto refuse = [&](std::string why) {
        if (out.opaque.empty()) { out.opaque = std::move(why); }
    };
    const auto name_at = [&](std::uint32_t index) -> const std::string * {
        return index < proto.names.size() ? &proto.names[index] : nullptr;
    };
    const auto missing = [&]() {
        // UNREACHABLE FROM THE COMPILER, and refused rather than ignored: the
        // importer's own cases give up on an out-of-range name index, and a
        // summary that silently dropped one would be the single thing this
        // record exists to prevent.
        refuse("a refused body names an index outside its own name pool, so what it stores "
               "cannot be read back");
    };
    const auto named = [&](std::uint32_t index, std::initializer_list<std::string_view> names) {
        const std::string * name = name_at(index);
        return name != nullptr && llvm::is_contained(names, std::string_view{*name});
    };

    // WHICH REGISTERS MAY HOLD THE GLOBAL OBJECT BEFORE EACH INSTRUCTION:
    // `this`, one of the host's names for it, a self-alias read off one that
    // does (the list hands_back_the_global_object() keeps for the pass), or a
    // copy. A forward dataflow over the bytecode's own edges, because the
    // compiler reuses a statement's temporaries - `this.f = g; yield 0;` puts
    // the 0 where `this` was. A handler can be entered from anywhere in its
    // region, so it is given every state.
    const std::size_t length = proto.code.size();
    std::vector<llvm::BitVector> holds(length, llvm::BitVector(proto.frame_size));
    const auto transfer = [&](std::size_t at) {
        const instruction & in = proto.code[at];
        const llvm::BitVector & before = holds[at];
        llvm::BitVector after = before;
        const auto held = [&](std::uint32_t slot) { return slot < before.size() && before[slot]; };
        const auto set = [&](std::uint32_t slot, bool value) {
            if (slot < after.size()) { after[slot] = value; }
        };
        switch (in.code) {
        case op::load_this: set(in.a, true); break;
        case op::get_global:
        case op::get_global_typeof:
            set(in.a, named(in.bx(), {"globalThis", "window", "self"}));
            break;
        case op::move:
        case op::get_index: set(in.a, held(in.b)); break;
        case op::get_prop:
            set(in.a, held(in.b) &&
                          named(in.c, {"window", "globalThis", "self", "parent", "top", "frames"}));
            break;
        default:
            if (opcode_writes_a(in.code)) { set(in.a, false); }
            break;
        }
        return after;
    };
    std::vector<std::size_t> handlers;
    for (std::size_t at = 0; at < length; ++at) {
        const std::int64_t target = jump_target(at, proto.code[at]);
        if (proto.code[at].code == op::push_handler && target >= 0 &&
            static_cast<std::size_t>(target) < length) {
            handlers.push_back(static_cast<std::size_t>(target));
        }
    }
    for (bool grew = true; grew;) {
        grew = false;
        const auto flow = [&](std::int64_t to, const llvm::BitVector & state) {
            if (to < 0 || static_cast<std::size_t>(to) >= length) { return; }
            llvm::BitVector & into = holds[static_cast<std::size_t>(to)];
            if (state.test(into)) {
                into |= state;
                grew = true;
            }
        };
        llvm::BitVector anywhere(proto.frame_size);
        for (std::size_t at = 0; at < length; ++at) {
            const llvm::BitVector after = transfer(at);
            anywhere |= after;
            for (const std::int64_t to : successors(at, proto.code[at])) { flow(to, after); }
        }
        for (const std::size_t handler : handlers) {
            flow(static_cast<std::int64_t>(handler), anywhere);
        }
    }

    // AND WHERE IT GOES FROM HERE. A body the pass never imported can hand the
    // object to a function the pass did import - as an argument, a stored
    // value, a return, a yield or a throw - and nothing in the IR marks the
    // parameter it arrives at. Read through, compared, converted or written
    // through by name, it stays here; any other register use hands it on.
    const auto hands_on = [&](const instruction & in, const llvm::BitVector & before) {
        const auto held = [&](std::uint32_t slot) { return slot < before.size() && before[slot]; };
        const auto window = [&](std::uint32_t first, std::uint32_t count) {
            for (std::uint32_t slot = first; slot <= first + count; ++slot) {
                if (held(slot)) { return true; }
            }
            return false;
        };
        // A PRIMITIVE COMES OUT of every operator the importer lowers to
        // ctjs.binary, ctjs.compare or ctjs.unary - the IR walk's rule too.
        const auto lowers_through = [&](const auto & rows) {
            return llvm::any_of(rows, [&](const auto & row) { return row.code == in.code; });
        };
        if (lowers_through(binary_rows) || lowers_through(compare_rows) ||
            lowers_through(unary_rows)) {
            return false;
        }
        switch (in.code) {
        case op::move:
        case op::get_prop:
        case op::get_index:
        case op::get_proto:
        case op::own_keys:
        case op::has_property:
        case op::instance_of:
        case op::jump_if_false:
        case op::jump_if_true:
        case op::jump_if_not_nullish:
        case op::jump_if_defined:
        case op::delete_prop:
        case op::delete_index: return false;
        // WRITTEN THROUGH, which the summary reads by name; the VALUE written
        // is what would leave.
        case op::set_prop:
        case op::set_index:
        case op::define_data:
        case op::define_getter:
        case op::define_setter: return held(in.c);
        // THE CALLEE OR RECEIVER IN a, THE ARGUMENTS ABOVE IT.
        case op::call:
        case op::call_method:
        case op::construct: return window(in.a, in.b);
        case op::call_computed:
        case op::call_receiver: return window(in.a, in.b) || held(in.c);
        // a READ AND WRITTEN IN PLACE: boxed into a cell, wrapped in a
        // promise, exported.
        case op::new_cell:
        case op::wrap_promise:
        case op::bind_export: return held(in.a);
        default: break;
        }
        const operand_row & row = opcode_operands[static_cast<std::size_t>(in.code)];
        return (row.a == operand::reg && !opcode_writes_a(in.code) && held(in.a)) ||
               (row.b == operand::reg && held(in.b)) || (row.c == operand::reg && held(in.c));
    };

    for (std::size_t at = 0; at < length; ++at) {
        const instruction & in = proto.code[at];
        const bool through = in.a < holds[at].size() && holds[at][in.a];
        switch (in.code) {
        case op::set_global: {
            const std::string * name = name_at(in.bx());
            if (name == nullptr) {
                missing();
                break;
            }
            out.stores.push_back(*name);
            break;
        }
        case op::get_global:
        case op::get_global_typeof: {
            const std::string * name = name_at(in.bx());
            if (name == nullptr) {
                missing();
                break;
            }
            if (*name == "Function" || *name == "eval") {
                refuse("a refused body reads `" + *name +
                       "`, and the program it compiles can store any global");
            }
            break;
        }
        case op::get_prop: {
            const std::string * name = name_at(in.c);
            if (name != nullptr && *name == "constructor") {
                refuse("a refused body reads `.constructor`, which is the run-time compiler on "
                       "any function, and this pass cannot follow a value it never imported");
            }
            break;
        }
        // THE NAMED WRITES THROUGH IT, whose target is as static as a
        // set_global's.
        case op::set_prop:
        case op::define_getter:
        case op::define_setter:
        case op::delete_prop: {
            const std::string * name = name_at(in.b);
            if (name == nullptr) {
                missing();
                break;
            }
            if (through) { out.stores.push_back(*name); }
            break;
        }
        // AND THE WRITES WHOSE KEY IS NOT. `o[k] = v` and `delete o[k]` name
        // nothing this can read.
        case op::set_index:
        case op::delete_index:
            if (through) {
                refuse("a refused body holds the global object and writes a property this pass "
                       "cannot name, which in a spec-conformant engine binds a global");
            }
            break;
        case op::dyn_import:
            refuse("a refused body loads a module this compile cannot see, and its top level "
                   "can store any global");
            break;
        default: break;
        }
        if (hands_on(in, holds[at])) {
            refuse("a refused body holds the global object and hands it on (" +
                   std::string{ctjs::opcode_name(in.code)} +
                   "), where this summary cannot follow it");
        }
    }

    llvm::sort(out.stores);
    out.stores.erase(std::unique(out.stores.begin(), out.stores.end()), out.stores.end());
    return out;
}

// WHAT A FUNCTION'S ENTRY BLOCK RECEIVES, in order, before its declared
// parameters. `this`, `new.target` and the callee are frame properties the
// bytecode reads with their own opcodes, and passing them as arguments makes
// those three opcodes free.

// WHERE A JUMP GOES. `ip` is post-incremented at fetch, so a displacement is
// relative to the instruction AFTER this one - which is why the encoder
// subtracts one when it patches. Getting this wrong by one produces a program
// that verifies and branches to the wrong place.
[[nodiscard]] std::int64_t jump_target(std::size_t at, const instruction & in) {
    return static_cast<std::int64_t>(at) + 1 + in.sbx();
}

// EVERY CONDITIONAL JUMP THE VM HAS, and the list is exhaustive on purpose.
//
// It named two of the four, and the other two were not unimplemented - their
// emission was written, correct, and unreachable. This predicate is what marks
// branch targets as block LEADERS, so an opcode missing from it gets no block
// at its target, no block at its fallthrough, and its own emitter then refuses
// with "branch target is not a block leader" - a message that reads like a
// malformed program rather than a classifier that has not heard of the opcode.
// It cost 12 of Bootstrap's functions, every one of them for `??` or `?.`.
[[nodiscard]] bool is_conditional_jump(op code) {
    return code == op::jump_if_false || code == op::jump_if_true || code == op::jump_if_defined ||
           code == op::jump_if_not_nullish;
}

[[nodiscard]] bool is_jump(op code) {
    return code == op::jump || is_conditional_jump(code);
}

[[nodiscard]] bool ends_a_block(op code) {
    return is_jump(code) || code == op::ret || code == op::ret_undef || code == op::halt ||
           code == op::throw_value || code == op::push_handler;
}

// WHETHER CONTROL CAN RUN OFF THIS INSTRUCTION INTO THE NEXT. Not the negation
// of ends_a_block: a CONDITIONAL jump ends a block and still falls through, and
// push_handler ends a block and its body IS the next instruction.
[[nodiscard]] bool falls_through(op code) {
    return code != op::jump && code != op::ret && code != op::ret_undef && code != op::halt &&
           code != op::throw_value;
}

// WHERE CONTROL CAN GO NEXT WITHOUT THROWING: the jump's target, then the
// instruction after. Neither is range-checked - a caller decides what a target
// outside the code means. A push_handler's pad is not one of them: it is
// entered by throwing, and the callers that care about it say so themselves.
[[nodiscard]] llvm::SmallVector<std::int64_t, 2> successors(std::size_t at,
                                                            const instruction & in) {
    llvm::SmallVector<std::int64_t, 2> next;
    if (is_jump(in.code)) { next.push_back(jump_target(at, in)); }
    if (falls_through(in.code)) { next.push_back(static_cast<std::int64_t>(at) + 1); }
    return next;
}

} // namespace ctcompile::js::bytecode_detail
