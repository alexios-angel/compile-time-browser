//===- source.cpp - CSSOM source scanning -----------------------*- C++ -*-===//
//
// The CSSOM's handling of source text and object slots: the comment-, string-
// and bracket-aware scanner over a rule's bytes, url() and @page selector
// serialisation, @page and @font-face descriptor grammars, and the
// private-slot and indexed-collection helpers every CSSOM object shares.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace detail {

// --- The one scanner over a rule's bytes ------------------------------------

/// Returns the index of the first byte of `stop` at or after `from` that is at
/// nesting depth zero and outside any string or comment, or `text.size()` when
/// there is none. `\` escapes the byte after it, in a string or out (`\{` in a
/// selector is an ident character, not a block).
///
/// String-, comment- and bracket-aware: `[title="{"]` is a selector with a
/// brace in it, `@media (a:1);` has a semicolon inside parentheses, and
/// `a { /* } */ }` has a brace in a comment. A naive scanner would cut a rule
/// in half at each. All five questions asked of a rule's bytes (block open and
/// close, end of a prelude component, query-list commas, start of the next
/// rule) share this scanner with a different stop set.
[[nodiscard]] std::size_t scan_to(std::string_view text, std::size_t from, std::string_view stop) {
    std::size_t depth = 0;
    char quote = '\0';
    for (std::size_t i = from; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\\') {
            ++i;
        } else if (quote != '\0') {
            if (c == quote) { quote = '\0'; }
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            const std::size_t close = text.find("*/", i + 2);
            if (close == std::string_view::npos) { return text.size(); }
            i = close + 1;
        } else if (depth == 0 && stop.find(c) != std::string_view::npos) {
            return i;
        } else if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if ((c == ')' || c == ']' || c == '}') && depth > 0) {
            --depth;
        }
    }
    return text.size();
}

// --- Splitting one rule into a prelude and a block -------------------------

/// The index of the `{` that opens a rule's block, or npos when it has none.
[[nodiscard]] std::size_t brace_at(std::string_view text) {
    const std::size_t at = scan_to(text, 0, "{");
    return at < text.size() ? at : std::string_view::npos;
}

/// The index of the `}` that closes the block opened at `open`, or npos when
/// the block is unterminated.
[[nodiscard]] std::size_t block_end(std::string_view text, std::size_t open) {
    const std::size_t at = scan_to(text, open + 1, "}");
    return at < text.size() ? at : std::string_view::npos;
}

// --- An at-rule's prelude ---------------------------------------------------
//
// `@import` and `@namespace` have no block, so their prelude is the whole rule
// and the interface reports its pieces one by one: `href`, `media`,
// `supportsText`, `prefix`, `namespaceURI`. Those cannot be answered from the
// author's bytes alone: `@import url(a.css)` and `@import "a.css"` are the
// same URL spelled two ways, and CSSOM serialises both as `url("a.css")`.

/// The next whitespace-delimited component of a prelude, starting at `at` and
/// advancing it, with any bracketed part kept whole:
/// `supports((display: flex) or (a: b))` is one component, and so is
/// `url("a b.css")`.
[[nodiscard]] std::string_view next_component(std::string_view text, std::size_t & at) {
    while (at < text.size() && html_whitespace.find(text[at]) != std::string_view::npos) { ++at; }
    const std::size_t start = at;
    at = scan_to(text, at, html_whitespace);
    return text.substr(start, at - start);
}

/// The value of a `<url>`, whichever of the three spellings the author used:
/// `url("x")`, `url(x)` and a bare `"x"` are one URL, and CSSOM reports the
/// string, not the token. An unquoted `url()` is not unescaped further,
/// because the tokenizer has already done that to the text this came from.
[[nodiscard]] std::string url_value(std::string_view text) {
    std::string_view inner = trim(text, html_whitespace);
    if (inner.size() >= 5 && ascii_iequals(inner.substr(0, 4), "url(") && inner.back() == ')') {
        inner = trim(inner.substr(4, inner.size() - 5), html_whitespace);
    }
    if (inner.size() >= 2 && (inner.front() == '"' || inner.front() == '\'') &&
        inner.back() == inner.front()) {
        std::string out;
        for (std::size_t i = 1; i + 1 < inner.size(); ++i) {
            if (inner[i] == '\\' && i + 2 < inner.size()) { ++i; }
            out += inner[i];
        }
        return out;
    }
    return std::string{inner};
}

