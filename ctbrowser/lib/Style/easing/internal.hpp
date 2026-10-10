//===- easing/internal.hpp - Interpolation internals ------------*- C++ -*-===//
//
// Declarations shared by lib/Style/easing.cpp and the easing/ sources
// (numeric.cpp, pairs.cpp, effects.cpp): parsing, interpolating and adding CSS
// values for transitions and animations. Values travel as CSS text throughout.
//
//===----------------------------------------------------------------------===//
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

/// The comma-separated, trimmed arguments of an easing function's body.
[[nodiscard]] std::vector<std::string_view> split_arguments(std::string_view body);

/// `text` as a finite number with nothing around it, or nullopt.
[[nodiscard]] std::optional<double> number_of(std::string_view text);

/// The positive integer count of `steps()`, clamped to INT_MAX, or nullopt.
[[nodiscard]] std::optional<int> step_count(std::string_view text);

// --- numbers, lengths, percentages and their calc() mixes ---

/// Two endpoints resolved to one numeric type.
struct numeric_pair {
    css::calc_result a, b;
};

/// Both endpoints as one numeric type, or nullopt when they do not pair
/// (`10px` and `2s`, or a keyword).
[[nodiscard]] std::optional<numeric_pair> numeric_of(std::string_view from, std::string_view to,
                                                     const css::length_context & ctx);

/// `(1 - p) * a + p * b`, term by term; `p` may lie outside [0, 1].
[[nodiscard]] css::calc_result mix(const css::calc_result & a, const css::calc_result & b,
                                   double p);

/// `a + b`, term by term (CSS Values 4 §4.3 addition).
[[nodiscard]] css::calc_result sum(const css::calc_result & a, const css::calc_result & b);

/// `out` serialized after clamping it to `property`'s computed-value range.
[[nodiscard]] std::string numeric_text(std::string_view property, css::calc_result out);

// --- colours ---

/// A colour as sRGB with alpha, premultiplied: CSS Color 4 §17 interpolates
/// legacy colours in sRGB with the channels weighted by alpha, so a transparent
/// endpoint contributes no hue. Colours come from style's resolver rather than
/// paint's, because paint holds a channel in eight bits: an alpha of 0.5 read
/// back as 128/255 put the midpoint of blue and half-transparent red at 0.753
/// rather than 0.75.
struct premultiplied {
    double r, g, b, a;
};

[[nodiscard]] premultiplied premultiply(const css::srgb_color & c);

/// A premultiplied colour back to `rgba()` text, clamped to the gamut.
[[nodiscard]] std::string color_text(const premultiplied & c);

/// Interpolates two colours in premultiplied sRGB, as `rgba()` text.
[[nodiscard]] std::string lerp_color(const css::srgb_color & from, const css::srgb_color & to,
                                     double p);

/// True for a legacy colour syntax (named, hex, rgb(), hsl(), hwb()), which
/// interpolates in sRGB; a pair with any other colour interpolates in Oklab.
[[nodiscard]] bool legacy_color(std::string_view text);

/// sRGB to Oklab (Björn Ottosson's matrices, as CSS Color 4 §17.4 gives them),
/// with a channel outside the gamut carried through sign-preserved.
struct oklab {
    double l, a, b;
};

[[nodiscard]] oklab oklab_of(const css::srgb_color & c);

/// Interpolates two colours in Oklab, as `oklab()` text.
[[nodiscard]] std::string lerp_oklab(const css::srgb_color & from, const css::srgb_color & to,
                                     double p);

/// Adds two colours channel by channel, premultiplied (CSS Color 4 §12.4).
[[nodiscard]] std::string add_color(const css::srgb_color & x, const css::srgb_color & y);

/// True for box-shadow and text-shadow.
[[nodiscard]] bool is_shadow(std::string_view property);

/// True when adding two of the property's lists concatenates them (shadows,
/// transforms, filters) rather than adding item by item.
[[nodiscard]] bool appends(std::string_view property);

