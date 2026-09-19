#pragma once

#include <ctbrowser/layout/algorithm/common.hpp>

namespace ctbrowser::layout {

// --- inline formatting context -------------------------------------------
// Text and inline boxes on shared lines, wrapping at the content width. Line
// breaking is greedy and breaks at spaces, which is what browsers do for
// ordinary text; the interesting cases it does not handle (hyphenation,
// bidi, shaping across an inline boundary) all belong to a text shaper.
struct inline_flow {
    // The factor `line-height: normal` falls back to. box_builder owns the
    // resolution now (see resolve_line_height); this is only the default, and it
    // stays a constant rather than becoming the font's own ascent+descent because
    // the measure function is injected and the goldens pin it to font8x8.
    static constexpr float line_height_factor = 1.25f;

    [[nodiscard]] intrinsic_sizes measure(const box_node & b, const constraints & c,
                                          const measure_text_fn & measure_text) const {
        intrinsic_sizes out;
        float line = 0;
        for (const box_node & child : b.children) {
            if (child.is_out_of_flow()) { continue; } // contributes no width
            // A replaced child's width is its OWN, not something derived from
            // content it does not have - without that an <img> or a <canvas>
            // measures as zero wide and never wraps, it just runs off the line.
            // measure_box knows that, and knows what an inline-flex child is too.
            const float w = child.kind == box_kind::text
                                ? measure_text(child.text, child.font_size, child.face)
                                : outer_intrinsic(child, c, measure_text).max_content;
            line += w;
            out.min_content = std::max(out.min_content, longest_word(child, measure_text));
        }
        out.max_content = line;
        return out;
    }

