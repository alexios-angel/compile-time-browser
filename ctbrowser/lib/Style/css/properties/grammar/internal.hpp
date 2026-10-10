//===- grammar/internal.hpp - Grammar internals --------------- -*- C++ -*-===//
//
// The value grammar: the unit and function lists, the token scan, the
// substitution and math-function rules, `<position>`, and one typed component
// matched and serialised.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../internal.hpp"

namespace ctbrowser::style::css::grammar_detail {

using namespace detail;

/// The length units of CSS Values 4 §6.1, plus the viewport-relative families
/// of §6.1.2. The engine resolves only a subset of these (see
/// layout/values.hpp: `rem` has a hardcoded 16px basis and several fall
/// through to pixels), but this list is about syntax: `width: 3ic` is a valid
/// declaration whether or not this engine can place the box.
inline constexpr std::array<std::string_view, 39> length_units{
    "em",  "rem", "ex",    "rex",   "ch",  "rch", "cap",  "rcap", "ic",    "ric",
    "lh",  "rlh", "vw",    "vh",    "vi",  "vb",  "vmin", "vmax", "svw",   "svh",
    "svi", "svb", "svmin", "svmax", "lvw", "lvh", "lvi",  "lvb",  "lvmin", "lvmax",
    "dvw", "dvh", "dvmin", "dvmax", "cm",  "mm",  "q",    "in",   "pt"};

/// The rest of the length units; `is_length_unit` consults both arrays. The
/// container query units are CSS Conditional 5 §4's.
inline constexpr std::array<std::string_view, 10> more_length_units{
    "pc", "px", "dvi", "dvb", "cqw", "cqh", "cqi", "cqb", "cqmin", "cqmax"};

/// The `<angle>` units.
inline constexpr std::array<std::string_view, 4> angle_units{"deg", "grad", "rad", "turn"};

/// The `<time>` units.
inline constexpr std::array<std::string_view, 2> time_units{"s", "ms"};

/// The math functions, CSS Values 4 §10. Their evaluation belongs to `calc/`,
/// which answers `unresolved` for cases that have no answer until layout
/// (`min(10px, 5%)`); such a value is accepted, not refused, because the
/// cascade deliberately keeps it.
inline constexpr std::array<std::string_view, 27> math_functions{
    "calc",      "min",      "max",           "clamp",         "round",  "mod",     "rem",
    "abs",       "sign",     "sin",           "cos",           "tan",    "asin",    "acos",
    "atan",      "atan2",    "pow",           "sqrt",          "hypot",  "log",     "exp",
    "calc-size", "progress", "sibling-index", "sibling-count", "random", "calc-mix"};

/// The arbitrary substitution functions (CSS Values 5). A value holding one of
/// these is valid by construction: what it means is not known until
/// substitution, so the declaration survives parsing with its tokens intact
/// (CSS Variables 1 §3). This is what makes `width: random-item(auto, 1px,
/// 2px)` and `left: inherit(--x)` declarations rather than unreadable
/// lengths. `substitution_grammar_ok` checks the part of their argument
/// grammar that is decided at parse time.
inline constexpr std::array<std::string_view, 6> substitution_functions{
    "var", "env", "attr", "random-item", "inherit", "ident"};

/// The substitution functions the engine actually performs, for which
/// `CSS.supports` answers true. css/substitute.cpp performs all six, so this
/// list matches substitution_functions; a function that is recognised but
/// not performed belongs only in that list.
inline constexpr std::array<std::string_view, 6> performed_substitutions{
    "var", "env", "attr", "random-item", "inherit", "ident"};

/// The value functions this engine implements, besides the math functions and
/// the substitutions. `CSS.supports` asks "would this declaration be
/// dropped", and a value calling a function nothing here can evaluate would
/// be, so answering true for `type(*)` or `image-set()` would be wrong.
///
/// This is an allow-list rather than a list of what is missing, because the
/// missing set grows with every new spec while this one grows only with the
/// engine. Its failure mode is a false negative (a function the engine handles
/// but this list omits), which makes feature detection skip rather than lie.
///
/// `repeat()`, `minmax()` and `fit-content()` belong to grid.cpp's track
/// grammar and, for the last, the sizing properties (declaration.cpp). The
/// filter functions are filter.cpp's and the basic shapes shapes.cpp's.
inline constexpr std::array<std::string_view, 76> value_functions{"cross-origin",
                                                                  "repeat",
                                                                  "minmax",
                                                                  "fit-content",
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
                                                                  "repeating-linear-gradient",
                                                                  "radial-gradient",
                                                                  "repeating-radial-gradient",
                                                                  "conic-gradient",
                                                                  "repeating-conic-gradient",
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
                                                                  "device-cmyk",
                                                                  "skewx",
                                                                  "skewy",
                                                                  "blur",
                                                                  "brightness",
                                                                  "contrast",
                                                                  "drop-shadow",
                                                                  "grayscale",
                                                                  "hue-rotate",
                                                                  "invert",
                                                                  "opacity",
                                                                  "saturate",
                                                                  "sepia",
                                                                  "circle",
                                                                  "ellipse",
                                                                  "inset",
                                                                  "polygon",
                                                                  "path",
                                                                  "xywh",
                                                                  "ray",
                                                                  "shape",
                                                                  "translatez",
                                                                  "scalez",
                                                                  "scale3d",
                                                                  "rotatex",
                                                                  "rotatey",
                                                                  "rotatez",
                                                                  "rotate3d",
                                                                  "counters",
                                                                  "symbols"};

/// Whether `unit` is a length unit, ASCII case-insensitively.
[[nodiscard]] bool is_length_unit(std::string_view unit);

/// The name of function token `t`, without its `(`.
[[nodiscard]] std::string_view function_name(const token_stream & ts, const css_token & t);

/// A number as CSSOM serialises it.
[[nodiscard]] std::string number_text(double value);

/// Checks the argument list of the random-item() whose function token is at
/// `open`.
[[nodiscard]] bool random_item_arguments_ok(const token_stream & ts, std::size_t open);

// --- `<position>` --------------------------------------------------------
//
// CSS Values 5 §position has exactly three forms, not four:
//
//   <position-one>  = [ <h-side> | <v-side> | center | <length-percentage> ]
//   <position-two>  = [ <h-side> | center | <lp> ] [ <v-side> | center | <lp> ]
//                   | [ <h-side> | center ] && [ <v-side> | center ]
//   <position-four> = [ <h-side> <lp> ] && [ <v-side> <lp> ]
//
// There is no three-value form. Backgrounds 3 still allows `left 4px top` for
// `background-position`, but CSS Values 5 `<position>` does not, so
// `object-position: left 4px top` is invalid.
//
// Level 5 also adds `x-start`/`x-end` (horizontal) and `y-start`/`y-end`
// (vertical).

/// Which half of a `<position>` a component can fill.
enum class position_axis : std::uint8_t {
    none,       ///< Not a position component at all.
    horizontal, ///< left, right, x-start, x-end.
    vertical,   ///< top, bottom, y-start, y-end.
    center,     ///< Fits either half.
    offset,     ///< A <length-percentage>.
};

/// Classifies one token as a position component and writes its
/// serialisation to `serialized`.

[[nodiscard]] position_axis position_axis_of(const token_stream & ts, const css_token & t,
                                             std::string & serialized);

} // namespace ctbrowser::style::css::grammar_detail
