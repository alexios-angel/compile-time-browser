//===- declaration.cpp - Declaration validation --------------- -*- C++ -*-===//
//
// Declaration validation and serialisation (check_declaration), shared by
// `el.style[p] = v` and `CSS.supports(p, v)`, plus the @supports condition
// grammar built on it.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

#include <ctbrowser/core/atom.hpp>
#include <ctbrowser/style/css/parser.hpp>

namespace ctbrowser::style::css {

using namespace detail;

namespace {

/// Finds `word` (`and`, `or`) at parenthesis depth zero in a @supports
/// condition, delimited by a space or parenthesis; returns npos if absent.
/// The condition grammar (`not X`, `X and Y`, `X or Y`, `(...)`, `( p : v )`)
/// is read from the raw text with a depth counter rather than the token
/// stream, because it is about the shape of the parentheses and the tokens no
/// longer record which ones were adjacent to what.
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
    // it parses. A syntax error and a selector this engine cannot match are
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

/// Matches `animation-name`: `[ none | <keyframes-name> ]#` with
/// `<keyframes-name> = <custom-ident> | <string>` (CSS Animations 1 §5.1). A
/// custom ident is not a CSS-wide keyword, `default` or `none`; a string is
/// written back as the identifier it names, except one that spells a word an
/// identifier could not be (`"none"`, `"initial"`), which stays a string.
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

/// True for a CSS-wide keyword. `revert-rule` (CSS Cascade 5) is included
/// because the cascade rolls it back like `revert` and `revert-layer`
/// (engine/cascade.cpp), so every property accepts it.
bool is_wide_keyword(std::string_view word) noexcept {
    return ascii_iequals_any(
        word, {"inherit", "initial", "unset", "revert", "revert-layer", "revert-rule"});
}

value_check check_declaration(std::string_view property, std::string_view value,
                              bool allow_important) {
    std::string_view text = trim(value, html_whitespace);
    // Strip `!important` before looking at any token, because everything below
    // treats a `!` as proof the value is invalid. A `style` attribute may carry
    // one (CSS syntax allows it), and refusing it would drop the declaration
    // entirely, which is far worse than mis-reporting its priority.
    bool important = false;
    if (allow_important) {
        const std::size_t bang = text.rfind('!');
        if (bang != std::string_view::npos &&
            ascii_iequals(trim(text.substr(bang + 1), html_whitespace), "important")) {
            important = true;
            text = trim(text.substr(0, bang), html_whitespace);
        }
    }
    // An empty value removes the declaration (how a page turns one off). It is
    // reported as invalid because both callers want the same result from it:
    // store nothing.
    if (text.empty()) { return {}; }

    // Blocks left open at EOF are closed (CSS Syntax 3), so append the missing
    // `)`s before parsing; every grammar below reads the author's text and would
    // otherwise see an unterminated function.
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
    // A value whose grammar this file does not model keeps the author's bytes:
    // re-serialising its tokens would change the string (`random-item(auto
    // ,serif)` would become `random-item(auto, serif)`), and CSSOM round-trips
    // must be exact. Canonicalisation is only for the values the table models.
    //
    // The exceptions are numbers, strings and URLs, which CSSOM §6.7.2 spells
    // one way wherever they stand (`.5%` is `0.5%`); normalize_value_tokens
    // respells only those three token kinds. Both forms also get the closers
    // for blocks left open at EOF, or a kept `attr(data-foo type(<color>)` would
    // swallow the next declaration when the block is serialised and reparsed.
    std::string verbatim{text};
    verbatim.append(static_cast<std::size_t>(found.unclosed), ')');
    bool bad_url = false;
    std::string normalized = normalize_value_tokens(ts, text, &bad_url);
    if (bad_url) { return {}; }
    normalized.append(static_cast<std::size_t>(found.unclosed), ')');

    // A CSS-wide keyword is valid for every property, including one this table
    // does not know, and serialises lowercased.
    if (found.significant.size() == 1) {
        const css_token & only = ts.tokens[found.significant.front()];
        if (only.type == token_type::ident && is_wide_keyword(ts.text_of(only))) {
            return yes(ascii_lower_copy(ts.text_of(only)));
        }
    }

    // A custom property takes anything that tokenises, by definition (CSS
    // Variables 1 §2): its value is a token stream, not a value.
    if (property.starts_with("--")) { return yes(std::string{text}); }

    // A value holding var()/env()/attr() is valid by construction, since what
    // it means is not known until substitution. Its argument list is known
    // now, though, and two of the functions have one worth checking.
    if (!substitution_grammar_ok(ts)) { return {}; }
    if (found.substituted) { return yes(verbatim); }

    // A malformed math function invalidates the declaration wherever it sits.
    // This is checked before the property's own grammar because most
    // properties the web-platform tests use here are `freeform` (this table
    // does not model their grammar): `transform: rotate(calc((0.25turn
    // error)))` wraps a broken calc() in an unmodelled value. `calc/` answers
    // only for the functions it implements, so a `calc-size()` is left alone.
    if (!math_syntax_ok(text)) { return {}; }
    // So does a non-integer literal in an <integer> slot, in the few freeform
    // properties that have one (grammar.cpp).
    if (!integer_slots_ok(property, ts)) { return {}; }

    // A well-formed math function is simplified wherever it sits: CSS Values 4
    // §10.12 makes the simplified form the specified value, not only when the
    // function is the whole value. `calc/` keeps the author's bytes for
    // anything it cannot answer, so a value with no math and a value whose math
    // needs a font size both come back untouched.
    std::string simplified = may_have_math(normalized) ? simplify_math(normalized) : normalized;
    // A random() also spells out its caching key, which needs the property name.
    if (!property.starts_with("--") && simplified.find("random(") != std::string::npos) {
        simplified = canonical_random(simplified, property);
    }

    // `unicode-range` is the one descriptor with a grammar of its own: it is not
    // a property, and its `<urange>` is a token-level shape no property has.
    if (ascii_iequals(property, "unicode-range")) {
        std::string ranges;
        return match_unicode_range(ts, found, ranges) ? yes(std::move(ranges)) : value_check{};
    }

    const property_syntax * p = find_property(property);
    // An unknown property is stored, not refused: CSSOM lets a page set one and
    // read it back, and pages set properties this table does not list yet. Its
    // math is still checked and simplified (above), because a malformed calc()
    // is invalid in every property and CSS Values 4 §10.12 simplifies calc()
    // wherever it stands.
    if (p == nullptr) { return yes(simplified); }

    // A percentage inside a math function needs a property that accepts
    // percentages (CSS Values 4 §10.11), so `font-weight: sign(10%)` and
    // `border-left-width: min(1px, 0%)` are invalid. `calc/` handles the other
    // half (a result type, such as an angle, with no percentage basis).
    if (!takes_percentage_of(p->kind) && math_uses_percentage(text)) { return {}; }

    // The gap rule lists (gaps.cpp), such as `10px, repeat(auto, thin)`, and
    // the basic shapes of clip-path and shape-outside (shapes.cpp).
    {
        std::string serialized;
        if (match_gap_list(property, text, serialized) ||
            match_basic_shape(property, simplified, serialized)) {
            if (serialized.empty()) { return {}; }
            return yes(std::move(serialized));
        }
    }

    // Animation and transition longhands are comma-separated lists (one value
    // per animation or transitioned property); the table has one row per
    // property, so each item is checked against that row recursively. A
    // CSS-wide keyword is valid only as the whole value (CSS Values 4
    // §common-keywords), so `animation-duration: 1s, initial` is invalid.
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
    // A mask border is a border image whose image is a mask (CSS Masking 1
    // §8): the same four grammars, under other names and other initials.
    if (ascii_iequals_any(property, {"mask-border-source", "mask-border-slice", "mask-border-width",
                                     "mask-border-outset"})) {
        std::string serialized;
        const std::string as_border = "border-image-" + std::string{p->name.substr(12)};
        if (!match_border_image(as_border, simplified, serialized)) { return {}; }
        return yes(std::move(serialized));
    }

    // An `<image>` list is freeform with its gradients canonicalised
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

    // The box alignment longhands (alignment.cpp): `first baseline` is
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

    // The background and mask layer lists (backgrounds.cpp).
    if (ascii_istarts_with(property, "background-") || ascii_istarts_with(property, "mask-")) {
        std::string serialized;
        if (match_background_list(property, ts, found, serialized)) {
            if (serialized.empty()) { return {}; }
            return yes(std::move(serialized));
        }
    }

    // A shadow list (shadows.cpp): colour first, lengths, then `inset`.
    if (p->kind == k::freeform && ascii_iequals_any(property, {"box-shadow", "text-shadow"})) {
        std::string serialized;
        if (match_shadow_list(property, ts, found, serialized)) {
            if (serialized.empty()) { return {}; }
            return yes(std::move(serialized));
        }
    }

    // The individual transforms and transform-origin (transforms.cpp).
    if (p->kind == k::freeform && ascii_iequals_any(property, {"rotate", "scale", "translate",
                                                               "transform", "transform-origin"})) {
        std::string serialized;
        if (match_transform_property(property, ts, found, serialized)) {
            if (serialized.empty()) { return {}; }
            return yes(std::move(serialized));
        }
    }

    // `animation-name` (match_animation_name above): `12`, `one two` and
    // `one, initial` are refused, `"something"` is `something`.
    if (ascii_iequals(property, "animation-name")) {
        std::string serialized;
        if (!match_animation_name(ts, found, serialized)) { return {}; }
        return yes(std::move(serialized));
    }

    // Keyword combinations (keywords.cpp): `overline underline` is
    // `underline overline`, `size style layout paint` is `strict`.
    if (p->kind == k::freeform) {
        std::string serialized;
        if (match_keywords(property, ts, found, serialized)) {
            if (serialized.empty()) { return {}; }
            return yes(std::move(serialized));
        }
    }

    // The grid grammars (grid.cpp): track lists, lines, areas and auto-flow.
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

    // `font-family` is freeform with one extra rule: its strings are the one
    // place CSSOM unquotes a string on the way back out, so `'Lucida Grande'`
    // reads back as `Lucida Grande`.
    if (p->kind == k::freeform) {
        return yes(ascii_iequals(property, "font-family") ? serialize_font_family(simplified)
                                                          : simplified);
    }

    // A <color> property: its own keywords first, then a paint's url()
    // fallback, then the colour grammar.
    if (p->kind == k::color) {
        // The property's own keywords beside the colour: `invert` is CSS
        // 2.1's outline colour, `auto` is caret-color's and accent-color's.
        if (found.significant.size() == 1) {
            const css_token & only = ts.tokens[found.significant.front()];
            if (only.type == token_type::ident && has_keyword(p->keywords, ts.text_of(only))) {
                return yes(ascii_lower_copy(ts.text_of(only)));
            }
        }
        // A paint's url() for fill and stroke, SVG 2 §13.2: `<url> [ none |
        // <color> ]?`, the second part being the fallback used when the paint
        // server does not load.
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

    // `display` is two keywords with short forms (display.cpp): `inline
    // flow-root` is `inline-block` and `flow list-item` is `list-item`.
    if (ascii_iequals(property, "display")) {
        std::string serialized;
        if (match_display(ts, found, serialized)) { return yes(std::move(serialized)); }
        return {};
    }

    // A `<position>` is the one multi-component value this table models, so it
    // is matched as a whole before the single-token path: `object-position:
    // 10%` serialises as `10% center`, which no per-token matcher can produce.
    //
    // A math function anywhere in it falls through to the author's bytes rather
    // than being refused: `object-position: calc(50% - 1px) center` is a valid
    // position whose components this reader does not evaluate.
    if (p->kind == k::position) {
        // The property's own keywords first: `offset-anchor: auto` and
        // `offset-position: normal` are not positions, so the whole-value
        // matcher below can never accept them.
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

    // A calc-size() has a grammar of its own, and only a sizing property takes
    // one (CSS Values 5 §calc-size): the basis is judged against the
    // property's keywords and the calculation is simplified with `size` in it.
    if (p->kind == k::length_percentage && ascii_istarts_with(text, "calc-size(")) {
        std::optional<std::string> sized = calc_size_text(text, p->keywords);
        if (!sized) { return {}; }
        return yes(std::move(*sized));
    }

    // A math function over the whole value: `calc/` evaluates it, and only its
    // type is checked here ("is a <length> valid for this property"). The
    // result is never substituted: the simplification above already wrote the
    // specified form, which CSS Values 4 §10.12 keeps wrapped in a function
    // (`calc(1px + 2px)` reads back as `calc(3px)`, not `3px`). Folding to a
    // used number is the cascade's job.
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
    // A shorthand is supported only when it splits into longhands.
    // check_declaration accepts a shorthand's text whole (splitting is the
    // declaration block's job), so `margin: foo` would pass here although
    // `el.style.margin = 'foo'` stores nothing; split it on a scratch block.
    if (checked.valid && !checked.substituted && !longhands_of(property).empty()) {
        declaration_block scratch;
        if (!set_declaration(scratch, property, checked.serialized, false)) { return false; }
    }
    // A function this engine cannot evaluate is not supported either.
    // `el.style` still stores such a value, as CSSOM requires, but it would be
    // dropped before rendering, so reporting support would make feature
    // detection lie. A value containing a substitution such as `var()` is the
    // exception: it is valid at parse time by definition (CSS Variables 1 §3).
    return checked.valid && (checked.substituted || !checked.uses_unknown_function);
}

bool supports_condition(std::string_view text) {
    // EOF closes an open block (CSS Syntax 3 §5.4.9): `selector(a:hover` is
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
