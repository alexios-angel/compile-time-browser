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

// Formatting contexts, one algorithm each.
//
// the previous engine laid everything out in one 300-line function with a chain of ifs for
// tables, inputs, textareas, selects and flow content. Adding flex to that
// means adding branches to a function that already does five unrelated jobs.
// Here a formatting context is a TYPE that satisfies a concept, so flex and
// grid arrive as new types rather than new conditionals - and the concept is
// what makes "did I implement the required shape" a compile error instead of
// a runtime surprise.

namespace ctbrowser::layout {

// The contract every formatting context implements.
//
//   measure  how wide the content wants to be, before deciding how wide it
//            gets. Shrink-to-fit and table columns need this.
//   arrange  place the children and produce the fragment.
template <typename A>
concept LayoutAlgorithm =
    requires(const A & a, const box_node & b, const constraints & c, const measure_text_fn & m) {
        { a.measure(b, c, m) } -> std::same_as<intrinsic_sizes>;
        { a.arrange(b, c, m) } -> std::same_as<fragment>;
    };

// Resolved box edges, in px.
struct resolved_edges {
    float margin_top = 0, margin_right = 0, margin_bottom = 0, margin_left = 0;
    float pad_top = 0, pad_right = 0, pad_bottom = 0, pad_left = 0;
    float border_top = 0, border_right = 0, border_bottom = 0, border_left = 0;

    [[nodiscard]] float horizontal_margin() const noexcept { return margin_left + margin_right; }
    // PADDING AND BORDER TOGETHER, because every caller wants the pair: they are
    // the two things between a box's outer edge and its content, and asking for
    // one without the other is the bug this replaced - a `.btn`'s 1px border was
    // simply not in the arithmetic, so every button was two pixels narrower and
    // two shorter than Chrome's.
    //
    // Named `inner` rather than `padding` so that a call site which really does
    // want the padding alone - the recorder's background inset, one day - has to
    // say so.
    [[nodiscard]] float horizontal_inner() const noexcept {
        return pad_left + pad_right + border_left + border_right;
    }
    [[nodiscard]] float vertical_inner() const noexcept {
        return pad_top + pad_bottom + border_top + border_bottom;
    }
    // Where a box's CONTENT starts, in from its own top left corner.
    [[nodiscard]] float content_left() const noexcept { return pad_left + border_left; }
    [[nodiscard]] float content_top() const noexcept { return pad_top + border_top; }
};

[[nodiscard]] resolved_edges resolve_edges(const box_node & b, const constraints & c);

// A STATED SIZE AS A BORDER BOX. The author's number names the content box
// unless `box-sizing: border-box`, so the padding and border on that axis are
// added around it (CSS UI 3 §3.1); `inner` is the edges' horizontal_inner or
// vertical_inner for the axis in question. Every site that turns a `width`,
// `height`, `min-*`, `max-*` or `flex-basis` into pixels comes through here -
// it is the one place the two box models meet, and before it existed every
// stated size was read as the border box, which is right only on a page that
// sets border-box on `*` as Bootstrap does.
[[nodiscard]] inline float border_box_size(const box_node & b, const length & want, float basis,
                                           float inner) {
    return want.resolve(basis, b.font_size) + (b.border_box ? 0.0f : inner);
}
// THE MIDDLE TERM OF A fit-content CLAMP, as a content size: the argument of
// `fit-content(<length-percentage>)`, which names the box-sizing box like the
// author's width does, or `room` - the available space - for the bare keyword.
[[nodiscard]] inline float fit_content_bound(const box_node & b, const length & want, float room,
                                             float basis, float inner) {
    if (want.u != unit::fit_content || want.fit_bound == unit::auto_) { return room; }
    const float argument =
        length{want.value, want.fit_bound, want.offset_px}.resolve(basis, b.font_size);
    return std::max(0.0f, b.border_box ? argument - inner : argument);
}
// A CONTENT SIZE RUN THROUGH A calc-size() CALCULATION, answered as a border
// box (CSS Values 5 §10.2). The keyword `size` is measured in the box-sizing
// box - the same box the author's number would name - and so is the answer,
// which is what makes `calc-size(auto, size * 2)` double the content box under
// content-box and the border box under border-box. A plain keyword is the
// identity calculation, so this is also how `width: max-content` becomes a
// border box.
[[nodiscard]] inline float calc_over_content(const box_node & b, const length & want, float content,
                                             float basis, float inner) {
    const float size = b.border_box ? content + inner : content;
    const float sized = want.apply(size, basis, b.font_size);
    return b.border_box ? sized : sized + inner;
}

// How wide a block-level box gets, and how much of that its content sees. The
// measure function is for a `width`, `min-width` or `max-width` spelled as an
// intrinsic sizing keyword, which only the box's content can answer.
[[nodiscard]] float outer_width_of(const box_node & b, const constraints & c,
                                   const resolved_edges & e, const measure_text_fn & measure);
[[nodiscard]] float content_width_of(const box_node & b, const constraints & c,
                                     const resolved_edges & e, const measure_text_fn & measure);
// A `min-content`, `max-content` or `fit-content` width AS A BORDER BOX (CSS
// Sizing 3 §5), from measure_box's content sizes: fit-content is the
// shrink-to-fit clamp between the other two within the available space. Every
// site that resolves a width asks this for a keyword and resolve() otherwise.
[[nodiscard]] float intrinsic_border_width(const box_node & b, const constraints & c,
                                           const resolved_edges & e,
                                           const measure_text_fn & measure, const length & want);
// A min-width / max-width bound, or nullopt for `auto`/`none`: resolve() for a
// number, intrinsic_border_width for a keyword.
[[nodiscard]] std::optional<float> width_bound(const box_node & b, const constraints & c,
                                               const resolved_edges & e,
                                               const measure_text_fn & measure,
                                               const length & want);
// The left offset an `auto` margin contributes. Beside the other two rather than
// inside block_flow because flex will want the same arithmetic, and two copies of
// it would be two things to keep in agreement forever.
[[nodiscard]] float auto_margin_left(const box_node & b, const constraints & c,
                                     const resolved_edges & e, float outer_width);

// Forward declaration: block and inline contexts nest inside each other, so
// each needs to be able to lay the other out.
[[nodiscard]] fragment layout_box(const box_node & b, const constraints & c,
                                  const measure_text_fn & measure);

// THE MEASURE-SIDE TWIN OF layout_box: how wide a box's content wants to be,
// dispatched on what the box IS.
//
// block_flow::measure used to hand-roll this dispatch inline, and got three
// cases wrong because they were invisible from where it was written: a REPLACED
// child measured as zero (its children are not laid out, so asking them gives
// nothing), a nested TABLE measured as a block, and - once one existed - a FLEX
// container measured as the widest of its items rather than the sum. The first
// two were latent: block_flow::measure is only reached from a table cell, and no
// page in the suite puts an image or a table in one. Flex reaches it on every
// item of every row, which is what made the third one worth fixing all three.
//
// Declared before the contexts and defined in the .cpp, so flex - which lives in
// its own header to keep this one readable - can be one of the cases without
// this header having to know the type.
[[nodiscard]] intrinsic_sizes measure_box(const box_node & b, const constraints & c,
                                          const measure_text_fn & measure);

// What a CHILD contributes to the intrinsic size of the box that holds it: its
// own BORDER box plus its margins, with a stated `width` and the min/max clamps
// applied.
//
// A different question from measure_box, which answers for a box's CONTENT
// because that is what its own formatting context asks. block_flow::measure and
// inline_flow::measure asked measure_box and then added nothing, so everything
// outside a child's content box was silently dropped - a `<td>` holding a
// `<div style="padding: 50px">hello</div>` measured 19px wide, and the div was
// then laid out at a content width of max(0, 19 - 100) = 0, where words_that_fit
// answers 0 for a non-positive width and THE TEXT DISAPPEARED. Before flex that
// path was reachable only from a table cell, which is why it went unnoticed; flex
// reaches it on every item whose base size comes from its content, and the
// visible symptom there was a Bootstrap `.nav-item` measuring exactly its
// `.nav-link` child's 32px of padding too narrow.
[[nodiscard]] intrinsic_sizes outer_intrinsic(const box_node & child, const constraints & c,
                                              const measure_text_fn & measure);

// Is this box's `height` a usable number?
//
// A PERCENTAGE AGAINST AN UNKNOWN CONTAINING BLOCK IS NOT ONE. `available_height
// == 0` means "as tall as it needs to be" (fragment.hpp), so `height: 100%` there
// has nothing to be a percentage OF, and CSS 2.1 §10.5 says it behaves as `auto`.
// Resolving it against zero instead COLLAPSES THE BOX - and Bootstrap's `.h-100`
// is exactly that declaration, so a row of three cards came out zero-high and
// everything below it drew straight through them.
// A KEYWORD HEIGHT IS NOT ONE EITHER: the block axis has no intrinsic size to
// name, so `height: max-content` behaves as `auto` (CSS Sizing 3 §5.1) and a
// calc-size() over it runs over that automatic height once the content is
// laid out - which is why its percentage children see no definite height, as
// calc-size-height's last test asserts.
[[nodiscard]] inline bool has_definite_height(const box_node & b, const constraints & c) {
    if (b.height.is_auto() || b.height.is_intrinsic()) { return false; }
    return b.height.u != unit::percent || c.available_height > 0;
}

// Apply the block used-height constraints. `auto_height` is the border box the
// content came to, for a `min-height`/`max-height` spelled as a keyword or as a
// calc-size() over one - those name the automatic height - and negative
// before the content is laid out, when such a bound cannot be applied yet.
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
    // MAX FIRST, THEN MIN, so min wins when the two conflict - the same rule as
    // width and flex, and the one CSS Sizing specifies.
    return std::max(min, max < 0 ? value : std::min(value, max));
}

