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
        return supports_selector(body.substr(9, body.size() - 10));
    }
    // `at-rule(<at-keyword-token>)`, `font-format(<ident>)` and
    // `font-tech(<ident>)` (CSS Conditional 5 §6, CSS Fonts 4 §11.1): one
    // token naming an at-rule, font format or font technology this engine
    // reads. `@charset` is not an at-rule, and a string or a list is no
    // answer at all.
    for (const auto & [function, words] :
         {std::pair<std::string_view, std::string_view>{
              "at-rule(", "media supports import namespace font-face keyframes layer container "
                          "scope counter-style page font-feature-values stylistic "
                          "historical-forms styleset character-variant swash ornaments annotation "
                          "property starting-style font-palette-values function"},
          {"font-format(", "collection opentype truetype woff woff2"},
          {"font-tech(", "features-opentype variations color-COLRv0 color-COLRv1"}}) {
        if (!ascii_istarts_with(body, function) || body.back() != ')') { continue; }
        const token_stream ts =
            tokenize(body.substr(function.size(), body.size() - function.size() - 1));
        const scan found = scan_tokens(ts);
        if (found.significant.size() != 1) { return false; }
        const css_token & t = ts.tokens[found.significant.front()];
        const bool at_rule = function == "at-rule(";
        if (t.type != (at_rule ? token_type::at_keyword : token_type::ident)) { return false; }
        return has_keyword(words, at_rule ? ts.value_of(t) : ts.text_of(t));
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
    // `selector()` and its kin are <supports-condition>s of their own,
    // parentheses or not.
    if (ascii_istarts_with(body, "selector(") || ascii_istarts_with(body, "at-rule(") ||
        ascii_istarts_with(body, "font-format(") || ascii_istarts_with(body, "font-tech(")) {
        return leaf(body, depth);
    }
    if (body.front() != '(' || body.back() != ')') { return false; }
    return leaf(body.substr(1, body.size() - 2), depth);
}

// `animation-name`: `[ none | <keyframes-name> ]#` with `<keyframes-name> =
// <custom-ident> | <string>` (CSS Animations 1 §5.1). A custom ident is not a
// CSS-wide keyword, `default` or `none`; a string is written back as the
// identifier it names, except one that spells a word an identifier could not
// be - `"none"`, `"initial"` - which stays a string.
[[nodiscard]] bool match_animation_name(const token_stream & ts, const scan & found,
                                        std::string & out) {
    bool want_name = true;
    for (const std::size_t i : found.significant) {
        const css_token & t = ts.tokens[i];
        if (!want_name) {
            if (t.type != token_type::comma) { return false; }
            want_name = true;
            continue;
        }
        want_name = false;
        const std::string_view text = ts.text_of(t);
        if (!out.empty()) { out += ", "; }
        if (t.type == token_type::ident) {
            if (is_wide_keyword(text) || ascii_iequals(text, "default")) { return false; }
            // A custom ident keeps its case and is written back escaped
            // (CSSOM §2.1): `multi\ word` round-trips as itself.
            out += ascii_iequals(text, "none") ? std::string{"none"} : serialize_identifier(text);
            continue;
        }
        if (t.type != token_type::string) { return false; }
        const std::string_view body = ts.value_of(t);
        if (body.empty()) { return false; }
        const bool reserved =
            is_wide_keyword(body) || ascii_iequals(body, "default") || ascii_iequals(body, "none");
        out += reserved ? string_text(body) : serialize_identifier(body);
    }
    return !want_name && !out.empty();
}

} // namespace

