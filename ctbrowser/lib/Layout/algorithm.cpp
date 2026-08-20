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

float outer_width_of(const box_node & b, const constraints & c, const resolved_edges & e) {
    // A WIDTH THE PARENT ALREADY DECIDED is used verbatim, and is not re-clamped
    // here: flex applied min/max-width inside its freeze loop, where the clamp
    // interacts with every other item on the line. Clamping a second time would
    // be right only for the items that never hit a constraint, which is the
    // subset for which it does nothing.
    if (c.forced_width >= 0) { return c.forced_width; }
    const float unclamped = b.width.is_auto() ? c.available_width - e.horizontal_margin()
                                              : b.width.resolve(c.available_width, b.font_size);
    // MAX FIRST, THEN MIN, because min wins: a box whose min-width exceeds its
    // max-width takes the min, which is what CSS 2.1 §10.4 says and the order
    // that produces it without a special case.
    float out = unclamped;
    if (!b.max_width.is_auto()) {
        out = std::min(out, b.max_width.resolve(c.available_width, b.font_size));
    }
    if (!b.min_width.is_auto()) {
        out = std::max(out, b.min_width.resolve(c.available_width, b.font_size));
    }
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

float content_width_of(const box_node & b, const constraints & c, const resolved_edges & e) {
    return std::max(0.0f, outer_width_of(b, c, e) - e.horizontal_inner());
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
    intrinsic_sizes out = measure_box(child, c, measure);
    out.min_content += edges.horizontal_inner();
    out.max_content += edges.horizontal_inner();
    // A STATED WIDTH IS THE CONTRIBUTION whatever the content wants, and it names
    // the BORDER box like every other size here. A PERCENTAGE has no answer while
    // the containing block's own width is the question being asked, so there the
    // content size stands - which is also why `.row > * { max-width: 100% }` never
    // reaches the clamp below.
    if (!child.width.is_auto() && child.width.u != unit::percent) {
        const float stated =
            std::max(0.0f, child.width.resolve(c.available_width, child.font_size));
        out.min_content = stated;
        out.max_content = stated;
    }
    // MAX FIRST, THEN MIN, the same order and the same reason as outer_width_of.
    if (!child.max_width.is_auto() && child.max_width.u != unit::percent) {
        const float hi = child.max_width.resolve(c.available_width, child.font_size);
        out.min_content = std::min(out.min_content, hi);
        out.max_content = std::min(out.max_content, hi);
    }
    if (!child.min_width.is_auto() && child.min_width.u != unit::percent) {
        const float lo = child.min_width.resolve(c.available_width, child.font_size);
        out.min_content = std::max(out.min_content, lo);
        out.max_content = std::max(out.max_content, lo);
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
    if (!b.max_width.is_auto()) {
        out = std::min(out, b.max_width.resolve(c.available_width, b.font_size));
    }
    if (!b.min_width.is_auto()) {
        out = std::max(out, b.min_width.resolve(c.available_width, b.font_size));
    }
    return std::max(0.0f, out);
}

fragment layout_box(const box_node & b, const constraints & c, const measure_text_fn & measure,
                    precomputed * ready) {
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
        // A STATED SIZE IS THE BORDER BOX, exactly as it is for every other box
        // here - only an INTRINSIC one is a content size that the padding and the
        // border are added around.
        //
        // Adding them to a stated width too made every `.form-control` 26px wider
        // than Chrome's: Bootstrap sets `box-sizing: border-box` on `*`, so its
        // `width: 100%` already contains the 24px of padding and the 2px of
        // border, and this counted them a second time. The rest of the engine has
        // treated a stated width as the border box since outer_width_of was
        // written; this was the one place that disagreed.
        f.bounds.width = b.width.is_auto() ? b.intrinsic_width + edges.horizontal_inner()
                                           : b.width.resolve(c.available_width, b.font_size);
        f.bounds.height = has_definite_height(b, c)
                              ? b.height.resolve(c.available_height, b.font_size)
                              : b.intrinsic_height + edges.vertical_inner();
        return f;
    }
    fragment out;
    if (b.kind == box_kind::table) {
        out = table_flow{}.arrange(b, c, measure, ready);
    } else if (b.kind == box_kind::flex) {
        out = flex_flow{}.arrange(b, c, measure, ready);
    } else if (b.kind == box_kind::inline_) {
        out = inline_flow{}.arrange(b, c, measure, ready);
    } else {
        out = block_flow{}.arrange(b, c, measure, ready);
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
