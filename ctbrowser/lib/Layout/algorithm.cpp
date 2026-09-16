#include <ctbrowser/layout/algorithm.hpp>

#include <ctbrowser/layout/flex.hpp>

// algorithm: the function bodies.
// The header says what these compute; this says how.

namespace ctbrowser::layout {

resolved_edges resolve_edges(const box_node & b, const constraints & c) {
    const float basis = c.available_width;
    return resolved_edges{
        b.margin.top.resolve(basis, b.font_size),     b.margin.right.resolve(basis, b.font_size),
        b.margin.bottom.resolve(basis, b.font_size),  b.margin.left.resolve(basis, b.font_size),
        b.padding.top.resolve(basis, b.font_size),    b.padding.right.resolve(basis, b.font_size),
        b.padding.bottom.resolve(basis, b.font_size), b.padding.left.resolve(basis, b.font_size),
        b.border.top.resolve(basis, b.font_size),     b.border.right.resolve(basis, b.font_size),
        b.border.bottom.resolve(basis, b.font_size),  b.border.left.resolve(basis, b.font_size)};
}

float intrinsic_border_width(const box_node & b, const constraints & c, const resolved_edges & e,
                             const measure_text_fn & measure, const length & want) {
    const float inner = e.horizontal_inner();
    const float room = std::max(0.0f, c.available_width - e.horizontal_margin() - inner);
    float content = 0;
    if (want.u == unit::auto_ && !b.inline_level && !b.is_out_of_flow()) {
        // `auto` AS A BASIS - `calc-size(auto, size * 2)` - is what auto means
        // for this box's width: the available space for a block-level box, the
        // rule outer_width_of applies to a plain `auto`; below, shrink-to-fit
        // for an inline-level or out-of-flow one, which is fit-content.
        content = room;
    } else {
        const intrinsic_sizes wants = measure_box(b, c, measure);
        switch (want.u) {
        case unit::min_content: content = wants.min_content; break;
        case unit::max_content: content = wants.max_content; break;
        default:
            // fit-content: clamp(min-content, stretch, max-content), the same
            // clamp shrink_to_fit_width makes for an inline-block - or the
            // argument of fit-content(<length-percentage>) in place of stretch.
            content = std::max(wants.min_content,
                               std::min(fit_content_bound(b, want, room, c.available_width, inner),
                                        wants.max_content));
            break;
        }
    }
    return std::max(0.0f,
                    calc_over_content(b, want, std::max(0.0f, content), c.available_width, inner));
}

std::optional<float> width_bound(const box_node & b, const constraints & c,
                                 const resolved_edges & e, const measure_text_fn & measure,
                                 const length & want) {
    if (want.is_auto()) { return std::nullopt; }
    if (want.is_intrinsic()) { return intrinsic_border_width(b, c, e, measure, want); }
    return border_box_size(b, want, c.available_width, e.horizontal_inner());
}

float outer_width_of(const box_node & b, const constraints & c, const resolved_edges & e,
                     const measure_text_fn & measure) {
    // A WIDTH THE PARENT ALREADY DECIDED is used verbatim, and is not re-clamped
    // here: flex applied min/max-width inside its freeze loop, where the clamp
    // interacts with every other item on the line. Clamping a second time would
    // be right only for the items that never hit a constraint, which is the
    // subset for which it does nothing.
    if (c.forced_width >= 0) { return c.forced_width; }
    const float unclamped =
        width_bound(b, c, e, measure, b.width).value_or(c.available_width - e.horizontal_margin());
    // MAX FIRST, THEN MIN, because min wins: a box whose min-width exceeds its
    // max-width takes the min, which is what CSS 2.1 §10.4 says and the order
    // that produces it without a special case.
    float out = unclamped;
    if (const auto hi = width_bound(b, c, e, measure, b.max_width)) { out = std::min(out, *hi); }
    if (const auto lo = width_bound(b, c, e, measure, b.min_width)) { out = std::max(out, *lo); }
    return std::max(0.0f, out);
}

float auto_margin_left(const box_node & b, const constraints & c, const resolved_edges & e,
                       float outer_width) {
    // `margin: 0 auto` on a box with a definite width CENTRES it, and that is the
    // whole of how a page is centred. Both autos resolved to 0 before
    // (length::resolve has no answer for auto), so `.container` sat hard against
    // the left edge.
    //
    // An AUTO WIDTH leaves both margins at zero, per CSS: the width absorbs the
    // remainder instead. That is why adding this moves nothing that was not
    // already asking to be centred.
    if (b.width.is_auto()) { return e.margin_left; }
    // The FLAGS, not is_auto(): an unset margin is also `unit::auto_`, so asking the
    // length would centre every definite-width box that declared no margins.
    const bool left_auto = b.margin_left_auto;
    const bool right_auto = b.margin_right_auto;
    if (!left_auto && !right_auto) { return e.margin_left; }
    const float remainder = c.available_width - outer_width;
    if (remainder <= 0) { return left_auto ? 0.0f : e.margin_left; }
    if (left_auto && right_auto) { return remainder / 2.0f; }
    return left_auto ? remainder : e.margin_left;
}

float content_width_of(const box_node & b, const constraints & c, const resolved_edges & e,
                       const measure_text_fn & measure) {
    return std::max(0.0f, outer_width_of(b, c, e, measure) - e.horizontal_inner());
}

intrinsic_sizes measure_box(const box_node & b, const constraints & c,
                            const measure_text_fn & measure) {
    if (b.kind == box_kind::text) {
        return intrinsic_sizes{inline_flow::longest_word(b, measure),
                               measure(b.text, b.font_size, b.face)};
    }
    if (b.is_replaced()) {
        // Its size is the ELEMENT's. A replaced box's children are not laid out
        // at all, so measuring them answers zero for a 300px canvas.
        return intrinsic_sizes{b.intrinsic_width, b.intrinsic_width};
    }
    if (b.kind == box_kind::flex) { return flex_flow{}.measure(b, c, measure); }
    if (b.kind == box_kind::table) { return table_flow{}.measure(b, c, measure); }
    if (b.establishes_inline_context()) { return inline_flow{}.measure(b, c, measure); }
    return block_flow{}.measure(b, c, measure);
}

intrinsic_sizes outer_intrinsic(const box_node & child, const constraints & c,
                                const measure_text_fn & measure) {
    const resolved_edges edges = resolve_edges(child, c);
    const float inner = edges.horizontal_inner();
    const intrinsic_sizes content = measure_box(child, c, measure);
    intrinsic_sizes out{content.min_content + inner, content.max_content + inner};
    // A STATED WIDTH IS THE CONTRIBUTION whatever the content wants, as a BORDER
    // box like every other size here. A PERCENTAGE has no answer while the
    // containing block's own width is the question being asked, so there the
    // content size stands - which is also why `.row > * { max-width: 100% }`
    // never reaches the clamp below.
    // A KEYWORD WIDTH IS ONE OF THE TWO SIZES BEING CONTRIBUTED: `width:
    // max-content` contributes its max-content size as both, `min-content` its
    // min-content size as both, and `fit-content` (or `auto` under a
    // calc-size()) contributes as `auto` does - and the calculation, if there is
    // one, runs over each of the two.
    const auto sized = [&](const length & want) -> std::optional<intrinsic_sizes> {
        if (want.is_intrinsic()) {
            float lo = content.min_content;
            float hi = content.max_content;
            if (want.u == unit::min_content) { hi = lo; }
            if (want.u == unit::max_content) { lo = hi; }
            return intrinsic_sizes{calc_over_content(child, want, lo, c.available_width, inner),
                                   calc_over_content(child, want, hi, c.available_width, inner)};
        }
        if (want.is_auto() || want.u == unit::percent) { return std::nullopt; }
        const float stated = std::max(0.0f, border_box_size(child, want, c.available_width, inner));
        return intrinsic_sizes{stated, stated};
    };
    if (const auto stated = sized(child.width)) { out = *stated; }
    // MAX FIRST, THEN MIN, the same order and the same reason as outer_width_of.
    if (const auto hi = sized(child.max_width)) {
        out.min_content = std::min(out.min_content, hi->min_content);
        out.max_content = std::min(out.max_content, hi->max_content);
    }
    if (const auto lo = sized(child.min_width)) {
        out.min_content = std::max(out.min_content, lo->min_content);
        out.max_content = std::max(out.max_content, lo->max_content);
    }
    out.min_content = std::max(0.0f, out.min_content) + edges.horizontal_margin();
    out.max_content = std::max(0.0f, out.max_content) + edges.horizontal_margin();
    return out;
}

float shrink_to_fit_width(const box_node & b, const constraints & c, const resolved_edges & e,
                          const measure_text_fn & measure) {
    const intrinsic_sizes wants = measure_box(b, c, measure);
    const float room =
        std::max(0.0f, c.available_width - e.horizontal_margin() - e.horizontal_inner());
    // The measured sizes are the CONTENT's; the answer is a border box, like every
    // other width here.
    float out =
        std::max(wants.min_content, std::min(room, wants.max_content)) + e.horizontal_inner();
    // MAX FIRST, THEN MIN, the same order and the same reason as outer_width_of.
    if (const auto hi = width_bound(b, c, e, measure, b.max_width)) { out = std::min(out, *hi); }
    if (const auto lo = width_bound(b, c, e, measure, b.min_width)) { out = std::max(out, *lo); }
    return std::max(0.0f, out);
}

fragment layout_box(const box_node & b, const constraints & c, const measure_text_fn & measure) {
    if (b.kind == box_kind::text) {
        fragment f;
        f.box = &b;
        f.source = b.source;
        f.text = b.text;
        f.bounds = rect{0, 0, measure(b.text, b.font_size, b.face), b.line_height};
        return f;
    }
    // Dispatch on what the box IS, not on what its children are. An inline box
    // shrink-wraps to its content; a block box takes its width from the block
    // rules and lays its children out inline or as blocks internally.
    if (b.kind == box_kind::replaced) {
        // Sized by the element, not by content. CSS width/height still win when
        // given - that is how `canvas { width: 100% }` scales the bitmap.
        const resolved_edges edges = resolve_edges(b, c);
        fragment f;
        f.box = &b;
        f.source = b.source;
        // A STATED SIZE GOES THROUGH THE SAME BOX MODEL as every other box here
        // (border_box_size); only an INTRINSIC one is always a content size that
        // the padding and the border are added around.
        //
        // Adding them to a stated width unconditionally made every
        // `.form-control` 26px wider than Chrome's: Bootstrap sets `box-sizing:
        // border-box` on `*`, so its `width: 100%` already contains the 24px of
        // padding and the 2px of border, and this counted them a second time.
        f.bounds.width = width_bound(b, c, edges, measure, b.width)
                             .value_or(b.intrinsic_width + edges.horizontal_inner());
        f.bounds.height =
            has_definite_height(b, c)
                ? border_box_size(b, b.height, c.available_height, edges.vertical_inner())
                : b.intrinsic_height + edges.vertical_inner();
        return f;
    }
    fragment out;
    if (b.kind == box_kind::table) {
        out = table_flow{}.arrange(b, c, measure);
    } else if (b.kind == box_kind::flex) {
        out = flex_flow{}.arrange(b, c, measure);
    } else if (b.kind == box_kind::inline_) {
        out = inline_flow{}.arrange(b, c, measure);
    } else {
        out = block_flow{}.arrange(b, c, measure);
    }
    // A flex or table container does not use block_flow, but it is still a
    // block-level participant in its PARENT's formatting context and its own
    // margins collapse with ordinary siblings there. block_flow already added
    // these for its kinds; append is idempotent, so one common exit keeps every
    // block-level dispatch on the same rule.
    if (b.is_block_level()) {
        const resolved_edges edges = resolve_edges(b, c);
        out.block_margins.before.append(edges.margin_top);
        out.block_margins.after.append(edges.margin_bottom);
    }
    return out;
}

} // namespace ctbrowser::layout
