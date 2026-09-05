#pragma once

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
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>

namespace ctcompile::js::bytecode_detail {
using namespace ctbrowser::script;

struct dropped_globals {
    std::vector<std::string> stores;
    std::string opaque;
};

std::string_view name_of(op code);
bool may_suspend(op code);
dropped_globals summarise_globals(const function_proto & proto);
std::int64_t jump_target(std::size_t at, const instruction & in);
bool is_conditional_jump(op code);
bool is_jump(op code);
bool ends_a_block(op code);
bool falls_through(op code);
bool in_terminator(op code);

inline constexpr unsigned implicit_arguments = 3;
inline constexpr unsigned arg_receiver = 0;
inline constexpr unsigned arg_new_target = 1;
inline constexpr unsigned arg_callee = 2;

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

    mlir::Block * block_at(std::int64_t target) const {
        const auto found = blocks.find(target);
        return found == blocks.end() ? nullptr : found->second;
    }
};

void importInstruction(function_importer & state, mlir::Block * entry, std::size_t at);

} // namespace ctcompile::js::bytecode_detail
