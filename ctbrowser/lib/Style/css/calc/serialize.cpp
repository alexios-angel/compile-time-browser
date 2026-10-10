//===- serialize.cpp - Serializing calc() values -------------- -*- C++ -*-===//
//
// How a calc() answer is printed: a computed value through serialize_calc,
// and the inside of a symbolic calc() in the order of CSS Values 4 §10.13.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

/// Formats a double without trailing zeros, so a folded `12px` is not
/// `12.000000px`. CSS serialization drops them, as every engine's
/// getComputedStyle does.
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

/// The inside of a symbolic calc(), in CSS Values 4 §10.13's order: the
/// percentage first, then one term per unit sorted ASCII case-insensitively by
/// unit name (symbols are stored lowercase). `calc(10px + 1vmin + 10%)` is
/// `calc(10% + 10px + 1vmin)` in every browser; WPT
/// calc-dimension-serialization-order checks all forty-four relative units,
/// with `px` in its alphabetical place between `lvw` and `rcap` rather than
/// first.
///
/// An infinity or a NaN moves its unit out to a multiplier, as `serialize_calc`
/// does, because `NaNem` is not a token. That is only spellable when the whole
/// sum is that one term (`NaN * 1em + 1px` has no canonical form), so anything
/// else returns empty, which means "print the author's text instead" and is
/// never a reason to reject a declaration. A plain number beside the symbols
/// also returns empty, with one exception below.
[[nodiscard]] std::string serialize_symbolic(const term & value) {
    // The exception: a number beside a channel keyword is written, with the
    // number first as §10.13 sorts a sum: `calc(alpha + 0.1)` is
    // `calc(0.1 + alpha)`.
    const bool channels = std::ranges::any_of(
        value.symbols, [](const auto & one) { return is_number_symbol(one.first); });
    if (value.value != 0.0 && !(channels && value.is_number() && !value.has_percent)) { return {}; }
    const std::size_t parts =
        value.symbols.size() + (value.has_percent ? 1 : 0) + (value.value != 0.0 ? 1 : 0);
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
    // A negative zero keeps its sign: `sign(-0em)` is -0 and `sign(0em)` is 0,
    // and WPT signs-abs-computed observes the difference through
    // `1 / sign(...)`, so printing a specified `-0em` as `0em` changes the answer.
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
    if (value.value != 0.0) { append(value.value, ""); }
    if (value.has_percent) { append(value.percent, "%"); }
    for (const auto & [unit, coefficient] : sorted) {
        // A function term: `360deg * sibling-count()`, `2 * sibling-index()`,
        // or the bare function when nothing multiplies it. calc-size()'s `size`
        // counts as one for this purpose: `30px + (0.5 * size)`. A
        // relative colour's channel keyword is written the same way, without
        // the `$` the evaluator keys it by: `0.5 * r`, `1deg * h`.
        const std::size_t marker = unit.find('$');
        if (unit.ends_with("()") || unit == "size" || unit.ends_with("*size") ||
            marker != std::string::npos) {
            const std::size_t star = unit.find('*');
            const std::string_view dimension = star == std::string::npos
                                                   ? std::string_view{}
                                                   : std::string_view{unit}.substr(0, star);
            std::string_view function = star == std::string::npos
                                            ? std::string_view{unit}
                                            : std::string_view{unit}.substr(star + 1);
            if (function.starts_with('$')) { function.remove_prefix(1); }
            if (dimension.empty() && coefficient == 1.0) {
                if (!out.empty()) { out += " + "; }
                out += function;
                continue;
            }
            // A product inside a sum is parenthesized (§10.13):
            // `calc(10% + (10px * sibling-index()))`; alone it is the whole calc().
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
    // A percentage that is the whole answer prints alone, with no dimension
    // beside it: `50%`, not `calc(50% + 0px)`.
    const bool percent_only = value.has_percent && value.px == 0.0;
    const double lead = percent_only ? value.percent : value.px;
    // A number has no unit: `opacity: calc(2 / 4)` is `0.5`. Appending `px`
    // would produce a value layout and the cascade would both misread.
    const std::string_view unit = percent_only ? "%" : canonical_unit(value.type);

    // An infinity or a NaN cannot be written as a token, so CSS Values 4 §10.12
    // keeps the calc() around it and moves the unit out to a multiplier:
    // `calc(infinity)`, `calc(-infinity)`, `calc(NaN * 1px)`. `std::to_string`
    // would print `infpx` or `nan%`, which a page could not parse back.
    const auto word_of = [](double v) -> std::string {
        return std::isnan(v) ? "NaN" : (v > 0 ? "infinity" : "-infinity");
    };
    // A percentage beside it is kept, finite or not:
    // `calc(infinity * 1px - infinity * 1%)` is a NaN once the basis exists,
    // and printing only the length would make it infinite instead (WPT
    // calc-infinity-nan-computed).
    if (value.has_percent && !percent_only &&
        (!std::isfinite(value.px) || !std::isfinite(value.percent))) {
        const auto one = [&](double v, std::string_view u) {
            return std::isfinite(v) ? format_number(v) + std::string{u}
                                    : word_of(v) + " * 1" + std::string{u};
        };
        return "calc(" + one(value.percent, "%") + " + " + one(value.px, unit) + ")";
    }
    if (!std::isfinite(lead)) {
        const std::string word = word_of(lead);
        if (unit.empty()) { return "calc(" + word + ")"; }
        return "calc(" + word + " * 1" + std::string{unit} + ")";
    }
    if (!value.has_percent || percent_only) { return format_number(lead) + std::string{unit}; }
    // The two-term canonical form, with the sign folded into the operator the
    // way Chrome prints it: `calc(100% - 12px)`, never `calc(100% + -12px)`.
    const bool negative = value.px < 0;
    return "calc(" + format_number(value.percent) + "% " + (negative ? "- " : "+ ") +
           format_number(negative ? -value.px : value.px) +
           std::string{canonical_unit(value.type)} + ")";
}

std::string serialize_number(double value) {
    calc_result number;
    number.px = value;
    number.is_number = true;
    number.type = numeric_type::number;
    return serialize_calc(number);
}

} // namespace ctbrowser::style::css
