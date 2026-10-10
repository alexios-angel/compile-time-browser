//===- block.hpp - Block formatting context ------------------- -*- C++ -*-===//
//
// The block formatting context: block_flow stacks a box's children vertically
// and collapses their vertical margins (CSS 2.2 §8.3.1).
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctbrowser/layout/algorithm/inline.hpp>

namespace ctbrowser::layout {

/// The block formatting context. Children stack vertically, each filling the
/// content width unless it says otherwise.
///
/// Vertical margins are resolved in two halves. A child lays out its own
/// subtree independently and returns the adjoining margins it exposes; this
/// context then merges those struts while it performs the sequential stacking
/// pass it already needed for child heights. That is why margin collapsing does
/// not cost the parallel driver its independence: a worker never needs a
/// sibling's margin to lay out local coordinates, only the final assembly needs
/// it to choose `y`.
struct block_flow {
    [[nodiscard]] intrinsic_sizes measure(const box_node & b, const constraints & c,
                                          const measure_text_fn & measure_text) const {
        intrinsic_sizes out;
        for (const box_node & child : b.children) {
            // An out-of-flow child contributes nothing to what its parent's
            // content wants to be: it is not part of that content.
            if (child.is_out_of_flow()) { continue; }
            // outer_intrinsic dispatches on the child's kind (text, replaced,
            // table, flex), so this loop must not special-case any of them.
            const intrinsic_sizes child_sizes = outer_intrinsic(child, c, measure_text);
            out.min_content = std::max(out.min_content, child_sizes.min_content);
            out.max_content = std::max(out.max_content, child_sizes.max_content);
        }
        return out;
    }

