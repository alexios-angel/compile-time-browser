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

// Phase 54A. Which bytecode register slots each imported SSA value ever
// occupied. The type inference produces a fact per SSA VALUE; the Phase 54B
// oracle observes a fact per REGISTER. This is the join between them.
//
// A VALUE MAY OCCUPY MORE THAN ONE SLOT, which is why the mapped type is a
// list: the importer models `mov dst, src` by aliasing one mlir::Value into a
// second slot rather than by emitting a copy.
//
// EVERY WRITE, NOT THE FINAL RESIDENT. A slot written twice inside one block -
// `r0 = a + b; r0 = c + d;` - holds only the second value by the time the
// block branches, and the interpreter observed BOTH. The importer therefore
// records at each write site rather than reading the mapping back off the IR,
// and the claim for a slot is the join over every value listed against it.
// Missing a write would leave the claim not covering a type the interpreter
// saw, which reads as unsoundness in a checker whose whole job is to be
// believed.
//
// THE KEYS BORROW `import_result::module`. An mlir::Value is a handle into the
// operations the module owns, so a map outlives nothing: it is only meaningful
// while the module it came from is alive.
struct register_map {
    std::uint32_t function_index = 0;
    llvm::MapVector<mlir::Value, llvm::SmallVector<std::uint16_t, 1>> slots;
};

struct import_result {
    mlir::OwningOpRef<mlir::ModuleOp> module;
    std::vector<unsupported_opcode> skipped;
    // AT THE END, WITH AN INITIALISER, because this struct has been extended
    // before and a field inserted mid-struct silently re-points any positional
    // aggregate initialiser. (There is none today - every construction is
    // `import_result out;` or copy-initialisation from import_program - and
    // appending keeps it that way.)
    //
    // ONE ENTRY PER FUNCTION THE MODULE ACTUALLY CONTAINS. A function that was
    // abandoned emits no ctjs.func and its values die with the scratch module
    // that held them, so recording a map for it would hand out dangling
    // handles; `skipped` is where those functions are reported.
    std::vector<register_map> register_maps{};
};

// Every eligible function of one program, as one module.
[[nodiscard]] import_result import_program(const ctbrowser::script::program & from,
                                           llvm::StringRef program_id, mlir::MLIRContext * context);

} // namespace ctcompile::js
