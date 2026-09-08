// dom_bindings' CSSOM - serialising: the canonical text of a compiled selector
// list (Selectors 4) and of a media query list (Media Queries 4).
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

} // namespace detail

namespace {

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

} // namespace

namespace detail {

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

} // namespace detail

namespace {

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

} // namespace

namespace detail {

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

} // namespace detail

namespace {

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

} // namespace

namespace detail {

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

} // namespace detail

} // namespace ctbrowser::shell
