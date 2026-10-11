//===- Analysis.h - Function reachability analysis --------------*- C++ -*-===//
//
// Finds the private ctjs functions that no root can reach, for
// --ctnative-prune-unreachable to delete, and the refused functions the
// native lowering removes after admission.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <string>

namespace ctcompile::ctnative::reachability {

/// The outcome of analyze().
struct result {
    /// Functions proved unreachable. Empty whenever `reason` is set.
    llvm::SmallVector<ctjs::FuncOp> dead;
    /// Why the proof was refused, or empty when it succeeded.
    std::string reason;
    /// Work units spent, bounded by the `maxSteps` given to analyze().
    uint64_t steps = 0;
    /// Number of functions kept.
    unsigned retained = 0;
};

/// Which functions start the walk, besides the top-level script (function
/// index 0), declarations without a body and anything a module attribute
/// names.
enum class roots {
    /// Every function that is not private, since code outside the module
    /// may name it. The default optimizations use this.
    nonPrivate,
    /// Only functions the embedder calls by name (`ctnative.string_entry`)
    /// and functions admission accepted (no `ctnative.not_native`). A
    /// function the importer left public because a ctjs.create_closure
    /// makes it is reached only through that closure. The native lowering
    /// uses this, after admission, to find refused functions nothing that
    /// can run creates or calls; an admitted function is a root so that no
    /// removal leaves native code naming an erased function.
    entries,
};

/// Analyzes one flat ctjs module without changing it. Any unsupported
/// reference or exhausted budget empties the deletion set. Existing
/// annotations on the IR are never trusted as evidence.
result analyze(mlir::ModuleOp module, unsigned maxSteps, roots from = roots::nonPrivate);

} // namespace ctcompile::ctnative::reachability
