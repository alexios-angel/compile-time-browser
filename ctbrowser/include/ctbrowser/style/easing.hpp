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

// Numeric values interpolate and extrapolate in the supplied length context,
// with computed-value and property bounds. Other values switch at progress 0.5.
// The returned text owns its bytes; neither input text nor context is retained.
[[nodiscard]] std::string interpolate_text(std::string_view property, std::string_view from,
                                           std::string_view to, double progress,
                                           const css::length_context & context);

} // namespace ctbrowser::style
