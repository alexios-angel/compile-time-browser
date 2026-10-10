#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::length_detail {

void LengthCases::finish() {
    std::printf("dense array length: %u rows, %u live states, two wide snapshots, "
                "%zu retention budget cutoffs\n",
                rows, liveStates, budgets);
}

} // namespace ctcompile::test::escape::arrays::length_detail
