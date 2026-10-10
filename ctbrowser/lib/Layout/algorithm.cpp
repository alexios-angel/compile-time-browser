//===- ctbrowser/lib/Layout/algorithm.cpp - Width and intrinsic sizing ----===//
//
// The box-model arithmetic shared by every formatting context: resolving
// edges, a box's used width and auto margins, intrinsic (min/max-content)
// measurement with a per-call cache, and layout_box, which dispatches a box to
// the formatting context it establishes.
//
//===----------------------------------------------------------------------===//
#include <ctbrowser/layout/algorithm.hpp>

#include <ctbrowser/layout/flex.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <unordered_map>

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

float intrinsic_border_width(const box_node & b, const constraints & c, const resolved_edges & e,
                             const measure_text_fn & measure, const length & want) {
    const float inner = e.horizontal_inner();
    const float room = std::max(0.0f, c.available_width - e.horizontal_margin() - inner);
    float content = 0;
    if (want.u == unit::auto_ && !b.inline_level && !b.is_out_of_flow()) {
        // `auto` as a basis, as in `calc-size(auto, size * 2)`, means what auto
        // means for this box's width: the available space for a block-level
        // box (the rule outer_width_of applies to a plain `auto`). An
        // inline-level or out-of-flow box takes the fit-content branch below.
        content = room;
    } else {
        const intrinsic_sizes wants = measure_box(b, c, measure);
        switch (want.u) {
        case unit::min_content: content = wants.min_content; break;
        case unit::max_content: content = wants.max_content; break;
        default:
            // fit-content: clamp(min-content, stretch, max-content), the same
            // clamp shrink_to_fit_width makes for an inline-block - or the
            // argument of fit-content(<length-percentage>) in place of stretch.
            content = std::max(wants.min_content,
                               std::min(fit_content_bound(b, want, room, c.available_width, inner),
                                        wants.max_content));
            break;
        }
    }
    return std::max(0.0f,
                    calc_over_content(b, want, std::max(0.0f, content), c.available_width, inner));
}

std::optional<float> width_bound(const box_node & b, const constraints & c,
                                 const resolved_edges & e, const measure_text_fn & measure,
                                 const length & want) {
    if (want.is_auto()) { return std::nullopt; }
    if (want.is_intrinsic()) { return intrinsic_border_width(b, c, e, measure, want); }
    return border_box_size(b, want, c.available_width, e.horizontal_inner());
}

float outer_width_of(const box_node & b, const constraints & c, const resolved_edges & e,
                     const measure_text_fn & measure) {
    // A width the parent already decided is used verbatim and not re-clamped
    // here: flex applied min/max-width inside its freeze loop, where the clamp
    // interacts with every other item on the line. Clamping a second time would
    // be right only for the items that never hit a constraint, which is the
    // subset for which it does nothing.
    if (c.forced_width >= 0) { return c.forced_width; }
    const float unclamped =
        width_bound(b, c, e, measure, b.width).value_or(c.available_width - e.horizontal_margin());
    // Apply max-width first and min-width second, so min-width wins when they
    // conflict, as CSS 2.1 §10.4 requires, without a special case.
    float out = unclamped;
    if (const auto hi = width_bound(b, c, e, measure, b.max_width)) { out = std::min(out, *hi); }
    if (const auto lo = width_bound(b, c, e, measure, b.min_width)) { out = std::max(out, *lo); }
    return std::max(0.0f, out);
}

float auto_margin_left(const box_node & b, const constraints & c, const resolved_edges & e,
                       float outer_width) {
    // `margin: 0 auto` on a box with a definite width centres it. length::resolve
    // has no answer for `auto`, so auto margins are resolved here from the
    // remaining space.
    //
    // An auto width leaves both margins at zero, per CSS: the width absorbs the
    // remainder instead.
    if (b.width.is_auto()) { return e.margin_left; }
    // Read the flags, not is_auto(): an unset margin is also `unit::auto_`, so asking the
    // length would centre every definite-width box that declared no margins.
    const bool left_auto = b.margin_left_auto;
    const bool right_auto = b.margin_right_auto;
    if (!left_auto && !right_auto) { return e.margin_left; }
    const float remainder = c.available_width - outer_width;
    if (remainder <= 0) { return left_auto ? 0.0f : e.margin_left; }
    if (left_auto && right_auto) { return remainder / 2.0f; }
    return left_auto ? remainder : e.margin_left;
}

