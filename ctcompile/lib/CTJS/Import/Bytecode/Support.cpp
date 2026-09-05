#include "Importer.h"

namespace ctcompile::js::bytecode_detail {

// EVERY OPCODE'S NAME, expanded from the same .def the VM decodes with. The
// plan asks for exactly this - "the importer expands it into a static dispatch
// table" - and the point is that a table built any other way is a second list
// that can disagree with the first.
#define CT_OPCODE(name_, ...) #name_,
constexpr std::string_view opcode_names[] = {
#include <ctbrowser/script/bytecode_opcodes.def>
};
#undef CT_OPCODE

static_assert(std::size(opcode_names) == opcode_count,
              "the importer's opcode-name table and `enum class op` disagree");

[[nodiscard]] std::string_view name_of(op code) {
    const auto index = static_cast<std::size_t>(code);
    return index < std::size(opcode_names) ? opcode_names[index] : "<unknown>";
}

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
//   * the global object read as a global. A write through it binds a global in
//     a spec-conformant engine - but the NAME it binds is `proto.names[in.b]`
//     of the op::set_prop that writes it, which is as static as a set_global's.
//     So those names join `stores` and the body stays bounded. A write through
//     a COMPUTED key is the one that cannot be named, and it is opaque.
//
// EVERY TEST IS ON AN OPERAND, and two earlier versions of this were not. The
// first asked whether the NAME OR STRING POOLS mentioned `constructor`,
// `window` or `Function` anywhere, which cost p5 all 101 of its globals over a
// body that merely contained the word. The second still refused any
// op::get_index in a body whose pool held the string, on the grounds that a
// computed key MIGHT be it - and that was not merely coarse, it was
// INCONSISTENT: the pass's own `constant_key` does not taint a
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

    bool reads_global_object = false;
    bool writes_a_computed_key = false;
    std::vector<std::string> through_the_global_object;

    for (const instruction & in : proto.code) {
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
        case op::get_global: {
            const std::string * name = name_at(in.bx());
            if (name == nullptr) {
                missing();
                break;
            }
            if (*name == "Function" || *name == "eval") {
                refuse("a refused body reads `" + *name +
                       "`, and the program it compiles can store any global");
            } else if (*name == "globalThis" || *name == "window" || *name == "self") {
                reads_global_object = true;
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
        // THE NAMED WRITES, whose target is as static as a set_global's. They
        // matter only if this body also holds the global object.
        case op::set_prop:
        case op::define_getter:
        case op::define_setter:
        case op::delete_prop: {
            const std::string * name = name_at(in.b);
            if (name == nullptr) {
                missing();
                break;
            }
            through_the_global_object.push_back(*name);
            break;
        }
        // AND THE WRITES WHOSE TARGET IS NOT. `o[k] = v`, `delete o[k]`,
        // `{...o}` and a prototype swap all name nothing this can read.
        case op::set_index:
        case op::delete_index:
        case op::copy_props:
        case op::set_proto: writes_a_computed_key = true; break;
        case op::dyn_import:
            refuse("a refused body loads a module this compile cannot see, and its top level "
                   "can store any global");
            break;
        default: break;
        }
    }

    if (reads_global_object) {
        if (writes_a_computed_key) {
            refuse("a refused body holds the global object and writes a property this pass "
                   "cannot name, which in a spec-conformant engine binds a global");
        } else {
            out.stores.insert(out.stores.end(), through_the_global_object.begin(),
                              through_the_global_object.end());
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

// AN INSTRUCTION THAT ALREADY LEFT THE BLOCK cannot be followed by a check -
// the block has a terminator and a second one does not verify. push_handler is
// here for the same reason it is in ends_a_block: it IS a terminator.
// WHETHER CONTROL CAN RUN OFF THIS INSTRUCTION INTO THE NEXT. Not the negation
// of ends_a_block: a CONDITIONAL jump ends a block and still falls through, and
// push_handler ends a block and its body IS the next instruction.
[[nodiscard]] bool falls_through(op code) {
    return code != op::jump && code != op::ret && code != op::ret_undef && code != op::halt &&
           code != op::throw_value;
}

[[nodiscard]] bool in_terminator(op code) {
    return ends_a_block(code) || code == op::halt;
}

} // namespace ctcompile::js::bytecode_detail
