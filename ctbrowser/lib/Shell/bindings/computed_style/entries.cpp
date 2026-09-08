// dom_bindings - computed_style_entries: every property of one element as
// (css name, value) pairs, gathered from the style map, the box tree and the
// fragment tree, with the property table supplying what nothing declared.
//
// One of three files carved out of a 1,326-line bindings/computed_style.cpp
// on 2026-09-08. The member functions belong to the one class declared in
// include/ctbrowser/shell/bindings.hpp; the serialisation helpers more than
// one file needs are declared in internal.hpp beside this, with external
// linkage in ctbrowser::shell::detail, and internal.hpp carries the note on
// where a computed value comes from. Nothing about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

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
[[nodiscard]] bool is_min_or_max_property(std::string_view property) {
    return property == "min-width" || property == "max-width" || property == "min-height" ||
           property == "max-height";
}

[[nodiscard]] bool is_length_property(std::string_view property) {
    return std::find(length_properties.begin(), length_properties.end(), property) !=
           length_properties.end();
}

[[nodiscard]] bool is_inset_property(std::string_view property) {
    return property == "top" || property == "right" || property == "bottom" || property == "left";
}

[[nodiscard]] bool is_color_property(std::string_view property) {
    return property == "color" || property == "background-color" || property == "border-color" ||
           property == "border-top-color" || property == "border-right-color" ||
           property == "border-bottom-color" || property == "border-left-color" ||
           property == "outline-color";
}

// A BORDER WIDTH IS NOT REPORTED AS THE KEYWORD IT WAS WRITTEN AS, and it is not
// reported as what the sheet asked for either: CSS Backgrounds 3 §4.3 makes the
// used width zero when the matching style is `none` or `hidden`, which is every
// element on a page with no borders. The initial value in the property table is
// `medium`, so without this an ordinary <div> would answer `medium` for all four
// sides where Chrome answers `0px`.
[[nodiscard]] bool is_border_width_property(std::string_view property) {
    return property == "border-top-width" || property == "border-right-width" ||
           property == "border-bottom-width" || property == "border-left-width" ||
           property == "outline-width";
}

// `border-top-width` -> `border-top-style`, `outline-width` -> `outline-style`.
[[nodiscard]] std::string style_property_for_width(std::string_view width_property) {
    return std::string{width_property.substr(0, width_property.size() - 5)} + "style";
}

[[nodiscard]] const layout::box_node * box_for(const layout::box_node * root, node_id id) {
    if (root == nullptr || !id) { return nullptr; }
    if (root->source == id) { return root; }
    for (const layout::box_node & child : root->children) {
        if (const layout::box_node * hit = box_for(&child, id)) { return hit; }
    }
    return nullptr;
}

[[nodiscard]] const layout::fragment * fragment_for(const layout::fragment * root, node_id id) {
    if (root == nullptr || !id) { return nullptr; }
    if (root->source == id) { return root; }
    for (const layout::fragment & child : root->children) {
        if (const layout::fragment * hit = fragment_for(&child, id)) { return hit; }
    }
    return nullptr;
}

// The same lookup, but in ABSOLUTE coordinates. A fragment's bounds are relative
// to its parent, so "where is this element on the page" is a different question
// from "which fragment is it" - and it is the question the used value of `top`
// and `left` is asked against.
[[nodiscard]] rect absolute_rect_of(const layout::fragment * root, node_id id) {
    rect found{};
    bool got = false;
    const auto walk = [&](auto && self, const layout::fragment & at, float dx, float dy) -> void {
        if (got) { return; }
        if (at.source == id) {
            found = rect{dx + at.bounds.x, dy + at.bounds.y, at.bounds.width, at.bounds.height};
            got = true;
            return;
        }
        for (const layout::fragment & c : at.children) {
            self(self, c, dx + at.bounds.x, dy + at.bounds.y);
        }
    };
    if (root != nullptr && id) { walk(walk, *root, 0, 0); }
    return found;
}

