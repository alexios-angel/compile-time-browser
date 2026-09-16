// The keyword-combination grammars: `a || b || c` over a few groups, with
// one or two words that stand alone. One table, one matcher:
//
//   text-decoration-line   none | [ underline || overline || line-through || blink ]
//   text-transform         none | math-auto | [ capitalize | uppercase | lowercase ]
//                          || full-width || full-size-kana
//   contain                none | strict | content | [ [size|inline-size] || layout
//                          || style || paint ]
//   font-synthesis, the font-variant-* longhands, text-underline-position,
//   hanging-punctuation
//
// ...and the three list grammars beside them: `will-change`, the counter
// properties, and `scroll-snap-align` / `scroll-snap-type`. A row says which
// words stand alone, the groups (one word from each, at most), whether the
// canonical form is the group order or the author's, and the spellings that
// collapse (`size style layout paint` is `strict`).

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

// `<custom-ident>` EXCLUDES THE CSS-WIDE KEYWORDS AND `default` (CSS Values 4
// §identifier-value). `revert-rule` is a CSS-wide keyword of CSS Cascade 6 that
// nothing else here implements, and it is excluded on the same grounds:
// `will-change: revert-rule` and `counter-reset: default 0` are two of those
// parsing files' assertions.
[[nodiscard]] bool reserved_ident(std::string_view word) {
    return is_wide_keyword(word) || ascii_iequals_any(word, {"default", "revert-rule"});
}

struct or_grammar {
    std::string_view property;
    std::string_view alone;                 // words valid only on their own
    std::array<std::string_view, 5> groups; // each a space-separated set; empty ends
    bool reorder;                           // canonical order is the group order
    std::string_view collapse_from = {};    // a full spelling that has a short form...
    std::string_view collapse_to = {};      // ...and the short form
    std::string_view collapse2_from = {};
    std::string_view collapse2_to = {};
};

constexpr or_grammar or_grammars[] = {
    {"text-decoration-line",
     "none spelling-error grammar-error",
     {"underline", "overline", "line-through", "blink"},
     true},
    {"text-transform",
     "none math-auto",
     {"capitalize uppercase lowercase", "full-width", "full-size-kana"},
     true},
    {"contain",
     "none strict content",
     {"size inline-size", "layout", "style", "paint"},
     true,
     "size layout style paint",
     "strict",
     "layout style paint",
     "content"},
    {"font-synthesis", "none", {"weight", "style", "small-caps", "position"}, true},
    {"font-variant-ligatures",
     "normal none",
     {"common-ligatures no-common-ligatures", "discretionary-ligatures no-discretionary-ligatures",
      "historical-ligatures no-historical-ligatures", "contextual no-contextual"},
     true},
    {"font-variant-numeric",
     "normal",
     {"lining-nums oldstyle-nums", "proportional-nums tabular-nums",
      "diagonal-fractions stacked-fractions", "ordinal", "slashed-zero"},
     true},
    {"font-variant-east-asian",
     "normal",
     {"jis78 jis83 jis90 jis04 simplified traditional", "full-width proportional-width", "ruby"},
     true},
    {"text-underline-position", "auto", {"from-font under", "left right"}, true},
    {"hanging-punctuation", "none", {"first", "force-end allow-end", "last"}, false},
    {"text-emphasis-position", "auto", {"over under", "right left"}, true},
    {"ruby-position", "inter-character", {"alternate", "over under"}, true},
    {"text-autospace",
     "normal auto no-autospace",
     {"ideograph-alpha", "ideograph-numeric", "punctuation", "insert replace"},
     true},
};

[[nodiscard]] std::optional<std::vector<std::string>> words_of(const token_stream & ts,
                                                               const scan & found) {
    std::vector<std::string> words;
    for (const std::size_t i : found.significant) {
        if (ts.tokens[i].type != token_type::ident) { return std::nullopt; }
        words.push_back(ascii_lower_copy(ts.text_of(ts.tokens[i])));
    }
    return words;
}

