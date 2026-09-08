// dom_bindings - how a computed value is written down: numbers as CSSOM's
// shortest round-tripping decimal, colours in the one form both engines
// normalise to, font-family lists with CSSOM's quoting, and keywords folded.
//
// One of three files carved out of a 1,326-line bindings/computed_style.cpp
// on 2026-09-08. The member functions belong to the one class declared in
// include/ctbrowser/shell/bindings.hpp; the serialisation helpers more than
// one file needs are declared in internal.hpp beside this, with external
// linkage in ctbrowser::shell::detail, and internal.hpp carries the note on
// where a computed value comes from. Nothing about the public header changed.

#include "internal.hpp"

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

// --- A FONT FAMILY IS NOT A KEYWORD --------------------------------------
//
// `collapse_keyword` below ASCII-lowercases, which is right for `display: BLOCK`
// and wrong for every family name a page has ever written: Chrome answers
// `Twisty Tie` and this answered `twisty tie`, on every element of every page
// that names a font. The case is the author's and it is significant.
//
// AND THE LIST IS SERIALISED, not handed back. CSSOM says a family name that is
// a valid IDENTIFIER SEQUENCE serialises without quotes and one that is not
// serialises as a string - so `'Times New Roman'` loses its quotes, `'34J'`
// keeps them because `34J` is not an identifier, `"A  B"` keeps them because
// the double space would not survive, and `"serif"` keeps them because dropping
// them would turn a family CALLED serif into the generic one. The quotes a name
// keeps are always DOUBLE ones, whichever the author used.
// css/cssom/font-family-serialization-001 is fourteen assertions about exactly
// this, and the five it makes about the COMPUTED value are the ones here.

[[nodiscard]] bool is_identifier_start(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

[[nodiscard]] bool is_identifier_char(unsigned char c) {
    return is_identifier_start(c) || (c >= '0' && c <= '9') || c == '-';
}

// A CSS identifier that needs no escaping to be written down. A LEADING RUN OF
// HYPHENS is allowed - `-webkit-serif` is an identifier and so is `--x` - but a
// hyphen run with nothing after it is not, and neither is anything starting with
// a digit, which is the whole reason `34J` has to stay a string.
[[nodiscard]] bool is_bare_identifier(std::string_view word) {
    std::size_t start = 0;
    while (start < word.size() && word[start] == '-') { ++start; }
    if (start >= word.size() || !is_identifier_start(static_cast<unsigned char>(word[start]))) {
        return false;
    }
    for (const char c : word) {
        if (!is_identifier_char(static_cast<unsigned char>(c))) { return false; }
    }
    return true;
}

// The words CSS has already spoken for. A family name that IS one of them can
// only be said as a string, because unquoting it would change what it means.
[[nodiscard]] bool is_reserved_family_word(std::string_view name) {
    for (const std::string_view reserved :
         {"inherit", "initial", "unset", "revert", "revert-layer", "default", "serif", "sans-serif",
          "monospace", "cursive", "fantasy", "system-ui", "math", "fangsong", "ui-serif",
          "ui-sans-serif", "ui-monospace", "ui-rounded", "emoji"}) {
        if (ascii_iequals(name, reserved)) { return true; }
    }
    return false;
}

[[nodiscard]] bool family_can_drop_its_quotes(std::string_view name) {
    if (name.empty() || is_reserved_family_word(name)) { return false; }
    for (std::size_t at = 0;;) {
        const std::size_t space = name.find(' ', at);
        const std::string_view word =
            space == std::string_view::npos ? name.substr(at) : name.substr(at, space - at);
        if (!is_bare_identifier(word)) { return false; }
        if (space == std::string_view::npos) { return true; }
        at = space + 1;
    }
}

[[nodiscard]] std::string quoted_family(std::string_view name) {
    std::string out{"\""};
    for (const char c : name) {
        if (c == '"' || c == '\\') { out += '\\'; }
        out += c;
    }
    out += '"';
    return out;
}

} // namespace

namespace detail {

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

[[nodiscard]] std::string font_family_text(std::string_view text) {
    // (name, was it written as a string). The pair is the whole of the rule: an
    // unquoted name is already an identifier sequence and is handed back as it
    // stands, and only a QUOTED one has a decision to make.
    std::vector<std::pair<std::string, bool>> families;
    std::string name;
    bool quoted = false;
    bool any = false;
    bool gap = false;
    // One past the end is read as a comma, so the last family is finished by the
    // same branch as every other one.
    for (std::size_t i = 0; i <= text.size();) {
        const char c = i < text.size() ? text[i] : ',';
        if (c == '"' || c == '\'') {
            const char close = c;
            ++i;
            quoted = true;
            any = true;
            while (i < text.size() && text[i] != close) {
                // A backslash escapes the next byte and is not itself part of
                // the name. This does not decode `\61` - a hex escape in a font
                // name is rare enough that carrying the digits through is a
                // better answer than a half-implemented decoder.
                if (text[i] == '\\' && i + 1 < text.size()) { ++i; }
                name += text[i];
                ++i;
            }
            if (i < text.size()) { ++i; }
            continue;
        }
        if (c == ',') {
            if (any) { families.emplace_back(name, quoted); }
            name.clear();
            quoted = false;
            any = false;
            gap = false;
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
            gap = !name.empty();
            ++i;
            continue;
        }
        if (gap) {
            name += ' ';
            gap = false;
        }
        name += c;
        any = true;
        ++i;
    }
    std::string out;
    for (const auto & [family, was_string] : families) {
        if (!out.empty()) { out += ", "; }
        out += !was_string || family_can_drop_its_quotes(family) ? family : quoted_family(family);
    }
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