/// CSSOM §2.1 "serialize a URL": the keyword, the value as a string, and the
/// closing parenthesis. `url(a.css)` comes back as `url("a.css")` in every
/// engine.
[[nodiscard]] std::string serialize_url(std::string_view value) {
    return "url(" + quoted_string(value) + ")";
}

/// Serialises a CSS Paged Media 3 §3 `<page-selector-list>`: comma-separated
/// `<page-selector>`s, each an optional identifier followed by any number of
/// `:left`, `:right`, `:first` or `:blank`, with no whitespace inside one
/// (`named :first` is invalid). `@page a, b` is one rule for two page names,
/// and an empty entry (`@page , { }`, `a,`) makes the whole list invalid
/// (WPT css/cssom/page-rules-001.html).
///
/// The text is read as tokens, so an escaped name is one identifier
/// (`name1\{\}name2`) and comes back in its canonical escaped form. The name
/// keeps the author's case and the pseudo-pages are lowercased
/// (css/cssom/cssom-pagerule.html writes `:First` and expects `:first`). `ok`
/// is set to false for a list this grammar refuses; CSSOM 6.4.5 says a refused
/// one leaves the rule unchanged.
[[nodiscard]] std::string serialize_page_selector(std::string_view text, bool & ok) {
    using style::css::token_type;
    const style::css::token_stream s = style::css::tokenize(text);
    ok = false;
    std::size_t i = 0;
    const auto type = [&] { return i < s.tokens.size() ? s.tokens[i].type : token_type::eof; };
    const auto skip_whitespace = [&] {
        while (type() == token_type::whitespace) { ++i; }
    };
    skip_whitespace();
    if (type() == token_type::eof) {
        ok = true;
        return {};
    }
    std::string out;
    for (;;) {
        std::string one;
        if (type() == token_type::ident) {
            one = style::css::serialize_identifier(s.value_of(s.tokens[i]));
            ++i;
        }
        while (type() == token_type::colon) {
            ++i;
            if (type() != token_type::ident) { return {}; }
            const std::string pseudo = ascii_lower_copy(s.value_of(s.tokens[i]));
            if (!ascii_iequals_any(pseudo, {"left", "right", "first", "blank"})) { return {}; }
            one += ':' + pseudo;
            ++i;
        }
        if (one.empty()) { return {}; }
        out += (out.empty() ? "" : ", ") + one;
        skip_whitespace();
        if (type() == token_type::eof) { break; }
        if (type() != token_type::comma) { return {}; }
        ++i;
        skip_whitespace();
    }
    ok = true;
    return out;
}

// --- The top level of a sheet, as source spans ------------------------------

/// CSS Syntax 3 §5.4 "consume a list of rules", returning the span of bytes
/// each top-level rule occupies rather than parsing its contents.
///
/// The style front end's `css::stylesheet` cannot be used: it keeps only
/// qualified rules, `@media` and `@font-face`, and drops qualified rules with
/// an empty block (`@media all { * {} }`), so every `cssRules` index after a
/// dropped rule would be wrong. Each span is instead handed to
/// `parse_one_rule`, so a sheet and `insertRule` parse a rule the same way.
[[nodiscard]] std::vector<std::string_view> split_top_level_rules(std::string_view css) {
    std::vector<std::string_view> out;
    std::size_t at = 0;
    while (at < css.size()) {
        // Skip whitespace, comments, and the CDO/CDC pair `<!--` `-->`, which
        // §5.4 drops at the top level and which old HTML puts in a `<style>`.
        if (html_whitespace.find(css[at]) != std::string_view::npos) {
            ++at;
            continue;
        }
        if (css.compare(at, 2, "/*") == 0) {
            const std::size_t close = css.find("*/", at + 2);
            at = close == std::string_view::npos ? css.size() : close + 2;
            continue;
        }
        if (css.compare(at, 4, "<!--") == 0) {
            at += 4;
            continue;
        }
        if (css.compare(at, 3, "-->") == 0) {
            at += 3;
            continue;
        }
        const std::size_t start = at;
        // Only an at-rule ends at a semicolon. A `;` in a qualified rule's
        // prelude is part of the prelude (the spec keeps consuming to the
        // block), so treating it as a terminator would split `a;b { }` into two
        // rules where the sheet parser sees one invalid one.
        const bool at_rule = css[start] == '@';
        std::size_t end = scan_to(css, start, at_rule ? "{;" : "{");
        if (end < css.size() && css[end] == '{') { end = scan_to(css, end + 1, "}"); }
        end = std::min(end + 1, css.size());
        out.push_back(trim(css.substr(start, end - start), html_whitespace));
        at = end;
    }
    return out;
}

