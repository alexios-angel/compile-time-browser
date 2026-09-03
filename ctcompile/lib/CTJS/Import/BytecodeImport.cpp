#include <ctcompile/CTJS/Import/BytecodeImport.hpp>

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"

#include <ctbrowser/script/bytecode.hpp>
#include <ctbrowser/script/source_lines.hpp>

#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Verifier.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <iterator>
#include <optional>
#include <string_view>

// THE IMPORTER.
//
// THE REGISTER FILE IS THE BLOCK ARGUMENT VECTOR, and that decision is the
// whole shape of this file. Every block other than the entry takes exactly
// `frame_size` arguments of !ctjs.value; a side table maps slot index to the
// current SSA value; every branch passes the whole vector as successor
// operands.
//
// THAT IS NOT SSA CONSTRUCTION, which the plan forbids here: there are no
// dominance frontiers, no phi minimisation and no backpatching of incomplete
// blocks. Blocks are created with their full argument list before anything is
// emitted, so successor operands are always known at the branch site - including
// on a back edge. Pruning the vector down to the arguments that are actually
// live is a later cleanup pass, which is exactly the plan's "later transition
// toward SSA/block arguments".
//
// The dialect was already built for it: ctjs.push_handler carries
// $bodyOperands AND $handlerOperands and implements BranchOpInterface, which
// only makes sense if a block carries a register vector.
namespace ctcompile::js {

namespace {

using namespace ctbrowser::script;

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
struct dropped_globals {
    std::vector<std::string> stores;
    std::string opaque;
};

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
constexpr unsigned implicit_arguments = 3;
constexpr unsigned arg_receiver = 0;
constexpr unsigned arg_new_target = 1;
constexpr unsigned arg_callee = 2;

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

// One function's worth of state.
struct function_importer {
    mlir::OpBuilder & builder;
    mlir::MLIRContext * context;
    const program & prog;
    const function_proto & proto;
    llvm::StringRef program_id;
    std::uint32_t function_index;
    std::vector<unsupported_opcode> & skipped;

    // slot -> the value currently in it
    llvm::SmallVector<mlir::Value> registers{};

    // AND EVERY SLOT EACH VALUE HAS EVER BEEN IN - Phase 54A's half of the
    // join with the Phase 54B oracle, which keys its observations by register
    // rather than by SSA value. See `register_map` in the header for why the
    // mapped type is a list and why this cannot be recovered afterwards.
    llvm::MapVector<mlir::Value, llvm::SmallVector<std::uint16_t, 1>> occupied{};
    llvm::DenseMap<std::int64_t, mlir::Block *> blocks{};
    mlir::Value frame{};
    bool gave_up = false;

    // THE PROGRAM'S LINE TABLE, built once and borrowed. Null when the source
    // was dropped from the image or the debug tables were compiled out.
    const ctbrowser::script::line_table * lines = nullptr;

    [[nodiscard]] mlir::Location location_for(std::size_t at) const {
        // FUSED, WHICH THE PHASE 7 CONVENTION ASKS FOR: a name that identifies
        // the instruction inside its program, and the source position it came
        // from. "Retrofitting locations later is far more expensive."
        const std::string name = "program:" + program_id.str() + ":" +
                                 std::to_string(function_index) + ":" + std::to_string(at);
        mlir::Location where = mlir::NameLoc::get(mlir::StringAttr::get(context, name));

        // THIS INSTRUCTION'S OWN LINE, when the program carries the table.
        //
        // It was the FUNCTION's span on every instruction in the function -
        // source_begin and source_end, identical for all of them - which is a
        // location that cannot tell two statements apart and is worth nothing
        // to a debugger. code_offsets is per instruction and line_table turns
        // one into a line and a column.
        //
        // THE OLD SPAN IS THE FALLBACK RATHER THAN A REGRESSION: code_offsets
        // is empty when the debug tables were compiled out or the image
        // dropped its source, and a per-function span still beats nothing.
        // THE FILE SLOT NAMES THE SOURCE the program came from - the program
        // id, which ctjs-translate sets to its input's buffer identifier - not
        // the function: a Stage 53F pin, a resolve-globals diagnostic and a
        // debugger all want `file.js:line:col`, and the function is already
        // named by the symbol the op sits in.
        const mlir::StringAttr file = mlir::StringAttr::get(context, program_id);
        if (lines != nullptr && at < proto.code_offsets.size()) {
            const std::uint32_t offset = proto.code_offsets[at];
            return mlir::FusedLoc::get(
                context, {where, mlir::FileLineColLoc::get(file, lines->line_of(offset),
                                                           lines->column_of(offset))});
        }
        return mlir::FusedLoc::get(
            context,
            {where, mlir::FileLineColLoc::get(file, proto.source_begin, proto.source_end)});
    }

    void give_up(std::size_t at, op code, std::string reason) {
        if (gave_up) { return; }
        gave_up = true;
        // THE GLOBALS SUMMARY IS TAKEN HERE, where the bytecode is still in
        // hand. Everything below this line abandons the IR; `proto` is the
        // program's, outlives the scratch module, and is the only description
        // of this function that survives the refusal.
        dropped_globals globals = summarise_globals(proto);
        skipped.push_back(unsupported_opcode{program_id.str(), function_index,
                                             static_cast<std::uint32_t>(at),
                                             std::string{name_of(code)}, std::move(reason),
                                             std::move(globals.stores), std::move(globals.opaque)});
    }

    [[nodiscard]] mlir::Value constant(mlir::Location where, mlir::Attribute value) {
        return ctjs::ConstantOp::create(builder, where, ctjs::ValueType::get(context), value);
    }

    [[nodiscard]] mlir::Value undefined(mlir::Location where) {
        return constant(where, ctjs::UndefinedAttr::get(context));
    }

    // THE HANDLERS OPEN AT THIS POINT IN THE WALK, innermost last.
    //
    // A `pad` is the block the unwinder transfers to and `slot` is the register
    // the thrown value lands in - which is bytecode's choice, not ours:
    // context::unwind_to_handler writes registers_[base + slot] and nothing
    // else, so the compiled tier has to put it in the same place.
    struct open_handler {
        mlir::Block * pad;
        std::uint16_t slot;
    };
    llvm::SmallVector<open_handler> handlers{};

    // THE ONE PLACE A REGISTER IS WRITTEN. Every assignment into `registers`
    // goes through here so that `occupied` cannot fall behind it - there were
    // five such sites when this was added (the entry seeding, the two
    // block-argument rebinds, the catch pad's thrown value and the `set`
    // lambda every opcode result funnels through), and a sixth written as a
    // bare `registers[slot] = ...` would silently drop a value from the map.
    //
    // THE BOUNDS TEST LIVES HERE TOO, because two of those five sites carried
    // their own copy of it - one of them the `set` lambda, whose callers pass a
    // slot straight out of an instruction operand and do not check it.
    void write(std::size_t slot, mlir::Value v) {
        if (slot >= registers.size()) { return; }
        registers[slot] = v;
        if (!v) { return; }
        // LINEAR, AND DELIBERATELY SO. The list is one element for almost
        // every value - a value in two slots is a `mov` alias, a value in
        // three is rare - so a set would cost more than the scan it saves.
        llvm::SmallVector<std::uint16_t, 1> & slots = occupied[v];
        const auto narrowed = static_cast<std::uint16_t>(slot);
        if (!llvm::is_contained(slots, narrowed)) { slots.push_back(narrowed); }
    }

