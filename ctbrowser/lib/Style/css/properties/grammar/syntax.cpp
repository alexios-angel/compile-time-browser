#include "internal.hpp"

namespace ctbrowser::style::css::grammar_detail {

[[nodiscard]] bool is_length_unit(std::string_view unit) {
    return ascii_iequals_any(unit, length_units) || ascii_iequals_any(unit, more_length_units);
}

// A function token's name, without the `(` the tokenizer keeps on it.
[[nodiscard]] std::string_view function_name(const token_stream & ts, const css_token & t) {
    const std::string_view raw = ts.text_of(t);
    return raw.empty() ? raw : raw.substr(0, raw.size() - 1);
}

// --- serialisation -------------------------------------------------------

// A CSS number, shortest form. `0` rather than `-0`, `0.5` rather than
// `0.500000`, and `1` rather than `1.0` - which is what CSSOM §6.7.2 means by
// "the smallest number of digits", and what every `assert_equals(readValue,
// "1")` in the corpus compares against.
//
// `std::to_chars` in fixed format IS that definition: the shortest decimal that
// reads back as the same double, with no exponent. `std::to_string` was six
// fixed decimals, which printed `0.1234567` as `0.123457` - a value the author
// wrote, altered on the way to `el.style` - and could not say `1e-7` at all.
[[nodiscard]] std::string number_text(double value) {
    if (!std::isfinite(value)) { return value > 0 ? "infinity" : "-infinity"; }
    if (value == 0) { return "0"; } // catches -0, which serialises as 0
    std::array<char, 400> buffer{};
    const std::to_chars_result written = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                                       value, std::chars_format::fixed);
    if (written.ec != std::errc{}) { return "0"; }
    return std::string{buffer.data(), static_cast<std::size_t>(written.ptr - buffer.data())};
}

// AN ARBITRARY SUBSTITUTION FUNCTION HAS A GRAMMAR AT PARSE TIME even though
// what it MEANS has none until substitution, and two of them are tested here to
// the letter (CSS Values 5 §arbitrary-substitution):
//
//   ident( <declaration-value> )       one argument, and not an empty one
//   inherit( <custom-property-name> [, <declaration-value>]? )
//
// `ident()`, `ident( )`, `ident({})` and `ident(a, b)` are four assertions of
// `ident-function-parsing`; `inherit(, foo)` and `inherit(!!, foo)` are two of
// `inherit-function-parsing`. The other twenty-one assertions of those two files
// are values that must SURVIVE - `ident(rgb(1, 2, 3))` and `ident( myident)` and
// `inherit(--x,)` among them - so this is the grammar and nothing more, and in
// particular the argument is never re-serialised: the corpus asserts that
// `ident( myident)` keeps its space.
//
// It looks INSIDE other functions, because `calc(inherit(--x) + 1px)` is one of
// the values that must survive and `left: inherit(!!)` is not.
// `random-item( <declaration-value>, [ <declaration-value>? ]# )`, CSS Values 5
// §funcdef-random-item, and the ten remaining assertions of
// `css/css-values/random-item-invalid` are exactly this grammar.
//
// THREE RULES, and each one is a group of those assertions:
//
//  * The KEY is required and so is the comma after it. `random-item()`,
//    `random-item( )`, `random-item(auto)` and `random-item(, serif, sans-serif)`
//    are the four ways of getting that wrong. The ITEMS may each be empty -
//    `[ <declaration-value>? ]#` - so `random-item(auto,)` is fine.
//
//  * NO UNMATCHED BRACKET ANYWHERE INSIDE, which is what `<declaration-value>`
//    means and which a depth counter cannot answer: `random-item(auto, {serif)`
//    closes a `{` with a `)`, and counting brackets rather than MATCHING them
//    reads that as balanced. So this keeps a stack of what each opener expects.
//    EOF is not an error - CSS Syntax 3 §5.4.9 closes every open block - which is
//    why `random-item(auto, serif` is still a value.
//
//  * A `{}` BLOCK IS A WHOLE ITEM. Braces are how an item that contains a comma
//    is written, so `{Times, serif}` is one item and `{Times, serif} extra` is
//    not an item at all.
[[nodiscard]] bool random_item_arguments_ok(const token_stream & ts, std::size_t open) {
    std::vector<token_type> expect{token_type::close_paren};
    std::size_t arguments = 1; // the key, plus one per top-level comma
    std::size_t in_item = 0;   // significant tokens in the CURRENT argument
    std::size_t blocks = 0;    // ...and how many of them were `{}` blocks
    bool key_empty = true;
    bool item_mixed = false;
    for (std::size_t j = open + 1; j < ts.tokens.size(); ++j) {
        const css_token & t = ts.tokens[j];
        if (t.type == token_type::eof) { break; }
        if (t.type == token_type::whitespace) { continue; }
        const bool top = expect.size() == 1;
        if (t.type == token_type::close_paren || t.type == token_type::close_square ||
            t.type == token_type::close_curly) {
            if (t.type != expect.back()) { return false; } // an UNMATCHED bracket
            expect.pop_back();
            if (expect.empty()) { break; } // the function's own `)`
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
