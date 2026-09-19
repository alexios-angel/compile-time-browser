#pragma once
#include "internal.hpp"

namespace ctbrowser::shell::detail {
[[nodiscard]] inline const layout::box_node * box_for(const layout::box_node * root, node_id id) {
    if (root == nullptr || !id) { return nullptr; }
    if (root->source == id) { return root; }
    for (const layout::box_node & child : root->children) {
        if (const layout::box_node * hit = box_for(&child, id)) { return hit; }
    }
    return nullptr;
}

[[nodiscard]] inline const layout::fragment * fragment_for(const layout::fragment * root,
                                                           node_id id) {
    if (root == nullptr || !id) { return nullptr; }
    if (root->source == id) { return root; }
    for (const layout::fragment & child : root->children) {
        if (const layout::fragment * hit = fragment_for(&child, id)) { return hit; }
    }
    return nullptr;
}

// The properties whose value is a LENGTH resolved against the containing block.
// Each is resolved with the same parse_length + length::resolve that layout used,
// so a percentage or an em gives layout's answer rather than a second one. Note
// what that inherits from layout: `rem` has a hardcoded 16px basis and vh/vw/pt
// fall through to pixels (include/ctbrowser/layout/values.hpp). Those are real
// differences from Chrome and they are supposed to show up in the diff.
//
// THE FOUR INSETS ARE NOT HERE any more: their resolved value is a used value
// for a positioned box and a computed value otherwise, which is a rule of its
// own further down rather than a length lookup.
constexpr std::array<std::string_view, 12> length_properties{
    "margin-top",  "margin-right",  "margin-bottom",  "margin-left",
    "padding-top", "padding-right", "padding-bottom", "padding-left",
    "min-width",   "max-width",     "min-height",     "max-height"};

// The four sizing constraints, whose percentages survive into the computed value.
[[nodiscard]] inline bool is_min_or_max_property(std::string_view property) {
    return property == "min-width" || property == "max-width" || property == "min-height" ||
           property == "max-height";
}

[[nodiscard]] inline bool is_length_property(std::string_view property) {
    return std::find(length_properties.begin(), length_properties.end(), property) !=
           length_properties.end();
}

[[nodiscard]] inline bool is_inset_property(std::string_view property) {
    return property == "top" || property == "right" || property == "bottom" || property == "left";
}

// Every property the table types as a `<color>`, and the shorthand of four.
[[nodiscard]] inline bool is_color_property(std::string_view property) {
    if (property == "border-color") { return true; }
    const style::css::property_syntax * known = style::css::find_property(property);
    return known != nullptr && known->kind == style::css::value_kind::color;
}

// A BORDER WIDTH IS NOT REPORTED AS THE KEYWORD IT WAS WRITTEN AS, and it is not
// reported as what the sheet asked for either: CSS Backgrounds 3 §4.3 makes the
// used width zero when the matching style is `none` or `hidden`, which is every
// element on a page with no borders. The initial value in the property table is
// `medium`, so without this an ordinary <div> would answer `medium` for all four
// sides where Chrome answers `0px`.
[[nodiscard]] inline bool is_border_width_property(std::string_view property) {
    return property == "border-top-width" || property == "border-right-width" ||
           property == "border-bottom-width" || property == "border-left-width" ||
           property == "outline-width";
}

// `border-top-width` -> `border-top-style`, `outline-width` -> `outline-style`.
[[nodiscard]] inline std::string style_property_for_width(std::string_view width_property) {
    return std::string{width_property.substr(0, width_property.size() - 5)} + "style";
}

// Per-property tree walks would be O(properties x boxes): the object publishes
// every property in the table, and rebuilding this for each of them would mean
// 125 walks over the whole box tree per getComputedStyle call.
//
// The ancestor CHAIN is held as ids rather than as a read transaction, so
// gathering does not pin a document version for as long as a page keeps the
// object alive.
struct computed_probe {
    const layout::box_node * box = nullptr;
    const layout::fragment * frag = nullptr;
    float basis = 0;        // the containing block's content width
    float basis_height = 0; // and its content height, for a relative inset
    float font_size = 16;
    float root_font_size = 16; // the <html> element's, for a `rem` folded here
    // Is this element a FLEX ITEM? A fact about its parent rather than about it,
    // and the two properties whose reported value depends on it - `min-width`
    // and `min-height` - are answered nowhere else, so it is gathered with the
    // rest of the parent lookup rather than costing a second tree walk.
    bool flex_item = false;
    // ...AND IS IT A GRID ITEM? The same question and the same two properties,
    // but it cannot be asked of the box tree the way `flex_item` is: there is no
    // grid box kind, because there is no grid algorithm - `display: grid` lays
    // out as a block. The parent's DECLARED display is where the fact survives,
    // and getComputedStyle-resolved-min-size-auto asks about a grid item by name.
    bool grid_item = false;
    // DOES THIS ELEMENT GENERATE A PRINCIPAL BOX? Everything CSSOM reports as a
    // USED value has no answer when it does not - `display: none`,
    // `display: contents`, and anything inside such a subtree - and the rule
    // then is the computed value. Read from the box tree AND from the cascade,
    // because the two disagree about `display: contents`: it generates no box by
    // definition and this engine's layout makes one anyway, which is a layout
    // gap rather than a licence to report a used width for a box the page
    // cannot see.
    bool has_box = false;
    // A NON-REPLACED INLINE has no used width or height either (CSS 2.1 §10.3.1),
    // and css/cssom's getComputedStyle-resolved-min-max-clamping asserts that
    // for a <span> beside the two boxless cases.
    bool inline_non_replaced = false;
    // POSITIONING, for the four inset properties. Chrome reports their USED
    // values for a positioned element - a number, not the `auto` that was
    // written - and `auto` only for a static one, so answering the declared text
    // differed on 104 of one fixture's 222 remaining values. Deriving them needs
    // the element's absolute rectangle and its containing block's, which are two
    // tree walks, so they are gathered here with everything else.
    layout::position_kind position = layout::position_kind::static_;
    rect box_abs{};
    rect containing{};          // the padding box of the nearest positioned ancestor
    std::vector<node_id> chain; // self first, then ancestors
    bool is_root = false;       // the document element itself
    // A PSEUDO-ELEMENT: `chain.front()` is the originating element, whose box
    // and fragment stand in for the parent's, and `declared` reads this style.
    style::computed_style_ptr pseudo;
};

// A disengaged result leaves the property's remaining non-geometric rules to the caller.
// Callbacks retain the already-gathered cascade state without copying or type erasure.
template <class Declared, class DeclaredOn, class ComputedLength>
std::optional<std::string> resolved_geometry(std::string_view property, std::string_view & text,
                                             const computed_probe & at, const Declared & declared,
                                             const DeclaredOn & declared_on,
                                             const ComputedLength & computed_length,
                                             const layout::box_node * boxes_,
                                             const layout::fragment * fragments_,
                                             int viewport_width_) {
    // 1. USED SIZES, from the fragment - and WHICH BOX depends on box-sizing.
    //
    //    `width` names the content box by default and the border box under
    //    `box-sizing: border-box`, and getComputedStyle reports the used value
    //    of `width` - so the same element answers with a different rectangle
    //    depending on one other property. Measured rather than assumed: Chrome
    //    answers 960 for a `.container` whose fragment is 960 wide and whose
    //    horizontal padding is 12 a side, and 936 would be the content box.
    //
    //    Bootstrap sets `box-sizing: border-box` on `*`, so this is not an edge
    //    case on any page that uses it - it was 399 of 3,518 differences, all of
    //    them exactly the padding.
    //
    //    ...UNLESS THERE IS NO USED SIZE. An element with no box and a
    //    non-replaced inline both report the computed value instead, and
    //    getComputedStyle-resolved-min-max-clamping asserts it for all three
    //    of them: `width: 10%` reads back `10%`, and a `width` outside its own
    //    min/max is NOT clamped, because clamping is something the used value
    //    goes through and there is no used value here.
    if (property == "width" || property == "height") {
        // A calc-size() with no used size to report - no box - serialises
        // as written: its computed value is the function (CSS Values 5
        // §10.2), not a number this cannot make without laying out.
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
        float used = horizontal ? at.frag->bounds.width : at.frag->bounds.height;
        // The fragment is the border box; under content-box the padding AND
        // the border come off it, which is the same arithmetic layout's
        // border_box_size did in the other direction.
        const bool border_box = ascii_iequals(declared("box-sizing"), "border-box");
        if (at.box != nullptr && !border_box) {
            const layout::constraints c{at.basis, at.basis_height, at.font_size};
            const layout::resolved_edges e = layout::resolve_edges(*at.box, c);
            used -= horizontal ? e.horizontal_inner() : e.vertical_inner();
        }
        return used_px_text(std::max(0.0f, used));
    }
    // 2. FONT SIZE and LINE HEIGHT, already absolute on the box - the two
    //    lengths this engine resolves eagerly, and font-size is the basis every
    //    `em` here resolves against.
    //
    //    LINE HEIGHT MUST COME FROM THE BOX when a sheet gave it one. Chrome
    //    reports a declared line-height as a length, so a unitless
    //    `line-height: 1.5` answered as `1.5` differs from `24px` on every
    //    text-bearing element on the page - which it did, on 34 of 40 elements
    //    of one fixture, for a reason that had nothing to do with layout being
    //    wrong.
    //
    //    `normal` IS NOT A LENGTH, though, and that is the other half of the
    //    same rule: Chrome answers the keyword verbatim for it, and answers it
    //    for an element no rule reached, because `normal` is the initial value.
    //    The box carries a resolved number there whatever the cascade said -
    //    it has to, to lay text out - so asking the box alone reported a px
    //    figure for every element on a page that declares no line-height at
    //    all. css/cssom's getComputedStyle-line-height is four assertions
    //    about exactly this.
    if (property == "font-size") {
        // NOT `at.box->font_size` directly: a boxless element's size is the
        // one the cascade declared, gathered above.
        return px_text(at.font_size);
    }
    if (property == "line-height") {
        const std::string_view given = declared(property);
        if (given.empty() || ascii_iequals(given, "normal")) { return "normal"; }
        // Resolved from the cascade's text the way the box builder resolves
        // it, rather than read off the box: the ROOT's box is the viewport
        // and does not carry one, so `:root { line-height: 2rem }` read
        // back as `normal`'s figure (rem-unit-root-element).
        return px_text(layout::box_builder::resolve_line_height(given, at.font_size));
    }
    text = declared(property);
    // 2b. DISPLAY, from the box tree when nothing declared it. The CSS initial
    //     value is `inline`, but almost nothing renders as its initial value -
    //     a <div> is `block` because a UA SHEET says so, and this engine's UA
    //     sheet (include/ctbrowser/style/ua.hpp) is deliberately trimmed to
    //     the properties that have a consumer, so it does not say `display`
    //     for a div at all. Falling through to the property table's `inline`
    //     would then report a difference on every block element on the page -
    //     hundreds of lines about one absent declaration.
    //
    //     What layout USED is the honest answer, and the box knows it. Note
    //     the deliberate ordering: a DECLARED `display: flex` is reported as
    //     `flex` even though parse_display collapses it to a block box, so
    //     `display` agrees with Chrome and the missing flex algorithm shows up
    //     where it actually bites - in the geometry.
    if (property == "display" && text.empty()) {
        // A pseudo-element's initial display is `inline`, blockified as a
        // flex or grid item is (CSS Display 3 §2.7).
        if (at.pseudo) { return at.flex_item || at.grid_item ? "block" : "inline"; }
        if (at.box == nullptr) { return "none"; } // display:none generates no box
        switch (at.box->kind) {
        case layout::box_kind::block:
        case layout::box_kind::anonymous:
            // `inline_level` is the OUTSIDE of the display value, and it is
            // what makes a `<button>` report `inline-block` rather than
            // `block` now that it is not a replaced element.
            return at.box->inline_level ? "inline-block" : "block";
        case layout::box_kind::inline_: return "inline";
        case layout::box_kind::table: return "table";
        case layout::box_kind::flex: return at.box->inline_level ? "inline-flex" : "flex";
        case layout::box_kind::text:
        case layout::box_kind::replaced: break; // the table's `inline` is right
        }
        return std::string{};
    }
    // 2c. THE AUTOMATIC MINIMUM SIZE, reported the way Chrome reports it. The
    //     initial value of `min-width`/`min-height` is `auto`, and Chrome
    //     answers that verbatim for a FLEX ITEM - where `auto` means the
    //     content-based minimum and really is not a length - while resolving
    //     it to `0px` everywhere else, because outside a flex or grid
    //     container `auto` behaves as zero.
    //
    //     Answering one of the two for both is not a small error: it was 139
    //     of the grid fixture's 490 differences, every one of them about
    //     which parent the element has rather than about the element. Nor can
    //     the property table's initial value fix it - it says `auto`, which is
    //     what the specification says and what moved 566 differences the WRONG
    //     way when css-parity.py tried it, because most elements on most pages
    //     are not flex items.
    //
    //     `auto` "resolves to zero when no box is generated" whatever the
    //     parent is (CSS Sizing 3), which is the second half of
    //     getComputedStyle-resolved-min-size-auto.
    //
    //     AN `auto` THE AUTHOR WROTE IS THE SAME `auto`. This asked
    //     `text.empty()`, so the rule applied to the INITIAL value and to
    //     nothing else: `min-width: auto` in a style attribute fell through
    //     to the length branch below and came back as the keyword. That is
    //     half of getComputedStyle-resolved-min-size-auto by construction -
    //     the file asserts each element twice, once as the initial value and
    //     once with the same `auto` set through `style.setProperty`, and
    //     seven of its fourteen failures were the second assertion of a pair
    //     whose first one passed.
    //
    //     AND THREE THINGS PRESERVE IT, not one. A flex item, a GRID item -
    //     which is a declaration on the parent rather than a box kind, since
    //     `display: grid` lays out as a block here - and any element with a
    //     specified `aspect-ratio`, where the automatic minimum is the
    //     transferred size and genuinely is not zero. A degenerate ratio
    //     (`0/1`) and the two-part form (`auto 1/1`) preserve it too: the
    //     rule is "an aspect-ratio was specified", not "it is usable".
    if (property == "min-width" || property == "min-height") {
        const std::string given = collapse_keyword(text);
        if (given.empty() || given == "auto") {
            if (!at.has_box) { return "0px"; }
            if (at.flex_item || at.grid_item) { return "auto"; }
            const std::string ratio = collapse_keyword(declared("aspect-ratio"));
            return !ratio.empty() && ratio != "auto" ? "auto" : "0px";
        }
    }
    // 2d. THE INSETS. CSSOM §6.7.2 gives them the longest special case in the
    //     specification, and all three of its arms are here:
    //
    //       no box, or not positioned  -> the COMPUTED value. An inset does
    //         not apply to a static box, so `top: 10%` reads back `10%` and
    //         `top: auto` reads back `auto`. This answered `auto` for all
    //         four sides of every static element, which is every element on
    //         most pages, and is what getComputedStyle-insets-static and
    //         -insets-nobox assert 216 times each.
    //       over-constrained           -> the COMPUTED value, again. When
    //         both sides and the size in that axis are given, one of the
    //         three is ignored, and CSSOM refuses to report a used value it
    //         would have to pick a loser for.
    //       otherwise                  -> the USED value.
    //
    //     A SIDE THE AUTHOR GAVE A LENGTH IS ITS OWN USED VALUE. Deriving it
    //     from the geometry instead - `containing.width - left - width` -
    //     is arithmetic on floats that the author's `20.7px` does not
    //     survive, which is getComputedStyle-insets-absolute-roundtrip. The
    //     geometry is still what answers for a side that says `auto`,
    //     because that one really was decided by layout.
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
        // A STICKY INSET RESOLVES AGAINST THE SCROLLPORT - the nearest
        // scroll container, or the viewport when there is none (CSS
        // Position 3 §3.4; getComputedStyle-sticky-pos-percent puts
        // `top: 50%` under an `overflow: hidden` ancestor and expects half
        // of THAT). `auto` is its own value; both sides may be given.
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
                // Its CONTENT box, which is what the getComputedStyle-insets-
                // sticky rows measure (clientHeight less the paddings).
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
            // A relative box's containing block is the one it would have had
            // staying put - its parent's content box - and NOT the nearest
            // positioned ancestor, which is the absolute rule.
            const float relative_basis = horizontal ? at.basis : at.basis_height;
            // Both sides given: CSS 9.4.3 ignores one of them, so this is
            // over-constrained and the computed value is the answer.
            if (!start_auto && !end_auto) { return computed_length(text); }
            if (!mine.is_auto()) { return px_text(mine.resolve(relative_basis, at.font_size)); }
            // `auto` against a given opposite side is its negation: the box
            // moved, and this side records the move from the other end.
            const layout::length other = layout::parse_length(start ? end_text : start_text);
            if (other.is_auto()) { return "0px"; }
            return px_text(-other.resolve(relative_basis, at.font_size));
        }
        // Absolute or fixed. Over-constrained needs the SIZE as well: with
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
    // 2e. THE BORDER AND OUTLINE WIDTHS, whose used value is zero unless the
    //     matching style draws something. Answered here rather than left to
    //     the initial value because the initial value is `medium` and no box
    //     with no border is three pixels wide.
    if (is_border_width_property(property)) {
        const std::string style_name = style_property_for_width(property);
        const std::string_view drawn = declared(style_name);
        if (drawn.empty() || ascii_iequals(drawn, "none") || ascii_iequals(drawn, "hidden")) {
            return "0px";
        }
        return px_text(border_width_px(text, at.font_size));
    }
    if (text.empty()) { return std::string{}; }
    // 3. LENGTHS, through layout's own parser against layout's own basis.
    //    `auto` stays `auto`, which is what Chrome returns for a margin that
    //    was never resolved to a used value.
    if (is_length_property(property)) {
        // ...WHEN THERE IS A BOX. A margin and a padding are on CSSOM's list
        // of properties whose resolved value is the USED one, and that list
        // is conditioned on the element generating a box: a `display: none`
        // element has no used margin, so `margin-left: 10%` reads back as
        // `10%` rather than as a share of a containing block it does not
        // have. Same sentence, same rule, as `width` above.
        if (!at.has_box) { return computed_length(text); }
        // A MATH FUNCTION THE CASCADE COULD NOT FOLD is folded here, against
        // the containing block: this is the used value, and the used value
        // is where `round(10%, 1px)` finally has a basis. A percentage in a
        // min or max survives as one below and never comes this way.
        if (style::css::may_have_math(text) && !is_min_or_max_property(property)) {
            style::css::length_context ctx;
            ctx.font_size = at.font_size;
            ctx.root_font_size = at.root_font_size;
            ctx.viewport_width = static_cast<float>(viewport_width_);
            ctx.viewport_height = fragments_ != nullptr ? fragments_->bounds.height : 0.0f;
            ctx.percent_basis = at.basis;
            const style::css::math_answer used = style::css::evaluate_math(text, ctx);
            if (used.outcome == style::css::math_outcome::resolved &&
                used.value.type == style::css::numeric_type::length && !used.value.has_percent) {
                // The same bound the cascade's fold applies (calc/fold.cpp):
                // an infinity lands on it and a NaN on zero.
                double px = used.value.px;
                if (std::isnan(px)) { px = 0; }
                if (std::isinf(px)) { px = std::copysign(33554432.0, px); }
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
            // A MARGIN'S USED VALUE IS A NUMBER, and `auto` is a value only
            // for a box no flow has placed: a centred block's `margin: 0
            // auto` and an absolutely positioned box's between two offsets
            // are the pixels the flow gave them (computed-style-005). Every
            // other length answers `auto` still - a width, an inset, which
            // have their own rows above.
            if (at.frag != nullptr && property.starts_with("margin-")) {
                const layout::fragment & f = *at.frag;
                return px_text(property == "margin-left"    ? f.margin_left
                               : property == "margin-right" ? f.margin_right
                               : property == "margin-top"   ? f.margin_top
                                                            : f.margin_bottom);
            }
            return "auto";
        }
        // A PERCENTAGE MIN OR MAX STAYS A PERCENTAGE. Their computed value is the
        // percentage as specified - resolving it needs a containing block, which
        // is a used-value question, and unlike width there is no used value to
        // report because a max is a constraint rather than a size. Chrome answers
        // `100%` for `.row > * { max-width: 100% }` and this answered 960px, on
        // 62 of the grid fixture's 111 elements.
        //
        // Margin and padding are NOT in this exception, deliberately: Chrome
        // resolves their percentages to pixels, because there a percentage does
        // have a used value and reporting it is what makes an auto margin
        // comparable at all.
        if (len.u == layout::unit::percent && is_min_or_max_property(property)) {
            return std::string{text};
        }
        return px_text(len.resolve(at.basis, at.font_size));
    }
    return std::nullopt;
}

} // namespace ctbrowser::shell::detail
