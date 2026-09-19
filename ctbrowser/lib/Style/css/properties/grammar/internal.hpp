#pragma once

// The value grammar: the unit and function lists, the token scan, the
// substitution and math-function rules, `<position>`, and one typed component
// matched and serialised.

#include "../internal.hpp"

namespace ctbrowser::style::css::grammar_detail {

using namespace detail;

// §6.1 of CSS Values 4, plus the viewport-relative families of §6.1.2. The
// engine RESOLVES only a subset of these (see layout/values.hpp - `rem` has a
// hardcoded 16px basis and several fall through to pixels), but resolving and
// PARSING are different questions and this one is about syntax: `width: 3ic`
// is a valid declaration whether or not this engine can place the box.
inline constexpr std::array<std::string_view, 39> length_units{
    "em",  "rem", "ex",    "rex",   "ch",  "rch", "cap",  "rcap", "ic",    "ric",
    "lh",  "rlh", "vw",    "vh",    "vi",  "vb",  "vmin", "vmax", "svw",   "svh",
    "svi", "svb", "svmin", "svmax", "lvw", "lvh", "lvi",  "lvb",  "lvmin", "lvmax",
    "dvw", "dvh", "dvmin", "dvmax", "cm",  "mm",  "q",    "in",   "pt"};

// The tail of the same list. Split only because the array above is at its
// declared size; `is_length_unit` consults both, so the seam is invisible.
inline constexpr std::array<std::string_view, 4> more_length_units{"pc", "px", "dvi", "dvb"};

inline constexpr std::array<std::string_view, 4> angle_units{"deg", "grad", "rad", "turn"};

inline constexpr std::array<std::string_view, 2> time_units{"s", "ms"};

// THE MATH FUNCTIONS, CSS Values 4 §10. A value that is one of these applied to
// the whole declaration is accepted without evaluating it: `calc/` already
// owns the evaluation and answers `unresolved` for the cases that have no
// answer until layout (`min(10px, 5%)`), so refusing here would condemn a
// declaration the cascade deliberately keeps.
inline constexpr std::array<std::string_view, 27> math_functions{
    "calc",      "min",      "max",           "clamp",         "round",  "mod",     "rem",
    "abs",       "sign",     "sin",           "cos",           "tan",    "asin",    "acos",
    "atan",      "atan2",    "pow",           "sqrt",          "hypot",  "log",     "exp",
    "calc-size", "progress", "sibling-index", "sibling-count", "random", "calc-mix"};

// A value containing one of these is valid by construction: what it means is
// not known until substitution, so the declaration survives parsing with its
// tokens intact. CSS Variables 1 §3.
// A value holding one of these is valid by construction: what it means is not
// known until substitution, so the declaration survives parsing with its tokens
// intact. CSS Variables 1 §3.
//
// `random-item()`, `inherit()` and `ident()` are three more of them, and naming
// them is what makes `width: random-item(auto, 1px, 2px, 3px)` and
// `left: inherit(--x)` declarations rather than lengths the grammar could not
// read. CSS Values 5 calls all of these ARBITRARY SUBSTITUTION FUNCTIONS: their
// specified value is their arguments and what those arguments mean is decided
// later. `substitution_grammar_ok` below is the part that IS decided now.
//
// EVERY ONE OF THEM IS PERFORMED - css/substitute.cpp runs all six - so
// `CSS.supports` says yes to all six. `attr()` and `random-item()` were kept
// off the performed list from before the engine could substitute them, and
// random-item-computed guards twenty-three assertions on the answer.
inline constexpr std::array<std::string_view, 6> substitution_functions{
    "var", "env", "attr", "random-item", "inherit", "ident"};

inline constexpr std::array<std::string_view, 6> performed_substitutions{
    "var", "env", "attr", "random-item", "inherit", "ident"};

// THE VALUE FUNCTIONS THIS ENGINE IMPLEMENTS, beside the math ones and the
// substitutions. `CSS.supports` is "would this declaration be dropped", and a
// value calling a function nothing here can evaluate WOULD be - so answering
// true for `type(*)` or `image-set()` is a lie: five `css/css-values` files
// guard their assertions on `CSS.supports`.
//
// AN ALLOW-LIST rather than a list of what is missing, because the missing set
// is the whole of CSS Values 5 and grows every month while this one grows only
// when the engine does. It costs a false NEGATIVE - a page asking about a
// function the engine handles but this list has not caught up with - which makes
// a test skip rather than lie.
inline constexpr std::array<std::string_view, 41> value_functions{"cross-origin",
                                                                  "integrity",
                                                                  "referrer-policy",
                                                                  "rgb",
                                                                  "rgba",
                                                                  "hsl",
                                                                  "hsla",
                                                                  "hwb",
                                                                  "color",
                                                                  "url",
                                                                  "src",
                                                                  "linear-gradient",
                                                                  "radial-gradient",
                                                                  "conic-gradient",
                                                                  "translate",
                                                                  "translatex",
                                                                  "translatey",
                                                                  "translate3d",
                                                                  "rotate",
                                                                  "scale",
                                                                  "scalex",
                                                                  "scaley",
                                                                  "skew",
                                                                  "matrix",
                                                                  "matrix3d",
                                                                  "perspective",
                                                                  "cubic-bezier",
                                                                  "steps",
                                                                  "counter",
                                                                  "rect",
                                                                  "format",
                                                                  "lab",
                                                                  "lch",
                                                                  "oklab",
                                                                  "oklch",
                                                                  "color-mix",
                                                                  "light-dark",
                                                                  "alpha",
                                                                  "contrast-color",
                                                                  "color-layers",
                                                                  "device-cmyk"};

[[nodiscard]] bool is_length_unit(std::string_view unit);

[[nodiscard]] std::string_view function_name(const token_stream & ts, const css_token & t);

[[nodiscard]] std::string number_text(double value);

[[nodiscard]] bool random_item_arguments_ok(const token_stream & ts, std::size_t open);

// --- `<position>` --------------------------------------------------------
//
// CSS Values 5 §position, and the shape of it is THREE FORMS AND NOT FOUR:
//
//   <position-one>  = [ <h-side> | <v-side> | center | <length-percentage> ]
//   <position-two>  = [ <h-side> | center | <lp> ] [ <v-side> | center | <lp> ]
//                   | [ <h-side> | center ] && [ <v-side> | center ]
//   <position-four> = [ <h-side> <lp> ] && [ <v-side> <lp> ]
//
// THE THREE-VALUE FORM IS GONE. Backgrounds 3 still allows `left 4px top` for
// `background-position`, and level 5's `<position>` does not - so
// `object-position: left 4px top` is invalid where the same text is a valid
// `background-position`. Eight of `position/position-invalid.tentative`'s
// twenty-one assertions are three-value forms and turn on nothing else.
//
// `x-start`/`x-end` are horizontal and `y-start`/`y-end` vertical, which is the
// whole of what level 5 added beside removing that form.
enum class position_axis : std::uint8_t {
    none,       // not a position keyword at all
    horizontal, // left, right, x-start, x-end
    vertical,   // top, bottom, y-start, y-end
    center,     // fits either half
    offset,     // a <length-percentage>
};

[[nodiscard]] position_axis position_axis_of(const token_stream & ts, const css_token & t,
                                             std::string & serialized);

} // namespace ctbrowser::style::css::grammar_detail
