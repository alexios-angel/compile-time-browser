// The property table - which properties exist, what each accepts and its
// initial value - and the two spellings of a property's name.
//
// One of three files carved out of a 1,155-line css/properties.cpp on
// 2026-09-08. The public surface is include/ctbrowser/style/css/properties.hpp
// and did not change; the helpers more than one of these files needs are
// declared in internal.hpp beside this, with external linkage in
// ctbrowser::style::css::detail.

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
    // --- the box ---------------------------------------------------------
    {"display", k::keyword_only,
     "block inline inline-block flex inline-flex grid inline-grid none table inline-table "
     "table-row table-row-group table-header-group table-footer-group table-column "
     "table-column-group table-cell table-caption list-item flow-root contents ruby",
     "inline", false, false},
    {"position", k::keyword_only, "static relative absolute fixed sticky", "static", false, false},
    {"float", k::keyword_only, "none left right inline-start inline-end", "none", false, false},
    {"clear", k::keyword_only, "none left right both inline-start inline-end", "none", false,
     false},
    {"visibility", k::keyword_only, "visible hidden collapse", "visible", true, false},
    {"overflow", k::freeform, "", "visible", false, false, true},
    {"overflow-x", k::keyword_only, "visible hidden clip scroll auto", "visible", false, false},
    {"overflow-y", k::keyword_only, "visible hidden clip scroll auto", "visible", false, false},
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
    {"padding", k::freeform, "", "0px", false, false, true},
    {"padding-top", k::length_percentage, "", "0px", false, true},
    {"padding-right", k::length_percentage, "", "0px", false, true},
    {"padding-bottom", k::length_percentage, "", "0px", false, true},
    {"padding-left", k::length_percentage, "", "0px", false, true},

    {"top", k::length_percentage, "auto", "auto", false, false},
    {"right", k::length_percentage, "auto", "auto", false, false},
    {"bottom", k::length_percentage, "auto", "auto", false, false},
    {"left", k::length_percentage, "auto", "auto", false, false},
    {"inset", k::freeform, "", "auto", false, false, true},

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
    {"border-top-color", k::freeform, "", "currentcolor", false, false},
    {"border-right-color", k::freeform, "", "currentcolor", false, false},
    {"border-bottom-color", k::freeform, "", "currentcolor", false, false},
    {"border-left-color", k::freeform, "", "currentcolor", false, false},
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
    {"outline-color", k::freeform, "", "currentcolor", false, false},
    {"outline-offset", k::length, "", "0px", false, false},

    // --- colour and background ------------------------------------------
    {"color", k::freeform, "", "rgb(0, 0, 0)", true, false},
    {"background", k::freeform, "", "none", false, false, true},
    {"background-color", k::freeform, "", "rgba(0, 0, 0, 0)", false, false},
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
    {"line-height", k::number_length_percentage, "normal", "normal", true, true},
    // CSS Text 4 gave both of these a percentage: `normal | <length-percentage>`.
    // `calc-letter-spacing` asks for `letter-spacing: calc(100%)` to compute to
    // `100%` rather than be dropped, which is the same question.
    {"letter-spacing", k::length_percentage, "normal", "normal", true, false},
    {"word-spacing", k::length_percentage, "normal", "normal", true, false},
    {"text-align", k::keyword_only, "start end left right center justify match-parent", "start",
     true, false},
    {"text-indent", k::length_percentage, "", "0px", true, false},
    {"text-transform", k::keyword_only,
     "none capitalize uppercase lowercase full-width full-size-kana", "none", true, false},
    {"text-decoration", k::freeform, "", "none", false, false, true},
    {"text-decoration-line", k::freeform, "", "none", false, false},
    {"text-decoration-color", k::freeform, "", "currentcolor", false, false},
    {"text-decoration-style", k::keyword_only, "solid double dotted dashed wavy", "solid", false,
     false},
    {"text-overflow", k::freeform, "", "clip", false, false},
    {"text-shadow", k::freeform, "", "none", true, false},
    {"white-space", k::keyword_only, "normal pre nowrap pre-wrap pre-line break-spaces", "normal",
     true, false},
    {"word-break", k::keyword_only, "normal break-all keep-all break-word", "normal", true, false},
    {"overflow-wrap", k::keyword_only, "normal break-word anywhere", "normal", true, false},
    {"direction", k::keyword_only, "ltr rtl", "ltr", true, false},
    {"unicode-bidi", k::freeform, "", "normal", false, false},
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
    {"justify-content", k::keyword_only,
     "normal stretch flex-start flex-end center space-between space-around space-evenly start end "
     "left right",
     "normal", false, false},
    {"align-items", k::keyword_only,
     "normal stretch center start end flex-start flex-end self-start self-end baseline", "normal",
     false, false},
    {"align-self", k::keyword_only,
     "auto normal stretch center start end flex-start flex-end self-start self-end baseline",
     "auto", false, false},
    {"align-content", k::keyword_only,
     "normal stretch center start end flex-start flex-end space-between space-around space-evenly",
     "normal", false, false},
    {"gap", k::freeform, "", "normal", false, false, true},
    {"row-gap", k::length_percentage, "normal", "normal", false, true},
    {"column-gap", k::length_percentage, "normal", "normal", false, true},
    {"order", k::integer, "", "0", false, false},
    {"z-index", k::integer, "auto", "auto", false, false},

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
    {"transition-duration", k::time, "", "0s", false, false},
    {"transition-delay", k::time, "", "0s", false, false},
    {"transition-property", k::freeform, "", "all", false, false},
    {"transition-timing-function", k::freeform, "", "ease", false, false},
    {"animation", k::freeform, "", "none", false, false, true},
    {"animation-duration", k::time, "", "0s", false, false},
    {"animation-delay", k::time, "", "0s", false, false},
    {"animation-name", k::freeform, "", "none", false, false},
    {"animation-iteration-count", k::freeform, "", "1", false, false},
    {"filter", k::freeform, "", "none", false, false},
    {"content", k::freeform, "", "normal", false, false},
    {"pointer-events", k::freeform, "", "auto", true, false},
    {"user-select", k::keyword_only, "auto text none contain all", "auto", false, false},
    {"resize", k::keyword_only, "none both horizontal vertical block inline", "none", false, false},
    {"object-fit", k::keyword_only, "fill contain cover none scale-down", "fill", false, false},
    {"object-position", k::position, "", "50% 50%", false, false},
    {"rotate", k::angle, "none", "none", false, false},
    {"scale", k::freeform, "", "none", false, false},
    {"translate", k::freeform, "", "none", false, false},
};

} // namespace

const property_syntax * find_property(std::string_view name) {
    for (const property_syntax & one : table) {
        if (ascii_iequals(one.name, name)) { return &one; }
    }
    return nullptr;
}

std::span<const property_syntax> known_properties() {
    return std::span<const property_syntax>{table, std::size(table)};
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
