#pragma once

#include "ctcompile/CTNative/Analysis/HostContract.h"

namespace ctcompile::ctnative {

// Prepare a fingerprinted DOM entry with the existing URI, helper, element and
// iteration proofs. Publish the module and refreshed contract together only
// after complete source reproof; both remain unchanged on refusal. This grants
// no class, ownership or native representation authority.
llvm::Error prepareDOMEntry(mlir::ModuleOp module, HostContract & contract, unsigned maxSteps);

} // namespace ctcompile::ctnative
