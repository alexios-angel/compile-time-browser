//===- computed_style/geometry.hpp - Layout-dependent values ----*- C++ -*-===//
//
// getComputedStyle resolved values that depend on layout: used sizes, insets,
// margins and paddings, border/outline/column-rule widths, line-height,
// display, and the automatic minimum size. resolved_geometry is the entry
// point; computed_probe carries the layout facts it needs.
//
//===----------------------------------------------------------------------===//
#pragma once
#include "internal.hpp"

#include <ctbrowser/shell/metrics.hpp>

namespace ctbrowser::page::detail {
/// The box generated for element `id` under `root`, or null if it has none.
[[nodiscard]] inline const layout::box_node * box_for(const layout::box_node * root, node_id id) {
    if (root == nullptr || !id) { return nullptr; }
    if (root->source == id) { return root; }
    for (const layout::box_node & child : root->children) {
        if (const layout::box_node * hit = box_for(&child, id)) { return hit; }
    }
    return nullptr;
}

/// The fragment laid out for element `id` under `root`, or null if it has none.
[[nodiscard]] inline const layout::fragment * fragment_for(const layout::fragment * root,
                                                           node_id id) {
    if (root == nullptr || !id) { return nullptr; }
    if (root->source == id) { return root; }
    for (const layout::fragment & child : root->children) {
        if (const layout::fragment * hit = fragment_for(&child, id)) { return hit; }
    }
    return nullptr;
}

/// The properties whose value is a length resolved against the containing
/// block. Each is resolved with the same parse_length + length::resolve that
/// layout used, so a percentage or an em gives layout's answer rather than a
/// second one. That inherits layout's gaps: `rem` has a hardcoded 16px basis
/// and vh/vw/pt fall through to pixels (include/ctbrowser/layout/values.hpp).
/// Those are real differences from Chrome and are meant to show up in the diff.
///
/// The four insets are not here: their resolved value is a used value for a
/// positioned box and a computed value otherwise, handled by step 2d of
/// resolved_geometry.
constexpr std::array<std::string_view, 12> length_properties{
    "margin-top",  "margin-right",  "margin-bottom",  "margin-left",
    "padding-top", "padding-right", "padding-bottom", "padding-left",
    "min-width",   "max-width",     "min-height",     "max-height"};

/// The four sizing constraints, whose percentages survive into the computed
/// value.
[[nodiscard]] inline bool is_min_or_max_property(std::string_view property) {
    return property == "min-width" || property == "max-width" || property == "min-height" ||
           property == "max-height";
}

[[nodiscard]] inline bool is_length_property(std::string_view property) {
    return std::find(length_properties.begin(), length_properties.end(), property) !=
           length_properties.end();
}

/// top, right, bottom or left.
[[nodiscard]] inline bool is_inset_property(std::string_view property) {
    return property == "top" || property == "right" || property == "bottom" || property == "left";
}

/// Every property the table types as a `<color>`, plus the `border-color`
/// shorthand.
[[nodiscard]] inline bool is_color_property(std::string_view property) {
    if (property == "border-color") { return true; }
    const style::css::property_syntax * known = style::css::find_property(property);
    return known != nullptr && known->kind == style::css::value_kind::color;
}

/// The four border-*-width longhands. Their used width is 0 when the matching
/// style is `none` or `hidden` (CSS Backgrounds 3 §4.3); see step 2e of
/// resolved_geometry.
[[nodiscard]] inline bool is_border_width_property(std::string_view property) {
    return property == "border-top-width" || property == "border-right-width" ||
           property == "border-bottom-width" || property == "border-left-width";
}

/// CSS Values 4's "snap as a border width": a length of at least one pixel
/// rounds down to whole pixels, and a smaller non-zero one is one pixel. The
/// rule applies to the magnitude, so an outline offset of -2.5px is -2px.
[[nodiscard]] inline float snapped_line_width(float px) {
    const float magnitude = std::fabs(px);
    const float snapped = magnitude >= 1 ? std::floor(magnitude) : (magnitude > 0 ? 1.0f : 0.0f);
    return px < 0 ? -snapped : snapped;
}

/// `border-top-width` -> `border-top-style`, `outline-width` -> `outline-style`.
[[nodiscard]] inline std::string style_property_for_width(std::string_view width_property) {
    return std::string{width_property.substr(0, width_property.size() - 5)} + "style";
}

/// The layout facts about one element that resolved_geometry needs, gathered
/// once per getComputedStyle object. Per-property tree walks would cost
/// O(properties x boxes): the object publishes every property in the table,
/// which would mean about 125 walks over the whole box tree per call.
///
/// The ancestor chain is held as ids rather than as a read transaction, so
/// gathering does not pin a document version for as long as a page keeps the
/// object alive.
struct computed_probe {
    const layout::box_node * box = nullptr;
    const layout::fragment * frag = nullptr;
    float basis = 0;        ///< The containing block's content width.
    float basis_height = 0; ///< Its content height, for a relative inset.
    /// Whether basis_height is a percentage's basis: only when the parent's
    /// own height is definite (CSS 2.1 §10.5). Layout treats `height: 50%`
    /// under an auto-height parent as `auto`, and an animated one must agree.
    bool definite_height = true;
    float font_size = 16;
    float root_font_size = 16; ///< The <html> element's, for a `rem` folded here.
    /// Whether this element is a flex item. It is a fact about the parent, and
    /// only `min-width`/`min-height` depend on it, so it is gathered with the
    /// rest of the parent lookup rather than costing a second tree walk.
    bool flex_item = false;
    /// Whether this element is a grid item. Unlike flex_item it cannot be read
    /// from the box tree: there is no grid layout algorithm, so `display: grid`
    /// lays out as a block. The parent's declared display is where the fact
    /// survives (getComputedStyle-resolved-min-size-auto asks about grid items).
    bool grid_item = false;
    /// Whether this element generates a principal box. Everything CSSOM
    /// reports as a used value has no answer without one (`display: none`,
    /// `display: contents`, and anything inside such a subtree), and the
    /// computed value is reported instead. Read from both the box tree and the
    /// cascade, because they disagree about `display: contents`: it generates
    /// no box by definition, but this engine's layout makes one anyway, and a
    /// used width must not be reported for a box the page cannot see.
    bool has_box = false;
    /// A non-replaced inline has no used width or height either (CSS 2.1
    /// §10.3.1); getComputedStyle-resolved-min-max-clamping asserts that for a
    /// <span>.
    bool inline_non_replaced = false;
    /// Positioning, for the inset properties: Chrome reports a positioned
    /// element's used insets, so the element's and its containing block's
    /// absolute rectangles are gathered here.
    layout::position_kind position = layout::position_kind::static_;
    rect box_abs{};
    rect containing{};          ///< The padding box of the nearest positioned ancestor.
    std::vector<node_id> chain; ///< Self first, then ancestors.
    bool is_root = false;       ///< Whether this is the document element itself.
    /// Set for a pseudo-element: `chain.front()` is then the originating
    /// element, whose box and fragment stand in for the parent's, and
    /// `declared` reads this style.
    style::computed_style_ptr pseudo;
};

/// The used size of an animated width or height, which layout never saw, in
/// the box `box-sizing` names (the author's number names that box, so the
/// specified size is the used one).
///
/// A length resolves against the containing block, as a pseudo-element's
/// does. A keyword, or a calc-size() over one, runs its calculation over the
/// size the keyword names for this box (CSS Values 5 §calc-size): on the block
/// axis the content height the fragment kept, or the containing block's for
/// `stretch`; on the inline axis layout's own answer for the keyword. A
/// percentage of an indefinite height is `auto` (CSS 2.1 §10.5), and so is a
/// whole calc-size() over one, while a percentage inside a calculation is zero
/// there. Returns nullopt when the box cannot say, so the fragment's size
/// stands.
[[nodiscard]] inline std::optional<float> animated_size(const computed_probe & at, bool horizontal,
                                                        std::string_view text) {
    layout::length len = layout::parse_length(text);
    const bool indefinite = !horizontal && !at.definite_height;
    if (indefinite && len.u == layout::unit::percent) { len = layout::length{}; }
    const float basis = horizontal ? at.basis : at.basis_height;
    if (!len.is_auto() && !len.is_intrinsic()) { return len.resolve(basis, at.font_size); }
    if (at.box == nullptr || at.frag == nullptr) { return std::nullopt; }
    const layout::constraints c{at.basis, at.basis_height, at.font_size};
    const layout::resolved_edges e = layout::resolve_edges(*at.box, c);
    const bool border_box = at.box->border_box;
    if (horizontal) {
        // ponytail: text is measured in the built-in 8x8 face, not the page's
        // fonts, because page_view carries no measure.
        // Hand it over when an animated intrinsic width over text must be exact.
        const float border =
            layout::intrinsic_border_width(*at.box, c, e, shell::font8x8_metrics(), len);
        return border_box ? border : border - e.horizontal_inner();
    }
    const float inner = e.vertical_inner();
    std::string_view keyword = trim(text, html_whitespace);
    while (ascii_istarts_with(keyword, "calc-size(")) {
        keyword = trim(keyword.substr(10), html_whitespace);
    }
    float content = 0;
    if (ascii_istarts_with(keyword, "stretch") && !indefinite) {
        content = at.basis_height - e.margin_top - e.margin_bottom - inner;
    } else if (at.frag->auto_height >= 0) {
        content = at.frag->auto_height - inner;
    } else {
        return std::nullopt;
    }
    const float size = std::max(0.0f, content) + (border_box ? inner : 0.0f);
    return len.apply(size, indefinite ? 0.0f : basis, at.font_size);
}

/// Returns getComputedStyle's resolved value for `property` when it depends on
/// layout, or nullopt to let the caller apply the non-geometric rules.
/// `declared(p)` reads this element's cascaded text for p, `declared_on(id, p)`
/// another element's, and `computed_length(text)` serialises a computed length.
/// `text` is set to the declared value as a side effect. The callbacks are
/// template parameters so they can use the caller's gathered cascade state
/// without copying or type erasure.
template <class Declared, class DeclaredOn, class ComputedLength>
std::optional<std::string> resolved_geometry(std::string_view property, std::string_view & text,
                                             const computed_probe & at, const Declared & declared,
                                             const DeclaredOn & declared_on,
                                             const ComputedLength & computed_length,
                                             const layout::box_node * boxes_,
                                             const layout::fragment * fragments_,
                                             int viewport_width_) {
    // 1. width/height: the used size from the fragment, as the content box or
    //    the border box per `box-sizing` (Chrome reports 960 for a 960-wide
    //    border-box `.container` with 12px padding a side, not 936). With no
    //    box, or for a non-replaced inline, the computed value is reported
    //    instead, unclamped by min/max, since clamping applies only to a used
    //    value (getComputedStyle-resolved-min-max-clamping).
    if (property == "width" || property == "height") {
        // A calc-size() with no used size to report (no box) serialises as
        // written: its computed value is the function (CSS Values 5 §10.2),
        // not a number, which would need layout.
        const auto as_computed = [&]() -> std::string {
            const std::string_view given = trim(declared(property), html_whitespace);
            if (ascii_istarts_with(given, "calc-size(")) { return std::string{given}; }
            return computed_length(given);
        };
        if (at.pseudo && at.has_box) {
            const layout::length len = layout::parse_length(declared(property));
            if (len.is_auto() || len.is_intrinsic()) { return as_computed(); }
            return used_px_text(
                len.resolve(property == "width" ? at.basis : at.basis_height, at.font_size));
        }
        if (!at.has_box || at.inline_non_replaced || at.frag == nullptr) { return as_computed(); }
        const bool horizontal = property == "width";
        // An animated size is not the fragment's: layout ran on the cascade's
        // value, and the running animation's value is the one to report.
        if (const std::string_view animated = declared(property);
            !at.chain.empty() && animated != declared_on(at.chain.front(), property)) {
            if (const std::optional<float> size = animated_size(at, horizontal, animated)) {
                return used_px_text(std::max(0.0f, *size));
            }
        }
        float used = horizontal ? at.frag->bounds.width : at.frag->bounds.height;
        // The fragment is the border box; under content-box the padding and
        // the border come off it, the inverse of layout's border_box_size.
        const bool border_box = ascii_iequals(declared("box-sizing"), "border-box");
        if (at.box != nullptr && !border_box) {
            const layout::constraints c{at.basis, at.basis_height, at.font_size};
            const layout::resolved_edges e = layout::resolve_edges(*at.box, c);
            used -= horizontal ? e.horizontal_inner() : e.vertical_inner();
        }
        return used_px_text(std::max(0.0f, used));
    }
    // 2. font-size and line-height, already absolute. A declared line-height
    //    is reported in px, as Chrome does (`1.5` reads back as `24px`);
    //    `normal`, declared or initial, is reported as the keyword even though
    //    the box carries a number for it (getComputedStyle-line-height).
    if (property == "font-size") {
        // Not `at.box->font_size` directly: a boxless element's size is the
        // one the cascade declared, gathered into the probe.
        return px_text(at.font_size);
    }
    if (property == "line-height") {
        const std::string_view given = declared(property);
        if (given.empty() || ascii_iequals(given, "normal")) { return "normal"; }
        // Resolved from the cascade's text the way the box builder resolves
        // it, rather than read off the box: the root's box is the viewport
        // and does not carry one, so `:root { line-height: 2rem }` would
        // read back as `normal`'s figure (rem-unit-root-element).
        return px_text(layout::box_builder::resolve_line_height(given, at.font_size));
    }
    text = declared(property);
    // 2b. display, from the box tree when nothing declared it. The initial
    //     value is `inline`, and a <div> is `block` only because a UA sheet
    //     says so; this engine's UA sheet (include/ctbrowser/style/ua.hpp) is
    //     trimmed to properties that have a consumer and does not set
    //     `display` for a div. So the box layout used is the answer here.
    //     A declared value is handled below and wins: `display: flex` is
    //     reported as `flex` even though parse_display may lay it out as a
    //     block box, so any layout gap shows up in the geometry instead.
    if (property == "display" && text.empty()) {
        // A pseudo-element's initial display is `inline`, blockified as a
        // flex or grid item is (CSS Display 3 §2.7).
        if (at.pseudo) { return at.flex_item || at.grid_item ? "block" : "inline"; }
        if (at.box == nullptr) { return "none"; } // display:none generates no box
        switch (at.box->kind) {
        case layout::box_kind::block:
        case layout::box_kind::anonymous:
            // `inline_level` is the outer display type; it is what makes a
            // `<button>` (not a replaced element) report `inline-block`
            // rather than `block`.
            return at.box->inline_level ? "inline-block" : "block";
        case layout::box_kind::inline_: return "inline";
        case layout::box_kind::table: return "table";
        case layout::box_kind::flex: return at.box->inline_level ? "inline-flex" : "flex";
        case layout::box_kind::text:
        case layout::box_kind::replaced: break; // The table's `inline` is right.
        }
        return std::string{};
    }
    //     A declared display is blockified where CSS Display 3 §2.7 says the
    //     box is: the root, a flex or grid item, and an absolutely positioned
    //     or floated box. The inner display survives and the outer becomes
    //     block: `inline-table` is `table`, `ruby` is `block ruby`, and an
    //     internal table or ruby display, `inline` and `run-in` are `block`.
    if (property == "display") {
        const std::string shown = collapse_keyword(text);
        const std::string position = collapse_keyword(declared("position"));
        const std::string floating = collapse_keyword(declared("float"));
        const bool blockify = at.is_root || at.flex_item || at.grid_item ||
                              position == "absolute" || position == "fixed" ||
                              (!floating.empty() && floating != "none");
        if (!blockify || ascii_iequals_any(shown, {"none", "contents"})) { return shown; }
        if (shown.starts_with("inline-") && shown != "inline-block") { return shown.substr(7); }
        if (shown == "ruby") { return "block ruby"; }
        for (const std::string_view outer : {"inline ", "run-in "}) {
            if (!shown.starts_with(outer)) { continue; }
            const std::string inner = shown.substr(outer.size());
            return inner == "ruby" ? "block ruby" : inner;
        }
        if (shown.starts_with("table-") || shown.starts_with("ruby-") ||
            ascii_iequals_any(shown, {"inline", "inline-block", "run-in"})) {
            return "block";
        }
        return shown;
    }
    // 2c. min-width/min-height `auto`, initial or author-written. Chrome
    //     reports `auto` for a flex or grid item and for an element with a
    //     specified aspect-ratio (even a degenerate `0/1` or the `auto 1/1`
    //     form), where the automatic minimum is content-based; otherwise
    //     `0px`, since outside those cases `auto` behaves as zero. With no box
    //     it is always `0px` (CSS Sizing 3;
    //     getComputedStyle-resolved-min-size-auto).
    if (property == "min-width" || property == "min-height") {
        const std::string given = collapse_keyword(text);
        if (given.empty() || given == "auto") {
            if (!at.has_box) { return "0px"; }
            if (at.flex_item || at.grid_item) { return "auto"; }
            const std::string ratio = collapse_keyword(declared("aspect-ratio"));
            return !ratio.empty() && ratio != "auto" ? "auto" : "0px";
        }
    }
    // 2d. The insets, per CSSOM §6.7.2:
    //
    //       no box, or not positioned  -> the computed value (`top: 10%`
    //         reads back `10%`; getComputedStyle-insets-static/-nobox).
    //       over-constrained           -> the computed value. When both
    //         sides and the size in that axis are given, one of the three is
    //         ignored, and CSSOM does not report a used value for the axis.
    //       otherwise                  -> the used value.
    //
    //     A side the author gave a length is its own used value. Deriving it
    //     from the geometry (`containing.width - left - width`) would lose
    //     the author's `20.7px` to float arithmetic
    //     (getComputedStyle-insets-absolute-roundtrip). The geometry answers
    //     only for a side that is `auto`, which layout really did decide.
    if (is_inset_property(property)) {
        const bool horizontal = property == "left" || property == "right";
        const bool start = property == "top" || property == "left";
        const std::string_view start_text = declared(horizontal ? "left" : "top");
        const std::string_view end_text = declared(horizontal ? "right" : "bottom");
        const layout::length mine = layout::parse_length(text);
        const bool start_auto = layout::parse_length(start_text).is_auto();
        const bool end_auto = layout::parse_length(end_text).is_auto();
        const float basis = horizontal ? at.containing.width : at.containing.height;

        if (!at.has_box || at.position == layout::position_kind::static_) {
            return computed_length(text);
        }
        // A sticky inset resolves against the scrollport: the nearest scroll
        // container, or the viewport when there is none (CSS Position 3 §3.4;
        // getComputedStyle-sticky-pos-percent puts `top: 50%` under an
        // `overflow: hidden` ancestor and expects half of that ancestor).
        // `auto` is reported as itself; both sides may be given.
        if (at.position == layout::position_kind::sticky) {
            float scrollport = horizontal
                                   ? static_cast<float>(viewport_width_)
                                   : (fragments_ != nullptr ? fragments_->bounds.height : 0.0f);
            for (std::size_t up = 1; up < at.chain.size(); ++up) {
                const std::string overflow = collapse_keyword(
                    declared_on(at.chain[up], horizontal ? "overflow-x" : "overflow-y"));
                if (overflow.empty() || overflow == "visible" || overflow == "clip") { continue; }
                const layout::box_node * scroller = box_for(boxes_, at.chain[up]);
                const layout::fragment * frag = fragment_for(fragments_, at.chain[up]);
                if (scroller == nullptr || frag == nullptr) { continue; }
                const layout::constraints c{frag->bounds.width, frag->bounds.height,
                                            scroller->font_size};
                const layout::resolved_edges e = layout::resolve_edges(*scroller, c);
                // Its content box, which is what getComputedStyle-insets-sticky
                // measures (clientHeight less the paddings).
                scrollport = horizontal ? frag->bounds.width - e.horizontal_inner()
                                        : frag->bounds.height - e.vertical_inner();
                break;
            }
            scrollport = std::max(0.0f, scrollport);
            // A `calc(10% - 1px)` is folded against the same basis.
            if (style::css::may_have_math(text)) {
                style::css::length_context ctx;
                ctx.font_size = at.font_size;
                ctx.root_font_size = at.root_font_size;
                ctx.viewport_width = static_cast<float>(viewport_width_);
                ctx.viewport_height = fragments_ != nullptr ? fragments_->bounds.height : 0.0f;
                ctx.percent_basis = scrollport;
                const style::css::math_answer used = style::css::evaluate_math(text, ctx);
                if (used.outcome == style::css::math_outcome::resolved &&
                    used.value.type == style::css::numeric_type::length &&
                    !used.value.has_percent) {
                    return px_text(static_cast<float>(used.value.px));
                }
            }
            if (mine.is_auto()) { return computed_length(text); }
            return px_text(mine.resolve(scrollport, at.font_size));
        }
        if (at.position == layout::position_kind::relative) {
            // A relative box's containing block is its parent's content box,
            // not the nearest positioned ancestor (that is the absolute rule).
            const float relative_basis = horizontal ? at.basis : at.basis_height;
            // A side that is given is its own used value against that
            // containing block, even when both sides are given: CSSOM's
            // over-constrained case is about an absolutely positioned box's
            // size, and getComputedStyle-insets-relative absolutizes all four
            // of a relative box's insets.
            if (!mine.is_auto()) { return px_text(mine.resolve(relative_basis, at.font_size)); }
            // `auto` against a given opposite side is its negation: the box
            // moved, and this side records the move from the other end.
            const layout::length other = layout::parse_length(start ? end_text : start_text);
            if (other.is_auto()) { return "0px"; }
            return px_text(-other.resolve(relative_basis, at.font_size));
        }
        // Absolute or fixed. Over-constrained needs the size as well: with
        // both insets and a definite size, CSS 10.3.7 ignores `right` (and
        // 10.6.4 ignores `bottom`), and CSSOM reports computed values for
        // the whole axis rather than a used value with a loser in it.
        const bool size_auto =
            layout::parse_length(declared(horizontal ? "width" : "height")).is_auto();
        if (!start_auto && !end_auto && !size_auto) { return computed_length(text); }
        if (!mine.is_auto()) { return px_text(mine.resolve(basis, at.font_size)); }
        const float left = at.box_abs.x - at.containing.x;
        const float top = at.box_abs.y - at.containing.y;
        if (property == "left") { return used_px_text(left); }
        if (property == "top") { return used_px_text(top); }
        if (property == "right") {
            return used_px_text(at.containing.width - left - at.box_abs.width);
        }
        return used_px_text(at.containing.height - top - at.box_abs.height);
    }
    // 2e. Border widths, whose used value is zero unless the matching style
    //     draws something (CSS Backgrounds 3 §4.3). The initial value is
    //     `medium`, so without this an ordinary <div> would report `medium`
    //     where Chrome reports `0px`.
    if (is_border_width_property(property)) {
        const std::string style_name = style_property_for_width(property);
        const std::string_view drawn = declared(style_name);
        if (drawn.empty() || ascii_iequals(drawn, "none") || ascii_iequals(drawn, "hidden")) {
            return "0px";
        }
        return px_text(border_width_px(text, at.font_size));
    }
    // An outline's width and offset are line widths, snapped as one, and
    // unlike a border's the width does not become zero when the style is
    // `none` (CSS UI 4 §5.2): an outline is drawn outside layout, so there is
    // no used width for the style to take away.
    if (property == "outline-width" || property == "outline-offset") {
        const std::string_view given = trim(text, html_whitespace);
        if (property == "outline-offset" && ascii_iequals(given, "inset")) { return "inset"; }
        style::css::length_context lengths;
        lengths.font_size = at.font_size;
        const float px =
            property == "outline-width"
                ? border_width_px(given.empty() ? std::string_view{"medium"} : given, at.font_size)
                : style::css::length_text_to_px(given.empty() ? std::string_view{"0px"} : given,
                                                lengths)
                      .value_or(0.0f);
        return px_text(snapped_line_width(px));
    }
    // Column rules keep their computed width even for `none` and `hidden`.
    // Resolve line-width keywords before the shorthand fold can omit `medium`.
    if ((property == "column-rule-width" || property == "row-rule-width") &&
        (text.empty() || ascii_iequals(text, "thin") || ascii_iequals(text, "medium") ||
         ascii_iequals(text, "thick"))) {
        return px_text(border_width_px(text, at.font_size));
    }
    // Each item of a rule-width list is a line width, snapped item by item
    // (CSS Gaps 1 §3.3): `14.1px` is `14px`. A list with a repeat() is left
    // as it is.
    if (property == "column-rule-width" || property == "row-rule-width") {
        style::css::length_context lengths;
        lengths.font_size = at.font_size;
        std::string widths;
        for (const std::string_view raw : split_top_level(text, ",")) {
            const std::string_view item = trim(raw, html_whitespace);
            const std::optional<float> px =
                ascii_iequals_any(item, {"thin", "medium", "thick"})
                    ? std::optional<float>{border_width_px(item, at.font_size)}
                    : style::css::length_text_to_px(item, lengths);
            if (!px || ascii_istarts_with(item, "repeat(")) {
                widths.clear();
                break;
            }
            widths +=
                (widths.empty() ? "" : ", ") + px_text(snapped_line_width(std::max(0.0f, *px)));
        }
        if (!widths.empty()) { return widths; }
    }
    if (text.empty()) { return std::string{}; }
    // 3. Lengths, through layout's own parser against layout's own basis.
    //    `auto` stays `auto`, which is what Chrome returns for a margin that
    //    was never resolved to a used value.
    if (is_length_property(property)) {
        // Margins and paddings report their used value only when there is a
        // box (CSSOM), as `width` does: a `display: none` element reads
        // `margin-left: 10%` back as `10%`.
        if (!at.has_box) { return computed_length(text); }
        // An out-of-flow box's percentages are of its containing block, the
        // positioned ancestor's padding box, not of its parent (CSS 2.1
        // §10.1; padding-computed's absolutely positioned target).
        const bool out_of_flow = at.position == layout::position_kind::absolute ||
                                 at.position == layout::position_kind::fixed;
        const float percent_basis = out_of_flow ? at.containing.width : at.basis;
        // A math function the cascade could not fold is folded here, against
        // the containing block: the used value is where `round(10%, 1px)`
        // finally has a basis. A percentage in a min or max survives as one
        // below and never comes this way.
        if (style::css::may_have_math(text) && !is_min_or_max_property(property)) {
            style::css::length_context ctx;
            ctx.font_size = at.font_size;
            ctx.root_font_size = at.root_font_size;
            ctx.viewport_width = static_cast<float>(viewport_width_);
            ctx.viewport_height = fragments_ != nullptr ? fragments_->bounds.height : 0.0f;
            ctx.percent_basis = percent_basis;
            const style::css::math_answer used = style::css::evaluate_math(text, ctx);
            if (used.outcome == style::css::math_outcome::resolved &&
                used.value.type == style::css::numeric_type::length && !used.value.has_percent) {
                // Clamp like the cascade's fold (calc/fold.cpp): infinity to
                // +/-2^25 px (33554432), NaN to 0.
                double px = used.value.px;
                if (std::isnan(px)) { px = 0; }
                if (std::isinf(px)) { px = std::copysign(33554432.0, px); }
                // A padding is never negative, `calc(10% - 40px)` against a
                // narrow block included (CSS Box 4 §4.2).
                if (property.starts_with("padding-")) { px = std::max(px, 0.0); }
                return px_text(static_cast<float>(px));
            }
        }
        const layout::length len = layout::parse_length(text);
        // A sizing keyword on a min/max property is its own computed value
        // (min-width: min-content, max-width: none); the used size it
        // clamps to is layout's. `none` is a max's initial value and not
        // the `auto` parse_length reads it as (max-block-size-computed).
        if (len.is_intrinsic() ||
            (is_min_or_max_property(property) && ascii_iequals(text, "none"))) {
            return std::string{text};
        }
        if (len.is_auto()) {
            // A margin's used value is a number: a centred block's `margin: 0
            // auto`, or an absolutely positioned box's between two offsets,
            // reports the pixels the flow gave it (computed-style-005). Other
            // lengths here (paddings, min/max) still report `auto`.
            if (at.frag != nullptr && property.starts_with("margin-")) {
                // An animated `auto` is not the fragment's: layout placed the
                // box on the cascade's margin. On the block axis, and beside
                // an `auto` width that leaves no remainder, an auto margin is
                // zero (CSS 2.1 §10.3.3, §10.6.3; margin-left-composition).
                // ponytail: a definite width's centring remainder is not
                // recomputed; the fragment's margin stands for it.
                const bool animated =
                    !at.chain.empty() && text != declared_on(at.chain.front(), property);
                if (animated && (at.box == nullptr || at.box->width.is_auto() ||
                                 property == "margin-top" || property == "margin-bottom")) {
                    return "0px";
                }
                const layout::fragment & f = *at.frag;
                return px_text(property == "margin-left"    ? f.margin_left
                               : property == "margin-right" ? f.margin_right
                               : property == "margin-top"   ? f.margin_top
                                                            : f.margin_bottom);
            }
            return "auto";
        }
        // A percentage min/max size is reported as written: it is a constraint,
        // not a size, so it has no used value (Chrome reports `100%` for
        // `max-width: 100%`). Margin and padding percentages do resolve to px,
        // as Chrome reports.
        if (len.u == layout::unit::percent && is_min_or_max_property(property)) {
            return std::string{text};
        }
        return px_text(len.resolve(percent_basis, at.font_size));
    }
    return std::nullopt;
}

} // namespace ctbrowser::page::detail