/// True for the background and mask layer lists, which pair by repeating the
/// shorter list to the least common multiple of the two lengths.
[[nodiscard]] bool repeatable(std::string_view property);

/// The longest list repeat_to_match will build.
inline constexpr std::size_t max_repeated_items = 10000;

/// Repeats both lists to the least common multiple of their lengths. Coprime
/// lengths of ten thousand would make a hundred million entries, so past
/// max_repeated_items the lists are too long to pair: both come back empty,
/// the result is false, and the pair is discrete.
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

/// `text` rewritten into a computed shape whose parts pair item by item:
/// shadows reordered with omitted lengths zero, keywords as the lengths or
/// percentages they name, and so on.
[[nodiscard]] std::string computed_shape(std::string_view property, std::string_view text,
                                         const css::length_context & ctx);

/// The transparent, all-zero shadow a shorter shadow list is padded with;
/// `inset` when `like` is.
[[nodiscard]] std::string blank_shadow(bool box, std::string_view like);

/// The non-empty, trimmed top-level items of a comma list.
[[nodiscard]] std::vector<std::string_view> comma_items(std::string_view text);

// --- transform lists (CSS Transforms 1 §12) ---

/// One transform function with its arguments as numbers: lengths in px,
/// angles in degrees. `scale(2)` is `scale(2, 2)`, `translate(1px)` is
/// `translate(1px, 0px)`, `rotateX(a)` is `rotate3d(1, 0, 0, a)`, so two
/// functions of one primitive always pair; a perspective() holds the
/// reciprocal of its depth, 0 for `none`.
struct transform_fn {
    std::string name;
    std::vector<double> args;
    /// A translate's percentage part per argument, kept in step with `args`
    /// (the px part): `translate(12px, 70%)` keeps its percentages and reads
    /// back as written, which is how the computed serializer prints it.
    std::vector<double> pct;
    /// The name as written, lowercased: `translatey` keeps its one-axis
    /// spelling through an interpolation with another `translateY`.
    std::string spelled = {};
};

/// The primitive a function name belongs to (`translateX` is `translate`).
[[nodiscard]] std::string_view primitive_of(std::string_view name);

/// A transform list in canonical form; empty for `none`, nullopt for a list
/// this does not model.
[[nodiscard]] std::optional<std::vector<transform_fn>> parse_transforms(std::string_view text);

/// The affine matrix as CSS writes it: x' = a*x + c*y + e, y' = b*x + d*y + f.
using matrix2d = std::array<double, 6>;

/// The product m * n: n is applied first.
[[nodiscard]] matrix2d multiply(const matrix2d & m, const matrix2d & n);

[[nodiscard]] double radians(double deg);

/// The matrix of one 2D function, or of a whole list multiplied in order.
[[nodiscard]] matrix2d matrix_of(const transform_fn & fn);

[[nodiscard]] matrix2d matrix_of(const std::vector<transform_fn> & list);

/// §12.2, the 2D decomposition: translation, scale, rotation and the
/// residual matrix, interpolated separately and recomposed.
struct decomposed2d {
    double tx, ty, sx, sy, angle, m11, m12, m21, m22;
};

[[nodiscard]] decomposed2d decompose(const matrix2d & m);

[[nodiscard]] matrix2d recompose(const decomposed2d & d);

/// `matrix(...)` text, rounded to six decimals.
[[nodiscard]] std::string matrix_text(const matrix2d & m);

/// A 4x4 matrix, column-major as matrix3d() lists it: m[c * 4 + r] is row r
/// of column c.
using matrix3d = std::array<double, 16>;

/// §16.2, the 3D decomposition: interpolated part by part, the rotation as a
/// quaternion by spherical interpolation, and recomposed.
struct decomposed3d {
    std::array<double, 3> translate, scale, skew;
    std::array<double, 4> perspective, quaternion;
};

