//===- algorithm.hpp - Formatting-context dispatch ------------ -*- C++ -*-===//
//
// Pulls in the block, inline and table formatting contexts, checks that each
// satisfies the LayoutAlgorithm concept, and declares layout_box, which picks
// the context a box establishes.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctbrowser/layout/algorithm/table.hpp>

namespace ctbrowser::layout {

static_assert(LayoutAlgorithm<table_flow>);
static_assert(LayoutAlgorithm<block_flow>);
static_assert(LayoutAlgorithm<inline_flow>);

/// Lays out `b` with the formatting context it establishes (block, inline,
/// table or flex).
[[nodiscard]] fragment layout_box(const box_node & b, const constraints & c,
                                  const measure_text_fn & measure);

} // namespace ctbrowser::layout
