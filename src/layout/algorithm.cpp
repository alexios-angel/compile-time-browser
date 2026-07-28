#include <ctbrowser/layout/algorithm.hpp>

// algorithm: the function bodies.
// The header says what these compute; this says how.

namespace ctbrowser::layout {

resolved_edges resolve_edges(const box_node & b, const constraints & c) {
    const float basis = c.available_width;
    return resolved_edges{
        b.margin.top.resolve(basis, b.font_size),     b.margin.right.resolve(basis, b.font_size),
        b.margin.bottom.resolve(basis, b.font_size),  b.margin.left.resolve(basis, b.font_size),
        b.padding.top.resolve(basis, b.font_size),    b.padding.right.resolve(basis, b.font_size),
        b.padding.bottom.resolve(basis, b.font_size), b.padding.left.resolve(basis, b.font_size)};
}

float outer_width_of(const box_node & b, const constraints & c, const resolved_edges & e) {
    return b.width.is_auto() ? c.available_width - e.horizontal_margin()
                             : b.width.resolve(c.available_width, b.font_size);
}

float content_width_of(const box_node & b, const constraints & c, const resolved_edges & e) {
    return std::max(0.0f, outer_width_of(b, c, e) - e.horizontal_padding());
}

fragment layout_box(const box_node & b, const constraints & c, const measure_text_fn & measure,
                    precomputed * ready) {
    if (b.kind == box_kind::text) {
        fragment f;
        f.box = &b;
        f.source = b.source;
        f.text = b.text;
        f.bounds = rect{0, 0, measure(b.text, b.font_size, b.face),
                        b.font_size * inline_flow::line_height_factor};
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
        f.bounds.width =
            b.width.is_auto() ? b.intrinsic_width : b.width.resolve(c.available_width, b.font_size);
        f.bounds.height = b.height.is_auto() ? b.intrinsic_height
                                             : b.height.resolve(c.available_height, b.font_size);
        f.bounds.width += edges.horizontal_padding();
        f.bounds.height += edges.vertical_padding();
        return f;
    }
    if (b.kind == box_kind::table) { return table_flow{}.arrange(b, c, measure, ready); }
    if (b.kind == box_kind::inline_) { return inline_flow{}.arrange(b, c, measure, ready); }
    return block_flow{}.arrange(b, c, measure, ready);
}

} // namespace ctbrowser::layout