    // The register vector as successor operands - the whole file, every time.
    [[nodiscard]] llvm::SmallVector<mlir::Value> outgoing() const {
        return llvm::SmallVector<mlir::Value>{registers.begin(), registers.end()};
    }
};

// THE OPCODES THIS CUT UNDERSTANDS, as a table rather than a switch scattered
// through the walk. A binary opcode that is not here is not silently wrong: it
// reaches the default arm and abandons the function.
struct binary_row {
    op code;
    ctjs::BinaryKind kind;
    bool re_entering;
};

constexpr binary_row binary_rows[] = {
    // THE STATIC FAMILY, which cannot run user code - to_number and to_int32
    // rather than to_number_value. A backend that proves both operands are
    // numbers may drop the call, the exception edge and the safepoint.
    {op::add, ctjs::BinaryKind::Add, false},
    {op::bit_and, ctjs::BinaryKind::BitAnd, false},
    {op::bit_or, ctjs::BinaryKind::BitOr, false},
    {op::bit_xor, ctjs::BinaryKind::BitXor, false},
    {op::shl, ctjs::BinaryKind::Shl, false},
    {op::shr, ctjs::BinaryKind::Shr, false},
    {op::ushr, ctjs::BinaryKind::UShr, false},
    // AND THE RE-ENTERING ONE. `add_generic` is what source `+` compiles to;
    // `op::add` comes only from `++` and three internal counters.
    {op::sub, ctjs::BinaryKind::Sub, true},
    {op::mul, ctjs::BinaryKind::Mul, true},
    {op::div, ctjs::BinaryKind::Div, true},
    {op::mod, ctjs::BinaryKind::Mod, true},
    {op::pow, ctjs::BinaryKind::Pow, true},
    {op::add_generic, ctjs::BinaryKind::Add, true},
    {op::concat, ctjs::BinaryKind::Concat, true},
};

struct compare_row {
    op code;
    ctjs::CompareKind kind;
    bool negate;
};

constexpr compare_row compare_rows[] = {
    {op::equal, ctjs::CompareKind::StrictEq, false},
    {op::not_equal, ctjs::CompareKind::StrictEq, true},
    {op::loose_equal, ctjs::CompareKind::Eq, false},
    {op::loose_not_equal, ctjs::CompareKind::Eq, true},
    {op::less, ctjs::CompareKind::Lt, false},
    {op::less_equal, ctjs::CompareKind::Le, false},
    {op::greater, ctjs::CompareKind::Gt, false},
    {op::greater_equal, ctjs::CompareKind::Ge, false},
};

struct unary_row {
    op code;
    ctjs::UnaryKind kind;
};

constexpr unary_row unary_rows[] = {
    {op::negate, ctjs::UnaryKind::Neg},
    {op::bit_not, ctjs::UnaryKind::BitNot},
    {op::logical_not, ctjs::UnaryKind::Not},
    {op::type_of, ctjs::UnaryKind::TypeOf},
    // UNARY PLUS, which was the only operation in this dialect that was fully
    // lowered and completely unreachable. ctjs.unary plus emits ct_aot_to_number
    // and boxes the double, operators.mlir has exercised it from hand-written
    // IR since it was written, and the helper has had a body all along - the
    // table simply had no row, so no JavaScript could ever produce it.
    {op::to_number, ctjs::UnaryKind::Plus},
};

} // namespace

import_result import_program(const program & from, llvm::StringRef program_id,
                             mlir::MLIRContext * context) {
    import_result out;
    // ONE LINE TABLE FOR THE WHOLE PROGRAM, built once. Every function's
    // instruction offsets index the same source text, so building it per
    // function would scan that text once per function.
    //
    // EMPTY SOURCE MEANS NO TABLE, not an empty one: an image that dropped its
    // source also dropped code_offsets, because an offset into text nobody has
    // cannot be turned into a line.
    const ctbrowser::script::line_table lines{from.source};
    const ctbrowser::script::line_table * const lines_or_null =
        from.source.empty() ? nullptr : &lines;
    // LOADED, NOT MERELY REGISTERED. A DialectRegistry says a dialect MAY be
    // used; `Type::get` and `Op::create` need it actually loaded into the
    // context, and mlir-translate's harness only registers. Without this the
    // first ctjs::ValueType::get crashes with a stack trace that names the
    // importer and says nothing about dialect loading.
    context->getOrLoadDialect<ctjs::CTJSDialect>();
    context->getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    mlir::OpBuilder builder(context);
    out.module = mlir::ModuleOp::create(builder.getUnknownLoc());

    for (std::size_t index = 0; index < from.functions.size(); ++index) {
        const function_proto & proto = from.functions[index];
        std::vector<unsupported_opcode> skipped;

        // A FUNCTION IS BUILT INTO A THROWAWAY MODULE FIRST and only adopted if
        // it survives. Emitting into the real module and erasing on failure
        // would leave whatever the walk had already created if it gave up in
        // the middle, which is precisely the partial translation the plan
        // forbids.
        mlir::OwningOpRef<mlir::ModuleOp> scratch = mlir::ModuleOp::create(builder.getUnknownLoc());
        mlir::OpBuilder into(scratch->getBodyRegion());

        const auto value_type = ctjs::ValueType::get(context);
        llvm::SmallVector<mlir::Type> inputs(implicit_arguments + proto.param_count, value_type);
        const auto signature = into.getFunctionType(inputs, {value_type});

        // THE INDEX IS PART OF THE NAME, ALWAYS. ctjs.func is a Symbol, and a real
        // program has many functions sharing one name - p5.js has dozens called
        // `constructor` and dozens more anonymous. Without the suffix the module
        // fails to verify on a duplicate symbol, which surfaces as
        // ctjs-translate producing NO OUTPUT AT ALL for a 4,000-function file
        // while every individual function verified perfectly well.
        //
        // The source name stays in front of it, because a trace that says
        // @createCanvas$412 is worth reading and @fn412 is not.
        std::string name = proto.name.empty() ? std::string{"fn"} : proto.name;
        for (char & c : name) {
            if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') { c = '_'; }
        }
        name += "$" + std::to_string(index);
        auto function = ctjs::FuncOp::create(
            into, into.getUnknownLoc(), into.getStringAttr(name), mlir::TypeAttr::get(signature),
            into.getI32IntegerAttr(static_cast<std::int32_t>(proto.upvalues.size())),
            /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);

        mlir::Block * entry = &function.getBody().emplaceBlock();
        entry->addArguments(inputs,
                            llvm::SmallVector<mlir::Location>(inputs.size(), into.getUnknownLoc()));

        function_importer state{
            into, context, from, proto, program_id, static_cast<std::uint32_t>(index), skipped};
        state.lines = lines_or_null;
        into.setInsertionPointToStart(entry);

        // THE FRAME, in the entry block and nowhere else. The entry dominates
        // every block in the region, so the context needs no threading - which
        // is fortunate, because push_handler's operands are !ctjs.value and
        // could not carry a !ctjs.context.
        //
        // AND IT CARRIES THE REGISTER WINDOW. proto.frame_size is read three
        // lines below to size this importer's own register file, and until now
        // it was dropped afterwards - so ct_aot_enter, which needs exactly this
        // number, had nowhere to get it. A backend guessing param_count would
        // size the window to the parameters and leave every local unslotted.
        state.frame = ctjs::FrameEnterOp::create(
            into, state.location_for(0), ctjs::ContextType::get(context),
            into.getI32IntegerAttr(static_cast<std::int32_t>(proto.frame_size)));

        // Seed the register file: parameters where the callee expects them,
        // undefined everywhere else.
        state.registers.assign(proto.frame_size, mlir::Value{});
        for (std::size_t slot = 0; slot < proto.frame_size; ++slot) {
            state.write(slot,
                        slot < proto.param_count
                            ? entry->getArgument(static_cast<unsigned>(implicit_arguments + slot))
                            : state.undefined(state.location_for(0)));
        }

        // ---- leaders ------------------------------------------------------
        std::vector<bool> leader(proto.code.size() + 1, false);
        bool targets_zero = false;
        if (!proto.code.empty()) { leader[0] = true; }
        bool malformed = false;
        for (std::size_t at = 0; at < proto.code.size(); ++at) {
            const instruction & in = proto.code[at];
            if (is_jump(in.code) || in.code == op::push_handler) {
                const std::int64_t target = jump_target(at, in);
                if (target < 0 || target > static_cast<std::int64_t>(proto.code.size())) {
                    malformed = true;
                    break;
                }
                leader[static_cast<std::size_t>(target)] = true;
                // AND WHETHER ANYTHING BRANCHES BACK TO INSTRUCTION ZERO,
                // which needs recording separately because leader[0] is true
                // for every function whether or not anything targets it.
                //
                // INSIDE THE COMBINED PREDICATE ON PURPOSE, not under is_jump
                // alone. op::push_handler computes its target the same way and
                // is handled here for that reason; narrowing this to jumps
                // would leave a handler whose pad is instruction 0 refusing
                // forever, and no corpus would show it - there are zero
                // push_handler targets of 0 across bootstrap, p5 and phaser.
                if (target == 0) { targets_zero = true; }
            }
            if (ends_a_block(in.code) && at + 1 <= proto.code.size()) { leader[at + 1] = true; }
        }
        if (malformed) {
            // THE THIRD REFUSAL SITE, AND THE ONLY ONE THAT IS DELIBERATELY
            // OPAQUE. The other two drop a function whose instruction stream is
            // well formed, so reading its op::set_globals back off that stream
            // is exact. This one drops a function whose CONTROL FLOW is not: a
            // jump leaves the code array, which means the program is corrupt
            // and nothing in it should be reasoned about instruction by
            // instruction. Precision here would buy nothing and would be the
            // one summary whose premise is already false.
            skipped.push_back(unsupported_opcode{
                program_id.str(),
                static_cast<std::uint32_t>(index),
                0,
                "jump",
                "a jump leaves the function's bytecode",
                {},
                "a refused body's jump leaves its own bytecode, so its instruction stream cannot "
                "be read back for the globals it stores"});
            out.skipped.insert(out.skipped.end(), skipped.begin(), skipped.end());
            continue;
        }

        // One block per leader, each carrying the whole register file.
        llvm::SmallVector<mlir::Type> slot_types(proto.frame_size, value_type);
        llvm::SmallVector<mlir::Location> slot_locs(proto.frame_size, into.getUnknownLoc());
        for (std::size_t at = targets_zero ? 0 : 1; at < proto.code.size(); ++at) {
            // STRICTLY INSIDE THE CODE. `ret` marks its successor a leader, and
            // for the last instruction that successor is one past the end - a
            // block no instruction would ever fill, which then needs a
            // terminator invented for it and shows up as an unreachable stub in
            // every imported function.
            //
            // AND INSTRUCTION ZERO GETS ONE ONLY WHEN SOMETHING BRANCHES BACK
            // TO IT. This loop started at 1, and the exclusion was never
            // deliberate - the comment above explains the UPPER bound and says
            // nothing about the lower one. leader[0] is set unconditionally, so
            // a function whose FIRST statement is a loop marked index 0 a
            // leader, got no block for it, and was refused whole with "jump
            // target is not a block leader".
            //
            // It needs the flag rather than leader[0] because leader[0] is true
            // for every function; without something actually targeting zero
            // this would put a pointless header block in front of every
            // imported body. Six functions across the three vendored corpora
            // have the shape - a loop with no prologue ahead of it, like
            // `while (a.length > n) a.pop();` as the first statement.
            //
            // THE ENTRY BLOCK CANNOT SIMPLY BE THE TARGET: MLIR forbids
            // predecessors on a FunctionOpInterface entry block, and its
            // arguments are the ABI's rather than the register file. So index 0
            // becomes a real header the entry falls through into, which is the
            // shape Import/loop-property.mlir already asserts for loops that
            // start one instruction later.
            if (!leader[at]) { continue; }
            mlir::Block * block = &function.getBody().emplaceBlock();
            block->addArguments(slot_types, slot_locs);
            state.blocks[static_cast<std::int64_t>(at)] = block;
        }

        // ---- the walk -----------------------------------------------------
        const auto block_at = [&](std::int64_t target) -> mlir::Block * {
            const auto found = state.blocks.find(target);
            return found == state.blocks.end() ? nullptr : found->second;
        };
        const auto enter_block = [&](std::size_t at) {
            mlir::Block * block = block_at(static_cast<std::int64_t>(at));
            if (block == nullptr) { return; }
            // THE FALL-THROUGH EDGE, WHICH IS NOT IMPLICIT IN AN MLIR CFG.
            //
            // Bytecode runs off the end of one instruction into the next; a
            // block does not run off its end into the block below it. Without
            // this branch the entry block of every loop simply ended, the
            // terminator pass below gave it `return undefined`, and the loop
            // header was reachable only from its own back edge - a function
            // that verifies, prints plausibly, and returns undefined.
            mlir::Block * previous = into.getInsertionBlock();
            if (previous != nullptr &&
                (previous->empty() || !previous->back().hasTrait<mlir::OpTrait::IsTerminator>())) {
                into.setInsertionPointToEnd(previous);
                mlir::cf::BranchOp::create(into, into.getUnknownLoc(), block, state.outgoing());
            }
            into.setInsertionPointToEnd(block);
            for (std::size_t slot = 0; slot < proto.frame_size; ++slot) {
                state.write(slot, block->getArgument(static_cast<unsigned>(slot)));
            }
        };

        // WHICH REGISTER EACH PAD'S THROWN VALUE LANDS IN, collected before the
        // walk because a pad block is reached from the CFG rather than from the
        // instruction that named it.
        llvm::DenseMap<std::int64_t, std::uint16_t> catch_slot_at;
        for (std::size_t at = 0; at < proto.code.size(); ++at) {
            if (proto.code[at].code == op::push_handler) {
                catch_slot_at[jump_target(at, proto.code[at])] = proto.code[at].a;
            }
        }

        // AND AT MOST ONE PROTECTED REGION PER FUNCTION, FOR NOW.
        //
        // Not because nesting is hard, but because TWO push_handlers is what
        // try/FINALLY compiles to - compile_try_with_finally pushes its second
        // one SEQUENTIALLY rather than nested, so a depth test would admit it -
        // and finally brings a completion record, a rethrow through
        // op::throw_value, and ctjs.resume_throw, which has no lowering. This
        // refuses finally, nested try, and two sibling try blocks alike; the
        // first is the one that would be WRONG rather than merely absent.
        if (!state.gave_up) {
            std::size_t pushes = 0;
            for (const instruction & each : proto.code) {
                if (each.code == op::push_handler) { ++pushes; }
            }
            if (pushes > 1) {
                state.give_up(0, op::push_handler,
                              "more than one protected region in a function, which is what "
                              "try/finally and nested try both compile to");
            }
        }

        // AND A SUSPENSION POINT REFUSES THE FUNCTION WITH ITS OWN REASON.
        //
        // BEFORE THE WALK RATHER THAN AS A `case`, for two reasons that both
        // matter. First, a `case` label naming a suspending opcode is what
        // ctcompile_importer_coverage reads as "the importer DISPATCHES this",
        // and a refusal is not a dispatch - the ratchet would stop measuring
        // anything. That test's own sanity check caught the first draft of THIS
        // COMMENT, which spelled the label out and was therefore matched as
        // one; it is deliberately paraphrased now, and the guard is load-
        // bearing rather than decorative. Second, the reason belongs to the
        // FUNCTION, not to the instruction: nothing in a body containing an
        // `await` is compilable, including the parts before it.
        //
        // It reports the FIRST suspension point rather than instruction 0, so
        // the offset in the diagnostic names something a person can go and
        // read.
        if (!state.gave_up) {
            for (std::size_t at = 0; at < proto.code.size(); ++at) {
                if (!may_suspend(proto.code[at].code)) { continue; }
                state.give_up(at, proto.code[at].code,
                              "a suspension point, which lifts the frame out of the register "
                              "stack and puts it back later - a compiled body is a C++ stack "
                              "frame with no register window to save, so this is Phase 14's "
                              "design decision and not a missing importer case");
                break;
            }
        }

        for (std::size_t at = 0; at < proto.code.size() && !state.gave_up; ++at) {
            if (leader[at] && (at > 0 || targets_zero)) { enter_block(at); }
            // A PAD BLOCK CLOSES THE REGION IT LANDS FROM. The bytecode's
            // pop_handler runs on the NORMAL path only, so a throw leaves the
            // handler stack as the interpreter's unwinder left it - popped.
            if (!state.handlers.empty() &&
                state.handlers.back().pad == block_at(static_cast<std::int64_t>(at))) {
                state.handlers.pop_back();
            }
            // A PAD BLOCK TAKES ITS THROWN VALUE FROM THE RUNTIME, not from a
            // predecessor. ctjs.catch_land is the block's first operation and
            // its result replaces the block argument for the catch register -
            // that argument stays dead on purpose.
            if (const auto landed = catch_slot_at.find(static_cast<std::int64_t>(at));
                landed != catch_slot_at.end() &&
                block_at(static_cast<std::int64_t>(at)) != nullptr) {
                auto land = ctjs::CatchLandOp::create(into, state.location_for(at),
                                                      into.getI32Type(), value_type);
                state.write(landed->second, land.getThrown());
            }
            const instruction & in = proto.code[at];
            const mlir::Location where = state.location_for(at);
            const auto reg = [&](std::uint16_t slot) -> mlir::Value {
                return slot < state.registers.size() ? state.registers[slot] : mlir::Value{};
            };
            const auto set = [&](std::uint16_t slot, mlir::Value v) { state.write(slot, v); };

            // THE REGISTER FILE AS OF THE THROW, SNAPSHOT BEFORE THE
            // INSTRUCTION RUNS. It is what the handler block will be given, and
            // taking it AFTER would name this instruction's own result in a
            // block this instruction does not dominate - which is a verifier
            // crash rather than a wrong answer, and only because MLIR checks.
            const llvm::SmallVector<mlir::Value> before_instruction =
                state.handlers.empty() ? llvm::SmallVector<mlir::Value>{} : state.outgoing();

            bool handled = false;
            for (const binary_row & row : binary_rows) {
                if (row.code != in.code) { continue; }
                mlir::Value made =
                    row.re_entering
                        ? ctjs::BinaryOp::create(into, where, value_type,
                                                 ctjs::BinaryKindAttr::get(context, row.kind),
                                                 reg(in.b), reg(in.c))
                              .getResult()
                        : ctjs::BinaryStaticOp::create(into, where, value_type,
                                                       ctjs::BinaryKindAttr::get(context, row.kind),
                                                       reg(in.b), reg(in.c))
                              .getResult();
                set(in.a, made);
                handled = true;
                break;
            }
            if (handled) { continue; }
            for (const compare_row & row : compare_rows) {
                if (row.code != in.code) { continue; }
                mlir::Value made = ctjs::CompareOp::create(
                    into, where, value_type, ctjs::CompareKindAttr::get(context, row.kind),
                    reg(in.b), reg(in.c));
                if (row.negate) {
                    made = ctjs::UnaryOp::create(
                        into, where, value_type,
                        ctjs::UnaryKindAttr::get(context, ctjs::UnaryKind::Not), made);
                }
                set(in.a, made);
                handled = true;
                break;
            }
            if (handled) { continue; }
            for (const unary_row & row : unary_rows) {
                if (row.code != in.code) { continue; }
                set(in.a,
                    ctjs::UnaryOp::create(into, where, value_type,
                                          ctjs::UnaryKindAttr::get(context, row.kind), reg(in.b)));
                handled = true;
                break;
            }
            if (handled) { continue; }

            switch (in.code) {
            case op::load_undef: set(in.a, state.undefined(where)); break;
            case op::load_null:
                set(in.a, state.constant(where, ctjs::NullAttr::get(context)));
                break;
            case op::load_true:
                set(in.a, state.constant(where, ctjs::BooleanAttr::get(context, true)));
                break;
            case op::load_false:
                set(in.a, state.constant(where, ctjs::BooleanAttr::get(context, false)));
                break;
            case op::load_string:
                if (in.bx() >= proto.strings.size()) {
                    state.give_up(at, in.code, "string index out of range");
                    break;
                }
                set(in.a,
                    state.constant(where, ctjs::StringAttr::get(context, proto.strings[in.bx()])));
                break;
            case op::load_const: {
                if (in.bx() >= proto.constants.size()) {
                    state.give_up(at, in.code, "constant index out of range");
                    break;
                }
                const value k = proto.constants[in.bx()];
                if (k.is_number()) {
                    set(in.a,
                        state.constant(where,
                                       ctjs::NumberAttr::get(
                                           context, std::bit_cast<std::uint64_t>(k.as_number()))));
                } else if (k.is_boolean()) {
                    set(in.a,
                        state.constant(where, ctjs::BooleanAttr::get(context, k.as_boolean())));
                } else if (k.is_undefined()) {
                    set(in.a, state.undefined(where));
                } else if (k.is_null()) {
                    set(in.a, state.constant(where, ctjs::NullAttr::get(context)));
                } else {
                    // A CONSTANT POOL ENTRY THIS CUT CANNOT NAME. Abandoning is
                    // the whole point: guessing would produce a function that
                    // runs and computes something else.
                    state.give_up(at, in.code,
                                  "constant is not a number, boolean, null or "
                                  "undefined");
                }
                break;
            }
            case op::move: set(in.a, reg(in.b)); break;
            case op::load_this: set(in.a, entry->getArgument(arg_receiver)); break;
            case op::load_new_target: set(in.a, entry->getArgument(arg_new_target)); break;
            case op::load_callee: set(in.a, entry->getArgument(arg_callee)); break;
            // NOT arg_callee AND THEN A LOOKUP. The callee is a block argument
            // because the ABI hands it to the entry; __home is a property ON
            // that closure, and reading it is the helper's job rather than two
            // operations here - the row's flags are all zero precisely because
            // closure_object::find touches no accessors.
            case op::load_home: set(in.a, ctjs::LoadHomeOp::create(into, where, value_type)); break;
            case op::pass_new_target: ctjs::PassNewTargetOp::create(into, where); break;
            case op::get_proto:
                set(in.a, ctjs::GetProtoOp::create(into, where, value_type, reg(in.b)));
                break;
            case op::set_proto:
                // a IS THE TARGET, b THE NEW LINK, and nothing is written back.
                ctjs::SetProtoOp::create(into, where, reg(in.a), reg(in.b));
                break;
            case op::closure: {
                // THE OPCODE THAT MADE EVERY DECLARING FUNCTION UNIMPORTABLE.
                // ctjs.create_closure's first operand was a !ctjs.program that
                // nothing produced, so this case did not exist and any function
                // declaring another was skipped whole - which is every top
                // level in a real file.
                if (in.bx() >= state.prog.functions.size()) {
                    state.give_up(at, in.code, "closure function index out of range");
                    break;
                }
                const function_proto & target = state.prog.functions[in.bx()];
                // IN PARALLEL WITH THE DESCRIPTORS, NOT PACKED. Only the
                // entries the compiler marked from_parent_local are read by the
                // helper; the rest it fills from the enclosing closure. A
                // packed list would silently capture the wrong bindings.
                //
                // AND AN ENTRY THE HELPER FILLS FROM THE ENCLOSING CLOSURE IS
                // WRITTEN AS THE READ OF THAT UPVALUE, not as `undefined`. The
                // VM copies `enclosing->upvalues[up.index]` into the slot
                // (context::make_closure, call.cpp) - the cell this frame's own
                // closure holds at that index - so the honest operand is what
                // this frame reads there: a ctjs.load_upvalue of its own
                // closure at `up.index`. The helper still never looks at it.
                // What does is the native tier's closure lift: once THIS
                // function is lifted, that load is its capture parameter, and
                // the nested closure's capture is then provably the same
                // constant - Phase 59 slice 1b. An `undefined` placeholder
                // carried no such edge, and the slice could not be built on it:
                // the descriptor's `up.index` is not in the IR anywhere else.
                //
                // OUT OF RANGE IS UNDEFINED, exactly as the VM has it
                // (`up.index < enclosing->upvalues.size()`, else undefined).
                llvm::SmallVector<mlir::Value> captured;
                captured.reserve(target.upvalues.size());
                bool reachable = true;
                for (const upvalue_desc & up : target.upvalues) {
                    if (!up.from_parent_local) {
                        captured.push_back(
                            up.index < proto.upvalues.size()
                                ? ctjs::LoadUpvalueOp::create(
                                      into, where, value_type, entry->getArgument(arg_callee),
                                      into.getI32IntegerAttr(static_cast<std::int32_t>(up.index)))
                                      .getResult()
                                : state.undefined(where));
                        continue;
                    }
                    if (up.index >= state.registers.size()) {
                        state.give_up(at, in.code, "upvalue names a register past the frame");
                        reachable = false;
                        break;
                    }
                    captured.push_back(reg(up.index));
                }
                if (!reachable) { break; }
                // `this` ONLY WHEN THE TARGET IS AN ARROW, and that is a
                // correction with a measured cost. The VM reads
                // $enclosing_this at exactly one line - call.cpp:924,
                // `if (target.is_arrow) { made->captured_this = ... }` - so
                // for every ordinary function this operand was DEAD, and a
                // dead operand is still a USE. The receiver arrives as %arg0,
                // and admission::function refuses any function that reads it:
                // so `function outer() { function inner() {} }` was refused
                // for "uses `this`" when nothing in it mentions `this` at all.
                // Measured over bootstrap, p5 and phaser, that artefact is
                // 1,866 of the 8,600 `uses \`this\`` refusals.
                //
                // AND THE UNDEFINED IS A MARKER, not only a smaller graph:
                // it is now the ONLY place the IR says whether a target is an
                // arrow, which is what lets the native tier lift an ordinary
                // closure and refuse an arrow that reads its lexical `this`
                // (LowerToEmitC.cpp, the Phase 59 lift).
                set(in.a,
                    ctjs::CreateClosureOp::create(
                        into, where, value_type, entry->getArgument(arg_callee),
                        target.is_arrow ? entry->getArgument(arg_receiver) : state.undefined(where),
                        into.getI32IntegerAttr(static_cast<std::int32_t>(in.bx())), captured));
                break;
            }
            case op::get_global:
                if (in.bx() >= proto.names.size()) {
                    state.give_up(at, in.code, "name index out of range");
                    break;
                }
                set(in.a, ctjs::LoadGlobalOp::create(into, where, value_type,
                                                     into.getStringAttr(proto.names[in.bx()])));
                break;
            case op::set_global:
                if (in.bx() >= proto.names.size()) {
                    state.give_up(at, in.code, "name index out of range");
                    break;
                }
                ctjs::StoreGlobalOp::create(into, where, into.getStringAttr(proto.names[in.bx()]),
                                            reg(in.a));
                break;
            case op::get_prop: {
                if (in.c >= proto.names.size()) {
                    state.give_up(at, in.code, "name index out of range");
                    break;
                }
                const mlir::Value key =
                    state.constant(where, ctjs::StringAttr::get(context, proto.names[in.c]));
                set(in.a, ctjs::GetPropertyOp::create(into, where, value_type, reg(in.b), key));
                break;
            }
            case op::set_prop: {
                if (in.b >= proto.names.size()) {
                    state.give_up(at, in.code, "name index out of range");
                    break;
                }
                const mlir::Value key =
                    state.constant(where, ctjs::StringAttr::get(context, proto.names[in.b]));
                ctjs::SetPropertyOp::create(into, where, reg(in.a), key, reg(in.c));
                break;
            }
            case op::get_index:
                set(in.a,
                    ctjs::GetPropertyOp::create(into, where, value_type, reg(in.b), reg(in.c)));
                break;
            case op::has_property:
                // b IS THE KEY AND c IS THE TARGET, which is the reverse of
                // both the operation's operand order and the reading order of
                // `key in obj`. Swapping them answers about the wrong object.
                set(in.a,
                    ctjs::HasPropertyOp::create(into, where, value_type, reg(in.c), reg(in.b)));
                break;
            case op::instance_of:
                // BOXED HERE, because ctjs.instanceof answers an i1 on purpose.
                set(in.a, ctjs::FromBoolOp::create(
                              into, where, value_type,
                              ctjs::InstanceOfOp::create(into, where, into.getI1Type(), reg(in.b),
                                                         reg(in.c))));
                break;
            case op::delete_index:
                // a IS THE TARGET HERE, not the destination - delete_index
                // produces nothing and writes no register.
                ctjs::DeletePropertyOp::create(into, where, reg(in.a), reg(in.b));
                break;
            case op::push_handler: {
                mlir::Block * pad = block_at(jump_target(at, in));
                mlir::Block * body = block_at(static_cast<std::int64_t>(at) + 1);
                if (pad == nullptr || body == nullptr) {
                    state.give_up(at, in.code, "handler target is not a block leader");
                    break;
                }
                // AND THE PAD MUST BE REACHABLE ONLY BY THROWING.
                //
                // ct_aot_catch_land reads back what the unwinder wrote and then
                // CLEARS the pad marker; its row says it is "called exactly
                // once, only after CT_AOT_CAUGHT". Reached on a normal path it
                // clears a bit nothing set and binds a thrown value that was
                // never thrown.
                //
                // `try { f(); } catch (e) {}` IS THAT SHAPE. With an empty catch
                // the compiler emits no jump over it, so the try body falls
                // straight into the pad. A non-empty catch is preceded by that
                // jump, which is why this tests the instruction before the pad
                // rather than the catch clause itself.
                //
                // TWO WAYS IN, AND BOTH HAVE TO BE CLOSED. The pad can be fallen
                // into from the instruction above it, and it can be JUMPED to -
                // which is not exotic: jumping over an EMPTY catch clause lands
                // exactly on the pad, because the clause it is skipping has no
                // instructions. `try { f(); } catch (e) {}` is that shape, and a
                // test that only looked at the instruction above it let that
                // through.
                const std::int64_t landing = jump_target(at, in);
                bool reachable_without_throwing =
                    landing > 0 &&
                    falls_through(proto.code[static_cast<std::size_t>(landing) - 1].code);
                for (std::size_t other = 0;
                     other < proto.code.size() && !reachable_without_throwing; ++other) {
                    if (other == at || !is_jump(proto.code[other].code)) { continue; }
                    if (jump_target(other, proto.code[other]) == landing) {
                        reachable_without_throwing = true;
                    }
                }
                if (reachable_without_throwing) {
                    state.give_up(at, in.code,
                                  "the catch clause is reachable without throwing, which would "
                                  "run ct_aot_catch_land on a normal path");
                    break;
                }
                // A TERMINATOR WITH TWO SUCCESSORS, and the handler edge here
                // exists only to keep the pad reachable - the edge the emitted
                // code takes is ctjs.check's, at the throw site, because that
                // is the only place the live register vector exists.
                ctjs::PushHandlerOp::create(into, where, state.outgoing(), state.outgoing(), body,
                                            pad);
                state.handlers.push_back(function_importer::open_handler{pad, in.a});
                // AND THE WALK MOVES ON BY ITSELF. push_handler ends a block, so
                // `body` is a leader and the next iteration's enter_block enters
                // it and rebinds the registers - doing it here too made
                // enter_block see `previous == body` and emit a branch from the
                // block to ITSELF, which is a cf.br that does not terminate its
                // parent and the only symptom is "the imported function did not
                // verify".
                break;
            }
            case op::pop_handler:
                if (state.handlers.empty()) {
                    state.give_up(at, in.code, "pop_handler with no open handler in this function");
                    break;
                }
                ctjs::PopHandlerOp::create(into, where);
                state.handlers.pop_back();
                break;
            case op::make_arguments:
                set(in.a, ctjs::MakeArgumentsOp::create(into, where, value_type));
                break;
            case op::gather_rest:
                // b IS A COUNT - how many parameters were declared before the
                // rest one - not a register. The emitter spells it
                // `{gather_rest, i, i}` with both fields equal, which makes a
                // mutation from b to a invisible; that is recorded in
                // docs/plans/arguments-and-rest.md rather than papered over.
                set(in.a, ctjs::GatherRestOp::create(into, where, value_type,
                                                     static_cast<std::uint32_t>(in.b)));
                break;
            case op::load_bigint:
                if (in.bx() >= proto.strings.size()) {
                    state.give_up(at, in.code, "bigint literal index out of range");
                    break;
                }
                // THE SOURCE TEXT, NOT A PARSED INTEGER. bigint_from_literal
                // owns `0x1fn`, `0b..n` and the 1.5n-to-0n substitution, and
                // parsing here would be a second implementation of all three.
                set(in.a,
                    state.constant(where, ctjs::BigIntAttr::get(context, proto.strings[in.bx()])));
                break;
            // ---- ES modules ---------------------------------------------
            //
            // THREE OF THESE FOUR ONLY EXIST IN functions[0] OF A MODULE
            // PROGRAM. compile_program emits load_import, bind_export and
            // load_namespace from its `if (module_scope_)` arm and from
            // nowhere else, so a classic script cannot contain one and a
            // fixture that means to reach them has to be compiled with
            // script_kind::module. dyn_import is the exception: `import(x)` is
            // an expression and appears in ordinary functions.
            case op::load_import:
                // b IS THE EXPORT NAME AND c IS THE SPECIFIER, which is the
                // reverse of the reading order and makes this the only opcode
                // that reads c as a standalone index. bytecode.hpp says the
                // same thing from the other side: "a = the cell exported as
                // names[b] by the module at specifier names[c]". Filling the
                // operation in reading order compiles and raises `module
                // `count` was not loaded` at run time.
                if (in.b >= proto.names.size() || in.c >= proto.names.size()) {
                    state.give_up(at, in.code, "name index out of range");
                    break;
                }
                // WHAT LANDS HERE IS A CELL, and it must NOT be boxed again -
                // the compiler marked this local boxed and deliberately emitted
                // no new_cell, so every later read is already a ctjs.cell_get.
                set(in.a, ctjs::ModuleImportCellOp::create(into, where, value_type,
                                                           into.getStringAttr(proto.names[in.c]),
                                                           into.getStringAttr(proto.names[in.b])));
                break;
            case op::bind_export:
                // bx(), NOT b. b is the HIGH half of the pair, so it is 0 for
                // every module with fewer than 65,536 names - which is all of
                // them - and a decoder that reads b alone publishes name 0
                // under this name, silently.
                if (in.bx() >= proto.names.size()) {
                    state.give_up(at, in.code, "name index out of range");
                    break;
                }
                // reg(in.a) IS A SOURCE AS WELL AS THE DESTINATION. The write
                // is conditional on a module being evaluated, and outside one
                // the register keeps the local being exported - so the
                // operation carries that value and answers it back rather than
                // answering undefined and destroying the local.
                set(in.a, ctjs::ModuleExportCellOp::create(into, where, value_type,
                                                           into.getStringAttr(proto.names[in.bx()]),
                                                           reg(in.a)));
                break;
            case op::load_namespace:
                if (in.b >= proto.names.size()) {
                    state.give_up(at, in.code, "name index out of range");
                    break;
                }
                // A PLAIN 16-BIT b HERE, where bind_export uses the bx pair:
                // three module opcodes, two encodings for one name table.
                set(in.a, ctjs::ModuleNamespaceOp::create(into, where, value_type,
                                                          into.getStringAttr(proto.names[in.b])));
                break;
            case op::dyn_import:
                // b IS A REGISTER, not a name index - the specifier is computed,
                // which is the whole difference between this and load_namespace.
                set(in.a, ctjs::DynamicImportOp::create(into, where, value_type, reg(in.b)));
                break;
            case op::delete_prop:
                // a IS THE TARGET, b NAMES THE PROPERTY, and nothing is
                // written back - the delete produces no value.
                ctjs::DeleteNamedOp::create(into, where, reg(in.a),
                                            into.getStringAttr(proto.names[in.b]));
                break;
            case op::own_keys:
                set(in.a, ctjs::OwnKeysOp::create(into, where, value_type, reg(in.b)));
                break;
            // AN ASYNC FUNCTION'S `return`, AND `a` IS BOTH SOURCE AND
            // DESTINATION. The compiler emits `{op::wrap_promise, r}` over the
            // register the return value is already in - b and c are unused, and
            // the opcode row says so - so reading a and writing a is the whole
            // encoding rather than a coincidence to be careful about.
            case op::wrap_promise:
                set(in.a, ctjs::WrapPromiseOp::create(into, where, value_type, reg(in.a)));
                break;
            case op::define_getter:
            case op::define_setter: {
                // a IS THE TARGET, b NAMES THE PROPERTY and c IS THE FUNCTION -
                // which half it is comes from the OPCODE and from nowhere else,
                // so it is resolved here and the operation carries both.
                const bool getter = in.code == op::define_getter;
                const mlir::Value nothing = state.undefined(where);
                ctjs::DefineAccessorOp::create(
                    into, where, reg(in.a), into.getStringAttr(proto.names[in.b]),
                    getter ? reg(in.c) : nothing, getter ? nothing : reg(in.c));
                break;
            }
            case op::copy_props:
                // a IS THE TARGET and is NOT written back - the object is
                // mutated in place, so this produces no value.
                ctjs::CopyPropsOp::create(into, where, reg(in.a), reg(in.b));
                break;
            case op::apply:
                // a IS BOTH THE CALLEE AND THE DESTINATION, b the argument
                // array, c the receiver - and the destination being an operand
                // is why this reads all three before writing.
                set(in.a, ctjs::CallSpreadOp::create(into, where, value_type, reg(in.a), reg(in.b),
                                                     reg(in.c)));
                break;
            case op::construct_apply:
                set(in.a,
                    ctjs::ConstructSpreadOp::create(into, where, value_type, reg(in.a), reg(in.b)));
                break;
            case op::iterable:
                // b IN, a OUT, and the row warns that both emitters spell it
                // `{iterable, source, source}` with a and b ALIASED - which
                // reading b and writing a handles without needing to know.
                set(in.a, ctjs::IterableOp::create(into, where, value_type, reg(in.b)));
                break;
            case op::set_index:
                ctjs::SetPropertyOp::create(into, where, reg(in.a), reg(in.b), reg(in.c));
                break;
            case op::new_object:
                set(in.a, ctjs::CreateObjectOp::create(into, where, value_type));
                break;
            case op::new_cell:
                // IN PLACE: the bytecode boxes the register's current value and
                // leaves the box where the value was.
                set(in.a, ctjs::CreateCellOp::create(into, where, value_type, reg(in.a)));
                break;
            case op::cell_get:
                set(in.a, ctjs::CellGetOp::create(into, where, value_type, reg(in.b)));
                break;
            case op::cell_set: ctjs::CellSetOp::create(into, where, reg(in.a), reg(in.b)); break;
            case op::get_upvalue:
                // THE FRAME'S OWN CLOSURE, which arrives as the third implicit
                // argument. The interpreter reads vm_frame->closure; a compiled
                // body is handed the same thing.
                set(in.a, ctjs::LoadUpvalueOp::create(into, where, value_type,
                                                      entry->getArgument(arg_callee),
                                                      into.getI32IntegerAttr(in.b)));
                break;
            case op::set_upvalue:
                ctjs::StoreUpvalueOp::create(into, where, entry->getArgument(arg_callee),
                                             into.getI32IntegerAttr(in.a), reg(in.b));
                break;
            case op::new_array:
                set(in.a, ctjs::CreateArrayOp::create(into, where, value_type,
                                                      llvm::SmallVector<mlir::Value>{}));
                break;
            case op::append: ctjs::AppendOp::create(into, where, reg(in.a), reg(in.b)); break;
            case op::call:
            case op::call_method:
            case op::call_computed:
            case op::call_receiver: {
                // THE ARGUMENTS ARE THE REGISTERS ABOVE THE CALLEE, which is
                // the interpreter's own layout: `arg_base = base + in.a + 1`,
                // and `in.b` of them. The callee's frame starts where its
                // arguments already are, which is why the bytecode puts them
                // there.
                mlir::Value callee = reg(in.a);
                mlir::Value receiver = state.undefined(where);
                if (in.code == op::call_receiver) {
                    receiver = reg(in.c);
                } else if (in.code == op::call_method) {
                    if (in.c >= proto.names.size()) {
                        state.give_up(at, in.code, "name index out of range");
                        break;
                    }
                    // THROUGH THE SAME LOOKUP AS get_prop, which the
                    // interpreter is explicit about: `s.split(...)` and
                    // `var f = s.split; f(...)` must find the same function.
                    receiver = reg(in.a);
                    const mlir::Value key =
                        state.constant(where, ctjs::StringAttr::get(context, proto.names[in.c]));
                    callee = ctjs::GetPropertyOp::create(into, where, value_type, receiver, key);
                } else if (in.code == op::call_computed) {
                    receiver = reg(in.a);
                    callee =
                        ctjs::GetPropertyOp::create(into, where, value_type, receiver, reg(in.c));
                }
                llvm::SmallVector<mlir::Value> args;
                bool reachable = true;
                for (unsigned i = 0; i < in.b; ++i) {
                    const auto slot = static_cast<std::uint16_t>(in.a + 1 + i);
                    if (slot >= state.registers.size()) {
                        state.give_up(at, in.code, "an argument register is outside the frame");
                        reachable = false;
                        break;
                    }
                    args.push_back(reg(slot));
                }
                if (!reachable) { break; }
                set(in.a, ctjs::CallOp::create(into, where, value_type, callee, receiver, args));
                break;
            }
            case op::construct: {
                llvm::SmallVector<mlir::Value> args;
                bool reachable = true;
                for (unsigned i = 0; i < in.b; ++i) {
                    const auto slot = static_cast<std::uint16_t>(in.a + 1 + i);
                    if (slot >= state.registers.size()) {
                        state.give_up(at, in.code, "an argument register is outside the frame");
                        reachable = false;
                        break;
                    }
                    args.push_back(reg(slot));
                }
                if (!reachable) { break; }
                // NEW.TARGET IS THE CALLEE for a plain `new C()`. A super() call
                // hands a different one along, and that is op::pass_new_target's
                // business rather than this opcode's.
                set(in.a,
                    ctjs::ConstructOp::create(into, where, value_type, reg(in.a), reg(in.a), args));
                break;
            }
            case op::jump_if_not_nullish:
            case op::jump_if_defined: {
                mlir::Block * taken = block_at(jump_target(at, in));
                mlir::Block * fallthrough = block_at(static_cast<std::int64_t>(at) + 1);
                if (taken == nullptr || fallthrough == nullptr) {
                    state.give_up(at, in.code, "branch target is not a block leader");
                    break;
                }
                // NOT TRUTHINESS. `jump_if_defined` branches when the value is
                // not `undefined` - which 0, "" and false all are not - and
                // `jump_if_not_nullish` when it is neither null nor undefined.
                // Lowering either through ctjs.truthy would send `x ?? y` down
                // the wrong arm for every falsy x, which is exactly the bug
                // optional chaining exists to avoid.
                const mlir::Value sentinel =
                    in.code == op::jump_if_defined
                        ? state.undefined(where)
                        : state.constant(where, ctjs::NullAttr::get(context));
                const auto kind = in.code == op::jump_if_defined ? ctjs::CompareKind::StrictEq
                                                                 : ctjs::CompareKind::Eq;
                const mlir::Value matches = ctjs::CompareOp::create(
                    into, where, value_type, ctjs::CompareKindAttr::get(context, kind), reg(in.a),
                    sentinel);
                const mlir::Value bit =
                    ctjs::TruthyOp::create(into, where, into.getI1Type(), matches);
                const auto operands = state.outgoing();
                // The comparison is TRUE when the value IS the sentinel, and
                // both opcodes jump when it is NOT - so the arms are swapped.
                mlir::cf::CondBranchOp::create(into, where, bit, fallthrough, operands, taken,
                                               operands);
                break;
            }
            case op::jump: {
                mlir::Block * target = block_at(jump_target(at, in));
                if (target == nullptr) {
                    state.give_up(at, in.code, "jump target is not a block leader");
                    break;
                }
                mlir::cf::BranchOp::create(into, where, target, state.outgoing());
                break;
            }
            case op::jump_if_false:
            case op::jump_if_true: {
                mlir::Block * taken = block_at(jump_target(at, in));
                mlir::Block * fallthrough = block_at(static_cast<std::int64_t>(at) + 1);
                if (taken == nullptr || fallthrough == nullptr) {
                    state.give_up(at, in.code, "branch target is not a block leader");
                    break;
                }
                // THE ONLY BRIDGE FROM A VALUE TO A BRANCH. cf.cond_br takes an
                // i1; ctjs.convert to_boolean would produce a JavaScript
                // boolean, which is a different thing.
                const mlir::Value bit =
                    ctjs::TruthyOp::create(into, where, into.getI1Type(), reg(in.a));
                const auto operands = state.outgoing();
                if (in.code == op::jump_if_true) {
                    mlir::cf::CondBranchOp::create(into, where, bit, taken, operands, fallthrough,
                                                   operands);
                } else {
                    mlir::cf::CondBranchOp::create(into, where, bit, fallthrough, operands, taken,
                                                   operands);
                }
                break;
            }
            case op::ret:
            case op::ret_undef:
            case op::halt: {
                const mlir::Value returned =
                    in.code == op::ret ? reg(in.a) : state.undefined(where);
                // THE FRAME IS RELEASED ON THE RETURNING PATH ONLY. On an
                // unwound one it is already gone, and leaving again would pop
                // somebody else's.
                ctjs::FrameExitOp::create(into, where, state.frame);
                ctjs::ReturnOp::create(into, where, returned);
                break;
            }
            case op::throw_value: ctjs::ThrowOp::create(into, where, reg(in.a)); break;
            default: state.give_up(at, in.code, "no CTJS operation for this opcode yet"); break;
            }

            // ---- and the caught edge, if a region is open ------------------
            //
            // ONE ctjs.check PER INSTRUCTION, not per fallible operation, and
            // the two are not the same: op::call_method emits a
            // ctjs.get_property AND a ctjs.call, and `try { o.m(); } catch (e)
            // {}` is about as ordinary as JavaScript gets. Splitting per
            // instruction is still correct because an instruction writes its
            // destination register LAST - so the snapshot taken before it is
            // exactly what the interpreter would have in hand at either throw.
            //
            // AND IT IS EMITTED AFTER EVERY INSTRUCTION, not only after ones
            // that can throw. A check whose block holds nothing fallible lowers
            // to a plain branch and costs an eliminated block argument; deciding
            // which instructions can throw is the lowering's job, because it is
            // the lowering that knows which operations became status calls.
            if (!state.gave_up && !state.handlers.empty() && !in_terminator(in.code)) {
                mlir::Block * fresh = &function.getBody().emplaceBlock();
                fresh->addArguments(slot_types, slot_locs);
                ctjs::CheckOp::create(into, where, state.outgoing(), before_instruction, fresh,
                                      state.handlers.back().pad);
                into.setInsertionPointToEnd(fresh);
                for (std::size_t slot = 0; slot < proto.frame_size; ++slot) {
                    state.write(slot, fresh->getArgument(static_cast<unsigned>(slot)));
                }
            }
        }

        if (!state.gave_up) {
            // A BLOCK THAT RAN OFF ITS END needs a terminator. The bytecode
            // guarantees one only at the end of the function; a leader in the
            // middle can be reached by falling through.
            for (mlir::Block & block : function.getBody()) {
                if (!block.empty() && block.back().hasTrait<mlir::OpTrait::IsTerminator>()) {
                    continue;
                }
                into.setInsertionPointToEnd(&block);
                const mlir::Value returned = ctjs::ConstantOp::create(
                    into, into.getUnknownLoc(), value_type, ctjs::UndefinedAttr::get(context));
                ctjs::FrameExitOp::create(into, into.getUnknownLoc(), state.frame);
                ctjs::ReturnOp::create(into, into.getUnknownLoc(), returned);
            }
        }

        out.skipped.insert(out.skipped.end(), skipped.begin(), skipped.end());
        if (state.gave_up) { continue; }

        // ADOPTED ONLY IF IT VERIFIES. "A verifier failure here is an importer
        // bug, never a reason to relax the verifier" - so a function that does
        // not verify is reported as unsupported rather than emitted.
        if (mlir::failed(mlir::verify(function))) {
            // THE SECOND REFUSAL SITE, and it needs the same summary: this
            // function is dropped exactly as a `give_up` one is, so a globals
            // record without it would be the one skipped row that still
            // refuses the whole module.
            dropped_globals globals = summarise_globals(proto);
            out.skipped.push_back(
                unsupported_opcode{program_id.str(), static_cast<std::uint32_t>(index), 0, "-",
                                   "the imported function did not verify",
                                   std::move(globals.stores), std::move(globals.opaque)});
            continue;
        }
        function->remove();
        out.module->push_back(function);

        // AND ITS REGISTER MAP, ONLY NOW. Everything above this line can still
        // abandon the function, and an abandoned function's values are erased
        // with the scratch module - so a map handed out earlier would be a set
        // of dangling handles rather than a coverage gap somebody notices.
        register_map occupancy;
        occupancy.function_index = static_cast<std::uint32_t>(index);
        occupancy.slots = std::move(state.occupied);
        out.register_maps.push_back(std::move(occupancy));
    }

    return out;
}

} // namespace ctcompile::js
