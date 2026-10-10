//===- BytecodeImport.cpp - Bytecode to ctjs import -----------------------===//
//
// Imports ctbrowser script bytecode into the ctjs MLIR dialect, one ctjs.func
// per bytecode function.
//
// The VM's register file becomes block arguments: every non-entry block takes
// `frame_size` !ctjs.value arguments, a side table maps each slot to its
// current SSA value, and every branch passes the whole vector. This
// deliberately avoids SSA construction: there are no dominance frontiers, no
// phi minimisation and no backpatching. Every block exists with its full
// argument list before emission, so successor operands are known at each
// branch, back edges included. A later pass prunes the dead arguments.
//
// ctjs.push_handler carries both $bodyOperands and $handlerOperands and
// implements BranchOpInterface, which assumes blocks carry this vector.
//
//===----------------------------------------------------------------------===//
#include "Bytecode/Importer.h"
#include "ctcompile/Support/CppLiterals.hpp"

namespace ctcompile::js {
using namespace bytecode_detail;

namespace {
// ---- leaders --------------------------------------------------------------

/// The instructions a block starts at.
struct leaders {
    /// Indexed by instruction, with one extra entry for one past the end.
    std::vector<bool> at;
    /// Whether any jump or handler targets instruction 0. leader[0] is always
    /// true, so this is recorded separately.
    bool targets_zero = false;
};

/// Every instruction a block starts at, or nothing when a jump leaves the code.
std::optional<leaders> find_leaders(const function_proto & proto) {
    leaders found{std::vector<bool>(proto.code.size() + 1, false)};
    std::vector<bool> & leader = found.at;
    if (!proto.code.empty()) { leader[0] = true; }
    for (std::size_t at = 0; at < proto.code.size(); ++at) {
        const instruction & in = proto.code[at];
        if (is_jump(in.code) || in.code == op::push_handler) {
            const std::int64_t target = jump_target(at, in);
            if (target < 0 || target > static_cast<std::int64_t>(proto.code.size())) {
                return std::nullopt;
            }
            leader[static_cast<std::size_t>(target)] = true;
            // Record whether anything branches back to instruction 0. This
            // sits under the combined jump-or-push_handler test on purpose:
            // a handler whose pad is instruction 0 needs it too, although no
            // corpus currently has one.
            if (target == 0) { found.targets_zero = true; }
        }
        if (ends_a_block(in.code) && at + 1 <= proto.code.size()) { leader[at + 1] = true; }
    }
    return found;
}

// ---- the refusals the walk cannot make -----------------------------------

/// Refuses function shapes that no single instruction reveals: several
/// protected regions, a suspension point, or a generator.
void refuse_unsupported_shape(function_importer & state) {
    const function_proto & proto = state.proto;

    // At most one protected region per function, for now. Two push_handlers
    // is what try/finally compiles to (compile_try_with_finally pushes its
    // second one sequentially, not nested, so a depth test would admit it),
    // and finally needs a completion record, a rethrow through
    // op::throw_value, and ctjs.resume_throw, which has no lowering. This
    // refuses finally, nested try and sibling try blocks alike; finally is the
    // one that would otherwise be miscompiled rather than merely refused.
    if (!state.gave_up) {
        const auto pushes = llvm::count_if(
            proto.code, [](const instruction & each) { return each.code == op::push_handler; });
        if (pushes > 1) {
            state.give_up(0, op::push_handler,
                          "more than one protected region in a function, which is what "
                          "try/finally and nested try both compile to");
        }
    }

    // A function containing a suspension point (await/yield) is refused as a
    // whole, with the offset of the first one so the diagnostic points at real
    // code. This is checked before the walk rather than as a `case` label,
    // because the importer-coverage test (test/CTJS/Import/Coverage.cpp) reads
    // this source and counts a `case` label as "dispatched", and a refusal is
    // not a dispatch. Do not spell a suspending opcode as a case label in this
    // comment: the coverage test would match it.
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
}

// ---- the handlers open at each instruction --------------------------------
using handler_stack = decltype(function_importer::handlers);

/// The handlers open at each instruction, or nothing for unreached ones.
std::vector<std::optional<handler_stack>> handlers_by_flow(function_importer & state) {
    const function_proto & proto = state.proto;
    // The handlers open at each instruction, computed by control flow rather
    // than by instruction order. A `return`, `break` or `continue` leaving a
    // try pops its handler on its own path before jumping (including one
    // routed through a finally block), so the try's normal-path pop comes
    // later in the code with the region still open on its own path. Read in
    // instruction order, that pop would find nothing open.
    //
    // A pad starts with the handlers open at its push_handler, because the
    // interpreter's unwinder pops the handler it lands from. Unreached code
    // keeps what the walk had, and its pop is emitted as it stands.
    std::vector<std::optional<handler_stack>> open_at(proto.code.size());
    if (!state.gave_up && !proto.code.empty()) {
        llvm::SmallVector<std::size_t> work{0};
        open_at[0].emplace();
        const auto reach = [&](std::size_t from, std::int64_t target, const handler_stack & open) {
            // A target past the end has no block; the walk refuses it.
            if (target < 0 || target >= static_cast<std::int64_t>(proto.code.size())) { return; }
            std::optional<handler_stack> & known = open_at[static_cast<std::size_t>(target)];
            if (!known) {
                known = open;
                work.push_back(static_cast<std::size_t>(target));
            } else if (*known != open) {
                state.give_up(from, proto.code[from].code,
                              "paths into one instruction have different handlers open");
            }
        };
        while (!work.empty() && !state.gave_up) {
            const std::size_t at = work.pop_back_val();
            const instruction & in = proto.code[at];
            handler_stack open = *open_at[at];
            if (in.code == op::push_handler) {
                reach(at, jump_target(at, in), open);
                open.push_back({state.block_at(jump_target(at, in)), in.a});
            } else if (in.code == op::pop_handler) {
                if (open.empty()) {
                    state.give_up(at, in.code, "pop_handler with no open handler in this function");
                    break;
                }
                open.pop_back();
            }
            for (const std::int64_t next : successors(at, in)) { reach(at, next, open); }
        }
    }
    return open_at;
}
} // namespace

import_result import_program(const program & from, llvm::StringRef program_id,
                             mlir::MLIRContext * context) {
    import_result out;
    // One line table for the whole program, built once. Every function's
    // instruction offsets index the same source text, so building it per
    // function would scan that text once per function.
    //
    // Empty source means no table, not an empty one: an image that dropped its
    // source also dropped code_offsets, because an offset into text nobody has
    // cannot be turned into a line.
    const ctbrowser::script::line_table lines{from.source};
    const ctbrowser::script::line_table * const lines_or_null =
        from.source.empty() ? nullptr : &lines;
    // Loaded, not merely registered. A DialectRegistry says a dialect may be
    // used; `Type::get` and `Op::create` need it actually loaded into the
    // context, and mlir-translate's harness only registers. Without this the
    // first ctjs::ValueType::get crashes with a stack trace that names the
    // importer and says nothing about dialect loading.
    context->getOrLoadDialect<ctjs::CTJSDialect>();
    context->getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    mlir::OpBuilder builder(context);
    out.module = mlir::ModuleOp::create(builder.getUnknownLoc());
    if (from.kind == script_kind::classic && !from.hoisted_vars.empty()) {
        llvm::SmallVector<mlir::Attribute> declarations;
        for (const std::string & name : from.hoisted_vars) {
            declarations.push_back(builder.getStringAttr(name));
        }
        // Declaration instantiation binds these names before the entry runs;
        // it preserves existing globals, so it supplies no initial value.
        (*out.module)->setAttr("ctjs.hoisted_vars", builder.getArrayAttr(declarations));
    }

    for (std::size_t index = 0; index < from.functions.size(); ++index) {
        const function_proto & proto = from.functions[index];
        std::vector<unsupported_opcode> skipped;

        // Each function is built in a scratch module and moved into the output
        // only if it is fully imported and verifies. Building it in the real
        // module and erasing it on failure could leave a half-translated
        // function behind.
        mlir::OwningOpRef<mlir::ModuleOp> scratch = mlir::ModuleOp::create(builder.getUnknownLoc());
        mlir::OpBuilder into(scratch->getBodyRegion());

        const auto value_type = ctjs::ValueType::get(context);
        llvm::SmallVector<mlir::Type> inputs(implicit_arguments + proto.param_count, value_type);
        const auto signature = into.getFunctionType(inputs, {value_type});

        // The index is always part of the name. ctjs.func is a Symbol, and a
        // real program has many functions sharing one name (p5.js has dozens
        // called `constructor` and dozens more anonymous). Without the suffix
        // the module fails to verify on a duplicate symbol, and ctjs-translate
        // produces no output at all even though every function verified.
        //
        // The source name stays in front of it, because a trace that says
        // @createCanvas$412 is worth reading and @fn412 is not.
        const std::string name =
            cpp::identifier(proto.name.empty() ? "fn" : proto.name) + "$" + std::to_string(index);
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
                .setLoc(state.location_for(0));
        }
        into.setInsertionPointToStart(entry);

        // The frame, in the entry block and nowhere else. The entry dominates
        // every block in the region, so the context needs no threading, which
        // matters because push_handler's operands are !ctjs.value and could not
        // carry a !ctjs.context.
        //
        // The frame op also records proto.frame_size, because ct_aot_enter
        // must size the register window from it. param_count alone would leave
        // locals without a slot.
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
        const std::optional<leaders> found = find_leaders(proto);
        if (!found) {
            // The only refusal whose globals summary is deliberately opaque.
            // The other refusals (give_up, and the verifier failure below)
            // drop a function whose instruction stream is well formed, so its
            // op::set_globals can be read back exactly. Here a jump leaves the
            // code array, so the program is corrupt and nothing in it should
            // be reasoned about instruction by instruction.
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

        const std::vector<bool> & leader = found->at;
        const bool targets_zero = found->targets_zero;

        // One block per leader, each carrying the whole register file.
        for (std::size_t at = targets_zero ? 0 : 1; at < proto.code.size(); ++at) {
            // Strictly inside the code. `ret` marks its successor a leader, and
            // for the last instruction that is one past the end: a block no
            // instruction would fill, which would need an invented terminator.
            //
            // Instruction 0 gets its own block only when something branches
            // back to it (for example, a function whose first statement is a
            // loop). leader[0] is always true, so `targets_zero` is checked
            // instead, so that every function does not get an empty header
            // block. The entry block cannot be the target: MLIR forbids
            // predecessors on a FunctionOpInterface entry block, and its
            // arguments are the ABI's. The entry falls through into the header
            // instead (see test/CTJS/Import/loop-property.mlir).
            if (!leader[at]) { continue; }
            state.blocks[static_cast<std::int64_t>(at)] = state.new_block(function.getBody(), at);
        }

        // ---- the walk -----------------------------------------------------
        const auto enter_block = [&](std::size_t at) {
            mlir::Block * block = state.block_at(static_cast<std::int64_t>(at));
            if (block == nullptr) { return; }
            // The fall-through edge, which is not implicit in an MLIR CFG.
            // Bytecode runs off the end of one instruction into the next; a
            // block does not run into the block below it. Without this branch
            // a block before a loop header would end, the terminator pass
            // below would give it `return undefined`, and the function would
            // verify and silently return undefined.
            mlir::Block * previous = into.getInsertionBlock();
            if (previous != nullptr && !previous->mightHaveTerminator()) {
                into.setInsertionPointToEnd(previous);
                mlir::cf::BranchOp::create(into, into.getUnknownLoc(), block, state.outgoing());
            }
            into.setInsertionPointToEnd(block);
            state.bind(block);
        };

        // Which register each pad's thrown value lands in, collected before the
        // walk because a pad block is reached from the CFG rather than from the
        // instruction that named it.
        llvm::DenseMap<std::int64_t, std::uint16_t> catch_slot_at;
        for (std::size_t at = 0; at < proto.code.size(); ++at) {
            if (proto.code[at].code == op::push_handler) {
                catch_slot_at[jump_target(at, proto.code[at])] = proto.code[at].a;
            }
        }

        refuse_unsupported_shape(state);
        const std::vector<std::optional<handler_stack>> open_at = handlers_by_flow(state);

        for (std::size_t at = 0; at < proto.code.size() && !state.gave_up; ++at) {
            if (leader[at] && (at > 0 || targets_zero)) { enter_block(at); }
            if (leader[at] && open_at[at]) { state.handlers = *open_at[at]; }
            // A pad block takes its thrown value from the runtime, not from a
            // predecessor. ctjs.catch_land is the block's first operation and
            // its result replaces the block argument for the catch register -
            // that argument stays dead on purpose.
            if (const auto landed = catch_slot_at.find(static_cast<std::int64_t>(at));
                landed != catch_slot_at.end() &&
                state.block_at(static_cast<std::int64_t>(at)) != nullptr) {
                auto land = ctjs::CatchLandOp::create(into, state.location_for(at),
                                                      into.getI32Type(), value_type);
                state.write(landed->second, land.getThrown());
            }
            const instruction & in = proto.code[at];
            const mlir::Location where = state.location_for(at);

            // The register file as of a throw, snapshotted before the
            // instruction runs. It is what the handler block will be given;
            // taking it after would name this instruction's own result in a
            // block the instruction does not dominate, which fails
            // verification.
            const llvm::SmallVector<mlir::Value> before_instruction =
                state.handlers.empty() ? llvm::SmallVector<mlir::Value>{} : state.outgoing();

            importInstruction(state, entry, at);

            // ---- and the caught edge, if a region is open ------------------
            //
            // One ctjs.check per instruction, not per fallible operation:
            // op::call_method emits a ctjs.get_property and a ctjs.call, and
            // either can throw. One check is still correct because an
            // instruction writes its destination register last, so the
            // snapshot taken before it is what the interpreter would have at
            // either throw.
            //
            // It is emitted after every instruction, not only after ones that
            // can throw. A check whose block holds nothing fallible lowers to a
            // plain branch; deciding which instructions can throw is the
            // lowering's job, because it knows which operations became status
            // calls. Never after a terminator: a second one does not verify.
            if (!state.gave_up && !state.handlers.empty() && !ends_a_block(in.code)) {
                mlir::Block * fresh = state.new_block(function.getBody(), at + 1);
                ctjs::CheckOp::create(into, where, state.outgoing(), before_instruction, fresh,
                                      state.handlers.back().pad);
                into.setInsertionPointToEnd(fresh);
                state.bind(fresh);
            }
        }

        if (!state.gave_up) {
            // A block that ran off its end needs a terminator. The bytecode
            // guarantees one only at the end of the function; a leader in the
            // middle can be reached by falling through.
            for (mlir::Block & block : function.getBody()) {
                if (block.mightHaveTerminator()) { continue; }
                into.setInsertionPointToEnd(&block);
                const mlir::Location nowhere = into.getUnknownLoc();
                state.emit_return(nowhere, state.undefined(nowhere));
            }
        }

        out.skipped.insert(out.skipped.end(), skipped.begin(), skipped.end());
        if (state.gave_up) { continue; }

        // Adopted only if it verifies. A verifier failure here is an importer
        // bug, never a reason to relax the verifier, so a function that does
        // not verify is reported as unsupported rather than emitted.
        if (mlir::failed(mlir::verify(function))) {
            // This refusal needs the same globals summary as give_up: the
            // function is dropped the same way, and a skipped row without a
            // summary would make the globals resolver refuse the whole module.
            dropped_globals globals = summarise_globals(proto);
            out.skipped.push_back(
                unsupported_opcode{program_id.str(), static_cast<std::uint32_t>(index), 0, "-",
                                   "the imported function did not verify",
                                   std::move(globals.stores), std::move(globals.opaque)});
            continue;
        }
        function->remove();
        out.module->push_back(function);

        // Publish its register map only now. Everything above can still
        // abandon the function, and an abandoned function's values are erased
        // with the scratch module, so a map handed out earlier would hold
        // dangling handles.
        register_map occupancy;
        occupancy.function_index = static_cast<std::uint32_t>(index);
        occupancy.slots = std::move(state.occupied);
        out.register_maps.push_back(std::move(occupancy));
    }

    return out;
}

} // namespace ctcompile::js
