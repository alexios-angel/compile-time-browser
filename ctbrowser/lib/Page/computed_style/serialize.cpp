//===- computed_style/serialize.cpp - Value serialisation -------*- C++ -*-===//
//
// How a computed value is written down. Numbers as CSSOM's
// shortest round-tripping decimal, colours in the one form both engines
// normalise to, transforms as matrices, shadows in canonical order, and
// keywords folded.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

#include <ctbrowser/style/css/token.hpp>

#include <numbers>

namespace ctbrowser::page {

using namespace detail;

namespace {} // namespace

namespace detail {

/// Serialises a CSS number as CSSOM §6.7.2 requires: the shortest decimal that
/// reads back as the same float, with no exponent and no trailing zeros, and an
/// integer printed as an integer. `std::to_chars` in `fixed` format is exactly
/// that. It takes a float, not a double, so the author's `20.7` does not print
/// as `20.700000762939453`.
///
/// It does not round: a value the author wrote must round-trip
/// (getComputedStyle-margins-roundtrip). Values produced by layout arithmetic
/// go through used_px_text, which snaps to 1/64. tools/check/css-parity.py
/// quantises both sides to 1/64 itself (EPSILON_PX), so the parity report does
/// not depend on this.
[[nodiscard]] std::string number_text(float value) {
    if (!std::isfinite(value)) { return "0"; }
    std::array<char, 64> buffer{};
    const std::to_chars_result written = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                                       value, std::chars_format::fixed);
    if (written.ec != std::errc{}) { return "0"; }
    std::string out{buffer.data(), static_cast<std::size_t>(written.ptr - buffer.data())};
    if (out.empty() || out == "-0") { return "0"; }
    return out;
}

/// Resolves a border width to px. `thin`, `medium` and `thick` are 1, 3 and 5
/// CSS pixels, the figures every engine uses; getComputedStyle reports a border
/// width as a length, never as its keyword. An empty text is `medium`.
[[nodiscard]] float border_width_px(std::string_view text, float font_size) {
    if (text.empty() || ascii_iequals(text, "medium")) { return 3.0f; }
    if (ascii_iequals(text, "thin")) { return 1.0f; }
    if (ascii_iequals(text, "thick")) { return 5.0f; }
    const layout::length len = layout::parse_length(text);
    if (len.is_auto()) { return 0.0f; }
    // A percentage is not a valid border width; one that got through the
    // cascade anyway resolves against a zero basis.
    return len.resolve(0.0f, font_size);
}

/// A computed length: the number as it is, plus `px`.
[[nodiscard]] std::string px_text(float value) {
    return number_text(value) + "px";
}

/// A used length, one layout arrived at by adding and subtracting fragment
/// bounds, rounded to 1/64 px. Chrome's LayoutUnit cannot represent anything
/// finer, and our own float arithmetic drifts below that quantum too:
/// `784 - 763.3 - 0` prints as `20.70001` unrounded and `20.703125` rounded.
[[nodiscard]] std::string used_px_text(float value) {
    if (!std::isfinite(value)) { return "0px"; }
    return number_text(std::round(value * 64.0f) / 64.0f) + "px";
}

/// A colour as `rgb(r, g, b)`, or `rgba(r, g, b, a)` when not opaque, the form
/// both engines normalise to; `#0d6efd` and `rgb(13,110,253)` then compare
/// equal in the parity harness.
///
/// The alpha is quantised to 1/64, unlike every other number here: it is stored
/// in eight bits, so the page's `.5` comes back as 128/255 = 0.5019608, and
/// rounding to 1/64 recovers the `0.5` every engine prints.
[[nodiscard]] std::string color_text(color c) {
    const auto channel = [](std::uint8_t v) { return std::to_string(static_cast<int>(v)); };
    const std::string rgb = channel(c.red()) + ", " + channel(c.green()) + ", " + channel(c.blue());
    if (c.opaque()) { return "rgb(" + rgb + ")"; }
    const float alpha = static_cast<float>(c.alpha()) / 255.0f;
    return "rgba(" + rgb + ", " + number_text(std::round(alpha * 64.0f) / 64.0f) + ")";
}

/// The colour a system colour keyword resolves to. CSS Color 4 §6.2 leaves the
/// value to the platform, and CSSOM makes the resolved value of a colour
/// property the used one, so `background-color: Menu` reads back as an `rgb()`
/// in every browser (getComputedStyle-resolved-colors). Style's light-scheme
/// table is the answer: nothing here is themed.
[[nodiscard]] std::optional<color> system_color(std::string_view text) {
    const std::string_view word = trim(text, html_whitespace);
    // `rebeccapurple` (#663399) is also handled here: it is the one named
    // colour CSS Color 4 added that paint's table predates
    // (adoptedstylesheets-cascade-order).
    const std::optional<std::uint32_t> rgb = ascii_iequals(word, "rebeccapurple")
                                                 ? std::optional<std::uint32_t>{0x663399}
                                                 : style::css::system_color(word);
    if (!rgb) { return std::nullopt; }
    return color{0xFF000000u | *rgb};
}

/// The resolved value of a transform list is a matrix (CSS Transforms 2 §9):
/// the product of its functions, serialised as `matrix(a, b, c, d, e, f)` when
/// that product is 2D (`scale(0.5)` reads back as
/// `matrix(0.5, 0, 0, 0.5, 0, 0)`) and as `matrix3d()` with sixteen values when
/// it is not; `none` stays `none`. This computes the product, column-major.
///
/// Only what can be multiplied here is: a function this reader does not model,
/// or a percentage with no box to resolve against, returns nullopt so the
/// caller keeps the cascade's text. `em` and `rem` are the element's and the
/// root's font sizes, and a translation's percentage is of the border box: x
/// of its width, y of its height (transform-interpolation-004).
std::optional<std::array<double, 16>> transform_matrix3d(std::string_view text,
                                                         const transform_basis & basis) {
    if (text.empty() || ascii_iequals(text, "none")) { return std::nullopt; }
    using style::css::token_type;
    const style::css::token_stream ts = style::css::tokenize(text);
    // Column-major, as matrix3d() lists it: m[c * 4 + r] is row r of column c.
    const auto identity = [] {
        return std::array<double, 16>{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    };
    std::array<double, 16> m = identity();
    const auto multiply = [&m](const std::array<double, 16> & n) {
        std::array<double, 16> out{};
        for (std::size_t c = 0; c < 4; ++c) {
            for (std::size_t r = 0; r < 4; ++r) {
                double v = 0;
                for (std::size_t k = 0; k < 4; ++k) { v += m[k * 4 + r] * n[c * 4 + k]; }
                out[c * 4 + r] = v;
            }
        }
        m = out;
    };
    std::size_t at = 0;
    bool any = false;
    for (;;) {
        while (ts.tokens[at].type == token_type::whitespace) { ++at; }
        if (ts.tokens[at].type == token_type::eof) { break; }
        if (ts.tokens[at].type != token_type::function) { return std::nullopt; }
        const std::string_view raw = ts.text_of(ts.tokens[at]);
        const std::string name = ascii_lower_copy(raw.substr(0, raw.size() - 1));
        ++at;
        // The arguments: numbers, angles in degrees, lengths in px; anything
        // else is not this reader's.
        std::vector<double> args;
        std::vector<bool> is_length;
        bool none = false;
        for (;;) {
            while (ts.tokens[at].type == token_type::whitespace) { ++at; }
            const style::css::css_token & t = ts.tokens[at];
            if (t.type == token_type::eof) { return std::nullopt; }
            if (t.type == token_type::close_paren) {
                ++at;
                break;
            }
            if (t.type == token_type::comma) {
                ++at;
                continue;
            }
            if (t.type == token_type::ident && name == "perspective" &&
                ascii_iequals(ts.text_of(t), "none")) {
                none = true;
                args.push_back(0);
                is_length.push_back(true);
            } else if (t.type == token_type::number) {
                args.push_back(t.number);
                is_length.push_back(false);
            } else if (t.type == token_type::percentage) {
                // translatex/translate's first argument is along x, and every
                // other place a percentage may stand in a translation is y.
                const bool along_x = name == "translatex" || (name != "translatey" && args.empty());
                const float box = along_x ? basis.width : basis.height;
                if (!name.starts_with("translate") || name == "translatez" || box < 0 ||
                    args.size() >= 2) {
                    return std::nullopt;
                }
                args.push_back(t.number / 100.0 * box);
                is_length.push_back(true);
            } else if (t.type == token_type::dimension) {
                const std::string unit = ascii_lower_copy(ts.unit_of(t));
                if (unit == "px") {
                    args.push_back(t.number);
                    is_length.push_back(true);
                } else if (unit == "em" || unit == "rem") {
                    args.push_back(t.number *
                                   (unit == "em" ? basis.font_size : basis.root_font_size));
                    is_length.push_back(true);
                } else if (unit == "deg") {
                    args.push_back(t.number);
                    is_length.push_back(false);
                } else if (unit == "rad") {
                    args.push_back(t.number * 180.0 / std::numbers::pi);
                    is_length.push_back(false);
                } else if (unit == "grad") {
                    args.push_back(t.number * 0.9);
                    is_length.push_back(false);
                } else if (unit == "turn") {
                    args.push_back(t.number * 360.0);
                    is_length.push_back(false);
                } else {
                    return std::nullopt;
                }
            } else {
                return std::nullopt;
            }
            ++at;
        }
        const auto radians = [](double deg) { return deg * std::numbers::pi / 180.0; };
        const std::size_t count = args.size();
        std::array<double, 16> n = identity();
        // A 2D function writes a, b, c, d, e and f where matrix3d() keeps them.
        const auto affine = [&n](double a, double b, double c, double d, double e, double f) {
            n[0] = a, n[1] = b, n[4] = c, n[5] = d, n[12] = e, n[13] = f;
        };
        // rotate3d()'s matrix, CSS Transforms 2 §13.2, about the normalised axis.
        const auto rotation = [&n, &radians](double x, double y, double z, double deg) {
            const double length = std::hypot(x, y, z);
            if (length == 0) { return; }
            x /= length, y /= length, z /= length;
            const double half = radians(deg) / 2;
            const double sc = std::sin(half) * std::cos(half);
            const double sq = std::sin(half) * std::sin(half);
            n[0] = 1 - 2 * (y * y + z * z) * sq;
            n[1] = 2 * (x * y * sq + z * sc);
            n[2] = 2 * (x * z * sq - y * sc);
            n[4] = 2 * (x * y * sq - z * sc);
            n[5] = 1 - 2 * (x * x + z * z) * sq;
            n[6] = 2 * (y * z * sq + x * sc);
            n[8] = 2 * (x * z * sq + y * sc);
            n[9] = 2 * (y * z * sq - x * sc);
            n[10] = 1 - 2 * (x * x + y * y) * sq;
        };
        bool lengths = name.starts_with("translate") || name == "perspective";
        if (name == "matrix" && count == 6) {
            affine(args[0], args[1], args[2], args[3], args[4], args[5]);
        } else if (name == "matrix3d" && count == 16) {
            for (std::size_t i = 0; i < 16; ++i) { n[i] = args[i]; }
        } else if (name == "scale" && (count == 1 || count == 2)) {
            n[0] = args[0];
            n[5] = count == 2 ? args[1] : args[0];
        } else if (name == "scalex" && count == 1) {
            n[0] = args[0];
        } else if (name == "scaley" && count == 1) {
            n[5] = args[0];
        } else if (name == "scalez" && count == 1) {
            n[10] = args[0];
        } else if (name == "scale3d" && count == 3) {
            n[0] = args[0], n[5] = args[1], n[10] = args[2];
        } else if (name == "translate" && (count == 1 || count == 2)) {
            n[12] = args[0];
            n[13] = count == 2 ? args[1] : 0.0;
        } else if (name == "translatex" && count == 1) {
            n[12] = args[0];
        } else if (name == "translatey" && count == 1) {
            n[13] = args[0];
        } else if (name == "translatez" && count == 1) {
            n[14] = args[0];
        } else if (name == "translate3d" && count == 3) {
            n[12] = args[0], n[13] = args[1], n[14] = args[2];
        } else if ((name == "rotate" || name == "rotatez") && count == 1) {
            rotation(0, 0, 1, args[0]);
        } else if (name == "rotatex" && count == 1) {
            rotation(1, 0, 0, args[0]);
        } else if (name == "rotatey" && count == 1) {
            rotation(0, 1, 0, args[0]);
        } else if (name == "rotate3d" && count == 4) {
            rotation(args[0], args[1], args[2], args[3]);
        } else if (name == "perspective" && count == 1) {
            // `none` is no perspective, and a depth under 1px is 1px (§13.1).
            if (!none) { n[11] = -1.0 / std::max(args[0], 1.0); }
        } else if (name == "skew" && (count == 1 || count == 2)) {
            affine(1, count == 2 ? std::tan(radians(args[1])) : 0.0, std::tan(radians(args[0])), 1,
                   0, 0);
        } else if (name == "skewx" && count == 1) {
            affine(1, 0, std::tan(radians(args[0])), 1, 0, 0);
        } else if (name == "skewy" && count == 1) {
            affine(1, std::tan(radians(args[0])), 0, 1, 0, 0);
        } else {
            return std::nullopt;
        }
        // A length where a number belongs, or the reverse, is a syntax error the
        // cascade let through; this reader does not guess. The last
        // argument of rotate3d() is its angle, which reads as a number here.
        for (std::size_t i = 0; i < count; ++i) {
            if (is_length[i] != lengths && !(lengths && args[i] == 0.0)) { return std::nullopt; }
        }
        multiply(n);
        any = true;
    }
    if (!any) { return std::nullopt; }
    return m;
}

/// The 2D form of transform_matrix3d's product, or nullopt when it is 3D.
std::optional<std::array<double, 6>> transform_matrix(std::string_view text,
                                                      const transform_basis & basis) {
    const auto m = transform_matrix3d(text, basis);
    if (!m) { return std::nullopt; }
    const std::array<double, 16> & n = *m;
    const bool flat = n[2] == 0 && n[3] == 0 && n[6] == 0 && n[7] == 0 && n[8] == 0 && n[9] == 0 &&
                      n[10] == 1 && n[11] == 0 && n[14] == 0 && n[15] == 1;
    if (!flat) { return std::nullopt; }
    return std::array<double, 6>{n[0], n[1], n[4], n[5], n[12], n[13]};
}

/// The resolved `transform` text; see transform_matrix3d.
std::string transform_matrix_text(std::string_view text, const transform_basis & basis) {
    if (const auto matrix = transform_matrix(text, basis)) {
        const auto & m = *matrix;
        std::string out{"matrix("};
        for (std::size_t i = 0; i < 6; ++i) {
            if (i != 0) { out += ", "; }
            // Rounded to six decimals, as Chrome prints a matrix: `cos(90deg)` is
            // 6e-17 in a double and `0` on the page.
            out += number_text(static_cast<float>(std::round(m[i] * 1e6) / 1e6));
        }
        out += ')';
        return out;
    }
    const auto matrix = transform_matrix3d(text, basis);
    if (!matrix) { return std::string{text}; }
    std::string out{"matrix3d("};
    for (std::size_t i = 0; i < 16; ++i) {
        if (i != 0) { out += ", "; }
        // Nine decimals, not six: a perspective term is -1/depth, and six
        // decimals of -1/450 lose too much (the 3D interpolation tests
        // compare to a relative 1e-5).
        const double v = (*matrix)[i];
        std::array<char, 64> buffer{};
        const auto written =
            std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                          std::fabs(v) < 5e-10 ? 0.0 : v, std::chars_format::fixed, 9);
        std::string one{buffer.data(), written.ptr};
        while (one.size() > 1 && one.back() == '0') { one.pop_back(); }
        if (one.back() == '.') { one.pop_back(); }
        out += one == "-0" ? "0" : one;
    }
    out += ')';
    return out;
}

/// A shadow list as its resolved value (CSS Backgrounds 3 §7.2, CSS Text
/// Decoration 3 §4). Each shadow's colour computes to a colour: a system or
/// named colour becomes its `rgb()`, and an absent one is `currentcolor`, which
/// the caller substitutes. Its lengths become px, the omitted ones zero. The
/// colour is serialised first, then the lengths, then `inset`, the order every
/// engine prints (getComputedStyle-resolved-colors asks that
/// `box-shadow: 1px 1px Menu` begin with `rgb(`). `box` selects box-shadow's
/// four lengths over text-shadow's three. `none` stays `none`; a shadow this
/// reader cannot take apart keeps its text.
[[nodiscard]] std::string shadow_text(std::string_view text, float font_size, bool box) {
    const std::string_view whole = trim(text, html_whitespace);
    if (whole.empty() || ascii_iequals(whole, "none")) { return "none"; }
    // Split at depth zero: a comma or a space inside `rgb(1, 2, 3)` is the
    // colour's, not the list's.
    const auto split = [](std::string_view in, bool on_comma) {
        std::vector<std::string_view> out;
        int depth = 0;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= in.size(); ++i) {
            const bool end = i == in.size();
            const char c = end ? '\0' : in[i];
            if (c == '(') { ++depth; }
            if (c == ')') { --depth; }
            const bool cut =
                end || (depth == 0 && (on_comma ? c == ',' : html_whitespace.contains(c)));
            if (!cut) { continue; }
            const std::string_view piece = trim(in.substr(start, i - start), html_whitespace);
            if (!piece.empty()) { out.push_back(piece); }
            start = i + 1;
        }
        return out;
    };
    std::string out;
    for (const std::string_view shadow : split(whole, true)) {
        std::string colour;
        std::vector<std::string> lengths;
        bool inset = false;
        for (const std::string_view part : split(shadow, false)) {
            if (ascii_iequals(part, "inset")) {
                inset = true;
            } else if (ascii_iequals(part, "currentcolor")) {
                colour = "currentcolor";
            } else if (std::string c = style::css::computed_color(part, {}); !c.empty()) {
                colour = std::move(c);
            } else if (const std::optional<color> c = paint::parse_color(part)) {
                colour = color_text(*c);
            } else if (const std::optional<color> s = system_color(part)) {
                colour = color_text(*s);
            } else {
                const layout::length len = layout::parse_length(part);
                if (len.is_auto() || len.u == layout::unit::percent) { return std::string{text}; }
                lengths.push_back(px_text(len.resolve(0.0f, font_size)));
            }
        }
        const std::size_t wanted = box ? 4 : 3;
        if (lengths.size() < 2 || lengths.size() > wanted) { return std::string{text}; }
        while (lengths.size() < wanted) { lengths.emplace_back("0px"); }
        if (!out.empty()) { out += ", "; }
        out += colour.empty() ? std::string{"currentcolor"} : colour;
        for (const std::string & len : lengths) { out += ' ' + len; }
        if (inset) { out += " inset"; }
    }
    return out;
}

/// Lowercases a keyword value and collapses whitespace runs to one space.
/// Quoted strings keep their case and spacing. With keep_case, only the
/// whitespace is collapsed.
[[nodiscard]] std::string collapse_keyword(std::string_view text, bool keep_case) {
    std::string out;
    bool gap = false;
    char quote = 0; // Inside a string, which keeps its case and its spaces.
    for (const char c : trim(text, html_whitespace)) {
        if (quote != 0) {
            out += c;
            if (c == quote && (out.size() < 2 || out[out.size() - 2] != '\\')) { quote = 0; }
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
            gap = true;
            continue;
        }
        if (gap && !out.empty()) { out += ' '; }
        gap = false;
        if (c == '"' || c == '\'') { quote = c; }
        out += keep_case ? c : ascii_lower(c);
    }
    return out;
}

} // namespace detail

} // namespace ctbrowser::page
