#include <ctbrowser/layout/flex.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

// flex: the function bodies.
// The header says what this computes and what it deliberately does not; this
// says how, in the order Flexbox 1 §9 says to do it.

namespace ctbrowser::layout {
namespace {

// "This size is not known." A container's main size is indefinite when it is a
// `column` with no stated height, and a flex line cannot be filled against a
// size nobody has yet - which is why an indefinite main size means no flexing at
// all rather than flexing against zero.
//
// Spelled once here because `constraints::available_height` already uses 0 for
// "as tall as it needs to be" and a second 0-means-unknown would be a trap: a
// container really can be 0 tall.
constexpr float indefinite = -1.0f;

[[nodiscard]] constexpr bool is_definite(float v) noexcept {
    return v >= 0;
}

// MAX FIRST, THEN MIN, because min wins - the same order and the same reason as
// outer_width_of (src/layout/algorithm.cpp): a box whose minimum exceeds its
// maximum takes the minimum, which CSS 2.1 §10.4 says outright and which this
// order produces with no special case. `max < 0` means there is no maximum.
[[nodiscard]] float clamp_extent(float value, float min, float max) {
    return std::max(min, max < 0 ? value : std::min(value, max));
}

// One flex item, carried through every pass. The algorithm is a sequence of
// passes and each needs what the one before it wrote, so this is one struct
// rather than eight parallel arrays indexed by the same integer.
struct flex_item {
    const box_node * box = nullptr;
    constraints c;         // the item's containing block, for its own percentages
    resolved_edges edges;  // resolved against that
    float main_margin = 0; // both margins on the main axis, autos counted as 0
    float cross_margin = 0;
    float main_padding = 0;
    // The margins at the FLEX-START edge of each axis - which is the right margin
    // under `row-reverse` and the bottom one under `wrap-reverse`, not the left
    // and the top. See where these are filled in.
    float main_lead = 0;
    float cross_lead = 0;
    bool main_start_auto = false; // flex-relative, like the two above
    bool main_end_auto = false;
    bool cross_start_auto = false;
    bool cross_end_auto = false;

    float base = 0;         // the flex base size, main axis, border box
    float hypothetical = 0; // that, clamped
    float target = 0;       // what the freeze loop settled on
    float min_main = 0;
    float max_main = indefinite;
    float grow = 0;
    float shrink = 1;
    bool frozen = false;
    float violation = 0;

