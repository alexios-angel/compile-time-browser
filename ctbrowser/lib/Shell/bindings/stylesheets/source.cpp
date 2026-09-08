// dom_bindings' CSSOM - the source text and the object slots: the comment-,
// string- and bracket-aware scanner over a rule's bytes, url() and @page
// serialisation, and the private-slot helpers every CSSOM object shares.
//
// One of six files carved out of a 2,814-line bindings/stylesheets.cpp on
// 2026-09-08. The member functions belong to one class declared in
// include/ctbrowser/shell/bindings.hpp; the helpers more than one of these
// files needs are declared in internal.hpp beside this and defined in
// serialize.cpp and source.cpp. Nothing about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace detail {

// --- splitting one rule into a prelude and a block -------------------------
//
// QUOTE-AWARE AND NOTHING ELSE, which is the whole trick: `[title="{"]` is a
// selector with a brace in it, and a scanner that did not know about strings
// would cut the rule in half there. That is the same defect the CSS front end
// was written to fix (a `;` inside a string ending a declaration), answered the
// same way one level up.

[[nodiscard]] std::size_t brace_at(std::string_view text) {
    char quote = '\0';
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote != '\0') {
            if (c == '\\') {
                ++i;
            } else if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '{') {
            return i;
        }
    }
    return std::string_view::npos;
}

[[nodiscard]] std::size_t block_end(std::string_view text, std::size_t open) {
    char quote = '\0';
    std::size_t depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        const char c = text[i];
        if (quote != '\0') {
            if (c == '\\') {
                ++i;
            } else if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) { return i; }
        }
    }
    return std::string_view::npos;
}

// --- an at-rule's prelude ---------------------------------------------------
//
// `@import` and `@namespace` have no block, so their prelude is the WHOLE rule
// and the interface reports its pieces one by one: `href`, `media`,
// `supportsText`, `prefix`, `namespaceURI`. Answering those from the author's
// bytes is not possible - `@import url(a.css)` and `@import "a.css"` are the
// same URL spelled two ways, and CSSOM serialises both as `url("a.css")`.

