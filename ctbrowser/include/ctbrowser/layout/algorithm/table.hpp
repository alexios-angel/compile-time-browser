#pragma once

#include <ctbrowser/layout/algorithm/block.hpp>

namespace ctbrowser::layout {

// --- table formatting context ---------------------------------------------
//
// The third formatting context, and the one the LayoutAlgorithm concept was
// written for: a table cannot be laid out as blocks because a cell's width is
// not its own business. Every cell in a column shares that column's width, so
// the whole table has to be MEASURED before any of it can be placed - which is
// exactly the split `measure` and `arrange` make.
//
// AUTO layout, like the previous engine's: each column takes the widest natural content in it,
// and the table shrinks to the sum. A stated `width` scales the columns
// proportionally rather than being ignored.
//
// The DOM shape is <table><tr><td>, with <thead>/<tbody>/<tfoot> transparent -
// the tree builder inserts them, and a page that writes them means the same
// thing as one that does not.
struct table_flow {
    // THE GAP BETWEEN CELLS, AND AROUND THEM - `border-spacing`, and zero under
    // `border-collapse: collapse`.
    //
    // This was a hardcoded 2px, along with a second hardcoded 2px of padding
    // inside every cell, and both were wrong in a way that shows: Bootstrap's
    // Reboot collapses every table's borders, so a striped table drew a sliver of
    // the table's own background between every pair of cells and a sliver of
    // white down each side of one. The padding constant was worse than
    // superfluous - a cell's CSS padding was applied INSIDE it as well, so every
    // cell was inset twice.
    // NOT MODELLED: merging the two borders that meet at a shared edge. Under
    // `border-collapse: collapse` CSS 2.1 §17.6.2 resolves the pair to ONE border
    // drawn once; here each cell paints its own, so a shared edge comes out twice
    // the width. Measured against Chrome on the kitchen fixture: the colour and
    // the outer edges agree exactly - (222,226,230) at x=37 and x=572 in both -
    // and the interior separators are 2px where Chrome's are 1px.
    //
    // Overlapping the cells by a border width was tried and is a DIFFERENT model
    // from Chrome's: Chrome's cells abut exactly (37.00 + 193.45 = 230.45, the
    // next cell's x) and split the shared border between them, so overlapping
    // gave 1px separators and moved the table's right edge 2px. Real conflict
    // resolution is the rung; a second wrong model is not an improvement on the
    // first.
    [[nodiscard]] static float spacing_of(const box_node & table, const constraints & c) {
        if (table.collapse_borders) { return 0; }
        return std::max(0.0f, table.border_spacing.resolve(c.available_width, table.font_size));
    }

    // Rows, flattened through any section elements.
    [[nodiscard]] static std::vector<const box_node *> rows_of(const box_node & table) {
        std::vector<const box_node *> out;
        const auto walk = [&out](auto && self, const box_node & at) -> void {
            for (const box_node & child : at.children) {
                if (child.is_row()) {
                    out.push_back(&child);
                } else if (child.is_row_group()) {
                    self(self, child); // <thead>/<tbody>/<tfoot> are transparent
                }
            }
        };
        walk(walk, table);
        return out;
    }

    // What each column WANTS: the widest cell in it, as a BORDER box.
    //
    // outer_intrinsic rather than measure_box, because a column has to hold the
    // cell's padding and border as well as its text - which is the same question
    // a block asks about its children, asked here of a column.
    [[nodiscard]] static std::vector<float> column_widths(const box_node & table,
                                                          const constraints & c,
                                                          const measure_text_fn & measure_text) {
        std::vector<float> widths;
        for (const box_node * row : rows_of(table)) {
            std::size_t column = 0;
            for (const box_node & cell : row->children) {
                if (!cell.is_cell()) { continue; }
                if (column >= widths.size()) { widths.push_back(0); }
                widths[column] =
                    std::max(widths[column], outer_intrinsic(cell, c, measure_text).max_content);
                ++column;
            }
        }
        return widths;
    }

