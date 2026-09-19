#include <ctbrowser/core/algorithms.hpp>

#include <algorithm>
#include <array>

namespace ctbrowser {
namespace {

struct lowercase_range {
    char16_t first;
    char16_t last;
    std::uint16_t stride;
    std::int32_t delta;
};

#include "unicode_lowercase.inc"

} // namespace

std::u16string unicode_lowercase_unit(char16_t unit) {
    // The only expanding default lowercase mapping for a single UTF-16 unit.
    if (unit == u'\u0130') { return u"i\u0307"; }
    const auto range = std::ranges::lower_bound(lowercase_ranges, unit, {}, &lowercase_range::last);
    if (range != lowercase_ranges.end() && unit >= range->first &&
        (unit - range->first) % range->stride == 0) {
        unit = static_cast<char16_t>(static_cast<std::int32_t>(unit) + range->delta);
    }
    return std::u16string(1, unit);
}

} // namespace ctbrowser
