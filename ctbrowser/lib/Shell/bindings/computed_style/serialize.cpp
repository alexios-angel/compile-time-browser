// dom_bindings - how a computed value is written down: numbers as CSSOM's
// shortest round-tripping decimal, colours in the one form both engines
// normalise to, and keywords folded.

#include "internal.hpp"

#include <ctbrowser/style/css/token.hpp>

#include <numbers>

namespace ctbrowser::shell {

using namespace detail;

namespace {

// A CSS number, serialised as CSSOM §6.7.2 requires: the SHORTEST decimal that
// reads back as the same number, with no exponent, no trailing zeros, and an
// integer printed as an integer. `std::to_chars` in `fixed` format is exactly
// that definition, and it is asked about a FLOAT rather than a double on
// purpose - every value here is one, and the shortest string for the float is
// the author's `20.7` where the shortest string for the double it widens to is
// `20.700000762939453`.
//
// IT DOES NOT ROUND. It used to snap to 1/64 - Chrome's LayoutUnit quantum - so
// that two values differing below the quantum could not print as a difference.
// That is right for a value layout produced and wrong for one the author wrote:
// `margin-left: 20.7px` came back as `20.703125px`, which is
// getComputedStyle-margins-roundtrip and getComputedStyle-insets-absolute-roundtrip
// in full, 8 subtests, and is the Chromium bug both files are named after. The
// snap lives in `used_px_text` below, and only the values that genuinely come
// out of layout arithmetic go through it. tools/check/css-parity.py quantises
// BOTH sides to 1/64 itself before comparing (EPSILON_PX), so nothing in the
// parity report depends on this rounding here.
// (Defined in `detail` below, beside the helpers that build on it.)

} // namespace

namespace detail {

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

// `thin` / `medium` / `thick` are 1, 3 and 5 CSS pixels - the figures every
// engine uses, and the reason the keyword has to become a number at all is that
// getComputedStyle reports a border width as a length and never as its keyword.
[[nodiscard]] float border_width_px(std::string_view text, float font_size) {
    if (text.empty() || ascii_iequals(text, "medium")) { return 3.0f; }
    if (ascii_iequals(text, "thin")) { return 1.0f; }
    if (ascii_iequals(text, "thick")) { return 5.0f; }
    const layout::length len = layout::parse_length(text);
    if (len.is_auto()) { return 0.0f; }
    // A percentage is not a valid border width; resolving one against a zero
    // basis is the honest answer to a declaration that got through anyway.
    return len.resolve(0.0f, font_size);
}

// A COMPUTED length: the number as it is.
[[nodiscard]] std::string px_text(float value) {
    return number_text(value) + "px";
}

// A USED length - one layout arrived at by adding and subtracting fragment
// bounds - rounded to 1/64. Chrome's LayoutUnit cannot represent anything finer,
// so a used value that differs from Chrome's below the quantum is not a
// difference; and our own arithmetic drifts there too, so `784 - 763.3 - 0`
// prints as `20.70001` without this and as `20.703125` with it. Both are honest
// about a number nobody wrote down.
[[nodiscard]] std::string used_px_text(float value) {
    if (!std::isfinite(value)) { return "0px"; }
    return number_text(std::round(value * 64.0f) / 64.0f) + "px";
}

// A colour in the one form both engines normalise to. Chrome prints
// `rgb(13, 110, 253)` and has drifted across versions, so the harness normalises
// both sides rather than either engine imitating the other. Resolving here still
// earns its keep: `#0d6efd` and `rgb(13,110,253)` then compare equal without the
// tool knowing Bootstrap's palette, and a colour that does NOT parse comes back
// as its raw text rather than silently as black.
//
// THE ALPHA IS QUANTISED, unlike every other number here. It is not a length and
// it is not the author's number either: an 8-bit channel divided by 255 is
// 0.5019608 for the `rgba(0, 0, 0, .5)` a page wrote, and rounding to 1/64
// recovers the `0.5` every engine prints. The precision that is lost was never
// there - the colour is stored in eight bits.
[[nodiscard]] std::string color_text(color c) {
    const auto channel = [](std::uint8_t v) { return std::to_string(static_cast<int>(v)); };
    const std::string rgb = channel(c.red()) + ", " + channel(c.green()) + ", " + channel(c.blue());
    if (c.opaque()) { return "rgb(" + rgb + ")"; }
    const float alpha = static_cast<float>(c.alpha()) / 255.0f;
    return "rgba(" + rgb + ", " + number_text(std::round(alpha * 64.0f) / 64.0f) + ")";
}

// THE COMPUTED `transform` IS A MATRIX. CSS Transforms 1 §9: the resolved value
// of a 2D transform list is the product of its functions, serialised as
// `matrix(a, b, c, d, e, f)` - `scale(0.5)` reads back as `matrix(0.5, 0, 0,
// 0.5, 0, 0)` and `none` stays `none`. Six of css/css-values' serialize files
// ask for it through `scale()`.
//
// ONLY WHAT CAN BE MULTIPLIED HERE IS. A function this reader does not model, a
// percentage (needs the box) and a 3D function (a `matrix3d`) keep the
// cascade's text, which is what every transform used to read back as.
//
// ponytail: 2D only, lengths in px alone; add `em`/percent against the probe's
// font size and box when a test asks.
[[nodiscard]] std::string transform_matrix_text(std::string_view text) {
    using style::css::token_type;
    const style::css::token_stream ts = style::css::tokenize(text);
    // The affine matrix as CSS writes it: x' = a*x + c*y + e, y' = b*x + d*y + f.
    double m[6] = {1, 0, 0, 1, 0, 0};
    const auto multiply = [&m](const double (&n)[6]) {
        const double a = m[0] * n[0] + m[2] * n[1];
        const double b = m[1] * n[0] + m[3] * n[1];
        const double c = m[0] * n[2] + m[2] * n[3];
        const double d = m[1] * n[2] + m[3] * n[3];
        const double e = m[0] * n[4] + m[2] * n[5] + m[4];
        const double f = m[1] * n[4] + m[3] * n[5] + m[5];
        m[0] = a, m[1] = b, m[2] = c, m[3] = d, m[4] = e, m[5] = f;
    };
    std::size_t at = 0;
    bool any = false;
    for (;;) {
        while (ts.tokens[at].type == token_type::whitespace) { ++at; }
        if (ts.tokens[at].type == token_type::eof) { break; }
        if (ts.tokens[at].type != token_type::function) { return std::string{text}; }
        const std::string_view raw = ts.text_of(ts.tokens[at]);
        const std::string name = ascii_lower_copy(raw.substr(0, raw.size() - 1));
        ++at;
        // The arguments: numbers, angles in degrees, lengths in px; anything
        // else is not this reader's.
        std::vector<double> args;
        std::vector<bool> is_length;
        for (;;) {
            while (ts.tokens[at].type == token_type::whitespace) { ++at; }
            const style::css::css_token & t = ts.tokens[at];
            if (t.type == token_type::close_paren) {
                ++at;
                break;
            }
            if (t.type == token_type::comma) {
                ++at;
                continue;
            }
            if (t.type == token_type::number) {
                args.push_back(t.number);
                is_length.push_back(false);
            } else if (t.type == token_type::dimension) {
                const std::string unit = ascii_lower_copy(ts.unit_of(t));
                if (unit == "px") {
                    args.push_back(t.number);
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
                    return std::string{text};
                }
            } else {
                return std::string{text};
            }
            ++at;
        }
        const auto radians = [](double deg) { return deg * std::numbers::pi / 180.0; };
        double n[6] = {1, 0, 0, 1, 0, 0};
        const std::size_t count = args.size();
        if (name == "matrix" && count == 6) {
            for (std::size_t i = 0; i < 6; ++i) { n[i] = args[i]; }
        } else if (name == "scale" && (count == 1 || count == 2)) {
            n[0] = args[0];
            n[3] = count == 2 ? args[1] : args[0];
        } else if (name == "scalex" && count == 1) {
            n[0] = args[0];
        } else if (name == "scaley" && count == 1) {
            n[3] = args[0];
        } else if (name == "translate" && (count == 1 || count == 2)) {
            n[4] = args[0];
            n[5] = count == 2 ? args[1] : 0.0;
        } else if (name == "translatex" && count == 1) {
            n[4] = args[0];
        } else if (name == "translatey" && count == 1) {
            n[5] = args[0];
        } else if (name == "rotate" && count == 1) {
            const double r = radians(args[0]);
            n[0] = std::cos(r), n[1] = std::sin(r), n[2] = -std::sin(r), n[3] = std::cos(r);
        } else if (name == "skew" && (count == 1 || count == 2)) {
            n[2] = std::tan(radians(args[0]));
            n[1] = count == 2 ? std::tan(radians(args[1])) : 0.0;
        } else if (name == "skewx" && count == 1) {
            n[2] = std::tan(radians(args[0]));
        } else if (name == "skewy" && count == 1) {
            n[1] = std::tan(radians(args[0]));
        } else {
            return std::string{text};
        }
        // A LENGTH WHERE A NUMBER BELONGS, or the reverse, is a syntax error the
        // cascade let through; it is not this reader's to guess at.
        for (std::size_t i = 0; i < count; ++i) {
            const bool translate = name.starts_with("translate");
            if (is_length[i] != translate && !(translate && args[i] == 0.0)) {
                return std::string{text};
            }
        }
        multiply(n);
        any = true;
    }
    if (!any) { return std::string{text}; }
    std::string out{"matrix("};
    for (std::size_t i = 0; i < 6; ++i) {
        if (i != 0) { out += ", "; }
        // TO SIX DECIMALS, as Chrome prints a matrix: `cos(90deg)` is 6e-17 in
        // a double and `0` on the page.
        out += number_text(static_cast<float>(std::round(m[i] * 1e6) / 1e6));
    }
    out += ')';
    return out;
}

[[nodiscard]] std::string collapse_keyword(std::string_view text) {
    std::string out;
    bool gap = false;
    for (const char c : trim(text, html_whitespace)) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
            gap = true;
            continue;
        }
        if (gap && !out.empty()) { out += ' '; }
        gap = false;
        out += ascii_lower(c);
    }
    return out;
}

} // namespace detail

} // namespace ctbrowser::shell
