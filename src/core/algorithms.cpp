#include <ctbrowser/core/algorithms.hpp>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <locale>

namespace ctbrowser {

bool ascii_iequals(std::string_view a, std::string_view b) noexcept {
    // THE LOCALE ARGUMENT IS THE POINT. Without it boost::iequals uses
    // std::locale(), the global one, and the answer starts depending on the
    // host - which a repository that byte-compares renders cannot have.
    return boost::algorithm::iequals(a, b, std::locale::classic());
}

// Boost's, with the classic locale for the same reason as above - and the same
// trap avoided: the default overloads take std::locale(), the global one.
void ascii_lower_in_place(std::string & text) noexcept {
    boost::algorithm::to_lower(text, std::locale::classic());
}

std::string ascii_lower_copy(std::string_view text) {
    return boost::algorithm::to_lower_copy(std::string{text}, std::locale::classic());
}

void ascii_upper_in_place(std::string & text) noexcept {
    boost::algorithm::to_upper(text, std::locale::classic());
}

std::string ascii_upper_copy(std::string_view text) {
    return boost::algorithm::to_upper_copy(std::string{text}, std::locale::classic());
}

} // namespace ctbrowser
