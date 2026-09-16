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
            if (is_wide_keyword(word) ||
                ascii_iequals_any(word, {"will-change", "none", "all", "default"})) {
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
            if (is_wide_keyword(word) || ascii_iequals(word, "none")) { return std::nullopt; }
            name = std::string{word};
            ++k;
        } else if (t.type == token_type::function && reset &&
                   ascii_iequals(ts.text_of(t), "reversed(")) {
            if (k + 2 >= at.size() || ts.tokens[at[k + 1]].type != token_type::ident ||
                ts.tokens[at[k + 2]].type != token_type::close_paren) {
                return std::nullopt;
            }
            const std::string_view word = ts.text_of(ts.tokens[at[k + 1]]);
            if (is_wide_keyword(word) || ascii_iequals(word, "none")) { return std::nullopt; }
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
    if (!handled) { return false; }
    out = answer.value_or(std::string{});
    return true;
}

} // namespace detail

} // namespace ctbrowser::style::css