    [[nodiscard]] fragment arrange(const box_node & b, const constraints & c,
                                   const measure_text_fn & measure_text) const {
        fragment out;
        out.box = &b;
        out.source = b.source;
        // THE BOX'S OWN line height, resolved by box_builder. This was
        // `font_size * 1.25` for every box on every page, which is why
        // `line-height` did nothing.
        const float line_height = b.line_height;

        float pen_x = 0;
        float pen_y = 0;
        float widest = 0;
        // The tallest thing on the current line. A line containing a 110px
        // canvas is 110px tall, not one text line - and using the font size
        // makes everything after it overlap.
        float line_extent = line_height;
        float total_height = 0;
        // CSS treats a line containing only zero-edge, empty inline boxes as
        // not existing for height/layout purposes. Keep their zero fragments
        // for geometry queries, but do not let vector non-emptiness manufacture
        // a phantom line box.
        bool has_line_content = false;
        // BASELINE ALIGNMENT. Every item on a line is placed so that
        // `y + ascent` is the same for all of them - which is what sharing a
        // baseline means, and what the rasterizer then draws: it puts each
        // run's glyphs at `y + ascent(its own size)`.
        //
        // Aligning the BOXES instead is only right when every item has the same
        // metrics. Tops made <big> hang above its neighbours; bottoms were
        // closer but still wrong, because a box's bottom is its descent below
        // the baseline and two faces do not descend by the same amount.
        //
        // A REPLACED item - an <img>, a <canvas> - sits ON the baseline, so its
        // ascent is its whole height. That is the CSS rule and it is why an
        // image in a line of text does not sink into the descenders.
        std::size_t line_start = 0;
        const auto ascent_of = [&measure_text](const fragment & f) {
            if (f.box == nullptr) { return 0.0f; }
            if (f.box->is_replaced()) { return f.bounds.height; }
            // AN INLINE-LEVEL BLOCK SITS ON ITS OWN LAST LINE'S BASELINE, not on
            // its font's ascent (CSS 2.1 §10.8.1). The two are far apart the
            // moment the box has padding: a `.badge` is .35em of padding, a line
            // of text and .35em more, so aligning by the font ascent hangs the
            // whole pill above the sentence it is in by exactly its top padding.
            //
            // With no in-flow line box - an empty `inline-block`, or one holding
            // only other blocks - the spec falls back to the bottom margin edge,
            // which is what a replaced element does and what the height is here.
            if (f.box->inline_level) {
                // The last line box may be inside a descendant - an inline-block
                // holding blocks takes the last block's last line - so this walks
                // rather than scanning one level, accumulating offsets as it goes.
                // The LOWEST baseline is the last one, and asking for that rather
                // than for document order is also what keeps it right when a float
                // or an out-of-flow box eventually reorders the children.
                float lowest = -1;
                const auto walk = [&](auto && self, const fragment & at, float dy) -> void {
                    if (at.box != nullptr && at.box->kind == box_kind::text) {
                        lowest = std::max(lowest,
                                          dy + at.bounds.y +
                                              measure_text.ascent(at.box->font_size, at.box->face));
                    }
                    for (const fragment & c : at.children) { self(self, c, dy + at.bounds.y); }
                };
                for (const fragment & c : f.children) { walk(walk, c, 0); }
                return lowest < 0 ? f.bounds.height : lowest;
            }
            return measure_text.ascent(f.box->font_size, f.box->face);
        };
        // TEXT-ALIGN IS THE SAME PASS. A line is aligned by shifting everything on
        // it, which is exactly the loop that already runs to put them on a shared
        // baseline - so it happens there rather than in a second walk that would
        // have to rediscover which fragments are on which line.
        //
        // The shift is measured against `c.available_width`, the CONTENT width the
        // container handed down, and not against `widest`: the leftover space is
        // the whole point, and a line that already fills its container has none.
        const float align = b.text_align;
        const auto align_line = [&out, &line_start, &ascent_of, align, &c](float top) {
            float line_ascent = 0;
            float extent = 0;
            for (std::size_t i = line_start; i < out.children.size(); ++i) {
                if (out.children[i].bounds.y != top ||
                    (out.children[i].bounds.width == 0 && out.children[i].bounds.height == 0)) {
                    continue;
                }
                line_ascent = std::max(line_ascent, ascent_of(out.children[i]));
                extent = std::max(extent, out.children[i].bounds.x + out.children[i].bounds.width);
            }
            const float shift =
                align > 0 ? std::max(0.0f, c.available_width - extent) * align : 0.0f;
            for (std::size_t i = line_start; i < out.children.size(); ++i) {
                fragment & f = out.children[i];
                if (f.bounds.y != top) { continue; } // a later line already
                f.bounds.y = top + line_ascent - ascent_of(f);
                f.bounds.x += shift;
            }
            line_start = out.children.size();
        };
        for (const box_node & child : b.children) {
            // Out of flow: no space on the line, an empty fragment at the pen for
            // its static position. See the same case in block_flow.
            if (child.is_out_of_flow()) {
                fragment placeholder;
                placeholder.box = &child;
                placeholder.source = child.source;
                placeholder.bounds = rect{pen_x, pen_y, 0, 0};
                out.children.push_back(std::move(placeholder));
                continue;
            }
            if (child.kind == box_kind::text) {
                const std::size_t before = out.children.size();
                if (child.preformatted) {
                    place_preformatted(child, line_height, pen_x, pen_y, out);
                } else {
                    place_text(child, c, measure_text, line_height, pen_x, pen_y, out);
                }
                has_line_content |= out.children.size() != before;
                continue;
            }
            // LAID OUT FIRST, THEN ASKED WHETHER IT FITS. The width that decides
            // a wrap has to be the width the box will actually take, and an
            // intrinsic estimate is not it: `outer_intrinsic` cannot resolve a
            // PERCENTAGE width - the containing block is the question it is
            // usually asked in the middle of - so a `.form-control`, which is
            // `width: 100%`, measured as its 20-character intrinsic and sat
            // happily beside its label instead of taking the line to itself.
            // Every Bootstrap form group was two lines short because of it.
            //
            // Laying out first costs nothing: a block or replaced child's layout
            // depends on the containing block's width and not on where the pen
            // happens to be, so the answer is the same either way and this call
            // was going to happen regardless.
            fragment f =
                layout_box(child, constraints{c.available_width, 0, child.font_size}, measure_text);
            // AN INLINE-LEVEL BOX'S MARGINS ARE PART OF THE LINE. They were
            // ignored outright, so `.form-label`'s `margin-bottom: .5rem` - which
            // is what separates every Bootstrap label from its field - did
            // nothing, and two inline-blocks side by side touched.
            const resolved_edges child_edges = resolve_edges(child, c);
            const bool empty_inline =
                child.kind == box_kind::inline_ && child.tag != "br" && f.bounds.width == 0 &&
                f.bounds.height == 0 && child_edges.margin_top == 0 &&
                child_edges.margin_right == 0 && child_edges.margin_bottom == 0 &&
                child_edges.margin_left == 0 && child_edges.pad_top == 0 &&
                child_edges.pad_right == 0 && child_edges.pad_bottom == 0 &&
                child_edges.pad_left == 0 && child_edges.border_top == 0 &&
                child_edges.border_right == 0 && child_edges.border_bottom == 0 &&
                child_edges.border_left == 0;
            if (empty_inline) {
                f.bounds.x = pen_x;
                f.bounds.y = pen_y;
                out.children.push_back(std::move(f));
                continue;
            }
            has_line_content = true;
            const float w =
                (f.bounds.width > 0 ? f.bounds.width
                                    : outer_intrinsic(child, c, measure_text).max_content) +
                child_edges.horizontal_margin();
            if (pen_x > 0 && pen_x + w > c.available_width) {
                align_line(pen_y);
                pen_x = 0;
                pen_y += line_extent;
                line_extent = line_height;
            }
            f.bounds.x = pen_x + child_edges.margin_left;
            f.bounds.y = pen_y;
            if (f.bounds.width <= 0) { f.bounds.width = w - child_edges.horizontal_margin(); }
            if (f.bounds.height <= 0) { f.bounds.height = line_height; }
            pen_x += f.bounds.width + child_edges.horizontal_margin();
            // The MARGIN box sets the line's extent: a margin below an
            // inline-block pushes the next line down, which is the whole of what
            // `margin-bottom` on a label does.
            line_extent = std::max(line_extent, f.bounds.height + child_edges.margin_top +
                                                    child_edges.margin_bottom);
            total_height = std::max(total_height, f.bounds.y + f.bounds.height);
            out.children.push_back(std::move(f));
        }
        align_line(pen_y); // the last line
        for (const fragment & line : out.children) {
            widest = std::max(widest, line.bounds.x + line.bounds.width);
        }
        // The extent the content actually REACHED, not what it was offered. A
        // block container replaces this with its own block-rule width; an inline
        // box keeps it, which is what shrink-to-fit means.
        out.bounds.width = widest;
        out.bounds.height = has_line_content ? std::max(pen_y + line_extent, total_height) : 0;
        out.has_line_box = has_line_content;
        return out;
    }

private:
    // Greedy wrap: take words while they fit, then break. Each visual line
    // becomes its own fragment, which is exactly the case the fragment tree
    // exists to represent.
    // `white-space: pre` keeps the newlines, and a kept newline is a LINE
    // BREAK - not a character. Handing it to the rasterizer draws .notdef,
    // which is a box, which is precisely what a <pre> block looked like.
    static void place_preformatted(const box_node & child, float line_height, float & pen_x,
                                   float & pen_y, fragment & out) {
        std::string_view rest = child.text;
        for (;;) {
            const std::size_t br = rest.find('\n');
            const std::string_view line = rest.substr(0, br);
            fragment f;
            f.box = &child;
            f.source = child.source;
            f.text = std::string{line};
            // Preformatted text is not wrapped and not re-measured: it is
            // exactly the line the page wrote.
            f.bounds = rect{pen_x, pen_y, 0, line_height};
            out.children.push_back(std::move(f));
            if (br == std::string_view::npos) { break; }
            pen_x = 0;
            pen_y += line_height;
            rest = rest.substr(br + 1);
        }
    }

