// The `font-family` list, serialised as CSSOM says - which is the one
// serialisation `el.style` and `getComputedStyle` share word for word, so it
// lives here and both ask for it.

#include "internal.hpp"

namespace ctbrowser::style::css {

namespace {

// A FONT FAMILY IS NOT A KEYWORD: the case is the author's and it is
// significant - Chrome answers `Twisty Tie`, never `twisty tie`.
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
    return ascii_iequals_any(name, {"inherit", "initial", "unset", "revert", "revert-layer",
                                    "default", "serif", "sans-serif", "monospace", "cursive",
                                    "fantasy", "system-ui", "math", "fangsong", "ui-serif",
                                    "ui-sans-serif", "ui-monospace", "ui-rounded", "emoji"});
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

std::string serialize_font_family(std::string_view text) {
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

} // namespace ctbrowser::style::css