[[nodiscard]] std::optional<std::string> match_or(const or_grammar & g,
                                                  std::span<const std::string> words) {
    if (words.empty()) { return std::nullopt; }
    if (words.size() == 1 && has_keyword(g.alone, words.front())) { return words.front(); }
    std::array<std::string, 5> chosen;
    std::vector<std::string> in_order;
    for (const std::string & w : words) {
        bool placed = false;
        for (std::size_t k = 0; k < g.groups.size() && !g.groups[k].empty(); ++k) {
            if (!has_keyword(g.groups[k], w)) { continue; }
            if (!chosen[k].empty()) { return std::nullopt; }
            chosen[k] = w;
            placed = true;
            break;
        }
        if (!placed) { return std::nullopt; }
        in_order.push_back(w);
    }
    std::string out;
    if (g.reorder) {
        for (const std::string & c : chosen) {
            if (c.empty()) { continue; }
            out += (out.empty() ? "" : " ") + c;
        }
    } else {
        for (const std::string & w : in_order) { out += (out.empty() ? "" : " ") + w; }
    }
    if (out == g.collapse_from) { return std::string{g.collapse_to}; }
    if (out == g.collapse2_from) { return std::string{g.collapse2_to}; }
    return out;
}

// `auto | [ scroll-position | contents | <custom-ident> ]#`, as written.
[[nodiscard]] std::optional<std::string> will_change(const token_stream & ts, const scan & found) {
    std::string out;
    bool expect_item = true;
    for (const std::size_t i : found.significant) {
        const css_token & t = ts.tokens[i];
        if (expect_item) {
            if (t.type != token_type::ident) { return std::nullopt; }
            const std::string_view word = ts.text_of(t);
            if (ascii_iequals(word, "auto")) {
                if (found.significant.size() != 1) { return std::nullopt; }
                return "auto";
            }
            if (reserved_ident(word) || ascii_iequals_any(word, {"will-change", "none", "all"})) {
                return std::nullopt;
            }
            out += (out.empty() ? "" : ", ") + std::string{word};
            expect_item = false;
        } else {
            if (t.type != token_type::comma) { return std::nullopt; }
            expect_item = true;
        }
    }
    if (expect_item) { return std::nullopt; }
    return out;
}

// `none | [ <counter-name> <integer>? | <reversed-counter-name> <integer>? ]+`,
// the integer always written: `foo` is `foo 1` for an increment and `foo 0`
// for a reset or a set.
[[nodiscard]] std::optional<std::string> counters(std::string_view property,
                                                  const token_stream & ts, const scan & found) {
    const bool increment = ascii_iequals(property, "counter-increment");
    const bool reset = ascii_iequals(property, "counter-reset");
    std::string out;
    std::size_t k = 0;
    const std::vector<std::size_t> & at = found.significant;
    if (at.size() == 1 && ts.tokens[at[0]].type == token_type::ident &&
        ascii_iequals(ts.text_of(ts.tokens[at[0]]), "none")) {
        return "none";
    }
    while (k < at.size()) {
        const css_token & t = ts.tokens[at[k]];
        std::string name;
        bool reversed = false;
        if (t.type == token_type::ident) {
            const std::string_view word = ts.text_of(t);
            if (reserved_ident(word) || ascii_iequals(word, "none")) { return std::nullopt; }
            name = std::string{word};
            ++k;
        } else if (t.type == token_type::function && reset &&
                   ascii_iequals(ts.text_of(t), "reversed(")) {
            if (k + 2 >= at.size() || ts.tokens[at[k + 1]].type != token_type::ident ||
                ts.tokens[at[k + 2]].type != token_type::close_paren) {
                return std::nullopt;
            }
            const std::string_view word = ts.text_of(ts.tokens[at[k + 1]]);
            if (reserved_ident(word) || ascii_iequals(word, "none")) { return std::nullopt; }
            name = "reversed(" + std::string{word} + ")";
            reversed = true;
            k += 3;
        } else {
            return std::nullopt;
        }
        std::string count = increment ? "1" : "0";
        if (k < at.size() && ts.tokens[at[k]].type == token_type::number) {
            const css_token & n = ts.tokens[at[k]];
            if ((n.flags & flag_integer) == 0) { return std::nullopt; }
            count = serialize_number(n.number);
            ++k;
        } else if (k < at.size() && ts.tokens[at[k]].type == token_type::function &&
                   may_have_math(ts.text_of(ts.tokens[at[k]]))) {
            // A calc() integer, rounded (CSS Values 4 §10.10).
            std::size_t end = k;
            int depth = 0;
            for (std::size_t j = at[k]; j < ts.tokens.size(); ++j) {
                const token_type type = ts.tokens[j].type;
                if (type == token_type::function || type == token_type::open_paren) { ++depth; }
                if (type == token_type::close_paren && --depth == 0) {
                    while (end < at.size() && at[end] <= j) { ++end; }
                    break;
                }
            }
            if (end == k) { return std::nullopt; }
            const css_token & first = ts.tokens[at[k]];
            const css_token & last = ts.tokens[at[end - 1]];
            const std::string_view text =
                std::string_view{ts.pool}.substr(first.text, last.text + last.length - first.text);
            const math_answer answer = evaluate_math(text, length_context{});
            if (answer.outcome == math_outcome::invalid) { return std::nullopt; }
            if (answer.outcome == math_outcome::resolved) {
                if (!answer.value.is_number) { return std::nullopt; }
                count = serialize_number(std::round(answer.value.px));
            } else {
                count = simplify_math(text);
            }
            k = end;
        } else if (reversed) {
            // A reversed counter with no number counts down from its end.
            out += (out.empty() ? "" : " ") + name;
            continue;
        }
        out += (out.empty() ? "" : " ") + name + " " + count;
    }
    if (out.empty()) { return std::nullopt; }
    return out;
}

