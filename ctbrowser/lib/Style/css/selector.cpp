#include "selector/internal.hpp"

namespace ctbrowser::style::css {

using namespace selector_detail;

bool known_pseudo_element(std::string_view name) {
    static constexpr std::string_view names[] = {"before",
                                                 "after",
                                                 "first-line",
                                                 "first-letter",
                                                 "marker",
                                                 "placeholder",
                                                 "selection",
                                                 "backdrop",
                                                 "file-selector-button",
                                                 "spelling-error",
                                                 "grammar-error",
                                                 "target-text",
                                                 "cue",
                                                 "details-content",
                                                 "view-transition",
                                                 "scroll-marker",
                                                 "scroll-marker-group",
                                                 "checkmark",
                                                 "picker-icon",
                                                 "column"};
    // A vendor-prefixed pseudo-element - `::-webkit-scrollbar`, `::-moz-selection` -
    // is whatever that vendor says it is, and every browser parses the others'.
    return ascii_iequals_any(name, names) || ascii_istarts_with(name, "-webkit-") ||
           ascii_istarts_with(name, "-moz-");
}

std::uint32_t parse_selector_list(stylesheet & sheet, std::span<const component_value> prelude,
                                  atom_table & atoms, bool * invalid,
                                  const nesting_context * nesting) {
    selector_parser parser{sheet, atoms};
    parser.set_nesting(nesting);
    const std::uint32_t written = parser.run(prelude);
    if (invalid && parser.invalid()) { *invalid = true; }
    return written;
}

} // namespace ctbrowser::style::css
