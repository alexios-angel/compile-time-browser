// dom_bindings - the CSSOM: `document.styleSheets` and everything under it.
//
// WHERE THE DATA COMES FROM, which is the whole design decision.
//
// The cascade does not keep a stylesheet. `style::engine::add_sheet` FLATTENS
// what it is given into (selector, declaration) rules filed by their rightmost
// simple selector and throws the `css::stylesheet` away, so there is nothing in
// the engine to hand a page. The FRONT END, though, is a public header-only API
// - `style/css/parser.hpp` - so the CSSOM parses the document's own `<style>`
// and `<link rel=stylesheet>` text a second time, with the SAME two rules
// `browser::load_author_styles` uses (HTML namespace only, and the same `rel`
// token test), so the object model and the cascade cannot disagree about WHICH
// sheets exist.
//
// The parse result is converted to OWNED strings immediately and the
// `css::stylesheet` is dropped. Holding it would mean holding a container that
// never moves - every string_view in a sheet points into its `pool` - and it
// buys nothing, because everything answered below is a SERIALISATION.
//
// SERIALISATION, AND WHY IT IS NOT THE AUTHOR'S BYTES. `cssText` and
// `selectorText` are compared as strings by these tests, and there are two
// things they could be:
//
//   * the source text. The sheet keeps it (`pool`, `text_of`, the token range on
//     every component value) - but a RULE has no recorded source span at all,
//     only its compiled selectors and its declarations, and even if it had one
//     the answer would be wrong: `css/cssom/CSSRuleList.html` writes its rules
//     indented across two lines and asserts `"body { width: 50%; }"`. Source
//     text answers that with the newlines and the indentation in it.
//   * the canonical serialisation the specification defines. CSSOM 6.7.2 for a
//     declaration block, Selectors 4 §serialize for a selector list.
//
// SO: CANONICAL, everywhere, and the two halves are built out of what the engine
// already has rather than a second opinion:
//
//   * a DECLARATION goes through `style::css::check_declaration`, which is the
//     same function `el.style` writes through. So `rule.style.width` and
//     `el.style.width` cannot canonicalise one value two ways - and a value the
//     grammar REFUSES is dropped here exactly as the cascade drops it, so
//     `cssRules` cannot advertise a declaration that does not apply.
//   * a SELECTOR is serialised from the `compiled_selector` the cascade matches
//     on, so `selectorText` cannot claim something the matcher does not do.
//
// The one place the author's bytes survive is an AT-RULE whose block this front
// end discards (`@keyframes`, `@page`, `@supports`), where the alternative is
// reporting nothing at all. Those are marked verbatim and say so.
//
// WHAT THE CASCADE DOES NOT SEE, stated here rather than discovered.
// `insertRule`, `deleteRule`, `replaceSync` and `disabled` change the object
// model correctly and completely; nothing in this file can reach
// `style::engine`, because the browser loads the author sheet exactly once per
// page (`browser::author_sheet_loaded_`) and rebuilding the cascade is its
// business. `set_author_styles_hook` is the slot: with a hook installed the
// browser is handed the new author CSS and re-runs the cascade; with none the
// render simply does not move. It is a hook rather than a lie because writing
// the rules into the elements' inline styles - the only thing this file COULD
// do on its own - would put the CSSOM and the cascade permanently out of step.

#include <ctbrowser/shell/bindings.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/parser.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <ctbrowser/style/css/value.hpp>
#include <ctbrowser/style/selector.hpp>

