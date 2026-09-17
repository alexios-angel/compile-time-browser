// `<urange>` (CSS Syntax 3 §6), the value of the `unicode-range` descriptor:
// a comma-separated list, each item one of the token sequences the
// specification lists after a `u` - `+ <ident> ?*`, `<dimension> ?*`,
// `<number> ?*`, `<number> <dimension>`, `<number> <number>`, `+ ?+` - whose
// representations are concatenated and then read as U+ hex, `?` wildcards
// and an optional `-` end. Serialised as `U+X` or `U+X-Y`, uppercase, no
// leading zeros. urange-parsing.html walks every clause.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

// The text of one urange (after the leading `u`), or nullopt.
[[nodiscard]] std::optional<std::string> interpret(std::string_view text) {
    if (text.size() < 2 || text[0] != '+') { return std::nullopt; }
    std::size_t at = 1;
    std::string start;
    std::string end;
    while (at < text.size() && hex_value(text[at]) >= 0 && start.size() < 6) {
        start += text[at++];
    }
    std::size_t wildcards = 0;
    while (at < text.size() && text[at] == '?' && start.size() + wildcards < 6) {
        ++wildcards;
        ++at;
    }
    if (start.empty() && wildcards == 0) { return std::nullopt; }
    if (wildcards > 0) {
        if (at != text.size()) { return std::nullopt; }
        end = start;
        start.append(wildcards, '0');
        end.append(wildcards, 'F');
    } else if (at == text.size()) {
        end = start;
    } else {
        if (text[at] != '-') { return std::nullopt; }
        ++at;
        while (at < text.size() && hex_value(text[at]) >= 0 && end.size() < 6) {
            end += text[at++];
        }
        if (end.empty() || at != text.size()) { return std::nullopt; }
    }
    const auto number = [](std::string_view hex) {
        unsigned long out = 0;
        for (const char ch : hex) { out = out * 16 + static_cast<unsigned long>(hex_value(ch)); }
        return out;
    };
    const unsigned long first = number(start);
    const unsigned long last = number(end);
    if (first > last || last > 0x10FFFF) { return std::nullopt; }
    char buffer[32];
    const int n = first == last ? std::snprintf(buffer, sizeof buffer, "U+%lX", first)
                                : std::snprintf(buffer, sizeof buffer, "U+%lX-%lX", first, last);
    return std::string{buffer, buffer + n};
}

} // namespace

namespace detail {

bool match_unicode_range(const token_stream & ts, const scan & found, std::string & out) {
    std::string result;
    std::size_t i = 0;
    const auto & sig = found.significant;
    while (i < sig.size()) {
        // `u` or `U`, an ident on its own (`u+0` tokenises as ident + dimension).
        const css_token & u = ts.tokens[sig[i]];
        if (u.type != token_type::ident || !ascii_iequals(ts.text_of(u), "u")) { return false; }
        // The tokens that follow with NO whitespace between them, up to a
        // comma: the spec's sequences are all adjacent.
        std::string text;
        std::size_t k = sig[i] + 1;
        std::size_t consumed = 0;
        for (; k < ts.tokens.size(); ++k) {
            const css_token & t = ts.tokens[k];
            if (t.type == token_type::whitespace || t.type == token_type::comma ||
                t.type == token_type::eof) {
                break;
            }
            const bool part =
                (t.type == token_type::delim && (ts.text_of(t) == "+" || ts.text_of(t) == "?")) ||
                t.type == token_type::ident || t.type == token_type::number ||
                t.type == token_type::dimension;
            if (!part) { return false; }
            text += ts.text_of(t);
            ++consumed;
        }
        if (consumed == 0) { return false; }
        const std::optional<std::string> one = interpret(text);
        if (!one) { return false; }
        result += (result.empty() ? "" : ", ") + *one;
        // Advance past the consumed significant tokens, then the comma.
        i += 1 + consumed;
        if (i < sig.size()) {
            if (ts.tokens[sig[i]].type != token_type::comma || i + 1 >= sig.size()) {
                return false;
            }
            ++i;
        }
    }
    out = result;
    return !result.empty();
}

} // namespace detail

} // namespace ctbrowser::style::css
