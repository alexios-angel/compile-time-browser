#include "substitute/internal.hpp"

namespace ctbrowser::style::css {

using namespace substitute_detail;

bool may_have_var(std::string_view value) noexcept {
    for (std::size_t i = 0; i + 3 <= value.size(); ++i) {
        if (i + 4 <= value.size() && ascii_iequals(value.substr(i, 4), "var(")) { return true; }
        if (i + 5 <= value.size() && ascii_iequals(value.substr(i, 5), "attr(")) { return true; }
        // `if(` and `ident(` at an identifier boundary: `notif(` and `--diff(`
        // are not it.
        const bool boundary = i == 0 || !is_name(value[i - 1]);
        if (boundary && ascii_iequals(value.substr(i, 3), "if(")) { return true; }
        if (boundary && i + 6 <= value.size() && ascii_iequals(value.substr(i, 6), "ident(")) {
            return true;
        }
        if (boundary && i + 12 <= value.size() &&
            ascii_iequals(value.substr(i, 12), "random-item(")) {
            return true;
        }
        if (boundary && i + 8 <= value.size() && ascii_iequals(value.substr(i, 8), "inherit(")) {
            return true;
        }
        // A DASHED FUNCTION - `--f(` - is a custom function call. `--diff(`
        // above is one too; what it is not is an `if()`.
        if (boundary && value.substr(i, 2) == "--") {
            std::size_t j = i + 2;
            while (j < value.size() && is_name(value[j])) { ++j; }
            if (j > i + 2 && j < value.size() && value[j] == '(') { return true; }
        }
    }
    return false;
}

std::optional<std::string> compute_registered(std::string_view text, std::string_view syntax,
                                              const length_context & ctx) {
    const std::optional<std::vector<syntax_alternative>> parsed = parse_syntax(syntax);
    // A syntax nothing here can read takes anything, as `*` does: refusing
    // every value for it would make the property's initial value the only one
    // it ever has.
    std::string matched;
    if (!parsed || parsed->empty()) {
        matched = std::string{trim(text, html_whitespace)};
    } else {
        const std::optional<std::string> found = match_syntax(text, *parsed);
        if (!found) { return std::nullopt; }
        matched = *found;
    }
    // Then computed like the type it names: a `calc()` folded, a dimension in
    // its canonical unit - `1em` under a 30px font is `30px`.
    if (may_have_math(matched)) {
        const folded_value done = fold_math(matched, ctx);
        if (!done.ok) { return std::nullopt; }
        matched = done.text;
    }
    if (const std::optional<std::string> canonical = canonical_dimension_text(matched, ctx)) {
        matched = *canonical;
    }
    return matched;
}

std::optional<std::string> substitute_var(std::string_view value, const custom_lookup & lookup,
                                          atom_table & atoms, const attribute_lookup & attributes,
                                          const condition_environment * conditions) {
    substituter engine{lookup, atoms, attributes, conditions};
    if (conditions != nullptr && conditions->property.starts_with("--")) {
        engine.resolving(atoms.intern(conditions->property));
    }
    std::string out;
    if (!engine.run(value, out, 0)) { return std::nullopt; }
    // A CYCLE IS NOT RESCUED BY A FALLBACK. CSS Variables 1 §3.1: every
    // property in a dependency cycle is invalid at computed-value time,
    // `var(--self, 3px)` included - the fallback is for a property that is
    // absent, not for one that cannot be computed (attr-argument-grammar).
    if (engine.reached_self()) { return std::nullopt; }
    if (introduced_structure(out)) { return std::nullopt; }
    return out;
}

} // namespace ctbrowser::style::css
