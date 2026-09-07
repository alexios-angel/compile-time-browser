#include "Bytecode/Importer.h"
#include "Bytecode/OperatorTables.h"

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
using namespace bytecode_detail;

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
        for (std::size_t slot = 0; slot < proto.param_count; ++slot) {
            entry->getArgument(static_cast<unsigned>(implicit_arguments + slot))
                .setLoc(state.names.location(state.location_for(0), slot, 0));
        }
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
            block->addArguments(slot_types, state.slot_locations(at));
            state.blocks[static_cast<std::int64_t>(at)] = block;
        }

        // ---- the walk -----------------------------------------------------
        const auto enter_block = [&](std::size_t at) {
            mlir::Block * block = state.block_at(static_cast<std::int64_t>(at));
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

        // A generator with no yield still has a different invocation: calling
        // it allocates an iterator and executes none of this body. Its source
        // flag is not encoded by an instruction, so an otherwise ordinary
        // literal return cannot become an eagerly callable ctjs.func. Retain
        // the existing skipped-function/global-store accounting until the
        // suspension ABI can represent this invocation kind.
        if (!state.gave_up && proto.is_generator) {
            state.give_up(0, proto.code.empty() ? op::ret_undef : proto.code.front().code,
                          "a generator invocation creates a deferred iterator instead of "
                          "executing the body, even without a suspension point - Phase 14");
        }

        for (std::size_t at = 0; at < proto.code.size() && !state.gave_up; ++at) {
            if (leader[at] && (at > 0 || targets_zero)) { enter_block(at); }
            // A PAD BLOCK CLOSES THE REGION IT LANDS FROM. The bytecode's
            // pop_handler runs on the NORMAL path only, so a throw leaves the
            // handler stack as the interpreter's unwinder left it - popped.
            if (!state.handlers.empty() &&
                state.handlers.back().pad == state.block_at(static_cast<std::int64_t>(at))) {
                state.handlers.pop_back();
            }
            // A PAD BLOCK TAKES ITS THROWN VALUE FROM THE RUNTIME, not from a
            // predecessor. ctjs.catch_land is the block's first operation and
            // its result replaces the block argument for the catch register -
            // that argument stays dead on purpose.
            if (const auto landed = catch_slot_at.find(static_cast<std::int64_t>(at));
                landed != catch_slot_at.end() &&
                state.block_at(static_cast<std::int64_t>(at)) != nullptr) {
                auto land = ctjs::CatchLandOp::create(into, state.location_for(at),
                                                      into.getI32Type(), value_type);
                state.assign(landed->second, land.getThrown(), at);
            }
            const instruction & in = proto.code[at];
            const mlir::Location where = state.location_for(at);
            const auto reg = [&](std::uint16_t slot) -> mlir::Value {
                return slot < state.registers.size() ? state.registers[slot] : mlir::Value{};
            };
            const auto set = [&](std::uint16_t slot, mlir::Value v) { state.assign(slot, v, at); };

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

            importInstruction(state, entry, at);

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
                fresh->addArguments(slot_types, state.slot_locations(at + 1));
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
