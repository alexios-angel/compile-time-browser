//===- ctbrowser/lib/Style/engine/cascade_values.hpp - Folding --*- C++ -*-===//
//
// Helpers the cascade (cascade.cpp) uses to bring declared values to their
// computed form: shorthand splitting through the CSSOM grammar, per-property
// length folding and clamping, and viewport and container unit handling.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <ctbrowser/style/css/properties.hpp>

namespace ctbrowser::style::cascade_detail {

/// Splits shorthand `name: text` into its longhands through the CSSOM
/// declaration block's grammar. Returns an empty block for a longhand, a value
/// the grammar refuses, or a shape the block keeps whole.
[[nodiscard]] css::declaration_block split_via_block(std::string_view name, std::string_view text);
/// Computes longhand `name`'s value before inheritance: lengths resolved against
/// `lengths` to canonical text, non-negative properties clamped, and list
/// values folded item by item.
[[nodiscard]] std::string fold_value(std::string text, std::string_view name,
                                     const css::length_context & lengths);
/// Whether `text` holds a viewport-percentage length (CSS Values 4 §6.1.2):
/// `vw` and its kin, including the small, large and dynamic ones.
[[nodiscard]] bool has_viewport_unit(std::string_view text);
/// Replaces every container-relative length in `text` (`cqw` and its kin, CSS
/// Conditional 5 §4.3) with its pixels against `lengths`' query container, or
/// against the small viewport where there is no container on its axis. Returns
/// `text` unchanged when it has none.
[[nodiscard]] std::string resolve_container_units(std::string text,
                                                  const css::length_context & lengths);

} // namespace ctbrowser::style::cascade_detail
