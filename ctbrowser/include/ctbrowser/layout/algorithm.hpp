#pragma once

#include <ctbrowser/layout/algorithm/table.hpp>

namespace ctbrowser::layout {

static_assert(LayoutAlgorithm<table_flow>);
static_assert(LayoutAlgorithm<block_flow>);
static_assert(LayoutAlgorithm<inline_flow>);

// Dispatch: pick the formatting context this box establishes.
[[nodiscard]] fragment layout_box(const box_node & b, const constraints & c,
                                  const measure_text_fn & measure);

} // namespace ctbrowser::layout