// `revert-rule` is Cascade 5's newest, and the cascade rolls it back like the
// other two (engine/cascade.cpp), so every property takes it.
bool is_wide_keyword(std::string_view word) noexcept {
    return ascii_iequals_any(
        word, {"inherit", "initial", "unset", "revert", "revert-layer", "revert-rule"});
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

    // A BLOCK EOF CLOSED IS CLOSED (CSS Syntax 3 §consume a component value).
    // The tokenizer closes it, so `scale: calc(sin(pi * sibling-index())` - one
    // paren short as written - is a perfectly good value; but every grammar
    // below reads the AUTHOR'S text through the token stream's pool, which is
    // the text as written, so `calc/` saw an unterminated function and called
    // the whole declaration a syntax error. The closers go on FIRST and the
    // value is read once, closed: seventeen assertions of css-values'
    // sin-cos-tan-computed and minmax-angle-computed are that shape, and the
    // serialisations below no longer have to append them by hand.
    std::string closed;
    {
        const token_stream probe = tokenize(text);
        const scan probed = scan_tokens(probe);
        if (probed.unclosed > 0) {
            closed = std::string{text};
            closed.append(static_cast<std::size_t>(probed.unclosed), ')');
            text = closed;
        }
    }

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

    // THE ONE DESCRIPTOR WITH A GRAMMAR OF ITS OWN: `unicode-range` is not a
    // property, and its `<urange>` is a token-level shape no property has.
    if (ascii_iequals(property, "unicode-range")) {
        std::string ranges;
        return match_unicode_range(ts, found, ranges) ? yes(std::move(ranges)) : value_check{};
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

    // THE GAP RULE LISTS (gaps.cpp): `10px, repeat(auto, thin)`, and the
    // BASIC SHAPES of clip-path and shape-outside (shapes.cpp).
    {
        std::string serialized;
        if (match_gap_list(property, text, serialized) ||
            match_basic_shape(property, simplified, serialized)) {
            if (serialized.empty()) { return {}; }
            return yes(std::move(serialized));
        }
    }

    // A `#` LIST IS THE PROPERTY'S OWN GRAMMAR, ONCE PER ITEM. Every longhand
    // of CSS Animations 1 and CSS Transitions 1 is a comma-separated list - one
    // value per animation, one per transitioned property - and this table has a
    // row per PROPERTY, not per item, so `animation-duration: 1s, 2s, 3s` was
    // refused by the very grammar that makes `1s` valid. The items are asked the
    // same question the whole value would have been, which keeps the two
    // answers one definition; only the `#` itself lives here.
    //
    // A CSS-WIDE KEYWORD IS NOT AN ITEM (CSS Values 4 §common-keywords): it is a
    // value for the WHOLE declaration or for nothing, which is what
    // `animation-duration: 1s, initial` asserts.
    if (p->kind != k::freeform && (ascii_istarts_with(property, "animation-") ||
                                   ascii_istarts_with(property, "transition-"))) {
        const std::vector<std::string_view> items = split_top_level(text, ",");
        if (items.size() > 1) {
            std::string list;
            for (const std::string_view item : items) {
                const value_check one = check_declaration(property, item, false);
                if (!one.valid || is_wide_keyword(one.serialized)) { return {}; }
                list += (list.empty() ? "" : ", ") + one.serialized;
            }
            return yes(std::move(list));
        }
    }

    if (ascii_iequals(property, "border-image") ||
        ascii_iequals_any(property, {"border-image-source", "border-image-slice",
                                     "border-image-width", "border-image-outset"})) {
        std::string serialized;
        if (!match_border_image(p->name, simplified, serialized)) { return {}; }
        return yes(std::move(serialized));
    }
    // A MASK BORDER IS A BORDER IMAGE whose image is a mask (CSS Masking 1
    // §8): the same four grammars, under other names and other initials.
    if (ascii_iequals_any(property, {"mask-border-source", "mask-border-slice", "mask-border-width",
                                     "mask-border-outset"})) {
        std::string serialized;
        const std::string as_border = "border-image-" + std::string{p->name.substr(12)};
        if (!match_border_image(as_border, simplified, serialized)) { return {}; }
        return yes(std::move(serialized));
    }

    // AN `<image>` LIST IS FREEFORM WITH ITS GRADIENTS CANONICALISED
    // (image.cpp): `linear-gradient(in srgb, red, blue)` drops the default
    // method and `radial-gradient(at bottom right, ...)` writes `at right
    // bottom`; a list with no gradient in it keeps the author's bytes.
    if (p->kind == k::freeform &&
        ascii_iequals_any(property, {"background-image", "mask-image", "list-style-image"})) {
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

    if (ascii_iequals(property, "background")) {
        std::string serialized;
        if (!match_background(simplified, serialized)) { return {}; }
        return yes(std::move(serialized));
    }

    // THE BACKGROUND AND MASK LAYER LISTS (backgrounds.cpp).
    if (ascii_istarts_with(property, "background-") || ascii_istarts_with(property, "mask-")) {
        std::string serialized;
        if (match_background_list(property, ts, found, serialized)) {
            if (serialized.empty()) { return {}; }
            return yes(std::move(serialized));
        }
    }

    // A SHADOW LIST (shadows.cpp): colour first, lengths, then `inset`.
    if (p->kind == k::freeform && ascii_iequals_any(property, {"box-shadow", "text-shadow"})) {
        std::string serialized;
        if (match_shadow_list(property, ts, found, serialized)) {
            if (serialized.empty()) { return {}; }
            return yes(std::move(serialized));
        }
    }

    // THE INDIVIDUAL TRANSFORMS AND THE ORIGINS (transforms.cpp).
    if (p->kind == k::freeform && ascii_iequals_any(property, {"rotate", "scale", "translate",
                                                               "transform", "transform-origin"})) {
        std::string serialized;
        if (match_transform_property(property, ts, found, serialized)) {
            if (serialized.empty()) { return {}; }
            return yes(std::move(serialized));
        }
    }

    // `animation-name`'s `[ none | <keyframes-name> ]#` (above): `12`, `one
    // two` and `one, initial` are refused, `"something"` is `something`.
    if (ascii_iequals(property, "animation-name")) {
        std::string serialized;
        if (!match_animation_name(ts, found, serialized)) { return {}; }
        return yes(std::move(serialized));
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

    if (p->name.ends_with("-radius") &&
        (p->name.starts_with("border-") || p->name.starts_with("-webkit-border-"))) {
        std::string serialized;
        if (!match_border_radius(simplified, p->shorthand, serialized)) { return {}; }
        return yes(std::move(serialized));
    }

    // `none | [ before | after ] [ links | tabs ]?`, CSS Overflow 5 §3.2: the
    // group's side, then how its markers behave - in that order
    // (scroll-markers-invalid.tentative).
    if (ascii_iequals(property, "scroll-marker-group")) {
        std::vector<std::string> words;
        for (const std::string_view part : split_top_level(simplified, html_whitespace)) {
            if (!trim(part, html_whitespace).empty()) {
                words.push_back(ascii_lower_copy(trim(part, html_whitespace)));
            }
        }
        if (words.size() == 1 && words[0] == "none") { return yes("none"); }
        if (words.empty() || words.size() > 2 || (words[0] != "before" && words[0] != "after") ||
            (words.size() == 2 && words[1] != "links" && words[1] != "tabs")) {
            return {};
        }
        return yes(words.size() == 1 ? words[0] : words[0] + " " + words[1]);
    }

    // `auto | spaces`, CSS Ruby 1 §4.2, `none` the legacy spelling of
    // `spaces` and parsed as it (ruby-overhang-valid).
    if (ascii_iequals(property, "ruby-overhang")) {
        const std::string_view word = trim(simplified, html_whitespace);
        if (ascii_iequals(word, "auto")) { return yes("auto"); }
        if (ascii_iequals(word, "spaces") || ascii_iequals(word, "none")) { return yes("spaces"); }
        return {};
    }

    // `auto | <length-percentage>{1,2}`, CSS Text Decoration 4 §2.7: how far
    // a line starts and ends inside its text, the end defaulting to the start
    // and written once when the two agree.
    if (ascii_iequals(property, "text-decoration-inset")) {
        std::vector<std::string_view> parts;
        for (const std::string_view part : split_top_level(simplified, html_whitespace)) {
            if (!trim(part, html_whitespace).empty()) { parts.push_back(part); }
        }
        if (parts.size() == 1 && ascii_iequals(trim(parts[0], html_whitespace), "auto")) {
            return yes("auto");
        }
        if (parts.empty() || parts.size() > 2) { return {}; }
        const property_syntax length{"", k::length_percentage, "", "", false, false};
        std::string ends[2];
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (!typed_component(parts[i], length, ends[i])) { return {}; }
        }
        if (parts.size() == 1 || ends[0] == ends[1]) { return yes(std::move(ends[0])); }
        return yes(ends[0] + " " + ends[1]);
    }

    // `<length-percentage> && hanging? && each-line?`, CSS Text 3 §8.1, the
    // keywords after the length in that order whatever the author's.
    if (ascii_iequals(property, "text-indent")) {
        static const property_syntax length{"", k::length_percentage, "", "", false, false};
        std::string indent;
        bool hanging = false, each_line = false;
        for (const std::string_view raw : split_top_level(simplified, html_whitespace)) {
            const std::string_view part = trim(raw, html_whitespace);
            if (part.empty()) { continue; }
            if (ascii_iequals(part, "hanging") && !hanging) {
                hanging = true;
            } else if (ascii_iequals(part, "each-line") && !each_line) {
                each_line = true;
            } else if (!indent.empty() || !typed_component(part, length, indent)) {
                return {};
            }
        }
        if (indent.empty()) { return {}; }
        return yes(indent + (hanging ? " hanging" : "") + (each_line ? " each-line" : ""));
    }

    // `<opacity-value>` (CSS Shapes 1 §5.1, CSS Color 4 §11.2, Filter Effects 1
    // §9.4), whose percentage is written as the number it is: `50%` is `0.5`
    // (opacity-valid, flood-opacity-valid).
    if (ascii_iequals_any(property, {"shape-image-threshold", "opacity", "flood-opacity"}) &&
        found.significant.size() == 1 &&
        ts.tokens[found.significant.front()].type == token_type::percentage) {
        return yes(serialize_number(ts.tokens[found.significant.front()].number / 100));
    }
    // ...and so a comparison over its percentages alone has an answer here,
    // the percentage of one it is: `min(50%, 0%)` is `calc(0%)`, where a
    // length's percentages could not be compared (opacity-valid). Only a
    // min(), max() or clamp() of percentages - `progress()` of them is a
    // ratio, not a percentage, and a constant like `pi` is no percentage.
    if (ascii_iequals_any(property, {"shape-image-threshold", "opacity", "flood-opacity"}) &&
        (ascii_istarts_with(simplified, "min(") || ascii_istarts_with(simplified, "max(") ||
         ascii_istarts_with(simplified, "clamp("))) {
        std::string as_numbers;
        bool only_percentages = true;
        bool any_percentage = false;
        for (const css_token & t : ts.tokens) {
            if (t.type == token_type::eof) { break; }
            if (t.type == token_type::number || t.type == token_type::dimension) {
                only_percentages = false;
            }
            any_percentage = any_percentage || t.type == token_type::percentage;
            as_numbers += t.type == token_type::percentage ? serialize_number(t.number / 100)
                                                           : std::string{ts.text_of(t)};
        }
        const std::string folded =
            only_percentages && any_percentage ? simplify_math(as_numbers) : std::string{};
        if (folded.starts_with("calc(") && folded.ends_with(')')) {
            double v = 0;
            const std::string_view inner{folded.data() + 5, folded.size() - 6};
            const auto [end, ec] = std::from_chars(inner.data(), inner.data() + inner.size(), v);
            if (ec == std::errc{} && end == inner.data() + inner.size()) {
                return yes("calc(" + serialize_number(v * 100) + "%)");
            }
        }
    }

    // `[ clip | ellipsis | <string> | fade | <fade()> ]{1,2}`, CSS Overflow 4
    // §6.1: one end, or the start and the end.
    if (ascii_iequals(property, "text-overflow")) {
        std::size_t count = 0;
        int depth = 0;
        for (const std::size_t i : found.significant) {
            const css_token & t = ts.tokens[i];
            const std::string_view word = ts.text_of(t);
            if (depth == 0) {
                const bool end = (t.type == token_type::ident &&
                                  ascii_iequals_any(word, {"clip", "ellipsis", "fade"})) ||
                                 t.type == token_type::string ||
                                 (t.type == token_type::function && ascii_iequals(word, "fade("));
                if (!end) { return {}; }
                ++count;
            }
            if (t.type == token_type::function || t.type == token_type::open_paren) { ++depth; }
            if (t.type == token_type::close_paren) { --depth; }
        }
        if (count == 0 || count > 2) { return {}; }
        return yes(simplified);
    }

    // `[ first | last ] || <'alignment-baseline'> || <'baseline-shift'>`, CSS
    // Inline 3 §4.4, written in that order with the defaults - `baseline`
    // and a zero shift - left out, and `baseline` when that is all of it.
    if (ascii_iequals(property, "vertical-align")) {
        static constexpr std::string_view alignments =
            "baseline text-bottom alphabetic ideographic middle central mathematical text-top "
            "hanging";
        static const property_syntax shift_syntax{
            "", k::length_percentage, "sub super top center bottom", "", false, false};
        std::string source, alignment, shift;
        for (const std::string_view raw : split_top_level(simplified, html_whitespace)) {
            const std::string_view part = trim(raw, html_whitespace);
            if (part.empty()) { continue; }
            if (source.empty() && ascii_iequals_any(part, {"first", "last"})) {
                source = ascii_lower_copy(part);
            } else if (alignment.empty() && has_keyword(alignments, part)) {
                alignment = ascii_lower_copy(part);
            } else if (!shift.empty() || !typed_component(part, shift_syntax, shift)) {
                return {};
            }
        }
        std::string out = source;
        const auto add = [&out](std::string_view part) {
            out += (out.empty() ? "" : " ") + std::string{part};
        };
        if (!alignment.empty() && alignment != "baseline") { add(alignment); }
        if (!shift.empty() && shift != "0px") { add(shift); }
        return yes(out.empty() ? std::string{"baseline"} : out);
    }

    // `<visual-box> || <length [0,∞]>`, CSS Overflow 4 §3.2: the box first,
    // `padding-box` (the default) and a zero length after a box left out,
    // and `0px` when that leaves nothing.
    if (ascii_iequals(property, "overflow-clip-margin")) {
        static const property_syntax length{"", k::length, "", "", false, true};
        std::string box, margin;
        for (const std::string_view raw : split_top_level(simplified, html_whitespace)) {
            const std::string_view part = trim(raw, html_whitespace);
            if (part.empty()) { continue; }
            if (box.empty() &&
                ascii_iequals_any(part, {"content-box", "padding-box", "border-box"})) {
                box = ascii_lower_copy(part);
            } else if (!margin.empty() || !typed_component(part, length, margin)) {
                return {};
            }
        }
        if (box.empty() && margin.empty()) { return {}; }
        if (box == "padding-box") { box.clear(); }
        if (!box.empty() && margin == "0px") { margin.clear(); }
        if (box.empty() && margin.empty()) { return yes("0px"); }
        return yes(box + (box.empty() || margin.empty() ? "" : " ") + margin);
    }

    // `[ auto | <integer> ]{1,3}`, CSS Text 4 §6.3: the word, and the least
    // before and after a hyphen. An omitted `before` is `auto` and an omitted
    // `after` is the `before`, so those are what the shortest form leaves out.
    if (ascii_iequals(property, "hyphenate-limit-chars")) {
        static const property_syntax integer{"", k::integer, "auto", "", false, false};
        std::vector<std::string> parts;
        for (const std::string_view raw : split_top_level(text, html_whitespace)) {
            if (trim(raw, html_whitespace).empty()) { continue; }
            std::string one;
            if (parts.size() == 3 || !typed_component(raw, integer, one)) { return {}; }
            parts.push_back(std::move(one));
        }
        if (parts.empty()) { return {}; }
        if (parts.size() == 3 && parts[2] == parts[1]) { parts.pop_back(); }
        if (parts.size() == 2 && parts[1] == "auto") { parts.pop_back(); }
        std::string out = parts[0];
        for (std::size_t i = 1; i < parts.size(); ++i) { out += " " + parts[i]; }
        return yes(std::move(out));
    }

    // `[ auto | <color> ]{1,2}`, CSS UI 4 §6.2.1: the caret's colour, and
    // the second its colour while it animates.
    if (ascii_iequals(property, "caret-color")) {
        std::string out;
        std::size_t count = 0;
        for (const std::string_view raw : split_top_level(text, html_whitespace)) {
            const std::string_view part = trim(raw, html_whitespace);
            if (part.empty()) { continue; }
            if (++count > 2) { return {}; }
            std::string one;
            if (ascii_iequals(part, "auto")) {
                one = "auto";
            } else {
                const value_check colour = check_declaration("text-decoration-color", part, false);
                if (!colour.valid || is_wide_keyword(colour.serialized)) { return {}; }
                one = colour.serialized;
            }
            out += (out.empty() ? "" : " ") + one;
        }
        if (count == 0) { return {}; }
        return yes(std::move(out));
    }

    if (ascii_iequals(property, "columns")) {
        std::vector<std::string> parts;
        return split_columns(simplified, parts) ? yes(simplified) : value_check{};
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
        // A PAINT'S url() - fill and stroke, SVG 2 §13.2: `<url> [ none |
        // <color> ]?`, the second being what a paint server that does not
        // load falls back to.
        if (ascii_iequals_any(property, {"fill", "stroke"}) && !found.significant.empty()) {
            const std::size_t first = found.significant.front();
            const css_token & head = ts.tokens[first];
            std::size_t after = first + 1;
            if (head.type == token_type::function && ascii_iequals(ts.text_of(head), "url(")) {
                int depth = 0;
                for (std::size_t j = first; j < ts.tokens.size(); ++j) {
                    const token_type type = ts.tokens[j].type;
                    if (type == token_type::function || type == token_type::open_paren) { ++depth; }
                    if (type == token_type::close_paren && --depth == 0) {
                        after = j + 1;
                        break;
                    }
                }
            } else if (head.type != token_type::url) {
                after = 0;
            }
            if (after != 0) {
                std::string out = normalize_value_tokens(ts, text, nullptr, first, after);
                const css_token & last = ts.tokens[after - 1];
                const std::size_t end = last.text + last.length;
                const std::string_view rest =
                    end <= ts.source_length
                        ? trim(std::string_view{ts.pool}.substr(end, ts.source_length - end),
                               html_whitespace)
                        : std::string_view{};
                if (rest.empty()) { return yes(std::move(out)); }
                if (ascii_iequals(rest, "none")) { return yes(out + " none"); }
                const token_stream fallback = tokenize(rest);
                const scan fallback_found = scan_tokens(fallback);
                std::string color;
                if (!fallback_found.malformed &&
                    match_color(fallback, fallback_found, rest, color)) {
                    return yes(out + " " + color);
                }
                return {};
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
        // ITS OWN KEYWORDS FIRST: `offset-anchor: auto` and `offset-position:
        // normal` are not positions, and no component of one is spelled that
        // way, so the whole-value matcher below can never answer for them.
        if (found.significant.size() == 1) {
            const css_token & only = ts.tokens[found.significant.front()];
            if (only.type == token_type::ident && has_keyword(p->keywords, ts.text_of(only))) {
                return yes(ascii_lower_copy(ts.text_of(only)));
            }
        }
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

    // `fit-content(<length-percentage [0,∞]>)`, CSS Sizing 3 §3.2: a sizing
    // property that takes the `fit-content` keyword takes the function too,
    // and its argument is that property's own <length-percentage> - never a
    // keyword and never a calc-size().
    if (p->kind == k::length_percentage && has_keyword(p->keywords, "fit-content") &&
        ascii_istarts_with(text, "fit-content(") && text.back() == ')') {
        // One typed component, so neither a keyword nor a nested function.
        const std::string_view given = trim(text.substr(12, text.size() - 13), html_whitespace);
        if (ascii_istarts_with(given, "calc-size(")) { return {}; }
        property_syntax argument = *p;
        argument.keywords = {};
        std::string inner;
        if (!typed_component(given, argument, inner)) { return {}; }
        return yes("fit-content(" + inner + ")");
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
    // `--` alone is no custom property name: CSS Variables 1 §2 reserves it.
    if (property.starts_with("--")) {
        return property.size() > 2 && !trim(value, html_whitespace).empty();
    }
    if (find_property(property) == nullptr) { return false; }
    // `allow_important` is true because `@supports (color: red !important)` is a
    // <declaration> and the priority does not change the answer.
    const value_check checked = check_declaration(property, value, true);
    // A SHORTHAND IS SUPPORTED WHEN IT SPLITS. check_declaration takes a
    // shorthand's text whole - the split is the declaration block's - so
    // `margin: foo` passed here while `el.style.margin = 'foo'` stored
    // nothing. The block's own split answers, on a scratch block.
    if (checked.valid && !checked.substituted && !longhands_of(property).empty()) {
        declaration_block scratch;
        if (!set_declaration(scratch, property, checked.serialized, false)) { return false; }
    }
    // ...AND A FUNCTION THIS ENGINE CANNOT EVALUATE IS NOT SUPPORT. `el.style`
    // still stores such a value - CSSOM says a page may - but a declaration
    // calling `attr()` or `random-item()` here really would be dropped by the
    // time anything rendered, and saying otherwise makes a test run and fail
    // where it should have skipped. A value with a `var()` in it is the
    // exception, whatever else it calls: it is valid at parse time by
    // definition (CSS Variables 1 §3), which is all the question asks.
    return checked.valid && (checked.substituted || !checked.uses_unknown_function);
}

bool supports_condition(std::string_view text) {
    // EOF CLOSES AN OPEN BLOCK (CSS Syntax 3 §5.4.9): `selector(a:hover` is
    // `selector(a:hover)`.
    const token_stream probe = tokenize(text);
    if (const scan found = scan_tokens(probe); found.unclosed > 0) {
        std::string closed{text};
        closed.append(static_cast<std::size_t>(found.unclosed), ')');
        return condition(closed, 0);
    }
    return condition(text, 0);
}

} // namespace ctbrowser::style::css