// --- JS object helpers --------------------------------------------------------

/// The non-negative number stored under `key` on `obj`, as an index, or
/// no_index when it is absent or not a number.
[[nodiscard]] std::size_t slot_index(script::object_object * obj, std::string_view key) {
    if (obj == nullptr) { return no_index; }
    const value * held = obj->find(key);
    if (held == nullptr || !held->is_number()) { return no_index; }
    const double at = held->as_number();
    if (!(at >= 0)) { return no_index; }
    return static_cast<std::size_t>(at);
}

/// Sets the indexed properties of a platform collection: enumerable,
/// non-writable, non-configurable, plus a `length` that tracks them. Written as
/// plain properties rather than a proxy because the collection is small, is
/// rebuilt whenever it changes, and must keep its own identity (a page's
/// expando on `document.styleSheets` survives a re-read).
///
/// When `own_length` is false (an interface whose `length` is an attribute on
/// its prototype, such as CSSStyleDeclaration; WPT cssom/idlharness: "must
/// inherit property length"), no own `length` is defined and stale indices are
/// removed by erasing upward from items.size() until one is missing.
void set_indexed(script::object_object & obj, std::span<const value> items, bool own_length) {
    if (!own_length) {
        for (std::size_t i = items.size(); obj.erase(std::to_string(i)); ++i) {}
    } else {
        std::size_t was = 0;
        if (const value * held = obj.find("length"); held != nullptr && held->is_number()) {
            const double count = held->as_number();
            if (count > 0) { was = static_cast<std::size_t>(count); }
        }
        for (std::size_t i = items.size(); i < was; ++i) { (void)obj.erase(std::to_string(i)); }
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        obj.define(std::to_string(i), items[i], script::attr_enumerable);
    }
    if (own_length) {
        obj.define("length", value::number(static_cast<double>(items.size())), script::attr_none);
    }
}

/// `item(index)` for every collection here: the indexed property, or null past
/// the end, unlike the indexed getter, which answers `undefined` (both checked
/// by css/cssom/CSSRuleList.html).
[[nodiscard]] value collection_item(context & cx, std::span<value> args) {
    const value self = cx.current_this();
    script::object_object * obj = object_of(self);
    // A proxy receiver (`document.styleSheets` is one) answers through its
    // `get` trap, which is where its list is brought up to date.
    const bool proxied = obj == nullptr && self.is_kind(script::heap_kind::proxy);
    if (obj == nullptr && !proxied) { return value::null(); }
    const double at = args.empty() ? 0 : context::to_number(args[0]);
    // Bounded before the cast: converting a double outside uint32_t's range is
    // undefined behaviour, and `list.item(1e30)` is easy to write.
    // 4294967294 (2^32 - 2) is the largest valid array index.
    if (!(at >= 0) || at > 4294967294.0) { return value::null(); }
    const std::string key = std::to_string(static_cast<std::uint32_t>(at));
    if (proxied) {
        const value found = cx.lookup_property(self, key);
        return found.is_undefined() ? value::null() : found;
    }
    const value * held = obj->find(key);
    return held == nullptr ? value::null() : *held;
}

