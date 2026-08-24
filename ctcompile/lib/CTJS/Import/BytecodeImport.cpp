#include <ctcompile/CTJS/Import/BytecodeImport.hpp>

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"

#include <ctbrowser/script/bytecode.hpp>

#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Verifier.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

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
    llvm::DenseMap<std::int64_t, mlir::Block *> blocks{};
    mlir::Value frame{};
    bool gave_up = false;

    [[nodiscard]] mlir::Location location_for(std::size_t at) const {
        // FUSED, WHICH THE PHASE 7 CONVENTION ASKS FOR: a name that identifies
        // the instruction inside its program, and the source span the function
        // came from. "Retrofitting locations later is far more expensive."
        const std::string name = "program:" + program_id.str() + ":" +
                                 std::to_string(function_index) + ":" + std::to_string(at);
        mlir::Location where = mlir::NameLoc::get(mlir::StringAttr::get(context, name));
        const mlir::Location span = mlir::FileLineColLoc::get(
            mlir::StringAttr::get(context, proto.name.empty() ? "<anonymous>" : proto.name),
            proto.source_begin, proto.source_end);
        return mlir::FusedLoc::get(context, {where, span});
    }

    void give_up(std::size_t at, op code, std::string reason) {
        if (gave_up) { return; }
        gave_up = true;
        skipped.push_back(unsupported_opcode{program_id.str(), function_index,
                                             static_cast<std::uint32_t>(at),
                                             std::string{name_of(code)}, std::move(reason)});
    }

    [[nodiscard]] mlir::Value constant(mlir::Location where, mlir::Attribute value) {
        return ctjs::ConstantOp::create(builder, where, ctjs::ValueType::get(context), value);
    }

    [[nodiscard]] mlir::Value undefined(mlir::Location where) {
        return constant(where, ctjs::UndefinedAttr::get(context));
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
};

} // namespace

import_result import_program(const program & from, llvm::StringRef program_id,
                             mlir::MLIRContext * context) {
    import_result out;
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
            state.registers[slot] =
                slot < proto.param_count
                    ? entry->getArgument(static_cast<unsigned>(implicit_arguments + slot))
                    : state.undefined(state.location_for(0));
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
            skipped.push_back(unsupported_opcode{program_id.str(),
                                                 static_cast<std::uint32_t>(index), 0, "jump",
                                                 "a jump leaves the function's bytecode"});
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
                state.registers[slot] = block->getArgument(static_cast<unsigned>(slot));
            }
        };

        for (std::size_t at = 0; at < proto.code.size() && !state.gave_up; ++at) {
            if (leader[at] && (at > 0 || targets_zero)) { enter_block(at); }
            const instruction & in = proto.code[at];
            const mlir::Location where = state.location_for(at);
            const auto reg = [&](std::uint16_t slot) -> mlir::Value {
                return slot < state.registers.size() ? state.registers[slot] : mlir::Value{};
            };
            const auto set = [&](std::uint16_t slot, mlir::Value v) {
                if (slot < state.registers.size()) { state.registers[slot] = v; }
            };

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
                // helper; the rest it fills from the enclosing closure, and
                // undefined here is a placeholder that is never looked at. A
                // packed list would silently capture the wrong bindings.
                llvm::SmallVector<mlir::Value> captured;
                captured.reserve(target.upvalues.size());
                bool reachable = true;
                for (const upvalue_desc & up : target.upvalues) {
                    if (!up.from_parent_local) {
                        captured.push_back(state.undefined(where));
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
                set(in.a,
                    ctjs::CreateClosureOp::create(
                        into, where, value_type, entry->getArgument(arg_callee),
                        entry->getArgument(arg_receiver),
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
            out.skipped.push_back(unsupported_opcode{program_id.str(),
                                                     static_cast<std::uint32_t>(index), 0, "-",
                                                     "the imported function did not verify"});
            continue;
        }
        function->remove();
        out.module->push_back(function);
    }

    return out;
}

} // namespace ctcompile::js