    [[nodiscard]] intrinsic_sizes measure(const box_node & b, const constraints & c,
                                          const measure_text_fn & measure_text) const {
        const std::vector<float> widths = column_widths(b, c, measure_text);
        const float spacing = spacing_of(b, c);
        float total = spacing;
        for (const float w : widths) { total += w + spacing; }
        return intrinsic_sizes{total, total};
    }

    [[nodiscard]] fragment arrange(const box_node & b, const constraints & c,
                                   const measure_text_fn & measure_text) const {
        const resolved_edges edges = resolve_edges(b, c);
        const float spacing = spacing_of(b, c);
        std::vector<float> widths = column_widths(b, c, measure_text);

        float natural = spacing;
        for (const float w : widths) { natural += w + spacing; }
        // A stated width SCALES the columns rather than being ignored, which is
        // what `<table width=600>` and `table { width: 100% }` mean. The spacing
        // is not scaled with them - it is a fixed length, not a share.
        if (!b.width.is_auto()) {
            const float wanted = b.width.resolve(c.available_width, b.font_size);
            const float gaps = spacing * static_cast<float>(widths.size() + 1);
            if (wanted > 0 && natural > gaps) {
                const float scale = (wanted - gaps) / (natural - gaps);
                if (scale > 0) {
                    for (float & w : widths) { w *= scale; }
                    natural = wanted;
                }
            }
        }

        fragment out;
        out.box = &b;
        out.source = b.source;
        float y = edges.content_top() + spacing;

        // THE CAPTION, above the grid and as wide as it. It is a child of the
        // table that is neither a row nor a row group, so a table that only
        // looked for rows never laid it out at all - it simply vanished.
        for (const box_node & child : b.children) {
            if (child.tag != "caption") { continue; }
            fragment caption =
                block_flow{}.arrange(child, constraints{natural, 0, child.font_size}, measure_text);
            caption.bounds.x = edges.content_left();
            caption.bounds.y = y;
            caption.bounds.width = natural;
            y += caption.bounds.height;
            out.children.push_back(std::move(caption));
            break; // one caption per table, per spec
        }

        for (const box_node * row : rows_of(b)) {
            float x = edges.content_left() + spacing;
            float row_height = 0;
            std::vector<fragment> cells;
            std::size_t column = 0;
            for (const box_node & cell : row->children) {
                if (!cell.is_cell()) { continue; }
                const float column_width = column < widths.size() ? widths[column] : 0;
                // THE COLUMN'S WIDTH IS THE CELL'S BORDER BOX, handed down as a
                // forced width - the same channel a flex item's main size uses,
                // and for the same reason: the size was decided by something
                // other than the box's own `width`, and its content has to wrap
                // to it before its height is known.
                //
                // Its own padding and border are then applied INSIDE by whatever
                // formatting context it runs, which is where they belong and why
                // there is no longer a constant here. Through layout_box because
                // a `<td class="d-flex">` is a flex container.
                fragment placed =
                    layout_box(cell, constraints{natural, 0, cell.font_size, column_width, true},
                               measure_text);
                placed.bounds.x = x;
                placed.bounds.y = y;
                row_height = std::max(row_height, placed.bounds.height);
                cells.push_back(std::move(placed));
                x += column_width + spacing;
                ++column;
            }
            // Every cell in a row is as tall as the tallest, so a row reads as a
            // row rather than as a ragged set of boxes.
            for (fragment & cell : cells) {
                cell.bounds.height = row_height;
                out.children.push_back(std::move(cell));
            }
            // The row itself is a fragment too, so a page can style and hit-test
            // it - a <tr> with a background is ordinary CSS.
            fragment row_fragment;
            row_fragment.box = row;
            row_fragment.source = row->source;
            row_fragment.bounds = rect{edges.content_left() + spacing, y,
                                       std::max(0.0f, natural - 2 * spacing), row_height};
            out.children.push_back(std::move(row_fragment));
            y += row_height + spacing;
        }

        out.bounds.width = natural + edges.horizontal_inner();
        out.bounds.height = y + edges.pad_bottom + edges.border_bottom;
        return out;
    }
};

} // namespace ctbrowser::layout
