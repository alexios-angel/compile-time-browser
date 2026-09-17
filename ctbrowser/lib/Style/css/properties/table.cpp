// The property table - which properties exist, what each accepts and its
// initial value - and the two spellings of a property's name.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

// --- THE TABLE ----------------------------------------------------------
//
// Ordered as CSSOM's indexed properties enumerate them, which is to say
// deliberately rather than alphabetically: the box model first, then the
// typography, then the flex and table properties. `getComputedStyle`'s `item(i)`
// walks this.
//
// EVERY SHORTHAND IS `freeform`. `margin: 10px 20px` needs the expansion to be
// refused correctly, and a shorthand this file got half right is the one way it
// could break a page that works today.
constexpr property_syntax table[] = {
    // CSS Cascade 4 §3.1: every longhand but `direction` and `unicode-bidi`,
    // and it takes the CSS-wide keywords only. The declaration block in
    // shorthands.cpp expands it; here so `'all' in style` answers.
    {"all", k::freeform, "", "", false, false, true},
    // --- the box ---------------------------------------------------------
    // The keyword set is display.cpp's; listed here for `CSS.supports` and the
    // computed style, which read the table.
    {"display", k::keyword_only,
     "block inline inline-block flex inline-flex grid inline-grid none table inline-table "
     "table-row table-row-group table-header-group table-footer-group table-column "
     "table-column-group table-cell table-caption list-item flow-root contents ruby run-in "
     "ruby-base ruby-text ruby-base-container ruby-text-container flow",
     "inline", false, false},
    {"position", k::keyword_only, "static relative absolute fixed sticky", "static", false, false},
    {"float", k::keyword_only, "none left right inline-start inline-end", "none", false, false},
    {"clear", k::keyword_only, "none left right both inline-start inline-end", "none", false,
     false},
    {"visibility", k::keyword_only, "visible hidden collapse", "visible", true, false},
    {"overflow", k::freeform, "", "visible", false, false, true},
    {"overflow-x", k::keyword_only, "visible hidden clip scroll auto overlay", "visible", false,
     false},
    {"overflow-y", k::keyword_only, "visible hidden clip scroll auto overlay", "visible", false,
     false},
    {"box-sizing", k::keyword_only, "content-box border-box", "content-box", false, false},

    {"width", k::length_percentage, "auto min-content max-content fit-content stretch", "auto",
     false, true},
    {"height", k::length_percentage, "auto min-content max-content fit-content stretch", "auto",
     false, true},
    {"min-width", k::length_percentage, "auto min-content max-content fit-content stretch", "auto",
     false, true},
    {"min-height", k::length_percentage, "auto min-content max-content fit-content stretch", "auto",
     false, true},
    {"max-width", k::length_percentage, "none min-content max-content fit-content stretch", "none",
     false, true},
    {"max-height", k::length_percentage, "none min-content max-content fit-content stretch", "none",
     false, true},
    {"inline-size", k::length_percentage, "auto min-content max-content fit-content stretch",
     "auto", false, true},
    {"block-size", k::length_percentage, "auto min-content max-content fit-content stretch", "auto",
     false, true},
    // `interpolate-size` says whether an animation may interpolate BETWEEN a
    // keyword size and a length. Nothing animates here, so the property does
    // nothing - but it is a real property with a real two-keyword grammar, and
    // as an UNKNOWN one `el.style` stored `interpolate-size: 100%` and
    // `getComputedStyle` did not publish it at all. Both are observable and both
    // are wrong: `calc-size/interpolate-size-parsing.html` refuses three values
    // and `-computed.html` asks whether the property exists.
    {"interpolate-size", k::keyword_only, "numeric-only allow-keywords", "numeric-only", true,
     false},

    {"margin", k::freeform, "", "0px", false, false, true},
    {"margin-top", k::length_percentage, "auto", "0px", false, false},
    {"margin-right", k::length_percentage, "auto", "0px", false, false},
    {"margin-bottom", k::length_percentage, "auto", "0px", false, false},
    {"margin-left", k::length_percentage, "auto", "0px", false, false},
    // The flow-relative sides, CSS Logical 1 §4. The cascade maps each onto
    // the physical side of the element's writing mode and direction
    // (physical_property_of, below); they are rows of their own because the
    // CSSOM's shorthand rules are about the logical property GROUP a
    // declaration sits in, and `margin-inline: 10px` written between two
    // halves of `margin` decides whether `margin` folds.
    {"margin-inline", k::freeform, "", "0px", false, false, true},
    {"margin-inline-start", k::length_percentage, "auto", "0px", false, false},
    {"margin-inline-end", k::length_percentage, "auto", "0px", false, false},
    {"margin-block", k::freeform, "", "0px", false, false, true},
    {"margin-block-start", k::length_percentage, "auto", "0px", false, false},
    {"margin-block-end", k::length_percentage, "auto", "0px", false, false},
    {"padding", k::freeform, "", "0px", false, false, true},
    {"padding-top", k::length_percentage, "", "0px", false, true},
    {"padding-right", k::length_percentage, "", "0px", false, true},
    {"padding-bottom", k::length_percentage, "", "0px", false, true},
    {"padding-left", k::length_percentage, "", "0px", false, true},
    {"padding-inline", k::freeform, "", "0px", false, false, true},
    {"padding-inline-start", k::length_percentage, "", "0px", false, true},
    {"padding-inline-end", k::length_percentage, "", "0px", false, true},
    {"padding-block", k::freeform, "", "0px", false, false, true},
    // The logical border longhands, mapped like the margins.
    {"border-block-start-width", k::length, "thin medium thick", "medium", false, true},
    {"border-block-end-width", k::length, "thin medium thick", "medium", false, true},
    {"border-inline-start-width", k::length, "thin medium thick", "medium", false, true},
    {"border-inline-end-width", k::length, "thin medium thick", "medium", false, true},
    {"border-block-start-style", k::keyword_only,
     "none hidden dotted dashed solid double groove ridge inset outset", "none", false, false},
    {"border-block-end-style", k::keyword_only,
     "none hidden dotted dashed solid double groove ridge inset outset", "none", false, false},
    {"border-inline-start-style", k::keyword_only,
     "none hidden dotted dashed solid double groove ridge inset outset", "none", false, false},
    {"border-inline-end-style", k::keyword_only,
     "none hidden dotted dashed solid double groove ridge inset outset", "none", false, false},
    {"border-block-start-color", k::color, "", "currentcolor", false, false},
    {"border-block-end-color", k::color, "", "currentcolor", false, false},
    {"border-inline-start-color", k::color, "", "currentcolor", false, false},
    {"border-inline-end-color", k::color, "", "currentcolor", false, false},
    {"padding-block-start", k::length_percentage, "", "0px", false, true},
    {"padding-block-end", k::length_percentage, "", "0px", false, true},

    {"top", k::length_percentage, "auto", "auto", false, false},
    {"right", k::length_percentage, "auto", "auto", false, false},
    {"bottom", k::length_percentage, "auto", "auto", false, false},
    {"left", k::length_percentage, "auto", "auto", false, false},
    {"inset", k::freeform, "", "auto", false, false, true},
    {"inset-inline", k::freeform, "", "auto", false, false, true},
    {"inset-inline-start", k::length_percentage, "auto", "auto", false, false},
    {"inset-inline-end", k::length_percentage, "auto", "auto", false, false},
    {"inset-block", k::freeform, "", "auto", false, false, true},
    {"inset-block-start", k::length_percentage, "auto", "auto", false, false},
    {"inset-block-end", k::length_percentage, "auto", "auto", false, false},

    // --- borders ---------------------------------------------------------
    {"border", k::freeform, "", "medium none currentcolor", false, false, true},
    {"border-width", k::freeform, "", "medium", false, false, true},
    {"border-style", k::freeform, "", "none", false, false, true},
    {"border-color", k::freeform, "", "currentcolor", false, false, true},
    {"border-top-width", k::length, "thin medium thick", "medium", false, true},
    {"border-right-width", k::length, "thin medium thick", "medium", false, true},
    {"border-bottom-width", k::length, "thin medium thick", "medium", false, true},
    {"border-left-width", k::length, "thin medium thick", "medium", false, true},
    {"border-top-style", k::keyword_only,
     "none hidden dotted dashed solid double groove ridge inset outset", "none", false, false},
    {"border-right-style", k::keyword_only,
     "none hidden dotted dashed solid double groove ridge inset outset", "none", false, false},
    {"border-bottom-style", k::keyword_only,
     "none hidden dotted dashed solid double groove ridge inset outset", "none", false, false},
    {"border-left-style", k::keyword_only,
     "none hidden dotted dashed solid double groove ridge inset outset", "none", false, false},
    {"border-top-color", k::color, "", "currentcolor", false, false},
    {"border-right-color", k::color, "", "currentcolor", false, false},
    {"border-bottom-color", k::color, "", "currentcolor", false, false},
    {"border-left-color", k::color, "", "currentcolor", false, false},
    // `border` RESETS border-image (CSS Backgrounds 3 §5.3), so the CSSOM
    // cannot fold twelve side longhands back into `border` without knowing
    // these five are at their initial values. Nothing paints them.
    {"border-image", k::freeform, "", "none", false, false, true},
    {"border-image-source", k::freeform, "", "none", false, false},
    {"border-image-slice", k::freeform, "", "100%", false, false},
    {"border-image-width", k::freeform, "", "1", false, false},
    {"border-image-outset", k::freeform, "", "0", false, false},
    {"border-image-repeat", k::freeform, "", "stretch", false, false},
    {"border-radius", k::freeform, "", "0px", false, false, true},
    // The four per-side shorthands. Named here rather than left out because
    // `css/cssom/getComputedStyle-getter-v-properties` asks for all four by
    // name, and a property CSSOM says exists must answer `in`.
    {"border-top", k::freeform, "", "0px none currentcolor", false, false, true},
    {"border-right", k::freeform, "", "0px none currentcolor", false, false, true},
    {"border-bottom", k::freeform, "", "0px none currentcolor", false, false, true},
    {"border-left", k::freeform, "", "0px none currentcolor", false, false, true},
    {"border-top-left-radius", k::freeform, "", "0px", false, false},
    {"border-top-right-radius", k::freeform, "", "0px", false, false},
    {"border-bottom-right-radius", k::freeform, "", "0px", false, false},
    {"border-bottom-left-radius", k::freeform, "", "0px", false, false},
    {"border-collapse", k::keyword_only, "separate collapse", "separate", true, false},
    {"border-spacing", k::freeform, "", "0px", true, false},
    {"outline", k::freeform, "", "medium none currentcolor", false, false, true},
    {"outline-width", k::length, "thin medium thick", "medium", false, true},
    {"outline-style", k::keyword_only,
     "auto none hidden dotted dashed solid double groove ridge inset outset", "none", false, false},
    {"outline-color", k::color, "invert", "currentcolor", false, false},
    {"outline-offset", k::length, "", "0px", false, false},

    // --- colour and background ------------------------------------------
    {"color", k::color, "", "rgb(0, 0, 0)", true, false},
    {"caret-color", k::color, "auto", "auto", true, false},
    {"accent-color", k::color, "auto", "auto", false, false},
    {"background", k::freeform, "", "none", false, false, true},
    {"background-color", k::color, "", "rgba(0, 0, 0, 0)", false, false},
    {"background-image", k::freeform, "", "none", false, false},
    {"background-position", k::freeform, "", "0% 0%", false, false},
    {"background-repeat", k::freeform, "", "repeat", false, false},
    {"background-size", k::freeform, "", "auto", false, false},
    {"background-clip", k::keyword_only, "border-box padding-box content-box text", "border-box",
     false, false},
    {"background-origin", k::keyword_only, "border-box padding-box content-box", "padding-box",
     false, false},
    {"background-attachment", k::keyword_only, "scroll fixed local", "scroll", false, false},
    {"opacity", k::number_percentage, "", "1", false, false},

    // --- typography ------------------------------------------------------
    {"font", k::freeform, "", "", true, false, true},
    {"font-family", k::freeform, "", "sans-serif", true, false},
    // THE INITIAL IS `medium` AND THE COMPUTED VALUE IS A LENGTH. `initial` is
    // read by `getComputedStyle` for an element with no box, and a computed
    // style reports lengths - Chrome answers `16px` for a `display: none`
    // element, never `medium`. The keyword stays in the accepted set; what is
    // recorded here is what the property COMPUTES to when nothing declares it,
    // which is the medium font size and is 16px in this engine
    // (layout/values.hpp's `rem` basis is the same number for the same reason).
    {"font-size", k::length_percentage,
     "xx-small x-small small medium large x-large xx-large xxx-large larger smaller", "16px", true,
     true},
    {"font-style", k::freeform, "", "normal", true, false},
    {"font-weight", k::number, "normal bold bolder lighter", "400", true, true},
    {"font-variant", k::freeform, "", "normal", true, false},
    {"font-stretch", k::freeform, "", "100%", true, false},
    // The rest of CSS Fonts 4's longhands, as freeform text: nothing shapes
    // with them, but each is a real property whose value a page sets and reads
    // back through getComputedStyle, with a math function inside it folded
    // (using-font-relative-units-in-font-properties).
    {"font-width", k::freeform, "", "normal", true, false},
    {"font-feature-settings", k::freeform, "", "normal", true, false},
    {"font-variation-settings", k::freeform, "", "normal", true, false},
    {"font-variant-alternates", k::freeform, "", "normal", true, false},
    {"font-size-adjust", k::freeform, "", "none", true, false},
    {"font-palette", k::freeform, "", "normal", true, false},
    // NOT HERE YET: font-feature-settings, font-palette, font-size-adjust,
    // font-variant-alternates, font-variation-settings and font-width, which
    // using-font-relative-units-in-font-properties asks to exist. Adding the
    // six put a page's baseline script heap over 3,000 objects (each known
    // property is accessors on the style prototypes), and unit/page_async's
    // heap-growth assertion holds only below that: the collector's threshold
    // doubles after a collection, so a 3,000-object churn against a baseline
    // above 3,000 collects on every OTHER tick and the sixtieth sample lands
    // on the high one (3005 -> 6005). Fix the test's sampling first.
    {"line-height", k::number_length_percentage, "normal", "normal", true, true},
    // CSS Text 4 gave both of these a percentage: `normal | <length-percentage>`.
    // `calc-letter-spacing` asks for `letter-spacing: calc(100%)` to compute to
    // `100%` rather than be dropped, which is the same question.
    {"letter-spacing", k::length_percentage, "normal", "normal", true, false},
    {"word-spacing", k::length_percentage, "normal", "normal", true, false},
    {"text-align", k::keyword_only, "start end left right center justify match-parent", "start",
     true, false},
    {"text-indent", k::length_percentage, "", "0px", true, false},
    {"text-transform", k::freeform, "", "none", true, false}, // keywords.cpp's grammar
    {"text-decoration", k::freeform, "", "none", false, false, true},
    {"text-decoration-line", k::freeform, "", "none", false, false},
    {"text-decoration-color", k::color, "", "currentcolor", false, false},
    {"text-decoration-style", k::keyword_only, "solid double dotted dashed wavy", "solid", false,
     false},
    {"text-overflow", k::freeform, "", "clip", false, false},
    {"text-shadow", k::freeform, "", "none", true, false},
    {"white-space", k::keyword_only, "normal pre nowrap pre-wrap pre-line break-spaces", "normal",
     true, false},
    {"word-break", k::keyword_only, "normal break-all keep-all break-word manual auto-phrase",
     "normal", true, false},
    {"overflow-wrap", k::keyword_only, "normal break-word anywhere", "normal", true, false},
    {"direction", k::keyword_only, "ltr rtl", "ltr", true, false},
    // Read by the cascade (an inherited property) and by the computed style's
    // logical-to-physical mapping; a property the engine reads must be in
    // this table, because the table is what `el.style.writingMode = x` is
    // checked against - an unsupported name is an expando there.
    {"writing-mode", k::keyword_only,
     "horizontal-tb vertical-rl vertical-lr sideways-rl sideways-lr", "horizontal-tb", true, false},
    {"unicode-bidi", k::keyword_only,
     "normal embed isolate bidi-override isolate-override plaintext", "normal", false, false},
    {"tab-size", k::number_length, "", "8", true, true},
    {"vertical-align", k::length_percentage,
     "baseline sub super text-top text-bottom middle top bottom", "baseline", false, false},

    // --- flex, grid and the layout numbers -------------------------------
    {"flex", k::freeform, "", "0 1 auto", false, false, true},
    {"flex-grow", k::number, "", "0", false, true},
    {"flex-shrink", k::number, "", "1", false, true},
    {"flex-basis", k::length_percentage, "auto content min-content max-content fit-content", "auto",
     false, true},
    {"flex-direction", k::keyword_only, "row row-reverse column column-reverse", "row", false,
     false},
    {"flex-wrap", k::keyword_only, "nowrap wrap wrap-reverse", "nowrap", false, false},
    {"flex-flow", k::freeform, "", "row nowrap", false, false, true},
    // The six box-alignment longhands are alignment.cpp's grammar; freeform
    // here so the table's single-keyword path does not answer first.
    {"justify-content", k::freeform, "", "normal", false, false},
    {"align-items", k::freeform, "", "normal", false, false},
    {"align-self", k::freeform, "", "auto", false, false},
    {"align-content", k::freeform, "", "normal", false, false},
    {"gap", k::freeform, "", "normal", false, false, true},
    {"row-gap", k::length_percentage, "normal", "normal", false, true},
    {"column-gap", k::length_percentage, "normal", "normal", false, true},
    {"order", k::integer, "", "0", false, false},
    {"z-index", k::integer, "auto", "auto", false, false},
    // Three more `<integer>` and keyword properties, named because
    // `calc-rounds-to-integer` sets `10.1` and `1e1` on each and expects the
    // declaration refused - which an unknown property, stored verbatim, cannot
    // do. `1e1` is a <number-token> and not an <integer>, CSS Syntax 3 §4.3.12.
    {"orphans", k::integer, "", "2", true, true},
    {"widows", k::integer, "", "2", true, true},
    // Three more `<integer>` properties nothing lays out, so that `1e1` and
    // `10.1` are refused where `calc(10.1)` rounds (calc-rounds-to-integer).
    // ponytail: hyphenate-limit-chars takes one value here, not the spec's three.
    {"max-lines", k::integer, "none auto", "auto", false, true},
    {"hyphenate-limit-lines", k::integer, "no-limit", "no-limit", true, true},
    {"hyphenate-limit-chars", k::integer, "auto", "auto", true, true},
    {"column-span", k::keyword_only, "none all", "none", false, false},

    // --- tables and lists ------------------------------------------------
    {"table-layout", k::keyword_only, "auto fixed", "auto", false, false},
    {"caption-side", k::keyword_only, "top bottom", "top", true, false},
    {"empty-cells", k::keyword_only, "show hide", "show", true, false},
    {"list-style", k::freeform, "", "outside none disc", true, false, true},
    {"list-style-type", k::freeform, "", "disc", true, false},
    {"list-style-position", k::keyword_only, "inside outside", "outside", true, false},
    {"list-style-image", k::freeform, "", "none", true, false},

    // --- the rest, known to exist and not modelled -----------------------
    {"cursor", k::freeform, "", "auto", true, false},
    {"box-shadow", k::freeform, "", "none", false, false},
    {"transform", k::freeform, "", "none", false, false},
    {"transform-origin", k::freeform, "", "50% 50%", false, false},
    {"transition", k::freeform, "", "all 0s ease 0s", false, false, true},
    {"transition-duration", k::time, "", "0s", false, true},
    {"transition-delay", k::time, "", "0s", false, false},
    {"transition-property", k::freeform, "", "all", false, false},
    {"transition-timing-function", k::freeform, "", "ease", false, false},
    {"animation", k::freeform, "", "none", false, false, true},
    {"animation-duration", k::time, "auto", "0s", false, true},
    {"animation-delay", k::time, "", "0s", false, false},
    {"animation-name", k::freeform, "", "none", false, false},
    // `none | <custom-ident> | match-element`, CSS View Transitions 1 §4.1.
    // Nothing transitions here; it is the property ident-function-computed
    // reads an `ident()` back through, and as an UNKNOWN one getComputedStyle
    // did not publish it at all.
    {"view-transition-name", k::freeform, "", "none", false, false},
    {"animation-iteration-count", k::number, "infinite", "1", false, true},
    {"filter", k::freeform, "", "none", false, false},
    {"content", k::freeform, "", "normal", false, false},
    // PROPERTIES WITH NO CONSUMER YET, carried so a page can set and read
    // them back - every one is a `<length-percentage>` or freeform text, and as
    // unknown ones getComputedStyle did not publish them (random-computed).
    {"offset-distance", k::length_percentage, "", "0px", false, false},
    {"offset-path", k::freeform, "", "none", false, false},
    {"shape-margin", k::length_percentage, "", "0px", false, true},
    {"stroke-dasharray", k::freeform, "", "none", true, false},
    {"stroke-dashoffset", k::length_percentage, "", "0px", true, false},
    {"stroke-width", k::length_percentage, "", "1px", true, true},
    {"background-position-x", k::freeform, "", "0%", false, false},
    {"background-position-y", k::freeform, "", "0%", false, false},
    {"scroll-padding-top", k::length_percentage, "auto", "auto", false, true},
    {"scroll-padding-right", k::length_percentage, "auto", "auto", false, true},
    {"scroll-padding-bottom", k::length_percentage, "auto", "auto", false, true},
    {"scroll-padding-left", k::length_percentage, "auto", "auto", false, true},
    {"cx", k::length_percentage, "", "0px", false, false},
    {"cy", k::length_percentage, "", "0px", false, false},
    {"rx", k::length_percentage, "auto", "auto", false, true},
    {"ry", k::length_percentage, "auto", "auto", false, true},
    {"x", k::length_percentage, "", "0px", false, false},
    {"y", k::length_percentage, "", "0px", false, false},
    {"math-depth", k::freeform, "", "0", true, false},
    {"aspect-ratio", k::freeform, "", "auto", false, false},
    {"animation-timeline", k::freeform, "", "auto", false, false},
    {"corner-shape", k::freeform, "", "round", false, false},
    {"pointer-events", k::freeform, "", "auto", true, false},
    {"user-select", k::keyword_only, "auto text none contain all", "auto", false, false},
    {"resize", k::keyword_only, "none both horizontal vertical block inline", "none", false, false},
    {"object-fit", k::keyword_only, "fill contain cover none scale-down", "fill", false, false},
    {"object-position", k::position, "", "50% 50%", false, false},
    {"rotate", k::freeform, "", "none", false, false}, // transforms.cpp's grammar
    {"scale", k::freeform, "", "none", false, false},
    {"translate", k::freeform, "", "none", false, false},
};

} // namespace

