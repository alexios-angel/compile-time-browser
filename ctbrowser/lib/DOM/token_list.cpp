#include <ctbrowser/dom/token_list.hpp>

#include <ctbrowser/core/algorithms.hpp>

#include <algorithm>
#include <utility>

namespace ctbrowser {

std::vector<std::string> parse_ordered_tokens(std::string_view text) {
    std::vector<std::string> out;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t start = text.find_first_not_of(html_whitespace, at);
        if (start == std::string_view::npos) { break; }
        std::size_t end = text.find_first_of(html_whitespace, start);
        if (end == std::string_view::npos) { end = text.size(); }
        std::string token{text.substr(start, end - start)};
        // ponytail: O(n²) for short attribute lists; hash dedup if measured hot.
        if (std::ranges::find(out, token) == out.end()) { out.push_back(std::move(token)); }
        at = end;
    }
    return out;
}

std::optional<token_error> validate_token(std::string_view token) {
    if (token.empty()) { return token_error::empty; }
    if (token.find_first_of(html_whitespace) != std::string_view::npos) {
        return token_error::whitespace;
    }
    return std::nullopt;
}

std::expected<bool, dom_error> update_tokens(document & doc, node_id element, atom attribute,
                                             std::span<const std::string> tokens) {
    if (tokens.empty() && !doc.read().has_attribute(element, attribute)) { return false; }
    std::string text;
    for (const std::string & token : tokens) {
        if (!text.empty()) { text += ' '; }
        text += token;
    }
    const auto written = doc.set_attribute(element, attribute, text);
    if (!written) { return std::unexpected{written.error()}; }
    return true;
}

std::expected<token_toggle_result, token_error> toggle_token(document & doc, node_id element,
                                                             atom attribute, std::string_view token,
                                                             std::optional<bool> force) {
    if (auto error = validate_token(token)) { return std::unexpected{*error}; }
    std::vector<std::string> tokens =
        parse_ordered_tokens(doc.read().attribute_value(element, attribute));
    const bool present = std::ranges::find(tokens, token) != tokens.end();
    if (force && *force == present) { return token_toggle_result{present, false}; }
    if (present) {
        std::erase(tokens, token);
    } else {
        tokens.emplace_back(token);
    }
    return token_toggle_result{!present, update_tokens(doc, element, attribute, tokens)};
}

} // namespace ctbrowser
