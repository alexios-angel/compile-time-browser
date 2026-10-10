//===- keywords.cpp - Keyword and list grammars --------------- -*- C++ -*-===//
//
// Hand-written grammars for properties the generic property table cannot
// express. Each parser returns the canonical specified value or nullopt, and
// match_keywords dispatches to them by property name.
//
// The largest group is the keyword combinations, `a || b || c` over a few
// groups plus one or two words that stand alone, driven by one table
// (or_grammars) and one matcher (match_or):
//
//   text-decoration-line   none | [ underline || overline || line-through || blink ]
//   text-transform         none | math-auto | [ capitalize | uppercase | lowercase ]
//                          || full-width || full-size-kana
//   contain                none | strict | content | [ [size|inline-size] || layout
//                          || style || paint ]
//   font-synthesis, the font-variant-* longhands, text-underline-position,
//   hanging-punctuation
//
// A row says which words stand alone, the groups (at most one word from
// each), whether the canonical form is the group order or the author's, and
// the spellings that collapse (`contain scale-down` is `scale-down`).
//
// The rest are list and per-property parsers: custom-ident and string lists,
// will-change, the counter properties, scroll-snap-*, font-*, easing lists,
// cursor, content, position-try-fallbacks, clip and more.
//
// Names in parentheses like `contain-valid` are web-platform-tests files under
// css/*/parsing/ that check the behaviour described.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

/// Returns whether `word` may not be a `<custom-ident>`. That excludes the
/// CSS-wide keywords and `default` (CSS Values 4 §identifier-value).
/// `revert-rule`, a CSS-wide keyword from CSS Cascade 6 that nothing else here
/// implements, is excluded too: WPT's will-change and counter-reset parsing
/// tests assert both.
[[nodiscard]] bool reserved_ident(std::string_view word) {
    return is_wide_keyword(word) || ascii_iequals_any(word, {"default", "revert-rule"});
}

/// One keyword-combination grammar; see the file header.
struct or_grammar {
    std::string_view property;
    std::string_view alone;                 ///< Words valid only on their own.
    std::array<std::string_view, 5> groups; ///< Space-separated sets; an empty one ends.
    bool reorder;                           ///< The canonical order is the group order.
    std::string_view collapse_from = {};    ///< A full spelling that has a short form...
    std::string_view collapse_to = {};      ///< ...and that short form.
    std::string_view collapse2_from = {};   ///< A second such pair.
    std::string_view collapse2_to = {};
    bool first_group_required = false; ///< `[ a | b ] && c?`: the first group is required.
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
    // The specified value keeps its words: `layout style paint` is not
    // `content` until it computes (contain-valid against contain-computed),
    // and getComputedStyle does that collapse.
    {"contain", "none strict content", {"size inline-size", "layout", "style", "paint"}, true},
    // `normal | [ [ size | inline-size ] || scroll-state ]`, CSS Conditional 5 §4.2.
    {"container-type", "normal", {"size inline-size", "scroll-state"}, true},
    // `oblique-only` is the style synthesis a shorthand may name instead of
    // `style` (csswg-drafts#9390): the two share a slot.
    {"font-synthesis", "none", {"weight", "style oblique-only", "small-caps", "position"}, true},
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
    // CSS Box 4 §4: `none | block | block-start || block-end`, the two ends
    // together being `block`. (The inline keywords are at risk and no
    // engine takes them.)
    {"margin-trim",
     "none block",
     {"block-start", "block-end"},
     true,
     "block-start block-end",
     "block"},
    // `none | [ space | ideographic-space ] && auto-phrase?`, CSS Text 4 §8.2,
    // in the order written: `&&` does not reorder (word-space-transform-valid).
    {"word-space-transform",
     "none",
     {"space ideographic-space", "auto-phrase"},
     false,
     {},
     {},
     {},
     {},
     true},
    // `none | all | [ start || end ]`, CSS Text Decoration 4 §2.6.
    {"text-decoration-skip-spaces", "none all", {"start", "end"}, true},
    // `fill | none | [ contain | cover ] || scale-down`, CSS Images 4 §5.5:
    // `contain scale-down` is `scale-down`, the same box either way.
    {"object-fit",
     "fill none",
     {"contain cover", "scale-down"},
     true,
     "contain scale-down",
     "scale-down"},
    {"hanging-punctuation", "none", {"first", "force-end allow-end", "last"}, false},
    // `right` is the default side and the shortest spelling leaves it out
    // (CSSOM §6.7.2): `over right` is `over` (text-emphasis-position-valid).
    {"text-emphasis-position",
     "auto",
     {"over under", "right left"},
     true,
     "over right",
     "over",
     "under right",
     "under"},
    {"ruby-position", "inter-character", {"alternate", "over under"}, true},
    {"text-autospace",
     "normal auto no-autospace",
     {"ideograph-alpha", "ideograph-numeric", "punctuation", "insert replace"},
     true},
};

/// Matches lowercased `words` against `g`: one `alone` word, or at most one
/// word from each group. Returns the canonical spelling, or nullopt.
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
    if (g.first_group_required && chosen[0].empty()) { return std::nullopt; }
    if (out == g.collapse_from) { return std::string{g.collapse_to}; }
    if (out == g.collapse2_from) { return std::string{g.collapse2_to}; }
    return out;
}

/// `auto | [ scroll-position | contents | <custom-ident> ]#`, as written.
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