// Everything about one element that answering a property needs, gathered ONCE.
// Per-property tree walks would be O(properties x boxes): the object publishes
// every property in the table, and rebuilding this for each of them would mean
// 125 walks over the whole box tree per getComputedStyle call.
//
// The ancestor CHAIN is held as ids rather than as a read transaction, so
// gathering does not pin a document version for as long as a page keeps the
// object alive.
struct probe {
    const layout::box_node * box = nullptr;
    const layout::fragment * frag = nullptr;
    float basis = 0;        // the containing block's content width
    float basis_height = 0; // and its content height, for a relative inset
    float font_size = 16;
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
};

} // namespace

// EVERY PROPERTY OF ONE ELEMENT, AS (css name, value) PAIRS: the longhands the
// table knows, lexicographically, then the shorthands that can be reassembled,
// then whatever the element declared that the table has never heard of. Empty
// for an element that is not in the document.
//
// The order is the object's: the leading run is exactly the set CSSOM's indexed
// properties enumerate, and it is already sorted.
std::vector<std::pair<std::string, std::string>> dom_bindings::computed_style_entries(node_id id) {
    std::vector<std::pair<std::string, std::string>> answers;

    probe at;
    at.box = box_for(boxes_, id);
    at.frag = fragment_for(fragments_, id);
    at.font_size = at.box != nullptr ? at.box->font_size : 16.0f;
    // IS THIS ELEMENT IN THE DOCUMENT AT ALL? A detached one has no computed
    // style: CSSOM leaves getComputedStyle on an element that is not being
    // rendered answering computed rather than resolved values, and every engine
    // returns an EMPTY declaration - which is what css/cssom's
    // getComputedStyle-detached-subtree asserts, `length == 0` and
    // `color === ""`. The cascade never resolved such an element either, so
    // publishing the table's 125 initial values for it would be inventing
    // an answer about a box that does not exist.
    bool connected = false;
    {
        const auto txn = doc_->read();
        for (node_id up = id; up;) {
            at.chain.push_back(up);
            const node_id next = txn.parent(up);
            if (next == up) { break; }
            up = next;
        }
        connected = !at.chain.empty() && at.chain.back() == txn.root();
        // The containing-block width a percentage resolves against: the parent's
        // CONTENT width, or the viewport at the root. That is the parent's
        // fragment less its padding, which is what content_width_of computes
        // (lib/Layout/algorithm.cpp) - there are no borders in this box model yet.
        // The containing block for an absolutely positioned element: the padding
        // box of the nearest POSITIONED ancestor, or the initial containing block
        // when there is none. Same rule the layout pass uses (layout/position.hpp),
        // asked here rather than shared because the two have different inputs -
        // that one walks the fragment tree once, this one answers about one node.
        at.position = at.box != nullptr ? at.box->position : layout::position_kind::static_;
        at.box_abs = absolute_rect_of(fragments_, id);
        at.containing = rect{0, 0, static_cast<float>(viewport_width_),
                             fragments_ != nullptr ? fragments_->bounds.height : 0.0f};
        for (std::size_t up = 1; up < at.chain.size(); ++up) {
            const layout::box_node * ancestor = box_for(boxes_, at.chain[up]);
            if (ancestor == nullptr || !ancestor->is_positioned()) { continue; }
            const rect outer = absolute_rect_of(fragments_, at.chain[up]);
            const layout::constraints c{outer.width, outer.height, ancestor->font_size};
            const layout::resolved_edges e = layout::resolve_edges(*ancestor, c);
            at.containing = rect{outer.x + e.border_left, outer.y + e.border_top,
                                 std::max(0.0f, outer.width - e.border_left - e.border_right),
                                 std::max(0.0f, outer.height - e.border_top - e.border_bottom)};
            break;
        }

        at.basis = static_cast<float>(viewport_width_);
        at.basis_height = fragments_ != nullptr ? fragments_->bounds.height : 0.0f;
        if (at.chain.size() >= 2) {
            const node_id parent = at.chain[1];
            const layout::box_node * up_box = box_for(boxes_, parent);
            at.flex_item = up_box != nullptr && up_box->kind == layout::box_kind::flex;
            if (const layout::fragment * up = fragment_for(fragments_, parent)) {
                at.basis = up->bounds.width;
                at.basis_height = up->bounds.height;
                if (up_box != nullptr) {
                    const layout::side_lengths & pad = up_box->padding;
                    // A PERCENTAGE PADDING RESOLVES AGAINST THE WIDTH on all four
                    // sides (CSS 2.1 §8.4), so the vertical pair is subtracted
                    // using the horizontal basis too. Taking the height as the
                    // basis there is the classic way to get a containing block
                    // that is a few pixels short.
                    at.basis -= pad.left.resolve(at.basis, up_box->font_size) +
                                pad.right.resolve(at.basis, up_box->font_size);
                    at.basis_height -= pad.top.resolve(at.basis, up_box->font_size) +
                                       pad.bottom.resolve(at.basis, up_box->font_size);
                }
            }
        }
    }
    if (!connected) { return answers; }

    // The cascade's text for one property. Empty when nothing declared it, and
    // the caller substitutes the property table's initial value - which is what
    // a computed value IS for a property no rule reached.
    // BY VALUE, all of it. The probe is a handful of pointers and a short vector
    // of node ids, so a copy is nothing; the pointers are the browser's and stay
    // valid for exactly as long as this call, which is why every answer is
    // computed before it returns.
    const style::style_map * styles = styles_;
    atom_table * atoms = atoms_;
    // WHAT THE CASCADE SAID ABOUT ONE NODE. Taken as a node rather than as
    // "this element" because two of the answers below are about the PARENT:
    // whether it is a grid container, which no box kind records.
    const auto declared_on = [styles, atoms](node_id node,
                                             std::string_view property) -> std::string_view {
        if (styles == nullptr || !node) { return {}; }
        const auto found = styles->find(style::engine::key_of(node));
        if (found == styles->end() || !found->second) { return {}; }
        return found->second->get(atoms->intern(property));
    };
    // A GRID CONTAINER IS A DECLARATION, NOT A BOX KIND - `display: grid` makes a
    // block box here, so the box tree cannot be asked the way it is asked for
    // flex just above.
    if (at.chain.size() >= 2) {
        const std::string parent_display = collapse_keyword(declared_on(at.chain[1], "display"));
        at.grid_item = parent_display == "grid" || parent_display == "inline-grid";
    }
    // ONE LOOKUP. This used to walk DOM ancestors for an inherited property, because
    // the cascade produced only the declarations that matched and inheritance happened
    // in four other places downstream. The cascade inherits now, so `get` on the
    // element's own style is the whole answer - and the fifth ad-hoc inheritance
    // mechanism this file used to be is gone.
    const auto declared = [declared_on, at](std::string_view property) -> std::string_view {
        if (at.chain.empty()) { return {}; }
        return declared_on(at.chain.front(), property);
    };

    // THE FONT SIZE OF AN ELEMENT WITH NO BOX. Every `em` below resolves against
    // it, and the box is where it normally lives - but a `display: none` element
    // has no box and still has a computed font-size, which Chrome reports and
    // which css/cssom's getComputedStyle-insets-nobox depends on: it declares
    // `font-size: 10px` beside `top: 1em` and expects `10px`. Only an absolute
    // length can be honoured here; a percentage or an em needs the parent's
    // computed size, and the cascade does not resolve one for a box that was
    // never built.
    if (at.box == nullptr) {
        const layout::length declared_size = layout::parse_length(declared("font-size"));
        if (!declared_size.is_auto() && declared_size.u != layout::unit::percent &&
            declared_size.u != layout::unit::em) {
            at.font_size = declared_size.resolve(0.0f, 16.0f);
        }
    }
    // THE PRINCIPAL BOX, and the two ways there is not one.
    const std::string declared_display = collapse_keyword(declared("display"));
    at.has_box = at.box != nullptr && declared_display != "none" && declared_display != "contents";
    at.inline_non_replaced = at.box != nullptr && at.box->kind == layout::box_kind::inline_;

    // A LENGTH AS A COMPUTED VALUE - CSS Values 3 §5.2 and CSSOM's "otherwise,
    // the computed value". `auto` and every other keyword survive verbatim, a
    // percentage survives as a percentage, and an absolute or font-relative
    // length becomes px. Nothing here needs a containing block, which is the
    // whole point: this is the answer for the cases where there is not one.
    const auto computed_length = [at](std::string_view text) -> std::string {
        const std::string_view given = trim(text, html_whitespace);
        if (given.empty()) { return {}; }
        const layout::length len = layout::parse_length(given);
        // `auto`, `min-content`, a percentage and anything parse_length does not
        // model are all their own computed value.
        if (len.is_auto() || len.u == layout::unit::percent) { return collapse_keyword(given); }
        return px_text(len.resolve(0.0f, at.font_size));
    };

    const auto value_of = [at, declared,
                           computed_length](std::string_view property) -> std::string {
        // 0. A CUSTOM PROPERTY IS NOT A KEYWORD. Its value is an arbitrary token
        //    sequence whose case is significant and whose computed value is the
        //    substituted text, so it is handed back as written rather than folded
        //    the way `display: BLOCK` is.
        if (property.starts_with("--")) {
            return std::string{trim(declared(property), html_whitespace)};
        }
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
            if (!at.has_box || at.inline_non_replaced || at.frag == nullptr) {
                return computed_length(declared(property));
            }
            const bool horizontal = property == "width";
            float used = horizontal ? at.frag->bounds.width : at.frag->bounds.height;
            const bool border_box = ascii_iequals(declared("box-sizing"), "border-box");
            if (at.box != nullptr && !border_box) {
                const layout::side_lengths & pad = at.box->padding;
                used -= horizontal ? pad.left.resolve(at.basis, at.font_size) +
                                         pad.right.resolve(at.basis, at.font_size)
                                   : pad.top.resolve(at.basis, at.font_size) +
                                         pad.bottom.resolve(at.basis, at.font_size);
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
            return at.box != nullptr ? px_text(at.box->line_height) : std::string{};
        }
        const std::string_view text = declared(property);
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
            return {};
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
            if (at.position == layout::position_kind::relative ||
                at.position == layout::position_kind::sticky) {
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
        if (text.empty()) { return {}; }
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
            const layout::length len = layout::parse_length(text);
            if (len.is_auto()) { return "auto"; }
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
        // 3b. EVERY OTHER LENGTH-VALUED PROPERTY, ABSOLUTIZED. A computed value
        //     is an absolute length wherever the specified value was a relative
        //     one, and that is a rule about the property's TYPE rather than
        //     about the eleven names above - which is why it is asked of the
        //     table rather than of a third list. `letter-spacing: 1em` under a
        //     20px font computes to `20px` in every engine and was reported here
        //     as `1em`; so were `word-spacing`, `text-indent`, `vertical-align`,
        //     `flex-basis`, `row-gap`, `column-gap` and `outline-offset`.
        //
        //     A PERCENTAGE STAYS A PERCENTAGE for all of them, which is the
        //     difference from the rule above: none of these is in CSSOM's list
        //     of properties whose resolved value is the used one, so there is no
        //     containing block in the question at all. `text-indent: 10%`
        //     computes to `10%`.
        const style::css::property_syntax * known = style::css::find_property(property);
        if (known != nullptr && (known->kind == style::css::value_kind::length ||
                                 known->kind == style::css::value_kind::length_percentage)) {
            return computed_length(text);
        }
        // 3c. THE FONT FAMILY LIST, whose case is the author's and whose quoting
        //     is CSSOM's. Answered before the keyword fold below, which would
        //     lowercase it - see `font_family_text`.
        if (property == "font-family") { return font_family_text(text); }
        // 4. COLOURS, resolved so the two engines' spellings converge.
        if (is_color_property(property)) {
            if (const std::optional<color> c = paint::parse_color(text)) { return color_text(*c); }
            return std::string{text};
        }
        // 5. Everything else is a keyword or a list, and its computed value IS
        //    its specified text.
        return collapse_keyword(text);
    };

    // `currentcolor` IS NOT A COLOUR. It is the initial value of all six border
    // and outline colours and it resolves to this element's own `color`, which is
    // what Chrome reports for a border nothing coloured. Computed first so the
    // loop below has it, and guarded against a `color` that is itself the keyword
    // - a declaration that cannot mean anything and has no answer but the initial.
    const std::string current_color = [&] {
        const std::string own = value_of("color");
        if (own.empty() || ascii_iequals(own, "currentcolor")) {
            return std::string{"rgb(0, 0, 0)"};
        }
        return own;
    }();

    // EVERY LONGHAND THIS ENGINE KNOWS ABOUT, whether or not anything declared
    // it. That is the whole difference between an object a page can read and the
    // handful of names this used to publish: computed-testcommon.js asks
    // `property in getComputedStyle(target)` before every single computed-value
    // test, and a property the cascade never reached failed there rather than on
    // its value.
    //
    // A SHORTHAND IS SKIPPED, AND THE TABLE IS ASKED WHICH ONE IS. CSSOM §6.7
    // says a computed style's INDEXED properties are the supported LONGHANDS,
    // and css/cssom's getComputedStyle-getter-v-properties asserts both halves
    // of that at once: `'border' in style` is true and `Array.from(style)` does
    // not contain it. serialize-all-longhands asserts the consequence - every
    // enumerated name must serialise to something, and a shorthand here would
    // serialise to nothing, because reconstructing one means deciding how each
    // engine rebuilds it.
    //
    // THIS FILE USED TO KEEP ITS OWN LIST, on the grounds that the table was not
    // this file's to change. The table has a `shorthand` bit now, and the two
    // had already drifted: `border-top`, `border-right`, `border-bottom` and
    // `border-left` are shorthands there and were missing from the list, so all
    // four were enumerated as longhands - which is the exact assertion
    // getComputedStyle-getter-v-properties makes about each of them by name.
    for (const style::css::property_syntax & p : style::css::known_properties()) {
        if (p.shorthand) { continue; }
        std::string text = value_of(p.name);
        if (text.empty()) { text = std::string{p.initial}; }
        if (ascii_iequals(text, "currentcolor")) { text = current_color; }
        answers.emplace_back(std::string{p.name}, std::move(text));
    }
    // SORTED, WHICH THE TABLE IS NOT. css/cssom's getComputedStyle-property-order
    // requires the indexed properties to come out in lexicographic order;
    // style/css/properties.hpp is written box-model-first because that is the
    // order a person reads it in. Sorting here keeps both true, and there are no
    // vendor-prefixed names in the table, so plain byte order is the whole rule.
    std::ranges::sort(answers, [](const auto & a, const auto & b) { return a.first < b.first; });
    const std::size_t longhand_count = answers.size();

    // THE SHORTHANDS, PRESENT BUT NOT INDEXED. CSSOM gives a computed style an
    // attribute for every SUPPORTED property, shorthands included, and
    // getComputedStyle-getter-v-properties asserts exactly that pair: `'border'
    // in style` is true while `Array.from(style)` does not contain it.
    //
    // Assembled from the longhands wherever the serialisation is a list. §6.7's
    // rule drops a trailing value that repeats the one two places before it, so
    // an ordinary <div>'s `margin` is `0px` rather than `0px 0px 0px 0px`.
    //
    // EMPTY FOR THE REST, deliberately. `font`, `background`, `outline`,
    // `list-style`, `transition`, `animation` and `text-decoration` are
    // grammars rather than lists; engines disagree about how to rebuild them -
    // Chrome disagrees with itself across versions - and CSSOM already says a
    // shorthand that cannot be represented serialises to the empty string. That
    // is the same answer, and the same reason, as `cssText` below.
    const auto longhand = [&answers, longhand_count](std::string_view name) -> std::string {
        for (std::size_t i = 0; i < longhand_count; ++i) {
            if (std::string_view{answers[i].first} == name) { return answers[i].second; }
        }
        return {};
    };
    const auto sides = [&longhand](std::string_view t, std::string_view r, std::string_view b,
                                   std::string_view l) -> std::string {
        const std::string top = longhand(t);
        const std::string right = longhand(r);
        const std::string bottom = longhand(b);
        const std::string left = longhand(l);
        if (top.empty() || right.empty() || bottom.empty() || left.empty()) { return {}; }
        if (left != right) { return top + " " + right + " " + bottom + " " + left; }
        if (bottom != top) { return top + " " + right + " " + bottom; }
        if (right != top) { return top + " " + right; }
        return top;
    };
    // Two components of DIFFERENT properties rather than two sides, so the only
    // collapse is the whole-value one: `gap: normal` when both axes are normal.
    const auto both = [&longhand](std::string_view a, std::string_view b) -> std::string {
        const std::string first = longhand(a);
        const std::string second = longhand(b);
        if (first.empty() || second.empty()) { return {}; }
        return first == second ? first : first + " " + second;
    };
    std::vector<std::pair<std::string, std::string>> shorthands;
    for (const style::css::property_syntax & p : style::css::known_properties()) {
        if (!p.shorthand) { continue; }
        std::string text;
        if (p.name == "margin") {
            text = sides("margin-top", "margin-right", "margin-bottom", "margin-left");
        } else if (p.name == "padding") {
            text = sides("padding-top", "padding-right", "padding-bottom", "padding-left");
        } else if (p.name == "inset") {
            text = sides("top", "right", "bottom", "left");
        } else if (p.name == "border-width") {
            text = sides("border-top-width", "border-right-width", "border-bottom-width",
                         "border-left-width");
        } else if (p.name == "border-style") {
            text = sides("border-top-style", "border-right-style", "border-bottom-style",
                         "border-left-style");
        } else if (p.name == "border-color") {
            text = sides("border-top-color", "border-right-color", "border-bottom-color",
                         "border-left-color");
        } else if (p.name == "border-radius") {
            // CORNERS, clockwise from the top left - not the top/right/bottom/left
            // of the edge shorthands, though the collapsing rule is the same one.
            text = sides("border-top-left-radius", "border-top-right-radius",
                         "border-bottom-right-radius", "border-bottom-left-radius");
        } else if (p.name == "gap") {
            text = both("row-gap", "column-gap");
        } else if (p.name == "overflow") {
            text = both("overflow-x", "overflow-y");
        } else if (p.name == "flex-flow") {
            text = both("flex-direction", "flex-wrap");
        } else if (p.name == "flex") {
            // THREE COMPONENTS THAT NEVER COLLAPSE: `flex: 0 1 auto` is the
            // initial value written out, and dropping a repeat would change
            // which component the survivors are.
            const std::string grow = longhand("flex-grow");
            const std::string shrink = longhand("flex-shrink");
            const std::string basis = longhand("flex-basis");
            if (!grow.empty() && !shrink.empty() && !basis.empty()) {
                text = grow + " " + shrink + " " + basis;
            }
        }
        shorthands.emplace_back(std::string{p.name}, std::move(text));
    }
    answers.insert(answers.end(), std::make_move_iterator(shorthands.begin()),
                   std::make_move_iterator(shorthands.end()));

    // AND EVERY PROPERTY THE ELEMENT ITSELF DECLARED that the table has never
    // heard of - a CUSTOM property above all, which no fixed table can enumerate.
    // Readable, and answering to `in`, but NOT indexed: CSSOM's indexed
    // properties are the supported longhands and a page's `--brand` is not one.
    if (styles_ != nullptr) {
        const auto found = styles_->find(style::engine::key_of(id));
        if (found != styles_->end() && found->second) {
            for (const style::declaration & d : found->second->declarations) {
                const std::string_view name = atoms_->text(d.property);
                if (style::css::find_property(name) != nullptr) { continue; }
                const auto seen =
                    std::find_if(answers.begin(), answers.end(), [name](const auto & e) {
                        return std::string_view{e.first} == name;
                    });
                if (seen != answers.end()) { continue; }
                std::string text = value_of(name);
                if (text.empty()) { continue; }
                answers.emplace_back(std::string{name}, std::move(text));
            }
        }
    }
    return answers;
}

} // namespace ctbrowser::shell
