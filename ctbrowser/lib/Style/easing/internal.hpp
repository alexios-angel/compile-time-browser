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

// Both lists repeated to the least common multiple of their lengths. Coprime
// lengths of ten thousand make a hundred million entries, so past
// max_repeated_items the lists are too long to pair: BOTH COME BACK EMPTY and
// the answer is false, and the pair is discrete.
inline constexpr std::size_t max_repeated_items = 10000;

template <typename T> bool repeat_to_match(std::vector<T> & a, std::vector<T> & b) {
    if (a.empty() || b.empty() || a.size() == b.size()) { return true; }
    const std::size_t a_size = a.size(), b_size = b.size();
    const std::size_t n = std::lcm(a_size, b_size);
    if (n > max_repeated_items) {
        a.clear();
        b.clear();
        return false;
    }
    for (std::size_t i = a_size; i < n; ++i) { a.push_back(a[i % a_size]); }
    for (std::size_t i = b_size; i < n; ++i) { b.push_back(b[i % b_size]); }
    return true;
}

[[nodiscard]] std::string computed_shape(std::string_view property, std::string_view text,
                                         const css::length_context & ctx);

[[nodiscard]] std::string blank_shadow(bool box, std::string_view like);

[[nodiscard]] std::vector<std::string_view> comma_items(std::string_view text);

// --- transform lists (CSS Transforms 1 §12) ---

// One transform function with its arguments as numbers: lengths in px,
// angles in degrees. `scale(2)` is `scale(2, 2)`, `translate(1px)` is
// `translate(1px, 0px)`, `rotateX(a)` is `rotate3d(1, 0, 0, a)`, so two
// functions of one primitive always pair; a perspective() holds the
// reciprocal of its depth, 0 for `none`.
struct transform_fn {
    std::string name;
    std::vector<double> args;
    // A translate's percentage part per argument, kept in step with `args`
    // (the px part): `translate(12px, 70%)` keeps its percentages and reads
    // back as written, which is how the computed serialiser prints it.
    std::vector<double> pct;
    // The name as written, lowercased: `translatey` keeps its one-axis
    // spelling through an interpolation with another `translateY`.
    std::string spelled = {};
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

// A 4x4 matrix, column-major as matrix3d() lists it: m[c * 4 + r] is row r
// of column c.
using matrix3d = std::array<double, 16>;

// §16.2, the 3D decomposition: interpolated part by part, the rotation as a
// quaternion by spherical interpolation, and recomposed.
struct decomposed3d {
    std::array<double, 3> translate, scale, skew;
    std::array<double, 4> perspective, quaternion;
};

void promote(transform_fn & fn);

[[nodiscard]] bool is_3d(const transform_fn & fn);

[[nodiscard]] matrix3d identity3d();

[[nodiscard]] matrix3d multiply(const matrix3d & a, const matrix3d & b);

[[nodiscard]] matrix3d rotation3d(double x, double y, double z, double deg);

[[nodiscard]] matrix3d matrix3d_of(const transform_fn & fn);

[[nodiscard]] matrix3d matrix3d_of(const std::vector<transform_fn> & list);

[[nodiscard]] bool flat(const matrix3d & m);

[[nodiscard]] double determinant3(double a, double b, double c, double d, double e, double f,
                                  double g, double h, double i);

[[nodiscard]] std::optional<matrix3d> inverse(const matrix3d & m);

[[nodiscard]] std::optional<decomposed3d> decompose3d(matrix3d m);

[[nodiscard]] matrix3d recompose3d(const decomposed3d & d);

[[nodiscard]] decomposed3d blend3d(const decomposed3d & a, const decomposed3d & b, double p);

[[nodiscard]] std::string matrix_text(const matrix3d & m);

[[nodiscard]] std::optional<std::string> blend_as_matrices(const std::vector<transform_fn> & a,
                                                           const std::vector<transform_fn> & b,
                                                           double p);

[[nodiscard]] std::optional<std::string> blend_lists(const std::vector<transform_fn> & a,
                                                     const std::vector<transform_fn> & b, double p);

[[nodiscard]] std::string function_text(const transform_fn & fn);

[[nodiscard]] transform_fn identity_like(const transform_fn & fn);

// nullopt, with `singular` set, when a matrix the lists blend through will
// not decompose: the pair is then not interpolable at all (CSS Transforms 1
// §10).
[[nodiscard]] std::optional<std::string> interpolate_transform(std::string_view from,
                                                               std::string_view to, double p,
                                                               bool * singular = nullptr);

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

[[nodiscard]] std::optional<std::string> accumulate_filter(std::string_view underlying,
                                                           std::string_view value,
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

// --- clip's rect() (CSS Masking 1 §7.1) ---

[[nodiscard]] std::optional<std::vector<std::string_view>> rect_parts(std::string_view text);

[[nodiscard]] std::optional<std::string> blend_rects(std::string_view from, std::string_view to,
                                                     double wa, double wb,
                                                     const css::length_context & ctx);

// --- grid track lists (CSS Grid 2 §7.2.5) ---

[[nodiscard]] std::vector<std::string_view> track_words(std::string_view text);

[[nodiscard]] std::string blend_track(std::string_view x, std::string_view y, double wa, double wb,
                                      const css::length_context & ctx);

[[nodiscard]] std::optional<std::string> interpolate_tracks(std::string_view from,
                                                            std::string_view to, double wa,
                                                            double wb,
                                                            const css::length_context & ctx);

[[nodiscard]] std::optional<std::string> interpolate_ratio(std::string_view from,
                                                           std::string_view to, double p);

[[nodiscard]] std::optional<std::pair<std::string_view, std::string_view>> calc_size_args(
    std::string_view text);

[[nodiscard]] std::string interpolate_pair(std::string_view property, std::string_view from,
                                           std::string_view to, double p,
                                           const css::length_context & ctx, bool & interpolable);

[[nodiscard]] std::string add_pair(std::string_view property, std::string_view underlying,
                                   std::string_view value, const css::length_context & ctx);

[[nodiscard]] std::string serialized_shape(std::string_view property, std::string text);

} // namespace ctbrowser::style::easing_detail