/// Rewrites a 2D translate, scale or rotate as its 3D form, for a pair whose
/// other half is 3D.
void promote(transform_fn & fn);

/// True for the canonical 3D functions.
[[nodiscard]] bool is_3d(const transform_fn & fn);

[[nodiscard]] matrix3d identity3d();

[[nodiscard]] matrix3d multiply(const matrix3d & a, const matrix3d & b);

/// rotate3d()'s matrix about the normalized axis (x, y, z).
[[nodiscard]] matrix3d rotation3d(double x, double y, double z, double deg);

[[nodiscard]] matrix3d matrix3d_of(const transform_fn & fn);

[[nodiscard]] matrix3d matrix3d_of(const std::vector<transform_fn> & list);

/// True when the 4x4 matrix is a 2D affine transform.
[[nodiscard]] bool flat(const matrix3d & m);

/// The determinant of the row-major 3x3 matrix (a b c; d e f; g h i).
[[nodiscard]] double determinant3(double a, double b, double c, double d, double e, double f,
                                  double g, double h, double i);

/// The inverse, or nullopt for a singular matrix.
[[nodiscard]] std::optional<matrix3d> inverse(const matrix3d & m);

/// §16.2.1's decomposition, or nullopt when the matrix will not decompose.
[[nodiscard]] std::optional<decomposed3d> decompose3d(matrix3d m);

/// §16.2.2's recomposition, the inverse of decompose3d.
[[nodiscard]] matrix3d recompose3d(const decomposed3d & d);

/// §16.2.3: every part linearly, the rotation by spherical interpolation.
[[nodiscard]] decomposed3d blend3d(const decomposed3d & a, const decomposed3d & b, double p);

/// `matrix3d(...)` text, or `matrix(...)` when the matrix is 2D.
[[nodiscard]] std::string matrix_text(const matrix3d & m);

/// Two single 3D functions blended through their matrices; nullopt when both
/// are 2D or either will not decompose.
[[nodiscard]] std::optional<std::string> blend_as_matrices(const std::vector<transform_fn> & a,
                                                           const std::vector<transform_fn> & b,
                                                           double p);

/// Two whole lists blended through 4x4 matrix decomposition; nullopt for a
/// percentage or a singular matrix.
[[nodiscard]] std::optional<std::string> blend_lists(const std::vector<transform_fn> & a,
                                                     const std::vector<transform_fn> & b, double p);

/// One canonical transform function back to CSS text.
[[nodiscard]] std::string function_text(const transform_fn & fn);

/// The identity function of `fn`'s kind, which pads the shorter list of a pair.
[[nodiscard]] transform_fn identity_like(const transform_fn & fn);

/// Interpolates two transform lists at progress p. Returns nullopt, with
/// `singular` set, when a matrix the lists blend through will not decompose:
/// the pair is then not interpolable at all (CSS Transforms 1 §10).
[[nodiscard]] std::optional<std::string> interpolate_transform(std::string_view from,
                                                               std::string_view to, double p,
                                                               bool * singular = nullptr);

// --- filter lists (Filter Effects 1 §11) ---

/// Interpolates one property's computed values `from` -> `to` at progress p;
/// sets `interpolable` to false when the pair cannot be blended and the answer
/// is a discrete flip. (Also declared again at the end of this file.)
[[nodiscard]] std::string interpolate_pair(std::string_view property, std::string_view from,
                                           std::string_view to, double p,
                                           const css::length_context & ctx, bool & interpolable);

/// One filter function: its lowercase name and its argument text.
struct filter_fn {
    std::string name;
    std::string args;
};

/// A filter list in computed form; empty for `none`, nullopt for a url().
[[nodiscard]] std::optional<std::vector<filter_fn>> parse_filters(std::string_view text);

/// The lacuna value of a filter function's missing argument.
[[nodiscard]] std::string_view filter_identity(std::string_view name);

