#include "parser/internal.hpp"

#include <ctbrowser/dom/encoding.hpp>

namespace ctbrowser::style::css {

using namespace parser_detail;

stylesheet parse_stylesheet(std::string_view css, atom_table & atoms) {
    parser p{css, atoms};
    return p.take_stylesheet();
}

std::string decode_stylesheet(std::string_view bytes, std::string_view protocol,
                              std::string_view environment) {
    // The decode algorithm's BOM sniff comes first and wins.
    if (bytes.starts_with("\xEF\xBB\xBF")) { return decode_document_bytes(bytes, "UTF-8"); }
    if (bytes.starts_with("\xFE\xFF")) { return decode_document_bytes(bytes, "UTF-16BE"); }
    if (bytes.starts_with("\xFF\xFE")) { return decode_document_bytes(bytes, "UTF-16LE"); }
    std::string_view fallback = encoding_from_label(protocol);
    // `@charset "` then ASCII other than `"`, then `";`, in the first 1024 bytes.
    constexpr std::string_view charset = "@charset \"";
    if (fallback.empty() && bytes.starts_with(charset)) {
        const std::string_view head = bytes.substr(0, 1024);
        const std::size_t close = head.find('"', charset.size());
        const std::string_view label = head.substr(charset.size(), close - charset.size());
        if (close != std::string_view::npos && head.substr(close).starts_with("\";") &&
            std::ranges::all_of(label,
                                [](char c) { return static_cast<unsigned char>(c) < 0x80; })) {
            fallback = encoding_from_label(label);
            if (fallback == "UTF-16BE" || fallback == "UTF-16LE") { fallback = "UTF-8"; }
        }
    }
    if (fallback.empty()) { fallback = encoding_from_label(environment); }
    return decode_document_bytes(bytes, fallback.empty() ? "UTF-8" : fallback);
}

stylesheet parse_selector_text(std::string_view text, atom_table & atoms, bool & invalid,
                               const std::vector<namespace_declaration> * namespaces,
                               const nesting_context * nesting) {
    parser p{text, atoms};
    return p.take_selector_list(invalid, namespaces, nesting);
}

bool supports_selector(std::string_view text) {
    atom_table atoms;
    parser p{text, atoms};
    bool invalid = false;
    const stylesheet parsed = p.take_selector_list(invalid, nullptr, nullptr, false);
    return !invalid && parsed.selectors.size() == 1;
}

std::string import_scope_prelude(std::string_view argument) {
    const std::string text{trim(argument, html_whitespace)};
    const token_stream s = tokenize(text);
    std::vector<const css_token *> significant;
    for (const css_token & t : s.tokens) {
        if (t.type == token_type::eof || significant.size() == 2) { break; }
        if (t.type != token_type::whitespace) { significant.push_back(&t); }
    }
    const auto is = [&](std::size_t i, token_type type) {
        return i < significant.size() && significant[i]->type == type;
    };
    const bool boundaries =
        is(0, token_type::open_paren) ||
        (is(0, token_type::ident) && ascii_iequals(s.text_of(*significant[0]), "to") &&
         is(1, token_type::open_paren));
    return text.empty() || boundaries ? text : "(" + text + ")";
}

stylesheet parse_declaration_list(std::string_view css, atom_table & atoms) {
    parser p{css, atoms};
    return p.take_declaration_list();
}

} // namespace ctbrowser::style::css