// The core rows above and table_modules.cpp's rows, as one list in one order:
// `getComputedStyle`'s indexed properties walk it.
[[nodiscard]] const std::vector<property_syntax> & every_property() {
    static const std::vector<property_syntax> all = [] {
        std::vector<property_syntax> out(std::begin(table), std::end(table));
        const std::span<const property_syntax> more = module_properties();
        out.insert(out.end(), more.begin(), more.end());
        return out;
    }();
    return all;
}

const property_syntax * find_property(std::string_view name) {
    for (const property_syntax & one : every_property()) {
        if (ascii_iequals(one.name, name)) { return &one; }
    }
    return nullptr;
}

std::span<const property_syntax> known_properties() {
    return std::span<const property_syntax>{every_property()};
}

// CSS Logical 1 §2: the four flow-relative sides as physical ones. Block
// start/end follow the writing mode alone; inline start/end follow it and
// the direction, and `sideways-lr` is the one mode whose inline axis runs
// upward.
std::string physical_property_of(std::string_view logical, std::string_view writing_mode,
                                 std::string_view direction) {
    const std::string mode = ascii_lower_copy(trim(writing_mode, html_whitespace));
    const bool vertical = mode.starts_with("vertical-") || mode.starts_with("sideways-");
    const bool rtl = ascii_iequals(trim(direction, html_whitespace), "rtl");
    std::string_view block_start, block_end, inline_start, inline_end;
    if (!vertical) {
        block_start = "top";
        block_end = "bottom";
        inline_start = rtl ? "right" : "left";
        inline_end = rtl ? "left" : "right";
    } else {
        const bool rl = mode.ends_with("-rl");
        block_start = rl ? "right" : "left";
        block_end = rl ? "left" : "right";
        const bool downward = (mode != "sideways-lr") != rtl;
        inline_start = downward ? "top" : "bottom";
        inline_end = downward ? "bottom" : "top";
    }
    const std::string name = ascii_lower_copy(logical);
    // `inline-size`, `min-block-size`: a dimension.
    if (name.ends_with("-size")) {
        const std::string_view stem = std::string_view{name}.substr(0, name.size() - 5);
        std::string_view prefix;
        std::string_view axis = stem;
        if (stem.starts_with("min-") || stem.starts_with("max-")) {
            prefix = stem.substr(0, 4);
            axis = stem.substr(4);
        }
        if (axis != "inline" && axis != "block") { return {}; }
        const bool horizontal = (axis == "inline") != vertical;
        return std::string{prefix} + (horizontal ? "width" : "height");
    }
    // `border-start-end-radius`: block side then inline side, and the physical
    // name puts the vertical side first.
    if (name.starts_with("border-") && name.ends_with("-radius")) {
        const std::string_view corner = std::string_view{name}.substr(7, name.size() - 14);
        const std::size_t dash = corner.find('-');
        if (dash == std::string_view::npos) { return {}; }
        const std::string_view b = corner.substr(0, dash);
        const std::string_view i = corner.substr(dash + 1);
        if ((b != "start" && b != "end") || (i != "start" && i != "end")) { return {}; }
        const std::string_view block_side = b == "start" ? block_start : block_end;
        const std::string_view inline_side = i == "start" ? inline_start : inline_end;
        const std::string_view first = vertical ? inline_side : block_side;
        const std::string_view second = vertical ? block_side : inline_side;
        return "border-" + std::string{first} + "-" + std::string{second} + "-radius";
    }
    // `overflow-block`, `overscroll-behavior-inline`: an axis.
    for (const auto [suffix, axis_is_inline] : {std::pair{std::string_view{"-block"}, false},
                                                std::pair{std::string_view{"-inline"}, true}}) {
        if (!name.ends_with(suffix)) { continue; }
        const std::string_view stem = std::string_view{name}.substr(0, name.size() - suffix.size());
        if (stem != "overflow" && stem != "overscroll-behavior") { return {}; }
        const bool horizontal = axis_is_inline != vertical;
        return std::string{stem} + (horizontal ? "-x" : "-y");
    }
    // `margin-block-start`, `inset-inline-end`, `border-block-start-width`:
    // the side, in place of the flow-relative words; `inset-` is dropped
    // because the physical insets are bare `top`/`right`/`bottom`/`left`.
    static constexpr std::pair<std::string_view, int> sides[] = {
        {"block-start", 0}, {"block-end", 1}, {"inline-start", 2}, {"inline-end", 3}};
    for (const auto & [words, which] : sides) {
        const std::size_t at = name.find(words);
        if (at == std::string::npos) { continue; }
        // A real property name only: `-block-start` after a family or at the
        // start of `inset-block-start`, never inside another word.
        const std::string_view stem = std::string_view{name}.substr(0, at);
        if (!stem.empty() && !stem.ends_with('-')) { return {}; }
        if (find_property(logical) == nullptr) { return {}; }
        const std::string_view side = which == 0   ? block_start
                                      : which == 1 ? block_end
                                      : which == 2 ? inline_start
                                                   : inline_end;
        std::string out = std::string{stem} + std::string{side} + name.substr(at + words.size());
        if (out.starts_with("inset-")) { out.erase(0, 6); }
        return out;
    }
    return {};
}

