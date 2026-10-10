//===- Finish.cpp - Dense array length summary ----------------------------===//
//
// Prints the summary line for the dense array `length` tests.
//
//===----------------------------------------------------------------------===//

#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::length_detail {

/// Prints the row, live-edit and budget-cutoff counts for the test log.
void LengthCases::finish() {
    std::printf("dense array length: %u rows, %u live states, two wide snapshots, "
                "%zu retention budget cutoffs\n",
                rows, liveStates, budgets);
}

} // namespace ctcompile::test::escape::arrays::length_detail
