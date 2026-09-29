// The rest of the property table, grouped by CSS module: every property the
// parsing and inheritance corpus tests that table.cpp's core set does not
// carry. Each row is the property's initial value and inheritance from its
// module's specification; the grammar is `freeform` until modelled, because
// a name absent from the table is an expando on `el.style` (CSSOM §6.7.2)
// and a property with no row has no initial value for getComputedStyle to
// publish - the two halves of `inheritance.html`.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

constexpr property_syntax modules[] = {
    // --- CSS Animations 1 and 2, Scroll-driven Animations ------------------
    {"animation-timing-function", k::freeform, "", "ease", false, false},
    {"animation-direction", k::keyword_only, "normal reverse alternate alternate-reverse", "normal",
     false, false},
    {"animation-fill-mode", k::keyword_only, "none forwards backwards both", "none", false, false},
    {"animation-play-state", k::keyword_only, "running paused", "running", false, false},
    {"animation-composition", k::keyword_only, "replace add accumulate", "replace", false, false},
    {"animation-range", k::freeform, "", "normal", false, false, true},
    {"animation-range-start", k::freeform, "", "normal", false, false},
    {"animation-range-end", k::freeform, "", "normal", false, false},
    {"transition-behavior", k::keyword_only, "normal allow-discrete", "normal", false, false},
    // --- CSS Viewport 1 ------------------------------------------------------
    // ponytail: zoom parses, computes and animates, and layout does not yet
    // scale by it; multiply a box's lengths by the effective zoom when a
    // page needs the picture to change.
    {"zoom", k::number_percentage, "normal", "1", false, true},
    // --- CSS Transforms 2 ----------------------------------------------------
    {"backface-visibility", k::keyword_only, "visible hidden", "visible", false, false},
    {"perspective", k::length, "none", "none", false, true},
    {"perspective-origin", k::position, "", "50% 50%", false, false},
    {"transform-box", k::keyword_only, "content-box border-box fill-box stroke-box view-box",
     "view-box", false, false},
    {"transform-style", k::keyword_only, "flat preserve-3d", "flat", false, false},
    {"-webkit-transform", k::freeform, "", "none", false, false},
    {"-webkit-perspective", k::length, "none", "none", false, true},
    // --- Filter Effects, Compositing ------------------------------------------
    {"backdrop-filter", k::freeform, "", "none", false, false},
    {"flood-color", k::color, "", "rgb(0, 0, 0)", false, false},
    {"flood-opacity", k::number_percentage, "", "1", false, false},
    {"lighting-color", k::color, "", "rgb(255, 255, 255)", false, false},
    {"color-interpolation-filters", k::keyword_only, "auto srgb linearrgb", "linearrgb", true,
     false},
    {"background-blend-mode", k::freeform, "", "normal", false, false},
    {"mix-blend-mode", k::keyword_only,
     "normal multiply screen overlay darken lighten color-dodge color-burn hard-light soft-light "
     "difference exclusion hue saturation color luminosity plus-darker plus-lighter",
     "normal", false, false},
    {"isolation", k::keyword_only, "auto isolate", "auto", false, false},
    // --- CSS Masking, Shapes, Motion ------------------------------------------
    {"clip", k::freeform, "", "auto", false, false},
    {"clip-path", k::freeform, "", "none", false, false},
    {"clip-rule", k::keyword_only, "nonzero evenodd", "nonzero", true, false},
    {"mask", k::freeform, "", "none", false, false, true},
    {"mask-image", k::freeform, "", "none", false, false},
    {"mask-mode", k::freeform, "", "match-source", false, false},
    {"mask-repeat", k::freeform, "", "repeat", false, false},
    {"mask-position", k::freeform, "", "0% 0%", false, false},
    {"mask-clip", k::freeform, "", "border-box", false, false},
    {"mask-origin", k::freeform, "", "border-box", false, false},
    {"mask-size", k::freeform, "", "auto", false, true},
    {"mask-composite", k::freeform, "", "add", false, false},
    {"mask-type", k::keyword_only, "luminance alpha", "luminance", false, false},
    // CSS Masking 1 §8: a border image used as a mask - border-image's
    // grammars (declaration.cpp), a nothing-sliced initial, and a mode.
    {"mask-border", k::freeform, "", "none", false, false, true},
    {"mask-border-source", k::freeform, "", "none", false, false},
    {"mask-border-slice", k::number_percentage, "", "0", false, true},
    {"mask-border-width", k::number_length_percentage, "auto", "auto", false, true},
    {"mask-border-outset", k::number_length, "", "0", false, true},
    {"mask-border-repeat", k::freeform, "", "stretch", false, false},
    {"mask-border-mode", k::keyword_only, "luminance alpha", "alpha", false, false},
    {"shape-outside", k::freeform, "", "none", false, false},
    {"shape-image-threshold", k::number_percentage, "", "0", false, false},
    {"offset", k::freeform, "", "normal", false, false, true},
    {"offset-anchor", k::position, "auto", "auto", false, false},
    {"offset-position", k::position, "normal auto", "normal", false, false},
    {"offset-rotate", k::freeform, "", "auto", false, false},
    // --- CSS Grid, Box Alignment ----------------------------------------------
    {"grid", k::freeform, "", "none", false, false, true},
    {"grid-template", k::freeform, "", "none", false, false, true},
    {"grid-template-columns", k::freeform, "", "none", false, false},
    {"grid-template-rows", k::freeform, "", "none", false, false},
    {"grid-template-areas", k::freeform, "", "none", false, false},
    {"grid-auto-columns", k::freeform, "", "auto", false, false},
    {"grid-auto-rows", k::freeform, "", "auto", false, false},
    {"grid-auto-flow", k::freeform, "", "row", false, false},
    {"grid-area", k::freeform, "", "auto", false, false, true},
    {"grid-row", k::freeform, "", "auto", false, false, true},
    {"grid-column", k::freeform, "", "auto", false, false, true},
    {"grid-row-start", k::freeform, "", "auto", false, false},
    {"grid-row-end", k::freeform, "", "auto", false, false},
    {"grid-column-start", k::freeform, "", "auto", false, false},
    {"grid-column-end", k::freeform, "", "auto", false, false},
    {"grid-gap", k::freeform, "", "normal", false, false, true},
    {"grid-row-gap", k::length_percentage, "normal", "normal", false, true},
    {"grid-column-gap", k::length_percentage, "normal", "normal", false, true},
    {"justify-items", k::freeform, "", "legacy", false, false},
    {"justify-self", k::freeform, "", "auto", false, false},
    {"place-content", k::freeform, "", "normal", false, false, true},
    {"place-items", k::freeform, "", "legacy", false, false, true},
    {"place-self", k::freeform, "", "auto", false, false, true},
    // --- CSS Multi-column, Fragmentation, Overflow ---------------------------
    {"columns", k::freeform, "", "auto", false, false, true},
    {"column-count", k::integer, "auto", "auto", false, true},
    {"column-width", k::length, "auto", "auto", false, true},
    {"column-height", k::length, "auto", "auto", false, true},
    {"column-wrap", k::keyword_only, "auto wrap nowrap", "auto", false, false},
    {"column-fill", k::keyword_only, "auto balance balance-all", "balance", false, false},
    // CSS Gap Decorations 1: a rule in every gap, columns and rows alike. The
    // width, style and colour are LISTS - `10px, repeat(auto, thin)` - whose
    // grammar is gaps.cpp's; `rule` and the other `rule-*` shorthands set
    // the column and the row property together.
    {"column-rule", k::freeform, "", "medium none currentcolor", false, false, true},
    {"column-rule-width", k::freeform, "", "medium", false, true},
    {"column-rule-style", k::freeform, "", "none", false, false},
    {"column-rule-color", k::freeform, "", "currentcolor", false, false},
    {"row-rule", k::freeform, "", "medium none currentcolor", false, false, true},
    {"row-rule-width", k::freeform, "", "medium", false, true},
    {"row-rule-style", k::freeform, "", "none", false, false},
    {"row-rule-color", k::freeform, "", "currentcolor", false, false},
    {"rule", k::freeform, "", "medium none currentcolor", false, false, true},
    {"rule-width", k::freeform, "", "medium", false, false, true},
    {"rule-style", k::freeform, "", "none", false, false, true},
    {"rule-color", k::freeform, "", "currentcolor", false, false, true},
    {"column-rule-break", k::keyword_only, "none normal intersection", "normal", false, false},
    {"row-rule-break", k::keyword_only, "none normal intersection", "normal", false, false},
    {"rule-break", k::freeform, "", "normal", false, false, true},
    {"column-rule-visibility-items", k::keyword_only, "all around between normal", "normal", false,
     false},
    {"row-rule-visibility-items", k::keyword_only, "all around between normal", "normal", false,
     false},
    {"rule-visibility-items", k::freeform, "", "normal", false, false, true},
    {"rule-overlap", k::keyword_only, "row-over-column column-over-row", "row-over-column", false,
     false},
    // Where a rule starts and ends: at a cap (the edge of the container) and
    // at a junction (a crossing gap), each end `<length-percentage> |
    // overlap-join`, with a shorthand for every grouping of the eight.
    {"column-rule-inset-cap-start", k::length_percentage, "overlap-join", "0px", false, false},
    {"column-rule-inset-cap-end", k::length_percentage, "overlap-join", "0px", false, false},
    {"column-rule-inset-junction-start", k::length_percentage, "overlap-join", "0px", false, false},
    {"column-rule-inset-junction-end", k::length_percentage, "overlap-join", "0px", false, false},
    {"row-rule-inset-cap-start", k::length_percentage, "overlap-join", "0px", false, false},
    {"row-rule-inset-cap-end", k::length_percentage, "overlap-join", "0px", false, false},
    {"row-rule-inset-junction-start", k::length_percentage, "overlap-join", "0px", false, false},
    {"row-rule-inset-junction-end", k::length_percentage, "overlap-join", "0px", false, false},
    {"column-rule-inset", k::freeform, "", "0px", false, false, true},
    {"row-rule-inset", k::freeform, "", "0px", false, false, true},
    {"rule-inset", k::freeform, "", "0px", false, false, true},
    {"column-rule-inset-cap", k::freeform, "", "0px", false, false, true},
    {"row-rule-inset-cap", k::freeform, "", "0px", false, false, true},
    {"rule-inset-cap", k::freeform, "", "0px", false, false, true},
    {"column-rule-inset-junction", k::freeform, "", "0px", false, false, true},
    {"row-rule-inset-junction", k::freeform, "", "0px", false, false, true},
    {"rule-inset-junction", k::freeform, "", "0px", false, false, true},
    {"column-rule-inset-start", k::freeform, "", "0px", false, false, true},
    {"column-rule-inset-end", k::freeform, "", "0px", false, false, true},
    {"row-rule-inset-start", k::freeform, "", "0px", false, false, true},
    {"row-rule-inset-end", k::freeform, "", "0px", false, false, true},
    {"rule-inset-start", k::freeform, "", "0px", false, false, true},
    {"rule-inset-end", k::freeform, "", "0px", false, false, true},
    {"break-after", k::keyword_only,
     "auto avoid always all avoid-page page left right recto verso avoid-column column "
     "avoid-region region",
     "auto", false, false},
    {"break-before", k::keyword_only,
     "auto avoid always all avoid-page page left right recto verso avoid-column column "
     "avoid-region region",
     "auto", false, false},
    {"break-inside", k::keyword_only, "auto avoid avoid-page avoid-column avoid-region", "auto",
     false, false},
    {"box-decoration-break", k::keyword_only, "slice clone", "slice", false, false},
    {"page", k::freeform, "", "auto", false, false},
    {"overflow-block", k::keyword_only, "visible hidden clip scroll auto", "visible", false, false},
    {"overflow-inline", k::keyword_only, "visible hidden clip scroll auto", "visible", false,
     false},
    {"overflow-anchor", k::keyword_only, "auto none", "auto", false, false},
    {"overflow-clip-margin", k::freeform, "", "0px", false, true}, // declaration.cpp
    {"block-ellipsis", k::freeform, "", "no-ellipsis", true, false},
    // Two shorthands of max-lines, block-ellipsis and continue (CSS Overflow 4
    // §5.1): the legacy one sets the same three, so it reads back as the other.
    {"line-clamp", k::freeform, "", "none", false, false, true},
    {"-webkit-line-clamp", k::freeform, "", "none", false, false, true},
    {"continue", k::keyword_only, "normal discard collapse -webkit-legacy", "normal", false, false},
    {"scrollbar-gutter", k::freeform, "", "auto", false, false},
    {"scrollbar-width", k::keyword_only, "auto thin none", "auto", false, false},
    {"scrollbar-color", k::freeform, "", "auto", true, false},
    {"scroll-behavior", k::keyword_only, "auto smooth", "auto", false, false},
    {"scroll-marker-group", k::freeform, "", "none", false, false}, // declaration.cpp
    {"scroll-target-group", k::keyword_only, "none auto", "none", false, false},
    // --- CSS Scroll Snap --------------------------------------------------------
    {"scroll-snap-type", k::freeform, "", "none", false, false},
    {"scroll-snap-align", k::freeform, "", "none", false, false},
    {"scroll-snap-stop", k::keyword_only, "normal always", "normal", false, false},
    {"scroll-margin", k::freeform, "", "0px", false, false, true},
    {"scroll-margin-block", k::freeform, "", "0px", false, false, true},
    {"scroll-margin-inline", k::freeform, "", "0px", false, false, true},
    {"scroll-margin-top", k::length, "", "0px", false, false},
    {"scroll-margin-right", k::length, "", "0px", false, false},
    {"scroll-margin-bottom", k::length, "", "0px", false, false},
    {"scroll-margin-left", k::length, "", "0px", false, false},
    {"scroll-margin-block-start", k::length, "", "0px", false, false},
    {"scroll-margin-block-end", k::length, "", "0px", false, false},
    {"scroll-margin-inline-start", k::length, "", "0px", false, false},
    {"scroll-margin-inline-end", k::length, "", "0px", false, false},
    {"scroll-padding", k::freeform, "", "auto", false, false, true},
    {"scroll-padding-block", k::freeform, "", "auto", false, false, true},
    {"scroll-padding-inline", k::freeform, "", "auto", false, false, true},
    {"scroll-padding-block-start", k::length_percentage, "auto", "auto", false, true},
    {"scroll-padding-block-end", k::length_percentage, "auto", "auto", false, true},
    {"scroll-padding-inline-start", k::length_percentage, "auto", "auto", false, true},
    {"scroll-padding-inline-end", k::length_percentage, "auto", "auto", false, true},
    // CSS Overflow 5 §scroll-axis-lock: `auto | none`, whether a scroll
    // gesture sticks to the axis it began on. Nothing scrolls by gesture
    // here, so it is a value and no behaviour (scroll-axis-lock-computed).
    {"scroll-axis-lock", k::keyword_only, "auto none", "auto", false, false},
    {"overscroll-behavior", k::freeform, "", "auto", false, false, true},
    {"overscroll-behavior-x", k::keyword_only, "contain none auto chain", "auto", false, false},
    {"overscroll-behavior-y", k::keyword_only, "contain none auto chain", "auto", false, false},
    {"overscroll-behavior-block", k::keyword_only, "contain none auto chain", "auto", false, false},
    {"overscroll-behavior-inline", k::keyword_only, "contain none auto chain", "auto", false,
     false},
    // --- CSS Logical, Sizing, Box ----------------------------------------------
    {"min-inline-size", k::length_percentage, "auto min-content max-content fit-content stretch",
     "0px", false, true},
    {"min-block-size", k::length_percentage, "auto min-content max-content fit-content stretch",
     "0px", false, true},
    {"max-inline-size", k::length_percentage, "none min-content max-content fit-content stretch",
     "none", false, true},
    {"max-block-size", k::length_percentage, "none min-content max-content fit-content stretch",
     "none", false, true},
    {"border-block", k::freeform, "", "medium none currentcolor", false, false, true},
    {"border-block-start", k::freeform, "", "medium none currentcolor", false, false, true},
    {"border-block-end", k::freeform, "", "medium none currentcolor", false, false, true},
    {"border-inline", k::freeform, "", "medium none currentcolor", false, false, true},
    {"border-inline-start", k::freeform, "", "medium none currentcolor", false, false, true},
    {"border-inline-end", k::freeform, "", "medium none currentcolor", false, false, true},
    {"border-block-color", k::freeform, "", "currentcolor", false, false, true},
    {"border-block-style", k::freeform, "", "none", false, false, true},
    {"border-block-width", k::freeform, "", "medium", false, false, true},
    {"border-inline-color", k::freeform, "", "currentcolor", false, false, true},
    {"border-inline-style", k::freeform, "", "none", false, false, true},
    {"border-inline-width", k::freeform, "", "medium", false, false, true},
    {"border-start-start-radius", k::freeform, "", "0px", false, true},
    {"border-start-end-radius", k::freeform, "", "0px", false, true},
    {"border-end-start-radius", k::freeform, "", "0px", false, true},
    {"border-end-end-radius", k::freeform, "", "0px", false, true},
    {"-webkit-border-radius", k::freeform, "", "0px", false, true, true},
    {"-webkit-border-top-left-radius", k::freeform, "", "0px", false, true},
    {"-webkit-border-top-right-radius", k::freeform, "", "0px", false, true},
    {"-webkit-border-bottom-right-radius", k::freeform, "", "0px", false, true},
    {"-webkit-border-bottom-left-radius", k::freeform, "", "0px", false, true},
    {"margin-trim", k::freeform, "", "none", false, false}, // keywords.cpp
    {"contain", k::freeform, "", "none", false, false},
    // CSS Conditional 5 §4: the container queries' two longhands and their
    // shorthand. `CSS.supports("container-type:size")` is how every
    // css-conditional container-query file asks whether they exist (110 of
    // them were HARNESS_ERROR while these were unknown); the engine has
    // evaluated `@container` against them since before they were in the
    // table. Grammars in keywords.cpp, the split in shorthands.cpp.
    {"container-type", k::freeform, "", "normal", false, false},
    {"container-name", k::freeform, "", "none", false, false},
    {"container", k::freeform, "", "none", false, false, true},
    {"will-change", k::freeform, "", "auto", false, false},
    {"field-sizing", k::keyword_only, "fixed content", "fixed", false, false},
    {"interactivity", k::keyword_only, "auto inert", "auto", true, false},
    {"appearance", k::freeform, "", "none", false, false},
    // --- CSS Fonts 4 ---------------------------------------------------------------
    {"font-kerning", k::keyword_only, "auto normal none", "auto", true, false},
    {"font-language-override", k::freeform, "", "normal", true, false},
    {"font-optical-sizing", k::keyword_only, "auto none", "auto", true, false},
    {"font-synthesis", k::freeform, "", "weight style small-caps position", true, false, true},
    {"font-synthesis-weight", k::keyword_only, "auto none", "auto", true, false},
    {"font-synthesis-style", k::keyword_only, "auto none oblique-only", "auto", true, false},
    {"font-synthesis-small-caps", k::keyword_only, "auto none", "auto", true, false},
    {"font-synthesis-position", k::keyword_only, "auto none", "auto", true, false},
    {"font-variant-caps", k::keyword_only,
     "normal small-caps all-small-caps petite-caps all-petite-caps unicase titling-caps", "normal",
     true, false},
    {"font-variant-east-asian", k::freeform, "", "normal", true, false},
    {"font-variant-emoji", k::keyword_only, "normal text emoji unicode", "normal", true, false},
    {"font-variant-ligatures", k::freeform, "", "normal", true, false},
    {"font-variant-numeric", k::freeform, "", "normal", true, false},
    {"font-variant-position", k::keyword_only, "normal sub super", "normal", true, false},
    // --- CSS Text 4, Text Decoration 4, Inline, Ruby, Writing Modes ----------------
    {"text-align-all", k::keyword_only, "start end left right center justify match-parent", "start",
     true, false},
    {"text-align-last", k::keyword_only, "auto start end left right center justify match-parent",
     "auto", true, false},
    {"text-justify", k::keyword_only, "auto none inter-word inter-character distribute", "auto",
     true, false},
    {"text-wrap", k::freeform, "", "wrap", true, false, true},
    {"text-wrap-mode", k::keyword_only, "wrap nowrap", "wrap", true, false},
    {"text-wrap-style", k::keyword_only, "auto balance stable pretty avoid-orphans", "auto", true,
     false},
    {"white-space-collapse", k::keyword_only,
     "collapse preserve preserve-breaks preserve-spaces break-spaces", "collapse", true, false},
    // `none | [ discard-before || discard-after || discard-inner ]`, CSS Text
    // 4 §4.5; the shorthand's split checks the words, nothing trims with it.
    {"white-space-trim", k::freeform, "", "none", false, false},
    {"word-wrap", k::keyword_only, "normal break-word anywhere", "normal", true, false},
    {"line-break", k::keyword_only, "auto loose normal strict anywhere", "auto", true, false},
    {"hyphens", k::keyword_only, "none manual auto", "manual", true, false},
    // CSS Text 4 §8.2 and §7.4: turning spaces into the script's word
    // separator, and aligning a group of lines as one.
    {"word-space-transform", k::freeform, "", "none", true, false}, // keywords.cpp
    {"text-group-align", k::keyword_only, "none start end left right center", "none", false, false},
    {"hyphenate-character", k::freeform, "", "auto", true, false},
    {"hanging-punctuation", k::freeform, "", "none", true, false},
    {"text-autospace", k::freeform, "", "normal", true, false},
    {"text-spacing-trim", k::keyword_only,
     "normal auto space-all trim-both trim-all trim-start space-first", "normal", true, false},
    // CSS Text 4 §8.1's shorthand of the two, and §10's text-fit.
    {"text-spacing", k::freeform, "", "normal", true, false, true},
    {"text-fit", k::freeform, "", "none", true, false}, // keywords.cpp; inherited, §10
    {"text-size-adjust", k::percentage, "auto none", "auto", true, true},
    {"text-combine-upright", k::freeform, "", "none", true, false},
    {"text-orientation", k::keyword_only, "mixed upright sideways", "mixed", true, false},
    {"text-decoration-skip-ink", k::keyword_only, "auto none all", "auto", true, false},
    // CSS Text Decoration 4 §2.4-2.5: the line's thickness and the underline's
    // offset, each a keyword or a <length-percentage> of 1em.
    {"text-decoration-thickness", k::length_percentage, "auto from-font", "auto", false, false},
    {"text-underline-offset", k::length_percentage, "auto", "auto", true, false},
    {"text-decoration-inset", k::freeform, "", "0px", false, false},            // declaration.cpp
    {"text-decoration-skip-spaces", k::freeform, "", "start end", true, false}, // keywords.cpp
    {"text-underline-position", k::freeform, "", "auto", true, false},          // keywords.cpp
    {"text-emphasis", k::freeform, "", "none currentcolor", true, false, true},
    {"text-emphasis-style", k::freeform, "", "none", true, false},
    {"text-emphasis-color", k::color, "", "currentcolor", true, false},
    {"text-emphasis-position", k::freeform, "", "auto", true, false},
    {"initial-letters", k::freeform, "", "normal", false, false},
    // CSS Inline 3's current name for it, the one engines ship.
    {"initial-letter", k::freeform, "", "normal", false, false},
    {"initial-letters-align", k::freeform, "", "alphabetic", true, false},
    {"initial-letters-wrap", k::freeform, "", "none", true, false},
    {"alignment-baseline", k::keyword_only,
     "baseline text-bottom alphabetic ideographic middle central mathematical text-top hanging",
     "baseline", false, false},
    {"dominant-baseline", k::keyword_only,
     "auto text-bottom alphabetic ideographic middle central mathematical hanging text-top", "auto",
     true, false},
    {"baseline-shift", k::length_percentage, "sub super top center bottom", "0px", false, false},
    {"ruby-align", k::keyword_only, "start center space-between space-around", "space-around", true,
     false},
    {"ruby-merge", k::keyword_only, "separate merge auto", "separate", true, false},
    {"ruby-overhang", k::freeform, "", "auto", true, false}, // declaration.cpp
    {"ruby-position", k::freeform, "", "alternate", true, false},
    {"quotes", k::freeform, "", "auto", true, false},
    // --- CSS Lists, Generated Content -------------------------------------------------
    {"counter-increment", k::freeform, "", "none", false, false},
    {"counter-reset", k::freeform, "", "none", false, false},
    {"counter-set", k::freeform, "", "none", false, false},
    {"bookmark-level", k::freeform, "", "none", false, false},
    {"bookmark-state", k::keyword_only, "open closed", "open", false, false},
    // --- CSS Color Adjust, Images, UI ---------------------------------------------------
    {"color-scheme", k::freeform, "", "normal", true, false},
    {"color-adjust", k::keyword_only, "economy exact", "economy", true, false},
    {"print-color-adjust", k::keyword_only, "economy exact", "economy", true, false},
    {"forced-color-adjust", k::keyword_only, "auto none preserve-parent-color", "auto", true,
     false},
    {"dynamic-range-limit", k::freeform, "", "no-limit", true, false},
    {"image-orientation", k::keyword_only, "from-image none", "from-image", true, false},
    {"image-rendering", k::keyword_only, "auto smooth high-quality pixelated crisp-edges", "auto",
     true, false},
    {"image-resolution", k::freeform, "", "1dppx", true, false},
    {"caret-shape", k::keyword_only, "auto bar block underscore", "auto", true, false},
    // CSS UI 4 §6.2.2: whether the UA blinks the caret or the page animates it.
    {"caret-animation", k::keyword_only, "auto manual", "auto", true, false},
    {"nav-up", k::freeform, "", "auto", false, false},
    {"nav-down", k::freeform, "", "auto", false, false},
    {"nav-left", k::freeform, "", "auto", false, false},
    {"nav-right", k::freeform, "", "auto", false, false},
    {"link-parameters", k::freeform, "", "none", false, false},
    // --- CSS Anchor Positioning, View Transitions, Exclusions ---------------------------
    {"anchor-scope", k::freeform, "", "none", false, false},
    {"position-try", k::freeform, "", "none", false, false, true},
    {"position-try-fallbacks", k::freeform, "", "none", false, false},
    {"position-try-order", k::keyword_only,
     "normal most-width most-height most-block-size most-inline-size", "normal", false, false},
    {"position-visibility", k::freeform, "", "always", false, false},
    {"view-transition-class", k::freeform, "", "none", false, false},
    {"view-transition-group", k::freeform, "", "normal", false, false},
    {"wrap-flow", k::keyword_only, "auto both start end minimum maximum clear", "auto", false,
     false},
    {"wrap-through", k::keyword_only, "wrap none", "wrap", false, false},
    {"initial-sizing", k::freeform, "", "normal", false, false},
    // --- CSS Rhythm -----------------------------------------------------------------------
    {"block-step", k::freeform, "", "none", false, false, true},
    {"block-step-size", k::length, "none", "none", false, true},
    {"block-step-insert", k::keyword_only, "margin-box padding-box content-box", "margin-box",
     false, false},
    {"block-step-align", k::keyword_only, "auto center start end", "auto", false, false},
    {"block-step-round", k::keyword_only, "up down nearest", "up", false, false},
};

} // namespace

namespace detail {

std::span<const property_syntax> module_properties() noexcept {
    return std::span<const property_syntax>{modules, std::size(modules)};
}

} // namespace detail

} // namespace ctbrowser::style::css
