#include "parser/internal.hpp"

namespace ctbrowser::style::css {

using namespace parser_detail;

stylesheet parse_stylesheet(std::string_view css, atom_table & atoms) {
    parser p{css, atoms};
    return p.take_stylesheet();
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

stylesheet parse_declaration_list(std::string_view css, atom_table & atoms) {
    parser p{css, atoms};
    return p.take_declaration_list();
}

} // namespace ctbrowser::style::css
