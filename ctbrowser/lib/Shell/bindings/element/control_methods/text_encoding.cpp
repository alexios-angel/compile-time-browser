#include "helpers.hpp"

namespace ctbrowser::shell::detail {

// --- UTF-16 code units over the store's UTF-8 value ---------------------------
//
// The selection API and `textLength` count code units; the store keeps byte
// offsets. A lone surrogate the page wrote is three WTF-8 bytes and one unit.
[[nodiscard]] std::size_t units_before(std::string_view text, std::size_t bytes) {
    std::size_t units = 0;
    const std::size_t stop = std::min(bytes, text.size());
    for (std::size_t at = 0; at < stop;) { units += decode_utf8(text, at) >= 0x10000 ? 2 : 1; }
    return units;
}
[[nodiscard]] std::size_t units_length_of(std::string_view text) {
    return units_before(text, text.size());
}
[[nodiscard]] std::size_t bytes_before(std::string_view text, std::size_t units) {
    std::size_t at = 0;
    std::size_t seen = 0;
    while (at < text.size() && seen < units) {
        const std::size_t here = at;
        const std::size_t width = decode_utf8(text, at) >= 0x10000 ? 2 : 1;
        if (seen + width > units) { return here; } // inside a pair: the pair's start
        seen += width;
    }
    return at;
}

} // namespace ctbrowser::shell::detail
