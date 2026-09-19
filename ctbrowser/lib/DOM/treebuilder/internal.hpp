#pragma once

#include <ctbrowser/dom/treebuilder.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/dom/xml.hpp>

#include <span>

namespace ctbrowser::html::treebuilder_detail {

[[nodiscard]] inline bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

[[nodiscard]] inline bool all_whitespace(std::string_view text) {
    return std::ranges::all_of(text, is_whitespace);
}

[[nodiscard]] inline bool one_of(std::string_view tag,
                                 std::initializer_list<std::string_view> names) {
    return std::ranges::find(names, tag) != names.end();
}

[[nodiscard]] inline bool is_heading(std::string_view tag) {
    return tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6';
}

[[nodiscard]] inline bool is_table_section(std::string_view tag) {
    return one_of(tag, {"tbody", "tfoot", "thead"});
}

} // namespace ctbrowser::html::treebuilder_detail
