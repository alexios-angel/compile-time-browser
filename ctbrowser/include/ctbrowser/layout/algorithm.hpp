#pragma once
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <span>
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

// How wide a block-level box gets, and how much of that its content sees.
// Factored out because the parallel driver has to compute the SAME width to
// hand its workers, and two copies of this arithmetic would be two things to
// keep in agreement forever.
[[nodiscard]] float outer_width_of(const box_node & b, const constraints & c,
                                   const resolved_edges & e);
[[nodiscard]] float content_width_of(const box_node & b, const constraints & c,
                                     const resolved_edges & e);
// The left offset an `auto` margin contributes. Beside the other two rather than
// inside block_flow because flex will want the same arithmetic, and two copies of
// it would be two things to keep in agreement forever.
[[nodiscard]] float auto_margin_left(const box_node & b, const constraints & c,
                                     const resolved_edges & e, float outer_width);

// Fragments already laid out for ONE box's children.
//
// This is how the parallel driver hands its results back to the ordinary
// sequential pass rather than reimplementing the assembly itself. That matters:
// it means "parallel layout equals sequential layout" is STRUCTURAL - the same
// stacking code runs either way - instead of two implementations that have to
// be kept agreeing by hand.
struct precomputed {
    const box_node * parent = nullptr;
    std::span<fragment> children; // index-parallel to parent->children
};

// Forward declaration: block and inline contexts nest inside each other, so
// each needs to be able to lay the other out.
[[nodiscard]] fragment layout_box(const box_node & b, const constraints & c,
                                  const measure_text_fn & measure, precomputed * ready = nullptr);

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
[[nodiscard]] inline bool has_definite_height(const box_node & b, const constraints & c) {
    if (b.height.is_auto()) { return false; }
    return b.height.u != unit::percent || c.available_height > 0;
}

