// check_declaration - the answer `el.style[p] = v` and `CSS.supports(p, v)`
// both need - and the `<supports-condition>` grammar over it.

#include "internal.hpp"

#include <ctbrowser/core/atom.hpp>
#include <ctbrowser/style/css/parser.hpp>

namespace ctbrowser::style::css {

using namespace detail;

namespace {

// `not X`, `X and Y`, `X or Y`, `(...)` and a bare `( p : v )`. Written over the
// raw text with a bracket-depth counter rather than over the token stream: the
// grammar is about the SHAPE of the parentheses, and the tokens have already
// thrown away which ones were adjacent to what.
[[nodiscard]] std::size_t find_top_level(std::string_view text, std::string_view word,
                                         std::size_t from) {
    int depth = 0;
    for (std::size_t i = from; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '(') { ++depth; }
        if (c == ')') { --depth; }
        if (depth != 0 || i == 0) { continue; }
        if (i + word.size() > text.size()) { break; }
        if (!ascii_iequals(text.substr(i, word.size()), word)) { continue; }
        const bool before = text[i - 1] == ' ' || text[i - 1] == ')';
        const std::size_t after_at = i + word.size();
        const bool after =
            after_at >= text.size() || text[after_at] == ' ' || text[after_at] == '(';
        if (before && after) { return i; }
    }
    return std::string_view::npos;
}

[[nodiscard]] bool condition(std::string_view text, int depth);

[[nodiscard]] bool leaf(std::string_view inner, int depth) {
    // A nested group - `((color: red))` - before a declaration, because a
    // declaration's property may not contain a parenthesis.
    const std::string_view body = trim(inner, html_whitespace);
    if (body.empty()) { return false; }
    if (body.front() == '(' || ascii_istarts_with(body, "not ")) {
        return condition(body, depth + 1);
    }
    if (find_top_level(body, "and", 0) != std::string_view::npos ||
        find_top_level(body, "or", 0) != std::string_view::npos) {
        return condition(body, depth + 1);
    }
    // `selector( <complex-selector> )`, CSS Conditional 4 §6.2: supported when
    // it parses. A SYNTAX error and a selector this engine cannot match are
    // different answers, and only the first is "not supported".
    if (ascii_istarts_with(body, "selector(") && body.back() == ')') {
        atom_table atoms;
        bool invalid = false;
        const stylesheet parsed =
            parse_selector_text(body.substr(9, body.size() - 10), atoms, invalid);
        return !invalid && !parsed.selectors.empty();
    }
    const std::size_t colon = body.find(':');
    if (colon == std::string_view::npos) { return false; }
    // `!important` is part of a <declaration> and does not change the answer, so
    // this leaf is one of the two places it is allowed.
    const std::string_view value = trim(body.substr(colon + 1), html_whitespace);
    const std::string_view name = trim(body.substr(0, colon), html_whitespace);
    return supports_declaration(name, value);
}

bool condition(std::string_view text, int depth) {
    // A page can nest these as deep as it likes; the recursion is bounded so a
    // hostile string cannot exhaust the C++ stack.
    if (depth > 32) { return false; }
    const std::string_view body = trim(text, html_whitespace);
    if (body.empty()) { return false; }

    if (ascii_istarts_with(body, "not ") || ascii_istarts_with(body, "not(")) {
        return !condition(body.substr(3), depth + 1);
    }
    for (const std::string_view op : {std::string_view{"and"}, std::string_view{"or"}}) {
        const std::size_t at = find_top_level(body, op, 0);
        if (at == std::string_view::npos) { continue; }
        const bool left = condition(body.substr(0, at), depth + 1);
        const bool right = condition(body.substr(at + op.size()), depth + 1);
        return op == "and" ? (left && right) : (left || right);
    }
    // `selector()` is a <supports-condition> of its own, parentheses or not.
    if (ascii_istarts_with(body, "selector(")) { return leaf(body, depth); }
    if (body.front() != '(' || body.back() != ')') { return false; }
    return leaf(body.substr(1, body.size() - 2), depth);
}

} // namespace

bool is_wide_keyword(std::string_view word) noexcept {
    return ascii_iequals_any(word, {"inherit", "initial", "unset", "revert", "revert-layer"});
}

