// dom_bindings - `getComputedStyle`. What the cascade and layout between them
// decided about one element, which is the channel tools/check/css-parity.py
// compares against Chrome property by property.
//
// WHY THIS NEEDS THREE SOURCES. `style::computed_style` is not a computed style
// despite the name: engine::resolve produces only the declarations that MATCHED,
// still as text, with no inheritance and no initial values
// (include/ctbrowser/style/computed.hpp, and unittests/unit/style_basics.cpp asserts
// that an unmatched element "resolves to nothing"). So:
//
//   the style map   every keyword-valued property - display, position, ...
//   the box tree    lengths, resolved the way LAYOUT resolved them
//   the fragment    used sizes, i.e. what the element actually got
//
// Reading the box tree rather than re-parsing the text is the point: asking "how
// wide is 50%" twice, in two places, is how the two answers start to differ.
// What layout used IS the computed value here.
//
// ...AND A FOURTH, WHICH IS NEW: style/css/properties.hpp's table says WHICH
// properties exist and what each one's initial value is. The three sources above
// can only answer about a property something DECLARED, so every other property
// was absent from the object entirely - which is what
// `getComputedStyle(el).someProperty === undefined` meant in 1,034 css/cssom
// subtests (docs/css-conformance.md §4). A property nothing declared has a
// computed value all the same, and it is the initial one.
//
// THE OBJECT PUBLISHES BOTH SPELLINGS. `css/css-values` asks with the hyphenated
// CSS name and `css/cssom` with the IDL one, and computed-testcommon.js asserts
// `property in getComputedStyle(target)` BEFORE it reads - so a name absent under
// either spelling fails the test before a value is ever compared. The conversion
// itself is style/css/properties.hpp's, because `el.style` needs the same one.
//
// NOTHING HERE OUTLIVES THE CALL EXCEPT STRINGS. The probe holds raw pointers
// into the box and fragment trees, and those trees are rebuilt by the next
// layout - which now happens INSIDE a script turn, because getComputedStyle
// flushes a pending restyle before it reads (browser::run_scripts installs the
// flush; docs/css-conformance.md §5 measured that without it the camelCase names
// alone move nothing, since a page reads the state from before its own write).
// So every value is computed eagerly here and the methods close over the strings.
//
// INHERITANCE IS NOT DONE HERE ANY MORE. This file used to walk DOM ancestors for an
// inherited property, because the cascade produced only the declarations that MATCHED
// and inheritance happened in four other places downstream. That made this the fifth
// ad-hoc mechanism for one idea. The cascade inherits now, so an inherited property is
// a plain `get` on the element's own style, and the walk is gone.

