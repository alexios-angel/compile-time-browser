//===- common.hpp - Shared layout algorithm types ------------- -*- C++ -*-===//
//
// What every formatting context shares: the LayoutAlgorithm concept, resolved
// box edges, the width and height arithmetic, and the greedy word wrapper.
//
// Each formatting context is a type satisfying LayoutAlgorithm, so a new
// context (flex, grid) is a new type rather than another branch in one layout
// function, and a missing member is a compile error.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/layout/box.hpp>
#include <ctbrowser/layout/fragment.hpp>
#include <ctbrowser/layout/values.hpp>

namespace ctbrowser::layout {

/// The contract every formatting context implements.
///
///   measure  how wide the content wants to be, before deciding how wide it
///            gets. Shrink-to-fit and table columns need this.
///   arrange  place the children and produce the fragment.
template <typename A>
concept LayoutAlgorithm =
    requires(const A & a, const box_node & b, const constraints & c, const measure_text_fn & m) {
        { a.measure(b, c, m) } -> std::same_as<intrinsic_sizes>;
        { a.arrange(b, c, m) } -> std::same_as<fragment>;
    };

/// Resolved box edges, in px.
struct resolved_edges {
    float margin_top = 0, margin_right = 0, margin_bottom = 0, margin_left = 0;
    float pad_top = 0, pad_right = 0, pad_bottom = 0, pad_left = 0;
    float border_top = 0, border_right = 0, border_bottom = 0, border_left = 0;

