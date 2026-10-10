//===- Importer.h - Bytecode importer state ---------------------*- C++ -*-===//
//
// Private to the bytecode importer: the per-function state that walks one
// ctbrowser bytecode function into ctjs IR, and the control-flow and globals
// helpers that BytecodeImport.cpp, Instructions.cpp and Support.cpp share.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctcompile/CTJS/Import/BytecodeImport.hpp>

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTJS/Lowering/OpcodeMapping.hpp"

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
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>

namespace ctcompile::js::bytecode_detail {
using namespace ctbrowser::script;

/// What a function the importer refused can still do to the globals table.
struct dropped_globals {
    /// Names of the globals it stores to through set_global.
    std::vector<std::string> stores;
    /// Why its writes cannot be enumerated, or empty when they can.
    std::string opaque;
};

/// Whether `code` is a suspension point (await or yield).
bool may_suspend(op code);
/// Summarises which globals `proto` can write; see Support.cpp.
dropped_globals summarise_globals(const function_proto & proto);
/// The absolute instruction index a jump at `at` transfers to.
std::int64_t jump_target(std::size_t at, const instruction & in);
/// Whether `code` is one of the VM's conditional jumps.
bool is_conditional_jump(op code);
/// Whether `code` is any jump, conditional or not.
bool is_jump(op code);
/// Whether `code` terminates its block (a jump, return, throw or push_handler).
bool ends_a_block(op code);
/// Whether control can continue to the next instruction after `code`.
bool falls_through(op code);
/// Where control can go from `at` without throwing: the jump target, then the
/// next instruction.
llvm::SmallVector<std::int64_t, 2> successors(std::size_t at, const instruction & in);

using ctjs::arg_callee;
using ctjs::arg_new_target;
using ctjs::arg_receiver;
using ctjs::implicit_arguments;

/// The state of importing one bytecode function into one ctjs.func.
struct function_importer {
    mlir::OpBuilder & builder;
    mlir::MLIRContext * context;
    const program & prog;
    const function_proto & proto;
    llvm::StringRef program_id;
    std::uint32_t function_index;
    std::vector<unsupported_opcode> & skipped;

    /// Register slot -> the SSA value currently in it.
    llvm::SmallVector<mlir::Value> registers{};

    /// Every register slot each value has ever been written to. The runtime
    /// type oracle records its observations per register, not per SSA value,
    /// and this map is what joins the two. See `register_map` in
    /// BytecodeImport.hpp for why the mapped type is a list and why it cannot
    /// be rebuilt from the IR later.
    llvm::MapVector<mlir::Value, llvm::SmallVector<std::uint16_t, 1>> occupied{};
    llvm::DenseMap<std::int64_t, mlir::Block *> blocks{};
    mlir::Value frame{};
    bool gave_up = false;

    /// The program's line table, built once and borrowed. Null when the source
    /// was dropped from the image or the debug tables were compiled out.
    const ctbrowser::script::line_table * lines = nullptr;