/// Whether `name: value` may appear in `rule`'s block. Descriptor blocks
/// (@page, @font-face, @counter-style, @font-feature-values) reject
/// sibling-index(), sibling-count() and random(), because there is no element
/// to evaluate them against. A keyframe rejects the animation properties, and
/// @page accepts only its descriptors and the page-context properties.
bool declaration_allowed(const dom_bindings::css_rule_record & rule, std::string_view name,
                         std::string_view value) {
    // A descriptor does not apply to an element, so the tree-counting functions
    // have nothing to count and the declaration is invalid (CSS Values 5
    // §tree-counting; WPT tree-counting/sibling-function-descriptors).
    const bool descriptor = rule.type == page_rule || rule.type == font_face_rule ||
                            rule.type == counter_style_rule ||
                            rule.type == font_feature_values_rule;
    // Nor is there an element to draw a random() for (WPT random-in-descriptors).
    if (descriptor) {
        for (std::size_t i = 0; i + 7 <= value.size(); ++i) {
            const char before = i == 0 ? ' ' : value[i - 1];
            const bool boundary =
                !((before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z') ||
                  (before >= '0' && before <= '9') || before == '-' || before == '_');
            if (boundary && (ascii_iequals(value.substr(i, 14), "sibling-index(") ||
                             ascii_iequals(value.substr(i, 14), "sibling-count(") ||
                             ascii_iequals(value.substr(i, 7), "random("))) {
                return false;
            }
        }
    }
    if (name.starts_with("--")) { return true; }
    // A keyframe takes every property except the animation ones, apart from
    // the two that describe the keyframe itself: its easing and its composite
    // operation (CSS Animations 1 §4.3, 2 §4.4; WPT keyframes-allowed-properties).
    if (rule.type == keyframe_rule) {
        return !name.starts_with("animation") || name == "animation-timing-function" ||
               name == "animation-composition";
    }
    if (rule.type != page_rule) { return true; }
    // The descriptors, then Appendix A's "applicable CSS 2.1 properties" -
    // bidi, background, border, counter, color, font, height, line-height,
    // margin, outline, padding, quotes, text, visibility and width.
    static constexpr std::string_view exact[] = {
        "size",           "page-orientation", "marks",       "bleed",
        "direction",      "unicode-bidi",     "color",       "height",
        "min-height",     "max-height",       "width",       "min-width",
        "max-width",      "line-height",      "quotes",      "visibility",
        "letter-spacing", "text-align",       "text-indent", "text-transform",
        "white-space",    "word-spacing"};
    static constexpr std::string_view prefixes[] = {"margin",   "background",     "border",
                                                    "counter-", "font",           "outline",
                                                    "padding",  "text-decoration"};
    for (const std::string_view each : exact) {
        if (name == each) { return true; }
    }
    for (const std::string_view each : prefixes) {
        if (name.starts_with(each)) { return true; }
    }
    return false;
}

/// The two `@page` descriptors with a grammar of their own (CSS Paged Media 3
/// §7.1-7.2): `size` is `<length>{1,2} | auto | [ <page-size> || [ portrait |
/// landscape ] ]` and `page-orientation` is three keywords. They are not
/// properties, so the property table does not know them. Returns the canonical
/// form (keywords lowercased), or an empty string for an invalid value. Any
/// other name is returned unchanged.
[[nodiscard]] std::string page_descriptor_value(std::string_view name, std::string_view text) {
    if (name == "page-orientation") {
        const std::string word = ascii_lower_copy(trim(text, html_whitespace));
        return word == "upright" || word == "rotate-left" || word == "rotate-right" ? word
                                                                                    : std::string{};
    }
    if (name != "size") { return std::string{text}; }
    static constexpr std::string_view sizes[] = {"a3",     "a4",     "a5",     "b4",    "b5",
                                                 "jis-b4", "jis-b5", "letter", "legal", "ledger"};
    const std::vector<std::string_view> parts = split_top_level(text, " \t\n\r\f");
    if (parts.empty() || parts.size() > 2) { return {}; }
    std::string out;
    int lengths = 0, keywords = 0, orientations = 0;
    for (const std::string_view part : parts) {
        const std::string word = ascii_lower_copy(part);
        if (word == "portrait" || word == "landscape") {
            ++orientations;
        } else if (word == "auto" ||
                   std::find(std::begin(sizes), std::end(sizes), word) != std::end(sizes)) {
            ++keywords;
        } else {
            // outline-offset's grammar is a plain <length>, so it validates and
            // canonicalises one.
            const style::css::value_check checked =
                check_declaration("outline-offset", part, false);
            if (!checked.valid) { return {}; }
            ++lengths;
            out += out.empty() ? "" : " ";
            out += checked.serialized;
            continue;
        }
        out += out.empty() ? "" : " ";
        out += word;
    }
    const bool ok = (lengths == static_cast<int>(parts.size())) ||
                    (lengths == 0 && keywords <= 1 && orientations <= 1);
    if (!ok) { return {}; }
    // `portrait` beside a page size is the size's natural orientation, so the
    // shortest spelling leaves it out: `letter portrait` is `letter`
    // (WPT size-valid).
    if (keywords == 1 && orientations == 1 && lengths == 0) {
        const std::size_t space = out.find(' ');
        const std::string first = out.substr(0, space);
        const std::string second = out.substr(space + 1);
        const bool first_is_size = first != "portrait" && first != "landscape";
        const std::string size = first_is_size ? first : second;
        const std::string orientation = first_is_size ? second : first;
        if (size == "auto") { return {}; }
        return orientation == "portrait" ? size : size + " " + orientation;
    }
    return out;
}

/// `@font-face`'s `src` (CSS Fonts 4 §4.3): a comma list of `<url> [
/// format(<font-format>) ]? [ tech(<font-tech>#) ]?` and `local(<family-name>)`.
/// Each entry is read on its own: one that does not parse is dropped, and the
/// descriptor is invalid only when no entry is left. Returns the canonical text
/// of the remaining entries, or an empty string.
[[nodiscard]] std::string font_face_src(std::string_view text) {
    using style::css::token_type;
    const style::css::token_stream ts = style::css::tokenize(text);
    std::vector<std::vector<std::size_t>> entries(1);
    int depth = 0;
    for (std::size_t i = 0; i < ts.tokens.size(); ++i) {
        const token_type type = ts.tokens[i].type;
        if (type == token_type::eof) { break; }
        if (type == token_type::comma && depth == 0) {
            entries.emplace_back();
            continue;
        }
        if (type == token_type::function || type == token_type::open_paren) { ++depth; }
        if (type == token_type::close_paren && depth > 0) { --depth; }
        if (type != token_type::whitespace || depth > 0) { entries.back().push_back(i); }
    }
    static constexpr std::string_view formats[] = {
        "collection", "embedded-opentype", "opentype", "svg", "truetype", "woff", "woff2"};
    static constexpr std::string_view techs[] = {
        "features-opentype", "features-aat", "features-graphite", "color-colrv0",
        "color-colrv1",      "color-svg",    "color-sbix",        "color-cbdt",
        "variations",        "palettes",     "incremental"};
    const auto name_of = [&](std::size_t i) {
        const std::string_view raw = ts.text_of(ts.tokens[i]);
        return ascii_lower_copy(raw.substr(0, raw.size() - 1));
    };
    // The significant tokens of the function opened at entry[k], or nullopt
    // when it is unclosed; k is left just past its `)`.
    const auto arguments = [&](const std::vector<std::size_t> & entry, std::size_t & k) {
        std::vector<std::size_t> inner;
        int level = 0;
        for (++k; k < entry.size(); ++k) {
            const token_type type = ts.tokens[entry[k]].type;
            if (type == token_type::close_paren && level == 0) {
                ++k;
                return std::optional{inner};
            }
            if (type == token_type::function || type == token_type::open_paren) { ++level; }
            if (type == token_type::close_paren) { --level; }
            if (type != token_type::whitespace) { inner.push_back(entry[k]); }
        }
        return std::optional<std::vector<std::size_t>>{};
    };
    std::string out;
    for (const std::vector<std::size_t> & entry : entries) {
        if (entry.empty()) { continue; }
        std::size_t k = 0;
        std::string one;
        const style::css::css_token & first = ts.tokens[entry[0]];
        if (first.type == token_type::function && name_of(entry[0]) == "local") {
            const std::optional<std::vector<std::size_t>> inner = arguments(entry, k);
            if (!inner || inner->empty() || k != entry.size()) { continue; }
            if (inner->size() == 1 && ts.tokens[inner->front()].type == token_type::string) {
                one = "local(" + std::string{ts.text_of(ts.tokens[inner->front()])} + ")";
            } else {
                std::string family;
                bool ok = true;
                for (const std::size_t at : *inner) {
                    ok = ok && ts.tokens[at].type == token_type::ident;
                    family += (family.empty() ? "" : " ") + std::string{ts.text_of(ts.tokens[at])};
                }
                if (!ok || (inner->size() == 1 && (style::css::is_wide_keyword(family) ||
                                                   ascii_iequals(family, "default")))) {
                    continue;
                }
                one = "local(" + family + ")";
            }
        } else {
            std::string location, modifiers;
            if (first.type == token_type::url) {
                location = ts.text_of(first);
                k = 1;
            } else if (first.type == token_type::function && name_of(entry[0]) == "url") {
                const std::optional<std::vector<std::size_t>> inner = arguments(entry, k);
                if (!inner || inner->empty() ||
                    ts.tokens[inner->front()].type != token_type::string) {
                    continue;
                }
                location = ts.value_of(ts.tokens[inner->front()]);
                // Its <url-modifier>s (CSS Values 4 §4.5.1.1: cross-origin(),
                // integrity(), referrer-policy()), kept as written.
                for (std::size_t n = 1; n < inner->size(); ++n) {
                    const style::css::css_token & t = ts.tokens[(*inner)[n]];
                    const bool tight =
                        t.type == token_type::close_paren || t.type == token_type::comma ||
                        (n > 1 && ts.tokens[(*inner)[n - 1]].type == token_type::function);
                    modifiers += (tight ? "" : " ") + std::string{ts.text_of(t)};
                }
            } else {
                continue;
            }
            one = "url(\"" + location + "\"" + modifiers + ")";
            bool ok = true;
            if (k < entry.size() && ts.tokens[entry[k]].type == token_type::function &&
                name_of(entry[k]) == "format") {
                const std::optional<std::vector<std::size_t>> inner = arguments(entry, k);
                ok = inner && inner->size() == 1;
                if (ok) {
                    const style::css::css_token & t = ts.tokens[inner->front()];
                    const std::string word = ascii_lower_copy(ts.text_of(t));
                    ok = t.type == token_type::string ||
                         (t.type == token_type::ident && ascii_iequals_any(word, formats));
                    one += " format(" +
                           (t.type == token_type::string ? std::string{ts.text_of(t)} : word) + ")";
                }
            }
            if (ok && k < entry.size() && ts.tokens[entry[k]].type == token_type::function &&
                name_of(entry[k]) == "tech") {
                const std::optional<std::vector<std::size_t>> inner = arguments(entry, k);
                ok = inner && !inner->empty() && inner->size() % 2 == 1;
                std::string list;
                for (std::size_t n = 0; ok && n < inner->size(); ++n) {
                    const style::css::css_token & t = ts.tokens[(*inner)[n]];
                    if (n % 2 == 1) {
                        ok = t.type == token_type::comma;
                        continue;
                    }
                    const std::string word = ascii_lower_copy(ts.text_of(t));
                    ok = t.type == token_type::ident && ascii_iequals_any(word, techs);
                    list += (list.empty() ? "" : ", ") + word;
                }
                one += " tech(" + list + ")";
            }
            if (!ok || k != entry.size()) { continue; }
        }
        out += (out.empty() ? "" : ", ") + one;
    }
    return out;
}

/// The `@font-face` descriptors with a grammar checked here (CSS Fonts 4 §4):
/// `src`, the three metric overrides (`[ normal | <percentage [0,∞]> ]{1,2}`)
/// and `size-adjust` (a `<percentage [0,∞]>`). Returns the canonical text, an
/// empty string for a value the grammar refuses, and nullopt for any other
/// descriptor.
[[nodiscard]] std::optional<std::string> font_face_descriptor(std::string_view name,
                                                              std::string_view text) {
    if (name == "src") { return font_face_src(text); }
    const bool metric =
        name == "ascent-override" || name == "descent-override" || name == "line-gap-override";
    if (!metric && name != "size-adjust") { return std::nullopt; }
    const style::css::token_stream ts = style::css::tokenize(text);
    std::string out;
    std::size_t count = 0;
    for (const style::css::css_token & t : ts.tokens) {
        if (t.type == style::css::token_type::eof) { break; }
        if (t.type == style::css::token_type::whitespace) { continue; }
        const bool normal = metric && t.type == style::css::token_type::ident &&
                            ascii_iequals(ts.text_of(t), "normal");
        const bool share = t.type == style::css::token_type::percentage && t.number >= 0;
        if ((!normal && !share) || ++count > (metric ? 2u : 1u)) { return std::string{}; }
        out += (out.empty() ? "" : " ") +
               (normal ? std::string{"normal"} : ascii_lower_copy(ts.text_of(t)));
    }
    return out;
}

/// Writes one declaration into a rule's block through the same functions
/// `el.style` uses: an invalid value is a no-op, an empty one removes the
/// property, and a shorthand is stored as its longhands. `@font-face` and
/// `@page` descriptors are checked against their own grammars first. Returns
/// whether the block changed.
bool store_declaration(dom_bindings::css_rule_record & rule, const std::string & css_name,
                       std::string_view text, bool important) {
    if (!declaration_allowed(rule, css_name, text)) { return false; }
    if (const std::optional<std::string> canonical =
            rule.type == font_face_rule && !trim(text, html_whitespace).empty()
                ? font_face_descriptor(css_name, text)
                : std::nullopt) {
        if (canonical->empty()) { return false; }
        return style::css::set_declaration(rule.declarations, css_name, *canonical, important);
    }
    if (rule.type == page_rule && (css_name == "size" || css_name == "page-orientation") &&
        !trim(text, html_whitespace).empty()) {
        const std::string canonical = page_descriptor_value(css_name, text);
        if (canonical.empty()) { return false; }
        return style::css::set_declaration(rule.declarations, css_name, canonical, important);
    }
    return style::css::set_declaration(rule.declarations, css_name, text, important);
}

/// Parses a block's declarations through the same parser a `style` attribute
/// uses. A value the grammar refuses is one the cascade drops, so keeping it
/// would advertise a declaration that does not apply.
void parse_declarations_into(dom_bindings::css_rule_record & rule, std::string_view body) {
    style::css::parse_declaration_block(
        rule.declarations, body,
        [](std::string_view name, std::string_view value, const void * ctx) {
            return declaration_allowed(*static_cast<const dom_bindings::css_rule_record *>(ctx),
                                       name, value);
        },
        &rule);
    if (rule.type == font_face_rule) {
        std::erase_if(rule.declarations, [](style::css::declaration & d) {
            std::optional<std::string> canonical = font_face_descriptor(d.name, d.value);
            if (!canonical) { return false; }
            d.value = std::move(*canonical);
            return d.value.empty();
        });
    }
    if (rule.type != page_rule) { return; }
    std::erase_if(rule.declarations, [](style::css::declaration & d) {
        if (d.name != "size" && d.name != "page-orientation") { return false; }
        d.value = page_descriptor_value(d.name, d.value);
        return d.value.empty();
    });
}

void detach_rule(std::vector<std::unique_ptr<dom_bindings::css_rule_record>> & store,
                 std::size_t rule) {
    if (rule >= store.size()) { return; }
    store[rule]->sheet = no_index;
    store[rule]->parent = no_index;
    for (const std::size_t child : store[rule]->children) { detach_rule(store, child); }
}

void mirror_rule_list(script::object_object & rule_obj) {
    const value * list = rule_obj.find(rules_key);
    script::object_object * held = list == nullptr ? nullptr : object_of(*list);
    if (held == nullptr) { return; }
    std::vector<value> items;
    if (const value * length = held->find("length"); length != nullptr && length->is_number()) {
        const double count = length->as_number();
        for (std::size_t i = 0; i < (count > 0 ? static_cast<std::size_t>(count) : 0); ++i) {
            if (const value * item = held->find(std::to_string(i))) { items.push_back(*item); }
        }
    }
    set_indexed(rule_obj, items);
}

/// A property name as the CSSOM's own methods take it: a custom property keeps
/// its case, everything else is lowercased. `setProperty`/`getPropertyValue`
/// are the only way to reach `--x`, which no identifier can name.
[[nodiscard]] std::string asked_name(context & cx, std::span<value> args) {
    const std::string given = arg_string(cx, args, 0);
    return given.starts_with("--") ? given : ascii_lower_copy(given);
}

} // namespace detail

} // namespace ctbrowser::shell