float content_width_of(const box_node & b, const constraints & c, const resolved_edges & e,
                       const measure_text_fn & measure) {
    return std::max(0.0f, outer_width_of(b, c, e, measure) - e.horizontal_inner());
}

namespace {

/// Cache key for measure_box: one measurement per (box, constraints) for the
/// duration of the outermost measure_box call. A flex container measures each
/// item twice (outer_intrinsic, and again for a growable one), so without the
/// cache thirty nested `display: flex; flex-grow: 1` divs would make 2^30 calls.
/// measure_box is a pure function of the box, its constraints and the measure
/// function, and the box tree is const for the whole call, so a hit is the same
/// answer bit for bit. The floats are keyed by their bits, not by ==, so -0 and
/// 0 stay distinct.
struct measure_key {
    const box_node * box;
    const measure_text_fn * measure;
    std::array<std::uint32_t, 4> c;
    bool suppress;
    bool operator==(const measure_key &) const = default;
};
/// Mixes the constraint bits with the 64-bit FNV prime (1099511628211).
struct measure_key_hash {
    std::size_t operator()(const measure_key & k) const noexcept {
        std::size_t h = std::hash<const void *>{}(k.box) ^ std::hash<const void *>{}(k.measure);
        for (const std::uint32_t v : k.c) { h = h * 1099511628211u ^ v; }
        return h ^ static_cast<std::size_t>(k.suppress);
    }
};
using measure_cache = std::unordered_map<measure_key, intrinsic_sizes, measure_key_hash>;
/// The cache of the outermost measure_box on this thread; null outside one.
thread_local measure_cache * active_measure_cache = nullptr;

intrinsic_sizes measure_box_uncached(const box_node & b, const constraints & c,
                                     const measure_text_fn & measure);

} // namespace

intrinsic_sizes measure_box(const box_node & b, const constraints & c,
                            const measure_text_fn & measure) {
    measure_cache local;
    const bool outermost = active_measure_cache == nullptr;
    if (outermost) { active_measure_cache = &local; }
    struct restore {
        bool on;
        ~restore() {
            if (on) { active_measure_cache = nullptr; }
        }
    } const guard{outermost};
    const measure_key key{&b,
                          &measure,
                          {std::bit_cast<std::uint32_t>(c.available_width),
                           std::bit_cast<std::uint32_t>(c.available_height),
                           std::bit_cast<std::uint32_t>(c.font_size),
                           std::bit_cast<std::uint32_t>(c.forced_width)},
                          c.suppress_margin_collapse};
    if (const auto hit = active_measure_cache->find(key); hit != active_measure_cache->end()) {
        return hit->second;
    }
    const intrinsic_sizes out = measure_box_uncached(b, c, measure);
    active_measure_cache->emplace(key, out);
    return out;
}

namespace {

intrinsic_sizes measure_box_uncached(const box_node & b, const constraints & c,
                                     const measure_text_fn & measure) {
    if (b.kind == box_kind::text) {
        return intrinsic_sizes{inline_flow::longest_word(b, measure),
                               measure(b.text, b.font_size, b.face)};
    }
    if (b.is_replaced()) {
        // Its size is the element's. A replaced box's children are not laid out
        // at all, so measuring them answers zero for a 300px canvas.
        return intrinsic_sizes{b.intrinsic_width, b.intrinsic_width};
    }
    // An inline-size-contained box contributes no content width (CSS
    // Containment 2 §3.2), so a container's size never waits on its content.
    if (b.contain_inline_size) { return intrinsic_sizes{}; }
    if (b.kind == box_kind::flex) { return flex_flow{}.measure(b, c, measure); }
    if (b.kind == box_kind::table) { return table_flow{}.measure(b, c, measure); }
    if (b.establishes_inline_context()) { return inline_flow{}.measure(b, c, measure); }
    return block_flow{}.measure(b, c, measure);
}

} // namespace

