#pragma once
#include <ctbrowser/core/core.hpp>

#include <ctbrowser/layout/algorithm.hpp>
#include <ctbrowser/layout/box.hpp>
#include <ctbrowser/layout/fragment.hpp>
#include <ctbrowser/layout/values.hpp>

// The flex formatting context - CSS Flexible Box Layout Level 1 §9.
//
// A FOURTH FORMATTING CONTEXT, arriving as a new TYPE rather than as new
// branches in an existing one, which is the whole reason algorithm.hpp made a
// formatting context a concept in the first place. It gets its own header
// because it is as long as the other three together: the freeze loop alone is
// three passes with a violation ledger, and burying that inside a 600-line
// header would make both harder to read.
//
// WHY IT IS THE DENSEST ALGORITHM HERE. Every other context sizes a box from
// itself: a block takes its width from its containing block, a table column from
// the widest thing in it. A flex item's size is a function of its SIBLINGS -
// free space is shared out, a min-width violation on one item is redistributed
// among the others, and that redistribution can create a new violation. §9.7 is
// a fixed-point iteration and there is no single-pass version of it that is
// right. Bootstrap's grid is the proof: `.col { flex: 1 0 0 }` has a base size of
// zero, so three of them in a 960px row are 320 each - but only after each has
// been clamped to `[min-content, 960]` and the loop has confirmed nobody moved.
//
// WHAT IS DELIBERATELY NOT HERE, each recorded as a known difference in
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

struct flex_flow {
    // How wide the container's content wants to be. Not decoration: a flex
    // container can be a table cell, an inline-flex box that must shrink to fit,
    // or an item of another flex container - and all three ask this before
    // anything is placed.
    [[nodiscard]] intrinsic_sizes measure(const box_node & b, const constraints & c,
                                          const measure_text_fn & measure_text) const;

    // `ready` is accepted and IGNORED, like table_flow's. The parallel driver
    // never hands a flex container precomputed children and must not: they are
    // not independent, which is the invariant engine::split_point enforces.
    [[nodiscard]] fragment arrange(const box_node & b, const constraints & c,
                                   const measure_text_fn & measure_text,
                                   precomputed * ready = nullptr) const;
};

static_assert(LayoutAlgorithm<flex_flow>);

} // namespace ctbrowser::layout
