#pragma once
#include <ctbrowser/core/core.hpp>

#include <ctbrowser/layout/algorithm.hpp>
#include <ctbrowser/layout/box.hpp>
#include <ctbrowser/layout/fragment.hpp>

// POSITIONING, as a pass over the finished fragment tree - CSS 2.1 §9.3 and §10.
//
// WHY IT IS A PASS AND NOT A FORMATTING CONTEXT. Every other piece of layout here
// is a box asking about its own children: block_flow stacks them, flex shares
// space between them, a table shares column widths across them. Positioning is
// the one part of CSS where the box that decides where a child goes is NOT its
// parent - an `absolute` box is placed against the nearest positioned ANCESTOR,
// which may be ten levels up and which has already finished laying out by the
// time the child is reached.
//
// So the flows do the one thing they can: an out-of-flow child reserves no space
// and leaves an EMPTY FRAGMENT where it would have gone. That marker is not
// bookkeeping - it is the STATIC POSITION, which is precisely the number CSS says
// to use for whichever of `top`/`right`/`bottom`/`left` is `auto`. This pass then
// walks down carrying the ancestor chain, and by the time it reaches the marker
// it knows both the containing block and the static position, which is everything
// the placement needs.
//
// Running after layout rather than during it also keeps the parallel driver's
// invariant intact: an out-of-flow box has no effect on any sibling, so the
// concurrent pass never has to know it exists.
//
// WHAT IS DELIBERATELY NOT HERE, each a recorded known difference:
//
//   fixed        placed against the VIEWPORT, correctly - but in the page's own
//                layer, so it scrolls with the page instead of staying put. The
//                fix is a layer of its own (paint/layer.hpp already names the
//                mechanism and the scrollbar layer is the working precedent),
//                which is the second half of this rung.
//   sticky       treated as `relative`, which is what it is until the page
//                scrolls past it. There is no scroll-driven layout here yet.
//   z-index      geometry does not read it; paint/record builds the in-layer
//                stacking-context order after this pass.
//   transform    only `translate(x, y)`, which for a translation alone is an
//                exact offset. A rotation or a scale needs a real transform on
//                the display list.

namespace ctbrowser::layout {

// Move every positioned fragment to where CSS says it goes.
//
// `viewport` is the initial containing block - as wide as the window and as tall
// as the DOCUMENT, which is what an absolutely positioned box with no positioned
// ancestor is placed against. `viewport_height` is the WINDOW's height, and only
// a `fixed` box uses it: that is the whole difference between the two, and
// getting it wrong puts `.fixed-bottom` at the end of the document instead of at
// the bottom of the screen.
void apply_positioning(fragment & root, const rect & viewport, float viewport_height,
                       const measure_text_fn & measure);

} // namespace ctbrowser::layout