    [[nodiscard]] fragment arrange(const box_node & b, const constraints & c,
                                   const measure_text_fn & measure_text) const {
        const resolved_edges edges = resolve_edges(b, c);
        // An inline-level block (`display: inline-block`, e.g. a `.badge`)
        // shrinks to fit rather than filling its containing block.
        //
        // `forced_width` still wins: a flex item's main size was decided by its
        // line, and an inline-block that is also a flex item is blockified anyway.
        const float outer_width = b.inline_level && b.width.is_auto() && c.forced_width < 0
                                      ? shrink_to_fit_width(b, c, edges, measure_text)
                                      : outer_width_of(b, c, edges, measure_text);
        const float content_width = std::max(0.0f, outer_width - edges.horizontal_inner());

        // The box's own height is resolved before its children are laid out,
        // because a child's percentage height (e.g. Bootstrap's `.h-100`)
        // resolves against it.
        // `stretch` against a definite height is the containing block's height
        // less this box's margins, as a border box. A calc-size() over
        // `stretch` runs its calculation over that size, measured in the box
        // box-sizing names.
        const auto stretched = [&] {
            const float border =
                std::max(0.0f, c.available_height - edges.margin_top - edges.margin_bottom);
            if (!b.height.has_calculation()) { return border; }
            const float inner = edges.vertical_inner();
            const float size = b.border_box ? border : std::max(0.0f, border - inner);
            const float applied = b.height.apply(size, c.available_height, b.font_size);
            return std::max(0.0f, b.border_box ? applied : applied + inner);
        };
        const float declared_height =
            b.height_stretch && c.available_height > 0 ? stretched()
            : has_definite_height(b, c)
                ? std::max(0.0f,
                           border_box_size(b, b.height, c.available_height, edges.vertical_inner()))
                : -1.0f;
        // An auto height is clamped only after its content is known; a stated
        // one is known now and supplies the percentage basis for its children
        // at that used size.
        const float stated_height =
            declared_height >= 0 ? clamp_used_height(b, c, edges, declared_height, -1.0f) : -1.0f;
        const float inner_height =
            stated_height >= 0 ? std::max(0.0f, stated_height - edges.vertical_inner()) : 0.0f;

        fragment out;
        out.box = &b;
        out.source = b.source;
        out.block_margins.before.append(edges.margin_top);
        out.block_margins.after.append(edges.margin_bottom);

        // A formatting-context boundary keeps its first/last child's margins
        // inside. Siblings within that boundary still collapse with each other.
        // `html` and the synthetic document box are both stopped: the root
        // element's margins never collapse (CSS 2.2 §8.3.1).
        const bool is_document_root = b.tag == "html" || (b.tag.empty() && b.source);
        const bool suppress_edge_collapse = c.suppress_margin_collapse || b.inline_level ||
                                            b.blocks_margin_collapse || b.is_out_of_flow() ||
                                            is_document_root;
        const bool min_height_is_zero =
            b.min_height.is_auto() || b.min_height.resolve(c.available_height, b.font_size) <= 0;
        const bool top_edge_is_open = edges.pad_top == 0 && edges.border_top == 0;
        const bool bottom_edge_is_open = edges.pad_bottom == 0 && edges.border_bottom == 0;
        const bool collapse_first_margin = !suppress_edge_collapse && top_edge_is_open;
        const bool collapse_last_margin =
            !suppress_edge_collapse && bottom_edge_is_open && !has_definite_height(b, c);

        float cursor = edges.content_top();
        bool content_collapses_through = false;
        if (b.establishes_inline_context()) {
            // The box itself is still block-level; only its children share
            // lines, so its own width, height, padding and margins still apply.
            fragment lines =
                inline_flow{}.arrange(b, constraints{content_width, 0, b.font_size}, measure_text);
            for (fragment & line : lines.children) {
                line.bounds.x += edges.content_left();
                line.bounds.y += cursor;
                out.children.push_back(std::move(line));
            }
            cursor += lines.bounds.height;
            // A line box can have zero used height (`line-height: 0`) and still
            // separates the block's top and bottom margins. Geometry alone
            // cannot distinguish it from no line box at all.
            content_collapses_through = !lines.has_line_box;
        } else {
            // The margin left after the previous non-empty border box. It stays
            // as a strut until the next one arrives: reducing it to a scalar
            // early gets a positive/negative/positive chain wrong.
            margin_strut pending;
            // Leading margins escape through an open parent top until the first
            // border box with content closes that edge. Empty blocks do not.
            bool at_collapsible_top = collapse_first_margin;
            bool all_in_flow_children_collapse_through = true;
            for (std::size_t i = 0; i < b.children.size(); ++i) {
                const box_node & child = b.children[i];
                const constraints child_c{content_width, inner_height, child.font_size};
                const resolved_edges child_edges = resolve_edges(child, child_c);
                // An out-of-flow child reserves nothing and is not laid out here:
                // its size depends on a containing block somewhere above, which
                // this context cannot see. It leaves an empty fragment at its
                // static position (where it would have gone), which is the
                // number CSS says to use when its offsets are `auto`.
                // layout::apply_positioning replaces it with the real thing once
                // the ancestor chain is known.
                if (child.is_out_of_flow()) {
                    fragment placeholder;
                    placeholder.box = &child;
                    placeholder.source = child.source;
                    float static_offset = 0;
                    if (!at_collapsible_top) {
                        // Its hypothetical in-flow top margin would collapse
                        // with the preceding pending group. The marker observes
                        // that position without consuming either margin, because
                        // the real out-of-flow box still reserves no space.
                        margin_strut hypothetical = pending;
                        hypothetical.append(child_edges.margin_top);
                        static_offset = hypothetical.value();
                    }
                    placeholder.bounds = rect{edges.content_left() + child_edges.margin_left,
                                              cursor + static_offset, 0, 0};
                    out.children.push_back(std::move(placeholder));
                    continue;
                }
                fragment f = layout_box(child, child_c, measure_text);
                all_in_flow_children_collapse_through &= f.block_margins.through;
                // auto_margin_left, not child_edges.margin_left: `margin: 0 auto`
                // centres a box with a definite width, which is how a page is
                // centred.
                f.margin_left = auto_margin_left(child, child_c, child_edges, f.bounds.width);
                // The right margin is whatever the line has left over once the
                // box and its left margin are placed, which is the resolved one
                // unless it was `auto` (or the pair over-constrains the box).
                f.margin_right = child.margin_right_auto
                                     ? child_c.available_width - f.margin_left - f.bounds.width
                                     : child_edges.margin_right;
                f.margin_top = child_edges.margin_top;
                f.margin_bottom = child_edges.margin_bottom;
                f.bounds.x = edges.content_left() + f.margin_left;
                if (at_collapsible_top) {
                    // The first child's top border edge is the parent's top
                    // border edge. A chain of empty children keeps adjoining and
                    // every one of its margin components escapes with that same
                    // group.
                    out.block_margins.before.append(f.block_margins.before);
                    f.bounds.y = cursor;
                    if (f.block_margins.through) {
                        out.block_margins.before.append(f.block_margins.after);
                    } else {
                        cursor += f.bounds.height;
                        pending = f.block_margins.after;
                        at_collapsible_top = false;
                    }
                } else {
                    margin_strut before = pending;
                    before.append(f.block_margins.before);
                    if (f.block_margins.through) {
                        // Its top border edge is where it would be with a
                        // non-zero bottom border; its bottom margin then joins
                        // the same group without advancing the following box.
                        f.bounds.y = cursor + before.value();
                        pending = before;
                        pending.append(f.block_margins.after);
                    } else {
                        cursor += before.value();
                        f.bounds.y = cursor;
                        cursor += f.bounds.height;
                        pending = f.block_margins.after;
                    }
                }
                out.children.push_back(std::move(f));
            }

            if (!at_collapsible_top) {
                // Reaching this branch means a real border box has separated
                // the last child's margin from the parent's top. The spec's
                // non-zero min-height exception therefore does not apply; it
                // matters only to the collapse-through predicate below.
                if (collapse_last_margin) {
                    out.block_margins.after.append(pending);
                } else {
                    cursor += pending.value();
                }
            }
            content_collapses_through = all_in_flow_children_collapse_through;
        }

        // A zero/auto-height block with no border, padding or effective line
        // box can join its own top and bottom margins. All empty descendants
        // join that same set, so both exposed struts must carry the whole group.
        // The condition is on computed `height`, not on a used height that
        // max-height happened to clamp to zero.
        const bool height_allows_through = declared_height < 0 || declared_height == 0;
        if (!suppress_edge_collapse && top_edge_is_open && bottom_edge_is_open &&
            min_height_is_zero && height_allows_through && content_collapses_through) {
            // Keep the edge struts separate. `through` says they adjoin, and the
            // parent joins them only after using `before` to place this box's
            // own top border edge. Replacing both with the joined value would
            // let a negative bottom margin move that edge.
            out.block_margins.through = true;
        }
        cursor += edges.pad_bottom + edges.border_bottom;

        out.bounds.width = outer_width;
        // A block-size-contained box's content is nothing (CSS Containment
        // 2 §3.2): an auto height is its padding and border.
        if (b.contain_block_size) { cursor = edges.vertical_inner(); }
        // A calc-size() over `auto` or a keyword runs over the height the
        // content came to (`cursor` is that border box) and is then clamped
        // like any other used height.
        const float used =
            stated_height >= 0 ? stated_height
            : b.height.is_intrinsic()
                ? std::max(0.0f, calc_over_content(b, b.height,
                                                   std::max(0.0f, cursor - edges.vertical_inner()),
                                                   c.available_height, edges.vertical_inner()))
                : cursor;
        out.bounds.height = clamp_used_height(b, c, edges, used, cursor);
        out.auto_height = cursor;
        return out;
    }
};

} // namespace ctbrowser::layout
