//===- flex.hpp - The flex formatting context ----------------- -*- C++ -*-===//
//
// The flex formatting context (CSS Flexible Box Layout Level 1 §9), in its
// own type (the LayoutAlgorithm concept, algorithm/common.hpp) and its own
// header, because it is as long as block, inline and table together: the
// freeze loop alone is three passes with a violation ledger.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <ctbrowser/core/core.hpp>

#include <ctbrowser/layout/algorithm.hpp>
#include <ctbrowser/layout/box.hpp>
#include <ctbrowser/layout/fragment.hpp>
#include <ctbrowser/layout/values.hpp>

// Why it is the densest algorithm here: every other context sizes a box from
// itself. A block takes its width from its containing block, a table column
// from the widest thing in it. A flex item's size depends on its siblings:
// free space is shared out, a min-width violation on one item is
// redistributed among the others, and that redistribution can create a new
// violation. §9.7 is a fixed-point iteration with no correct single-pass
// version. Bootstrap's grid shows it: `.col { flex: 1 0 0 }` has a base size of
// zero, so three of them in a 960px row are 320 each, but only after each has
// been clamped to `[min-content, 960]` and the loop has confirmed nothing moved.
//
// Deliberately not implemented, each recorded as a known difference in
// docs/plans/bootstrap.md rather than approximated:
//
//   flex-basis: content     Bootstrap never writes it; treated as `auto`
//   baseline alignment      utility-only, no component uses it. inline_flow
//                           already shares a baseline, so it is mechanical later
//   absolutely positioned   excluded from flex sizing, emitted at their static
//                           marker, then placed by the positioning pass
//   a % basis against an    it behaves as `auto`, which is what §9.2.3 says to
//   indefinite main size    do when the percentage cannot resolve

namespace ctbrowser::layout {

/// The flex formatting context.
struct flex_flow {
    /// How wide the container's content wants to be. A flex container can be a
    /// table cell, an inline-flex box that must shrink to fit, or an item of
    /// another flex container, and all three ask this before anything is
    /// placed.
    [[nodiscard]] intrinsic_sizes measure(const box_node & b, const constraints & c,
                                          const measure_text_fn & measure_text) const;

    /// Sizes and places the container's items (§9.2 to §9.9).
    [[nodiscard]] fragment arrange(const box_node & b, const constraints & c,
                                   const measure_text_fn & measure_text) const;
};

static_assert(LayoutAlgorithm<flex_flow>);

} // namespace ctbrowser::layout
