#pragma once

#include <ctbrowser/style/easing.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <ctbrowser/style/css/token.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace ctbrowser::style::easing_detail {

[[nodiscard]] std::vector<std::string_view> split_arguments(std::string_view body);

[[nodiscard]] std::optional<double> number_of(std::string_view text);

[[nodiscard]] std::optional<int> step_count(std::string_view text);

// --- numbers, lengths, percentages and their calc() mixes ---

struct numeric_pair {
    css::calc_result a, b;
};

[[nodiscard]] std::optional<numeric_pair> numeric_of(std::string_view from, std::string_view to,
                                                     const css::length_context & ctx);

[[nodiscard]] css::calc_result mix(const css::calc_result & a, const css::calc_result & b,
                                   double p);

[[nodiscard]] css::calc_result sum(const css::calc_result & a, const css::calc_result & b);

[[nodiscard]] std::string numeric_text(std::string_view property, css::calc_result out);

// --- colours ---

// A colour as sRGB with alpha, premultiplied: CSS Color 4 §17 interpolates
// legacy colours in sRGB with the channels weighted by alpha, so a transparent
// endpoint contributes no hue. Through style's resolver rather than paint's:
// paint holds a channel in eight bits, and an alpha of 0.5 read back as
// 128/255 - which put the midpoint of blue and half-transparent red at 0.753
// rather than 0.75.
struct premultiplied {
    double r, g, b, a;
};

[[nodiscard]] premultiplied premultiply(const css::srgb_color & c);

[[nodiscard]] std::string color_text(const premultiplied & c);

[[nodiscard]] std::string lerp_color(const css::srgb_color & from, const css::srgb_color & to,
                                     double p);

[[nodiscard]] bool legacy_color(std::string_view text);

// sRGB to Oklab (Björn Ottosson's matrices, as CSS Color 4 §17.4 gives them),
// with a channel outside the gamut carried through sign-preserved.
struct oklab {
    double l, a, b;
};

[[nodiscard]] oklab oklab_of(const css::srgb_color & c);

[[nodiscard]] std::string lerp_oklab(const css::srgb_color & from, const css::srgb_color & to,
                                     double p);

[[nodiscard]] std::string add_color(const css::srgb_color & x, const css::srgb_color & y);

[[nodiscard]] bool is_shadow(std::string_view property);

[[nodiscard]] bool appends(std::string_view property);

[[nodiscard]] bool repeatable(std::string_view property);

template <typename T> void repeat_to_match(std::vector<T> & a, std::vector<T> & b) {
    if (a.empty() || b.empty() || a.size() == b.size()) { return; }
    const std::size_t n = std::lcm(a.size(), b.size());
    for (std::size_t i = a.size(); i < n; ++i) { a.push_back(a[i % a.size()]); }
    for (std::size_t i = b.size(); i < n; ++i) { b.push_back(b[i % b.size()]); }
}

[[nodiscard]] std::string computed_shape(std::string_view property, std::string_view text,
                                         const css::length_context & ctx);

[[nodiscard]] std::string blank_shadow(bool box, std::string_view like);

[[nodiscard]] std::vector<std::string_view> comma_items(std::string_view text);

// --- transform lists (CSS Transforms 1 §12) ---

// One 2D transform function with its arguments as numbers: lengths in px,
// angles in degrees. `scale(2)` is `scale(2, 2)`, `translate(1px)` is
// `translate(1px, 0px)`, so two functions of one primitive always pair.
struct transform_fn {
    std::string name;
    std::vector<double> args;
    // A translate's percentage part per argument, kept in step with `args`
    // (the px part): `translate(12px, 70%)` keeps its percentages and reads
    // back as written, which is how the computed serialiser prints it.
    std::vector<double> pct;
};

[[nodiscard]] std::string_view primitive_of(std::string_view name);

[[nodiscard]] std::optional<std::vector<transform_fn>> parse_transforms(std::string_view text);

// The affine matrix as CSS writes it: x' = a*x + c*y + e, y' = b*x + d*y + f.
using matrix2d = std::array<double, 6>;

[[nodiscard]] matrix2d multiply(const matrix2d & m, const matrix2d & n);

[[nodiscard]] double radians(double deg);

[[nodiscard]] matrix2d matrix_of(const transform_fn & fn);

[[nodiscard]] matrix2d matrix_of(const std::vector<transform_fn> & list);

// §12.2, the 2D decomposition: translation, scale, rotation and the
// residual matrix, interpolated separately and recomposed.
struct decomposed2d {
    double tx, ty, sx, sy, angle, m11, m12, m21, m22;
};

[[nodiscard]] decomposed2d decompose(const matrix2d & m);

[[nodiscard]] matrix2d recompose(const decomposed2d & d);

[[nodiscard]] std::string matrix_text(const matrix2d & m);

[[nodiscard]] std::string function_text(const transform_fn & fn);

[[nodiscard]] transform_fn identity_like(const transform_fn & fn);

[[nodiscard]] std::optional<std::string> interpolate_transform(std::string_view from,
                                                               std::string_view to, double p);

// --- filter lists (Filter Effects 1 §11) ---

[[nodiscard]] std::string interpolate_pair(std::string_view property, std::string_view from,
                                           std::string_view to, double p,
                                           const css::length_context & ctx, bool & interpolable);

struct filter_fn {
    std::string name;
    std::string args;
};

[[nodiscard]] std::optional<std::vector<filter_fn>> parse_filters(std::string_view text);

[[nodiscard]] std::string_view filter_identity(std::string_view name);

[[nodiscard]] std::optional<std::string> interpolate_filter(std::string_view from,
                                                            std::string_view to, double p,
                                                            const css::length_context & ctx);

[[nodiscard]] std::optional<std::string> accumulate_transform(std::string_view underlying,
                                                              std::string_view value);

// --- the rotate property (CSS Transforms 2 §7.2) ---

struct rotation {
    double x = 0, y = 0, z = 1, angle = 0;
};

[[nodiscard]] std::optional<rotation> rotation_of(std::string_view text,
                                                  const css::length_context & ctx);

[[nodiscard]] std::optional<std::string> interpolate_rotate(std::string_view from,
                                                            std::string_view to, double p,
                                                            const css::length_context & ctx);

[[nodiscard]] std::optional<double> ratio_of(std::string_view text);

[[nodiscard]] std::optional<std::string> interpolate_ratio(std::string_view from,
                                                           std::string_view to, double p);

[[nodiscard]] std::optional<std::pair<std::string_view, std::string_view>> calc_size_args(
    std::string_view text);

[[nodiscard]] std::string interpolate_pair(std::string_view property, std::string_view from,
                                           std::string_view to, double p,
                                           const css::length_context & ctx, bool & interpolable);

[[nodiscard]] std::string add_pair(std::string_view property, std::string_view underlying,
                                   std::string_view value, const css::length_context & ctx);

} // namespace ctbrowser::style::easing_detail
