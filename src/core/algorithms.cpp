#include <ctbrowser/core/algorithms.hpp>

#include <boost/algorithm/string/predicate.hpp>

#include <locale>

namespace ctbrowser {

bool ascii_iequals(std::string_view a, std::string_view b) noexcept {
    // THE LOCALE ARGUMENT IS THE POINT. Without it boost::iequals uses
    // std::locale(), the global one, and the answer starts depending on the
    // host - which a repository that byte-compares renders cannot have.
    return boost::algorithm::iequals(a, b, std::locale::classic());
}

void ascii_lower_in_place(std::string & text) noexcept {
    for (char & c : text) { c = ascii_lower(c); }
}

std::string ascii_lower_copy(std::string_view text) {
    std::string out{text};
    ascii_lower_in_place(out);
    return out;
}

} // namespace ctbrowser
