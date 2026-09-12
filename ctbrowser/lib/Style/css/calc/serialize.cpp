// calc() - how an answer is printed: a computed value through serialize_calc,
// and the inside of a symbolic calc() in §10.13's order.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

// Trailing zeros off a double, so a folded `12px` is not `12.000000px`. CSS
// serialisation drops them and so does every engine's getComputedStyle.
[[nodiscard]] std::string format_number(double value) {
    if (value == std::floor(value) && std::fabs(value) < 1e9) {
        return std::to_string(static_cast<long long>(value));
    }
    std::string text = std::to_string(value);
    while (text.size() > 1 && text.back() == '0') { text.pop_back(); }
    if (!text.empty() && text.back() == '.') { text.pop_back(); }
    return text;
}

} // namespace

namespace detail {

// THE INSIDE OF A SYMBOLIC calc(), in CSS Values 4 §10.13's order: the
// percentage first, then one term per unit sorted ASCII case-insensitively by
// unit name. `calc(10px + 1vmin + 10%)` is `calc(10% + 10px + 1vmin)` in every
// browser, and `calc-dimension-serialization-order` walks all forty-four
// relative units to say so - `px` among them, in its alphabetical place between
// `lvw` and `rcap` rather than first for being the canonical one.
//
// AN INFINITY OR A NaN MOVES ITS UNIT OUT TO A MULTIPLIER, exactly as
// `serialize_calc` does it and for the same reason: `NaNem` is not a token.
// It is only spellable when the whole sum is that one term - `NaN * 1em + 1px`
// has no canonical form and there is no case for one - so anything else comes
// back EMPTY, which means "print the author's bytes instead" and is never a
// reason to condemn a declaration. A plain number sitting beside the symbols is
// the other such shape, and this file does not build it.
[[nodiscard]] std::string serialize_symbolic(const term & value) {
    if (value.value != 0.0) { return {}; }
    const std::size_t parts = value.symbols.size() + (value.has_percent ? 1 : 0);
    const bool finite =
        std::isfinite(value.percent) && std::ranges::all_of(value.symbols, [](const auto & one) {
            return std::isfinite(one.second);
        });
    if (!finite) {
        if (parts != 1) { return {}; }
        const double lead = value.has_percent ? value.percent : value.symbols.front().second;
        const std::string unit = value.has_percent ? "%" : value.symbols.front().first;
        const std::string word = std::isnan(lead) ? "NaN" : (lead > 0 ? "infinity" : "-infinity");
        return word + " * 1" + unit;
    }
    std::vector<std::pair<std::string, double>> sorted = value.symbols;
    std::ranges::sort(sorted, [](const auto & a, const auto & b) { return a.first < b.first; });
    std::string out;
    // The sign is folded into the operator the way every engine prints it:
    // `calc(100% - 12px)`, never `calc(100% + -12px)`.
    //
    // A NEGATIVE ZERO KEEPS ITS SIGN. `sign(-0em)` is -0 and `sign(0em)` is 0,
    // and `signs-abs-computed` reads the difference back through `1 / sign(...)`
    // - so a specified `-0em` that came back as `0em` changed the answer.
    const auto append = [&out](double n, std::string_view unit) {
        const bool negative = std::signbit(n);
        if (out.empty()) {
            if (negative) { out += '-'; }
            out += format_number(std::fabs(n));
        } else {
            out += negative ? " - " : " + ";
            out += format_number(std::fabs(n));
        }
        out += unit;
    };
    if (value.has_percent) { append(value.percent, "%"); }
    for (const auto & [unit, coefficient] : sorted) {
        // A FUNCTION TERM: `360deg * sibling-count()`, `2 * sibling-index()`,
        // or the bare function when nothing multiplies it.
        if (unit.ends_with("()")) {
            const std::size_t star = unit.find('*');
            const std::string_view dimension = star == std::string::npos
                                                   ? std::string_view{}
                                                   : std::string_view{unit}.substr(0, star);
            const std::string_view function = star == std::string::npos
                                                  ? std::string_view{unit}
                                                  : std::string_view{unit}.substr(star + 1);
            if (dimension.empty() && coefficient == 1.0) {
                if (!out.empty()) { out += " + "; }
                out += function;
                continue;
            }
            // A product inside a sum is parenthesised, §10.13: `calc(10% +
            // (10px * sibling-index()))`; alone it is the whole calc().
            const bool in_sum = parts > 1;
            if (in_sum) {
                out += out.empty() ? "(" : " + (";
                std::string one;
                std::swap(one, out);
                append(coefficient, dimension);
                std::swap(one, out);
                out += one;
            } else {
                append(coefficient, dimension);
            }
            out += " * ";
            out += function;
            if (in_sum) { out += ')'; }
            continue;
        }
        append(coefficient, unit);
    }
    return out;
}

} // namespace detail

std::string serialize_calc(const calc_result & value) {
    // A PERCENTAGE THAT IS THE WHOLE ANSWER prints as one, with no dimension
    // beside it; `50%` and not `calc(50% + 0px)`.
    const bool percent_only = value.has_percent && value.px == 0.0;
    const double lead = percent_only ? value.percent : value.px;
    // A NUMBER HAS NO UNIT. `opacity: calc(2 / 4)` is `0.5`, and appending `px`
    // to it would be a different kind of wrong from dropping it - a value layout
    // and the cascade would both happily misread.
    const std::string_view unit = percent_only ? "%" : canonical_unit(value.type);

    // AN INFINITY OR A NaN CANNOT BE WRITTEN AS A TOKEN, so CSS Values 4 §10.12
    // keeps the calc() around it and moves the unit out to a multiplier:
    // `calc(infinity)`, `calc(-infinity)`, `calc(NaN * 1px)`. Printing `infpx`
    // or `nan%` - which `std::to_string` would have done - is not a CSS value at
    // all, and a page reading it back gets something it cannot re-parse.
    if (!std::isfinite(lead)) {
        const std::string word = std::isnan(lead) ? "NaN" : (lead > 0 ? "infinity" : "-infinity");
        if (unit.empty()) { return "calc(" + word + ")"; }
        return "calc(" + word + " * 1" + std::string{unit} + ")";
    }
    if (!value.has_percent || percent_only) { return format_number(lead) + std::string{unit}; }
    // The two-term canonical form, with the sign folded into the operator the way
    // Chrome prints it: `calc(100% - 12px)`, never `calc(100% + -12px)`.
    const bool negative = value.px < 0;
    return "calc(" + format_number(value.percent) + "% " + (negative ? "- " : "+ ") +
           format_number(negative ? -value.px : value.px) +
           std::string{canonical_unit(value.type)} + ")";
}

} // namespace ctbrowser::style::css