    /// The location of instruction `at`: a FusedLoc of a name that identifies
    /// the instruction within its program (`program:<id>:<function>:<pc>`) and
    /// the source position it came from.
    [[nodiscard]] mlir::Location location_for(std::size_t at) const {
        const std::string name = "program:" + program_id.str() + ":" +
                                 std::to_string(function_index) + ":" + std::to_string(at);
        mlir::Location where = mlir::NameLoc::get(mlir::StringAttr::get(context, name));

        // The instruction's own line and column, from code_offsets and the
        // line table. When those are missing (debug tables compiled out, or
        // the image dropped its source), fall back to the function's source
        // span. The file part is the program id, which ctjs-translate sets to
        // the input's buffer name, so diagnostics and debuggers see
        // `file.js:line:col`; the function is already named by the symbol the
        // op sits in.
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

    /// Abandons this function, recording the first refusal reason only.
    void give_up(std::size_t at, op code, std::string reason) {
        if (gave_up) { return; }
        gave_up = true;
        // The globals summary is taken here, while the bytecode is in hand.
        // The IR is about to be abandoned; `proto` belongs to the program,
        // outlives the scratch module, and is the only description of this
        // function that survives the refusal.
        dropped_globals globals = summarise_globals(proto);
        skipped.push_back(
            unsupported_opcode{program_id.str(), function_index, static_cast<std::uint32_t>(at),
                               std::string{ctjs::opcode_name(code)}, std::move(reason),
                               std::move(globals.stores), std::move(globals.opaque)});
    }

    /// proto.names[index], or null after giving up on an index past the pool.
    [[nodiscard]] const std::string * name_at(std::size_t at, op code, std::size_t index) {
        if (index < proto.names.size()) { return &proto.names[index]; }
        give_up(at, code, "name index out of range");
        return nullptr;
    }

    [[nodiscard]] mlir::Value constant(mlir::Location where, mlir::Attribute value) {
        return ctjs::ConstantOp::create(builder, where, ctjs::ValueType::get(context), value);
    }

    [[nodiscard]] mlir::Value undefined(mlir::Location where) {
        return constant(where, ctjs::UndefinedAttr::get(context));
    }

    /// A named property's key, as the string constant a lookup takes.
    [[nodiscard]] mlir::Value key(mlir::Location where, const std::string & name) {
        return constant(where, ctjs::StringAttr::get(context, name));
    }

    /// The returning path. The frame is released here and only here: on an
    /// unwound path it is already gone, and leaving again would pop another
    /// function's frame.
    void emit_return(mlir::Location where, mlir::Value returned) {
        ctjs::FrameExitOp::create(builder, where, frame);
        ctjs::ReturnOp::create(builder, where, returned);
    }

    /// An exception handler open at this point in the walk.
    ///
    /// `pad` is the block the unwinder transfers to and `slot` is the register
    /// the thrown value lands in. The bytecode chooses the slot:
    /// context::unwind_to_handler writes registers_[base + slot] and nothing
    /// else, so compiled code must put it in the same place.
    struct open_handler {
        mlir::Block * pad;
        std::uint16_t slot;
        bool operator==(const open_handler &) const = default;
    };
    /// The handlers open at this point in the walk, innermost last. Reset at
    /// every reachable leader from the control-flow dataflow in
    /// import_program; within a block, push_handler and pop_handler update it.
    llvm::SmallVector<open_handler> handlers{};

    /// The only place a register is written. Every write to `registers` must go
    /// through here so `occupied` stays in step; a bare `registers[slot] = ...`
    /// would silently drop the value from that map. Out-of-range slots are
    /// ignored here because some callers pass an instruction operand unchecked.
    void write(std::size_t slot, mlir::Value v) {
        if (slot >= registers.size()) { return; }
        registers[slot] = v;
        if (!v) { return; }
        // A linear scan on purpose. The list has one element for almost every
        // value (a value in two slots is a `mov` alias, in three is rare), so a
        // set would cost more than the scan it saves.
        llvm::SmallVector<std::uint16_t, 1> & slots = occupied[v];
        const auto narrowed = static_cast<std::uint16_t>(slot);
        if (!llvm::is_contained(slots, narrowed)) { slots.push_back(narrowed); }
    }

    [[nodiscard]] llvm::SmallVector<mlir::Location> slot_locations(std::size_t pc) const {
        return llvm::SmallVector<mlir::Location>(proto.frame_size, location_for(pc));
    }

    /// The register vector as successor operands: the whole file, every time.
    [[nodiscard]] llvm::SmallVector<mlir::Value> outgoing() const {
        return llvm::SmallVector<mlir::Value>{registers.begin(), registers.end()};
    }

    /// The block that starts at instruction `target`, or null if none does.
    mlir::Block * block_at(std::int64_t target) const {
        const auto found = blocks.find(target);
        return found == blocks.end() ? nullptr : found->second;
    }

    /// A conditional terminator's two successors (the jump's target and the
    /// instruction after it), or nothing after giving up because either is not
    /// a block leader.
    [[nodiscard]] std::optional<std::pair<mlir::Block *, mlir::Block *>> branch_targets(
        std::size_t at, const instruction & in, std::string reason) {
        mlir::Block * taken = block_at(jump_target(at, in));
        mlir::Block * next = block_at(static_cast<std::int64_t>(at) + 1);
        if (taken == nullptr || next == nullptr) {
            give_up(at, in.code, std::move(reason));
            return std::nullopt;
        }
        return std::pair{taken, next};
    }

    /// A block carrying the whole register file, its arguments located at `pc`.
    mlir::Block * new_block(mlir::Region & body, std::size_t pc) {
        mlir::Block * block = &body.emplaceBlock();
        block->addArguments(
            llvm::SmallVector<mlir::Type>(proto.frame_size, ctjs::ValueType::get(context)),
            slot_locations(pc));
        return block;
    }

    /// Rebinds the register file to a block's arguments on entering it.
    void bind(mlir::Block * block) {
        for (std::size_t slot = 0; slot < proto.frame_size; ++slot) {
            write(slot, block->getArgument(static_cast<unsigned>(slot)));
        }
    }
};

/// Imports the instruction at `at` into the current insertion point, or gives
/// up on the function.
void importInstruction(function_importer & state, mlir::Block * entry, std::size_t at);

} // namespace ctcompile::js::bytecode_detail
