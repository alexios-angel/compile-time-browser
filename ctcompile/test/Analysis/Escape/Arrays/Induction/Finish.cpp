//===- Finish.cpp - Induction test summary ----------------------*- C++ -*-===//
//
// Prints the array-induction tests' row and budget counts.
//
//===----------------------------------------------------------------------===//
#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::induction_detail {

void InductionCases::finish() {
    std::printf("array induction: %u rows, %u live states, %zu retention budget cutoffs\n", rows,
                liveStates, budgets);
}

} // namespace ctcompile::test::escape::arrays::induction_detail
