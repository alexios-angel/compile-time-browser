//===- ctbrowser/lib/Layout/box.cpp - Box tree construction -----*- C++ -*-===//
//
// box_builder: turns the styled DOM into the box tree that layout runs on,
// dropping collapsible whitespace and wrapping inline runs in anonymous boxes
// where a block or flex container needs them.
//
//===----------------------------------------------------------------------===//
#include <ctbrowser/layout/box.hpp>

namespace ctbrowser::layout {

box_node box_builder::build(const read_txn & txn, node_id root) {
    box_node out;
    out.kind = box_kind::block;
    out.source = root;
    out.style = style_of(root);
    // The root element's box is styled like any other (its width, height,
    // margins, padding and border apply, so `html { height: 3000px }` makes the
    // page scroll). Its overflow belongs to the viewport instead (CSS Overflow 3
    // §3.3), so the box itself neither scrolls nor clips. The tag stays empty so
    // that body propagation can tell its parent is the root.
    style_box(out, out.style, display_kind::block, 16.0f, text_face{}, false, false);
    out.scroll_container = out.clips_overflow = out.blocks_margin_collapse = false;
    build_children(txn, root, out, out.font_size, out.face, out.underline, out.line_through);
    drop_collapsible_spaces(out);
    normalise(out);
    return out;
}

computed_style_ptr box_builder::style_of(node_id id) const {
    const auto it = styles_->find(style::engine::key_of(id));
    return it == styles_->end() ? computed_style_ptr{} : it->second;
}

std::string_view box_builder::prop(const computed_style_ptr & s, atom name) const {
    return s ? s->get(name) : std::string_view{};
}

void box_builder::drop_collapsible_spaces(box_node & parent) {
    const auto renders_inline = [](const box_node & b) {
        return !b.is_block_level() && !b.collapsible_space;
    };
    std::vector<box_node> kept;
    kept.reserve(parent.children.size());
    for (std::size_t i = 0; i < parent.children.size(); ++i) {
        if (parent.children[i].collapsible_space) {
            const bool inline_before = !kept.empty() && renders_inline(kept.back());
            bool inline_after = false;
            for (std::size_t j = i + 1; j < parent.children.size(); ++j) {
                if (parent.children[j].collapsible_space) { continue; }
                inline_after = renders_inline(parent.children[j]);
                break;
            }
            if (!inline_before || !inline_after) { continue; }
        }
        kept.push_back(std::move(parent.children[i]));
    }
    parent.children = std::move(kept);
}

/// One anonymous box wrapping a run of inline-level children, in its parent's
/// inherited context.
///
/// The wrapper takes the parent's line height because inline_flow::arrange
/// reads line_height from the containing box, not from each text run.
box_node box_builder::wrap_run(const box_node & parent, std::vector<box_node> & run) {
    box_node wrapper;
    wrapper.kind = box_kind::anonymous;
    // Only the inherited half. An anonymous box inherits the inherited properties
    // and takes the initial value for everything else (CSS 2.1 §9.2.1.1): it has
    // no rules of its own, because it has no element for a rule to match.
    //
    // Copying only the inherited half keeps the parent's background, border,
    // padding and shadow from being painted a second time around the wrapped
    // text, and needs no list of excluded properties.
    wrapper.style = parent.style
                        ? computed_style_ptr{new style::computed_style{{}, parent.style->inherited}}
                        : computed_style_ptr{};
    wrapper.font_size = parent.font_size;
    wrapper.line_height = parent.line_height;
    // An anonymous box has no style of its own, so it takes its parent's
    // alignment along with everything else it inherits.
    wrapper.text_align = parent.text_align;
    wrapper.children = std::move(run);
    run.clear();
    return wrapper;
}

void box_builder::normalise(box_node & parent) {
    // A flex container's children are all flex items, and its rule is not the
    // block one below. Flexbox 1 §4: every in-flow child element is a flex item
    // whatever its display, and each contiguous sequence of child text runs is
    // wrapped in one anonymous block container that is then a flex item.
    //
    // This differs from the block rule in two ways.
    //
    // It runs unconditionally, where the block rule fires only on mixed content,
    // because a block whose children are all inline simply runs an inline
    // formatting context instead, and a flex container has none to fall back on.
    // `<div class=d-flex>text</div>` is homogeneous and still needs wrapping.
    //
    // And it wraps text, not "everything inline-level". A replaced element is
    // inline-level and is nonetheless an item in its own right: wrapping `<img>`
    // in an anonymous box would make two images one item and lay them out on a
    // line inside it. Every other element child is already block-level, because
    // being a flex item blockified it.
    if (parent.kind == box_kind::flex) {
        std::vector<box_node> rebuilt;
        std::vector<box_node> run;
        // A run that is only whitespace is not rendered at all (§4 again), which
        // is what stops the newline between two `<img>`s becoming a third column.
        // drop_collapsible_spaces has already removed the ones between block-level
        // siblings; this is the same rule for the ones it could not see.
        const auto flush = [&] {
            if (run.empty()) { return; }
            const bool blank = std::all_of(run.begin(), run.end(), [](const box_node & t) {
                return t.collapsible_space || trimmed(t.text).empty();
            });
            if (!blank) { rebuilt.push_back(wrap_run(parent, run)); }
            run.clear();
        };
        for (box_node & c : parent.children) {
            if (c.kind == box_kind::text) {
                run.push_back(std::move(c));
            } else {
                flush();
                rebuilt.push_back(std::move(c));
            }
        }
        flush();
        parent.children = std::move(rebuilt);
        return;
    }

    bool has_block = false;
    bool has_inline = false;
    for (const box_node & c : parent.children) {
        (c.is_block_level() ? has_block : has_inline) = true;
    }
    if (!has_block || !has_inline) { return; } // homogeneous: nothing to do

    std::vector<box_node> rebuilt;
    std::vector<box_node> run;
    for (box_node & c : parent.children) {
        if (c.is_block_level()) {
            if (!run.empty()) { rebuilt.push_back(wrap_run(parent, run)); }
            rebuilt.push_back(std::move(c));
        } else {
            run.push_back(std::move(c));
        }
    }
    if (!run.empty()) { rebuilt.push_back(wrap_run(parent, run)); }
    parent.children = std::move(rebuilt);
}

std::string box_builder::collapse_whitespace(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool in_run = false;
    for (const char c : text) {
        if (html_whitespace.contains(c)) {
            if (!in_run) { out.push_back(' '); }
            in_run = true;
        } else {
            out.push_back(c);
            in_run = false;
        }
    }
    return out;
}

float box_builder::parse_number(std::string_view text) {
    float value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} ? value : 0;
}

std::string box_builder::first_family(std::string_view list) {
    std::string_view first = trim(list.substr(0, list.find(',')), " \t");
    // Quoted names - `font-family: "Fira Sans"` - are the same name.
    if (first.size() >= 2 && (first.front() == '"' || first.front() == '\'') &&
        first.back() == first.front()) {
        first = first.substr(1, first.size() - 2);
    }
    return std::string{first};
}

} // namespace ctbrowser::layout
