#pragma once

#include <ctbrowser/style/css/properties.hpp>

namespace ctbrowser::style::cascade_detail {

[[nodiscard]] css::declaration_block split_via_block(std::string_view name, std::string_view text);
[[nodiscard]] std::string fold_value(std::string text, std::string_view name,
                                     const css::length_context & lengths);

} // namespace ctbrowser::style::cascade_detail