// scroll-snap-align: `[ none | start | end | center ]{1,2}`, one word when
// both are the same. scroll-snap-type: `none | [ x | y | block | inline |
// both ] [ mandatory | proximity ]?`, `proximity` being the default.
[[nodiscard]] std::optional<std::string> scroll_snap(std::string_view property,
                                                     std::span<const std::string> words) {
    if (words.empty() || words.size() > 2) { return std::nullopt; }
    if (ascii_iequals(property, "scroll-snap-align")) {
        for (const std::string & w : words) {
            if (!has_keyword("none start end center", w)) { return std::nullopt; }
        }
        if (words.size() == 2 && words[0] != words[1]) { return words[0] + " " + words[1]; }
        return words[0];
    }
    if (words[0] == "none") { return words.size() == 1 ? std::optional{words[0]} : std::nullopt; }
    if (!has_keyword("x y block inline both", words[0])) { return std::nullopt; }
    if (words.size() == 1 || words[1] == "proximity") { return words[0]; }
    if (words[1] != "mandatory") { return std::nullopt; }
    return words[0] + " mandatory";
}

// font-size-adjust: `none | [ ex-height | cap-height | ch-width | ic-width |
// ic-height ]? [ from-font | <number [0,inf]> ]`, the default `ex-height`
// dropped.
[[nodiscard]] std::optional<std::string> font_size_adjust(const token_stream & ts,
                                                          const scan & found) {
    if (found.significant.empty()) { return std::nullopt; }
    const css_token & first = ts.tokens[found.significant.front()];
    const css_token & last = ts.tokens[found.significant.back()];
    if (first.text >= ts.source_length || last.text >= ts.source_length) { return std::nullopt; }
    const std::string_view text =
        std::string_view{ts.pool}.substr(first.text, last.text + last.length - first.text);
    const std::vector<std::string_view> parts = split_top_level(text, " \t\n\r\f");
    if (parts.empty() || parts.size() > 2) { return std::nullopt; }
    std::size_t k = 0;
    std::string metric;
    const std::string word = ascii_lower_copy(parts[0]);
    if (word == "none") { return parts.size() == 1 ? std::optional{word} : std::nullopt; }
    if (has_keyword("ex-height cap-height ch-width ic-width ic-height", word)) {
        metric = word == "ex-height" ? "" : word + " ";
        k = 1;
    }
    if (k + 1 != parts.size()) { return std::nullopt; }
    const std::string_view value = parts[k];
    if (ascii_iequals(value, "from-font")) { return metric + "from-font"; }
    const token_stream sub = tokenize(value);
    if (sub.tokens.size() == 2 && sub.tokens.front().type == token_type::number) {
        if (sub.tokens.front().number < 0) { return std::nullopt; }
        return metric + serialize_number(sub.tokens.front().number);
    }
    if (sub.tokens.front().type == token_type::function && may_have_math(value)) {
        const math_answer answer = evaluate_math(value, length_context{});
        if (answer.outcome == math_outcome::invalid) { return std::nullopt; }
        if (answer.outcome == math_outcome::resolved && !answer.value.is_number) {
            return std::nullopt;
        }
        return metric + simplify_math(value);
    }
    return std::nullopt;
}