/// `none | [ <counter-name> <integer>? | <reversed-counter-name> <integer>? ]+`,
/// the integer always written: `foo` is `foo 1` for an increment and `foo 0`
/// for a reset or a set.
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
            // Written back as an identifier, escapes and all: `a\ 8 9` is the
            // counter `a 8` at 9, and unescaped it would read as the counter
            // `a` and a stray number (counter-reset-valid).
            name = serialize_identifier(word);
            ++k;
        } else if (t.type == token_type::function && reset &&
                   ascii_iequals(ts.text_of(t), "reversed(")) {
            if (k + 2 >= at.size() || ts.tokens[at[k + 1]].type != token_type::ident ||
                ts.tokens[at[k + 2]].type != token_type::close_paren) {
                return std::nullopt;
            }
            const std::string_view word = ts.text_of(ts.tokens[at[k + 1]]);
            if (reserved_ident(word) || ascii_iequals(word, "none")) { return std::nullopt; }
            name = "reversed(" + serialize_identifier(word) + ")";
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
            // A math function in the integer's place (CSS Values 4 §10.10),
            // kept unrounded; see below.
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
            // The specified value keeps the function (CSS Values 4 §10.12);
            // it is rounded to its integer when it computes, in the cascade.
            const math_answer answer = evaluate_math(text, length_context{});
            if (answer.outcome == math_outcome::invalid) { return std::nullopt; }
            if (answer.outcome == math_outcome::resolved && !answer.value.is_number) {
                return std::nullopt;
            }
            count = simplify_math(text);
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

/// scroll-snap-align: `[ none | start | end | center ]{1,2}`, one word when
/// both are the same. scroll-snap-type: `none | [ x | y | block | inline |
/// both ] [ mandatory | proximity ]?`, `proximity` being the default.
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

/// font-size-adjust: `none | [ ex-height | cap-height | ch-width | ic-width |
/// ic-height ]? [ from-font | <number [0,inf]> ]`, the default `ex-height`
/// dropped.
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

/// The source text of a run of significant tokens, empty when any of it came
/// from the decoded-escape tail of the pool rather than the author's bytes.
[[nodiscard]] std::string_view run_text(const token_stream & ts, std::span<const std::size_t> at) {
    if (at.empty()) { return {}; }
    const css_token & first = ts.tokens[at.front()];
    const css_token & last = ts.tokens[at.back()];
    if (first.text >= ts.source_length || last.text >= ts.source_length) { return {}; }
    return std::string_view{ts.pool}.substr(first.text, last.text + last.length - first.text);
}

/// Parses the math function at significant index `k` as one component of
/// type `want`. Returns nullopt when `k` is not on a function, the block does
/// not close, the math evaluator (css/calc/) rejects it, or it resolves to
/// another type. Leaves `k` on the closing paren. This lets a keyword list
/// take calc() in place of its one number.
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

/// A property whose value is `<custom-ident>`s: words the property does not
/// spell for itself. `alone` are the property's own words, valid only as the
/// whole value and never custom idents, so `view-transition-class: foo none`
/// is invalid. `lowercase` are words that may stand inside the list but are
/// still keywords: `transition-property: ALL, SRC` is `all, SRC`, because a
/// custom ident keeps its case and a keyword never does. `list` says whether
/// more than one may follow and `comma` whether commas separate them.
struct ident_grammar {
    std::string_view property;
    std::string_view alone;
    std::string_view lowercase;
    bool list;
    bool comma;
};

constexpr ident_grammar ident_grammars[] = {
    {"page", "auto", "", false, false},
    {"view-transition-group", "normal contain nearest none", "", false, false},
    {"view-transition-class", "none", "", true, false},
    // `none | match-element | <custom-ident>`, CSS View Transitions 2 §3.1.
    {"view-transition-name", "none match-element auto", "", false, false},
    {"transition-property", "none", "all", true, true},
};

/// Parses a value of `g`'s custom-ident grammar into its canonical text.
[[nodiscard]] std::optional<std::string> custom_idents(const ident_grammar & g,
                                                       const token_stream & ts,
                                                       const scan & found) {
    std::string out;
    std::size_t count = 0;
    bool want_ident = true;
    for (const std::size_t i : found.significant) {
        const css_token & t = ts.tokens[i];
        if (!want_ident) {
            if (!g.comma || t.type != token_type::comma) { return std::nullopt; }
            want_ident = true;
            continue;
        }
        if (t.type != token_type::ident) { return std::nullopt; }
        const std::string_view word = ts.text_of(t);
        if (has_keyword(g.alone, word)) {
            if (found.significant.size() != 1) { return std::nullopt; }
            return ascii_lower_copy(word);
        }
        if (reserved_ident(word)) { return std::nullopt; }
        if (++count > 1 && !g.list) { return std::nullopt; }
        out += (out.empty() ? "" : (g.comma ? ", " : " "));
        // The ident as CSSOM serialises one (§2.1): `ident(3 "myident")` made
        // the word `3myident`, which written bare would read back as a number.
        out += has_keyword(g.lowercase, word) ? ascii_lower_copy(word) : serialize_identifier(word);
        want_ident = !g.comma;
    }
    if (out.empty() || (g.comma && want_ident)) { return std::nullopt; }
    return out;
}

/// A property whose value is one string or one of its own keywords.
/// `font-language-override` also restricts which strings: CSS Fonts 4 wants an
/// OpenType language system tag, one to four printable ASCII characters, and
/// its shortest serialisation drops the trailing padding spaces (`"ENG "` is
/// `"ENG"`, `" en "` is `" en"`).
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

/// `normal | [ light | dark | <custom-ident> ]+ && only?` (CSS Color Adjust 1).
/// The `&&` is why `light only dark` is invalid where `only light dark` is not:
/// `only` sits beside the whole list, never inside it, and serialises last.
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

/// `normal | italic | oblique <angle>?` (CSS Fonts 4 §font-style). A zero angle
/// is `normal` (`oblique 0deg` and `normal` are the same style and serialise
/// the same way), and an angle written in grad or rad keeps its unit, because
/// only a math function is simplified.
[[nodiscard]] std::optional<std::string> font_style(const token_stream & ts, const scan & found) {
    const std::vector<std::size_t> & at = found.significant;
    if (at.empty() || ts.tokens[at[0]].type != token_type::ident) { return std::nullopt; }
    const std::string word = ascii_lower_copy(ts.text_of(ts.tokens[at[0]]));
    if (at.size() == 1) {
        if (word != "normal" && word != "italic" && word != "oblique") { return std::nullopt; }
        return word;
    }
    if (word != "oblique") { return std::nullopt; }
    const css_token & t = ts.tokens[at[1]];
    if (t.type == token_type::function) {
        std::size_t k = 1;
        const std::optional<std::string> math = math_component(ts, found, k, numeric_type::angle);
        if (!math || k + 1 != at.size()) { return std::nullopt; }
        return "oblique " + *math;
    }
    if (at.size() != 2 || t.type != token_type::dimension) { return std::nullopt; }
    const std::string_view unit = ts.unit_of(t);
    if (!ascii_iequals_any(unit, {"deg", "grad", "rad", "turn"})) { return std::nullopt; }
    if (t.number == 0) { return "normal"; }
    return "oblique " + serialize_number(t.number) + ascii_lower_copy(unit);
}

/// `[ a | b | ... ]{1,2}`, the pair written once when both words are the same
/// (`border-image-repeat: space space` is `space`, CSS Backgrounds 3 §6.4).
struct pair_grammar {
    std::string_view property;
    std::string_view keywords;
};

constexpr pair_grammar pair_grammars[] = {
    {"border-image-repeat", "stretch repeat round space"},
    {"mask-border-repeat", "stretch repeat round space"},
};

[[nodiscard]] std::optional<std::string> keyword_pair(const pair_grammar & g,
                                                      std::span<const std::string> words) {
    if (words.empty() || words.size() > 2) { return std::nullopt; }
    for (const std::string & w : words) {
        if (!has_keyword(g.keywords, w)) { return std::nullopt; }
    }
    if (words.size() == 2 && words[0] != words[1]) { return words[0] + " " + words[1]; }
    return words[0];
}

/// `auto | stable && both-edges?` (CSS Overflow 3): `both-edges` needs `stable`
/// beside it, and the pair serialises in that order however it was written.
[[nodiscard]] std::optional<std::string> scrollbar_gutter(std::span<const std::string> words) {
    if (words.size() == 1 && words.front() == "auto") { return words.front(); }
    if (words.size() == 1 && words.front() == "stable") { return words.front(); }
    if (words.size() == 2 && ((words[0] == "stable" && words[1] == "both-edges") ||
                              (words[0] == "both-edges" && words[1] == "stable"))) {
        return "stable both-edges";
    }
    return std::nullopt;
}

/// `none | all | [ digits <integer [2,4]>? ]` (CSS Writing Modes 4).
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

/// `[ auto | reverse ] || <angle>` (CSS Motion 1), the keyword written first.
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

/// One `<length-percentage>`, literal or a math function, canonical.
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

/// The comma-separated items of a value, each a run of significant tokens -
/// a comma inside a function's or a block's parentheses is the item's own.
/// `nullopt` for an empty item - a leading, trailing or doubled comma.
[[nodiscard]] std::optional<std::vector<std::vector<std::size_t>>> comma_items(
    const token_stream & ts, const scan & found) {
    std::vector<std::vector<std::size_t>> items{{}};
    int depth = 0;
    for (const std::size_t i : found.significant) {
        const token_type type = ts.tokens[i].type;
        if (type == token_type::function || type == token_type::open_paren) {
            ++depth;
        } else if (type == token_type::close_paren && depth > 0) {
            --depth;
        }
        if (depth == 0 && type == token_type::comma) {
            if (items.back().empty()) { return std::nullopt; }
            items.emplace_back();
            continue;
        }
        items.back().push_back(i);
    }
    if (items.back().empty()) { return std::nullopt; }
    return items;
}

/// `[ normal | <length-percentage> | <timeline-range-name> <length-percentage>? ]#`
/// (Scroll-driven Animations §animation-range). The offset that names the whole
/// of the named range is dropped, which is 0% at the start and 100% at the end.
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

/// `[ from-image || <resolution> ] && snap?` (CSS Images 4). The `&&` is why
/// `3dpi snap from-image` is invalid and `snap 3dpi from-image` is not: `snap`
/// sits beside the pair, never inside it. Written as the author wrote it.
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
        // A math function is a `<resolution>` too. WPT's css/css-values tests
        // put every resolution-typed calc() through image-resolution (their
        // numeric-testcommon.js picks it for `type:'resolution'`), so the
        // function's whole block is one component here.
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

/// `[ <family-name> | <generic-family> ]#` (CSS Fonts 4 §2.1): a family is a
/// string or a run of identifiers, none of them a generic family or a
/// CSS-wide keyword - `cursive serif` is no family - and a generic family is
/// one identifier, which is ASCII case-insensitive and folds (`Serif` is
/// `serif`). Anything else between the commas - `Red/Black`, `test@foo`, a
/// string with a word after it - is invalid. serialize_font_family writes it.
[[nodiscard]] std::optional<std::string> font_family(const token_stream & ts, const scan & found) {
    static constexpr std::string_view generics =
        "serif sans-serif cursive fantasy monospace system-ui emoji math fangsong ui-serif "
        "ui-sans-serif ui-monospace ui-rounded";
    const std::optional<std::vector<std::vector<std::size_t>>> items = comma_items(ts, found);
    if (!items) { return std::nullopt; }
    std::string out;
    for (const std::vector<std::size_t> & item : *items) {
        std::string one;
        if (item.size() == 1 && ts.tokens[item[0]].type == token_type::string) {
            one = normalize_value_tokens(ts, ts.text_of(ts.tokens[item[0]]), nullptr, item[0],
                                         item[0] + 1);
        } else {
            for (const std::size_t at : item) {
                const css_token & t = ts.tokens[at];
                if (t.type != token_type::ident) { return std::nullopt; }
                const std::string_view word = ts.text_of(t);
                if (item.size() == 1 && has_keyword(generics, word)) {
                    one = ascii_lower_copy(word);
                    break;
                }
                if (has_keyword(generics, word) || is_wide_keyword(word) ||
                    ascii_iequals(word, "default")) {
                    return std::nullopt;
                }
                one += (one.empty() ? "" : " ") + std::string{word};
            }
        }
        out += (out.empty() ? "" : ", ") + one;
    }
    return serialize_font_family(out);
}

/// A four-character ASCII <string>, the OpenType tag of a feature or an axis
/// (CSS Fonts 4 §6.12, §7.2); its canonical spelling, or nullopt.
[[nodiscard]] std::optional<std::string> opentype_tag(const token_stream & ts,
                                                      const css_token & t) {
    if (t.type != token_type::string) { return std::nullopt; }
    const std::string_view tag = ts.value_of(t);
    if (tag.size() != 4) { return std::nullopt; }
    for (const char c : tag) {
        if (c < 0x20 || c > 0x7e) { return std::nullopt; }
    }
    return "\"" + std::string{tag} + "\"";
}

/// `normal | <feature-tag-value>#`, <feature-tag-value> = <opentype-tag>
/// [ <integer [0,∞]> | on | off ]? (CSS Fonts 4 §6.12): a value of 1 - `on`
/// or none at all - is left unwritten, and `off` is 0.
[[nodiscard]] std::optional<std::string> font_feature_settings(const token_stream & ts,
                                                               const scan & found) {
    const std::vector<std::size_t> & at = found.significant;
    if (at.size() == 1 && ts.tokens[at[0]].type == token_type::ident &&
        ascii_iequals(ts.text_of(ts.tokens[at[0]]), "normal")) {
        return "normal";
    }
    const std::optional<std::vector<std::vector<std::size_t>>> items = comma_items(ts, found);
    if (!items) { return std::nullopt; }
    std::string out;
    for (const std::vector<std::size_t> & item : *items) {
        if (item.empty() || item.size() > 2) { return std::nullopt; }
        std::optional<std::string> tag = opentype_tag(ts, ts.tokens[item[0]]);
        if (!tag) { return std::nullopt; }
        std::string value;
        if (item.size() == 2) {
            const css_token & v = ts.tokens[item[1]];
            if (v.type == token_type::ident && ascii_iequals(ts.text_of(v), "on")) {
                value.clear();
            } else if (v.type == token_type::ident && ascii_iequals(ts.text_of(v), "off")) {
                value = "0";
            } else if (v.type == token_type::number && (v.flags & flag_integer) != 0 &&
                       v.number >= 0) {
                value = v.number == 1 ? std::string{} : serialize_number(v.number);
            } else {
                return std::nullopt;
            }
        }
        out += (out.empty() ? "" : ", ") + *tag + (value.empty() ? "" : " " + value);
    }
    return out;
}

/// `normal | [ <opentype-tag> <number> ]#` (CSS Fonts 4 §7.2).
[[nodiscard]] std::optional<std::string> font_variation_settings(const token_stream & ts,
                                                                 const scan & found) {
    const std::vector<std::size_t> & at = found.significant;
    if (at.size() == 1 && ts.tokens[at[0]].type == token_type::ident &&
        ascii_iequals(ts.text_of(ts.tokens[at[0]]), "normal")) {
        return "normal";
    }
    const std::optional<std::vector<std::vector<std::size_t>>> items = comma_items(ts, found);
    if (!items) { return std::nullopt; }
    std::string out;
    for (const std::vector<std::size_t> & item : *items) {
        if (item.size() != 2) { return std::nullopt; }
        std::optional<std::string> tag = opentype_tag(ts, ts.tokens[item[0]]);
        const css_token & v = ts.tokens[item[1]];
        if (!tag || v.type != token_type::number) { return std::nullopt; }
        out += (out.empty() ? "" : ", ") + *tag + " " + serialize_number(v.number);
    }
    return out;
}

/// `normal | light | dark | <palette-identifier> | <palette-mix()>` (CSS
/// Fonts 4 §9.1), a <palette-identifier> being a <dashed-ident>.
[[nodiscard]] std::optional<std::string> font_palette(const token_stream & ts, const scan & found) {
    const std::vector<std::size_t> & at = found.significant;
    if (at.empty()) { return std::nullopt; }
    const css_token & first = ts.tokens[at[0]];
    if (first.type == token_type::function && ascii_iequals(ts.text_of(first), "palette-mix(")) {
        return std::string{run_text(ts, at)};
    }
    if (at.size() != 1 || first.type != token_type::ident) { return std::nullopt; }
    const std::string_view word = ts.text_of(first);
    if (ascii_iequals_any(word, {"normal", "light", "dark"})) { return ascii_lower_copy(word); }
    if (word.starts_with("--")) { return std::string{word}; }
    return std::nullopt;
}

/// `symbols( <symbols-type>? <string>+ )` (CSS Counter Styles 3 §6.1, without
/// the <image> alternative engines leave out): two symbols at least for
/// `numeric` and `alphabetic`, and `additive` is no type here. `inner` is the
/// significant tokens between the parentheses.
[[nodiscard]] bool symbols_valid(const token_stream & ts, std::span<const std::size_t> inner) {
    std::size_t first = 0;
    std::size_t least = 1;
    if (!inner.empty() && ts.tokens[inner[0]].type == token_type::ident) {
        const std::string kind = ascii_lower_copy(ts.text_of(ts.tokens[inner[0]]));
        if (!has_keyword("cyclic numeric alphabetic symbolic fixed", kind)) { return false; }
        if (kind == "numeric" || kind == "alphabetic") { least = 2; }
        first = 1;
    }
    for (std::size_t i = first; i < inner.size(); ++i) {
        if (ts.tokens[inner[i]].type != token_type::string) { return false; }
    }
    return inner.size() - first >= least;
}

/// `<counter-style> | <string> | none` (CSS Lists 3 §3.3): one identifier,
/// one string, or one symbols(). Only checked here: the generic `freeform`
/// fallback in declaration.cpp writes the value as typed.
[[nodiscard]] bool list_style_type_valid(const token_stream & ts, const scan & found) {
    const std::vector<std::size_t> & at = found.significant;
    if (at.size() == 1) {
        const token_type t = ts.tokens[at[0]].type;
        return t == token_type::ident || t == token_type::string;
    }
    if (at.size() < 2 || ts.tokens[at[0]].type != token_type::function ||
        !ascii_iequals(ts.text_of(ts.tokens[at[0]]), "symbols(") ||
        ts.tokens[at.back()].type != token_type::close_paren) {
        return false;
    }
    return symbols_valid(ts, std::span{at}.subspan(1, at.size() - 2));
}

/// `[ <cursor-image> [ <x> <y> ]? , ]* <cursor-predefined>` (CSS UI 4 §4.1),
/// a <cursor-image> being a url(), an image-set(), or a light-dark() of two -
/// no gradient - and <x> <y> two numbers. Only checked here: the freeform
/// fallback in declaration.cpp writes the value.
[[nodiscard]] bool cursor_valid(const token_stream & ts, const scan & found) {
    static constexpr std::string_view predefined =
        "auto default none context-menu help pointer progress wait cell crosshair text "
        "vertical-text alias copy move no-drop not-allowed grab grabbing e-resize n-resize "
        "ne-resize nw-resize s-resize se-resize sw-resize w-resize ew-resize ns-resize "
        "nesw-resize nwse-resize col-resize row-resize all-scroll zoom-in zoom-out";
    const std::optional<std::vector<std::vector<std::size_t>>> items = comma_items(ts, found);
    if (!items) { return false; }
    const auto name_of = [&](std::size_t i) {
        std::string name = ascii_lower_copy(ts.text_of(ts.tokens[i]));
        if (!name.empty() && name.back() == '(') { name.pop_back(); }
        return name;
    };
    // The index in `item` past the image starting at `k`, or 0 when none is.
    const auto image_end = [&](const std::vector<std::size_t> & item, std::size_t k) {
        const css_token & t = ts.tokens[item[k]];
        if (t.type == token_type::url) { return k + 1; }
        if (t.type != token_type::function) { return std::size_t{0}; }
        const std::string name = name_of(item[k]);
        int depth = 0;
        std::size_t close = k;
        for (; close < item.size(); ++close) {
            const token_type type = ts.tokens[item[close]].type;
            if (type == token_type::function || type == token_type::open_paren) { ++depth; }
            if (type == token_type::close_paren && --depth == 0) { break; }
        }
        if (close >= item.size()) { return std::size_t{0}; }
        if (ascii_iequals_any(name, {"url", "image-set", "-webkit-image-set"})) {
            return close + 1;
        }
        if (name != "light-dark") { return std::size_t{0}; }
        // Each of its two arguments a url() or an image-set() alone.
        std::size_t at = k + 1, args = 0;
        while (at < close) {
            const css_token & a = ts.tokens[item[at]];
            if (a.type == token_type::url) {
                ++at;
            } else if (a.type == token_type::function &&
                       ascii_iequals_any(name_of(item[at]), {"url", "image-set"})) {
                int inner = 0;
                for (; at < close; ++at) {
                    const token_type type = ts.tokens[item[at]].type;
                    if (type == token_type::function || type == token_type::open_paren) { ++inner; }
                    if (type == token_type::close_paren && --inner == 0) { break; }
                }
                ++at;
            } else {
                return std::size_t{0};
            }
            ++args;
            if (at < close && ts.tokens[item[at]].type == token_type::comma) { ++at; }
        }
        return args == 2 ? close + 1 : std::size_t{0};
    };
    for (std::size_t n = 0; n < items->size(); ++n) {
        const std::vector<std::size_t> & item = (*items)[n];
        if (n + 1 == items->size()) {
            if (item.size() != 1 || ts.tokens[item[0]].type != token_type::ident) { return false; }
            const std::string word = ascii_lower_copy(ts.text_of(ts.tokens[item[0]]));
            return has_keyword(predefined, word) || word.starts_with("-webkit-");
        }
        const std::size_t past = image_end(item, 0);
        if (past == 0) { return false; }
        const std::size_t rest = item.size() - past;
        if (rest != 0 && rest != 2) { return false; }
        for (std::size_t k = past; k < item.size(); ++k) {
            if (ts.tokens[item[k]].type != token_type::number) { return false; }
        }
    }
    return false;
}

/// `none | [ grow | shrink ] [ consistent | per-line | per-line-all ]?
/// <percentage>?` (CSS Text 4 §10.1).
[[nodiscard]] std::optional<std::string> text_fit(const token_stream & ts, const scan & found) {
    const std::vector<std::size_t> & at = found.significant;
    const auto word = [&](std::size_t i) {
        const css_token & t = ts.tokens[at[i]];
        return t.type == token_type::ident ? ascii_lower_copy(ts.text_of(t)) : std::string{};
    };
    if (at.empty() || at.size() > 3) { return std::nullopt; }
    if (at.size() == 1 && word(0) == "none") { return "none"; }
    if (word(0) != "grow" && word(0) != "shrink") { return std::nullopt; }
    std::string out = word(0);
    std::size_t k = 1;
    if (k < at.size() && !word(k).empty() &&
        has_keyword("consistent per-line per-line-all", word(k))) {
        out += " " + word(k++);
    }
    if (k < at.size() && ts.tokens[at[k]].type == token_type::percentage) {
        out += " " + serialize_number(ts.tokens[at[k++]].number) + "%";
    }
    if (k != at.size()) { return std::nullopt; }
    return out;
}

/// A <'position-area'> as position-try-fallbacks lists it (CSS Anchor
/// Positioning 1 §3.1): one or two keywords of one grammar - x with y, block
/// with inline, self-block with self-inline, or two axis-free ones - with
/// `center` and `span-all` fitting any, the horizontal or block keyword
/// written first and a repeated one once. Lowercase words in.
[[nodiscard]] std::optional<std::string> position_area(std::vector<std::string> words) {
    const auto family = [](std::string_view w) {
        if (w == "center" || w == "span-all") { return 0; }
        static constexpr std::pair<std::string_view, int> sets[] = {
            {"left right span-left span-right x-start x-end span-x-start span-x-end self-x-start "
             "self-x-end span-self-x-start span-self-x-end",
             1},
            {"top bottom span-top span-bottom y-start y-end span-y-start span-y-end self-y-start "
             "self-y-end span-self-y-start span-self-y-end",
             2},
            {"block-start block-end span-block-start span-block-end", 3},
            {"inline-start inline-end span-inline-start span-inline-end", 4},
            {"self-block-start self-block-end span-self-block-start span-self-block-end", 5},
            {"self-inline-start self-inline-end span-self-inline-start span-self-inline-end", 6},
            {"start end span-start span-end", 7},
            {"self-start self-end span-self-start span-self-end", 8}};
        for (const auto & [set, id] : sets) {
            if (has_keyword(set, w)) { return id; }
        }
        return -1;
    };
    if (words.empty() || words.size() > 2) { return std::nullopt; }
    for (const std::string & w : words) {
        if (family(w) < 0) { return std::nullopt; }
    }
    if (words.size() == 1) { return words[0]; }
    const int a = family(words[0]), b = family(words[1]);
    // 1 pairs with 2, 3 with 4, 5 with 6; the axis-free ones with their own.
    const auto partner = [](int f) {
        return f >= 1 && f <= 6 ? ((f % 2) != 0 ? f + 1 : f - 1) : f;
    };
    if (a != 0 && b != 0 && partner(a) != b) { return std::nullopt; }
    if (a == 2 || a == 4 || a == 6 || b == 1 || b == 3 || b == 5) { std::swap(words[0], words[1]); }
    if (words[0] == words[1]) { return words[0]; }
    return words[0] + " " + words[1];
}

/// `none | [ [ <dashed-ident> || <try-tactic> ] | <'position-area'> ]#` (CSS
/// Anchor Positioning 1 §5.2), a <try-tactic> being flip-block, flip-inline,
/// flip-start, flip-x and flip-y, each once: written with the name first and
/// the tactics in the author's order.
[[nodiscard]] std::optional<std::string> position_try_fallbacks(const token_stream & ts,
                                                                const scan & found) {
    const std::vector<std::size_t> & at = found.significant;
    if (at.size() == 1 && ts.tokens[at[0]].type == token_type::ident &&
        ascii_iequals(ts.text_of(ts.tokens[at[0]]), "none")) {
        return "none";
    }
    const std::optional<std::vector<std::vector<std::size_t>>> items = comma_items(ts, found);
    if (!items) { return std::nullopt; }
    std::string out;
    for (const std::vector<std::size_t> & item : *items) {
        std::string name;
        std::vector<std::string> tactics, words;
        // `<dashed-ident> || <try-tactic>`: the tactic's keywords are one
        // component, so the name may not stand between two of them.
        bool before_name = false, after_name = false;
        for (const std::size_t i : item) {
            if (ts.tokens[i].type != token_type::ident) { return std::nullopt; }
            const std::string_view word = ts.text_of(ts.tokens[i]);
            if (word.starts_with("--")) {
                if (!name.empty()) { return std::nullopt; }
                name = std::string{word};
                continue;
            }
            const std::string lower = ascii_lower_copy(word);
            if (has_keyword("flip-block flip-inline flip-start flip-x flip-y", lower)) {
                if (std::ranges::find(tactics, lower) != tactics.end()) { return std::nullopt; }
                (name.empty() ? before_name : after_name) = true;
                if (before_name && after_name) { return std::nullopt; }
                tactics.push_back(lower);
            } else {
                words.push_back(lower);
            }
        }
        std::string one;
        if (!words.empty()) {
            if (!name.empty() || !tactics.empty()) { return std::nullopt; }
            const std::optional<std::string> area = position_area(words);
            if (!area) { return std::nullopt; }
            one = *area;
        } else {
            one = name;
            for (const std::string & t : tactics) { one += (one.empty() ? "" : " ") + t; }
        }
        out += (out.empty() ? "" : ", ") + one;
    }
    return out;
}

/// `normal | none | [ <content-replacement> | <content-list> ] [ / [ <string> |
/// <counter> | <attr()> ]+ ]?` (CSS Generated Content 3 §2), with counter() and
/// counters() as CSS Lists 3 §4 writes them and symbols() as CSS Counter Styles 3
/// §6.1 does. Only checked here: the freeform fallback in declaration.cpp
/// writes the value.
[[nodiscard]] bool content_valid(const token_stream & ts, const scan & found) {
    const std::vector<std::size_t> & at = found.significant;
    const auto type_of = [&](std::size_t k) { return ts.tokens[at[k]].type; };
    const auto name_of = [&](std::size_t k) {
        std::string name = ascii_lower_copy(ts.text_of(ts.tokens[at[k]]));
        if (!name.empty() && name.back() == '(') { name.pop_back(); }
        return name;
    };
    // The position in `at` of the `)` closing the block opened at `open` -
    // `at.size()` when the value ends first, which closes it.
    const auto close_of = [&](std::size_t open) {
        int depth = 0;
        for (std::size_t k = open; k < at.size(); ++k) {
            const token_type t = type_of(k);
            if (t == token_type::function || t == token_type::open_paren) {
                ++depth;
            } else if (t == token_type::close_paren && --depth == 0) {
                return k;
            }
        }
        return at.size();
    };
    // The comma-separated arguments of the function at `open`, as positions.
    const auto args_of = [&](std::size_t open, std::size_t close) {
        std::vector<std::vector<std::size_t>> args{{}};
        for (std::size_t k = open + 1; k < close; ++k) {
            if (type_of(k) == token_type::comma) {
                args.emplace_back();
                continue;
            }
            const std::size_t end = type_of(k) == token_type::function ? close_of(k) : k;
            for (std::size_t j = k; j <= end && j < close; ++j) { args.back().push_back(j); }
            k = end;
        }
        return args;
    };
    const auto single_ident = [&](const std::vector<std::size_t> & arg) {
        return arg.size() == 1 && type_of(arg[0]) == token_type::ident;
    };
    const auto symbols_ok = [&](std::size_t open, std::size_t close) {
        return symbols_valid(ts, std::span{at}.subspan(open + 1, close - open - 1));
    };
    // counter( <counter-name>, <counter-style>? ) and counters( <counter-name>,
    // <string>, <counter-style>? ).
    const auto counter_ok = [&](std::size_t open, std::size_t close) {
        const bool plural = name_of(open) == "counters";
        const std::vector<std::vector<std::size_t>> args = args_of(open, close);
        const std::size_t fixed = plural ? 2 : 1;
        if (args.size() < fixed || args.size() > fixed + 1 || !single_ident(args[0])) {
            return false;
        }
        if (plural && (args[1].size() != 1 || type_of(args[1][0]) != token_type::string)) {
            return false;
        }
        if (args.size() == fixed) { return true; }
        const std::vector<std::size_t> & style = args[fixed];
        if (single_ident(style)) {
            return name_of(style[0]) != "none" && !is_wide_keyword(name_of(style[0]));
        }
        return !style.empty() && type_of(style[0]) == token_type::function &&
               name_of(style[0]) == "symbols" && close_of(style[0]) == style.back() &&
               symbols_ok(style[0], style.back());
    };
    if (at.size() == 1 && type_of(0) == token_type::ident &&
        ascii_iequals_any(name_of(0), {"normal", "none"})) {
        return true;
    }
    bool alt = false;
    std::size_t items = 0;
    for (std::size_t k = 0; k < at.size(); ++k) {
        const token_type t = type_of(k);
        if (t == token_type::delim && ts.text_of(ts.tokens[at[k]]) == "/") {
            if (alt || items == 0) { return false; }
            alt = true;
            items = 0;
            continue;
        }
        ++items;
        if (t == token_type::string) { continue; }
        if (t == token_type::function) {
            const std::size_t close = close_of(k);
            const std::string name = name_of(k);
            if (name == "counter" || name == "counters") {
                if (!counter_ok(k, close)) { return false; }
            } else if (alt && name != "attr") {
                return false;
            }
            k = close;
            continue;
        }
        if (alt) { return false; }
        if (t == token_type::url) { continue; }
        if (t != token_type::ident ||
            !has_keyword("open-quote close-quote no-open-quote no-close-quote contents",
                         name_of(k))) {
            return false;
        }
    }
    return items > 0;
}

/// `<easing-function>#` (CSS Easing 1 §2 and 2 §2), each written canonically:
/// `step-start` is `steps(1, start)`, `step-end` and an `end`/`jump-end` step
/// position are left unwritten, a cubic-bezier()'s x values lie in [0, 1], and
/// a steps() count is a positive integer - two or more for `jump-none`.
/// linear() is kept as written. Anything else - `auto`, `ease-in ease-out`,
/// `steps(2, ())` - is invalid (transition-timing-function-invalid).
[[nodiscard]] std::optional<std::string> easing_list(const token_stream & ts, const scan & found) {
    const std::optional<std::vector<std::vector<std::size_t>>> items = comma_items(ts, found);
    if (!items) { return std::nullopt; }
    std::string out;
    for (const std::vector<std::size_t> & item : *items) {
        const css_token & first = ts.tokens[item[0]];
        std::string one;
        if (item.size() == 1 && first.type == token_type::ident) {
            const std::string word = ascii_lower_copy(ts.text_of(first));
            if (word == "step-start") {
                one = "steps(1, start)";
            } else if (word == "step-end") {
                one = "steps(1)";
            } else if (has_keyword("linear ease ease-in ease-out ease-in-out", word)) {
                one = word;
            } else {
                return std::nullopt;
            }
        } else if (first.type == token_type::function) {
            const std::string name = ascii_lower_copy(ts.text_of(first));
            // The function's arguments: the tokens after it, up to its `)`.
            std::vector<std::vector<std::size_t>> args{{}};
            std::size_t k = 1;
            int depth = 1;
            for (; k < item.size(); ++k) {
                const css_token & t = ts.tokens[item[k]];
                if (t.type == token_type::function || t.type == token_type::open_paren) {
                    ++depth;
                } else if (t.type == token_type::close_paren && --depth == 0) {
                    break;
                }
                if (depth == 1 && t.type == token_type::comma) {
                    args.emplace_back();
                    continue;
                }
                args.back().push_back(item[k]);
            }
            if (k + 1 != item.size() && !(k == item.size() && depth > 0)) { return std::nullopt; }
            const std::string_view whole = run_text(ts, item);
            if (name == "linear(") {
                one = std::string{whole};
            } else if (name == "cubic-bezier(") {
                if (args.size() != 4) { return std::nullopt; }
                std::string numbers;
                for (std::size_t i = 0; i < 4; ++i) {
                    if (args[i].size() != 1) { return std::nullopt; }
                    const css_token & n = ts.tokens[args[i][0]];
                    if (n.type != token_type::number) { return std::nullopt; }
                    if ((i == 0 || i == 2) && (n.number < 0 || n.number > 1)) {
                        return std::nullopt;
                    }
                    // Every digit the author gave: the curve is evaluated
                    // from this text, and `0.233333` for `0.2333333333` moves
                    // an eased colour channel or a snapped width by one.
                    std::array<char, 64> digits{};
                    const double v = n.number == 0 ? 0.0 : n.number;
                    const auto written = std::to_chars(digits.data(), digits.data() + digits.size(),
                                                       v, std::chars_format::fixed);
                    numbers += (i == 0 ? "" : ", ") + std::string(digits.data(), written.ptr);
                }
                one = "cubic-bezier(" + numbers + ")";
            } else if (name == "steps(") {
                if (args.empty() || args.size() > 2 || args[0].empty()) { return std::nullopt; }
                std::string position = "end";
                if (args.size() == 2) {
                    if (args[1].size() != 1 || ts.tokens[args[1][0]].type != token_type::ident) {
                        return std::nullopt;
                    }
                    position = ascii_lower_copy(ts.text_of(ts.tokens[args[1][0]]));
                    if (!has_keyword("start end jump-start jump-end jump-none jump-both",
                                     position)) {
                        return std::nullopt;
                    }
                }
                std::string count;
                const css_token & n = ts.tokens[args[0][0]];
                if (args[0].size() == 1 && n.type == token_type::number) {
                    const double least = position == "jump-none" ? 2 : 1;
                    if ((n.flags & flag_integer) == 0 || n.number < least) { return std::nullopt; }
                    count = serialize_number(n.number);
                } else if (n.type == token_type::function) {
                    // A math function or a tree-counting one: its value is the
                    // computed value's business.
                    count = std::string{run_text(ts, args[0])};
                } else {
                    return std::nullopt;
                }
                const bool omitted = position == "end" || position == "jump-end";
                one = "steps(" + count + (omitted ? "" : ", " + position) + ")";
            } else {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
        out += (out.empty() ? "" : ", ") + one;
    }
    return out;
}

/// `<length [0,∞]>{1,2}` (CSS Tables 3 §border-spacing): the horizontal
/// spacing and the vertical one, which is the horizontal when omitted - kept as
/// written, one value or two (border-spacing-valid).
[[nodiscard]] std::optional<std::string> border_spacing(const token_stream & ts,
                                                        const scan & found) {
    static constexpr property_syntax spacing{"", k::length, "", "", false, true};
    std::vector<std::string> parts;
    for (std::size_t k = 0; k < found.significant.size(); ++k) {
        std::string one;
        if (match_typed(ts, ts.tokens[found.significant[k]], spacing, one)) {
            parts.push_back(std::move(one));
            continue;
        }
        std::optional<std::string> math = math_component(ts, found, k, numeric_type::length);
        if (!math) { return std::nullopt; }
        parts.push_back(std::move(*math));
    }
    if (parts.empty() || parts.size() > 2) { return std::nullopt; }
    return parts.size() == 1 ? parts[0] : parts[0] + " " + parts[1];
}

/// `auto | rect( [ <length> | auto ]#{4} )` (CSS Masking 1, the CSS 2.1
/// property), plus CSS 2.1's space-separated `rect(0 0 0 0)`. Mixing the two
/// separators, as in `rect(10px 20px, 30px 40px)`, is a syntax error.
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
        // Separators must be uniform. `rect(10px, 20px, 30px, 40px)` is CSS
        // Masking 1's `[ <length> | auto ]#{4}` and `rect(0 0 0 0)` is CSS 2.1's
        // comma-less form that every engine still takes; `rect(10px 20px, 30px
        // 40px)` is neither, and css-masking/parsing/clip-invalid says so.
        if (!want_side && commas != 0) { return std::nullopt; }
        std::string one;
        if (t.type == token_type::ident && ascii_iequals(ts.text_of(t), "auto")) {
            one = "auto";
        } else if (t.type == token_type::function) {
            // A side may be a math function: `rect(10px, 20px, calc(-1em +
            // 10px), 1em)` is clip-computed's, and `k` lands on its close paren.
            std::size_t block = k;
            const std::optional<std::string> math =
                math_component(ts, found, block, numeric_type::length);
            if (!math) { return std::nullopt; }
            one = *math;
            k = block;
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

/// `normal | [ stylistic(<feature-value-name>) || historical-forms ||
/// styleset(<feature-value-name>#) || character-variant(<feature-value-name>#) ||
/// swash(<feature-value-name>) || ornaments(<feature-value-name>) ||
/// annotation(<feature-value-name>) ]`, CSS Fonts 4 §6.8: each at most once,
/// one name in all but the two lists, and written back in that order.
[[nodiscard]] std::optional<std::string> font_variant_alternates(const token_stream & ts,
                                                                 const scan & found) {
    static constexpr std::array<std::string_view, 7> slots = {
        "stylistic(", "historical-forms", "styleset(",  "character-variant(",
        "swash(",     "ornaments(",       "annotation("};
    const std::vector<std::size_t> & at = found.significant;
    if (at.size() == 1 && ts.tokens[at[0]].type == token_type::ident &&
        ascii_iequals(ts.text_of(ts.tokens[at[0]]), "normal")) {
        return "normal";
    }
    std::array<std::string, 7> given;
    for (std::size_t k = 0; k < at.size(); ++k) {
        const css_token & t = ts.tokens[at[k]];
        const std::string word = ascii_lower_copy(ts.text_of(t));
        const auto slot = std::ranges::find(slots, word);
        if (slot == slots.end()) { return std::nullopt; }
        std::string & out = given[static_cast<std::size_t>(slot - slots.begin())];
        if (!out.empty()) { return std::nullopt; }
        if (t.type == token_type::ident && word == "historical-forms") {
            out = word;
            continue;
        }
        if (t.type != token_type::function) { return std::nullopt; }
        const bool list = word == "styleset(" || word == "character-variant(";
        out = word;
        bool want_name = true;
        std::size_t names = 0;
        for (++k; k < at.size() && ts.tokens[at[k]].type != token_type::close_paren; ++k) {
            const css_token & a = ts.tokens[at[k]];
            if (want_name) {
                if (a.type != token_type::ident || reserved_ident(ts.text_of(a))) {
                    return std::nullopt;
                }
                out += (names++ == 0 ? "" : ", ") + serialize_identifier(ts.text_of(a));
                want_name = false;
            } else if (!list || a.type != token_type::comma) {
                return std::nullopt;
            } else {
                want_name = true;
            }
        }
        if (k == at.size() || want_name) { return std::nullopt; }
        out += ')';
    }
    std::string joined;
    for (const std::string & one : given) {
        if (!one.empty()) { joined += (joined.empty() ? "" : " ") + one; }
    }
    if (joined.empty()) { return std::nullopt; }
    return joined;
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
        const std::optional<std::vector<std::string>> words = words_of(ts, found.significant);
        if (words) { answer = match_or(g, *words); }
    }
    if (!handled && ascii_iequals(property, "font-size-adjust")) {
        handled = true;
        answer = font_size_adjust(ts, found);
    }
    if (!handled && ascii_iequals(property, "font-variant-alternates")) {
        handled = true;
        answer = font_variant_alternates(ts, found);
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
        const std::optional<std::vector<std::string>> words = words_of(ts, found.significant);
        if (words) { answer = scroll_snap(property, *words); }
    }
    for (const ident_grammar & g : ident_grammars) {
        if (handled || !ascii_iequals(g.property, property)) { continue; }
        handled = true;
        answer = custom_idents(g, ts, found);
    }
    if (!handled && ascii_iequals(property, "container-name")) {
        // `none | <custom-ident>+`, CSS Conditional 5 §4.1 - which also takes
        // `and`, `or` and `not` out of the idents, since a query names them.
        handled = true;
        answer = custom_idents({"container-name", "none", "", true, false}, ts, found);
        if (answer) {
            for (const std::string_view word : split_top_level(*answer, " ")) {
                if (ascii_iequals_any(word, {"and", "or", "not"})) { answer = std::nullopt; }
            }
        }
    }
    for (const string_grammar & g : string_grammars) {
        if (handled || !ascii_iequals(g.property, property)) { continue; }
        handled = true;
        answer = one_string(g, ts, found);
    }
    if (!handled && ascii_iequals_any(property, {"color-scheme", "scrollbar-gutter"})) {
        handled = true;
        const std::optional<std::vector<std::string>> words = words_of(ts, found.significant);
        if (words) {
            answer = ascii_iequals(property, "color-scheme") ? color_scheme(*words)
                                                             : scrollbar_gutter(*words);
        }
    }
    for (const pair_grammar & g : pair_grammars) {
        if (handled || !ascii_iequals(g.property, property)) { continue; }
        handled = true;
        const std::optional<std::vector<std::string>> words = words_of(ts, found.significant);
        if (words) { answer = keyword_pair(g, *words); }
    }
    if (!handled && ascii_iequals(property, "font-style")) {
        handled = true;
        answer = font_style(ts, found);
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
    if (!handled && ascii_iequals(property, "border-spacing")) {
        handled = true;
        answer = border_spacing(ts, found);
    }
    // A family list or a tag list with a function in it (`random-item()`,
    // `"liga" calc(10.1)`) is left for the function to check; the freeform
    // fallback in declaration.cpp keeps it as written.
    const bool has_function = std::ranges::any_of(found.significant, [&ts](std::size_t i) {
        return ts.tokens[i].type == token_type::function;
    });
    if (!handled && ascii_iequals(property, "font-family") && !has_function) {
        handled = true;
        answer = font_family(ts, found);
    }
    if (!handled &&
        ascii_iequals_any(property, {"transition-timing-function", "animation-timing-function"})) {
        handled = true;
        answer = easing_list(ts, found);
    }
    if (!handled && !has_function && ascii_iequals(property, "font-feature-settings")) {
        handled = true;
        answer = font_feature_settings(ts, found);
    }
    if (!handled && !has_function && ascii_iequals(property, "font-variation-settings")) {
        handled = true;
        answer = font_variation_settings(ts, found);
    }
    if (!handled && ascii_iequals(property, "font-palette")) {
        handled = true;
        answer = font_palette(ts, found);
    }
    if (!handled && ascii_iequals(property, "cursor") && !cursor_valid(ts, found)) {
        handled = true;
    }
    if (!handled && ascii_iequals(property, "text-fit")) {
        handled = true;
        answer = text_fit(ts, found);
    }
    if (!handled && ascii_iequals(property, "position-try-fallbacks")) {
        handled = true;
        answer = position_try_fallbacks(ts, found);
    }
    if (!handled && ascii_iequals(property, "list-style-type") &&
        !list_style_type_valid(ts, found)) {
        handled = true;
    }
    // `content` is only checked here; the freeform fallback writes a valid one.
    if (!handled && ascii_iequals(property, "content") && !content_valid(ts, found)) {
        handled = true;
    }
    if (!handled) { return false; }
    out = answer.value_or(std::string{});
    return true;
}

} // namespace detail

std::string computed_offset_rotate(std::string_view specified, bool keep_auto) {
    bool automatic = false;
    double degrees = 0;
    for (const std::string_view raw : split_top_level(specified, html_whitespace)) {
        const std::string_view word = trim(raw, html_whitespace);
        if (word.empty()) { continue; }
        if (ascii_iequals(word, "auto")) {
            automatic = true;
        } else if (ascii_iequals(word, "reverse")) {
            automatic = true;
            degrees += 180;
        } else {
            const math_answer answer = evaluate_math(word, length_context{});
            if (answer.outcome != math_outcome::resolved || answer.value.is_number ||
                answer.value.type != numeric_type::angle) {
                return {};
            }
            degrees += answer.value.px;
        }
    }
    const std::string angle = serialize_number(degrees) + "deg";
    if (!automatic) { return angle; }
    if (keep_auto && degrees == 0) { return "auto"; }
    return "auto " + angle;
}

std::string computed_zoom(std::string_view specified, const length_context & ctx) {
    const std::string_view text = trim(specified, html_whitespace);
    double factor = 1;
    if (!ascii_iequals(text, "normal")) {
        const math_answer answer = evaluate_math(text, ctx);
        if (answer.outcome != math_outcome::resolved) { return {}; }
        const calc_result & v = answer.value;
        if (v.is_number) {
            factor = v.px;
        } else if (v.has_percent && v.px == 0) {
            factor = v.percent / 100;
        } else {
            return {};
        }
    }
    if (!(factor > 0)) { factor = 1; }
    return serialize_number(factor);
}

std::optional<std::string_view> font_width_percentage(std::string_view keyword) {
    static constexpr std::pair<std::string_view, std::string_view> table[] = {
        {"ultra-condensed", "50%"},  {"extra-condensed", "62.5%"}, {"condensed", "75%"},
        {"semi-condensed", "87.5%"}, {"normal", "100%"},           {"semi-expanded", "112.5%"},
        {"expanded", "125%"},        {"extra-expanded", "150%"},   {"ultra-expanded", "200%"}};
    keyword = trim(keyword, html_whitespace);
    for (const auto & [name, percentage] : table) {
        if (ascii_iequals(name, keyword)) { return percentage; }
    }
    return std::nullopt;
}

} // namespace ctbrowser::style::css