namespace ctbrowser::shell {
namespace {

using ctbrowser::style::css::check_declaration;
using ctbrowser::style::css::css_name_of;
using ctbrowser::style::css::idl_name_of;
using ctbrowser::style::css::known_properties;

constexpr std::size_t no_index = static_cast<std::size_t>(-1);

// The private slots. Non-enumerable, non-writable and non-configurable, under
// names no author would write - the same device `bindings/exceptions.cpp` uses,
// and for the same reason: this engine has no internal-slot mechanism, and a
// CSSOM object's index into the C++ record store has to live somewhere script
// cannot see it in `Object.keys` or delete out from under a getter.
constexpr std::string_view internals_key = "__ctbrowser_cssom";
constexpr std::string_view sheet_key = "__ctbrowser_sheet";
constexpr std::string_view rule_key = "__ctbrowser_rule";
constexpr std::string_view rules_key = "__ctbrowser_rules";
constexpr std::string_view style_key = "__ctbrowser_style";
constexpr std::string_view media_key = "__ctbrowser_media";

// CSSRule's type constants. Only the ones this file can produce are ever set on
// a rule; all of them are exposed, because a page reads `rule.MEDIA_RULE` to
// compare against whatever it was given.
constexpr std::uint32_t style_rule = 1;
constexpr std::uint32_t import_rule = 3;
constexpr std::uint32_t media_rule = 4;
constexpr std::uint32_t font_face_rule = 5;
constexpr std::uint32_t page_rule = 6;
constexpr std::uint32_t keyframes_rule = 7;
constexpr std::uint32_t keyframe_rule = 8;
constexpr std::uint32_t namespace_rule = 10;
constexpr std::uint32_t counter_style_rule = 11;
constexpr std::uint32_t supports_rule = 12;

// WHAT AN AT-RULE'S BLOCK CONTAINS - the one thing the at-keyword decides that
// nothing else can, and the reason a rule's contents can be modelled at all
// rather than kept as the author's bytes. There are exactly three answers:
// a list of RULES, a list of DECLARATIONS, or no block, and getting it wrong
// means reporting `@page`'s margins as child rules or `@media`'s rules as
// declarations. Anything not named here keeps its bytes and says nothing.
[[nodiscard]] bool at_rule_holds_rules(std::string_view name) {
    return name == "media" || name == "supports" || name == "container" || name == "keyframes" ||
           name == "-webkit-keyframes" || name == "scope" || name == "starting-style";
}

[[nodiscard]] bool at_rule_holds_declarations(std::string_view name) {
    return name == "font-face" || name == "page" || name == "counter-style" || name == "property" ||
           name == "font-palette-values" || name == "view-transition";
}

// A SECOND COPY OF browser.cpp's rel test, and it has to be one: that function
// is in an anonymous namespace in a file this rung does not own, and the CSSOM
// answering a different question from the cascade about which links are
// stylesheets is exactly the disagreement this file exists to avoid. `rel` is a
// space-separated token list matched ASCII case-insensitively, and `alternate
// stylesheet` is NOT one - it is user-selectable and a browser leaves it
// disabled.
[[nodiscard]] bool rel_is_stylesheet(std::string_view rel) {
    bool stylesheet = false;
    std::size_t at = 0;
    while (at < rel.size()) {
        const std::size_t start = rel.find_first_not_of(html_whitespace, at);
        if (start == std::string_view::npos) { break; }
        std::size_t end = rel.find_first_of(html_whitespace, start);
        if (end == std::string_view::npos) { end = rel.size(); }
        const std::string_view token = rel.substr(start, end - start);
        if (ascii_iequals(token, "alternate")) { return false; }
        if (ascii_iequals(token, "stylesheet")) { stylesheet = true; }
        at = end;
    }
    return stylesheet;
}

// --- serialising a selector ------------------------------------------------
//
// Selectors 4 §serialize, over the COMPILED form. The compiled form has lost
// the author's ordering within a compound (`a.b#c` and `a#c.b` compile the
// same), so this emits the canonical order the specification asks for: type,
// then id, then classes, then attributes, then pseudo-classes.

// A CSS string, quoted and escaped. An attribute selector's value is serialised
// as a string whatever the author wrote it as - `[a=b]` comes back `[a="b"]`.
[[nodiscard]] std::string quoted_string(std::string_view text) {
    std::string out;
    out += '"';
    for (const char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; }
        out += c;
    }
    out += '"';
    return out;
}

// `An+B`, the form `:nth-child()` and its three siblings take.
[[nodiscard]] std::string an_plus_b(std::int32_t a, std::int32_t b) {
    if (a == 0) { return std::to_string(b); }
    std::string out;
    if (a == 1) {
        out = "n";
    } else if (a == -1) {
        out = "-n";
    } else {
        out = std::to_string(a) + "n";
    }
    if (b > 0) { out += "+" + std::to_string(b); }
    if (b < 0) { out += "-" + std::to_string(-b); }
    return out;
}

void append_compound(std::string & out, const style::compound & part, const atom_table & atoms);

[[nodiscard]] std::string serialize_selector(const style::compiled_selector & sel,
                                             const atom_table & atoms) {
    std::string out;
    if (sel.parts.empty()) { return out; }
    // Parts are stored RIGHTMOST FIRST and `links[i]` joins parts[i] to
    // parts[i+1], so the text runs from the last part backwards and the link
    // between parts[i] and parts[i-1] is links[i-1].
    for (std::size_t i = sel.parts.size(); i-- > 0;) {
        append_compound(out, sel.parts[i], atoms);
        if (i == 0) { break; }
        const style::combinator link =
            i - 1 < sel.links.size() ? sel.links[i - 1] : style::combinator::descendant;
        switch (link) {
        case style::combinator::child: out += " > "; break;
        case style::combinator::next_sibling: out += " + "; break;
        case style::combinator::subsequent_sibling: out += " ~ "; break;
        case style::combinator::none:
        case style::combinator::descendant: out += ' '; break;
        }
    }
    return out;
}

[[nodiscard]] std::string serialize_selector_list(std::span<const style::compiled_selector> list,
                                                  const atom_table & atoms) {
    std::string out;
    for (const style::compiled_selector & sel : list) {
        if (!out.empty()) { out += ", "; }
        out += serialize_selector(sel, atoms);
    }
    return out;
}

// IS THE COMPILED FORM THE WHOLE OF WHAT THE AUTHOR WROTE?
//
// `never_matches` is how the selector compiler records a construct it can PARSE
// and cannot MATCH: a pseudo-element, a namespace prefix, a pseudo-class it does
// not model. Nothing of that compound survives into the compiled form, so
// `append_compound` finds an empty compound and emits `*` for it - and that does
// not merely look wrong. `author_style_text` hands these selectors back to the
// cascade, so an inserted `::before { color: red }` serialised as `*` would
// paint every element on the page red. Wherever the author's bytes are still to
// hand they are the honest answer, and this is the question that decides.
[[nodiscard]] bool representable(std::span<const style::compiled_selector> list) {
    const auto compound_ok = [](auto && self, const style::compound & part) -> bool {
        if (part.never_matches) { return false; }
        for (const style::pseudo_ref & pseudo : part.pseudos) {
            for (const style::compiled_selector & inner : pseudo.args) {
                for (const style::compound & nested : inner.parts) {
                    if (!self(self, nested)) { return false; }
                }
            }
        }
        return true;
    };
    for (const style::compiled_selector & sel : list) {
        for (const style::compound & part : sel.parts) {
            if (!compound_ok(compound_ok, part)) { return false; }
        }
    }
    return !list.empty();
}

void append_compound(std::string & out, const style::compound & part, const atom_table & atoms) {
    const std::size_t was = out.size();
    // EVERY NAME IS AN IDENTIFIER, and CSSOM §6.7 says each is serialised by
    // §2.1's "serialize an identifier" - the same algorithm `CSS.escape` is.
    // The tokenizer DECODES escapes, so `[\30 zonk]` reaches the compiled form
    // as the name `0zonk`; printing that back unescaped produces a selector that
    // is not a selector, because an identifier may not begin with a digit.
    const auto ident = &dom_bindings::serialize_css_identifier;
    if (part.tag) { out += ident(atoms.text(part.tag)); }
    if (part.id) {
        out += '#';
        out += ident(atoms.text(part.id));
    }
    for (const atom cls : part.classes) {
        out += '.';
        out += ident(atoms.text(cls));
    }
    for (const style::attribute_match & attribute : part.attributes) {
        out += '[';
        out += ident(atoms.text(attribute.name));
        switch (attribute.op) {
        case style::attr_op::present: break;
        case style::attr_op::exact: out += '='; break;
        case style::attr_op::includes: out += "~="; break;
        case style::attr_op::dash: out += "|="; break;
        case style::attr_op::prefix: out += "^="; break;
        case style::attr_op::suffix: out += "$="; break;
        case style::attr_op::substring: out += "*="; break;
        }
        if (attribute.op != style::attr_op::present) {
            out += quoted_string(attribute.value);
            if (attribute.case_insensitive) { out += " i"; }
        }
        out += ']';
    }
    // The pseudo-classes. `:checked` and `:disabled` are reachable through TWO
    // bitfields - they were state bits before they were structural ones and both
    // spellings are still declared - so the names are de-duplicated rather than
    // emitted twice.
    struct named_bit {
        std::uint32_t bit;
        std::string_view name;
    };
    static constexpr named_bit state_bits[] = {{style::state_hover, "hover"},
                                               {style::state_active, "active"},
                                               {style::state_focus, "focus"},
                                               {style::state_checked, "checked"},
                                               {style::state_disabled, "disabled"}};
    static constexpr named_bit structural_bits[] = {
        {style::structural_root, "root"},
        {style::structural_empty, "empty"},
        {style::structural_first_child, "first-child"},
        {style::structural_last_child, "last-child"},
        {style::structural_only_child, "only-child"},
        {style::structural_first_of_type, "first-of-type"},
        {style::structural_last_of_type, "last-of-type"},
        {style::structural_only_of_type, "only-of-type"},
        {style::structural_disabled, "disabled"},
        {style::structural_enabled, "enabled"},
        {style::structural_checked, "checked"},
        {style::structural_link, "link"},
        {style::structural_visited, "visited"}};
    std::vector<std::string_view> emitted;
    const auto emit = [&](std::string_view name) {
        if (std::find(emitted.begin(), emitted.end(), name) != emitted.end()) { return; }
        emitted.push_back(name);
        out += ':';
        out += name;
    };
    for (const named_bit & each : state_bits) {
        if ((part.states & each.bit) != 0) { emit(each.name); }
    }
    for (const named_bit & each : structural_bits) {
        if ((part.structural & each.bit) != 0) { emit(each.name); }
    }
    for (const style::pseudo_ref & pseudo : part.pseudos) {
        switch (pseudo.kind) {
        case style::pseudo_kind::nth_child:
            out += ":nth-child(" + an_plus_b(pseudo.a, pseudo.b) + ")";
            break;
        case style::pseudo_kind::nth_last_child:
            out += ":nth-last-child(" + an_plus_b(pseudo.a, pseudo.b) + ")";
            break;
        case style::pseudo_kind::nth_of_type:
            out += ":nth-of-type(" + an_plus_b(pseudo.a, pseudo.b) + ")";
            break;
        case style::pseudo_kind::nth_last_of_type:
            out += ":nth-last-of-type(" + an_plus_b(pseudo.a, pseudo.b) + ")";
            break;
        case style::pseudo_kind::not_:
            out += ":not(" + serialize_selector_list(pseudo.args, atoms) + ")";
            break;
        case style::pseudo_kind::is_:
            out += ":is(" + serialize_selector_list(pseudo.args, atoms) + ")";
            break;
        case style::pseudo_kind::where_:
            out += ":where(" + serialize_selector_list(pseudo.args, atoms) + ")";
            break;
        // `:lang()` AND `:dir()` KEEP THEIR ARGUMENT AS WRITTEN, because a
        // language RANGE is not an identifier: `*-Latn` is a legal one, and
        // `:lang(de, fr)` is a comma-separated list of them. They used to
        // compile to nothing at all, so a rule carrying one serialised as `*` -
        // eight of `css/cssom/selectorSerialize.html`'s twenty-three.
        case style::pseudo_kind::lang:
        case style::pseudo_kind::dir: {
            out += pseudo.kind == style::pseudo_kind::lang ? ":lang(" : ":dir(";
            bool first = true;
            for (const std::string & range : pseudo.ranges) {
                if (!first) { out += ", "; }
                first = false;
                out += range;
            }
            out += ')';
            break;
        }
        }
    }
    // "If there is only one simple selector in the compound selector which is a
    // universal selector, append '*'." A compound that produced nothing is that
    // selector - and it is also what a compound this engine could not represent
    // (`never_matches`, which is how a pseudo-ELEMENT compiles) degrades to.
    if (out.size() == was) { out += '*'; }
}

// A `@media` PRELUDE IS THE AUTHOR'S BYTES, always.
//
// There used to be a second serialiser here, over the compiled `media_query`
// AST, for the one caller that had no source text: a `@media` recovered from
// `style::css::parse_stylesheet`, which keeps a rule's condition as a compiled
// index and no span back to the bytes. It was lossy and said so - `(min-width:
// 48em)` came back in px, `speech` came back as `all`, and a feature the
// cascade does not model came back as nothing - so a `@media` from a `<style>`
// and the same one from `insertRule` serialised two different ways.
//
// `parse_sheet_rules` reads the source now, so that caller is gone and with it
// the only reason to reconstruct a prelude from an AST at all.

// --- a media query list, as CSSOM asks for it ------------------------------
//
// MEDIA QUERIES 4 §"serializing a media query list", over the query's TEXT
// rather than over the compiled `media_query`. The AST is what the CASCADE needs
// and it is deliberately narrow - three media types, twelve features, one float
// per value - so it answers `all` for `speech`, `768px` for `48em` and nothing
// at all for a feature this engine does not model. Every one of those is a
// string `css/cssom/serialize-media-rule.html` compares byte for byte, so the
// object model normalises the text instead and never consults the AST when it
// has the author's bytes.
//
// The normalisation is exactly the specification's and no more: lowercase the
// `not`/`only`, the media type and each feature NAME; put one space after a
// feature's colon; drop an `all` that has features after it and keep a negated
// one; and preserve the order and the multiplicity of the features, because
// `(max-width: 23px) and (max-width: 45px)` is a query a page may have written
// on purpose and de-duplicating it is an open CSSWG issue.

[[nodiscard]] std::string collapse_whitespace(std::string_view text) {
    std::string out;
    bool space = false;
    for (const char c : trim(text, html_whitespace)) {
        if (html_whitespace.find(c) != std::string_view::npos) {
            space = true;
            continue;
        }
        if (space && !out.empty()) { out += ' '; }
        space = false;
        out += c;
    }
    return out;
}

// The top-level commas of a media query list. Top-level because a feature's
// parentheses may hold one - `(width >= calc(1px, 2px))` does not exist, but a
// `url()` in a `@supports` prelude does, and this splitter is used for both.
[[nodiscard]] std::vector<std::string_view> split_on_commas(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t depth = 0;
    std::size_t start = 0;
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
        } else if (c == '(') {
            ++depth;
        } else if (c == ')' && depth != 0) {
            --depth;
        } else if (c == ',' && depth == 0) {
            out.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    out.push_back(text.substr(start));
    return out;
}

// `(name)` or `(name: value)`, with the parentheses already on it.
[[nodiscard]] std::string serialize_media_feature_text(std::string_view part) {
    const std::string_view inside = part.substr(1, part.size() - 2);
    const std::size_t colon = inside.find(':');
    if (colon == std::string_view::npos) {
        return "(" + ascii_lower_copy(collapse_whitespace(inside)) + ")";
    }
    // THE NAME IS FOLDED AND THE VALUE IS NOT. A feature name is an identifier
    // and `(Color)` and `(color)` are one feature; a value may be a string, a
    // `url()` or a number with a unit, none of which fold.
    return "(" + ascii_lower_copy(collapse_whitespace(inside.substr(0, colon))) + ": " +
           collapse_whitespace(inside.substr(colon + 1)) + ")";
}

// One query. An unparseable one is `not all`, which is what Media Queries says a
// query a browser does not understand means - never true, and never an error.
[[nodiscard]] std::string serialize_media_query_text(std::string_view text) {
    static constexpr std::string_view not_all = "not all";
    // The parts: `not`/`only`, a type, and parenthesised features joined by
    // `and`. A feature is ATOMIC - its parentheses may contain spaces - which is
    // why this is a scan and not a split on whitespace.
    std::vector<std::string> parts;
    std::size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && html_whitespace.find(text[at]) != std::string_view::npos) {
            ++at;
        }
        if (at >= text.size()) { break; }
        const std::size_t start = at;
        if (text[at] == '(') {
            std::size_t depth = 0;
            bool closed = false;
            for (; at < text.size(); ++at) {
                if (text[at] == '(') {
                    ++depth;
                } else if (text[at] == ')') {
                    --depth;
                    if (depth == 0) {
                        ++at;
                        closed = true;
                        break;
                    }
                }
            }
            if (!closed) { return std::string{not_all}; }
            parts.push_back(std::string{text.substr(start, at - start)});
            continue;
        }
        while (at < text.size() && text[at] != '(' &&
               html_whitespace.find(text[at]) == std::string_view::npos) {
            ++at;
        }
        parts.push_back(std::string{text.substr(start, at - start)});
    }
    if (parts.empty()) { return {}; }

    std::size_t i = 0;
    std::string prefix;
    if (parts[i].front() != '(' &&
        (ascii_iequals(parts[i], "not") || ascii_iequals(parts[i], "only"))) {
        prefix = ascii_lower_copy(parts[i]) + " ";
        ++i;
    }
    std::string type;
    if (i < parts.size() && parts[i].front() != '(') {
        // A MEDIA TYPE IS AN IDENTIFIER, so `@media 42` and `@media .x` are
        // queries this cannot serialise rather than types it has not heard of -
        // and the two have to be told apart, `speech` and `projection` being
        // perfectly good types that this engine does not model.
        const auto ident_char = [](unsigned char c, bool start) {
            if (c >= 0x80 || c == '_' || c == '-' || (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z')) {
                return true;
            }
            return !start && c >= '0' && c <= '9';
        };
        const std::string & word = parts[i];
        bool ident = !word.empty();
        for (std::size_t c = 0; c < word.size() && ident; ++c) {
            ident = ident_char(static_cast<unsigned char>(word[c]), c == 0);
        }
        if (!ident || ascii_iequals(word, "and")) { return std::string{not_all}; }
        type = ascii_lower_copy(word);
        ++i;
    }
    std::vector<std::string> features;
    bool first = true;
    while (i < parts.size()) {
        // Everything after the media type - and everything after the first
        // feature - is joined to what precedes it by `and`. `not (color)` has
        // neither, and is a query with one feature and no type.
        if (!first || !type.empty()) {
            if (!ascii_iequals(parts[i], "and")) { return std::string{not_all}; }
            ++i;
            if (i >= parts.size()) { return std::string{not_all}; }
        }
        if (parts[i].front() != '(') { return std::string{not_all}; }
        features.push_back(serialize_media_feature_text(parts[i]));
        ++i;
        first = false;
    }

    std::string out = prefix;
    // "If the query is `all and <features>`, omit the `all and`" - but only for
    // a query that is not negated: `not all and (color)` is a query that is
    // false whenever `(color)` is true, and dropping the type inverts it.
    const bool omit_all = type == "all" && prefix.empty() && !features.empty();
    if (!type.empty() && !omit_all) { out += type; }
    for (const std::string & feature : features) {
        if (!out.empty() && out.back() != ' ') { out += " and "; }
        out += feature;
    }
    return out;
}

[[nodiscard]] std::vector<std::string> parse_media_query_list(std::string_view text) {
    std::vector<std::string> out;
    if (trim(text, html_whitespace).empty()) { return out; }
    for (const std::string_view one : split_on_commas(text)) {
        std::string query = serialize_media_query_text(one);
        if (query.empty()) { query = "not all"; }
        out.push_back(std::move(query));
    }
    return out;
}

// "To serialize a comma-separated list, concatenate all items while separating
// them by a COMMA followed by a SPACE" - CSSOM §2.
[[nodiscard]] std::string serialize_media_query_list(std::span<const std::string> list) {
    std::string out;
    for (const std::string & query : list) {
        if (!out.empty()) { out += ", "; }
        out += query;
    }
    return out;
}

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
    // has in element.cpp, answered the same way.
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

} // namespace

// CSSOM §2.1. Shared with `CSS.escape` in bindings/css.cpp, which is the same
// algorithm asked for by a page rather than by a serialiser.
std::string dom_bindings::serialize_css_identifier(std::string_view text) {
    std::string out;
    const auto hex_escape = [&out](unsigned char c) {
        static constexpr char digits[] = "0123456789abcdef";
        out += '\\';
        if (c >= 16) { out += digits[c >> 4]; }
        out += digits[c & 0xF];
        out += ' ';
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        // NULL is not escaped, it is REPLACED - §2.1 step 3, the same U+FFFD
        // substitution the CSS tokenizer does to its input.
        if (c == 0) {
            out += "\xEF\xBF\xBD";
            continue;
        }
        if (c <= 0x1F || c == 0x7F) {
            hex_escape(c);
            continue;
        }
        // A LEADING DIGIT, or a digit after a leading `-`, would make the
        // identifier a number: both are escaped numerically rather than with a
        // backslash, because `\1` is not a valid identifier start either.
        if (c >= '0' && c <= '9' && (i == 0 || (i == 1 && text[0] == '-'))) {
            hex_escape(c);
            continue;
        }
        if (c == '-' && text.size() == 1) {
            out += "\\-";
            continue;
        }
        if (c >= 0x80 || c == '-' || c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z')) {
            out += static_cast<char>(c);
            continue;
        }
        out += '\\';
        out += static_cast<char>(c);
    }
    return out;
}

// --- the record store -------------------------------------------------------

