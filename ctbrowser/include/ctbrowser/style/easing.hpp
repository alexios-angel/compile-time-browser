//===- easing.hpp - Easing functions ----------------------------*- C++ -*-===//
//
// Animation timing functions (CSS Easing), and the interpolation and
// composition of computed values that transitions and animations use.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctbrowser/style/css/calc.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ctbrowser::style {

/// An owned easing function, linear by default. Parsing retains no source text.
class easing {
public:
    easing() = default;

    /// Maps input progress to output progress. Progress may lie outside
    /// [0, 1]. At a step boundary, `before` selects the value immediately
    /// before the jump; it does not affect other shapes.
    [[nodiscard]] double operator()(double input, bool before) const;

private:
    enum class kind : std::uint8_t {
        linear,
        bezier,
        steps
    };
    kind shape = kind::linear;
    double x1 = 0, y1 = 0, x2 = 1, y2 = 1;
    int steps = 1;
    enum class jump : std::uint8_t {
        start,
        end,
        none,
        both
    };
    jump position = jump::end;

    [[nodiscard]] double bezier(double input) const;
    [[nodiscard]] double step(double input, bool before) const;
    friend std::optional<easing> parse_easing(std::string_view text);
};

/// Parses CSS easing keywords, cubic-bezier() and steps(); invalid text returns
/// nullopt.
[[nodiscard]] std::optional<easing> parse_easing(std::string_view text);

/// Interpolates between two values by the property's animation type (CSS
/// Values 4 §4 "combining values"): a number, length, percentage or calc()
/// mixes arithmetically in the supplied length context (extrapolating, then
/// clamped to the computed and property bounds); a colour mixes premultiplied
/// in sRGB (CSS Color 4 §17); a shadow list in its computed shape, padded with
/// transparent zero shadows (CSS Backgrounds 3 §7.2); and any list of those
/// (comma-separated, then space-separated) item by item. A pair that is none
/// of these switches at progress 0.5. The returned text owns its bytes.
[[nodiscard]] std::string interpolate_text(std::string_view property, std::string_view from,
                                           std::string_view to, double progress,
                                           const css::length_context & context);

/// Whether interpolate_text() would interpolate the pair rather than switch
/// it, which decides whether a transition starts (CSS Transitions 1 §3). The
/// context carries the element's `interpolate-size`.
[[nodiscard]] bool interpolable_text(std::string_view property, std::string_view from,
                                     std::string_view to, const css::length_context & context = {});

/// Replaces `currentcolor` in a value (whole or as a list item) with the
/// element's colour text. A colour interpolates as the colour it resolves to
/// (CSS Color 4 §7.1), and `text-shadow: currentcolor 1px 1px` can pair with a
/// colour only once it is one.
[[nodiscard]] std::string with_currentcolor(std::string_view value, std::string_view color);

/// The composite operations of Web Animations 1 §4.5.1. composite_text() adds
/// or accumulates `value` onto `underlying` by the property's animation type
/// (CSS Values 4 §4.3-4.4: numbers and lengths sum, colours sum premultiplied,
/// a shadow or transform list is appended, a list of the rest adds item by
/// item). A pair the type cannot add gives `value` alone, as the specification
/// says.
enum class composite_op : std::uint8_t {
    replace,
    add,
    accumulate
};
[[nodiscard]] std::string composite_text(std::string_view property, std::string_view underlying,
                                         std::string_view value, composite_op op,
                                         const css::length_context & context);

} // namespace ctbrowser::style