// The source text of a run of significant tokens, empty when any of it came
// from the decoded-escape tail of the pool rather than the author's bytes.
[[nodiscard]] std::string_view run_text(const token_stream & ts, std::span<const std::size_t> at) {
    if (at.empty()) { return {}; }
    const css_token & first = ts.tokens[at.front()];
    const css_token & last = ts.tokens[at.back()];
    if (first.text >= ts.source_length || last.text >= ts.source_length) { return {}; }
    return std::string_view{ts.pool}.substr(first.text, last.text + last.length - first.text);
}

// A MATH FUNCTION'S WHOLE BLOCK AS ONE COMPONENT, typed. `nullopt` when the
// cursor is not on a function, the block does not close, `calc/` calls it
// malformed, or it resolves to something other than `want`; `k` is left on the
// closing paren. It is what lets a property whose grammar is a list of
// keywords and one number still take `calc()` in the number's place.
[[nodiscard]] std::optional<std::string> math_component(const token_stream & ts, const scan & found,
                                                        std::size_t & k, numeric_type want) {
    if (ts.tokens[found.significant[k]].type != token_type::function) { return std::nullopt; }
    const std::size_t first = k;
    int depth = 0;
    for (; k < found.significant.size(); ++k) {
        const token_type type = ts.tokens[found.significant[k]].type;
        if (type == token_type::function || type == token_type::open_paren) { ++depth; }
        if (type == token_type::close_paren && --depth == 0) { break; }
    }
    if (k == found.significant.size()) { return std::nullopt; }
    const std::string_view text =
        run_text(ts, std::span<const std::size_t>{found.significant}.subspan(first, k - first + 1));
    if (text.empty() || !may_have_math(text)) { return std::nullopt; }
    const math_answer answer = evaluate_math(text, length_context{});
    if (answer.outcome == math_outcome::invalid) { return std::nullopt; }
    if (answer.outcome == math_outcome::resolved &&
        (answer.value.is_number || answer.value.type != want)) {
        return std::nullopt;
    }
    return simplify_math(text);
}

// `<custom-ident>` PROPERTIES: a word the property does not spell for itself.
// `keywords` are that property's own words - they are NOT custom idents, so
// `view-transition-class: foo none` is invalid - and `list` says whether more
// than one ident may follow. A custom ident KEEPS ITS CASE; a keyword
// lowercases, like every other keyword here.
struct ident_grammar {
    std::string_view property;
    std::string_view keywords;
    bool list;
};

constexpr ident_grammar ident_grammars[] = {
    {"page", "auto", false},
    {"view-transition-group", "normal contain nearest none", false},
    {"view-transition-class", "none", true},
};

[[nodiscard]] std::optional<std::string> custom_idents(const ident_grammar & g,
                                                       const token_stream & ts,
                                                       const scan & found) {
    std::string out;
    for (const std::size_t i : found.significant) {
        const css_token & t = ts.tokens[i];
        if (t.type != token_type::ident) { return std::nullopt; }
        const std::string_view word = ts.text_of(t);
        if (has_keyword(g.keywords, word)) {
            if (found.significant.size() != 1) { return std::nullopt; }
            return ascii_lower_copy(word);
        }
        if (reserved_ident(word)) { return std::nullopt; }
        if (!out.empty() && !g.list) { return std::nullopt; }
        out += (out.empty() ? "" : " ") + std::string{word};
    }
    if (out.empty()) { return std::nullopt; }
    return out;
}

