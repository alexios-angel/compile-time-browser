#pragma once

#include <ctbrowser/style/css/calc.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ctbrowser::style {

// An owned easing value, linear by default. Parsing retains no source text.
class easing {
public:
    easing() = default;

    // Progress may lie outside [0, 1]. At a step boundary, before selects
    // the value immediately before the jump; it does not affect other shapes.
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

// CSS easing keywords, cubic-bezier() and steps(); invalid text returns nullopt.
[[nodiscard]] std::optional<easing> parse_easing(std::string_view text);

// ONE VALUE BETWEEN TWO, CSS Values 4 §4 "combining values" by the property's
// animation type: a number, length, percentage or calc() mix arithmetically
// in the supplied length context (extrapolating, then clamped to the computed
// and property bounds), a colour premultiplied in sRGB (CSS Color 4 §17), a
// shadow list in its computed shape padded with transparent zero shadows (CSS
// Backgrounds 3 §7.2), and any LIST of those - comma-separated, then
// space-separated - item by item. A pair that is none of these switches at
// progress 0.5. The returned text owns its bytes; nothing is retained.
[[nodiscard]] std::string interpolate_text(std::string_view property, std::string_view from,
                                           std::string_view to, double progress,
                                           const css::length_context & context);

// Whether `interpolate_text` would interpolate the pair rather than switch it -
// which is what decides whether a transition starts (CSS Transitions 1 §3).
[[nodiscard]] bool interpolable_text(std::string_view property, std::string_view from,
                                     std::string_view to);

// `currentcolor` in a value replaced by the element's colour text, whole or
// as a list item: a colour interpolates as the colour it resolves to (CSS
// Color 4 §7.1), and `text-shadow: currentcolor 1px 1px` pairs with a colour
// only once it is one.
[[nodiscard]] std::string with_currentcolor(std::string_view value, std::string_view color);

// THE COMPOSITE OPERATIONS of Web Animations 1 §4.5.1: `value` added to, or
// accumulated onto, `underlying` by the property's animation type (CSS Values
// 4 §4.3-4.4: numbers and lengths sum, colours sum premultiplied, a shadow or
// transform list is appended, a list of the rest adds item by item). A pair
// the type cannot add is `value` alone, as the specification says.
enum class composite_op : std::uint8_t {
    replace,
    add,
    accumulate
};
[[nodiscard]] std::string composite_text(std::string_view property, std::string_view underlying,
                                         std::string_view value, composite_op op,
                                         const css::length_context & context);

} // namespace ctbrowser::style