    fragment placed;
    float cross = 0; // the item's border-box size on the cross axis
};

// `align-self`, resolved against the container's `align-items`.
//
// `auto` defers to the container and `normal` behaves as `stretch` in a flex
// container - the two spellings of "nothing was said" arriving from two
// properties. BASELINE ALIGNMENT IS NOT IMPLEMENTED: §8.3 already says an item
// whose baseline cannot be determined aligns to flex-start, so answering that is
// the honest subset rather than an approximation, and it is recorded as a known
// difference in docs/plans/bootstrap.md.
[[nodiscard]] flex_align resolved_align(const box_node & container, const box_node & item) {
    flex_align out = item.flex.self;
    if (out == flex_align::auto_) { out = container.flex.items; }
    if (out == flex_align::normal || out == flex_align::auto_) { out = flex_align::stretch; }
    if (out == flex_align::baseline) { out = flex_align::flex_start; }
    return out;
}

// A COLUMN CONTAINER'S ITEM IS SIZED ACROSS FIRST, and that asymmetry is why the
// two axes are not one shared code path. How TALL a box is depends on how WIDE
// it is, because its text wraps - so down a column the cross size has to be
// settled before the main size can even be asked for, while across a row the
// main size comes first and the cross size falls out of laying the item out.
[[nodiscard]] float column_cross_size(const box_node & container, const flex_item & it,
                                      float content_width, const measure_text_fn & measure_text) {
    const box_node & child = *it.box;
    float size = 0;
    if (!child.width.is_auto()) {
        size = child.width.resolve(content_width, child.font_size);
    } else if (container.flex.wrap == flex_wrap::nowrap &&
               resolved_align(container, child) == flex_align::stretch && !it.cross_start_auto &&
               !it.cross_end_auto) {
        // STRETCH ONLY WHEN THERE IS ONE LINE, because a stretched item is as wide
        // as its LINE and the lines do not exist yet. Pre-stretching every item to
        // the whole container made each line as wide as the container, so a
        // two-line column put its second line entirely outside itself. Where there
        // may be several lines the item takes its content size here and the
        // placement pass stretches it to whatever line it landed on - which is the
        // same order the row axis uses, and it is why that axis never had this bug.
        size = content_width - it.cross_margin;
    } else {
        // Shrink to fit, the same clamp an inline-flex container takes: an item
        // that is not stretched is as wide as its content wants within the room
        // it has.
        const intrinsic_sizes wants = measure_box(child, it.c, measure_text);
        const float room =
            std::max(0.0f, content_width - it.cross_margin - it.edges.horizontal_inner());
        size = std::max(wants.min_content, std::min(room, wants.max_content)) +
               it.edges.horizontal_inner();
    }
    const float min =
        child.min_width.is_auto() ? 0.0f : child.min_width.resolve(content_width, child.font_size);
    const float max = child.max_width.is_auto()
                          ? indefinite
                          : child.max_width.resolve(content_width, child.font_size);
    return std::max(0.0f, clamp_extent(size, min, max));
}

// Where a run starts, and how much space goes between its members, for one of
// the six distribution values. `justify-content` distributes ITEMS along a line
// and `align-content` distributes LINES across the container by the same six
// rules, which is why they share an enum and share this.
struct distribution {
    float start = 0;
    float between = 0;
};

[[nodiscard]] distribution distribute(flex_align how, float free, std::size_t count) {
    if (count == 0) { return {}; }
    // OVERFLOW FALLS BACK TO START for the three space-* values, per §8.2. Left
    // alone, `space-between` on a negative free space would give a negative gap:
    // the items would overlap each other while the first one still sat at the
    // start, which is worse in both directions at once.
    const bool spacing = how == flex_align::space_between || how == flex_align::space_around ||
                         how == flex_align::space_evenly;
    if (free < 0 && spacing) { return {}; }
    switch (how) {
    case flex_align::flex_end: return {free, 0};
    case flex_align::center: return {free / 2, 0};
    case flex_align::space_between:
        return count > 1 ? distribution{0, free / static_cast<float>(count - 1)} : distribution{};
    case flex_align::space_around: {
        const float each = free / static_cast<float>(count);
        return {each / 2, each};
    }
    case flex_align::space_evenly: {
        const float each = free / static_cast<float>(count + 1);
        return {each, each};
    }
    case flex_align::auto_:
    case flex_align::normal:
    case flex_align::flex_start:
    case flex_align::baseline:
    case flex_align::stretch: break; // start alignment; stretch is the caller's
    }
    return {};
}

// THE INNER FLEX BASE SIZE - the CONTENT box, padding excluded. §9.7.4 weights
// an item's share of a shrink by this and not by its border box, and the two are
// a real distance apart here because every size in this engine names the border
// box: two 200px items shrinking into 300px are 150/150 only if neither is
// padded, and 166.67/133.33 if one of them has 100px of it. Blink calls the same
// quantity flex_base_content_size for the same reason.
//
// It is NOT the same question as `box-sizing`, which is about what the AUTHOR's
// `width` names; this is fixed by the spec whatever that says.
[[nodiscard]] float inner_base(const flex_item & it) {
    return std::max(0.0f, it.base - it.main_padding);
}

// §9.7, RESOLVING THE FLEXIBLE LENGTHS of one line.
//
// A FIXED-POINT ITERATION, and there is no correct single-pass version of it.
// Growing three `.col`s from a flex base size of zero in a 960px row gives 320
// each - but only after each has been clamped to its own [min-content, max-width]
// and the loop has confirmed that nobody's clamp handed free space back for the
// others to re-share. One proportional pass gets the common case right and the
// interesting case wrong, silently.
void resolve_flexible_lengths(std::vector<flex_item> & items, std::span<const std::size_t> line,
                              float line_main, float main_gap) {
    const float gaps = line.size() > 1 ? main_gap * static_cast<float>(line.size() - 1) : 0.0f;
    float hypothetical_total = gaps;
    for (const std::size_t i : line) {
        hypothetical_total += items[i].hypothetical + items[i].main_margin;
    }
    // WHICH FACTOR IS IN PLAY is decided once, from the hypothetical sizes, and
    // never revisited. Re-deciding it per pass would let a line that overshoots
    // while growing start shrinking, then overshoot the other way, and never
    // converge.
    const bool growing = hypothetical_total < line_main;

    for (const std::size_t i : line) {
        flex_item & it = items[i];
        const float factor = growing ? it.grow : it.shrink;
        // An item already past its clamp in the direction it would flex is frozen
        // at the clamped size: flexing it further would only be undone below.
        it.frozen = factor == 0 || (growing && it.base > it.hypothetical) ||
                    (!growing && it.base < it.hypothetical);
        it.target = it.frozen ? it.hypothetical : it.base;
    }

    // §9.7.3, THE INITIAL FREE SPACE: computed ONCE, from the state the pre-freeze
    // above left behind, and never recomputed. The fraction a sub-one factor sum
    // takes below is a fraction of THIS, not of what is left after some other item
    // hit its clamp - scaling the remainder instead compounds the shrinkage on
    // every pass, so `flex-grow: .25` beside an item that froze at its max-width
    // came out 225 where Chrome says 250.
    float initial_free = line_main - gaps;
    for (const std::size_t i : line) {
        const flex_item & it = items[i];
        initial_free -= it.main_margin;
        initial_free -= it.frozen ? it.target : it.base;
    }

    for (;;) {
        float remaining = line_main - gaps;
        float factor_sum = 0;
        float scaled_shrink_sum = 0;
        std::size_t unfrozen = 0;
        for (const std::size_t i : line) {
            const flex_item & it = items[i];
            remaining -= it.main_margin;
            remaining -= it.frozen ? it.target : it.base;
            if (it.frozen) { continue; }
            ++unfrozen;
            factor_sum += growing ? it.grow : it.shrink;
            scaled_shrink_sum += inner_base(it) * it.shrink;
        }
        if (unfrozen == 0) { return; }

        // §9.7.4: when the unfrozen flex factors sum to less than one, only that
        // FRACTION of the INITIAL free space is distributed - which is what makes
        // `flex-grow: .5` take half the room rather than all of it - and only when
        // that is the SMALLER of the two, which is what stops it overshooting once
        // other items have frozen.
        float free = remaining;
        if (factor_sum < 1) {
            const float scaled = initial_free * factor_sum;
            if (std::fabs(scaled) < std::fabs(remaining)) { free = scaled; }
        }

        float total_violation = 0;
        for (const std::size_t i : line) {
            flex_item & it = items[i];
            if (it.frozen) { continue; }
            float target = it.base;
            if (factor_sum > 0 && free != 0) {
                if (growing) {
                    target = it.base + free * it.grow / factor_sum;
                } else if (scaled_shrink_sum > 0) {
                    // SHRINKING IS WEIGHTED BY THE BASE SIZE and growing is not.
                    // Two items with the same `flex-shrink` do not give up the
                    // same number of pixels, they give up the same PROPORTION -
                    // which is what stops a narrow item collapsing to nothing
                    // beside a wide one.
                    target = it.base + free * (inner_base(it) * it.shrink) / scaled_shrink_sum;
                }
            }
            const float clamped = clamp_extent(target, it.min_main, it.max_main);
            it.violation = clamped - target;
            it.target = clamped;
            total_violation += it.violation;
        }

        if (total_violation == 0) {
            for (const std::size_t i : line) { items[i].frozen = true; }
            return;
        }
        // Freeze the items that hit a clamp in the direction the total went. What
        // they gave back is what the rest share on the next pass, and that is the
        // whole of why this is a loop.
        std::size_t froze = 0;
        for (const std::size_t i : line) {
            flex_item & it = items[i];
            if (it.frozen) { continue; }
            if (total_violation > 0 ? it.violation > 0 : it.violation < 0) {
                it.frozen = true;
                ++froze;
            }
        }
        // A non-zero total with nothing to freeze is arithmetically impossible -
        // the total IS the sum of the violations - but a float sum of
        // opposite-signed terms can land on a value no individual term's sign
        // matches. Freezing everything terminates; spinning here does not.
        if (froze == 0) {
            for (const std::size_t i : line) { items[i].frozen = true; }
            return;
        }
    }
}

} // namespace

intrinsic_sizes flex_flow::measure(const box_node & b, const constraints & c,
                                   const measure_text_fn & measure_text) const {
    const resolved_edges edges = resolve_edges(b, c);
    // The basis an item's own percentages resolve against. Approximate when the
    // container's width is itself being measured, which is what makes a
    // percentage-sized item contribute its content size instead - see below.
    const float basis = std::max(0.0f, c.available_width - edges.horizontal_inner());
    const bool horizontal = b.flex.horizontal();
    const float gap = std::max(
        0.0f, (horizontal ? b.flex.column_gap : b.flex.row_gap).resolve(basis, b.font_size));

    intrinsic_sizes out;
    float sum_min = 0;
    float sum_max = 0;
    std::size_t counted = 0;
    for (const box_node & child : b.children) {
        const constraints child_c{basis, c.available_height, child.font_size};
        const resolved_edges child_edges = resolve_edges(child, child_c);
        // WHAT THE ITEM CONTRIBUTES, asked in the one place that answers it for
        // every parent: its own border box plus its margins, with a stated `width`
        // and its min/max clamps already applied.
        intrinsic_sizes item = outer_intrinsic(child, child_c, measure_text);
        float item_min = item.min_content;
        float item_max = item.max_content;
        const float outside = child_edges.horizontal_margin();
        // A DEFINITE FLEX BASIS OVERRIDES THE `width` outer_intrinsic honoured,
        // because that is where the freeze loop will start - `flex: 0 0 100px` on
        // a `width: 50%` item is 100px wide. Only across a ROW: down a column the
        // basis is a HEIGHT and says nothing about the width, which is the cross
        // size column_cross_size will take from `width` instead.
        //
        // A percentage has no answer while the container's own width is the
        // question being asked, so there the content size stands.
        if (horizontal && !child.flex.basis.is_auto() && child.flex.basis.u != unit::percent) {
            const float stated = std::max(0.0f, child.flex.basis.resolve(basis, child.font_size));
            item_max = stated + outside;
            // A SHRINKABLE item can still give back everything down to its
            // automatic minimum, so its minimum contribution is the smaller of the
            // two. An inflexible one is its base size and nothing else.
            item_min =
                child.flex.shrink > 0 ? std::min(item_min, stated + outside) : stated + outside;
        }
        // A GROWABLE item is not capped by its base size: `flex: 1 0 0` has a base
        // of ZERO and would otherwise contribute nothing at all, which collapsed an
        // inline-flex container full of `.col`s to width 0. The spec's own answer
        // (§9.9.1) is the LINE's max-content flex fraction, which needs a second
        // pass over the line; the larger of the two is the same answer whenever one
        // item dominates and an under-estimate otherwise, and it is recorded as a
        // known difference rather than left as a zero.
        if (horizontal && child.flex.grow > 0) {
            const intrinsic_sizes content = measure_box(child, child_c, measure_text);
            item_max =
                std::max(item_max, content.max_content + child_edges.horizontal_inner() + outside);
        }
        sum_min += item_min;
        sum_max += item_max;
        out.min_content = std::max(out.min_content, item_min);
        out.max_content = std::max(out.max_content, item_max);
        ++counted;
    }
    if (!horizontal) { return out; } // a column stacks: the widest item is the width

    const float gaps = counted > 1 ? gap * static_cast<float>(counted - 1) : 0.0f;
    // A WRAPPING container's minimum is its widest item, because it may put every
    // item on its own line; a nowrap container's is the sum, because it may not.
    if (b.flex.wrap == flex_wrap::nowrap) { out.min_content = sum_min + gaps; }
    out.max_content = sum_max + gaps;
    return out;
}

fragment flex_flow::arrange(const box_node & b, const constraints & c,
                            const measure_text_fn & measure_text, precomputed *) const {
    const bool horizontal = b.flex.horizontal();
    const resolved_edges edges = resolve_edges(b, c);

    // --- 1. THE CONTAINER'S OWN BOX ---------------------------------------
    //
    // Everything but `inline-flex` goes through the same outer_width_of every
    // other formatting context uses, which is what makes `.row`'s -12px side
    // margins widen it to 960 inside a 936 container in exactly one place rather
    // than in a flex-shaped copy of that arithmetic.
    float outer_width = 0;
    if (b.inline_level && b.width.is_auto() && c.forced_width < 0) {
        // SHRINK TO FIT, through the shared helper - the one thing `inline-flex`
        // does differently from `flex`, and exactly what `inline-block` does
        // differently from `block`. Two copies of that clamp would be two answers
        // to one question, and the block one is the same code.
        outer_width = shrink_to_fit_width(b, c, edges, measure_text);
    } else {
        outer_width = outer_width_of(b, c, edges);
    }
    const float content_width = std::max(0.0f, outer_width - edges.horizontal_inner());
    // A STATED HEIGHT IS THE BORDER BOX, the same as a stated width - which is
    // what block_flow::arrange already means when it assigns the resolved height
    // straight to the fragment and starts its cursor at pad_top.
    const float outer_height =
        has_definite_height(b, c)
            ? std::max(0.0f, b.height.resolve(c.available_height, b.font_size))
            : indefinite;
    const float content_height = is_definite(outer_height)
                                     ? std::max(0.0f, outer_height - edges.vertical_inner())
                                     : indefinite;

    const float inner_main = horizontal ? content_width : content_height;
    const float inner_cross = horizontal ? content_height : content_width;
    const float main_gap =
        std::max(0.0f, (horizontal ? b.flex.column_gap : b.flex.row_gap)
                           .resolve(is_definite(inner_main) ? inner_main : 0, b.font_size));
    const float cross_gap =
        std::max(0.0f, (horizontal ? b.flex.row_gap : b.flex.column_gap)
                           .resolve(is_definite(inner_cross) ? inner_cross : 0, b.font_size));

    fragment out;
    out.box = &b;
    out.source = b.source;

    // --- 2. THE ITEMS ------------------------------------------------------
    const float child_height = is_definite(content_height) ? content_height : 0.0f;
    std::vector<flex_item> items;
    items.reserve(b.children.size());
    for (const box_node & child : b.children) {
        // AN OUT-OF-FLOW CHILD IS NOT A FLEX ITEM (§4): it takes no part in the
        // free-space arithmetic at all. It leaves an empty fragment at its static
        // position, exactly as it does in a block container, and
        // layout::apply_positioning places it once the ancestor chain is known.
        //
        // Its static position is the flex container's content origin rather than
        // a cursor, because a flex container has no cursor to be partway along -
        // §4 says as much, and it is what Chrome does.
        if (child.is_out_of_flow()) { continue; }
        flex_item it;
        it.box = &child;
        it.c = constraints{content_width, child_height, child.font_size};
        it.edges = resolve_edges(child, it.c);
        it.main_margin = horizontal ? it.edges.horizontal_margin()
                                    : it.edges.margin_top + it.edges.margin_bottom;
        it.cross_margin = horizontal ? it.edges.margin_top + it.edges.margin_bottom
                                     : it.edges.horizontal_margin();
        it.main_padding = horizontal ? it.edges.horizontal_inner() : it.edges.vertical_inner();
        // THE LEADING MARGIN IS THE ONE AT THE FLEX-START EDGE, which under
        // `row-reverse` is the RIGHT margin and under `wrap-reverse` the BOTTOM
        // one. Naming them physically instead put the free space an auto margin
        // absorbs on the wrong side of its item, and offset every reversed item by
        // exactly (trailing margin - leading margin) - invisible whenever the two
        // are equal, which is why a symmetric test does not catch it.
        const bool reversed = b.flex.reversed();
        const bool cross_reversed = b.flex.wrap == flex_wrap::wrap_reverse;
        it.main_lead = horizontal ? (reversed ? it.edges.margin_right : it.edges.margin_left)
                                  : (reversed ? it.edges.margin_bottom : it.edges.margin_top);
        it.cross_lead = horizontal
                            ? (cross_reversed ? it.edges.margin_bottom : it.edges.margin_top)
                            : (cross_reversed ? it.edges.margin_right : it.edges.margin_left);
        it.main_start_auto = horizontal
                                 ? (reversed ? child.margin_right_auto : child.margin_left_auto)
                                 : (reversed ? child.margin_bottom_auto : child.margin_top_auto);
        it.main_end_auto = horizontal
                               ? (reversed ? child.margin_left_auto : child.margin_right_auto)
                               : (reversed ? child.margin_top_auto : child.margin_bottom_auto);
        it.cross_start_auto =
            horizontal ? (cross_reversed ? child.margin_bottom_auto : child.margin_top_auto)
                       : (cross_reversed ? child.margin_right_auto : child.margin_left_auto);
        it.cross_end_auto =
            horizontal ? (cross_reversed ? child.margin_top_auto : child.margin_bottom_auto)
                       : (cross_reversed ? child.margin_left_auto : child.margin_right_auto);
        it.grow = child.flex.grow;
        it.shrink = child.flex.shrink;
        items.push_back(std::move(it));
    }
    // `order`, as a STABLE sort - and the stability is the property rather than
    // an implementation detail. Two items with the same order value keep their
    // document order, which is the whole of what `order: 1` on one of three
    // columns means.
    std::stable_sort(items.begin(), items.end(), [](const flex_item & x, const flex_item & y) {
        return x.box->flex.order < y.box->flex.order;
    });

    // --- 3. FLEX BASE SIZES AND HYPOTHETICAL MAIN SIZES (§9.2) -------------
    for (flex_item & it : items) {
        const box_node & child = *it.box;
        const length & preferred = horizontal ? child.width : child.height;
        const length & min_len = horizontal ? child.min_width : child.min_height;
        const length & max_len = horizontal ? child.max_width : child.max_height;
        // A percentage on the main axis needs a definite main size to resolve
        // against; without one it behaves as `auto` (§9.2.3).
        const float main_basis = is_definite(inner_main) ? inner_main : 0.0f;
        const bool main_percent_resolves = is_definite(inner_main);

        // THE ITEM'S CONTENT-BASED MAIN SIZE, and the two axes ask it
        // differently. Across a row it is the max-content WIDTH, which
        // measure_box answers. Down a column it is a HEIGHT, and the only way to
        // know a box's height is to lay it out - at the width it is about to get,
        // because how wide a box is decides how many lines its text takes.
        float content_main = 0;
        float min_content_main = 0;
        if (horizontal) {
            const intrinsic_sizes wants = measure_box(child, it.c, measure_text);
            content_main = wants.max_content + it.main_padding;
            min_content_main = wants.min_content + it.main_padding;
        } else {
            it.cross = column_cross_size(b, it, content_width, measure_text);
            it.placed = layout_box(
                child, constraints{content_width, child_height, child.font_size, it.cross},
                measure_text);
            content_main = it.placed.bounds.height;
            min_content_main = content_main;
        }

        const length & basis = child.flex.basis;
        if (!basis.is_auto() && (basis.u != unit::percent || main_percent_resolves)) {
            it.base = basis.resolve(main_basis, child.font_size);
        } else if (!preferred.is_auto() &&
                   (preferred.u != unit::percent || main_percent_resolves)) {
            it.base = preferred.resolve(main_basis, child.font_size);
        } else {
            it.base = content_main;
        }
        it.base = std::max(0.0f, it.base);

        // A PERCENTAGE MAXIMUM THAT CANNOT RESOLVE BEHAVES AS `none`, exactly as
        // the two branches above treat an unresolvable percentage size as `auto`.
        // Resolving it against zero instead made it a definite maximum of 0, which
        // then clamped the automatic minimum to 0 as well and collapsed the item -
        // `max-height: 100%` in a column with no stated height gave height 0.
        it.max_main = max_len.is_auto() || (max_len.u == unit::percent && !main_percent_resolves)
                          ? indefinite
                          : max_len.resolve(main_basis, child.font_size);
        if (min_len.is_auto() || (min_len.u == unit::percent && !main_percent_resolves)) {
            // THE AUTOMATIC MINIMUM SIZE (§4.5). `min-width: auto` on a flex item
            // is NOT zero: it is min(the specified size suggestion, the content
            // size suggestion), which is what stops a long unbreakable token being
            // squeezed to nothing. Bootstrap's `.card { min-width: 0 }` exists
            // precisely to defeat it, which is the evidence that it matters.
            float suggestion = min_content_main;
            if (!preferred.is_auto() && (preferred.u != unit::percent || main_percent_resolves)) {
                suggestion = std::min(suggestion, preferred.resolve(main_basis, child.font_size));
            }
            if (is_definite(it.max_main)) { suggestion = std::min(suggestion, it.max_main); }
            it.min_main = std::max(0.0f, suggestion);
        } else {
            it.min_main = std::max(0.0f, min_len.resolve(main_basis, child.font_size));
        }
        it.hypothetical = clamp_extent(it.base, it.min_main, it.max_main);
    }

    // --- 4. COLLECT INTO LINES (§9.3) --------------------------------------
    //
    // `.row { flex-wrap: wrap }` IS the Bootstrap grid: thirteen twelfths cannot
    // share a line, so this step is what puts the thirteenth on a second one.
    std::vector<std::vector<std::size_t>> lines;
    if (b.flex.wrap == flex_wrap::nowrap || !is_definite(inner_main) || items.empty()) {
        std::vector<std::size_t> only;
        only.reserve(items.size());
        for (std::size_t i = 0; i < items.size(); ++i) { only.push_back(i); }
        if (!only.empty()) { lines.push_back(std::move(only)); }
    } else {
        std::vector<std::size_t> line;
        float used = 0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const float outer = items[i].hypothetical + items[i].main_margin;
            const float with_gap = line.empty() ? outer : used + main_gap + outer;
            // AT LEAST ONE ITEM PER LINE, however wide it is: an item that does
            // not fit on an empty line still has to go somewhere, and starting a
            // new line for it would make no progress and loop forever.
            if (!line.empty() && with_gap > inner_main) {
                lines.push_back(std::move(line));
                line.clear();
                used = outer;
            } else {
                used = with_gap;
            }
            line.push_back(i);
        }
        if (!line.empty()) { lines.push_back(std::move(line)); }
    }

