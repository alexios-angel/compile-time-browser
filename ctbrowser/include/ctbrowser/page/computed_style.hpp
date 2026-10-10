//===- ctbrowser/page/computed_style.hpp - Computed style values -*- C++ -*-===//
//
// computed_style_entries: every property of one element as getComputedStyle
// reports it, as (CSS name, value) pairs. The script bindings' CSSStyleDeclaration
// object answers from these, so a caller with no script engine gets the same
// values getComputedStyle(el).getPropertyValue(name) gives in the VM.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <string>
#include <utility>
#include <vector>

#include <ctbrowser/page/view.hpp>

namespace ctbrowser::page {

/// Returns every property of \p id as (CSS name, value) pairs: the property
/// table's longhands sorted by name (the set CSSOM's indexed properties
/// enumerate), then the shorthands that can be reassembled, then declared
/// properties the table does not know (custom properties). \p pseudo, when
/// set, is a pseudo-element's resolved style and the answer is for it. Empty
/// for an element outside the flat tree.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> computed_style_entries(
    const page_view & view, node_id id, const style::computed_style_ptr & pseudo = {});

} // namespace ctbrowser::page