/// Interpolates two filter lists function by function; nullopt when they do
/// not pair.
[[nodiscard]] std::optional<std::string> interpolate_filter(std::string_view from,
                                                            std::string_view to, double p,
                                                            const css::length_context & ctx);

/// Accumulates `value` onto `underlying` (Filter Effects 1 §11.3); nullopt
/// when the keyframe's list should replace instead.
[[nodiscard]] std::optional<std::string> accumulate_filter(std::string_view underlying,
                                                           std::string_view value,
                                                           const css::length_context & ctx);

/// Accumulates `value` onto `underlying` (CSS Transforms 2 §14); nullopt when
/// the caller should append instead.
[[nodiscard]] std::optional<std::string> accumulate_transform(std::string_view underlying,
                                                              std::string_view value);

// --- the rotate property (CSS Transforms 2 §7.2) ---

/// A `rotate` value: an axis (not necessarily normalized) and an angle in
/// degrees; `none` is 0deg about z.
struct rotation {
    double x = 0, y = 0, z = 1, angle = 0;
};

/// Parses a `rotate` value; nullopt when it is not one.
[[nodiscard]] std::optional<rotation> rotation_of(std::string_view text,
                                                  const css::length_context & ctx);

/// Interpolates two `rotate` values, slerping when their axes differ.
[[nodiscard]] std::optional<std::string> interpolate_rotate(std::string_view from,
                                                            std::string_view to, double p,
                                                            const css::length_context & ctx);

/// A `<ratio>` as one number, or nullopt for `auto` or a degenerate ratio.
[[nodiscard]] std::optional<double> ratio_of(std::string_view text);

// --- clip's rect() (CSS Masking 1 §7.1) ---

/// The four components of a `rect()`, or nullopt.
[[nodiscard]] std::optional<std::vector<std::string_view>> rect_parts(std::string_view text);

/// Two rects blended component by component as `wa * a + wb * b`.
[[nodiscard]] std::optional<std::string> blend_rects(std::string_view from, std::string_view to,
                                                     double wa, double wb,
                                                     const css::length_context & ctx);

// --- grid track lists (CSS Grid 2 §7.2.5) ---

/// The top-level words of a track list: a `[names]` block, a `repeat(...)` or
/// one track size.
[[nodiscard]] std::vector<std::string_view> track_words(std::string_view text);

/// One track size of each list blended as `wa * x + wb * y`.
[[nodiscard]] std::string blend_track(std::string_view x, std::string_view y, double wa, double wb,
                                      const css::length_context & ctx);

/// Two track lists of one shape blended track by track; nullopt otherwise.
[[nodiscard]] std::optional<std::string> interpolate_tracks(std::string_view from,
                                                            std::string_view to, double wa,
                                                            double wb,
                                                            const css::length_context & ctx);

/// Two ratios interpolated as the logarithm of their values (CSS Sizing 4 §7.1).
[[nodiscard]] std::optional<std::string> interpolate_ratio(std::string_view from,
                                                           std::string_view to, double p);

/// calc-size(A, B)'s basis and calculation, or nullopt when `text` is not one.
[[nodiscard]] std::optional<std::pair<std::string_view, std::string_view>> calc_size_args(
    std::string_view text);

/// See the first declaration of interpolate_pair above.
[[nodiscard]] std::string interpolate_pair(std::string_view property, std::string_view from,
                                           std::string_view to, double p,
                                           const css::length_context & ctx, bool & interpolable);

/// Adds `value` to `underlying` for one property (Web Animations 1 §4.5.1).
[[nodiscard]] std::string add_pair(std::string_view property, std::string_view underlying,
                                   std::string_view value, const css::length_context & ctx);

/// Undoes the computed_shape expansions the computed serializer would not
/// print.
[[nodiscard]] std::string serialized_shape(std::string_view property, std::string text);

} // namespace ctbrowser::style::easing_detail
