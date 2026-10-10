//===- inline.hpp - Inline formatting context ----------------- -*- C++ -*-===//
//
// The inline formatting context: inline_flow places text and inline-level
// boxes on shared lines, aligned on a common baseline.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctbrowser/layout/algorithm/common.hpp>

namespace ctbrowser::layout {

/// The inline formatting context: text and inline boxes on shared lines,
/// wrapping at the content width. Line breaking is greedy and breaks at spaces,
/// which is what browsers do for ordinary text; the cases it does not handle
/// (hyphenation, bidi, shaping across an inline boundary) all belong to a text
/// shaper.
struct inline_flow {
    /// The factor `line-height: normal` uses. Nothing reads this constant:
    /// box_builder::resolve_line_height (layout/box.hpp) resolves line heights
    /// with its own copy of the same 1.25. It is a constant rather than the
    /// font's ascent+descent because the measure is injected and the goldens
    /// pin it to font8x8.
    static constexpr float line_height_factor = 1.25f;

    [[nodiscard]] intrinsic_sizes measure(const box_node & b, const constraints & c,
                                          const measure_text_fn & measure_text) const {
        intrinsic_sizes out;
        float line = 0;
        for (const box_node & child : b.children) {
            if (child.is_out_of_flow()) { continue; } // contributes no width
            // A replaced child (<img>, <canvas>) has a width of its own, not one
            // derived from children it does not have; outer_intrinsic, through
            // measure_box, knows that and how to measure an inline-flex child.
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
        // The box's own line height, already resolved by box_builder.
        const float line_height = b.line_height;

        // An inline box's own padding and border take room on the line (CSS
        // 2.1 §10.3.1, §10.8.1): content starts after the start edge and the
        // box ends after the end edge, matching outer_intrinsic. A block
        // container's edges are block_flow's job, and continuation lines start
        // at zero.
        const bool inline_box = b.kind == box_kind::inline_;
        const resolved_edges own = inline_box ? resolve_edges(b, c) : resolved_edges{};
        float pen_x = own.content_left();
        float pen_y = 0;
        float widest = 0;
        // The tallest thing on the current line. A line containing a 110px
        // canvas is 110px tall, not one text line; using the font size would
        // make everything after it overlap.
        float line_extent = line_height;
        float total_height = 0;
        // CSS treats a line containing only zero-edge, empty inline boxes as
        // not existing for height/layout purposes. Keep their zero fragments
        // for geometry queries, but do not let vector non-emptiness manufacture
        // a phantom line box.
        bool has_line_content = false;
        // Baseline alignment. Every item on a line is placed so that
        // `y + ascent` is the same for all of them, which is what sharing a
        // baseline means and what the rasterizer then draws: it puts each run's
        // glyphs at `y + ascent(its own size)`. Aligning box tops or bottoms
        // instead is only right when every item has the same font metrics.
        //
        // A replaced item (an <img>, a <canvas>) sits on the baseline, so its
        // ascent is its whole height. That is the CSS rule, and it is why an
        // image in a line of text does not sink into the descenders.
        std::size_t line_start = 0;
        const auto ascent_of = [&measure_text](const fragment & f) {
            if (f.box == nullptr) { return 0.0f; }
            if (f.box->is_replaced()) { return f.bounds.height; }
            // An inline-level block sits on its own last line's baseline, not on
            // its font's ascent (CSS 2.1 §10.8.1). The two differ by the box's
            // top padding and border (e.g. a padded `.badge`).
            //
            // With no in-flow line box (an empty `inline-block`, or one holding
            // only other blocks), the spec falls back to the bottom margin edge,
            // which is what a replaced element does and what the height is here.
            if (f.box->inline_level) {
                // The last line box may be inside a descendant (an inline-block
                // holding blocks takes the last block's last line), so this walks
                // the subtree, accumulating offsets as it goes. It takes the
                // lowest baseline rather than the last in document order, which
                // stays right if a float or out-of-flow box reorders children.
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
        // `text-align` is applied in the same pass: a line is aligned by
        // shifting everything on it, which the baseline loop already visits, so
        // no second walk has to rediscover which fragments are on which line.
        //
        // The shift is measured against `c.available_width`, the content width
        // the container handed down, not against `widest`: it distributes the
        // leftover space, and a line that fills its container has none.
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
                if (f.bounds.y != top) { continue; } // belongs to a later line
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
            // Lay the child out before deciding whether it wraps: outer_intrinsic
            // cannot resolve a percentage width (e.g. `width: 100%`), so only the
            // laid-out width is reliable. This costs nothing, since a block's
            // layout depends on the containing block's width, not on the pen
            // position.
            fragment f =
                layout_box(child, constraints{c.available_width, 0, child.font_size}, measure_text);
            // An inline-level box's margins take room on the line, horizontally
            // and in the line's extent.
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
            // An inline box with no height of its own is a line tall. An atomic
            // inline (an inline-block) is the height it came to, including a
            // stated `height: 0` or an empty one's zero, as with its `auto`
            // (height-interpolation reads both).
            // ponytail: a replaced box with no height keeps the line height:
            // an iframe of zero height would make its document's
            // `(device-height)` false, because device-* is the frame's
            // viewport here rather than the screen (test_media_queries);
            // drop the exception once media_environment has a device size.
            if (f.bounds.height <= 0 && (child.kind == box_kind::inline_ || child.is_replaced())) {
                f.bounds.height = line_height;
            }
            pen_x += f.bounds.width + child_edges.horizontal_margin();
            // The margin box sets the line's extent: a margin below an
            // inline-block pushes the next line down.
            line_extent = std::max(line_extent, f.bounds.height + child_edges.margin_top +
                                                    child_edges.margin_bottom);
            total_height = std::max(total_height, f.bounds.y + f.bounds.height);
            out.children.push_back(std::move(f));
        }
        align_line(pen_y); // the last line
        for (const fragment & line : out.children) {
            widest = std::max(widest, line.bounds.x + line.bounds.width);
        }
        // The extent the content actually reached, not what it was offered. A
        // block container replaces this with its own block-rule width; an inline
        // box keeps it, which is what shrink-to-fit means.
        out.bounds.width = std::max(widest, own.content_left()) + own.pad_right + own.border_right;
        out.bounds.height = has_line_content ? std::max(pen_y + line_extent, total_height) : 0;
        out.has_line_box = has_line_content;
        return out;
    }

private:
    /// Places `white-space: pre` text: each kept newline is a line break, not a
    /// character (the rasterizer would draw it as a .notdef box), and lines are
    /// neither wrapped nor re-measured.
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

    /// Greedy wrap: takes words while they fit in the remaining width, then
    /// breaks. Each visual line becomes its own fragment.
    static void place_text(const box_node & child, const constraints & c,
                           const measure_text_fn & measure_text, float line_height, float & pen_x,
                           float & pen_y, fragment & out) {
        std::string_view rest = child.text;
        // A space at the start of a line is removed. Otherwise the space
        // between two elements indents the next line whenever the first of them
        // ends one.
        if (pen_x == 0) {
            while (!rest.empty() && rest.front() == ' ') { rest.remove_prefix(1); }
        }
        while (!rest.empty()) {
            const std::size_t take = words_that_fit(rest, c.available_width - pen_x,
                                                    child.font_size, child.face, measure_text);
            if (take == 0) {
                // Nothing fits in what is left of this line: start a new one,
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
    /// Returns the width of the widest space-separated word in a text box, which
    /// is its min-content width; zero for any other box. Also used by
    /// measure_box in lib/Layout/algorithm.cpp.
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