intrinsic_sizes outer_intrinsic(const box_node & child, const constraints & c,
                                const measure_text_fn & measure) {
    const resolved_edges edges = resolve_edges(child, c);
    const float inner = edges.horizontal_inner();
    const intrinsic_sizes content = measure_box(child, c, measure);
    intrinsic_sizes out{content.min_content + inner, content.max_content + inner};
    // A stated width is the contribution whatever the content wants, as a border
    // box like every other size here. A percentage has no answer while the
    // containing block's own width is the question being asked, so there the
    // content size stands; that is also why `.row > * { max-width: 100% }`
    // never reaches the clamp below.
    // A keyword width picks one of the two sizes being contributed: `width:
    // max-content` contributes its max-content size as both, `min-content` its
    // min-content size as both, and `fit-content` (or `auto` under a
    // calc-size()) contributes as `auto` does - and the calculation, if there is
    // one, runs over each of the two.
    const auto sized = [&](const length & want) -> std::optional<intrinsic_sizes> {
        if (want.is_intrinsic()) {
            float lo = content.min_content;
            float hi = content.max_content;
            if (want.u == unit::min_content) { hi = lo; }
            if (want.u == unit::max_content) { lo = hi; }
            return intrinsic_sizes{calc_over_content(child, want, lo, c.available_width, inner),
                                   calc_over_content(child, want, hi, c.available_width, inner)};
        }
        if (want.is_auto() || want.u == unit::percent) { return std::nullopt; }
        const float stated = std::max(0.0f, border_box_size(child, want, c.available_width, inner));
        return intrinsic_sizes{stated, stated};
    };
    if (const auto stated = sized(child.width)) { out = *stated; }
    // Max first, then min, in the same order and for the same reason as
    // outer_width_of.
    if (const auto hi = sized(child.max_width)) {
        out.min_content = std::min(out.min_content, hi->min_content);
        out.max_content = std::min(out.max_content, hi->max_content);
    }
    if (const auto lo = sized(child.min_width)) {
        out.min_content = std::max(out.min_content, lo->min_content);
        out.max_content = std::max(out.max_content, lo->max_content);
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
    // The measured sizes are the content's; the answer is a border box, like
    // every other width here.
    float out =
        std::max(wants.min_content, std::min(room, wants.max_content)) + e.horizontal_inner();
    // Max first, then min, in the same order and for the same reason as
    // outer_width_of.
    if (const auto hi = width_bound(b, c, e, measure, b.max_width)) { out = std::min(out, *hi); }
    if (const auto lo = width_bound(b, c, e, measure, b.min_width)) { out = std::max(out, *lo); }
    return std::max(0.0f, out);
}

fragment layout_box(const box_node & b, const constraints & c, const measure_text_fn & measure) {
    if (b.kind == box_kind::text) {
        fragment f;
        f.box = &b;
        f.source = b.source;
        f.text = b.text;
        f.bounds = rect{0, 0, measure(b.text, b.font_size, b.face), b.line_height};
        return f;
    }
    // Dispatch on what the box is, not on what its children are. An inline box
    // shrink-wraps to its content; a block box takes its width from the block
    // rules and lays its children out inline or as blocks internally.
    if (b.kind == box_kind::replaced) {
        // Sized by the element, not by content. CSS width/height still win when
        // given - that is how `canvas { width: 100% }` scales the bitmap.
        const resolved_edges edges = resolve_edges(b, c);
        fragment f;
        f.box = &b;
        f.source = b.source;
        // A stated size goes through border_box_size like every other box, so
        // `box-sizing: border-box` is honoured. Only an intrinsic size is a
        // content size that padding and border are added around.
        f.bounds.width = width_bound(b, c, edges, measure, b.width)
                             .value_or(b.intrinsic_width + edges.horizontal_inner());
        f.bounds.height =
            has_definite_height(b, c)
                ? border_box_size(b, b.height, c.available_height, edges.vertical_inner())
                : b.intrinsic_height + edges.vertical_inner();
        return f;
    }
    fragment out;
    if (b.kind == box_kind::table) {
        out = table_flow{}.arrange(b, c, measure);
    } else if (b.kind == box_kind::flex) {
        out = flex_flow{}.arrange(b, c, measure);
    } else if (b.kind == box_kind::inline_) {
        out = inline_flow{}.arrange(b, c, measure);
    } else {
        out = block_flow{}.arrange(b, c, measure);
    }
    // A flex or table container does not use block_flow, but it is still a
    // block-level participant in its parent's formatting context and its own
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