// `<string>` PROPERTIES: one string, or one of the property's own keywords.
// `font-language-override` is the one with a rule about WHICH string: CSS Fonts
// 4 says an OpenType language system tag, which is one to four characters from
// the printable ASCII range, and its shortest serialisation drops the trailing
// spaces the tag is padded with (`"ENG "` is `"ENG"`, `" en "` is `" en"`).
struct string_grammar {
    std::string_view property;
    std::string_view keywords;
    bool opentype_tag;
};

constexpr string_grammar string_grammars[] = {
    {"hyphenate-character", "auto", false},
    {"block-ellipsis", "no-ellipsis ellipsis", false},
    {"font-language-override", "normal", true},
};

[[nodiscard]] std::optional<std::string> one_string(const string_grammar & g,
                                                    const token_stream & ts, const scan & found) {
    if (found.significant.size() != 1) { return std::nullopt; }
    const css_token & only = ts.tokens[found.significant.front()];
    if (only.type == token_type::ident) {
        const std::string_view word = ts.text_of(only);
        if (!has_keyword(g.keywords, word)) { return std::nullopt; }
        return ascii_lower_copy(word);
    }
    if (only.type != token_type::string) { return std::nullopt; }
    const std::string_view quoted = ts.text_of(only);
    if (quoted.size() < 2) { return std::nullopt; }
    std::string_view body = quoted.substr(1, quoted.size() - 2);
    if (g.opentype_tag) {
        if (body.empty() || body.size() > 4) { return std::nullopt; }
        for (const char c : body) {
            const auto code = static_cast<unsigned char>(c);
            if (code < 0x20 || code > 0x7E) { return std::nullopt; }
        }
        while (!body.empty() && body.back() == ' ') { body.remove_suffix(1); }
        if (body.empty()) { return std::nullopt; }
    }
    return string_text(body);
}

// `normal | [ light | dark | <custom-ident> ]+ && only?` (CSS Color Adjust 1).
// The `&&` is why `light only dark` is invalid where `only light dark` is not:
// `only` sits beside the whole list, never inside it, and serialises last.
[[nodiscard]] std::optional<std::string> color_scheme(std::span<const std::string> words) {
    if (words.empty()) { return std::nullopt; }
    if (words.size() == 1 && words.front() == "normal") { return words.front(); }
    std::string out;
    bool only = false;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::string & w = words[i];
        if (w == "only") {
            // Only at an end, and once: the list itself has to stay contiguous.
            if (only || (i != 0 && i + 1 != words.size())) { return std::nullopt; }
            only = true;
            continue;
        }
        if (w == "normal" || reserved_ident(w)) { return std::nullopt; }
        out += (out.empty() ? "" : " ") + w;
    }
    if (out.empty()) { return std::nullopt; }
    return only ? out + " only" : out;
}

// `auto | stable && both-edges?` (CSS Overflow 3): `both-edges` needs `stable`
// beside it, and the pair serialises in that order however it was written.
[[nodiscard]] std::optional<std::string> scrollbar_gutter(std::span<const std::string> words) {
    if (words.size() == 1 && words.front() == "auto") { return words.front(); }
    if (words.size() == 1 && words.front() == "stable") { return words.front(); }
    if (words.size() == 2 && ((words[0] == "stable" && words[1] == "both-edges") ||
                              (words[0] == "both-edges" && words[1] == "stable"))) {
        return "stable both-edges";
    }
    return std::nullopt;
}

// `none | all | [ digits <integer [2,4]>? ]` (CSS Writing Modes 4).
[[nodiscard]] std::optional<std::string> text_combine_upright(const token_stream & ts,
                                                              const scan & found) {
    const std::vector<std::size_t> & at = found.significant;
    if (at.empty() || at.size() > 2) { return std::nullopt; }
    if (ts.tokens[at[0]].type != token_type::ident) { return std::nullopt; }
    const std::string word = ascii_lower_copy(ts.text_of(ts.tokens[at[0]]));
    if (at.size() == 1 && (word == "none" || word == "all")) { return word; }
    if (word != "digits") { return std::nullopt; }
    if (at.size() == 1) { return word; }
    const css_token & n = ts.tokens[at[1]];
    if (n.type != token_type::number || (n.flags & flag_integer) == 0) { return std::nullopt; }
    if (n.number < 2 || n.number > 4) { return std::nullopt; }
    return word + " " + serialize_number(n.number);
}