#include <ctbrowser/shell/bindings.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/properties.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser::shell {
namespace {

// The properties whose value is a LENGTH resolved against the containing block.
// Each is resolved with the same parse_length + length::resolve that layout used,
// so a percentage or an em gives layout's answer rather than a second one. Note
// what that inherits from layout: `rem` has a hardcoded 16px basis and vh/vw/pt
// fall through to pixels (include/ctbrowser/layout/values.hpp). Those are real
// differences from Chrome and they are supposed to show up in the diff.
constexpr std::array<std::string_view, 16> length_properties{
    "margin-top",  "margin-right",  "margin-bottom",  "margin-left",
    "padding-top", "padding-right", "padding-bottom", "padding-left",
    "top",         "right",         "bottom",         "left",
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

[[nodiscard]] bool is_color_property(std::string_view property) {
    return property == "color" || property == "background-color" || property == "border-color" ||
           property == "border-top-color" || property == "border-right-color" ||
           property == "border-bottom-color" || property == "border-left-color" ||
           property == "outline-color";
}

// THE SHORTHANDS IN style/css/properties.hpp's TABLE. CSSOM §6.7 says a computed
// style's INDEXED properties are the supported LONGHANDS, and css/cssom's
// getComputedStyle-getter-v-properties asserts both halves of that at once:
// `'border' in style` is true and `Array.from(style)` does not contain it.
// serialize-all-longhands asserts the consequence - every enumerated name must
// serialise to something, and a shorthand here would serialise to nothing,
// because reconstructing one means deciding how each engine rebuilds it.
//
// A LIST HERE RATHER THAN A FLAG ON `property_syntax`, because that table is not
// this file's to change. It belongs there and should move the moment the struct
// grows a `shorthand` bit; until then a shorthand added to the table and not to
// this list is enumerated, which is the failure mode to watch for.
[[nodiscard]] bool is_shorthand(std::string_view property) {
    static constexpr std::array<std::string_view, 19> names{
        "animation",    "background",    "border",
        "border-color", "border-radius", "border-style",
        "border-width", "flex",          "flex-flow",
        "font",         "gap",           "inset",
        "list-style",   "margin",        "outline",
        "overflow",     "padding",       "text-decoration",
        "transition"};
    return std::find(names.begin(), names.end(), property) != names.end();
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

// `thin` / `medium` / `thick` are 1, 3 and 5 CSS pixels - the figures every
// engine uses, and the reason the keyword has to become a number at all is that
// getComputedStyle reports a border width as a length and never as its keyword.
[[nodiscard]] float border_width_px(std::string_view text, float font_size) {
    if (text.empty() || ascii_iequals(text, "medium")) { return 3.0f; }
    if (ascii_iequals(text, "thin")) { return 1.0f; }
    if (ascii_iequals(text, "thick")) { return 5.0f; }
    const layout::length len = layout::parse_length(text);
    if (len.is_auto()) { return 0.0f; }
    // A percentage is not a valid border width; resolving one against a zero
    // basis is the honest answer to a declaration that got through anyway.
    return len.resolve(0.0f, font_size);
}

// A CSS number, serialised so a person can read the report: no trailing zeros and
// an integer prints as an integer. Rounded to 1/64 - Chrome's LayoutUnit quantum,
// and the same rounding the harness compares at - so the text and the comparison
// agree about what this number is. Without that, two values that differ only
// below the quantum print as different strings and read as a difference.
[[nodiscard]] std::string number_text(float value) {
    if (!std::isfinite(value)) { return "0"; }
    const float snapped = std::round(value * 64.0f) / 64.0f;
    std::string out = std::to_string(static_cast<double>(snapped));
    if (out.find('.') != std::string::npos) {
        while (!out.empty() && out.back() == '0') { out.pop_back(); }
        if (!out.empty() && out.back() == '.') { out.pop_back(); }
    }
    if (out.empty() || out == "-0") { return "0"; }
    return out;
}

[[nodiscard]] std::string px_text(float value) {
    return number_text(value) + "px";
}

// A colour in the one form both engines normalise to. Chrome prints
// `rgb(13, 110, 253)` and has drifted across versions, so the harness normalises
// both sides rather than either engine imitating the other. Resolving here still
// earns its keep: `#0d6efd` and `rgb(13,110,253)` then compare equal without the
// tool knowing Bootstrap's palette, and a colour that does NOT parse comes back
// as its raw text rather than silently as black.
[[nodiscard]] std::string color_text(color c) {
    const auto channel = [](std::uint8_t v) { return std::to_string(static_cast<int>(v)); };
    const std::string rgb = channel(c.red()) + ", " + channel(c.green()) + ", " + channel(c.blue());
    if (c.opaque()) { return "rgb(" + rgb + ")"; }
    return "rgba(" + rgb + ", " + number_text(static_cast<float>(c.alpha()) / 255.0f) + ")";
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
// The ancestor CHAIN is held as ids rather than as a read transaction, so the
// snapshot does not pin a document version for as long as a page keeps the
// object alive.
struct probe {
    const layout::box_node * box = nullptr;
    const layout::fragment * frag = nullptr;
    float basis = 0; // the containing block's content width
    float font_size = 16;
    // Is this element a FLEX ITEM? A fact about its parent rather than about it,
    // and the two properties whose reported value depends on it - `min-width`
    // and `min-height` - are answered nowhere else, so it is gathered with the
    // rest of the parent lookup rather than costing a second tree walk.
    bool flex_item = false;
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

[[nodiscard]] std::string collapse_keyword(std::string_view text) {
    std::string out;
    bool gap = false;
    for (const char c : trim(text, html_whitespace)) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
            gap = true;
            continue;
        }
        if (gap && !out.empty()) { out += ' '; }
        gap = false;
        out += ascii_lower(c);
    }
    return out;
}

} // namespace

value dom_bindings::computed_style_object(context & cx, node_id id) {
    auto * held = static_cast<script::object_object *>(cx.make_object().as_heap());

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
        if (at.chain.size() >= 2) {
            const node_id parent = at.chain[1];
            const layout::box_node * up_box = box_for(boxes_, parent);
            at.flex_item = up_box != nullptr && up_box->kind == layout::box_kind::flex;
            if (const layout::fragment * up = fragment_for(fragments_, parent)) {
                at.basis = up->bounds.width;
                if (up_box != nullptr) {
                    at.basis -= up_box->padding.left.resolve(at.basis, up_box->font_size) +
                                up_box->padding.right.resolve(at.basis, up_box->font_size);
                }
            }
        }
    }

    // The cascade's text for one property, walking the chain when it inherits.
    // Empty when nothing declared it, and the caller substitutes the property
    // table's initial value - which is what a computed value IS for a property
    // no rule reached.
    // BY VALUE, all of it. The probe is a handful of pointers and a short vector
    // of node ids, so a copy is nothing; the pointers are the browser's and stay
    // valid for exactly as long as this call, which is why every answer is
    // computed before it returns.
    const style::style_map * styles = styles_;
    atom_table * atoms = atoms_;
    // ONE LOOKUP. This used to walk DOM ancestors for an inherited property, because
    // the cascade produced only the declarations that matched and inheritance happened
    // in four other places downstream. The cascade inherits now, so `get` on the
    // element's own style is the whole answer - and the fifth ad-hoc inheritance
    // mechanism this file used to be is gone.
    const auto declared = [styles, atoms, at](std::string_view property) -> std::string_view {
        if (styles == nullptr || at.chain.empty()) { return {}; }
        const auto found = styles->find(style::engine::key_of(at.chain.front()));
        if (found == styles->end() || !found->second) { return {}; }
        return found->second->get(atoms->intern(property));
    };

    const auto value_of = [at, declared](std::string_view property) -> std::string {
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
        if (property == "width" || property == "height") {
            if (at.frag == nullptr) { return {}; }
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
            return px_text(std::max(0.0f, used));
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
            return at.box != nullptr ? px_text(at.box->font_size) : std::string{};
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
        if (text.empty() && (property == "min-width" || property == "min-height")) {
            return at.flex_item ? "auto" : "0px";
        }
        // 2d. THE INSETS, as USED values. Chrome answers `auto` only for a static
        //     element; for a positioned one it answers the number the box ended
        //     up at, whether or not the sheet wrote it. So `position: relative`
        //     with nothing else reports `0px` on all four sides, and an absolute
        //     box with only `top` and `left` still reports a `right` and a
        //     `bottom` - derived from where it is.
        //
        //     Derived from the GEOMETRY rather than re-resolved from the text,
        //     for the same reason `width` is: layout already answered this
        //     question, and asking it twice is how two answers start to differ.
        if (property == "top" || property == "right" || property == "bottom" ||
            property == "left") {
            if (at.position == layout::position_kind::static_) { return "auto"; }
            if (at.position == layout::position_kind::relative ||
                at.position == layout::position_kind::sticky) {
                // A relative box's used inset is what it was OFFSET by, and an
                // omitted one is zero - it did not move.
                if (text.empty()) { return "0px"; }
                const layout::length len = layout::parse_length(text);
                if (len.is_auto()) { return "0px"; }
                const bool horizontal = property == "left" || property == "right";
                return px_text(len.resolve(horizontal ? at.containing.width : at.containing.height,
                                           at.font_size));
            }
            const float left = at.box_abs.x - at.containing.x;
            const float top = at.box_abs.y - at.containing.y;
            if (property == "left") { return px_text(left); }
            if (property == "top") { return px_text(top); }
            if (property == "right") {
                return px_text(at.containing.width - left - at.box_abs.width);
            }
            return px_text(at.containing.height - top - at.box_abs.height);
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
        // 4. COLOURS, resolved so the two engines' spellings converge.
        if (is_color_property(property)) {
            if (const std::optional<color> c = paint::parse_color(text)) { return color_text(*c); }
            return std::string{text};
        }
        // 5. Everything else is a keyword or a list, and its computed value IS
        //    its specified text.
        return collapse_keyword(text);
    };

    // ONE PUBLISHED PROPERTY. `idl` is empty when the two spellings coincide,
    // which is true of every one-word property - there is no point storing
    // `width` twice.
    struct entry {
        std::string css;
        std::string idl;
        std::string text;
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
    std::vector<entry> published;
    published.reserve(style::css::known_properties().size());
    for (const style::css::property_syntax & p : style::css::known_properties()) {
        if (is_shorthand(p.name)) { continue; }
        std::string text;
        if (connected) {
            text = value_of(p.name);
            if (text.empty()) { text = std::string{p.initial}; }
            if (ascii_iequals(text, "currentcolor")) { text = current_color; }
        }
        std::string idl = style::css::idl_name_of(p.name);
        if (std::string_view{idl} == p.name) { idl.clear(); }
        published.push_back(entry{std::string{p.name}, std::move(idl), std::move(text)});
    }
    // SORTED, WHICH THE TABLE IS NOT. css/cssom's getComputedStyle-property-order
    // requires the indexed properties to come out in lexicographic order;
    // style/css/properties.hpp is written box-model-first because that is the
    // order a person reads it in. Sorting here keeps both true, and there are no
    // vendor-prefixed names in the table, so plain byte order is the whole rule.
    std::ranges::sort(published, [](const entry & a, const entry & b) { return a.css < b.css; });

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
    const auto longhand = [&published](std::string_view name) -> std::string {
        for (const entry & one : published) {
            if (std::string_view{one.css} == name) { return one.text; }
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
    std::vector<entry> shorthands;
    for (const style::css::property_syntax & p : style::css::known_properties()) {
        if (!is_shorthand(p.name)) { continue; }
        std::string text;
        if (!connected) {
            // nothing to assemble - a detached element has no longhands either
        } else if (p.name == "margin") {
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
        std::string idl = style::css::idl_name_of(p.name);
        if (std::string_view{idl} == p.name) { idl.clear(); }
        shorthands.push_back(entry{std::string{p.name}, std::move(idl), std::move(text)});
    }

    // AND EVERY PROPERTY THE ELEMENT ITSELF DECLARED that the table has never
    // heard of - a CUSTOM property above all, which no fixed table can enumerate.
    // Readable, and answering to `in`, but NOT indexed: CSSOM's indexed
    // properties are the supported longhands and a page's `--brand` is not one.
    std::vector<entry> extra;
    if (connected && styles_ != nullptr) {
        const auto found = styles_->find(style::engine::key_of(id));
        if (found != styles_->end() && found->second) {
            for (const style::declaration & d : found->second->declarations) {
                const std::string_view name = atoms_->text(d.property);
                if (style::css::find_property(name) != nullptr) { continue; }
                const auto seen = std::find_if(extra.begin(), extra.end(), [name](const entry & e) {
                    return std::string_view{e.css} == name;
                });
                if (seen != extra.end()) { continue; }
                std::string text = value_of(name);
                if (text.empty()) { continue; }
                std::string idl = style::css::idl_name_of(name);
                if (std::string_view{idl} == name) { idl.clear(); }
                extra.push_back(entry{std::string{name}, std::move(idl), std::move(text)});
            }
        }
    }

    // BOTH SPELLINGS, SHARING ONE STRING OBJECT. `background-color` and
    // `backgroundColor` are one property read two ways, and allocating the value
    // twice would double the ~250 heap objects a getComputedStyle call already
    // costs on a page that asks for one per element.
    const auto publish = [&](const entry & one) {
        const value text = cx.string(one.text);
        held->set(one.css, text);
        if (!one.idl.empty()) { held->set(one.idl, text); }
    };
    for (const entry & one : published) { publish(one); }
    for (const entry & one : shorthands) { publish(one); }
    for (const entry & one : extra) { publish(one); }

    // THE INDEXED GETTER AND `length`, CSSOM §6.7 - as DATA properties, which is
    // also what makes the object ITERABLE. `[...style]` and `for (const p of
    // style)` go through context::iterable_values, and that recognises anything
    // carrying a numeric `length` beside indexed properties; there is no
    // Symbol.iterator dispatch in this VM to hook instead. serialize-all-longhands
    // and getComputedStyle-property-order both spread one.
    if (connected) {
        for (std::size_t i = 0; i < published.size(); ++i) {
            held->set(std::to_string(i), cx.string(published[i].css));
        }
    }
    held->set("length", value::number(connected ? static_cast<double>(published.size()) : 0.0));

    const auto method = [&](std::string name, script::native_fn fn) {
        held->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // THE SPELLING THE DUMP USES, and the only one both engines agree on. It
    // takes a CSS name and accepts the IDL one too, because a page holding
    // `backgroundColor` should not have to hyphenate it itself. It reads the
    // table computed above rather than recomputing, so the object holds no
    // pointer into a box tree that the next layout - which may now happen inside
    // the same script turn - is about to free.
    std::vector<entry> answers = published;
    answers.insert(answers.end(), shorthands.begin(), shorthands.end());
    answers.insert(answers.end(), extra.begin(), extra.end());
    method("getPropertyValue", [answers](context & c, std::span<value> args) {
        if (args.empty()) { return c.string(std::string{}); }
        const std::string asked = style::css::css_name_of(c.to_string(args[0]));
        for (const entry & one : answers) {
            if (one.css == asked) { return c.string(one.text); }
        }
        return c.string(std::string{});
    });
    // Always empty. Importance is a cascade INPUT, and by the time a value is
    // computed the question has been settled; this engine does not keep which
    // declaration won past resolve().
    method("getPropertyPriority", [](context & c, std::span<value>) { return c.string(""); });
    std::vector<std::string> names;
    if (connected) {
        names.reserve(published.size());
        for (const entry & one : published) { names.push_back(one.css); }
    }
    method("item", [names](context & c, std::span<value> args) {
        if (args.empty()) { return c.string(std::string{}); }
        const double i = context::to_number(args[0]);
        if (!(i >= 0) || static_cast<std::size_t>(i) >= names.size()) { return c.string(""); }
        return c.string(names[static_cast<std::size_t>(i)]);
    });
    // A COMPUTED STYLE IS READ-ONLY, and CSSOM §6.7.2 says how: every mutating
    // member throws NoModificationAllowedError. Doing nothing instead let a page
    // write to it and believe the write had landed.
    //
    // The property-ASSIGNMENT half of the same rule is deliberately absent:
    // `style.color = "blue"` has to throw too, and making it would mean a setter
    // accessor on each of the 125 published names, per call. That is a real cost
    // for one subtest, and computed-style-001 asserts it beside two other things
    // that already fail.
    const auto refuse = [this](context & c, std::span<value>) {
        throw_dom_exception(c, "NoModificationAllowedError",
                            "a computed style declaration is read-only");
        return value::undefined();
    };
    method("setProperty", refuse);
    method("removeProperty", refuse);
    // Empty rather than reconstructed - which is also what the specification
    // says: a computed style's cssText getter returns the empty string, because
    // serialising one means deciding how to rebuild every shorthand and engines,
    // including Chrome across its own versions, disagree there. Which is exactly
    // why the compared property set is longhands only.
    held->set("cssText", cx.string(std::string{}));
    // A computed style belongs to no rule.
    held->set("parentRule", value::null());
    return value::object(held);
}

void dom_bindings::install_computed_style(context & cx) {
    // A BARE GLOBAL IS ENOUGH for `window.getComputedStyle` as well: `window` is
    // a proxy whose handler falls back to the globals (install_window), so it
    // does not need its own copy. p5.js reads the bare form
    // (`getComputedStyle(el)` in vendor/p5/p5.js) and pages write both.
    //
    // browser::run_scripts WRAPS THIS GLOBAL to flush a pending restyle before
    // it runs - the bindings deliberately do not know that layout exists, and
    // without the flush every answer below is the one from before the script's
    // own write. See the note there; it is half of what makes this file useful.
    cx.define_native("getComputedStyle", [this](context & c, std::span<value> args) {
        // A second argument names a pseudo-element. Accepted and IGNORED rather
        // than rejected: ::before and ::after generate no boxes yet, and throwing
        // here would make the dump script engine-specific, which is the one thing
        // a parity harness must never be.
        const node_id id = args.empty() ? node_id{} : handle_of(args[0]);
        if (!id) { return c.make_object(); }
        return computed_style_object(c, id);
    });
}

} // namespace ctbrowser::shell