std::string dom_bindings::rule_css_text(const css_rule_record & rule) const {
    // A PRELUDE AND A DECLARATION BLOCK. A keyframe's prelude is its keyText and
    // a style rule's is its selector; neither carries an at-keyword.
    if (rule.type == style_rule || rule.type == keyframe_rule) {
        const std::string block = serialize_block(rule.declarations);
        if (block.empty()) { return rule.selector + " { }"; }
        return rule.selector + " { " + block + " }";
    }
    // AN AT-RULE THIS ENGINE DOES NOT MODEL answers with the author's bytes: an
    // `@layer a, b;` reconstructed from a prelude nobody parsed would be a claim
    // about a rule nobody read. `verbatim` is cleared exactly when the block WAS
    // read, so this is the test for which of the two a record is.
    if (!rule.verbatim.empty()) { return rule.verbatim; }
    // THE TWO WITH NO BLOCK AT ALL. CSSOM 6.4.7 and 6.4.9, and both serialise
    // their URL as a `url()` whatever the author wrote: `@import "a.css"` comes
    // back `@import url("a.css");` from every engine.
    if (rule.type == import_rule) {
        std::string out = "@import " + serialize_url(rule.selector);
        if (!rule.prelude.empty()) { out += " " + rule.prelude; }
        const std::string media = serialize_media_query_list(rule.media_queries);
        if (!media.empty()) { out += " " + media; }
        return out + ";";
    }
    if (rule.type == namespace_rule) {
        std::string out = "@namespace ";
        if (!rule.selector.empty()) { out += rule.selector + " "; }
        return out + serialize_url(rule.prelude) + ";";
    }
    // AN AT-RULE WHOSE BLOCK IS DECLARATIONS - `@font-face`, `@page`,
    // `@counter-style`. The prelude is `@page`'s page selector and empty for
    // most of them, and CSSOM 6.4.5 puts a SPACE on each side of the block
    // whether or not there is anything in it: `@page { }`, not `@page {}`.
    if (at_rule_holds_declarations(rule.at_name)) {
        std::string out = "@" + rule.at_name;
        if (!rule.prelude.empty()) { out += " " + rule.prelude; }
        const std::string block = serialize_block(rule.declarations);
        return block.empty() ? out + " { }" : out + " { " + block + " }";
    }
    // A GROUPING RULE IS THE ONE MULTI-LINE SERIALISATION IN THE CSSOM, and it
    // is not a style choice - CSSOM 6.4.1 spells it out for CSSMediaRule: the
    // at-keyword, a SPACE, the media query list, a SPACE, `{`, a newline, then
    // each child rule indented by two spaces and followed by a newline, then
    // `}`. `css/cssom/serialize-media-rule.html` compares the result byte for
    // byte, including the DOUBLE space an empty query list leaves in
    // `@media  {` - one after the at-keyword and one before the brace.
    std::string out = "@" + rule.at_name;
    if (rule.type == media_rule) {
        out += " " + serialize_media_query_list(rule.media_queries);
    } else if (!rule.prelude.empty()) {
        out += " " + rule.prelude;
    }
    out += " {\n";
    for (const std::size_t child : rule.children) {
        if (child >= css_rule_store_.size()) { continue; }
        // TWO SPACES ON EVERY LINE of the child rather than on its first, so a
        // nested group indents cumulatively - which is what the recursion means
        // and what a single leading indent would get wrong.
        out += "  ";
        for (const char c : rule_css_text(*css_rule_store_[child])) {
            out += c;
            if (c == '\n') { out += "  "; }
        }
        out += '\n';
    }
    out += '}';
    return out;
}

// THE SOURCE, RULE BY RULE, through the same entry point `insertRule` uses.
//
// It used to be built from `style::css::parse_stylesheet`'s output, which meant
// the CSSOM could only report the three rule shapes the CASCADE needs: a
// qualified rule, `@media` and `@font-face`. Everything else the author wrote -
// `@import`, `@namespace`, `@page`, `@keyframes`, `@supports`,
// `@counter-style` - was not a rule with less in it, it was not there, so
// `cssRules[0]` was the wrong rule and every index after it was off by one.
// `css/cssom/cssom-ruleTypeAndOrder.html` asserts exactly that indexing over
// seven sheets. The front end also drops a qualified rule whose block is empty,
// which is what `@media all { * {} }` is made of and what the setup of every
// `css/cssom/CSSGroupingRule-*.html` fixture asserts before it tests anything.
//
// `split_top_level_rules` walks the bytes and `parse_one_rule` is asked about
// each span, so a rule from a `<style>` and the same rule from `insertRule`
// cannot be spelled - or typed, or counted - two different ways.
void dom_bindings::parse_sheet_rules(std::size_t sheet, std::string_view css) {
    if (sheet >= css_sheets_.size()) { return; }
    css_sheets_[sheet]->rules.clear();
    for (const std::string_view span : split_top_level_rules(css)) {
        std::string error;
        const std::size_t at = parse_one_rule(sheet, span, error);
        // A SPAN THAT IS NOT A RULE IS DROPPED AND THE SHEET CONTINUES, which is
        // §5.4's recovery and is the difference between a stylesheet with a
        // mistake in it and a broken one.
        if (at == no_index) { continue; }
        css_sheets_[sheet]->rules.push_back(at);
    }
}

std::size_t dom_bindings::parse_one_rule(std::size_t sheet, std::string_view text,
                                         std::string & error) {
    const std::string_view trimmed = trim(text, html_whitespace);
    if (trimmed.empty()) {
        error = "SyntaxError";
        return no_index;
    }
    css_rule_store_.push_back(std::make_unique<css_rule_record>());
    const std::size_t at = css_rule_store_.size() - 1;
    css_rule_record & made = *css_rule_store_[at];
    made.sheet = sheet;

    // THE DECLARATIONS OF A BLOCK, through the two entry points that exist for
    // exactly this - `parse_declaration_list`, which a `style` attribute uses,
    // and `check_declaration`, which `el.style` writes through. A value the
    // grammar refuses is one the cascade drops, so publishing it would advertise
    // a declaration that does not apply.
    const auto collect_into = [this](css_rule_record & into, std::string_view body) {
        const style::css::stylesheet parsed = style::css::parse_declaration_list(body, *atoms_);
        for (const style::css::raw_declaration & d : parsed.declarations) {
            const std::string property{atoms_->text(d.property)};
            const style::css::value_check checked =
                check_declaration(property, parsed.text_of(d), false);
            if (!checked.valid) { continue; }
            into.declarations.push_back(css_declaration{property, checked.serialized, d.important});
        }
    };

    // ONE KEYFRAME. `0%, to { opacity: 0 }` is a qualified rule whose prelude is
    // a `<keyframe-selector>#` and NOT a selector, so `parse_selector_text`
    // refuses it and treating a `@keyframes` block as a list of ordinary rules
    // would empty it. CSSOM 6.4.10's keyText is that list, comma-separated;
    // `from` and `to` keep the spelling the author used.
    const auto make_keyframe = [this, sheet, &collect_into](std::string_view span) -> std::size_t {
        const std::string_view one = trim(span, html_whitespace);
        const std::size_t brace = brace_at(one);
        const std::size_t shut = brace == std::string_view::npos ? brace : block_end(one, brace);
        if (brace == std::string_view::npos || shut == std::string_view::npos) { return no_index; }
        css_rule_store_.push_back(std::make_unique<css_rule_record>());
        css_rule_record & frame = *css_rule_store_.back();
        frame.sheet = sheet;
        frame.type = keyframe_rule;
        for (const std::string_view part : split_on_commas(one.substr(0, brace))) {
            if (!frame.selector.empty()) { frame.selector += ", "; }
            frame.selector += collapse_whitespace(part);
        }
        collect_into(frame, one.substr(brace + 1, shut - brace - 1));
        return css_rule_store_.size() - 1;
    };

    if (trimmed.front() == '@') {
        // AN AT-RULE. The name decides the type, which is what `instanceof
        // CSSMediaRule` asks; what happens to its BLOCK depends on which of the
        // three shapes the rule is - a list of rules, a list of declarations, or
        // neither, in which case the author's bytes are the honest answer to
        // `cssText` for a rule nothing here has modelled.
        const std::size_t name_end = trimmed.find_first_of(std::string{html_whitespace} + "{;", 1);
        const std::string_view name =
            trimmed.substr(1, (name_end == std::string_view::npos ? trimmed.size() : name_end) - 1);
        made.at_name = ascii_lower_copy(name);
        made.verbatim = std::string{trimmed};
        {
            // The prelude is what stands between the at-keyword and the block
            // (or the `;` that ends a statement at-rule): `@media` -> `all and
            // (min-width: 10px)`.
            const std::size_t after =
                name_end == std::string_view::npos ? trimmed.size() : name_end;
            std::size_t body = trimmed.find_first_of("{;", after);
            if (body == std::string_view::npos) { body = trimmed.size(); }
            made.prelude = std::string{trim(trimmed.substr(after, body - after), html_whitespace)};
        }
        if (ascii_iequals(name, "media")) {
            made.type = media_rule;
        } else if (ascii_iequals(name, "font-face")) {
            made.type = font_face_rule;
        } else if (ascii_iequals(name, "import")) {
            made.type = import_rule;
        } else if (ascii_iequals(name, "namespace")) {
            made.type = namespace_rule;
        } else if (ascii_iequals(name, "supports")) {
            made.type = supports_rule;
        } else if (ascii_iequals(name, "page")) {
            made.type = page_rule;
        } else if (ascii_iequals(name, "counter-style")) {
            made.type = counter_style_rule;
        } else if (ascii_iequals(name, "keyframes") || ascii_iequals(name, "-webkit-keyframes")) {
            made.type = keyframes_rule;
        } else {
            made.type = 0;
        }

        // The block, if there is one. `@import` and `@namespace` end at a `;`
        // and have none at all.
        const std::size_t open = brace_at(trimmed);
        const std::size_t close = open == std::string_view::npos ? open : block_end(trimmed, open);
        const std::string_view body =
            open == std::string_view::npos || close == std::string_view::npos
                ? std::string_view{}
                : trimmed.substr(open + 1, close - open - 1);

        if (made.type == media_rule) {
            // THE AUTHOR'S BYTES, which is the whole reason a media rule made
            // here serialises exactly and one recovered from a sheet does not.
            made.media_queries = parse_media_query_list(made.prelude);
            made.prelude.clear();
        } else if (made.type == import_rule) {
            // `<url> <layer>? <supports()>? <media-query-list>?`. The URL and
            // the media list get fields of their own because the interface has
            // a reader for each; `layer` and `supports()` stay in `prelude` in
            // the order the author wrote them, which is the only order CSS
            // Cascade 5 allows.
            const std::string written = made.prelude;
            std::size_t after = 0;
            const std::string href = url_value(next_component(written, after));
            std::string extra;
            for (std::size_t probe = after;;) {
                const std::string_view part = next_component(written, probe);
                if (part.empty()) { break; }
                const bool layer =
                    ascii_iequals(part, "layer") || ascii_iequals(part.substr(0, 6), "layer(");
                if (!layer && !ascii_iequals(part.substr(0, 9), "supports(")) { break; }
                if (!extra.empty()) { extra += ' '; }
                extra += collapse_whitespace(part);
                after = probe;
            }
            if (!href.empty()) {
                made.selector = href;
                made.media_queries = parse_media_query_list(written.substr(after));
                made.prelude = extra;
                made.verbatim.clear();
            }
        } else if (made.type == namespace_rule) {
            // `[<prefix>]? <url>`, and the prefix is optional - a default
            // namespace has none, which is not the same as having an empty one.
            const std::string written = made.prelude;
            std::size_t after = 0;
            const std::string_view first = next_component(written, after);
            const std::string_view second = next_component(written, after);
            const std::string uri = url_value(second.empty() ? first : second);
            if (!uri.empty()) {
                made.selector = second.empty() ? std::string{} : std::string{first};
                made.prelude = uri;
                made.verbatim.clear();
            }
        } else if (made.type == page_rule) {
            bool ok = false;
            made.prelude = serialize_page_selector(made.prelude, ok);
        }
        if (at_rule_holds_rules(made.at_name)) {
            // RECURSIVELY, THROUGH THIS SAME FUNCTION, rather than through the
            // sheet parser: the sheet parser drops a qualified rule with an
            // empty block, and `@media all { * {} }` is nothing but one. The
            // record store holds `unique_ptr`s, so the reference above stays
            // valid however far the recursion pushes the vector about.
            for (const std::string_view span : split_top_level_rules(body)) {
                std::string ignored;
                const std::size_t child = made.type == keyframes_rule
                                              ? make_keyframe(span)
                                              : parse_one_rule(sheet, span, ignored);
                if (child == no_index) { continue; }
                css_rule_store_[child]->parent = at;
                made.children.push_back(child);
            }
            // Reconstructible from here on, so the author's bytes are dropped
            // and `cssText` serialises the group and its children.
            made.verbatim.clear();
        } else if (at_rule_holds_declarations(made.at_name) && open != std::string_view::npos &&
                   close != std::string_view::npos) {
            collect_into(made, body);
            made.verbatim.clear();
        }
        return at;
    }

    // A QUALIFIED RULE, SPLIT AT THE BRACE rather than run through the sheet
    // parser, and the reason is the EMPTY BLOCK. `consume_qualified_rule` keeps
    // a rule only when it has both a selector and a declaration, so `div { }`
    // came back as no rule at all and `insertRule` answered SyntaxError for text
    // that is perfectly good CSS. That is not an edge case in this suite: it is
    // what `addRule()` with no arguments produces (`undefined { }`, asserted in
    // `css/cssom/CSSStyleSheet.html`) and it is what every `@media all { * {} }`
    // fixture in `CSSGroupingRule-*.html` is made of.
    //
    // The two halves go through the two entry points that exist for exactly
    // them - `parse_selector_text`, which `querySelector` uses, and
    // `parse_declaration_list`, which a `style` attribute uses - so nothing is
    // parsed here by a rule of its own.
    const std::size_t open = brace_at(trimmed);
    const std::size_t close = open == std::string_view::npos ? open : block_end(trimmed, open);
    if (open == std::string_view::npos || close == std::string_view::npos ||
        !trim(trimmed.substr(close + 1), html_whitespace).empty()) {
        // No block at all, an unterminated one, or a second rule after the
        // first - CSSOM asks for exactly one rule and all three are the same
        // answer.
        css_rule_store_.pop_back();
        error = "SyntaxError";
        return no_index;
    }
    bool bad = false;
    const std::string_view prelude = trim(trimmed.substr(0, open), html_whitespace);
    const style::css::stylesheet selectors = style::css::parse_selector_text(prelude, *atoms_, bad);
    if (bad || selectors.selectors.empty()) {
        css_rule_store_.pop_back();
        error = "SyntaxError";
        return no_index;
    }
    made.type = style_rule;
    // The canonical serialisation when the compiled form IS the selector, and
    // the author's bytes when it is not - see `representable`. Whitespace is
    // collapsed either way, so `span  div  ` is `span div` in both.
    made.selector = representable(selectors.selectors)
                        ? serialize_selector_list(selectors.selectors, *atoms_)
                        : collapse_whitespace(prelude);
    collect_into(made, trimmed.substr(open + 1, close - open - 1));
    return at;
}