value_check check_declaration(std::string_view property, std::string_view value,
                              bool allow_important) {
    std::string_view text = trim(value, html_whitespace);
    // `!important` COMES OFF FIRST, before a single token is looked at, because
    // everything below treats a `!` as proof the value is not a value. Split it
    // here and the rest of this function never has to know the difference.
    //
    // It is not optional for the caller to get right: a `style` attribute may
    // carry one and CSS syntax says so, so refusing it there would DROP the
    // declaration - a page whose inline `width: 100px !important` stopped
    // applying at all, which is a great deal worse than mis-reporting its
    // priority.
    bool important = false;
    if (allow_important) {
        const std::size_t bang = text.rfind('!');
        if (bang != std::string_view::npos &&
            ascii_iequals(trim(text.substr(bang + 1), html_whitespace), "important")) {
            important = true;
            text = trim(text.substr(0, bang), html_whitespace);
        }
    }
    // An EMPTY value removes the declaration, which is how `test_invalid_value`
    // clears the property before setting it and how a page turns one off. It is
    // reported as invalid because the two callers want the same thing from it:
    // store nothing.
    if (text.empty()) { return {}; }

    const token_stream ts = tokenize(text);
    const scan found = scan_tokens(ts);
    if (found.malformed || found.important || found.significant.empty()) { return {}; }

    const auto yes = [important, &found](std::string serialized) {
        return value_check{true, std::move(serialized), important, found.unknown_function,
                           found.substituted};
    };
    // THE AUTHOR'S BYTES, for every value this file does not model. A
    // re-serialised token stream is not the same string - `random-item(auto
    // ,serif)` comes back as `random-item(auto, serif)` - and `test_valid_value`
    // asserts the round-trip exactly, so normalising a value whose grammar is
    // unknown converts a passing test into a failing one for no gain.
    // Canonicalisation is for the values the table DOES model, where it is the
    // whole point.
    //
    // ...EXCEPT THE NUMBERS, STRINGS AND URLS IN THEM, which CSSOM §6.7.2 spells
    // one way wherever they stand: `.5%` is `0.5%` in a `background-position`
    // this table leaves freeform exactly as it is in a `width` it types. Only
    // those three token kinds are respelled; every other byte is the author's.
    // ...WITH THE BLOCKS EOF CLOSED WRITTEN OUT: a value kept as
    // `attr(data-foo type(<color>)` would swallow the declaration after it
    // when the block is serialised and parsed again (attr-all-types).
    std::string verbatim{text};
    verbatim.append(static_cast<std::size_t>(found.unclosed), ')');
    bool bad_url = false;
    std::string normalized = normalize_value_tokens(ts, text, &bad_url);
    if (bad_url) { return {}; }
    normalized.append(static_cast<std::size_t>(found.unclosed), ')');

    // A CSS-WIDE KEYWORD is valid for every property, including one this table
    // has never heard of, and serialises lowercased.
    if (found.significant.size() == 1) {
        const css_token & only = ts.tokens[found.significant.front()];
        if (only.type == token_type::ident && is_wide_keyword(ts.text_of(only))) {
            return yes(ascii_lower_copy(ts.text_of(only)));
        }
    }

    // A CUSTOM PROPERTY takes anything that tokenises, by definition (CSS
    // Variables 1 §2): its value is a token stream, not a value.
    if (property.starts_with("--")) { return yes(std::string{text}); }

    // A value holding var()/env()/attr() is valid by construction - what it
    // means is not known until substitution. Its ARGUMENT LIST is known now,
    // though, and two of the functions have one worth checking.
    if (!substitution_grammar_ok(ts)) { return {}; }
    if (found.substituted) { return yes(verbatim); }

    // A MALFORMED MATH FUNCTION KILLS THE DECLARATION WHEREVER IT SITS, and
    // that has to be asked before the property's own grammar because most of the
    // properties the corpus asks it about are `freeform` ones: `transform:
    // rotate(calc((0.25turn error)))` is one value of a syntax this table does
    // not model, wrapped around a calc() that is simply wrong. `calc/` owns
    // the question - it is the only thing here that knows what `round()` takes -
    // and it answers only about the functions it implements, so a `calc-size()`
    // is left alone rather than guessed at.
    if (!math_syntax_ok(text)) { return {}; }
    // ...AND SO DOES A NON-INTEGER LITERAL IN AN <integer> SLOT, in the handful
    // of freeform properties that have one (grammar.cpp).
    if (!integer_slots_ok(property, ts)) { return {}; }

    // ...AND A WELL FORMED ONE IS SIMPLIFIED WHEREVER IT SITS. CSS Values 4
    // §10.12 says a math function's specified value is its simplified form; it
    // does not say "when the function is the whole value", and the corpus tests
    // these functions through `transform`, `background-image` and `scale`, none
    // of which this table models. `calc/` owns the rule and keeps the author's
    // bytes for everything it cannot answer, so a value with no math in it and a
    // value whose math needs a font size both come back untouched.
    std::string simplified = may_have_math(normalized) ? simplify_math(normalized) : normalized;
    // ...AND A random() SPELLS ITS KEY, which needs the property's name.
    if (!property.starts_with("--") && simplified.find("random(") != std::string::npos) {
        simplified = canonical_random(simplified, property);
    }

    const property_syntax * p = find_property(property);
    // AN UNKNOWN PROPERTY IS STORED, NOT REFUSED. CSSOM says a page may set one
    // and read it back; refusing here would be a behaviour change for every
    // property this table has not reached yet, and the corpora write several.
    //
    // ...BUT ITS MATH IS STILL MATH, which is why the two questions above are
    // asked before this one rather than after it. `offset-rotate:
    // calc(sign(50%) * 1deg)` and `offset-path: ray(calc(sign(50%) * 1deg))` are
    // two properties this table has never heard of carrying an expression that
    // is a syntax error in every property there is, and `calc()` is simplified
    // by CSS Values 4 §10.12 wherever it stands - the table knowing the name is
    // not one of the conditions.
    if (p == nullptr) { return yes(simplified); }

    // A PERCENTAGE INSIDE A MATH FUNCTION IS STILL A PERCENTAGE, and this is the
    // half of §10.11's calculation context that only the table can supply.
    // `calc/` refuses one whose own answer has no percentages to resolve - an
    // angle, a time; this refuses one whose PROPERTY has none, which is
    // `border-left-width: min(1px, 0%)`, `font-weight: sign(10%)` and
    // `tab-size: abs(10%)`, the last failures of `minmax-length-invalid` and
    // `signs-abs-invalid`. It is asked before `freeform` because a freeform
    // property answers yes to it and the two orders are the same answer.
    if (!takes_percentage_of(p->kind) && math_uses_percentage(text)) { return {}; }

    // AN `<image>` LIST IS FREEFORM WITH ITS GRADIENTS CANONICALISED
    // (image.cpp): `linear-gradient(in srgb, red, blue)` drops the default
    // method and `radial-gradient(at bottom right, ...)` writes `at right
    // bottom`; a list with no gradient in it keeps the author's bytes.
    if (p->kind == k::freeform &&
        ascii_iequals_any(property, {"background-image", "mask-image", "border-image-source",
                                     "list-style-image"})) {
        std::string serialized;
        if (match_image_list(ts, found, serialized)) {
            if (serialized.empty()) { return {}; }
            return yes(std::move(serialized));
        }
    }

    // THE BOX ALIGNMENT LONGHANDS (alignment.cpp): `first baseline` is
    // `baseline`, `center legacy` is `legacy center`, `safe` needs a position.
    if (p->kind == k::freeform &&
        (ascii_istarts_with(property, "align-") || ascii_istarts_with(property, "justify-"))) {
        std::string serialized;
        if (match_alignment(property, ts, found, serialized)) { return yes(std::move(serialized)); }
        if (ascii_iequals_any(property, {"align-content", "justify-content", "align-items",
                                         "justify-items", "align-self", "justify-self"})) {
            return {};
        }
    }

    // THE KEYWORD COMBINATIONS (keywords.cpp): `overline underline` is
    // `underline overline`, `size style layout paint` is `strict`.
    if (p->kind == k::freeform) {
        std::string serialized;
        if (match_keywords(property, ts, found, serialized)) {
            if (serialized.empty()) { return {}; }
            return yes(std::move(serialized));
        }
    }

    // THE GRID GRAMMARS (grid.cpp): track lists, lines, areas and auto-flow.
    if (p->kind == k::freeform && ascii_istarts_with(property, "grid-")) {
        std::string serialized;
        if (match_grid(property, ts, found, serialized)) {
            if (serialized.empty()) { return {}; }
            return yes(std::move(serialized));
        }
    }

    // A `<filter-value-list>` (filter.cpp): `blur()` fills in its argument,
    // `grayscale(300%)` is `grayscale(100%)`, `blur(-1px)` is refused.
    if (p->kind == k::freeform && ascii_iequals_any(property, {"filter", "backdrop-filter"})) {
        std::string serialized;
        if (!match_filter_list(ts, found, serialized)) { return {}; }
        return yes(std::move(serialized));
    }

    // `font-family` IS FREEFORM WITH ONE EXTRA RULE: its strings are the one
    // place CSSOM unquotes a string on the way back out. `'Lucida Grande'`
    // reads back as `Lucida Grande`, and serialize-values asks for it.
    if (p->kind == k::freeform) {
        return yes(ascii_iequals(property, "font-family") ? serialize_font_family(simplified)
                                                          : simplified);
    }

    // A `<position>` IS THE ONE MULTI-COMPONENT VALUE THIS TABLE MODELS, so it
    // is asked before the single-token path: `object-position: 10%` is a whole
    // value and its canonical form is `10% center`, which no per-token matcher
    // can produce.
    //
    // A math function anywhere in it falls through to the author's bytes rather
    // than being refused. `object-position: calc(50% - 1px) center` is a
    // perfectly good position whose components this reader does not evaluate,
    // and refusing it would be exactly the 80%-right grammar this table exists
    // not to be.
    if (p->kind == k::color) {
        // The property's own keywords beside the colour: `invert` is CSS
        // 2.1's outline colour, `auto` is caret-color's and accent-color's.
        if (found.significant.size() == 1) {
            const css_token & only = ts.tokens[found.significant.front()];
            if (only.type == token_type::ident && has_keyword(p->keywords, ts.text_of(only))) {
                return yes(ascii_lower_copy(ts.text_of(only)));
            }
        }
        std::string serialized;
        if (match_color(ts, found, simplified, serialized)) { return yes(std::move(serialized)); }
        return {};
    }

    // `display` IS TWO KEYWORDS WITH A SHORT FORM (display.cpp): `inline
    // flow-root` is `inline-block` and `flow list-item` is `list-item`.
    if (ascii_iequals(property, "display")) {
        std::string serialized;
        if (match_display(ts, found, serialized)) { return yes(std::move(serialized)); }
        return {};
    }

    if (p->kind == k::position) {
        std::string serialized;
        if (match_position(ts, found, serialized)) { return yes(std::move(serialized)); }
        for (const std::size_t i : found.significant) {
            if (ts.tokens[i].type == token_type::function) { return yes(simplified); }
        }
        return {};
    }

    if (found.significant.size() == 1) {
        const css_token & only = ts.tokens[found.significant.front()];
        if (only.type == token_type::ident && has_keyword(p->keywords, ts.text_of(only))) {
            return yes(ascii_lower_copy(ts.text_of(only)));
        }
        std::string serialized;
        if (p->kind != k::keyword_only && match_typed(ts, only, *p, serialized)) {
            return yes(std::move(serialized));
        }
    }

    // A calc-size() HAS A GRAMMAR OF ITS OWN, and only a sizing property takes
    // one (CSS Values 5 §calc-size): the basis is judged against the
    // property's keywords and the calculation is simplified with `size` in it.
    if (p->kind == k::length_percentage && ascii_istarts_with(text, "calc-size(")) {
        std::optional<std::string> sized = calc_size_text(text, p->keywords);
        if (!sized) { return {}; }
        return yes(std::move(*sized));
    }

    // A math function over the whole value: `calc/` owns the evaluation and
    // has a third answer besides folded and invalid.
    //
    // ITS TYPE IS CHECKED HERE AND NOT ITS VALUE. What comes back is used only to
    // ask "is a <length> a value for this property", never to substitute an
    // answer - the simplification above already wrote the specified form, which
    // CSS Values 4 §10.12 keeps a function around: `el.style.width = 'calc(1px +
    // 2px)'` reads back as `calc(3px)` and not as `3px`. Folding to a used
    // number is the cascade's job, one layer up.
    if (p->kind != k::keyword_only && whole_value_is_math(ts, found)) {
        const math_answer answer = evaluate_math(text, length_context{});
        if (!math_type_fits(*p, answer, text)) { return {}; }
        return yes(simplified);
    }
    return {};
}

bool supports_declaration(std::string_view property, std::string_view value) {
    // `CSS.supports` asks about a property this engine implements, so an
    // unknown name is false here even though `el.style` stores it. That is the
    // one place the two callers differ and it is what the specification says:
    // §5 of CSS Conditional 3 is "would the declaration be dropped".
    if (property.starts_with("--")) { return !trim(value, html_whitespace).empty(); }
    if (find_property(property) == nullptr) { return false; }
    // `allow_important` is true because `@supports (color: red !important)` is a
    // <declaration> and the priority does not change the answer.
    const value_check checked = check_declaration(property, value, true);
    // ...AND A FUNCTION THIS ENGINE CANNOT EVALUATE IS NOT SUPPORT. `el.style`
    // still stores such a value - CSSOM says a page may - but a declaration
    // calling `attr()` or `random-item()` here really would be dropped by the
    // time anything rendered, and saying otherwise makes a test run and fail
    // where it should have skipped.
    return checked.valid && !checked.uses_unknown_function;
}

bool supports_condition(std::string_view text) {
    return condition(text, 0);
}

} // namespace ctbrowser::style::css
