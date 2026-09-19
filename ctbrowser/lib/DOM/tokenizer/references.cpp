#include <ctbrowser/dom/tokenizer.hpp>

#include <ctbrowser/core/algorithms.hpp>

namespace ctbrowser::html {
namespace {

// The 106 named references that decode WITHOUT a semicolon (the ones from
// HTML 4's Latin-1 set): the specification's table lists both spellings and
// the entity table here carries only the `;` ones.
constexpr std::string_view legacy_names[] = {
    "AElig",  "AMP",    "Aacute", "Acirc",  "Agrave", "Aring",  "Atilde", "Auml",   "COPY",
    "Ccedil", "ETH",    "Eacute", "Ecirc",  "Egrave", "Euml",   "GT",     "Iacute", "Icirc",
    "Igrave", "Iuml",   "LT",     "Ntilde", "Oacute", "Ocirc",  "Ograve", "Oslash", "Otilde",
    "Ouml",   "QUOT",   "REG",    "THORN",  "Uacute", "Ucirc",  "Ugrave", "Uuml",   "Yacute",
    "aacute", "acirc",  "acute",  "aelig",  "agrave", "amp",    "aring",  "atilde", "auml",
    "brvbar", "ccedil", "cedil",  "cent",   "copy",   "curren", "deg",    "divide", "eacute",
    "ecirc",  "egrave", "eth",    "euml",   "frac12", "frac14", "frac34", "gt",     "iacute",
    "icirc",  "iexcl",  "igrave", "iquest", "iuml",   "laquo",  "lt",     "macr",   "micro",
    "middot", "nbsp",   "not",    "ntilde", "oacute", "ocirc",  "ograve", "ordf",   "ordm",
    "oslash", "otilde", "ouml",   "para",   "plusmn", "pound",  "quot",   "raquo",  "reg",
    "sect",   "shy",    "sup1",   "sup2",   "sup3",   "szlig",  "thorn",  "times",  "uacute",
    "ucirc",  "ugrave", "uml",    "uuml",   "yacute", "yen",    "yuml"};

[[nodiscard]] bool is_legacy_name(std::string_view name) {
    return std::ranges::find(legacy_names, name) != std::ranges::end(legacy_names);
}

// "Numeric character reference end state": the C1 controls that Windows-1252
// put printable characters at, which is what an author who wrote `&#150;`
// meant.
struct c1_remap {
    std::uint32_t from;
    char32_t to;
};
constexpr c1_remap c1_table[] = {
    {0x80, 0x20AC}, {0x82, 0x201A}, {0x83, 0x0192}, {0x84, 0x201E}, {0x85, 0x2026}, {0x86, 0x2020},
    {0x87, 0x2021}, {0x88, 0x02C6}, {0x89, 0x2030}, {0x8A, 0x0160}, {0x8B, 0x2039}, {0x8C, 0x0152},
    {0x8E, 0x017D}, {0x91, 0x2018}, {0x92, 0x2019}, {0x93, 0x201C}, {0x94, 0x201D}, {0x95, 0x2022},
    {0x96, 0x2013}, {0x97, 0x2014}, {0x98, 0x02DC}, {0x99, 0x2122}, {0x9A, 0x0161}, {0x9B, 0x203A},
    {0x9C, 0x0153}, {0x9E, 0x017E}, {0x9F, 0x0178}};

} // namespace

// ============================================================================
// CHARACTER REFERENCES, 13.2.5.72-80
// ============================================================================

void tokenizer::character_reference(std::string & out, bool in_attribute) {
    const std::size_t start = at_;
    ++at_; // '&'
    if (eof()) {
        out += '&';
        return;
    }
    if (input_[at_] == '#') {
        // Numeric character reference state.
        ++at_;
        bool hex = false;
        if (!eof() && (input_[at_] == 'x' || input_[at_] == 'X')) {
            hex = true;
            ++at_;
        }
        std::uint32_t code = 0;
        bool any = false;
        while (!eof()) {
            const int digit = hex_value(input_[at_]);
            if (digit < 0 || (!hex && digit > 9)) { break; }
            if (code < 0x110000) {
                code = code * (hex ? 16u : 10u) + static_cast<std::uint32_t>(digit);
            }
            any = true;
            ++at_;
        }
        if (!any) {
            // absence-of-digits: flush `&#` (and the x) as text
            out += input_.substr(start, at_ - start);
            return;
        }
        if (!eof() && input_[at_] == ';') { ++at_; }
        out += encode_utf8(numeric_reference_code(code));
        return;
    }
    if (!is_alnum(input_[at_])) {
        out += '&';
        return;
    }
    // Named character reference state: the longest match in the table.
    std::size_t end = at_;
    while (end < input_.size() && is_alnum(input_[end])) { ++end; }
    const std::string_view run = input_.substr(at_, end - at_);
    const bool semicolon = end < input_.size() && input_[end] == ';';
    const html_entities::entity_ref * found = nullptr;
    std::size_t consumed = 0; // bytes after the `&`, the `;` included
    if (semicolon) {
        found = html_entities::find_entity(run);
        if (found != nullptr) { consumed = run.size() + 1; }
    }
    if (found == nullptr) {
        // The longest legacy name that is a prefix of the run.
        for (std::size_t length = std::min<std::size_t>(run.size(), 6);
             length > 0 && found == nullptr; --length) {
            const std::string_view prefix = run.substr(0, length);
            if (is_legacy_name(prefix)) {
                found = html_entities::find_entity(prefix);
                consumed = length;
            }
        }
    }
    if (found == nullptr) {
        // Not a reference: the `&` is text, and so is the run after it.
        // (An alphanumeric run with a `;` is the "ambiguous ampersand" parse
        // error - also text.)
        out += '&';
        return;
    }
    const bool with_semicolon = consumed > run.size();
    if (in_attribute && !with_semicolon) {
        // For historical reasons: in an attribute a legacy reference followed
        // by `=` or an alphanumeric is NOT a reference - `?a=1&copy=2`.
        const char next = at_ + consumed < input_.size() ? input_[at_ + consumed] : '\0';
        if (next == '=' || is_alnum(next)) {
            out += '&';
            return;
        }
    }
    at_ += consumed;
    out += encode_utf8(found->first);
    if (found->second != 0) { out += encode_utf8(found->second); }
}

char32_t tokenizer::numeric_reference_code(std::uint32_t code) {
    if (code == 0 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) { return 0xFFFD; }
    for (const c1_remap & entry : c1_table) {
        if (entry.from == code) { return entry.to; }
    }
    return static_cast<char32_t>(code);
}

std::string tokenizer::encode_utf8(char32_t code) {
    std::string out;
    append_utf8(out, code);
    return out;
}

} // namespace ctbrowser::html