std::string dom_bindings::author_style_text() {
    // THE DOCUMENT'S OWN SHEETS ARE COLLECTED LAZILY, on a read of
    // `document.styleSheets` - so a page that adopted a constructed sheet
    // without ever touching `document.styleSheets` had `css_document_sheets_`
    // empty here, and its `<style>` element vanished from the serialisation
    // while the adopted sheets survived. Syncing first is what makes this the
    // FINAL list of style sheets CSSOM defines rather than whatever the page
    // happened to have asked for.
    if (cx_ != nullptr) { sync_style_sheets(*cx_); }
    std::string out;
    const auto emit = [&](std::size_t at) {
        if (at >= css_sheets_.size()) { return; }
        const std::unique_ptr<css_sheet_record> & sheet = css_sheets_[at];
        if (sheet->disabled) { return; }
        for (const std::size_t rule : sheet->rules) {
            if (rule >= css_rule_store_.size()) { continue; }
            out += rule_css_text(*css_rule_store_[rule]);
            out += '\n';
        }
    };
    for (const std::size_t at : css_document_sheets_) { emit(at); }
    // AND THE ADOPTED SHEETS, AFTER THEM AND IN THEIR OWN ORDER.
    //
    // They were in the object model and in nothing else: `adoptedStyleSheets`
    // held an array, `document.styleSheets` correctly did not include it (a
    // constructed sheet is not a document sheet), and so a sheet a page adopted
    // reached the cascade through no route at all. CSSOM puts them LAST in the
    // final list of style sheets, which is what makes
    // `adoptedStyleSheets = [red, green]` green and `[green, red]` red -
    // `adoptedstylesheets-cascade-order.html` asserts exactly that pair, and
    // then asserts it again for a rotation, which only an ordered walk of the
    // array can answer.
    //
    // Read from the ARRAY rather than from a mirror kept beside it, because the
    // page owns that array and a mirror updated only on assignment would be a
    // second answer to what has been adopted.
    if (script::object_object * internals = as_object(cssom_internals_)) {
        if (const value * held = internals->find("adopted"); held != nullptr && held->is_array()) {
            for (const value each : static_cast<script::array_object *>(held->as_heap())->items) {
                emit(slot_index(as_object(each), sheet_key));
            }
        }
    }
    return out;
}

void dom_bindings::style_sheets_changed() {
    if (on_author_styles_) { on_author_styles_(author_style_text()); }
    // MARKED DIRTY EITHER WAY. With no hook the cascade does not observe the
    // change and the frame is identical, which costs one recomposite on an
    // operation a page performs a handful of times; with a hook the browser has
    // already replaced the author sheet and this is what schedules the restyle.
    mutated();
}

// --- the document's sheets, re-derived --------------------------------------

script::object_object * dom_bindings::cssom_internals(context & cx) {
    if (script::object_object * held = as_object(cssom_internals_)) { return held; }
    script::object_object * doc = document_object();
    if (doc == nullptr) { return nullptr; }
    if (const value * found = doc->find(internals_key)) {
        cssom_internals_ = *found;
        return as_object(cssom_internals_);
    }
    cssom_internals_ = cx.make_object();
    // ON THE DOCUMENT, non-configurable, and that is what roots it. Everything
    // the CSSOM holds hangs off this object, `register_roots` already marks
    // `document_`, and the collector traces an object's properties - so this
    // needs no line in a file this rung does not own. A page cannot delete it
    // either, the property being non-configurable.
    doc->define(internals_key, cssom_internals_, script::attr_none);
    return as_object(cssom_internals_);
}

value dom_bindings::style_sheet_list(context & cx) {
    script::object_object * internals = cssom_internals(cx);
    if (internals == nullptr) { return value::undefined(); }
    if (const value * found = internals->find("list")) { return *found; }
    const value list = cx.make_object();
    if (script::object_object * obj = as_object(list)) {
        if (const value * proto = internals->find("StyleSheetList.prototype")) {
            obj->prototype = *proto;
        }
        obj->define("length", value::number(0), script::attr_none);
    }
    internals->set("list", list);
    return list;
}