// Apply the block used-height constraints. Shared with the parallel driver:
// it has to derive the SAME content-height basis down to its split point before
// workers lay out percentage-height descendants.
[[nodiscard]] inline float clamp_used_height(const box_node & b, const constraints & c,
                                             float value) {
    const float min = b.min_height.is_auto()
                          ? 0.0f
                          : std::max(0.0f, b.min_height.resolve(c.available_height, b.font_size));
    // A percentage maximum against an indefinite containing height is `none`,
    // not zero (CSS 2.2 §10.7). Percentage minimums deliberately keep the
    // zero basis below: their indefinite answer is zero.
    const float max =
        b.max_height.is_auto() || (b.max_height.u == unit::percent && c.available_height <= 0)
            ? -1.0f
            : std::max(0.0f, b.max_height.resolve(c.available_height, b.font_size));
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
// which is why it cannot live in outer_width_of - that one is also called by the
// parallel driver, which has no business measuring anything.
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
                                   const measure_text_fn & measure_text,
                                   precomputed * ready = nullptr) const {
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
            fragment f = layout_box(child, constraints{c.available_width, 0, child.font_size},
                                    measure_text, ready);
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

// --- block formatting context ---------------------------------------------
// Children stack vertically, each filling the content width unless it says
// otherwise.
//
// Vertical margins are resolved in TWO halves. A child lays out its own subtree
// independently and returns the adjoining margins it exposes; this context then
// merges those struts while it performs the sequential stacking pass it already
// needed for child heights. That is why margin collapsing does not cost the
// parallel driver its independence: a worker never needs a sibling's margin to
// lay out local coordinates, only the final assembly needs it to choose `y`.
struct block_flow {
    [[nodiscard]] intrinsic_sizes measure(const box_node & b, const constraints & c,
                                          const measure_text_fn & measure_text) const {
        intrinsic_sizes out;
        for (const box_node & child : b.children) {
            // An out-of-flow child contributes nothing to what its parent's
            // content wants to be: it is not part of that content.
            if (child.is_out_of_flow()) { continue; }
            // A TEXT child is measured directly, not handed to inline_flow -
            // which measures a box's CHILDREN, so it asked a text box what its
            // children were, got none, and every block whose content is text
            // measured as ZERO WIDE. Nothing noticed until a table asked how wide
            // its columns wanted to be and got 0. measure_box owns that
            // distinction now, along with the three cases this loop got wrong.
            const intrinsic_sizes child_sizes = outer_intrinsic(child, c, measure_text);
            out.min_content = std::max(out.min_content, child_sizes.min_content);
            out.max_content = std::max(out.max_content, child_sizes.max_content);
        }
        return out;
    }

    [[nodiscard]] fragment arrange(const box_node & b, const constraints & c,
                                   const measure_text_fn & measure_text,
                                   precomputed * ready = nullptr) const {
        const resolved_edges edges = resolve_edges(b, c);
        // AN INLINE-LEVEL BLOCK SHRINKS TO FIT rather than filling its containing
        // block - `display: inline-block`, which is what a `.badge` and a `.btn`
        // on an `<a>` are. Left as the block rule, every badge on a Bootstrap page
        // was a full-width bar and the anchor button spanned the row.
        //
        // `forced_width` still wins: a flex item's main size was decided by its
        // line, and an inline-block that is also a flex item is blockified anyway.
        const float outer_width = b.inline_level && b.width.is_auto() && c.forced_width < 0
                                      ? shrink_to_fit_width(b, c, edges, measure_text)
                                      : outer_width_of(b, c, edges);
        const float content_width = std::max(0.0f, outer_width - edges.horizontal_inner());
        const bool use_ready =
            ready != nullptr && ready->parent == &b && ready->children.size() == b.children.size();

        // THE BOX'S OWN HEIGHT, resolved BEFORE its children are laid out, because
        // a child's percentage height resolves against it. Left until afterwards -
        // which is where it used to be - every child was handed an available
        // height of ZERO, so `height: 50%` had nothing to be a percentage of and
        // silently became `auto`. Bootstrap's `.h-100` is that declaration, and a
        // card with a header, a body and a footer is three of them.
        const float declared_height =
            has_definite_height(b, c)
                ? std::max(0.0f, b.height.resolve(c.available_height, b.font_size))
                : -1.0f;
        // An auto height is clamped only after its content is known; a stated
        // one is known now and supplies the percentage basis for its children
        // at that used size.
        const float stated_height =
            declared_height >= 0 ? clamp_used_height(b, c, declared_height) : -1.0f;
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
            // The box is STILL BLOCK-LEVEL. Only its children share lines.
            // Conflating the two is what made a block box containing text ignore
            // its own width, height, padding and margins - and since nearly every
            // leaf element in a real document contains only text, that was nearly
            // every leaf element.
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
            // as a STRUT until the next one arrives: reducing it to a scalar
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
                // AN OUT-OF-FLOW CHILD RESERVES NOTHING and is not laid out here:
                // its size depends on a containing block that is somewhere above,
                // which this context cannot see. What it leaves behind is an empty
                // fragment at its STATIC POSITION - where it would have gone - and
                // that is exactly the number CSS says to use when its offsets are
                // `auto`. layout::apply_positioning replaces it with the real
                // thing once the ancestor chain is known.
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
                fragment f = use_ready ? std::move(ready->children[i])
                                       : layout_box(child, child_c, measure_text, ready);
                all_in_flow_children_collapse_through &= f.block_margins.through;
                // auto_margin_left, not child_edges.margin_left: `margin: 0 auto`
                // centres a box with a definite width, and that is the whole of how
                // a page is centred.
                f.bounds.x = edges.content_left() +
                             auto_margin_left(child, child_c, child_edges, f.bounds.width);
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
        // join that SAME set, so both exposed struts must carry the whole group.
        // The condition is on computed `height`, not on a used height that
        // max-height happened to clamp to zero.
        const bool height_allows_through = declared_height < 0 || declared_height == 0;
        if (!suppress_edge_collapse && top_edge_is_open && bottom_edge_is_open &&
            min_height_is_zero && height_allows_through && content_collapses_through) {
            // Keep the edge struts SEPARATE. `through` says they adjoin, and the
            // parent joins them only after using `before` to place this box's
            // own top border edge. Replacing both with the joined value would
            // let a negative bottom margin move that edge.
            out.block_margins.through = true;
        }
        cursor += edges.pad_bottom + edges.border_bottom;

        out.bounds.width = outer_width;
        out.bounds.height = clamp_used_height(b, c, stated_height >= 0 ? stated_height : cursor);
        return out;
    }
};

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
                                   const measure_text_fn & measure_text,
                                   precomputed * = nullptr) const {
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

static_assert(LayoutAlgorithm<table_flow>);
static_assert(LayoutAlgorithm<block_flow>);
static_assert(LayoutAlgorithm<inline_flow>);

// Dispatch: pick the formatting context this box establishes.
[[nodiscard]] fragment layout_box(const box_node & b, const constraints & c,
                                  const measure_text_fn & measure, precomputed * ready);

} // namespace ctbrowser::layout
