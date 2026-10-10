//===- source_lines.hpp - Offset to line/column -----------------*- C++ -*-===//
//
// Maps a byte offset in a source to a line and a column, with the newlines
// found once per source rather than once per question.
//
// `function_proto::code_offsets` carries a byte offset per instruction, and
// every consumer of it (a stack trace, the MLIR importer's FileLineColLoc)
// wants a line. Counting newlines from the start of the file for each offset
// is O(offset), which becomes quadratic when a 133 KB bundle with 30,000
// instructions asks 30,000 times. So the newlines are found once and each
// answer is a binary search.
//
// This is a type rather than a method on `program`: a method would have to
// rebuild the table on every call or cache it, and a cache would be a mutable
// field on an object everything else treats as immutable.
//
// The offsets are bytes, not code points. The compiler's offsets are too (a
// lexeme's address minus the source's address), so the two agree, and a
// column here is a byte column. A UTF-8-aware column would disagree with every
// other offset in this engine, which is the worse failure.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

namespace ctbrowser::script {

/// A source's line-start table, answering line and column queries for byte
/// offsets.
class line_table {
public:
    line_table() = default;
    explicit line_table(std::string_view source);

    /// The 1-based line and column of byte `offset`. 1-based because that is
    /// what editors, debuggers and MLIR's FileLineColLoc mean by a line and a
    /// column. An offset past the end clamps to the last line.
    [[nodiscard]] std::uint32_t line_of(std::uint32_t offset) const noexcept;
    [[nodiscard]] std::uint32_t column_of(std::uint32_t offset) const noexcept;

    /// The number of lines; at least 1, even for an empty source.
    [[nodiscard]] std::uint32_t line_count() const noexcept {
        return static_cast<std::uint32_t>(starts_.size());
    }

private:
    /// The offset each line starts at. `starts_[0]` is 0, so a table built from
    /// any source, the empty one included, has at least one line.
    std::vector<std::uint32_t> starts_{0};
    std::uint32_t size_ = 0;
};

} // namespace ctbrowser::script