// SHRINK TO FIT - `clamp(min-content, available, max-content)`, CSS 2.1 §10.3.5.
//
// What an INLINE-LEVEL box with an auto width takes, instead of filling its
// containing block the way a block-level one does. That is the whole difference
// between `display: block` and `display: inline-block`, and between `flex` and
// `inline-flex`: same formatting context inside, different width rule outside.
//
// Here rather than inside either context because BOTH need exactly this, and two
// copies would be two answers to one question. It needs the measure function,
// which is why it cannot live in outer_width_of.
[[nodiscard]] float shrink_to_fit_width(const box_node & b, const constraints & c,
                                        const resolved_edges & e, const measure_text_fn & measure);

// How much of `text` fits in `available`, measured in whole words. A break
// opportunity is AFTER a run of spaces, and a LEADING run of spaces belongs
// to the first candidate rather than being a zero-width candidate of its
// own. That distinction is the whole bug: the whitespace between two
// elements is a text run that IS a space, and treating the position before
// it as a break opportunity meant nothing ever fit - so a lone space
// wrapped the line and every label was left sitting above its control.
//
// A free function rather than a private member of inline_flow, because a
// textarea soft-wraps its value with exactly this rule and the shell has to be
// able to call it. Two greedy wrappers would be two answers to "where does this
// line break", and a field that disagrees with the page around it is the bug
// this being shared prevents.
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
