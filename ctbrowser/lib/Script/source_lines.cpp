//===- source_lines.cpp - Source offset to line and column ------*- C++ -*-===//
//
// line_table: maps a byte offset in a script's source to a 1-based line and
// column. See ctbrowser/script/source_lines.hpp for why it is a type.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/script/source_lines.hpp>

#include <algorithm>

namespace ctbrowser::script {

line_table::line_table(std::string_view source) : size_(static_cast<std::uint32_t>(source.size())) {
    starts_.reserve(source.size() / 32 + 1); // About 32 bytes per line in real script.
    for (std::size_t i = 0; i < source.size(); ++i) {
        // Only '\n' ends a line here. A lone CR, U+2028 and U+2029 are
        // JavaScript line terminators too, but `compiler::compile`'s
        // parse-error position counts only '\n', and two line numberings that
        // disagree are worse than one that is approximate on rare sources.
        if (source[i] == '\n') { starts_.push_back(static_cast<std::uint32_t>(i + 1)); }
    }
}

std::uint32_t line_table::line_of(std::uint32_t offset) const noexcept {
    const std::uint32_t clamped = std::min(offset, size_);
    // upper_bound gives the first line starting after the offset; the line the
    // offset is on is the one before it, and there is always one because
    // starts_[0] is 0.
    const auto after = std::upper_bound(starts_.begin(), starts_.end(), clamped);
    return static_cast<std::uint32_t>(after - starts_.begin());
}

std::uint32_t line_table::column_of(std::uint32_t offset) const noexcept {
    const std::uint32_t clamped = std::min(offset, size_);
    return clamped - starts_[line_of(clamped) - 1] + 1;
}

} // namespace ctbrowser::script
