#pragma once
// BYTECODE INTO CTJS MLIR. Phase 9.
//
// HAND-WRITTEN C++, and the plan says why: "This is one of the places where
// TableGen does not apply: it is a graph-reconstruction algorithm, not a
// pattern match." What IS declarative is the opcode metadata it consumes -
// ctbrowser's bytecode_opcodes.def - which the importer reads rather than
// re-deriving.
//
// NEVER A PARTIAL TRANSLATION. An opcode with no CTJS mapping abandons the
// WHOLE function, records why, and leaves it to be interpreted. The plan states
// the invariant in four words: "never emit partially correct AOT code", and the
// reason is that a half-translated function is one that runs and is wrong.
#include <cstdint>
#include <string>
#include <vector>

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OwningOpRef.h"
#include "llvm/ADT/StringRef.h"

namespace ctbrowser::script {
struct program;
enum class op : std::uint8_t;
} // namespace ctbrowser::script

namespace mlir {
class MLIRContext;
} // namespace mlir

namespace ctcompile::js {

// WHY ONE FUNCTION WAS NOT TRANSLATED. Structured because Phase 42's build
// statistics consume it and because "make the failure loud and machine-readable"
// is the plan's instruction - a coverage gap that is only a log line is a gap
// nobody counts.
struct unsupported_opcode {
    std::string program_id;
    std::uint32_t function_index = 0;
    // THE INSTRUCTION INDEX, not a byte offset. `code` is a vector of
    // fixed-size instructions, so an index is what every other part of this
    // engine means by an offset into it.
    std::uint32_t bc_offset = 0;
    std::string opcode;
    std::string reason;
};

struct import_result {
    mlir::OwningOpRef<mlir::ModuleOp> module;
    std::vector<unsupported_opcode> skipped;
};

// Every eligible function of one program, as one module.
[[nodiscard]] import_result import_program(const ctbrowser::script::program & from,
                                           llvm::StringRef program_id, mlir::MLIRContext * context);

} // namespace ctcompile::js
