// dom_bindings' CSSOM - serialising: the canonical text of a compiled selector
// list (Selectors 4) and of a media query list (Media Queries 4).

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

using namespaces = std::span<const style::css::namespace_declaration>;

void append_compound(std::string & out, const style::compound & part, const atom_table & atoms,
                     namespaces scope);

[[nodiscard]] std::string serialize_selector(const style::compiled_selector & sel,
                                             const atom_table & atoms, namespaces scope) {
    std::string out;
    if (sel.parts.empty()) { return out; }
    // Parts are stored RIGHTMOST FIRST and `links[i]` joins parts[i] to
    // parts[i+1], so the text runs from the last part backwards and the link
    // between parts[i] and parts[i-1] is links[i-1].
    for (std::size_t i = sel.parts.size(); i-- > 0;) {
        append_compound(out, sel.parts[i], atoms, scope);
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

// `namespaces` is the sheet's `@namespace` rules, which CSSOM §6.7 needs to
// serialise a type selector: a prefix that maps to the DEFAULT namespace is
// omitted, `*|` is omitted when there is no default for it to differ from, and
// any other prefix is written. With no rules to consult - `querySelector`'s
// case - only the null namespace's `|` and a named prefix survive.
[[nodiscard]] std::string serialize_selector_list(
    std::span<const style::compiled_selector> list, const atom_table & atoms,
    std::span<const style::css::namespace_declaration> namespaces) {
    std::string out;
    for (const style::compiled_selector & sel : list) {
        if (!out.empty()) { out += ", "; }
        out += serialize_selector(sel, atoms, namespaces);
    }
    return out;
}

[[nodiscard]] std::string serialize_selector_list(std::span<const style::compiled_selector> list,
                                                  const atom_table & atoms) {
    return serialize_selector_list(list, atoms, {});
}

// IS THE COMPILED FORM THE WHOLE OF WHAT THE AUTHOR WROTE?
//
// `dropped` is how the selector compiler records a construct it can PARSE and
// has no field for: `:has()`, `::part(x)`, a namespaced attribute. Nothing of
// that survives into the compiled form, so `append_compound` would emit the
// compound without it - and that does not merely look wrong. `author_style_text`
// hands these selectors back to the cascade, so an inserted `div:has(a) { color:
// red }` serialised as `div` would colour every div on the page. Wherever the
// author's bytes are still to hand they are the honest answer, and this is the
// question that decides. A compound that is merely UNMATCHABLE - `::before`,
// `ns|e` - is still whole, and serialises canonically.
[[nodiscard]] bool representable(std::span<const style::compiled_selector> list) {
    const auto compound_ok = [](auto && self, const style::compound & part) -> bool {
        if (part.dropped) { return false; }
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

void append_compound(std::string & out, const style::compound & part, const atom_table & atoms,
                     namespaces scope) {
    const std::size_t was = out.size();
    // EVERY NAME IS AN IDENTIFIER, and CSSOM §6.7 says each is serialised by
    // §2.1's "serialize an identifier" - the same algorithm `CSS.escape` is.
    // The tokenizer DECODES escapes, so `[\30 zonk]` reaches the compiled form
    // as the name `0zonk`; printing that back unescaped produces a selector that
    // is not a selector, because an identifier may not begin with a digit.
    const auto ident = &style::css::serialize_identifier;
    // THE NAMESPACE PREFIX, CSSOM §6.7 "serialize a simple selector": written
    // when it maps to a namespace that is neither the default nor the null one,
    // `|` alone for the null one. `*|` means any namespace, which is what an
    // unprefixed name means too until a default namespace is declared.
    const style::css::namespace_declaration * default_ns = nullptr;
    for (const style::css::namespace_declaration & each : scope) {
        if (each.prefix.empty()) { default_ns = &each; }
    }
    switch (part.ns) {
    case style::ns_prefix::unset: break;
    case style::ns_prefix::any:
        if (default_ns != nullptr) { out += "*|"; }
        break;
    case style::ns_prefix::none: out += '|'; break;
    case style::ns_prefix::named: {
        const std::string_view prefix = atoms.text(part.ns_name);
        const style::css::namespace_declaration * bound = nullptr;
        for (const style::css::namespace_declaration & each : scope) {
            if (each.prefix == prefix) { bound = &each; }
        }
        const bool is_default =
            bound != nullptr && default_ns != nullptr && bound->uri == default_ns->uri;
        if (!is_default) {
            out += ident(prefix);
            out += '|';
        }
        break;
    }
    }
    // A universal selector is written only when a prefix stands before it, or
    // when it is the whole compound - which the fall-through at the end handles.
    if (part.tag) {
        out += ident(atoms.text(part.tag));
    } else if (out.size() != was) {
        out += '*';
    }
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
        // The attribute's prefix, CSSOM §6.7: written when it maps to a
        // namespace that is not the null one - so `[|lang]` is `[lang]` - and
        // `*|` for any. A named prefix is found back from the URI it bound.
        switch (attribute.ns) {
        case style::ns_prefix::unset:
        case style::ns_prefix::none: break;
        case style::ns_prefix::any: out += "*|"; break;
        case style::ns_prefix::named:
            for (const style::css::namespace_declaration & each : scope) {
                if (each.prefix.empty() || each.uri != atoms.text(attribute.ns_uri)) { continue; }
                out += ident(each.prefix);
                out += '|';
                break;
            }
            break;
        }
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
            out += ":not(" + serialize_selector_list(pseudo.args, atoms, scope) + ")";
            break;
        case style::pseudo_kind::is_:
            out += ":is(" + serialize_selector_list(pseudo.args, atoms, scope) + ")";
            break;
        case style::pseudo_kind::where_:
            out += ":where(" + serialize_selector_list(pseudo.args, atoms, scope) + ")";
            break;
        // `:lang()` AND `:dir()` KEEP THEIR ARGUMENT, because a language RANGE
        // is not an identifier: `*-Latn` is a legal one, and `:lang(de, fr)` is
        // a comma-separated list of them. They used to compile to nothing at
        // all, so a rule carrying one serialised as `*` - eight of
        // `css/cssom/selectorSerialize.html`'s twenty-three. A range that IS an
        // identifier is written as one, escapes and all - `:lang(j\ a)` - and
        // one with a wildcard in it as the string it can only be.
        case style::pseudo_kind::lang:
        case style::pseudo_kind::dir: {
            out += pseudo.kind == style::pseudo_kind::lang ? ":lang(" : ":dir(";
            bool first = true;
            for (const std::string & range : pseudo.ranges) {
                if (!first) { out += ", "; }
                first = false;
                out += range.find('*') == std::string::npos ? ident(range) : quoted_string(range);
            }
            out += ')';
            break;
        }
        }
    }
    // "If there is only one simple selector in the compound selector which is a
    // universal selector, append '*'." A compound that produced nothing is that
    // selector - and a pseudo-element is a simple selector, so `*::before` is
    // `::before`.
    if (out.size() == was && !part.pseudo_element) { out += '*'; }
    // THE PSEUDO-ELEMENT LAST, with two colons whichever the author wrote: CSSOM
    // §6.7 serialises `:before` as `::before`.
    if (part.pseudo_element) {
        out += "::";
        out += ident(atoms.text(part.pseudo_element));
    }
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

// The top-level commas of a media query list. Top-level because a feature's
// parentheses may hold one - `(width >= calc(1px, 2px))` does not exist, but a
// `url()` in a `@supports` prelude does, and this splitter is used for both.
// EMPTY PARTS ARE KEPT - `"screen,"` is two queries and the second serialises
// as `not all` - which is why this is not core's split_top_level.
[[nodiscard]] std::vector<std::string_view> split_on_commas(std::string_view text) {
    std::vector<std::string_view> out;
    for (std::size_t start = 0;;) {
        const std::size_t comma = scan_to(text, start, ",");
        out.push_back(text.substr(start, comma - start));
        if (comma >= text.size()) { return out; }
        start = comma + 1;
    }
}

} // namespace detail

namespace {

// `(name)` or `(name: value)`, with the parentheses already on it.
[[nodiscard]] std::string serialize_media_feature_text(std::string_view part) {
    const std::string_view inside = part.substr(1, part.size() - 2);
    const std::size_t colon = inside.find(':');
    if (colon == std::string_view::npos) {
        return "(" + ascii_lower_copy(collapse_whitespace(inside, html_whitespace)) + ")";
    }
    // THE NAME IS FOLDED AND THE VALUE IS NOT. A feature name is an identifier
    // and `(Color)` and `(color)` are one feature; a value may be a string, a
    // `url()` or a number with a unit, none of which fold.
    return "(" + ascii_lower_copy(collapse_whitespace(inside.substr(0, colon), html_whitespace)) +
           ": " + collapse_whitespace(inside.substr(colon + 1), html_whitespace) + ")";
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
