#pragma once

#include <string>

namespace ctcompile {

// Where a comparator's two inputs first disagree, and how. Each comparator
// (HTML, CSS, JavaScript) spells `where` in its own terms.
struct difference {
    std::string where;
    std::string what;
};

} // namespace ctcompile