// `[ auto | reverse ] || <angle>` (CSS Motion 1), the keyword written first.
[[nodiscard]] std::optional<std::string> offset_rotate(const token_stream & ts,
                                                       const scan & found) {
    const std::vector<std::size_t> & at = found.significant;
    if (at.empty()) { return std::nullopt; }
    std::string keyword;
    std::string angle;
    for (std::size_t k = 0; k < at.size(); ++k) {
        const css_token & t = ts.tokens[at[k]];
        if (t.type == token_type::ident) {
            const std::string word = ascii_lower_copy(ts.text_of(t));
            if (!keyword.empty() || (word != "auto" && word != "reverse")) { return std::nullopt; }
            keyword = word;
            continue;
        }
        if (!angle.empty()) { return std::nullopt; }
        if (t.type == token_type::function) {
            const std::optional<std::string> math =
                math_component(ts, found, k, numeric_type::angle);
            if (!math) { return std::nullopt; }
            angle = *math;
            continue;
        }
        if (t.type != token_type::dimension) { return std::nullopt; }
        const std::string_view unit = ts.unit_of(t);
        if (!ascii_iequals_any(unit, {"deg", "grad", "rad", "turn"})) { return std::nullopt; }
        angle = serialize_number(t.number) + ascii_lower_copy(unit);
    }
    if (keyword.empty() && angle.empty()) { return std::nullopt; }
    if (keyword.empty()) { return angle; }
    if (angle.empty()) { return keyword; }
    return keyword + " " + angle;
}

// One `<length-percentage>`, literal or a math function, canonical.
[[nodiscard]] std::optional<std::string> length_percentage(const token_stream & ts,
                                                           std::span<const std::size_t> at) {
    static constexpr property_syntax any_length{"", k::length_percentage, "", "", false, false};
    if (at.empty()) { return std::nullopt; }
    if (at.size() == 1) {
        std::string one;
        if (match_typed(ts, ts.tokens[at.front()], any_length, one)) { return one; }
        if (ts.tokens[at.front()].type != token_type::function) { return std::nullopt; }
    }
    const std::string_view text = run_text(ts, at);
    if (text.empty() || !may_have_math(text)) { return std::nullopt; }
    const math_answer answer = evaluate_math(text, length_context{});
    if (answer.outcome == math_outcome::invalid) { return std::nullopt; }
    if (answer.outcome == math_outcome::resolved &&
        (answer.value.is_number || answer.value.type != numeric_type::length)) {
        return std::nullopt;
    }
    return simplify_math(text);
}

// The comma-separated items of a value, each a run of significant tokens.
// `nullopt` for an empty item - a leading, trailing or doubled comma.
[[nodiscard]] std::optional<std::vector<std::vector<std::size_t>>> comma_items(
    const token_stream & ts, const scan & found) {
    std::vector<std::vector<std::size_t>> items{{}};
    for (const std::size_t i : found.significant) {
        if (ts.tokens[i].type == token_type::comma) {
            if (items.back().empty()) { return std::nullopt; }
            items.emplace_back();
            continue;
        }
        items.back().push_back(i);
    }
    if (items.back().empty()) { return std::nullopt; }
    return items;
}

