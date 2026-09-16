// `display`, CSS Display 3 §2: the two-value syntax and its short forms.
//
//   [ <display-outside> || <display-inside> ]
//   | [ <display-outside>? && [ flow | flow-root ]? && list-item ]
//   | <display-internal> | <display-box> | <display-legacy>
//
// §2.7 says the specified value serialises in the SHORTEST equivalent form:
// `block flow` is `block`, `inline flow-root` is `inline-block`, `flex block`
// is `flex`, and only the pairs with no single-keyword spelling - `block
// ruby`, `run-in flex`, `inline list-item` - keep two words, outside first.
// layout/values.hpp reads the result by its single-keyword forms and treats
// anything else as `block`, which is what every unlisted pair is.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace detail {

bool match_display(const token_stream & ts, const scan & found, std::string & out) {
    static constexpr std::string_view outsides = "block inline run-in";
    static constexpr std::string_view insides = "flow flow-root table flex grid ruby";
    static constexpr std::string_view alone =
        "table-row-group table-header-group table-footer-group table-row table-cell "
        "table-column-group table-column table-caption ruby-base ruby-text ruby-base-container "
        "ruby-text-container contents none inline-block inline-table inline-flex inline-grid "
        "-webkit-box -webkit-inline-box";
    std::string outside, inside;
    bool list_item = false;
    for (const std::size_t i : found.significant) {
        const css_token & t = ts.tokens[i];
        if (t.type != token_type::ident) { return false; }
        const std::string word = ascii_lower_copy(ts.text_of(t));
        if (found.significant.size() == 1 && has_keyword(alone, word)) {
            out = word;
            return true;
        }
        if (has_keyword(outsides, word)) {
            if (!outside.empty()) { return false; }
            outside = word;
        } else if (has_keyword(insides, word)) {
            if (!inside.empty()) { return false; }
            inside = word;
        } else if (word == "list-item") {
            if (list_item) { return false; }
            list_item = true;
        } else {
            return false;
        }
    }
    if (list_item) {
        // `list-item` takes a flow inside only; `flex list-item` is not a value.
        if (!inside.empty() && inside != "flow" && inside != "flow-root") { return false; }
        out = outside == "block" || outside.empty() ? "" : outside + " ";
        if (inside == "flow-root") { out += "flow-root "; }
        out += "list-item";
        return true;
    }
    if (outside.empty() && inside.empty()) { return false; }
    if (outside.empty()) { outside = "block"; }
    if (inside.empty()) { inside = "flow"; }
    if (outside == "block") {
        // `block flow` is `block`; `block <inside>` is the inside alone, but
        // `block ruby` keeps both because `ruby` alone is `inline ruby`.
        out = inside == "flow" ? "block" : (inside == "ruby" ? "block ruby" : inside);
        return true;
    }
    if (outside == "inline") {
        if (inside == "flow") {
            out = "inline";
        } else if (inside == "flow-root") {
            out = "inline-block";
        } else if (inside == "ruby") {
            out = "ruby";
        } else {
            out = "inline-" + inside;
        }
        return true;
    }
    out = inside == "flow" ? "run-in" : "run-in " + inside;
    return true;
}

} // namespace detail

} // namespace ctbrowser::style::css
