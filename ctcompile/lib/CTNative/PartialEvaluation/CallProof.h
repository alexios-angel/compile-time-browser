#pragma once

#include "Heap.h"

namespace ctcompile::ctnative::partial_eval {
enum class callProofResult {
    equivalent,
    different,
    exhausted
};

// Existing heap identities are anchored. Fresh reachable nodes may be renamed
// only by a bijection, preserving aliases, Map order and closure/cell edges.
callProofResult equivalentCallState(const snapshot & candidate, value candidateResult,
                                    const snapshot & original, value originalResult,
                                    unsigned anchored, unsigned & steps, unsigned maxSteps);
} // namespace ctcompile::ctnative::partial_eval