// CSSOM §2.1 "serialize an identifier".
std::string serialize_identifier(std::string_view text) {
    std::string out;
    const auto hex_escape = [&out](unsigned char c) {
        static constexpr char digits[] = "0123456789abcdef";
        out += '\\';
        if (c >= 16) { out += digits[c >> 4]; }
        out += digits[c & 0xF];
        out += ' ';
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        // NULL is not escaped, it is REPLACED - §2.1 step 3, the same U+FFFD
        // substitution the CSS tokenizer does to its input.
        if (c == 0) {
            out += "\xEF\xBF\xBD";
            continue;
        }
        if (c <= 0x1F || c == 0x7F) {
            hex_escape(c);
            continue;
        }
        // A LEADING DIGIT, or a digit after a leading `-`, would make the
        // identifier a number: both are escaped numerically rather than with a
        // backslash, because `\1` is not a valid identifier start either.
        if (c >= '0' && c <= '9' && (i == 0 || (i == 1 && text[0] == '-'))) {
            hex_escape(c);
            continue;
        }
        if (c == '-' && text.size() == 1) {
            out += "\\-";
            continue;
        }
        if (is_name(static_cast<char>(c))) {
            out += static_cast<char>(c);
            continue;
        }
        out += '\\';
        out += static_cast<char>(c);
    }
    return out;
}