    // --- 5. RESOLVE THE FLEXIBLE LENGTHS, AND SIZE ACROSS ------------------
    std::vector<float> line_cross(lines.size(), 0.0f);
    float main_extent = 0; // what the content actually reached along the main axis
    for (std::size_t l = 0; l < lines.size(); ++l) {
        const std::vector<std::size_t> & line = lines[l];
        // AN INDEFINITE MAIN SIZE MEANS NO FLEXING: there is no free space to
        // share out, so every item takes its hypothetical size and the container
        // sizes to their sum. That is CSS's rule, not a shortcut.
        if (is_definite(inner_main)) {
            resolve_flexible_lengths(items, line, inner_main, main_gap);
        } else {
            for (const std::size_t i : line) { items[i].target = items[i].hypothetical; }
        }

        float used_main = line.size() > 1 ? main_gap * static_cast<float>(line.size() - 1) : 0.0f;
        for (const std::size_t i : line) {
            flex_item & it = items[i];
            used_main += it.target + it.main_margin;
            const box_node & child = *it.box;
            if (horizontal) {
                // Laid out at the main size the freeze loop settled on, which is
                // why forced_width exists: the item's own `width` cannot produce
                // it, and `.row > * { width: 100% }` on every column is the proof.
                it.placed = layout_box(
                    child, constraints{content_width, child_height, child.font_size, it.target},
                    measure_text);
                it.cross = it.placed.bounds.height;
            } else {
                // The column case laid the item out in step 3, at its cross size;
                // only its main size moves here. Nothing inside depends on it,
                // because a block stacks downward from its own top edge - the same
                // reason table_flow can equalise a row's cell heights afterwards.
                it.placed.bounds.height = it.target;
            }
            it.placed.bounds.width = horizontal ? it.target : it.cross;
        }
        main_extent = std::max(main_extent, used_main);

        // THE LINE'S CROSS SIZE: the tallest item on it, margins included. A
        // SINGLE-LINE container with a definite cross size takes that instead,
        // per §9.4.3 - which is what makes `align-items: center` in a 120px row
        // centre against 120 rather than against the tallest column.
        //
        // "SINGLE-LINE" MEANS `flex-wrap: nowrap` (§5.2), not "produced one line".
        // A wrapping container that happens to fit on one is still multi-line, and
        // testing the count instead let it skip align-content entirely. The two
        // agree for the DEFAULT `align-content`, which stretches the single line to
        // fill the container anyway - which is why the Bootstrap rows still land in
        // the same place, and why the difference only shows when a page asks for
        // something other than stretch.
        float tallest = 0;
        for (const std::size_t i : line) {
            tallest = std::max(tallest, items[i].cross + items[i].cross_margin);
        }
        line_cross[l] =
            b.flex.wrap == flex_wrap::nowrap && is_definite(inner_cross) ? inner_cross : tallest;
    }

