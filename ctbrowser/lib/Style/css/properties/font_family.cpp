//===- font_family.cpp - font-family serialisation ------------ -*- C++ -*-===//
//
// The `font-family` list, serialised as CSSOM says. `el.style` and
// `getComputedStyle` share this serialisation exactly, so both call it.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::style::css {

namespace {

// A font family is not a keyword: its case is the author's and is significant
// (Chrome answers `Twisty Tie`, never `twisty tie`).
//
// The list is re-serialised, not handed back. CSSOM says a family name that is
// a valid identifier sequence serialises without quotes and one that is not
// serialises as a string: `'Times New Roman'` loses its quotes, `'34J'` keeps
// them because `34J` is not an identifier, `"A  B"` keeps them because the
// double space would not survive, and `"serif"` keeps them because dropping
// them would turn a family called serif into the generic one. A name that
// keeps its quotes always gets double ones, whichever the author used (WPT
// css/cssom/font-family-serialization-001).

/// True for a CSS identifier that needs no escaping to be written down. A
/// leading run of hyphens is allowed (`-webkit-serif` and `--x` are
/// identifiers), but a hyphen run with nothing after it is not, and neither is
/// anything starting with a digit, which is why `34J` has to stay a string.
[[nodiscard]] bool is_bare_identifier(std::string_view word) {
    std::size_t start = 0;
    while (start < word.size() && word[start] == '-') { ++start; }
    if (start >= word.size() || !is_name_start(word[start])) { return false; }
    for (const char c : word) {
        if (!is_name(c)) { return false; }
    }
    return true;
}

/// The words CSS has already reserved. A family name equal to one of them can
/// only be written as a string, because unquoting it would change its meaning.
[[nodiscard]] bool is_reserved_family_word(std::string_view name) {
    return ascii_iequals_any(name, {"inherit", "initial", "unset", "revert", "revert-layer",
                                    "default", "serif", "sans-serif", "monospace", "cursive",
                                    "fantasy", "system-ui", "math", "fangsong", "ui-serif",
                                    "ui-sans-serif", "ui-monospace", "ui-rounded", "emoji"});
}

/// True when a quoted family name can be written without quotes: every
/// space-separated word is a bare identifier and the name is not reserved.
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

/// Writes `name` as a double-quoted CSS string.
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

std::string serialize_font_family(std::string_view text) {
    // (name, was it written as a string). An unquoted name is already an
    // identifier sequence and is handed back as it stands; only a quoted one
    // needs the quote-dropping decision.
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
                // the name. Hex escapes such as `\61` are not decoded; they are
                // rare in font names, so the digits are carried through.
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
        if (html_whitespace.contains(c)) {
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

} // namespace ctbrowser::style::css