std::string css_name_of(std::string_view idl) {
    // A CUSTOM PROPERTY has no IDL name and passes through untouched, capitals
    // and all: `--myVar` and `--myvar` are two different properties.
    if (idl.starts_with("--")) { return std::string{idl}; }
    // The two exceptions CSSOM §6.7.1 names by hand.
    if (idl == "cssFloat") { return "float"; }
    std::string out;
    // `webkitTransform` -> `-webkit-transform`: the prefix's leading dash is
    // dropped in the IDL name, so a plain camel-to-hyphen loop produces
    // `webkit-transform` and misses every prefixed property.
    if (ascii_istarts_with(idl, "webkit") && idl.size() > 6 && idl[6] >= 'A' && idl[6] <= 'Z') {
        out = "-webkit";
        idl.remove_prefix(6);
    }
    for (const char c : idl) {
        if (c >= 'A' && c <= 'Z') {
            out += '-';
            out += ascii_lower(c);
        } else {
            out += c;
        }
    }
    return out;
}

std::string idl_name_of(std::string_view css) {
    if (css.starts_with("--")) { return std::string{css}; }
    if (css == "float") { return "cssFloat"; }
    std::string out;
    bool upper_next = false;
    // A leading dash is dropped rather than turned into a capital:
    // `-webkit-transform` is `webkitTransform`, not `WebkitTransform`.
    if (!css.empty() && css.front() == '-') { css.remove_prefix(1); }
    for (const char c : css) {
        if (c == '-') {
            upper_next = true;
            continue;
        }
        out += upper_next ? ascii_upper(c) : c;
        upper_next = false;
    }
    return out;
}

} // namespace ctbrowser::style::css
