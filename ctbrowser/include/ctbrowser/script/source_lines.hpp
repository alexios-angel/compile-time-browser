#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

// A BYTE OFFSET INTO A LINE AND A COLUMN, ONCE PER SOURCE RATHER THAN ONCE PER
// QUESTION.
//
// `function_proto::code_offsets` carries a byte offset per instruction, and
// every consumer of it - a stack trace, the MLIR importer's FileLineColLoc -
// wants a line. Counting newlines from the start of the file to answer one
// offset is O(offset), which is fine once and quadratic when a 133 KB bundle
// with 30,000 instructions asks 30,000 times.
//
// So the newlines are found ONCE and the answer is a binary search. That is
// also why this is a type rather than a method on `program`: a method would
// have to either rebuild the table on every call or cache it, and a cache
// inside a compiled artefact is a mutable field on a thing everything else
// treats as immutable.
//
// The offsets are BYTES, not code points. The compiler's are too - a lexeme's
// address minus the source's address - so the two agree, and a column here is a
// byte column. A UTF-8 aware column would disagree with every other offset in
// this engine, which is the worse failure.

namespace ctbrowser::script {

class line_table {
public:
    line_table() = default;
    explicit line_table(std::string_view source);

    // 1-BASED, both of them, because that is what every editor, every debugger
    // and MLIR's FileLineColLoc mean by a line and a column. An offset past the
    // end clamps to the last line rather than reading off it.
    [[nodiscard]] std::uint32_t line_of(std::uint32_t offset) const noexcept;
    [[nodiscard]] std::uint32_t column_of(std::uint32_t offset) const noexcept;

    [[nodiscard]] std::uint32_t line_count() const noexcept {
        return static_cast<std::uint32_t>(starts_.size());
    }

private:
    // The offset each line STARTS at. `starts_[0]` is 0, so a table built from
    // any source - the empty one included - has at least one line.
    std::vector<std::uint32_t> starts_{0};
    std::uint32_t size_ = 0;
};

} // namespace ctbrowser::script