    [[nodiscard]] float horizontal_margin() const noexcept { return margin_left + margin_right; }
    /// Padding plus border on this axis: everything between the outer edge and
    /// the content. Named `inner`, not `padding`, so code that wants padding
    /// alone has to say so.
    [[nodiscard]] float horizontal_inner() const noexcept {
        return pad_left + pad_right + border_left + border_right;
    }
    [[nodiscard]] float vertical_inner() const noexcept {
        return pad_top + pad_bottom + border_top + border_bottom;
    }
    /// Where a box's content starts, in from its own top left corner.
    [[nodiscard]] float content_left() const noexcept { return pad_left + border_left; }
    [[nodiscard]] float content_top() const noexcept { return pad_top + border_top; }
};

/// Resolves `b`'s margins, padding and borders to px against the containing
/// block in `c`.
[[nodiscard]] resolved_edges resolve_edges(const box_node & b, const constraints & c);

/// A stated size as a border box. The author's number names the content box
/// unless `box-sizing: border-box`, so the padding and border on that axis are
/// added around it (CSS UI 3 §3.1); `inner` is the edges' horizontal_inner or
/// vertical_inner for the axis in question. Every site that turns a `width`,
/// `height`, `min-*`, `max-*` or `flex-basis` into pixels comes through here,
/// so this is the one place the two box models meet.
[[nodiscard]] inline float border_box_size(const box_node & b, const length & want, float basis,
                                           float inner) {
    return want.resolve(basis, b.font_size) + (b.border_box ? 0.0f : inner);
}
/// The middle term of a fit-content clamp, as a content size: the argument of
/// `fit-content(<length-percentage>)`, which names the box-sizing box like the
/// author's width does, or `room` (the available space) for the bare keyword.
[[nodiscard]] inline float fit_content_bound(const box_node & b, const length & want, float room,
                                             float basis, float inner) {
    if (want.u != unit::fit_content || want.fit_bound == unit::auto_) { return room; }
    const float argument =
        length{want.value, want.fit_bound, want.offset_px}.resolve(basis, b.font_size);
    return std::max(0.0f, b.border_box ? argument - inner : argument);
}
/// A content size run through a calc-size() calculation, answered as a border
/// box (CSS Values 5 §10.2). The keyword `size` is measured in the box-sizing
/// box (the same box the author's number would name), and so is the answer,
/// so `calc-size(auto, size * 2)` doubles the content box under content-box
/// and the border box under border-box. A plain keyword is the identity
/// calculation, so this is also how `width: max-content` becomes a border box.
[[nodiscard]] inline float calc_over_content(const box_node & b, const length & want, float content,
                                             float basis, float inner) {
    const float size = b.border_box ? content + inner : content;
    const float sized = want.apply(size, basis, b.font_size);
    return b.border_box ? sized : sized + inner;
}

/// How wide a block-level box gets (outer_width_of), and how much of that its
/// content sees (content_width_of). The measure function is for a `width`,
/// `min-width` or `max-width` spelled as an intrinsic sizing keyword, which
/// only the box's content can answer.
[[nodiscard]] float outer_width_of(const box_node & b, const constraints & c,
                                   const resolved_edges & e, const measure_text_fn & measure);
[[nodiscard]] float content_width_of(const box_node & b, const constraints & c,
                                     const resolved_edges & e, const measure_text_fn & measure);
/// A `min-content`, `max-content` or `fit-content` width as a border box (CSS
/// Sizing 3 §5), from measure_box's content sizes: fit-content is the
/// shrink-to-fit clamp between the other two within the available space. Every
/// site that resolves a width asks this for a keyword and resolve() otherwise.
[[nodiscard]] float intrinsic_border_width(const box_node & b, const constraints & c,
                                           const resolved_edges & e,
                                           const measure_text_fn & measure, const length & want);
/// A min-width / max-width bound, or nullopt for `auto`/`none`: resolve() for
/// a number, intrinsic_border_width for a keyword.
[[nodiscard]] std::optional<float> width_bound(const box_node & b, const constraints & c,
                                               const resolved_edges & e,
                                               const measure_text_fn & measure,
                                               const length & want);
/// The left offset an `auto` margin contributes. Shared here rather than kept
/// inside block_flow so that every context uses the same arithmetic.
[[nodiscard]] float auto_margin_left(const box_node & b, const constraints & c,
                                     const resolved_edges & e, float outer_width);

/// Lays out `b` with the formatting context it establishes. Declared here
/// because block and inline contexts nest inside each other, so each needs to
/// be able to lay the other out.
[[nodiscard]] fragment layout_box(const box_node & b, const constraints & c,
                                  const measure_text_fn & measure);

/// The measure-side twin of layout_box: how wide a box's content wants to be,
/// dispatched on what the box is (replaced element, table, flex container or
/// flow). A replaced box is measured from its intrinsic size, since its
/// children are never laid out. Defined in the .cpp so flex, in its own header,
/// can be a case without this header knowing its type.
[[nodiscard]] intrinsic_sizes measure_box(const box_node & b, const constraints & c,
                                          const measure_text_fn & measure);

/// What a child contributes to its parent's intrinsic size: its border box
/// plus margins, with a stated `width` and min/max clamps applied. Unlike
/// measure_box, which answers for the content box only; using measure_box here
/// drops padding, border and margins, and a padded child is then laid out too
/// narrow for its own text.
[[nodiscard]] intrinsic_sizes outer_intrinsic(const box_node & child, const constraints & c,
                                              const measure_text_fn & measure);

/// Whether this box's `height` is a usable (definite) number.
///
/// A percentage against an unknown containing block is not: `available_height
/// == 0` means "as tall as it needs to be" (fragment.hpp), so `height: 100%`
/// there has nothing to be a percentage of, and CSS 2.1 §10.5 says it behaves
/// as `auto`. Resolving it against zero instead would collapse the box (e.g.
/// Bootstrap's `.h-100` cards).
///
/// A keyword height is not either: the block axis has no intrinsic size to
/// name, so `height: max-content` behaves as `auto` (CSS Sizing 3 §5.1) and a
/// calc-size() over it runs over that automatic height once the content is
/// laid out. Its percentage children therefore see no definite height, as
/// calc-size-height's last test asserts.
[[nodiscard]] inline bool has_definite_height(const box_node & b, const constraints & c) {
    // `stretch` is the containing block's height, which is definite or not.
    if (b.height_stretch) { return c.available_height > 0; }
    if (b.height.is_auto() || b.height.is_intrinsic()) { return false; }
    return b.height.u != unit::percent || c.available_height > 0;
}

/// Applies `min-height` and `max-height` to the used height `value`.
/// `auto_height` is the border box the content came to, needed for a bound
/// spelled as a keyword or as a calc-size() over one (those name the automatic
/// height); it is negative before the content is laid out, when such a bound
/// cannot be applied yet and is skipped.
[[nodiscard]] inline float clamp_used_height(const box_node & b, const constraints & c,
                                             const resolved_edges & e, float value,
                                             float auto_height) {
    const float inner = e.vertical_inner();
    const auto bound = [&](const length & want, bool is_max) {
        if (want.is_auto()) { return -1.0f; }
        if (want.is_intrinsic()) {
            if (auto_height < 0) { return -1.0f; }
            return std::max(0.0f, calc_over_content(b, want, std::max(0.0f, auto_height - inner),
                                                    c.available_height, inner));
        }
        // A percentage maximum against an indefinite containing height is
        // `none`, not zero (CSS 2.2 §10.7). Percentage minimums deliberately
        // keep the zero basis: their indefinite answer is zero.
        if (is_max && want.u == unit::percent && c.available_height <= 0) { return -1.0f; }
        return std::max(0.0f, border_box_size(b, want, c.available_height, inner));
    };
    const float min = std::max(0.0f, bound(b.min_height, false));
    const float max = bound(b.max_height, true);
    // Max first, then min, so min wins when the two conflict: the same rule as
    // width and flex, and the one CSS Sizing specifies.
    return std::max(min, max < 0 ? value : std::min(value, max));
}

/// Shrink-to-fit width: `clamp(min-content, available, max-content)`, CSS 2.1
/// §10.3.5.
///
/// What an inline-level box with an auto width takes, instead of filling its
/// containing block the way a block-level one does. That is the difference
/// between `display: block` and `display: inline-block`, and between `flex` and
/// `inline-flex`: same formatting context inside, different width rule outside.
/// Shared here because both block and flex need it; it needs the measure
/// function, which is why it is not part of outer_width_of.
[[nodiscard]] float shrink_to_fit_width(const box_node & b, const constraints & c,
                                        const resolved_edges & e, const measure_text_fn & measure);

/// Returns how many bytes of `text` fit in `available`, in whole words. A
/// break opportunity is after a run of spaces, and a leading run of spaces
/// belongs to the first candidate rather than being a zero-width candidate of
/// its own. The whitespace between two elements is a text run that is only a
/// space; if the position before it were a break opportunity, nothing would
/// ever fit and a lone space would wrap the line.
///
/// A free function rather than a private member of inline_flow because a
/// textarea soft-wraps its value with exactly this rule and the shell calls
/// it, so a field and the page around it break lines the same way.
[[nodiscard]] inline std::size_t words_that_fit(std::string_view text, float available,
                                                float font_size, const text_face & face,
                                                const measure_text_fn & measure_text) {
    if (available <= 0) { return 0; }
    std::size_t fits = 0;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = at;
        while (end < text.size() && text[end] == ' ') { ++end; }
        while (end < text.size() && text[end] != ' ') { ++end; }
        if (measure_text(text.substr(0, end), font_size, face) > available) { break; }
        fits = end;
        at = end;
    }
    // A single word longer than the line still has to go somewhere, or
    // layout makes no progress and loops forever. It overflows, which is
    // what a browser does with an unbreakable word.
    if (fits == 0) {
        std::size_t end = 0;
        while (end < text.size() && text[end] == ' ') { ++end; }
        while (end < text.size() && text[end] != ' ') { ++end; }
        return end;
    }
    return fits;
}

} // namespace ctbrowser::layout