// The next whitespace-delimited component of a prelude, with any bracketed part
// of it kept whole: `supports((display: flex) or (a: b))` is ONE component, and
// so is `url("a b.css")`.
[[nodiscard]] std::string_view next_component(std::string_view text, std::size_t & at) {
    while (at < text.size() && html_whitespace.find(text[at]) != std::string_view::npos) { ++at; }
    const std::size_t start = at;
    std::size_t depth = 0;
    char quote = '\0';
    while (at < text.size()) {
        const char c = text[at];
        if (quote != '\0') {
            if (c == '\\') {
                ++at;
            } else if (c == quote) {
                quote = '\0';
            }
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(' || c == '[') {
            ++depth;
        } else if ((c == ')' || c == ']') && depth > 0) {
            --depth;
        } else if (depth == 0 && html_whitespace.find(c) != std::string_view::npos) {
            break;
        }
        ++at;
    }
    return text.substr(start, at - start);
}

// The VALUE of a `<url>`, whichever of the three spellings the author used:
// `url("x")`, `url(x)` and a bare `"x"` are one URL and CSSOM reports the
// string, not the token. A `url()` unquoted inside is NOT unescaped further -
// the tokenizer has already done that to the pool this came from.
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

// CSSOM §2.1's "serialize a URL": the keyword, the value as a STRING, and the
// closing parenthesis. `url(a.css)` comes back `url("a.css")` from every engine.
[[nodiscard]] std::string serialize_url(std::string_view value) {
    return "url(" + quoted_string(value) + ")";
}

// CSS Paged Media 3 §3's `<page-selector>`: an optional identifier followed by
// any number of `:left`, `:right`, `:first` or `:blank`, with NO whitespace
// anywhere in it - `named :first` is two selectors and therefore not one.
//
// The name keeps the author's case and the pseudo-pages are lowercased, which
// is the split `css/cssom/cssom-pagerule.html` asserts by writing `:First` and
// demanding `:first` back. `ok` is false for a selector this grammar refuses,
// and CSSOM 6.4.5 says a refused one leaves the rule alone.
[[nodiscard]] std::string serialize_page_selector(std::string_view text, bool & ok) {
    ok = true;
    const std::string_view one = trim(text, html_whitespace);
    if (one.empty()) { return {}; }
    if (one.find_first_of(html_whitespace) != std::string_view::npos) {
        ok = false;
        return {};
    }
    const auto ident_char = [](char c, bool first) {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 0x80 || c == '_' || c == '-' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            return true;
        }
        return !first && c >= '0' && c <= '9';
    };
    std::size_t at = 0;
    std::string out;
    while (at < one.size() && one[at] != ':') {
        if (!ident_char(one[at], at == 0)) {
            ok = false;
            return {};
        }
        out += one[at];
        ++at;
    }
    while (at < one.size()) {
        const std::size_t next = one.find(':', at + 1);
        const std::string_view pseudo =
            one.substr(at + 1, (next == std::string_view::npos ? one.size() : next) - at - 1);
        if (!ascii_iequals(pseudo, "left") && !ascii_iequals(pseudo, "right") &&
            !ascii_iequals(pseudo, "first") && !ascii_iequals(pseudo, "blank")) {
            ok = false;
            return {};
        }
        out += ':';
        out += ascii_lower_copy(pseudo);
        at = next == std::string_view::npos ? one.size() : next;
    }
    return out;
}

// --- the top level of a sheet, as source spans ------------------------------
//
// CSS Syntax 3 §5.4's "consume a list of rules", stopping at the SPAN of bytes
// each rule occupies rather than parsing what is in it.
//
// It exists because the style front end's `css::stylesheet` has nowhere to put
// most of them. It models qualified rules, `@media` and `@font-face` and
// DISCARDS every other at-rule, so a CSSOM assembled from that structure does
// not report an `@import`, an `@namespace`, a `@page` or a `@keyframes` at all -
// and the damage is not the missing rule, it is that `cssRules[0]` is then the
// wrong rule and every index after it is off by one. It also drops a qualified
// rule with an EMPTY block, which is what `@media all { * {} }` is made of and
// what every fixture in `css/cssom/CSSGroupingRule-*.html` builds.
//
// Splitting here and handing each span to `parse_one_rule` gives a sheet and
// `insertRule` ONE answer to what a rule is - the same reason `insertRule` does
// not have a parser of its own.
//
// COMMENT-, STRING- AND BRACKET-AWARE, which is the whole trick: `[title="{"]`
// is a selector with a brace in it and `@media (a:1);` has a semicolon inside
// parentheses. A scanner that knew about none of the three would cut a rule in
// half at each, which is the same defect the CSS front end was written to fix
// one level down.
[[nodiscard]] std::vector<std::string_view> split_top_level_rules(std::string_view css) {
    std::vector<std::string_view> out;
    std::size_t at = 0;
    while (at < css.size()) {
        // Whitespace, comments, and the CDO/CDC pair `<!--` `-->`, which §5.4
        // drops at the top level and which a `<style>` inside old HTML has.
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
        // ONLY AN AT-RULE ENDS AT A SEMICOLON. A `;` in a qualified rule's
        // prelude is part of the prelude - the spec keeps consuming to the
        // block - so treating one as a terminator would split `a;b { }` into two
        // rules where the sheet parser sees one bad one.
        const bool at_rule = css[start] == '@';
        char quote = '\0';
        std::size_t brackets = 0;
        std::size_t braces = 0;
        bool block = false;
        std::size_t end = css.size();
        for (std::size_t i = start; i < css.size(); ++i) {
            const char c = css[i];
            if (quote != '\0') {
                if (c == '\\') {
                    ++i;
                } else if (c == quote) {
                    quote = '\0';
                }
                continue;
            }
            if (c == '/' && i + 1 < css.size() && css[i + 1] == '*') {
                const std::size_t close = css.find("*/", i + 2);
                i = close == std::string_view::npos ? css.size() : close + 1;
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '\\') {
                ++i;
            } else if (c == '(' || c == '[') {
                ++brackets;
            } else if ((c == ')' || c == ']') && brackets > 0) {
                --brackets;
            } else if (brackets > 0) {
                continue;
            } else if (c == '{') {
                ++braces;
                block = true;
            } else if (c == '}') {
                if (braces > 0) { --braces; }
                if (braces == 0 && block) {
                    end = i + 1;
                    break;
                }
            } else if (c == ';' && at_rule && braces == 0) {
                end = i + 1;
                break;
            }
        }
        out.push_back(trim(css.substr(start, end - start), html_whitespace));
        at = end;
    }
    return out;
}

// --- the JS side, in general -----------------------------------------------

[[nodiscard]] script::object_object * as_object(value v) {
    return v.is_object() ? static_cast<script::object_object *>(v.as_heap()) : nullptr;
}

[[nodiscard]] std::size_t slot_index(script::object_object * obj, std::string_view key) {
    if (obj == nullptr) { return no_index; }
    const value * held = obj->find(key);
    if (held == nullptr || !held->is_number()) { return no_index; }
    const double at = held->as_number();
    if (!(at >= 0)) { return no_index; }
    return static_cast<std::size_t>(at);
}

// The indexed properties of a platform collection: enumerable, non-writable,
// non-configurable, plus a `length` that tracks them. Written this way rather
// than as a proxy because the collection is small, is rebuilt whenever it
// changes, and has to keep its OWN identity - a page's expando on
// `document.styleSheets` survives a re-read.
void set_indexed(script::object_object & obj, std::span<const value> items) {
    std::size_t was = 0;
    if (const value * held = obj.find("length"); held != nullptr && held->is_number()) {
        const double count = held->as_number();
        if (count > 0) { was = static_cast<std::size_t>(count); }
    }
    for (std::size_t i = items.size(); i < was; ++i) { (void)obj.erase(std::to_string(i)); }
    for (std::size_t i = 0; i < items.size(); ++i) {
        obj.define(std::to_string(i), items[i], script::attr_enumerable);
    }
    obj.define("length", value::number(static_cast<double>(items.size())), script::attr_none);
}

// `item(index)` for every collection here: the indexed property, or NULL past
// the end - which is the difference from the indexed getter, whose answer is
// `undefined`. Both are asserted, separately, in `css/cssom/CSSRuleList.html`.
[[nodiscard]] value collection_item(context & cx, std::span<value> args) {
    const value self = cx.current_this();
    script::object_object * obj = as_object(self);
    if (obj == nullptr) { return value::null(); }
    const double at = args.empty() ? 0 : context::to_number(args[0]);
    // BOUNDED BEFORE THE CAST. `list.item(1e30)` is one keystroke, and a
    // float-to-integer conversion out of the destination's range is undefined
    // behaviour rather than a large number - the same hazard `el.style[1e30]`
    // has in element/views.cpp, answered the same way.
    if (!(at >= 0) || at > 4294967294.0) { return value::null(); }
    const value * held = obj->find(std::to_string(static_cast<std::uint32_t>(at)));
    return held == nullptr ? value::null() : *held;
}

// --- the declaration block, serialised -------------------------------------
//
// CSSOM 6.7.2: each declaration is `name: value;`, with ` !important` before the
// semicolon, and the block joins them with a single space.
[[nodiscard]] std::string serialize_block(
    const std::vector<dom_bindings::css_declaration> & block) {
    std::string out;
    for (const dom_bindings::css_declaration & declared : block) {
        if (!out.empty()) { out += ' '; }
        out += declared.name;
        out += ": ";
        out += declared.value;
        if (declared.important) { out += " !important"; }
        out += ';';
    }
    return out;
}

// ONE WRITE THROUGH THE VALUE GRAMMAR, the same rule `el.style` follows: an
// invalid value is a NO-OP and an empty one REMOVES the declaration.
bool store_declaration(std::vector<dom_bindings::css_declaration> & block,
                       const std::string & css_name, std::string_view text, bool allow_important,
                       bool force_important) {
    const style::css::value_check checked = check_declaration(css_name, text, allow_important);
    const auto found =
        std::find_if(block.begin(), block.end(), [&](const dom_bindings::css_declaration & each) {
            return each.name == css_name;
        });
    if (!checked.valid) {
        if (trim(text, html_whitespace).empty()) {
            if (found != block.end()) { block.erase(found); }
            return true;
        }
        return false;
    }
    const bool important = checked.important || force_important;
    if (found != block.end()) {
        found->value = checked.serialized;
        found->important = important;
        return true;
    }
    block.push_back(dom_bindings::css_declaration{css_name, checked.serialized, important});
    return true;
}

// A property name as the CSSOM's own methods take it: a custom property keeps
// its case, everything else is lowercased. `setProperty`/`getPropertyValue` are
// the only way to reach `--x`, which no identifier can name.
[[nodiscard]] std::string asked_name(context & cx, std::span<value> args) {
    const std::string given = arg_string(cx, args, 0);
    return given.starts_with("--") ? given : ascii_lower_copy(given);
}

} // namespace detail

} // namespace ctbrowser::shell
