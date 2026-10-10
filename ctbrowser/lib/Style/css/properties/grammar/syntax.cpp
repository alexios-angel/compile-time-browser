//===- grammar/syntax.cpp - Function argument syntax ---------- -*- C++ -*-===//
//
// Small token-level helpers of the value grammar: length units, function
// names, CSSOM number serialisation, the random-item() argument grammar and
// classification of `<position>` components.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::style::css::grammar_detail {

/// Whether `unit` is a length unit, ASCII case-insensitively.
[[nodiscard]] bool is_length_unit(std::string_view unit) {
    return ascii_iequals_any(unit, length_units) || ascii_iequals_any(unit, more_length_units);
}

/// A function token's name, without the `(` the tokenizer keeps on it.
[[nodiscard]] std::string_view function_name(const token_stream & ts, const css_token & t) {
    const std::string_view raw = ts.text_of(t);
    return raw.empty() ? raw : raw.substr(0, raw.size() - 1);
}

// --- serialisation -------------------------------------------------------

/// A CSS number in shortest form: `0` rather than `-0`, `0.5` rather than
/// `0.500000`, and `1` rather than `1.0`, which is what CSSOM §6.7.2 means by
/// "the smallest number of digits".
///
/// `std::to_chars` in fixed format is exactly that: the shortest decimal that
/// reads back as the same double, with no exponent. (`std::to_string` would
/// print six fixed decimals, altering `0.1234567` and losing `1e-7`.)
[[nodiscard]] std::string number_text(double value) {
    if (!std::isfinite(value)) { return value > 0 ? "infinity" : "-infinity"; }
    if (value == 0) { return "0"; } // Catches -0, which serialises as 0.
    std::array<char, 400> buffer{};
    const std::to_chars_result written = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                                       value, std::chars_format::fixed);
    if (written.ec != std::errc{}) { return "0"; }
    return std::string{buffer.data(), static_cast<std::size_t>(written.ptr - buffer.data())};
}

/// Checks `random-item( <declaration-value>, [ <declaration-value>? ]# )`
/// (CSS Values 5 §funcdef-random-item) for the function token at `open`.
/// Three rules:
///
///  * The key is required, and so is the comma after it: `random-item()`,
///    `random-item(auto)` and `random-item(, serif)` are invalid. The items may
///    each be empty, so `random-item(auto,)` is fine.
///
///  * No unmatched bracket anywhere inside, which is what `<declaration-value>`
///    means. A depth counter cannot check this (`random-item(auto, {serif)`
///    closes a `{` with a `)`), so a stack records what each opener expects.
///    EOF is not an error, since CSS Syntax 3 §5.4.9 closes every open block.
///
///  * A `{}` block is a whole item. Braces are how an item containing a comma
///    is written, so `{Times, serif}` is one item and `{Times, serif} extra` is
///    not an item at all.
[[nodiscard]] bool random_item_arguments_ok(const token_stream & ts, std::size_t open) {
    std::vector<token_type> expect{token_type::close_paren};
    std::size_t arguments = 1; // The key, plus one per top-level comma.
    std::size_t in_item = 0;   // Significant tokens in the current argument.
    std::size_t blocks = 0;    // How many of those were `{}` blocks.
    bool key_empty = true;
    bool item_mixed = false;
    for (std::size_t j = open + 1; j < ts.tokens.size(); ++j) {
        const css_token & t = ts.tokens[j];
        if (t.type == token_type::eof) { break; }
        if (t.type == token_type::whitespace) { continue; }
        const bool top = expect.size() == 1;
        if (t.type == token_type::close_paren || t.type == token_type::close_square ||
            t.type == token_type::close_curly) {
            if (t.type != expect.back()) { return false; } // An unmatched bracket.
            expect.pop_back();
            if (expect.empty()) { break; } // The function's own `)`.
            if (expect.size() == 1 && t.type == token_type::close_curly) { ++blocks; }
            continue;
        }
        if (top && t.type == token_type::comma) {
            if (arguments == 1) { key_empty = in_item == 0; }
            item_mixed = item_mixed || (blocks != 0 && in_item != blocks);
            ++arguments;
            in_item = 0;
            blocks = 0;
            continue;
        }
        if (top && t.type == token_type::semicolon) { return false; }
        if (top && t.type == token_type::delim && ts.text_of(t) == "!") { return false; }
        if (top) { ++in_item; }
        if (t.type == token_type::function || t.type == token_type::open_paren) {
            expect.push_back(token_type::close_paren);
        } else if (t.type == token_type::open_square) {
            expect.push_back(token_type::close_square);
        } else if (t.type == token_type::open_curly) {
            expect.push_back(token_type::close_curly);
        }
    }
    if (arguments == 1) { key_empty = in_item == 0; }
    item_mixed = item_mixed || (blocks != 0 && in_item != blocks);
    return arguments >= 2 && !key_empty && !item_mixed;
}

/// Classifies one token as a `<position>` component and writes its
/// serialisation to `serialized`.
[[nodiscard]] position_axis position_axis_of(const token_stream & ts, const css_token & t,
                                             std::string & serialized) {
    if (t.type == token_type::ident) {
        const std::string_view word = ts.text_of(t);
        serialized = ascii_lower_copy(word);
        if (ascii_iequals(word, "center")) { return position_axis::center; }
        if (ascii_iequals(word, "left") || ascii_iequals(word, "right") ||
            ascii_iequals(word, "x-start") || ascii_iequals(word, "x-end")) {
            return position_axis::horizontal;
        }
        if (ascii_iequals(word, "top") || ascii_iequals(word, "bottom") ||
            ascii_iequals(word, "y-start") || ascii_iequals(word, "y-end")) {
            return position_axis::vertical;
        }
        return position_axis::none;
    }
    // A <length-percentage>, serialised as everything else here is: a unitless
    // zero is a length and gains its `px`, and a unit folds to lowercase.
    if (t.type == token_type::number && t.number == 0) {
        serialized = "0px";
        return position_axis::offset;
    }
    if (t.type == token_type::percentage) {
        serialized = number_text(t.number) + "%";
        return position_axis::offset;
    }
    if (t.type == token_type::dimension && is_length_unit(ts.unit_of(t))) {
        serialized = number_text(t.number) + ascii_lower_copy(ts.unit_of(t));
        return position_axis::offset;
    }
    return position_axis::none;
}

} // namespace ctbrowser::style::css::grammar_detail
