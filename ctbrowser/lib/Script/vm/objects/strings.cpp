// string_object's UTF-16 view - its marks, and the two walks that use them.
// value/base.hpp says what the marks are; script/utf16.hpp is the walk.

#include <algorithm>

#include <ctbrowser/script/value.hpp>

namespace ctbrowser::script {

const utf16_mark * string_object::marks() const {
    // A string past 4 GiB has none, and every walk starts from the front: the
    // marks are 32-bit, and a string that size is not one a page indexes.
    if (marks_ || text.size() > 0xFFFFFFFFu) { return marks_.get(); }
    const std::size_t count = length() / utf16_stride + 1;
    auto made = std::make_unique<utf16_mark[]>(count);
    std::size_t next = 0;
    std::size_t unit = 0;
    for (std::size_t byte = 0; byte < text.size() && next < count;) {
        const std::size_t start = byte;
        const std::size_t n = utf16::units_of(decode_utf8(text, byte));
        for (; next < count && next * utf16_stride < unit + n; ++next) {
            made[next] = {static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(unit)};
        }
        unit += n;
    }
    // A length that is a multiple of the stride has its last mark at the end.
    for (; next < count; ++next) {
        made[next] = {static_cast<std::uint32_t>(text.size()), static_cast<std::uint32_t>(unit)};
    }
    marks_ = std::move(made);
    return marks_.get();
}

utf16::position string_object::locate(std::size_t i) const {
    if (bytewise()) { return {std::min(i, text.size()), false}; }
    if (const utf16_mark * m = marks()) {
        const utf16_mark & from = m[std::min(i, length()) / utf16_stride];
        return utf16::locate(text, i, from.byte, from.unit);
    }
    return utf16::locate(text, i);
}

std::size_t string_object::unit_index(std::size_t byte) const {
    if (bytewise()) { return std::min(byte, text.size()); }
    if (const utf16_mark * m = marks()) {
        // The last mark at or before the byte: marks are in byte order.
        const std::size_t count = length() / utf16_stride + 1;
        const utf16_mark * after =
            std::upper_bound(m, m + count, byte,
                             [](std::size_t b, const utf16_mark & mark) { return b < mark.byte; });
        const utf16_mark & from = after == m ? m[0] : after[-1];
        return utf16::units_before(text, byte, from.byte, from.unit);
    }
    return utf16::units_before(text, byte);
}

} // namespace ctbrowser::script