void dom_bindings::sync_style_sheets(context & cx) {
    script::object_object * internals = cssom_internals(cx);
    if (internals == nullptr) { return; }
    const value list = style_sheet_list(cx);
    script::object_object * list_obj = as_object(list);
    if (list_obj == nullptr) { return; }

    // WHAT THE DOM SAYS NOW - the same walk browser::load_author_styles makes.
    struct found_sheet {
        node_id owner;
        bool linked = false;
        std::string href;
        std::string title;
        std::string media;
        std::string text;
    };
    std::vector<found_sheet> found;
    {
        const auto txn = doc_->read();
        const atom style_tag = atoms_->intern_lower("style");
        const atom link_tag = atoms_->intern_lower("link");
        const atom rel_attribute = atoms_->intern_lower("rel");
        const atom href_attribute = atoms_->intern_lower("href");
        const atom title_attribute = atoms_->intern_lower("title");
        const atom media_attribute = atoms_->intern_lower("media");
        const auto walk = [&](auto && self, node_id at) -> void {
            // HTML ONLY, for both, and for the reason load_author_styles gives:
            // an SVG carries its own <style>, it interns to the same atom, and
            // it is not a document stylesheet.
            if (txn.element_ns(at) == ctbrowser::node_ns::html) {
                const atom tag = txn.tag(at).value_or(atom{});
                const bool is_style = tag == style_tag;
                const bool is_link =
                    tag == link_tag && rel_is_stylesheet(txn.attribute_value(at, rel_attribute));
                if (is_style || is_link) {
                    found_sheet made;
                    made.owner = at;
                    made.linked = is_link;
                    made.href = std::string{txn.attribute_value(at, href_attribute)};
                    made.title = std::string{txn.attribute_value(at, title_attribute)};
                    made.media = std::string{txn.attribute_value(at, media_attribute)};
                    if (is_style) {
                        for (const node_id child : txn.children(at)) {
                            made.text += txn.text(child);
                        }
                    }
                    found.push_back(std::move(made));
                }
            }
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        walk(walk, txn.root());
    }

    // The sheet objects the list already holds, so a record that survives keeps
    // its JavaScript identity - and with it whatever the page put on it.
    std::vector<std::pair<std::size_t, value>> existing;
    {
        std::size_t was = 0;
        if (const value * held = list_obj->find("length"); held != nullptr && held->is_number()) {
            const double count = held->as_number();
            if (count > 0) { was = static_cast<std::size_t>(count); }
        }
        for (std::size_t i = 0; i < was; ++i) {
            const value * held = list_obj->find(std::to_string(i));
            if (held == nullptr) { continue; }
            existing.emplace_back(slot_index(as_object(*held), sheet_key), *held);
        }
    }

    flat_map<std::uint64_t, std::size_t> by_owner;
    std::vector<value> ordered;
    css_document_sheets_.clear();
    for (const found_sheet & each : found) {
        const std::uint64_t key = pack(each.owner);
        const auto it = css_sheet_by_owner_.find(key);
        bool fresh = false;
        std::size_t at = no_index;
        if (it == css_sheet_by_owner_.end() || it->second >= css_sheets_.size()) {
            css_sheets_.push_back(std::make_unique<css_sheet_record>());
            at = css_sheets_.size() - 1;
            css_sheets_[at]->owner = each.owner;
            fresh = true;
        } else {
            at = it->second;
        }
        css_sheet_record & record = *css_sheets_[at];
        record.title = each.title;
        // THE ATTRIBUTE IS RE-READ, THE LIST IS NOT. This walk runs on every
        // read of `document.styleSheets`, and a MediaList is mutable - a page
        // that has called `appendMedium` must not have it undone by the next
        // property access. So the query list is re-derived only when the
        // element's `media` attribute has actually changed under it.
        if (fresh || record.media != each.media) {
            record.media = each.media;
            record.media_queries = parse_media_query_list(record.media);
        }
        if (each.linked) {
            // A `<link>`'s bytes come from the asset registry, exactly as
            // load_author_styles resolves them, and only when the href changes -
            // this walk runs on every read of `document.styleSheets` and
            // re-reading a file each time would be a load per property access.
            if (fresh || record.href != each.href) {
                record.href = each.href;
                std::string text;
                if (assets_ != nullptr) {
                    const std::vector<std::byte> bytes = assets_->load(record.href);
                    text.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
                }
                record.source = std::move(text);
                parse_sheet_rules(at, record.source);
            }
        } else if (fresh || record.source != each.text) {
            // EDITING A `<style>` REPLACES ITS SHEET, which is what the source
            // comparison is for. An insertRule does NOT change the element's
            // text, so the CSSOM's own mutations survive this.
            record.source = each.text;
            parse_sheet_rules(at, record.source);
        }
        by_owner.emplace(key, at);
        css_document_sheets_.push_back(at);
        value object = value::undefined();
        for (const auto & [index, held] : existing) {
            if (index == at) {
                object = held;
                break;
            }
        }
        if (object.is_undefined()) { object = make_sheet_object(cx, at); }
        ordered.push_back(object);
    }
    css_sheet_by_owner_ = std::move(by_owner);
    set_indexed(*list_obj, ordered);
}

value dom_bindings::sheet_object_of(context & cx, node_id owner) {
    sync_style_sheets(cx);
    const auto it = css_sheet_by_owner_.find(pack(owner));
    if (it == css_sheet_by_owner_.end()) { return value::null(); }
    script::object_object * list_obj = as_object(style_sheet_list(cx));
    if (list_obj == nullptr) { return value::null(); }
    std::size_t count = 0;
    if (const value * held = list_obj->find("length"); held != nullptr && held->is_number()) {
        const double n = held->as_number();
        if (n > 0) { count = static_cast<std::size_t>(n); }
    }
    for (std::size_t i = 0; i < count; ++i) {
        const value * held = list_obj->find(std::to_string(i));
        if (held == nullptr) { continue; }
        if (slot_index(as_object(*held), sheet_key) == it->second) { return *held; }
    }
    return value::null();
}

// --- the objects ------------------------------------------------------------

dom_bindings::css_sheet_record * dom_bindings::receiver_sheet(context & cx) {
    const std::size_t at = slot_index(as_object(cx.current_this()), sheet_key);
    return at < css_sheets_.size() ? css_sheets_[at].get() : nullptr;
}

dom_bindings::css_rule_record * dom_bindings::receiver_rule(context & cx) {
    const std::size_t at = slot_index(as_object(cx.current_this()), rule_key);
    return at < css_rule_store_.size() ? css_rule_store_[at].get() : nullptr;
}

std::vector<std::string> * dom_bindings::receiver_media(context & cx) {
    // A RULE FIRST, because a media rule's own object carries both private slots
    // - `rule_key` and the `sheet_key` that says which sheet it came from - and
    // the sheet's list is not the rule's. A MediaList itself carries exactly one.
    if (css_rule_record * rule = receiver_rule(cx)) { return &rule->media_queries; }
    if (css_sheet_record * sheet = receiver_sheet(cx)) { return &sheet->media_queries; }
    return nullptr;
}

value dom_bindings::media_list_object(context & cx, script::object_object & owner) {
    // [SameObject]: `sheet.media === sheet.media`, and a page's expando on one
    // survives - so the list is cached on its owner under a private key and
    // REFRESHED rather than rebuilt.
    if (const value * held = owner.find(media_key)) {
        refresh_media_list(cx, *held);
        return *held;
    }
    const value list = cx.make_object();
    script::object_object * obj = as_object(list);
    if (obj == nullptr) { return list; }
    if (script::object_object * internals = cssom_internals(cx)) {
        if (const value * proto = internals->find("MediaList.prototype")) {
            obj->prototype = *proto;
        }
    }
    // THE OWNER'S SLOT, COPIED ONTO THE LIST. It is how every method below finds
    // the vector it is a view of: a MediaList has no owner pointer of its own,
    // and re-deriving one from the JS object graph would need a back-reference
    // the collector would then have to know about.
    if (const value * rule = owner.find(rule_key)) {
        obj->define(rule_key, *rule, script::attr_none);
    } else if (const value * sheet = owner.find(sheet_key)) {
        obj->define(sheet_key, *sheet, script::attr_none);
    }
    obj->define("length", value::number(0), script::attr_none);
    owner.define(media_key, list, script::attr_none);
    refresh_media_list(cx, list);
    return list;
}

void dom_bindings::refresh_media_list(context & cx, value list) {
    script::object_object * obj = as_object(list);
    if (obj == nullptr) { return; }
    const std::vector<std::string> * queries = nullptr;
    {
        const std::size_t rule = slot_index(obj, rule_key);
        const std::size_t sheet = slot_index(obj, sheet_key);
        if (rule < css_rule_store_.size()) {
            queries = &css_rule_store_[rule]->media_queries;
        } else if (sheet < css_sheets_.size()) {
            queries = &css_sheets_[sheet]->media_queries;
        }
    }
    if (queries == nullptr) { return; }
    std::vector<value> items;
    items.reserve(queries->size());
    for (const std::string & query : *queries) { items.push_back(cx.string(query)); }
    set_indexed(*obj, items);
}

value dom_bindings::make_rule_list(context & cx, std::span<const std::size_t> rules) {
    const value list = cx.make_object();
    script::object_object * obj = as_object(list);
    if (obj == nullptr) { return list; }
    if (script::object_object * internals = cssom_internals(cx)) {
        if (const value * proto = internals->find("CSSRuleList.prototype")) {
            obj->prototype = *proto;
        }
    }
    obj->define("length", value::number(0), script::attr_none);
    refresh_rule_list(cx, list, rules);
    return list;
}

void dom_bindings::refresh_rule_list(context & cx, value list, std::span<const std::size_t> rules) {
    script::object_object * obj = as_object(list);
    if (obj == nullptr) { return; }
    // The rule OBJECTS already in the list, by record index. insertRule must not
    // rebuild the ones it did not touch: `css/cssom/CSSStyleSheet.html` puts an
    // expando on two rules, inserts a third between them, and asserts both
    // expandos survived.
    std::vector<std::pair<std::size_t, value>> existing;
    std::size_t was = 0;
    if (const value * held = obj->find("length"); held != nullptr && held->is_number()) {
        const double count = held->as_number();
        if (count > 0) { was = static_cast<std::size_t>(count); }
    }
    for (std::size_t i = 0; i < was; ++i) {
        const value * held = obj->find(std::to_string(i));
        if (held == nullptr) { continue; }
        existing.emplace_back(slot_index(as_object(*held), rule_key), *held);
    }
    std::vector<value> ordered;
    for (const std::size_t rule : rules) {
        value object = value::undefined();
        for (const auto & [index, held] : existing) {
            if (index == rule) {
                object = held;
                break;
            }
        }
        if (object.is_undefined()) { object = make_rule_object(cx, rule); }
        ordered.push_back(object);
    }
    set_indexed(*obj, ordered);
}

value dom_bindings::make_rule_object(context & cx, std::size_t rule) {
    const value object = cx.make_object();
    script::object_object * obj = as_object(object);
    if (obj == nullptr || rule >= css_rule_store_.size()) { return object; }
    script::object_object * internals = cssom_internals(cx);
    if (internals != nullptr) {
        const css_rule_record & record = *css_rule_store_[rule];
        std::string_view interface = "CSSRule.prototype";
        if (record.type == style_rule) {
            interface = "CSSStyleRule.prototype";
        } else if (record.type == media_rule) {
            interface = "CSSMediaRule.prototype";
        } else if (record.type == font_face_rule) {
            interface = "CSSFontFaceRule.prototype";
        } else if (record.type == import_rule) {
            interface = "CSSImportRule.prototype";
        } else if (record.type == supports_rule) {
            interface = "CSSSupportsRule.prototype";
        } else if (record.type == page_rule) {
            interface = "CSSPageRule.prototype";
        } else if (record.type == keyframes_rule) {
            interface = "CSSKeyframesRule.prototype";
        } else if (record.type == keyframe_rule) {
            interface = "CSSKeyframeRule.prototype";
        } else if (record.type == namespace_rule) {
            interface = "CSSNamespaceRule.prototype";
        } else if (record.type == counter_style_rule) {
            interface = "CSSCounterStyleRule.prototype";
        }
        if (const value * proto = internals->find(interface)) { obj->prototype = *proto; }
    }
    obj->define(rule_key, value::number(static_cast<double>(rule)), script::attr_none);
    obj->define(sheet_key, value::number(static_cast<double>(css_rule_store_[rule]->sheet)),
                script::attr_none);
    return object;
}

value dom_bindings::declaration_object(context & cx, std::size_t rule) {
    const value object = cx.make_object();
    script::object_object * obj = as_object(object);
    if (obj == nullptr) { return object; }
    if (script::object_object * internals = cssom_internals(cx)) {
        if (const value * proto = internals->find("CSSStyleDeclaration.prototype")) {
            obj->prototype = *proto;
        }
    }
    obj->define(rule_key, value::number(static_cast<double>(rule)), script::attr_none);
    refresh_declaration_object(cx, object);
    return object;
}

void dom_bindings::refresh_declaration_object(context & cx, value declarations) {
    (void)cx;
    script::object_object * obj = as_object(declarations);
    if (obj == nullptr) { return; }
    const std::size_t at = slot_index(obj, rule_key);
    if (at >= css_rule_store_.size()) { return; }
    // AN INDEX NAMES A PROPERTY, not a value - CSSOM 6.7.1 - which is what
    // `[...style]` and every `for (const p of style)` iterates. These are data
    // properties refreshed on every write rather than accessors, because there
    // is no bound on how many a block may have and a prototype cannot carry an
    // accessor per index.
    std::vector<value> names;
    for (const css_declaration & declared : css_rule_store_[at]->declarations) {
        names.push_back(cx.string(declared.name));
    }
    set_indexed(*obj, names);
}

value dom_bindings::make_sheet_object(context & cx, std::size_t sheet) {
    const value object = cx.make_object();
    script::object_object * obj = as_object(object);
    if (obj == nullptr) { return object; }
    if (script::object_object * internals = cssom_internals(cx)) {
        if (const value * proto = internals->find("CSSStyleSheet.prototype")) {
            obj->prototype = *proto;
        }
    }
    obj->define(sheet_key, value::number(static_cast<double>(sheet)), script::attr_none);
    return object;
}

void dom_bindings::install_sheet_property(context & cx, script::object_object & obj, node_id id) {
    // `sheet`, the LinkStyle interface. An ACCESSOR rather than a property: a
    // `<style>` a script has just created and appended has no sheet until the
    // document is walked again, and `dom/events`' four animation tests do
    // exactly that - `document.head.appendChild(style); style.sheet.insertRule(...)`.
    obj.define_accessor(
        "sheet",
        value::object(cx.allocate<script::native_object>(
            "get sheet",
            [this, id](context & c, std::span<value>) { return sheet_object_of(c, id); })),
        value::undefined(), script::attr_configurable);
}

// --- the interfaces ---------------------------------------------------------

void dom_bindings::install_stylesheet_prototypes(context & cx) {
    script::object_object * internals = cssom_internals(cx);
    if (internals == nullptr) { return; }

    // One interface object plus its prototype, wired the way
    // `bindings/exceptions.cpp` wires DOMException: the constructor carries a
    // non-writable `prototype`, the prototype carries `constructor`, and the
    // prototype is reachable from the internals object so a page deleting the
    // global cannot collect it.
    const auto interface = [&](const char * name, const char * inherits,
                               script::native_fn construct) -> script::object_object * {
        auto * proto = static_cast<script::object_object *>(cx.make_object().as_heap());
        if (inherits != nullptr) {
            if (const value * parent = internals->find(std::string{inherits} + ".prototype")) {
                proto->prototype = *parent;
            }
        }
        auto * ctor = cx.allocate<script::native_object>(
            name, construct
                      ? std::move(construct)
                      : script::native_fn{[name](context & c, std::span<value>) {
                            c.throw_error("TypeError", std::string{"Illegal constructor: "} + name);
                            return value::undefined();
                        }});
        ctor->define("prototype", value::object(proto), script::attr_none);
        proto->define("constructor", value::object(ctor), script::attr_builtin);
        internals->set(std::string{name} + ".prototype", value::object(proto));
        internals->set(std::string{name}, value::object(ctor));
        cx.define_global(name, value::object(ctor));
        return proto;
    };
    const auto method = [&](script::object_object * on, const char * name, script::native_fn fn) {
        on->define(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))),
                   script::attr_builtin);
    };
    const auto getter = [&](script::object_object * on, const char * name, script::native_fn read) {
        on->define_accessor(name,
                            value::object(cx.allocate<script::native_object>(
                                std::string{"get "} + name, std::move(read))),
                            value::undefined(), script::attr_configurable);
    };
    const auto accessor = [&](script::object_object * on, const char * name, script::native_fn read,
                              script::native_fn write) {
        on->define_accessor(name,
                            value::object(cx.allocate<script::native_object>(
                                std::string{"get "} + name, std::move(read))),
                            value::object(cx.allocate<script::native_object>(
                                std::string{"set "} + name, std::move(write))),
                            script::attr_configurable);
    };

    // --- MediaList
    //
    // A VIEW OF A RECORD'S QUERY LIST, not a list of its own. `mediaText`,
    // `appendMedium` and `deleteMedium` all write through to the sheet or the
    // media rule the object came from, which is what makes
    // `rule.media.appendMedium('print')` change `rule.cssText` - the two are one
    // list read two ways rather than two lists that have to be kept in step.
    script::object_object * media_proto = interface("MediaList", nullptr, nullptr);
    method(media_proto, "item",
           [](context & c, std::span<value> a) { return collection_item(c, a); });
    accessor(
        media_proto, "mediaText",
        [this](context & c, std::span<value>) {
            const std::vector<std::string> * queries = receiver_media(c);
            return c.string(queries == nullptr ? std::string{}
                                               : serialize_media_query_list(*queries));
        },
        [this](context & c, std::span<value> a) {
            std::vector<std::string> * queries = receiver_media(c);
            if (queries == nullptr) { return value::undefined(); }
            // [LegacyNullToEmptyString]: `media.mediaText = null` EMPTIES the
            // list rather than parsing the string "null", which
            // `css/cssom/MediaList.html` asserts by name.
            const std::string text = a.empty() || a[0].is_null() || a[0].is_undefined()
                                         ? std::string{}
                                         : c.to_string(a[0]);
            *queries = parse_media_query_list(text);
            refresh_media_list(c, c.current_this());
            style_sheets_changed();
            return value::undefined();
        });
    // The stringifier. `media.toString()` and `'' + media` are both `mediaText`.
    method(media_proto, "toString", [](context & c, std::span<value>) {
        return c.lookup_property(c.current_this(), "mediaText");
    });
    method(media_proto, "appendMedium", [this](context & c, std::span<value> a) {
        std::vector<std::string> * queries = receiver_media(c);
        if (queries == nullptr) { return value::undefined(); }
        // "Parse A MEDIA QUERY" - singular. `appendMedium("screen, print")` is
        // not two appends and it is not one query called `screen, print`
        // either: the parse returns null, and step 1 says return. A top-level
        // comma is the whole test for it.
        const std::string one = arg_string(c, a, 0);
        if (split_on_commas(one).size() != 1) { return value::undefined(); }
        const std::string added = serialize_media_query_text(one);
        if (added.empty()) { return value::undefined(); }
        // "If comparing medium with any of the media queries in the collection
        // returns true, then return" - appending a medium twice is a no-op.
        if (std::find(queries->begin(), queries->end(), added) != queries->end()) {
            return value::undefined();
        }
        queries->push_back(added);
        refresh_media_list(c, c.current_this());
        style_sheets_changed();
        return value::undefined();
    });
    method(media_proto, "deleteMedium", [this](context & c, std::span<value> a) {
        std::vector<std::string> * queries = receiver_media(c);
        if (queries == nullptr) { return value::undefined(); }
        // A REQUIRED ARGUMENT, so calling it with none is a TypeError and not a
        // NotFoundError about the empty string - `medialist-interfaces-002.html`
        // asserts which of the two by name.
        if (a.empty()) {
            c.throw_error("TypeError", "deleteMedium requires a medium");
            return value::undefined();
        }
        const std::string wanted = serialize_media_query_text(c.to_string(a[0]));
        // "Remove ALL media queries in the collection that match" - a list may
        // hold the same query twice (`screen, print, screen`) and removing only
        // the first leaves one behind that the page has just asked to be rid of.
        const auto gone = std::remove(queries->begin(), queries->end(), wanted);
        if (wanted.empty() || gone == queries->end()) {
            // "If nothing was removed, then throw a NotFoundError" - the one
            // place in the CSSOM where deleting something absent is an error.
            throw_dom_exception(c, "NotFoundError", "that medium is not in the list");
            return value::undefined();
        }
        queries->erase(gone, queries->end());
        refresh_media_list(c, c.current_this());
        style_sheets_changed();
        return value::undefined();
    });

    // --- StyleSheetList
    script::object_object * list_proto = interface("StyleSheetList", nullptr, nullptr);
    method(list_proto, "item",
           [](context & c, std::span<value> a) { return collection_item(c, a); });

    // --- CSSRuleList
    script::object_object * rules_proto = interface("CSSRuleList", nullptr, nullptr);
    method(rules_proto, "item",
           [](context & c, std::span<value> a) { return collection_item(c, a); });

    // --- StyleSheet / CSSStyleSheet
    script::object_object * base_proto = interface("StyleSheet", nullptr, nullptr);
    getter(base_proto, "type", [](context & c, std::span<value>) { return c.string("text/css"); });
    getter(base_proto, "href", [this](context & c, std::span<value>) {
        const css_sheet_record * sheet = receiver_sheet(c);
        if (sheet == nullptr || sheet->href.empty()) { return value::null(); }
        return c.string(sheet->href);
    });
    getter(base_proto, "ownerNode", [this](context & c, std::span<value>) {
        const css_sheet_record * sheet = receiver_sheet(c);
        if (sheet == nullptr || !sheet->owner) { return value::null(); }
        return wrap(c, sheet->owner);
    });
    getter(base_proto, "ownerRule", [](context &, std::span<value>) { return value::null(); });
    getter(base_proto, "parentStyleSheet",
           [](context &, std::span<value>) { return value::null(); });
    getter(base_proto, "title", [this](context & c, std::span<value>) {
        const css_sheet_record * sheet = receiver_sheet(c);
        // "The title attribute must return the title or null if the title is
        // the empty string" - and a CONSTRUCTED sheet has no title at all,
        // whatever was passed to the constructor.
        if (sheet == nullptr || sheet->constructed || sheet->title.empty()) {
            return value::null();
        }
        return c.string(sheet->title);
    });
    accessor(
        base_proto, "media",
        [this](context & c, std::span<value>) {
            script::object_object * self = as_object(c.current_this());
            if (self == nullptr || receiver_sheet(c) == nullptr) { return value::undefined(); }
            return media_list_object(c, *self);
        },
        [](context & c, std::span<value> a) {
            // [PutForwards=mediaText]: `sheet.media = 'print'` assigns to the
            // MediaList's mediaText and the list object itself never changes.
            const value list = c.lookup_property(c.current_this(), "media");
            c.store_property(list, "mediaText", a.empty() ? c.string("") : a[0]);
            return value::undefined();
        });
    accessor(
        base_proto, "disabled",
        [this](context & c, std::span<value>) {
            const css_sheet_record * sheet = receiver_sheet(c);
            return value::boolean(sheet != nullptr && sheet->disabled);
        },
        [this](context & c, std::span<value> a) {
            if (css_sheet_record * sheet = receiver_sheet(c)) {
                const bool wanted = !a.empty() && context::truthy(a[0]);
                if (sheet->disabled != wanted) {
                    sheet->disabled = wanted;
                    style_sheets_changed();
                }
            }
            return value::undefined();
        });

    script::object_object * sheet_proto =
        interface("CSSStyleSheet", "StyleSheet", [this](context & c, std::span<value> args) {
            // `new CSSStyleSheet(options)`. `media` and `disabled` are honoured;
            // `title` is NOT, which is not an omission - the specification says
            // a constructed sheet has no title however it was constructed, and
            // `CSSStyleSheet-constructable.html` asserts exactly that.
            css_sheets_.push_back(std::make_unique<css_sheet_record>());
            const std::size_t at = css_sheets_.size() - 1;
            css_sheets_[at]->constructed = true;
            if (!args.empty() && args[0].is_object()) {
                auto * options = static_cast<script::object_object *>(args[0].as_heap());
                if (const value * media = options->find("media")) {
                    css_sheets_[at]->media = c.to_string(*media);
                    css_sheets_[at]->media_queries = parse_media_query_list(css_sheets_[at]->media);
                }
                if (const value * disabled = options->find("disabled")) {
                    css_sheets_[at]->disabled = context::truthy(*disabled);
                }
            }
            return make_sheet_object(c, at);
        });
    getter(sheet_proto, "cssRules", [this](context & c, std::span<value>) {
        // [SameObject]: `sheet.cssRules === sheet.cssRules` and
        // `sheet.cssRules === sheet.rules` are both asserted, so the list is
        // built once and REFRESHED rather than rebuilt.
        script::object_object * self = as_object(c.current_this());
        const css_sheet_record * sheet = receiver_sheet(c);
        if (self == nullptr || sheet == nullptr) { return value::undefined(); }
        if (const value * held = self->find(rules_key)) {
            refresh_rule_list(c, *held, sheet->rules);
            return *held;
        }
        const value list = make_rule_list(c, sheet->rules);
        self->define(rules_key, list, script::attr_none);
        return list;
    });
    // `rules` is the legacy alias and must be the SAME object.
    sheet_proto->define_accessor("rules",
                                 value::object(cx.allocate<script::native_object>(
                                     "get rules",
                                     [](context & c, std::span<value>) {
                                         const value self = c.current_this();
                                         return c.lookup_property(self, "cssRules");
                                     })),
                                 value::undefined(), script::attr_configurable);
    method(sheet_proto, "insertRule", [this](context & c, std::span<value> args) {
        css_sheet_record * sheet = receiver_sheet(c);
        if (sheet == nullptr) { return value::undefined(); }
        if (args.empty()) {
            c.throw_error("TypeError", "insertRule requires a rule");
            return value::undefined();
        }
        const std::string text = c.to_string(args[0]);
        // `insertRule(text)` and `insertRule(text, undefined)` ARE THE SAME
        // CALL. `index` is an optional unsigned long defaulting to 0, and Web
        // IDL says an `undefined` passed for an optional argument means the
        // default was not supplied - it is not `ToNumber(undefined)`, which is
        // NaN and was answering IndexSizeError for a perfectly ordinary
        // insertion. `insertRule-no-index.html` and its four siblings are one
        // subtest each of exactly that.
        const double asked =
            args.size() > 1 && !args[1].is_undefined() ? context::to_number(args[1]) : 0;
        if (!(asked >= 0) || asked > static_cast<double>(sheet->rules.size())) {
            throw_dom_exception(c, "IndexSizeError", "the index is past the end of the sheet");
            return value::undefined();
        }
        const auto at = static_cast<std::size_t>(asked);
        std::string error;
        const std::size_t made = parse_one_rule(
            static_cast<std::size_t>(slot_index(as_object(c.current_this()), sheet_key)), text,
            error);
        if (made == no_index) {
            throw_dom_exception(c, error.empty() ? std::string{"SyntaxError"} : error,
                                "the text is not a single CSS rule");
            return value::undefined();
        }
        const std::uint32_t kind = css_rule_store_[made]->type;
        if (kind == import_rule && sheet->constructed) {
            // "@import rules are not allowed in a constructed stylesheet."
            throw_dom_exception(c, "SyntaxError", "@import is not allowed here");
            return value::undefined();
        }
        // WHAT MAY PRECEDE WHAT - CSSOM 6.3.3 steps 4 and 5, and the two steps
        // answer different questions. Step 4 is about the POSITION: `@import`
        // may only go where everything before it is `@charset`, `@layer` or
        // another `@import`, `@namespace` may additionally follow those, and
        // everything else may only go after all of them. Step 5 is about the
        // WHOLE LIST: a `@namespace` may not be added to a sheet that already
        // has a style rule in it at all, wherever the insertion point is,
        // because the namespace would change what the existing selectors mean.
        // `at-namespace.html` is one assertion of exactly that and says so in
        // its title.
        const auto rule_type = [this, sheet](std::size_t i) {
            const std::size_t which = sheet->rules[i];
            return which < css_rule_store_.size() ? css_rule_store_[which]->type : 0;
        };
        const auto before_ok = [&](std::uint32_t allowed_a, std::uint32_t allowed_b) {
            for (std::size_t i = 0; i < at; ++i) {
                const std::uint32_t each = rule_type(i);
                if (each != allowed_a && each != allowed_b) { return false; }
            }
            return true;
        };
        bool allowed = true;
        if (kind == import_rule) {
            allowed = before_ok(import_rule, import_rule);
        } else if (kind == namespace_rule) {
            allowed = before_ok(import_rule, namespace_rule);
        } else {
            // Nothing else may be inserted BEFORE an `@import` or a
            // `@namespace`, so every rule from the insertion point on must be
            // neither.
            for (std::size_t i = at; i < sheet->rules.size(); ++i) {
                const std::uint32_t each = rule_type(i);
                if (each == import_rule || each == namespace_rule) { allowed = false; }
            }
        }
        if (!allowed) {
            throw_dom_exception(c, "HierarchyRequestError", "that rule may not go there");
            return value::undefined();
        }
        if (kind == namespace_rule) {
            for (std::size_t i = 0; i < sheet->rules.size(); ++i) {
                const std::uint32_t each = rule_type(i);
                if (each != import_rule && each != namespace_rule) {
                    throw_dom_exception(c, "InvalidStateError",
                                        "the sheet already has rules a namespace would change");
                    return value::undefined();
                }
            }
        }
        sheet->rules.insert(sheet->rules.begin() + static_cast<std::ptrdiff_t>(at), made);
        style_sheets_changed();
        return value::number(asked);
    });
    method(sheet_proto, "deleteRule", [this](context & c, std::span<value> args) {
        css_sheet_record * sheet = receiver_sheet(c);
        if (sheet == nullptr) { return value::undefined(); }
        if (args.empty()) {
            c.throw_error("TypeError", "deleteRule requires an index");
            return value::undefined();
        }
        const double asked = context::to_number(args[0]);
        if (!(asked >= 0) || asked >= static_cast<double>(sheet->rules.size())) {
            throw_dom_exception(c, "IndexSizeError", "there is no rule at that index");
            return value::undefined();
        }
        // THE MIRROR OF THE INSERT RULE, CSSOM 6.3.4: removing a `@namespace`
        // from a sheet that has anything but `@import` and `@namespace` in it
        // would change what the remaining selectors mean, so it is refused.
        const std::size_t going = sheet->rules[static_cast<std::size_t>(asked)];
        if (going < css_rule_store_.size() && css_rule_store_[going]->type == namespace_rule) {
            for (const std::size_t each : sheet->rules) {
                const std::uint32_t kind =
                    each < css_rule_store_.size() ? css_rule_store_[each]->type : 0;
                if (kind != import_rule && kind != namespace_rule) {
                    throw_dom_exception(c, "InvalidStateError",
                                        "the sheet has rules that namespace would change");
                    return value::undefined();
                }
            }
        }
        sheet->rules.erase(sheet->rules.begin() + static_cast<std::ptrdiff_t>(asked));
        style_sheets_changed();
        return value::undefined();
    });
    // The two legacy IE spellings CSSOM keeps: `removeRule` is `deleteRule` with
    // a default index, and `addRule` builds a rule out of two strings and always
    // answers -1.
    method(sheet_proto, "removeRule", [](context & c, std::span<value> args) {
        const value self = c.current_this();
        const value method_value = c.lookup_property(self, "deleteRule");
        const value index = args.empty() ? value::number(0) : args[0];
        const value forwarded[1] = {index};
        return c.call(method_value, forwarded, self);
    });
    method(sheet_proto, "addRule", [](context & c, std::span<value> args) {
        const value self = c.current_this();
        const std::string selector = args.empty() ? std::string{"undefined"} : c.to_string(args[0]);
        const std::string block = args.size() > 1 ? c.to_string(args[1]) : std::string{"undefined"};
        const value length = c.lookup_property(c.lookup_property(self, "cssRules"), "length");
        const value index = args.size() > 2 ? args[2] : length;
        const value method_value = c.lookup_property(self, "insertRule");
        const value forwarded[2] = {c.string(selector + " { " + block + " }"), index};
        (void)c.call(method_value, forwarded, self);
        return value::number(-1);
    });
    // The two differ in HOW they refuse, not in what they refuse: `replaceSync`
    // THROWS a NotAllowedError and `replace` returns a promise REJECTED with
    // one. `CSSStyleSheet-constructable-replace-on-regular-sheet.html` asserts
    // both halves separately, and a throw out of `replace` fails that test with
    // an uncaught exception rather than the rejection it is waiting for.
    const auto replace_rules = [this](context & c, std::span<value> args) -> bool {
        css_sheet_record * sheet = receiver_sheet(c);
        if (sheet == nullptr || !sheet->constructed) { return false; }
        const std::size_t at = slot_index(as_object(c.current_this()), sheet_key);
        parse_sheet_rules(at, args.empty() ? std::string{} : c.to_string(args[0]));
        style_sheets_changed();
        return true;
    };
    method(sheet_proto, "replaceSync", [this, replace_rules](context & c, std::span<value> args) {
        if (!replace_rules(c, args)) {
            throw_dom_exception(c, "NotAllowedError",
                                "replace is only allowed on a constructed stylesheet");
        }
        return value::undefined();
    });
    method(sheet_proto, "replace", [this, replace_rules](context & c, std::span<value> args) {
        // The work is synchronous - there is no subresource to fetch, `@import`
        // being ignored - so the promise is already settled. What matters to a
        // page is that it IS a promise, that it resolves with the sheet, and
        // that a refusal arrives as a rejection.
        const value self = c.current_this();
        if (!replace_rules(c, args)) {
            return c.make_promise(make_dom_exception(c, "NotAllowedError",
                                                     "replace is only allowed on a "
                                                     "constructed stylesheet"),
                                  true);
        }
        return c.make_promise(self, false);
    });

    // --- CSSRule and its subclasses
    script::object_object * rule_proto = interface("CSSRule", nullptr, nullptr);
    struct rule_constant {
        const char * name;
        std::uint32_t value;
    };
    static constexpr rule_constant rule_constants[] = {
        {"STYLE_RULE", style_rule},         {"CHARSET_RULE", 2},
        {"IMPORT_RULE", import_rule},       {"MEDIA_RULE", media_rule},
        {"FONT_FACE_RULE", font_face_rule}, {"PAGE_RULE", page_rule},
        {"KEYFRAMES_RULE", keyframes_rule}, {"KEYFRAME_RULE", 8},
        {"NAMESPACE_RULE", namespace_rule}, {"COUNTER_STYLE_RULE", counter_style_rule},
        {"SUPPORTS_RULE", supports_rule},   {"FONT_FEATURE_VALUES_RULE", 14}};
    for (const rule_constant & each : rule_constants) {
        const value held = value::number(static_cast<double>(each.value));
        rule_proto->define(each.name, held, script::attr_enumerable);
        if (const value * ctor = internals->find("CSSRule")) {
            if (auto * fn = static_cast<script::native_object *>(ctor->as_heap())) {
                fn->define(each.name, held, script::attr_enumerable);
            }
        }
    }
    getter(rule_proto, "type", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        return value::number(rule == nullptr ? 0 : static_cast<double>(rule->type));
    });
    getter(rule_proto, "cssText", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        return c.string(rule == nullptr ? std::string{} : rule_css_text(*rule));
    });
    getter(rule_proto, "parentRule", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr || rule->parent >= css_rule_store_.size()) { return value::null(); }
        return make_rule_object(c, rule->parent);
    });
    getter(rule_proto, "parentStyleSheet", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr || rule->sheet >= css_sheets_.size()) { return value::null(); }
        if (!css_sheets_[rule->sheet]->constructed && css_sheets_[rule->sheet]->owner) {
            return sheet_object_of(c, css_sheets_[rule->sheet]->owner);
        }
        return make_sheet_object(c, rule->sheet);
    });

    script::object_object * style_rule_proto = interface("CSSStyleRule", "CSSRule", nullptr);
    accessor(
        style_rule_proto, "selectorText",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : rule->selector);
        },
        [this](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            // "Parse the given value; if it returns failure, do nothing" -
            // CSSOM 6.4.2, and the whole of that test is the difference between
            // a selector that is WRONG and one this engine merely cannot match.
            // `parse_selector_text` is the entry point that tells them apart,
            // and it is the same one `querySelector` uses: its out parameter is
            // a SYNTAX error, so `::gibberish` leaves the rule alone while
            // `::before` replaces it and simply matches nothing. It replaced a
            // probe that appended `{--ctbrowser-probe:1}` and ran the SHEET
            // parser, which could not tell the two apart at all and which a `{`
            // inside an attribute value could derail.
            const std::string text = arg_string(c, a, 0);
            bool bad = false;
            const style::css::stylesheet parsed =
                style::css::parse_selector_text(text, *atoms_, bad);
            if (bad || parsed.selectors.empty()) { return value::undefined(); }
            rule->selector = representable(parsed.selectors)
                                 ? serialize_selector_list(parsed.selectors, *atoms_)
                                 : collapse_whitespace(text);
            style_sheets_changed();
            return value::undefined();
        });
    // `.style`, ON EVERY RULE WHOSE BLOCK IS DECLARATIONS - which is five of
    // them and was one. CSSOM gives a CSSStyleDeclaration to CSSStyleRule,
    // CSSFontFaceRule, CSSPageRule, CSSKeyframeRule and CSSCounterStyleRule
    // alike; `css/cssom/property-accessors.html` reaches for
    // `document.styleSheets[0].cssRules[0].style` where rule zero is a
    // `@font-face`, and nine of its nine subtests died on `getPropertyValue is
    // undefined` rather than on anything it set out to test.
    const auto declaration_accessor = [&](script::object_object * on) {
        accessor(
            on, "style",
            [this](context & c, std::span<value>) {
                // [SameObject], and LAZY. The declaration object is where the
                // ~290 property accessors are reachable from, and a sheet with
                // three thousand rules in it would otherwise build three
                // thousand of them for a page that never reads one.
                script::object_object * self = as_object(c.current_this());
                if (self == nullptr) { return value::undefined(); }
                if (const value * held = self->find(style_key)) {
                    refresh_declaration_object(c, *held);
                    return *held;
                }
                const std::size_t at = slot_index(self, rule_key);
                const value made = declaration_object(c, at);
                self->define(style_key, made, script::attr_none);
                return made;
            },
            [](context & c, std::span<value> a) {
                // [PutForwards=cssText]: `rule.style = "margin: 42px"` assigns
                // to `rule.style.cssText` and the object itself never changes.
                const value self = c.current_this();
                const value declarations = c.lookup_property(self, "style");
                c.store_property(declarations, "cssText", a.empty() ? c.string("") : a[0]);
                return value::undefined();
            });
    };
    declaration_accessor(style_rule_proto);

    script::object_object * grouping_proto = interface("CSSGroupingRule", "CSSRule", nullptr);
    getter(grouping_proto, "cssRules", [this](context & c, std::span<value>) {
        script::object_object * self = as_object(c.current_this());
        const css_rule_record * rule = receiver_rule(c);
        if (self == nullptr || rule == nullptr) { return value::undefined(); }
        if (const value * held = self->find(rules_key)) {
            refresh_rule_list(c, *held, rule->children);
            return *held;
        }
        const value list = make_rule_list(c, rule->children);
        self->define(rules_key, list, script::attr_none);
        return list;
    });
    // insertRule/deleteRule ON THE GROUP, which is the same pair of methods
    // CSSStyleSheet has and NOT the same list: a rule inserted here becomes a
    // child of the group and never a sibling of it. `@media print {}` followed
    // by `rule.insertRule(...)` is how `css/cssom/serialize-media-rule.html`
    // builds every one of its fixtures.
    method(grouping_proto, "insertRule", [this](context & c, std::span<value> args) {
        css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return value::undefined(); }
        if (args.empty()) {
            c.throw_error("TypeError", "insertRule requires a rule");
            return value::undefined();
        }
        const std::string text = c.to_string(args[0]);
        // `undefined` for an optional argument is the DEFAULT, not
        // `ToNumber(undefined)` - see the sheet's `insertRule`.
        const double asked =
            args.size() > 1 && !args[1].is_undefined() ? context::to_number(args[1]) : 0;
        // THE INDEX IS CHECKED BEFORE THE TEXT IS PARSED, which is the order
        // CSSOM 6.4.3 gives and which `CSSGroupingRule-insertRule.html` asserts
        // by passing a deliberate syntax error at an out-of-range index and
        // demanding the IndexSizeError.
        if (!(asked >= 0) || asked > static_cast<double>(rule->children.size())) {
            throw_dom_exception(c, "IndexSizeError", "the index is past the end of the rule");
            return value::undefined();
        }
        const std::size_t self = slot_index(as_object(c.current_this()), rule_key);
        std::string error;
        const std::size_t made = parse_one_rule(rule->sheet, text, error);
        if (made == no_index) {
            throw_dom_exception(c, error.empty() ? std::string{"SyntaxError"} : error,
                                "the text is not a single CSS rule");
            return value::undefined();
        }
        // "If new rule cannot be inserted at index because the rule is not
        // allowed there, throw a HierarchyRequestError." `@import` and
        // `@namespace` are top-level rules and a grouping rule is not the top
        // level. The record is dropped rather than orphaned - it was appended a
        // moment ago and nothing else has seen it.
        const std::uint32_t kind = css_rule_store_[made]->type;
        if (kind == import_rule || kind == namespace_rule) {
            if (made + 1 == css_rule_store_.size()) { css_rule_store_.pop_back(); }
            throw_dom_exception(c, "HierarchyRequestError",
                                "that rule is not allowed inside a grouping rule");
            return value::undefined();
        }
        css_rule_store_[made]->parent = self;
        rule->children.insert(rule->children.begin() + static_cast<std::ptrdiff_t>(asked), made);
        style_sheets_changed();
        return value::number(asked);
    });
    method(grouping_proto, "deleteRule", [this](context & c, std::span<value> args) {
        css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return value::undefined(); }
        if (args.empty()) {
            c.throw_error("TypeError", "deleteRule requires an index");
            return value::undefined();
        }
        const double asked = context::to_number(args[0]);
        if (!(asked >= 0) || asked >= static_cast<double>(rule->children.size())) {
            throw_dom_exception(c, "IndexSizeError", "there is no rule at that index");
            return value::undefined();
        }
        rule->children.erase(rule->children.begin() + static_cast<std::ptrdiff_t>(asked));
        style_sheets_changed();
        return value::undefined();
    });
    script::object_object * condition_proto =
        interface("CSSConditionRule", "CSSGroupingRule", nullptr);
    getter(condition_proto, "conditionText", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return c.string(""); }
        // "The conditionText of a CSSMediaRule is its media's mediaText" -
        // CSSOM 6.4.4. Only `@supports` answers from `prelude`, its condition
        // being a `<supports-condition>` and not a media query list.
        if (rule->type == media_rule) {
            return c.string(serialize_media_query_list(rule->media_queries));
        }
        return c.string(rule->prelude);
    });
    script::object_object * media_rule_proto =
        interface("CSSMediaRule", "CSSConditionRule", nullptr);
    accessor(
        media_rule_proto, "media",
        [this](context & c, std::span<value>) {
            script::object_object * self = as_object(c.current_this());
            if (self == nullptr || receiver_rule(c) == nullptr) { return value::undefined(); }
            return media_list_object(c, *self);
        },
        [](context & c, std::span<value> a) {
            const value list = c.lookup_property(c.current_this(), "media");
            c.store_property(list, "mediaText", a.empty() ? c.string("") : a[0]);
            return value::undefined();
        });
    (void)interface("CSSSupportsRule", "CSSConditionRule", nullptr);
    declaration_accessor(interface("CSSFontFaceRule", "CSSRule", nullptr));

    // --- CSSImportRule, CSSOM 6.4.7
    //
    // `styleSheet` IS NULL AND THAT IS THE HONEST ANSWER: nothing here fetches
    // an `@import`, and a page reading `rule.styleSheet.cssRules` must find out
    // that there is no sheet rather than find an empty one that claims the
    // import succeeded. `cssimportrule.html` asserts a CSSStyleSheet there and
    // that subtest stays red until the loader does the fetch.
    script::object_object * import_proto = interface("CSSImportRule", "CSSRule", nullptr);
    getter(import_proto, "href", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        return c.string(rule == nullptr ? std::string{} : rule->selector);
    });
    getter(import_proto, "styleSheet", [](context &, std::span<value>) { return value::null(); });
    accessor(
        import_proto, "media",
        [this](context & c, std::span<value>) {
            script::object_object * self = as_object(c.current_this());
            if (self == nullptr || receiver_rule(c) == nullptr) { return value::undefined(); }
            return media_list_object(c, *self);
        },
        [](context & c, std::span<value> a) {
            const value list = c.lookup_property(c.current_this(), "media");
            c.store_property(list, "mediaText", a.empty() ? c.string("") : a[0]);
            return value::undefined();
        });
    // `layerName` and `supportsText` are NULL when the import has neither,
    // which is the difference CSSOM draws between "no layer" and "an anonymous
    // one" - `@import url(a) layer` has a layer whose name is the empty string.
    const auto import_part = [this](context & c, std::string_view keyword, bool bare) {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return value::null(); }
        std::size_t at = 0;
        while (true) {
            const std::string_view part = next_component(rule->prelude, at);
            if (part.empty()) { return value::null(); }
            if (bare && ascii_iequals(part, keyword)) { return c.string(""); }
            if (part.size() > keyword.size() + 1 &&
                ascii_iequals(part.substr(0, keyword.size() + 1), std::string{keyword} + "(") &&
                part.back() == ')') {
                return c.string(
                    std::string{part.substr(keyword.size() + 1, part.size() - keyword.size() - 2)});
            }
        }
    };
    getter(import_proto, "layerName",
           [import_part](context & c, std::span<value>) { return import_part(c, "layer", true); });
    getter(import_proto, "supportsText", [import_part](context & c, std::span<value>) {
        return import_part(c, "supports", false);
    });

    // --- CSSNamespaceRule, CSSOM 6.4.9. A DEFAULT namespace has no prefix, and
    // the empty string is how CSSOM reports that - not `null`.
    script::object_object * namespace_proto = interface("CSSNamespaceRule", "CSSRule", nullptr);
    getter(namespace_proto, "prefix", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        return c.string(rule == nullptr ? std::string{} : rule->selector);
    });
    getter(namespace_proto, "namespaceURI", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        return c.string(rule == nullptr ? std::string{} : rule->prelude);
    });

    // --- CSSPageRule. A grouping rule in CSSOM's current draft and a plain
    // CSSRule in the 2011 one; the draft is what Chrome exposes.
    script::object_object * page_proto = interface("CSSPageRule", "CSSGroupingRule", nullptr);
    declaration_accessor(page_proto);
    accessor(
        page_proto, "selectorText",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : rule->prelude);
        },
        [this](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            // "If the parse failed, do nothing" - and `named :first` is a parse
            // failure rather than two selectors, because a `<page-selector>`
            // has no whitespace in it anywhere.
            bool ok = false;
            const std::string parsed = serialize_page_selector(arg_string(c, a, 0), ok);
            if (!ok) { return value::undefined(); }
            rule->prelude = parsed;
            style_sheets_changed();
            return value::undefined();
        });

    // --- CSSKeyframesRule and CSSKeyframeRule
    script::object_object * keyframes_proto = interface("CSSKeyframesRule", "CSSRule", nullptr);
    accessor(
        keyframes_proto, "name",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : rule->prelude);
        },
        [this](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            rule->prelude = collapse_whitespace(arg_string(c, a, 0));
            style_sheets_changed();
            return value::undefined();
        });
    getter(keyframes_proto, "cssRules", [this](context & c, std::span<value>) {
        script::object_object * self = as_object(c.current_this());
        const css_rule_record * rule = receiver_rule(c);
        if (self == nullptr || rule == nullptr) { return value::undefined(); }
        if (const value * held = self->find(rules_key)) {
            refresh_rule_list(c, *held, rule->children);
            return *held;
        }
        const value list = make_rule_list(c, rule->children);
        self->define(rules_key, list, script::attr_none);
        return list;
    });
    getter(keyframes_proto, "length", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        return value::number(rule == nullptr ? 0 : static_cast<double>(rule->children.size()));
    });
    // `appendRule` takes a whole keyframe and `deleteRule`/`findRule` take a
    // keyText - NOT an index, which is what makes this trio different from
    // every other insert/delete pair in the CSSOM.
    method(keyframes_proto, "appendRule", [this](context & c, std::span<value> a) {
        css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return value::undefined(); }
        const std::size_t self = slot_index(as_object(c.current_this()), rule_key);
        std::string error;
        const std::size_t made =
            parse_one_rule(rule->sheet, "@keyframes _ {" + arg_string(c, a, 0) + "}", error);
        if (made == no_index || css_rule_store_[made]->children.empty()) {
            return value::undefined();
        }
        const std::size_t frame = css_rule_store_[made]->children.front();
        css_rule_store_[frame]->parent = self;
        rule->children.push_back(frame);
        style_sheets_changed();
        return value::undefined();
    });
    const auto keyframe_at = [this](context & c, const std::string & key) -> std::size_t {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return no_index; }
        for (std::size_t i = rule->children.size(); i > 0; --i) {
            const std::size_t child = rule->children[i - 1];
            if (child < css_rule_store_.size() && css_rule_store_[child]->selector == key) {
                return i - 1;
            }
        }
        return no_index;
    };
    method(keyframes_proto, "findRule", [this, keyframe_at](context & c, std::span<value> a) {
        css_rule_record * rule = receiver_rule(c);
        // "Return the LAST rule that matches", which is why the search above
        // walks backwards: a keyframes rule may name the same key twice.
        const std::size_t found = keyframe_at(c, collapse_whitespace(arg_string(c, a, 0)));
        if (rule == nullptr || found == no_index) { return value::null(); }
        return make_rule_object(c, rule->children[found]);
    });
    method(keyframes_proto, "deleteRule", [this, keyframe_at](context & c, std::span<value> a) {
        css_rule_record * rule = receiver_rule(c);
        const std::size_t found = keyframe_at(c, collapse_whitespace(arg_string(c, a, 0)));
        if (rule == nullptr || found == no_index) { return value::undefined(); }
        rule->children.erase(rule->children.begin() + static_cast<std::ptrdiff_t>(found));
        style_sheets_changed();
        return value::undefined();
    });
    script::object_object * keyframe_proto = interface("CSSKeyframeRule", "CSSRule", nullptr);
    declaration_accessor(keyframe_proto);
    accessor(
        keyframe_proto, "keyText",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : rule->selector);
        },
        [this](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            rule->selector = collapse_whitespace(arg_string(c, a, 0));
            style_sheets_changed();
            return value::undefined();
        });
    declaration_accessor(interface("CSSCounterStyleRule", "CSSRule", nullptr));

    // --- CSSStyleDeclaration
    //
    // ONE PROTOTYPE FOR THE WHOLE PAGE, carrying an accessor per property under
    // both spellings. `el.style` is a PROXY over a store, which is the other way
    // to answer an unbounded property set - it costs two natives per element and
    // makes `el.style instanceof CSSStyleDeclaration` false, a proxy not being
    // an object as far as the prototype walk is concerned. Here the set is
    // bounded (the property table IS the set of IDL attributes a
    // CSSStyleDeclaration has), so ~290 accessors on one shared prototype answer
    // every rule in the document and `instanceof` works.
    script::object_object * declaration_proto = interface("CSSStyleDeclaration", nullptr, nullptr);
    const auto property_accessor = [&](const std::string & idl, const std::string & css) {
        declaration_proto->define_accessor(
            idl,
            value::object(cx.allocate<script::native_object>(
                "get " + idl,
                [this, css](context & c, std::span<value>) {
                    const css_rule_record * rule = receiver_rule(c);
                    if (rule == nullptr) { return c.string(""); }
                    for (const css_declaration & declared : rule->declarations) {
                        if (declared.name == css) { return c.string(declared.value); }
                    }
                    return c.string("");
                })),
            value::object(cx.allocate<script::native_object>(
                "set " + idl,
                [this, css](context & c, std::span<value> a) {
                    css_rule_record * rule = receiver_rule(c);
                    if (rule == nullptr) { return value::undefined(); }
                    if (store_declaration(rule->declarations, css, arg_string(c, a, 0), false,
                                          false)) {
                        refresh_declaration_object(c, c.current_this());
                        style_sheets_changed();
                    }
                    return value::undefined();
                })),
            script::attr_enumerable | script::attr_configurable);
    };
    for (const style::css::property_syntax & property : known_properties()) {
        const std::string css{property.name};
        property_accessor(css, css);
        const std::string idl = idl_name_of(css);
        if (idl != css) { property_accessor(idl, css); }
    }
    getter(declaration_proto, "length", [this](context & c, std::span<value>) {
        const css_rule_record * rule = receiver_rule(c);
        return value::number(rule == nullptr ? 0 : static_cast<double>(rule->declarations.size()));
    });
    getter(declaration_proto, "parentRule", [this](context & c, std::span<value>) {
        script::object_object * self = as_object(c.current_this());
        const std::size_t at = slot_index(self, rule_key);
        if (at >= css_rule_store_.size()) { return value::null(); }
        return make_rule_object(c, at);
    });
    accessor(
        declaration_proto, "cssText",
        [this](context & c, std::span<value>) {
            const css_rule_record * rule = receiver_rule(c);
            return c.string(rule == nullptr ? std::string{} : serialize_block(rule->declarations));
        },
        [this](context & c, std::span<value> a) {
            css_rule_record * rule = receiver_rule(c);
            if (rule == nullptr) { return value::undefined(); }
            // A WHOLE-BLOCK REPLACEMENT, not a merge, and through the same
            // declaration-list parser a `style` attribute goes through - so a
            // `;` inside a string cannot end a declaration here either.
            rule->declarations.clear();
            const style::css::stylesheet parsed =
                style::css::parse_declaration_list(arg_string(c, a, 0), *atoms_);
            for (const style::css::raw_declaration & d : parsed.declarations) {
                const std::string name{atoms_->text(d.property)};
                const style::css::value_check checked =
                    check_declaration(name, parsed.text_of(d), false);
                if (!checked.valid) { continue; }
                rule->declarations.push_back(
                    css_declaration{name, checked.serialized, d.important});
            }
            refresh_declaration_object(c, c.current_this());
            style_sheets_changed();
            return value::undefined();
        });
    method(declaration_proto, "item", [](context & c, std::span<value> a) {
        const value held = collection_item(c, a);
        // CSSOM's `item` answers the EMPTY STRING past the end here, unlike the
        // collections above whose `item` answers null.
        return held.is_null() ? c.string("") : held;
    });
    method(declaration_proto, "getPropertyValue", [this](context & c, std::span<value> a) {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return c.string(""); }
        const std::string name = asked_name(c, a);
        for (const css_declaration & declared : rule->declarations) {
            if (declared.name == name) { return c.string(declared.value); }
        }
        return c.string("");
    });
    method(declaration_proto, "getPropertyPriority", [this](context & c, std::span<value> a) {
        const css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return c.string(""); }
        const std::string name = asked_name(c, a);
        for (const css_declaration & declared : rule->declarations) {
            if (declared.name == name) { return c.string(declared.important ? "important" : ""); }
        }
        return c.string("");
    });
    method(declaration_proto, "setProperty", [this](context & c, std::span<value> a) {
        css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return value::undefined(); }
        // "If priority is not the empty string and is not an ASCII
        // case-insensitive match for 'important', return" - CSSOM 6.7.2.
        const std::string priority = a.size() > 2 ? c.to_string(a[2]) : std::string{};
        if (!priority.empty() && !ascii_iequals(priority, "important")) {
            return value::undefined();
        }
        if (store_declaration(rule->declarations, asked_name(c, a),
                              a.size() > 1 ? c.to_string(a[1]) : std::string{}, false,
                              !priority.empty())) {
            refresh_declaration_object(c, c.current_this());
            style_sheets_changed();
        }
        return value::undefined();
    });
    method(declaration_proto, "removeProperty", [this](context & c, std::span<value> a) {
        css_rule_record * rule = receiver_rule(c);
        if (rule == nullptr) { return c.string(""); }
        const std::string name = asked_name(c, a);
        std::string was;
        for (std::size_t i = 0; i < rule->declarations.size(); ++i) {
            if (rule->declarations[i].name != name) { continue; }
            was = rule->declarations[i].value;
            rule->declarations.erase(rule->declarations.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
        refresh_declaration_object(c, c.current_this());
        style_sheets_changed();
        return c.string(was);
    });
}

void dom_bindings::install_style_sheets(context & cx) {
    install_stylesheet_prototypes(cx);
    script::object_object * doc = document_object();
    if (doc == nullptr) { return; }
    // `document.styleSheets` - [SameObject], and LIVE ENOUGH: the list object
    // keeps its identity forever and its contents are re-derived from the DOM on
    // every read, which is what makes a `<style>` a script appended a moment ago
    // show up without an invalidation hook.
    doc->define_accessor(
        "styleSheets",
        value::object(cx.allocate<script::native_object>("get styleSheets",
                                                         [this](context & c, std::span<value>) {
                                                             sync_style_sheets(c);
                                                             return style_sheet_list(c);
                                                         })),
        value::undefined(), script::attr_configurable);
    doc->define_accessor(
        "adoptedStyleSheets",
        value::object(cx.allocate<script::native_object>("get adoptedStyleSheets",
                                                         [this](context & c, std::span<value>) {
                                                             script::object_object * internals =
                                                                 cssom_internals(c);
                                                             if (internals == nullptr) {
                                                                 return value::undefined();
                                                             }
                                                             if (const value * held =
                                                                     internals->find("adopted")) {
                                                                 return *held;
                                                             }
                                                             const value made = c.make_array();
                                                             internals->set("adopted", made);
                                                             return made;
                                                         })),
        value::object(cx.allocate<script::native_object>(
            "set adoptedStyleSheets",
            [this](context & c, std::span<value> a) {
                script::object_object * internals = cssom_internals(c);
                if (internals == nullptr) { return value::undefined(); }
                const value made = c.make_array();
                auto * items = static_cast<script::array_object *>(made.as_heap());
                if (!a.empty() && a[0].is_array()) {
                    auto * given = static_cast<script::array_object *>(a[0].as_heap());
                    for (const value each : given->items) {
                        const std::size_t at = slot_index(as_object(each), sheet_key);
                        // "Only sheets constructed in this document may be
                        // adopted", which is the one check this can make and the
                        // one `adoptedstylesheets-*` asserts.
                        if (at >= css_sheets_.size() || !css_sheets_[at]->constructed) {
                            throw_dom_exception(c, "NotAllowedError",
                                                "only a constructed CSSStyleSheet may be adopted");
                            return value::undefined();
                        }
                        items->items.push_back(each);
                    }
                }
                internals->set("adopted", made);
                style_sheets_changed();
                return value::undefined();
            })),
        script::attr_configurable);
}

} // namespace ctbrowser::shell