// `[ normal | <length-percentage> | <timeline-range-name> <length-percentage>? ]#`
// (Scroll-driven Animations §animation-range). The offset that names the whole
// of the named range is dropped, which is 0% at the start and 100% at the end.
[[nodiscard]] std::optional<std::string> animation_range(std::string_view property,
                                                         const token_stream & ts,
                                                         const scan & found) {
    const std::optional<std::vector<std::vector<std::size_t>>> items = comma_items(ts, found);
    if (!items) { return std::nullopt; }
    const std::string_view whole = ascii_iequals(property, "animation-range-start") ? "0%" : "100%";
    std::string out;
    for (const std::vector<std::size_t> & item : *items) {
        std::string one;
        std::size_t k = 0;
        if (ts.tokens[item.front()].type == token_type::ident) {
            const std::string word = ascii_lower_copy(ts.text_of(ts.tokens[item.front()]));
            if (word == "normal") {
                if (item.size() != 1) { return std::nullopt; }
                one = word;
            } else if (has_keyword("cover contain entry exit entry-crossing exit-crossing", word)) {
                one = word;
                k = 1;
            } else {
                return std::nullopt;
            }
        }
        if (one != "normal" && k < item.size()) {
            const std::optional<std::string> offset =
                length_percentage(ts, std::span<const std::size_t>{item}.subspan(k));
            if (!offset) { return std::nullopt; }
            if (one.empty()) {
                one = *offset;
            } else if (*offset != whole) {
                one += " " + *offset;
            }
        } else if (one.empty()) {
            return std::nullopt;
        }
        out += (out.empty() ? "" : ", ") + one;
    }
    return out;
}

// `[ from-image || <resolution> ] && snap?` (CSS Images 4). The `&&` is why
// `3dpi snap from-image` is invalid and `snap 3dpi from-image` is not: `snap`
// sits beside the pair, never inside it. Written as the author wrote it.
[[nodiscard]] std::optional<std::string> image_resolution(const token_stream & ts,
                                                          const scan & found) {
    std::string out;
    bool from_image = false;
    bool resolution = false;
    std::size_t snap_at = found.significant.size();
    for (std::size_t k = 0; k < found.significant.size(); ++k) {
        const css_token & t = ts.tokens[found.significant[k]];
        if (t.type == token_type::ident) {
            const std::string word = ascii_lower_copy(ts.text_of(t));
            if (word == "snap") {
                if (snap_at != found.significant.size()) { return std::nullopt; }
                snap_at = k;
            } else if (word == "from-image" && !from_image) {
                from_image = true;
            } else {
                return std::nullopt;
            }
            out += (out.empty() ? "" : " ") + word;
            continue;
        }
        if (resolution) { return std::nullopt; }
        // A MATH FUNCTION IS A `<resolution>` TOO, and image-resolution is the
        // property css/css-values puts every resolution-typed calc() through
        // (numeric-testcommon.js picks it for `type:'resolution'`), so the
        // whole of the function's block is one component here.
        if (t.type == token_type::function) {
            const std::optional<std::string> math =
                math_component(ts, found, k, numeric_type::resolution);
            if (!math) { return std::nullopt; }
            resolution = true;
            out += (out.empty() ? "" : " ") + *math;
            continue;
        }
        if (t.type != token_type::dimension) { return std::nullopt; }
        const std::string_view unit = ts.unit_of(t);
        if (!ascii_iequals_any(unit, {"dpi", "dpcm", "dppx", "x"})) { return std::nullopt; }
        resolution = true;
        out += (out.empty() ? "" : " ") + serialize_number(t.number) + ascii_lower_copy(unit);
    }
    if (!from_image && !resolution) { return std::nullopt; }
    if (snap_at != found.significant.size() && snap_at != 0 &&
        snap_at + 1 != found.significant.size()) {
        return std::nullopt;
    }
    return out;
}

