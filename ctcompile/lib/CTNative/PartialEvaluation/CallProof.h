//===- CallProof.h - Heap equivalence of two call results -------*- C++ -*-===//
//
// Proves that calling a specialised function and the original function on the
// same arguments leaves equivalent heaps and returns equivalent results. The
// partial evaluator uses it before trusting a ctjs.call_direct whose target
// differs from the callee value it was specialised from.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "Heap.h"

namespace ctcompile::ctnative::partial_eval {
/// Result of comparing a specialised call against the original: equivalent,
/// observably different, or `exhausted` when the step budget ran out first.
enum class callProofResult {
    equivalent,
    different,
    exhausted
};

/// Compares the state after the candidate call with the state after the
/// original call. The first `anchored` heap nodes existed before the call and
/// must keep their exact identities. Nodes allocated by the call may be
/// renamed, but only by a bijection that preserves aliases, Map order and
/// closure/cell edges. `steps` is charged per comparison, up to `maxSteps`.
callProofResult equivalentCallState(const snapshot & candidate, value candidateResult,
                                    const snapshot & original, value originalResult,
                                    unsigned anchored, unsigned & steps, unsigned maxSteps);
} // namespace ctcompile::ctnative::partial_eval
