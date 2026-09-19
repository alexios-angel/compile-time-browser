#include "internal.hpp"

namespace ctbrowser::style::css::parser_detail {

[[nodiscard]] at_kind at_kind_of(std::string_view name) {
    // Vendor prefixes are stripped before the comparison, so `@-webkit-keyframes`
    // is the same at-rule as `@keyframes` - which matters because a sheet that
    // writes both would otherwise have the prefixed one skipped by a different
    // branch than the unprefixed one.
    for (const std::string_view prefix : {"-webkit-", "-moz-", "-ms-", "-o-"}) {
        if (name.size() > prefix.size() && ascii_istarts_with(name, prefix)) {
            name.remove_prefix(prefix.size());
            break;
        }
    }
    if (ascii_iequals(name, "media")) { return at_kind::media; }
    if (ascii_iequals(name, "supports")) { return at_kind::supports; }
    if (ascii_iequals(name, "layer")) { return at_kind::layer; }
    if (ascii_iequals(name, "scope")) { return at_kind::scope; }
    if (ascii_iequals(name, "container")) { return at_kind::container; }
    if (ascii_iequals(name, "document")) {
        // A conditional group whose contents are rules and whose condition
        // this engine cannot read: its rules apply.
        return at_kind::media_like;
    }
    if (ascii_iequals(name, "font-face")) { return at_kind::font_face; }
    if (ascii_iequals(name, "property")) { return at_kind::property; }
    if (ascii_iequals(name, "function")) { return at_kind::function; }
    if (ascii_iequals(name, "keyframes")) { return at_kind::keyframes; }
    if (ascii_iequals(name, "import") || ascii_iequals(name, "charset") ||
        ascii_iequals(name, "namespace")) {
        return at_kind::statement;
    }
    return at_kind::skip;
}

} // namespace ctbrowser::style::css::parser_detail