    // --- 6. ALIGN THE LINES (§9.6) -----------------------------------------
    float cross_extent = lines.empty() ? 0.0f : cross_gap * static_cast<float>(lines.size() - 1);
    for (const float size : line_cross) { cross_extent += size; }
    const float cross_free = is_definite(inner_cross) ? inner_cross - cross_extent : 0.0f;
    flex_align line_align = b.flex.line_align;
    if (line_align == flex_align::normal || line_align == flex_align::auto_) {
        line_align = flex_align::stretch;
    }
    distribution lines_at{};
    if (is_definite(inner_cross) && cross_free != 0) {
        if (line_align == flex_align::stretch) {
            // STRETCH SHARES THE LEFTOVER AMONG THE LINES rather than moving
            // them, which is the one distribution value that changes a size
            // instead of a position. Only when there is leftover: a negative
            // remainder is overflow, and lines do not shrink.
            if (cross_free > 0 && !lines.empty()) {
                const float each = cross_free / static_cast<float>(lines.size());
                for (float & size : line_cross) { size += each; }
                cross_extent = inner_cross;
            }
        } else {
            lines_at = distribute(line_align, cross_free, lines.size());
        }
    }

    // --- 7. PLACE ----------------------------------------------------------
    const float used_main_size = is_definite(inner_main) ? inner_main : main_extent;
    const float used_cross_size = is_definite(inner_cross) ? inner_cross : cross_extent;
    float cross_at = lines_at.start;
    for (std::size_t l = 0; l < lines.size(); ++l) {
        const std::vector<std::size_t> & line = lines[l];
        float used = line.size() > 1 ? main_gap * static_cast<float>(line.size() - 1) : 0.0f;
        std::size_t autos = 0;
        for (const std::size_t i : line) {
            used += items[i].target + items[i].main_margin;
            autos += (items[i].main_start_auto ? 1u : 0u) + (items[i].main_end_auto ? 1u : 0u);
        }
        const float free = used_main_size - used;
        // AUTO MAIN MARGINS EAT THE FREE SPACE FIRST (§9.5), and only what is
        // left reaches justify-content. `.navbar-nav { margin-right: auto }` is
        // the whole of how Bootstrap pushes the rest of a navbar to the right,
        // and treating that margin as zero put every navbar item hard left.
        const float auto_share = autos > 0 && free > 0 ? free / static_cast<float>(autos) : 0.0f;
        const distribution items_at =
            autos > 0 && free > 0 ? distribution{} : distribute(b.flex.justify, free, line.size());

        float main_at = items_at.start;
        for (std::size_t k = 0; k < line.size(); ++k) {
            flex_item & it = items[line[k]];
            if (k > 0) { main_at += main_gap + items_at.between; }
            if (it.main_start_auto) { main_at += auto_share; }
            const float main_position = main_at + it.main_lead;

            // THE CROSS POSITION, within this line.
            const flex_align align = resolved_align(b, *it.box);
            const length & cross_len = horizontal ? it.box->height : it.box->width;
            if (align == flex_align::stretch && cross_len.is_auto() && !it.cross_start_auto &&
                !it.cross_end_auto) {
                // Stretch is a SIZE, not a position: the item becomes as tall as
                // its line. Applied after the item was laid out for the same
                // reason table_flow equalises a row's cells afterwards - a block
                // stacks from its own top edge, so nothing inside moves.
                //
                // AND IT IS CLAMPED, per §9.4.11: a stretched item still honours
                // its own min and max cross size, so `max-height: 50px` in a 200px
                // row is 50 tall and not 200. The column axis already clamped in
                // column_cross_size and the row axis did not, which made the two
                // disagree about the same rule.
                const length & cross_min = horizontal ? it.box->min_height : it.box->min_width;
                const length & cross_max = horizontal ? it.box->max_height : it.box->max_width;
                const float cross_basis = horizontal ? used_cross_size : content_width;
                it.cross = clamp_extent(
                    std::max(0.0f, line_cross[l] - it.cross_margin),
                    cross_min.is_auto() ? 0.0f : cross_min.resolve(cross_basis, it.box->font_size),
                    cross_max.is_auto() ? indefinite
                                        : cross_max.resolve(cross_basis, it.box->font_size));
                (horizontal ? it.placed.bounds.height : it.placed.bounds.width) = it.cross;
            }
            const float cross_free_here = line_cross[l] - it.cross - it.cross_margin;
            float cross_offset = 0;
            if (it.cross_start_auto || it.cross_end_auto) {
                // Auto CROSS margins centre or push, exactly as on the main axis.
                if (cross_free_here > 0) {
                    cross_offset = it.cross_start_auto && it.cross_end_auto ? cross_free_here / 2
                                   : it.cross_start_auto                    ? cross_free_here
                                                                            : 0.0f;
                }
            } else {
                switch (align) {
                case flex_align::flex_end: cross_offset = cross_free_here; break;
                case flex_align::center: cross_offset = cross_free_here / 2; break;
                default: break; // flex-start, and a stretch that already applied
                }
            }
            const float cross_position = cross_at + cross_offset + it.cross_lead;

            // REVERSE IS A MIRROR, applied once at the end rather than threaded
            // through every placement decision above. `row-reverse` mirrors the
            // main axis and `wrap-reverse` the cross one, and mirroring the final
            // position flips the item ORDER and the alignment together, which is
            // exactly what both keywords mean.
            const float main_final =
                b.flex.reversed() ? used_main_size - main_position - it.target : main_position;
            const float cross_final = b.flex.wrap == flex_wrap::wrap_reverse
                                          ? used_cross_size - cross_position - it.cross
                                          : cross_position;
            it.placed.bounds.x = edges.content_left() + (horizontal ? main_final : cross_final);
            it.placed.bounds.y = edges.content_top() + (horizontal ? cross_final : main_final);

            main_at += it.target + it.main_margin;
            if (it.main_end_auto) { main_at += auto_share; }
        }
        cross_at += line_cross[l] + cross_gap + lines_at.between;
    }

    // ITEMS ARE EMITTED IN ORDER-MODIFIED ORDER, which is Chrome's paint order
    // (CSS Display 3 §3.3): `order` moves an item in front of its siblings for
    // painting as well as for placement. Everything that looks a fragment up does
    // so by node id, so no consumer depends on the source ordering.
    out.children.reserve(items.size() + 1);
    for (flex_item & it : items) { out.children.push_back(std::move(it.placed)); }
    // The out-of-flow children, after the items and in document order: markers at
    // the container's content origin, which is the static position §4 gives them.
    for (const box_node & child : b.children) {
        if (!child.is_out_of_flow()) { continue; }
        fragment placeholder;
        placeholder.box = &child;
        placeholder.source = child.source;
        placeholder.bounds = rect{edges.content_left(), edges.content_top(), 0, 0};
        out.children.push_back(std::move(placeholder));
    }

    out.bounds.width = outer_width;
    // The block-direction extent: down a row that is the lines stacked across,
    // down a column it is the longest line along the main axis.
    const float block_extent = horizontal ? used_cross_size : main_extent;
    out.bounds.height =
        is_definite(outer_height) ? outer_height : block_extent + edges.vertical_inner();
    return out;
}

} // namespace ctbrowser::layout