    static void place_text(const box_node & child, const constraints & c,
                           const measure_text_fn & measure_text, float line_height, float & pen_x,
                           float & pen_y, fragment & out) {
        std::string_view rest = child.text;
        // A space at the START of a line is removed. Otherwise the space
        // between two elements indents the next line whenever the first of them
        // ends one.
        if (pen_x == 0) {
            while (!rest.empty() && rest.front() == ' ') { rest.remove_prefix(1); }
        }
        while (!rest.empty()) {
            const std::size_t take = words_that_fit(rest, c.available_width - pen_x,
                                                    child.font_size, child.face, measure_text);
            if (take == 0) {
                // nothing fits in what is left of this line: start a new one
                if (pen_x == 0) { break; } // ...unless the line is already empty
                pen_x = 0;
                pen_y += line_height;
                continue;
            }
            const std::string_view run = rest.substr(0, take);
            fragment f;
            f.box = &child;
            f.source = child.source;
            f.text = std::string{run};
            f.bounds =
                rect{pen_x, pen_y, measure_text(run, child.font_size, child.face), line_height};
            out.children.push_back(std::move(f));
            pen_x += out.children.back().bounds.width;
            rest.remove_prefix(take);
            while (!rest.empty() && rest.front() == ' ') { rest.remove_prefix(1); }
            if (!rest.empty()) {
                pen_x = 0;
                pen_y += line_height;
            }
        }
    }

public:
    // Used by block_flow to measure a text child, and by table_flow through it.
    [[nodiscard]] static float longest_word(const box_node & b,
                                            const measure_text_fn & measure_text) {
        if (b.kind != box_kind::text) { return 0; }
        float widest = 0;
        std::size_t at = 0;
        while (at <= b.text.size()) {
            const std::size_t space = b.text.find(' ', at);
            const std::size_t end = space == std::string::npos ? b.text.size() : space;
            widest = std::max(widest, measure_text(std::string_view{b.text}.substr(at, end - at),
                                                   b.font_size, b.face));
            if (space == std::string::npos) { break; }
            at = space + 1;
        }
        return widest;
    }
};

} // namespace ctbrowser::layout
