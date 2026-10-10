//===- overflow.hpp - Scrollable overflow --------------------- -*- C++ -*-===//
//
// The scrollable overflow of a laid-out box (CSS Overflow 3 §3.3) and the
// scrolling area CSSOM View §2 builds on it: what `scrollWidth`,
// `scrollHeight` and the clamp on a scroll offset are made of.
//
// This is a pass over the finished fragments rather than a number layout
// writes onto them, for the reason position.hpp gives: the question is about
// descendants a formatting context never sees, and a fragment is the one place
// all of them meet. Nothing here changes a fragment.
//
// One writing mode: the overflow directions are rightward and downward, so
// the scrolling area's origin is the box's top-left padding edge and only the
// right and bottom edges grow, except in a flex container whose main or cross
// axis runs backwards, where the origin is the right or bottom edge and the
// area grows the other way (overflow.cpp's origin_of).
//
//===----------------------------------------------------------------------===//
#pragma once
#include <ctbrowser/core/core.hpp>

#include <ctbrowser/layout/algorithm.hpp>
#include <ctbrowser/layout/fragment.hpp>

namespace ctbrowser::layout {

/// The box's edges, resolved the way the positioning pass resolves them (a
/// percentage against the fragment's own width). Zero for a fragment with no
/// box, such as a line or a generated piece.
[[nodiscard]] resolved_edges edges_of(const fragment & f) noexcept;

/// The padding box, in the fragment's own border-box coordinates.
[[nodiscard]] rect padding_box_of(const fragment & f) noexcept;

/// The scrolling area of the fragment's box, in its own border-box coordinates.
///
/// Its top-left is the padding edge; its right and bottom are the furthest of
/// the padding edge and every descendant's contribution (a border box, a flex
/// item's margin box, a descendant's own scrollable overflow unless that
/// descendant clips it). Out-of-flow boxes whose containing block is an
/// ancestor are excluded. A scroll container's end-side padding is added past
/// its in-flow content so a scroll to the end shows it (the padding rule of
/// CSS Overflow 3 §3.3).
[[nodiscard]] rect scrolling_area_of(const fragment & f) noexcept;

/// The viewport's scrolling area given the document's fragment tree: the
/// initial containing block united with everything the document's boxes
/// reach. This is what the root element's scrollWidth/scrollHeight report.
[[nodiscard]] rect viewport_scrolling_area(const fragment & document, float viewport_width,
                                           float viewport_height) noexcept;

} // namespace ctbrowser::layout