// `auto | rect( [ <length> | auto ]#{4} )` (CSS Masking 1, the CSS 2.1
// property). The comma-separated form is the only one: `rect(10px 20px, 30px
// 40px)` is a syntax error however many engines once took it.
[[nodiscard]] std::optional<std::string> clip_rect(const token_stream & ts, const scan & found) {
    const std::vector<std::size_t> & at = found.significant;
    if (at.size() == 1 && ts.tokens[at[0]].type == token_type::ident &&
        ascii_iequals(ts.text_of(ts.tokens[at[0]]), "auto")) {
        return "auto";
    }
    if (at.size() < 2 || ts.tokens[at.front()].type != token_type::function ||
        !ascii_iequals(ts.text_of(ts.tokens[at.front()]), "rect(") ||
        ts.tokens[at.back()].type != token_type::close_paren) {
        return std::nullopt;
    }
    static constexpr property_syntax any_length{"", k::length, "", "", false, false};
    std::string out{"rect("};
    std::size_t sides = 0;
    std::size_t commas = 0;
    bool want_side = true;
    for (std::size_t k = 1; k + 1 < at.size(); ++k) {
        const css_token & t = ts.tokens[at[k]];
        if (t.type == token_type::comma) {
            if (want_side) { return std::nullopt; }
            ++commas;
            want_side = true;
            continue;
        }
        // THE SEPARATORS ARE UNIFORM. `rect(10px, 20px, 30px, 40px)` is CSS
        // Masking 1's `[ <length> | auto ]#{4}` and `rect(0 0 0 0)` is CSS 2.1's
        // comma-less form that every engine still takes; `rect(10px 20px, 30px
        // 40px)` is neither, and css-masking/parsing/clip-invalid says so.
        if (!want_side && commas != 0) { return std::nullopt; }
        std::string one;
        if (t.type == token_type::ident && ascii_iequals(ts.text_of(t), "auto")) {
            one = "auto";
        } else if (!match_typed(ts, t, any_length, one)) {
            return std::nullopt;
        }
        out += (sides == 0 ? "" : ", ") + one;
        ++sides;
        want_side = false;
    }
    if (want_side || sides != 4 || (commas != 0 && commas != 3)) { return std::nullopt; }
    return out + ")";
}

} // namespace

namespace detail {

bool match_keywords(std::string_view property, const token_stream & ts, const scan & found,
                    std::string & out) {
    std::optional<std::string> answer;
    bool handled = false;
    for (const or_grammar & g : or_grammars) {
        if (!ascii_iequals(g.property, property)) { continue; }
        handled = true;
        const std::optional<std::vector<std::string>> words = words_of(ts, found);
        if (words) { answer = match_or(g, *words); }
    }
    if (!handled && ascii_iequals(property, "font-size-adjust")) {
        handled = true;
        answer = font_size_adjust(ts, found);
    }
    if (!handled && ascii_iequals(property, "will-change")) {
        handled = true;
        answer = will_change(ts, found);
    }
    if (!handled &&
        ascii_iequals_any(property, {"counter-increment", "counter-reset", "counter-set"})) {
        handled = true;
        answer = counters(property, ts, found);
    }
    if (!handled && (ascii_iequals(property, "scroll-snap-align") ||
                     ascii_iequals(property, "scroll-snap-type"))) {
        handled = true;
        const std::optional<std::vector<std::string>> words = words_of(ts, found);
        if (words) { answer = scroll_snap(property, *words); }
    }
    for (const ident_grammar & g : ident_grammars) {
        if (handled || !ascii_iequals(g.property, property)) { continue; }
        handled = true;
        answer = custom_idents(g, ts, found);
    }
    for (const string_grammar & g : string_grammars) {
        if (handled || !ascii_iequals(g.property, property)) { continue; }
        handled = true;
        answer = one_string(g, ts, found);
    }
    if (!handled && ascii_iequals_any(property, {"color-scheme", "scrollbar-gutter"})) {
        handled = true;
        const std::optional<std::vector<std::string>> words = words_of(ts, found);
        if (words) {
            answer = ascii_iequals(property, "color-scheme") ? color_scheme(*words)
                                                             : scrollbar_gutter(*words);
        }
    }
    if (!handled && ascii_iequals(property, "text-combine-upright")) {
        handled = true;
        answer = text_combine_upright(ts, found);
    }
    if (!handled && ascii_iequals(property, "offset-rotate")) {
        handled = true;
        answer = offset_rotate(ts, found);
    }
    if (!handled && ascii_iequals_any(property, {"animation-range-start", "animation-range-end"})) {
        handled = true;
        answer = animation_range(property, ts, found);
    }
    if (!handled && ascii_iequals(property, "image-resolution")) {
        handled = true;
        answer = image_resolution(ts, found);
    }
    if (!handled && ascii_iequals(property, "clip")) {
        handled = true;
        answer = clip_rect(ts, found);
    }
    if (!handled) { return false; }
    out = answer.value_or(std::string{});
    return true;
}

} // namespace detail

} // namespace ctbrowser::style::css
