//===- BytecodeImport.hpp - Bytecode to CTJS MLIR ---------------*- C++ -*-===//
//
// Imports ctbrowser bytecode into the CTJS MLIR dialect.
//
// This is hand-written C++ rather than TableGen patterns because it rebuilds
// a control-flow graph from bytecode, which is not a pattern match. The opcode
// metadata it consumes is declarative: ctbrowser's bytecode_opcodes.def.
//
// A function is never partly translated. An opcode with no CTJS mapping
// abandons the whole function, records the reason in `unsupported_opcode`,
// and leaves the function to the interpreter, because a half-translated
// function would run and compute the wrong answer.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/IR/Value.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace ctbrowser::script {
struct program;
enum class op : std::uint8_t;
} // namespace ctbrowser::script

namespace mlir {
class MLIRContext;
} // namespace mlir

namespace ctcompile::js {

/// Why one function was not translated. It is a struct, not a log line, so
/// build statistics can count coverage gaps by opcode and reason.
struct unsupported_opcode {
    std::string program_id;
    std::uint32_t function_index = 0;
    /// The instruction index, not a byte offset. `code` is a vector of
    /// fixed-size instructions, so an index is what every other part of this
    /// engine means by an offset into it.
    std::uint32_t bc_offset = 0;
    std::string opcode;
    std::string reason;

    /// The global names this untranslated function may store with
    /// op::set_global.
    ///
    /// An untranslated function still runs in the interpreter, so its stores
    /// can rebind globals that --ctjs-resolve-globals is trying to prove are
    /// bound only once. The set can be read straight from the bytecode:
    /// set_global's name operand is `proto.names[in.bx()]`, a static index into
    /// the function's own name pool. That lets the resolver refuse only these
    /// names instead of every global in the module.
    std::vector<std::string> stores;

    /// Why `stores` is not the whole answer, or empty when it is. `stores`
    /// bounds a body's effect on the globals table only if that body cannot run
    /// code from outside this module (`Function`, `eval`, `import()`) and cannot
    /// hand the global object to something that writes through it. Those are
    /// the hazards --ctjs-resolve-globals checks in the IR, which an
    /// untranslated body does not have. Non-empty means "this summary bounds
    /// nothing; refuse every global in the module".
    std::string opaque;
};

/// Which bytecode register slots each imported SSA value ever occupied.
///
/// Type inference produces one fact per SSA value, while the interpreter-side
/// type oracle records one per register; this map joins the two.
///
/// A value may occupy more than one slot, hence the list: the importer models
/// `mov dst, src` by aliasing one mlir::Value into a second slot rather than by
/// emitting a copy.
///
/// Every write is recorded, not just the final resident. A slot written twice
/// inside one block (`r0 = a + b; r0 = c + d;`) holds only the second value by
/// the time the block branches, but the interpreter observed both. So the
/// importer records at each write site rather than reading the mapping back
/// off the IR, and the claim for a slot is the join over every value listed
/// against it. A missed write would make the checker report unsoundness that
/// is not there.
///
/// The keys borrow `import_result::module`: an mlir::Value is a handle into
/// operations the module owns, so the map is only meaningful while that module
/// is alive.
struct register_map {
    std::uint32_t function_index = 0;
    llvm::MapVector<mlir::Value, llvm::SmallVector<std::uint16_t, 1>> slots;
};

/// The output of import_program().
struct import_result {
    mlir::OwningOpRef<mlir::ModuleOp> module;
    std::vector<unsupported_opcode> skipped;
    /// One entry per function the module actually contains. Abandoned
    /// functions have no ctjs.func, so their values are gone and they appear
    /// only in `skipped`. Keep new fields at the end: a field inserted
    /// mid-struct would silently shift any positional aggregate initialiser.
    std::vector<register_map> register_maps{};
};

/// Imports every eligible function of one program into one module.
[[nodiscard]] import_result import_program(const ctbrowser::script::program & from,
                                           llvm::StringRef program_id, mlir::MLIRContext * context);

} // namespace ctcompile::js
