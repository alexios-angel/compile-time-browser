//===- serialize.cpp - CSSOM serialisation ----------------------*- C++ -*-===//
//
// CSSOM serialisation: the canonical text of a compiled selector list
// (Selectors 4, CSSOM §6.7) and of a media query list (Media Queries 4),
// plus the CSS string quoting both use.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace detail {

// --- Serialising a selector ------------------------------------------------
//
// Selectors 4 §serialize, over the compiled form. The compiled form has lost
// the author's ordering within a compound (`a.b#c` and `a#c.b` compile the
// same), so this emits the canonical order the specification asks for: type,
// then id, then classes, then attributes, then pseudo-classes.

/// A CSS string, double-quoted, with `"` and `\` escaped. An attribute
/// selector's value is serialised as a string however the author wrote it:
/// `[a=b]` comes back as `[a="b"]`.
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

/// Serialises `An+B`, the argument of `:nth-child()` and its three siblings.
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

/// Serialises one complex selector: its compounds joined by combinators.
[[nodiscard]] std::string serialize_selector(const style::compiled_selector & sel,
                                             const atom_table & atoms, namespaces scope) {
    std::string out;
    if (sel.parts.empty()) { return out; }
    // Parts are stored rightmost first and `links[i]` joins parts[i] to
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

/// Serialises a selector list, joined by ", ". `namespaces` is the sheet's
/// `@namespace` rules, which CSSOM §6.7 needs to serialise a type selector: a
/// prefix that maps to the default namespace is omitted, `*|` is omitted when
/// there is no default for it to differ from, and any other prefix is written.
/// With no rules to consult (`querySelector`'s case) only the null namespace's
/// `|` and a named prefix survive.
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

/// Whether the compiled form holds everything the author wrote, so that it can
/// be serialised from the compiled form rather than the author's text.
///
/// `dropped` is how the selector compiler records a construct it can parse but
/// has no field for: `:has()`, `::part(x)`, a namespaced attribute. None of it
/// survives compilation, so append_compound would emit the compound without
/// it. That changes meaning, not just text: author_style_text hands these
/// selectors back to the cascade, so `div:has(a) { color: red }` serialised as
/// `div` would colour every div. A compound that is merely unmatchable
/// (`::before`, `ns|e`) is still whole and serialises canonically.
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

/// Appends one compound selector to `out` in canonical order.
void append_compound(std::string & out, const style::compound & part, const atom_table & atoms,
                     namespaces scope) {
    const std::size_t was = out.size();
    // Every name is an identifier, which CSSOM §6.7 serialises with §2.1's
    // "serialize an identifier", the same algorithm as `CSS.escape`. The
    // tokenizer decodes escapes, so `[\30 zonk]` reaches the compiled form as
    // the name `0zonk`; printed back unescaped it would be invalid, because an
    // identifier may not begin with a digit.
    const auto ident = &style::css::serialize_identifier;
    // The namespace prefix (CSSOM §6.7 "serialize a simple selector"): written
    // when it maps to a namespace that is neither the default nor the null one,
    // and `|` alone for the null one. `*|` means any namespace, which is also
    // what an unprefixed name means until a default namespace is declared.
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
    // when it is the whole compound, which the check at the end handles.
    if (part.tag) {
        out += ident(atoms.text(part.tag));
    } else if (out.size() != was) {
        out += '*';
    }
    // The nesting selector is written back as `&`; what it compiled to (an
    // `:is()` of the parent list, or the `:scope` bit) is skipped below.
    if (part.nesting) { out += '&'; }
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
        // The attribute's prefix (CSSOM §6.7): written when it maps to a
        // namespace other than the null one (so `[|lang]` is `[lang]`), and
        // `*|` for any. A named prefix is recovered from the URI it bound.
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
            if (attribute.case_sensitive_flag) { out += " s"; }
        }
        out += ']';
    }
    // The pseudo-classes. `:checked` and `:disabled` exist in both the state and
    // the structural bitfields, so names are de-duplicated rather than emitted
    // twice.
    struct named_bit {
        std::uint32_t bit;
        std::string_view name;
    };
    static constexpr named_bit state_bits[] = {{style::state_hover, "hover"},
                                               {style::state_active, "active"},
                                               {style::state_focus, "focus"},
                                               {style::state_checked, "checked"},
                                               {style::state_disabled, "disabled"},
                                               {style::state_target, "target"},
                                               {style::state_focus_within, "focus-within"},
                                               {style::state_focus_visible, "focus-visible"}};
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
        {style::structural_visited, "visited"},
        {style::structural_scope, "scope"}};
    std::vector<std::string_view> emitted;
    const auto emit = [&](std::string_view name) {
        if (std::find(emitted.begin(), emitted.end(), name) != emitted.end()) { return; }
        emitted.push_back(name);
        out += ':';
        out += name;
    };
    for (const named_bit & each : state_bits) {
        if ((part.states & ~part.states_after_pe & each.bit) != 0) { emit(each.name); }
    }
    for (const named_bit & each : structural_bits) {
        if ((part.structural & ~part.structural_after_pe & each.bit) == 0) { continue; }
        if (each.bit == style::structural_scope && part.nesting) { continue; } // Written as `&`.
        emit(each.name);
    }
    for (const style::pseudo_ref & pseudo : part.pseudos) {
        if (pseudo.nesting) { continue; } // Written as `&` above.
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
        case style::pseudo_kind::heading: {
            out += ":heading";
            if (!pseudo.levels.empty()) {
                out += '(';
                for (std::size_t i = 0; i < pseudo.levels.size(); ++i) {
                    out += (i == 0 ? "" : ", ") + std::to_string(pseudo.levels[i]);
                }
                out += ')';
            }
            break;
        }
        // `:lang()` and `:dir()` keep their arguments as text, because a language
        // range is not always an identifier: `*-Latn` is legal, and `:lang(de, fr)`
        // is a list. A range that is an identifier is written as one, escapes
        // included (`:lang(j\ a)`); a range containing `*` is written as a string.
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
    // selector. A pseudo-element counts as a simple selector, so `*::before` is
    // `::before`.
    if (out.size() == was && !part.pseudo_element) { out += '*'; }
    // The pseudo-element comes last, with two colons whichever the author
    // wrote: CSSOM §6.7 serialises `:before` as `::before`.
    if (part.pseudo_element) {
        out += "::";
        // `highlight(name)` is stored as one atom (selector.cpp): the function
        // name and its identifier argument serialise as two identifiers.
        const std::string_view text = atoms.text(part.pseudo_element);
        if (const std::size_t paren = text.find('('); paren != std::string_view::npos) {
            out += ident(text.substr(0, paren));
            out += '(';
            out += ident(text.substr(paren + 1, text.size() - paren - 2));
            out += ')';
        } else {
            out += ident(text);
        }
        // Pseudo-classes written after the pseudo-element follow it again.
        for (const named_bit & each : state_bits) {
            if ((part.states_after_pe & each.bit) != 0) { out += ":" + std::string{each.name}; }
        }
        for (const named_bit & each : structural_bits) {
            if ((part.structural_after_pe & each.bit) != 0) { out += ":" + std::string{each.name}; }
        }
    }
}

} // namespace

namespace detail {

// --- A media query list, as CSSOM asks for it ------------------------------
//
// Media Queries 4 §"serializing a media query list", over the query's text
// rather than the compiled `media_query`, so a rule from a <style> sheet and
// one from insertRule() serialise the same way. The compiled form is what the
// cascade needs and is deliberately narrow (three media types, twelve
// features, one float per value): it would answer `all` for `speech`, `768px`
// for `48em`, and nothing for a feature this engine does not model, all of
// which css/cssom/serialize-media-rule.html compares byte for byte.
//
// The normalisation is exactly the specification's: lowercase `not`/`only`,
// the media type and each feature name; put one space after a feature's
// colon; drop an `all` that has features after it but keep a negated one; and
// preserve the order and multiplicity of the features, because
// `(max-width: 23px) and (max-width: 45px)` may be intentional and
// de-duplicating it is an open CSSWG issue.

/// Splits a media query list (or a `@supports` prelude) at its top-level
/// commas, ignoring commas inside brackets or strings such as a `url()`.
/// Empty parts are kept (`"screen,"` is two queries, the second serialising as
/// `not all`), which is why this is not core's split_top_level.
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

/// Serialises one parenthesised media feature, `(name)` or `(name: value)`.
[[nodiscard]] std::string serialize_media_feature_text(std::string_view part) {
    const std::string_view inside = trim(part.substr(1, part.size() - 2), html_whitespace);
    // A nested condition (`((a) and (b))`, `(not (a))`) is a query without a
    // type and is serialised as one (Media Queries 4 §2.2).
    if (inside.starts_with('(') || ascii_istarts_with(inside, "not ") ||
        ascii_istarts_with(inside, "not(")) {
        return "(" + serialize_media_query_text(inside) + ")";
    }
    const std::size_t colon = inside.find(':');
    // Feature names are lowercased, except a custom property's: a
    // <dashed-ident> is case-sensitive, so `(--FOO: bar)` stays as written
    // (WPT mq-unknown-feature-custom-property).
    const auto name = [](std::string_view text) {
        std::string collapsed = collapse_whitespace(text, html_whitespace);
        return collapsed.starts_with("--") ? collapsed : ascii_lower_copy(collapsed);
    };
    if (colon == std::string_view::npos) { return "(" + name(inside) + ")"; }
    // The name is case-folded and the value is not: `(Color)` and `(color)` are
    // one feature, but a value may be a string, a `url()` or a number with a
    // unit, none of which fold. A `<ratio>` is written with its slash spaced:
    // `1/3` is `1 / 3` (Media Queries 4 §2.4).
    std::string value = collapse_whitespace(inside.substr(colon + 1), html_whitespace);
    if (const std::size_t slash = value.find('/');
        slash != std::string::npos &&
        value.find_first_not_of("0123456789. /") == std::string::npos) {
        std::string spaced = std::string{trim(std::string_view{value}.substr(0, slash), " ")};
        spaced += " / ";
        spaced += trim(std::string_view{value}.substr(slash + 1), " ");
        value = std::move(spaced);
    }
    return "(" + name(inside.substr(0, colon)) + ": " + value + ")";
}

} // namespace

namespace detail {

/// Serialises one media query. An unparseable query becomes `not all`, which
/// Media Queries defines as the meaning of a query a browser does not
/// understand: never true, and never an error. Empty text gives an empty
/// string.
[[nodiscard]] std::string serialize_media_query_text(std::string_view text) {
    static constexpr std::string_view not_all = "not all";
    // The parts: `not`/`only`, a type, and parenthesised features joined by
    // `and`. A feature is atomic (its parentheses may contain spaces), which is
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
            // End of input closes an open block (CSS Syntax 3 §5.4.9): `(color`
            // is `(color)` (WPT css/mediaqueries/match-media-parsing.html).
            parts.push_back(std::string{text.substr(start, at - start)} + (closed ? "" : ")"));
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
        // A prefix needs something to apply to: `@media not` is not a query,
        // and `only` is valid only in front of a type.
        if (i >= parts.size() || (prefix == "only " && parts[i].front() == '(')) {
            return std::string{not_all};
        }
    }
    std::string type;
    if (i < parts.size() && parts[i].front() != '(') {
        // A media type must be an identifier, so `@media 42` and `@media .x`
        // are invalid queries, while `speech` and `projection` are valid types
        // that this engine merely does not model.
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
        // Nor is it one of the five words the grammar reserves (Media
        // Queries 4 §3): `@media not or` is malformed, not an unknown type.
        if (!ident || ascii_iequals_any(word, {"and", "not", "only", "or", "layer"})) {
            return std::string{not_all};
        }
        type = ascii_lower_copy(word);
        ++i;
    }
    std::vector<std::string> features;
    bool first = true;
    std::string joiner;
    while (i < parts.size()) {
        // Everything after the media type, and everything after the first
        // feature, is joined to what precedes it by `and`, or by `or` in a
        // typeless condition, never a mix. `not (color)` has neither and is a
        // query with one feature and no type.
        if (!first || !type.empty()) {
            const bool is_and = ascii_iequals(parts[i], "and");
            const bool is_or = ascii_iequals(parts[i], "or") && type.empty();
            if (!is_and && !is_or) { return std::string{not_all}; }
            const std::string want = is_and ? " and " : " or ";
            if (!joiner.empty() && joiner != want) { return std::string{not_all}; }
            joiner = want;
            ++i;
            if (i >= parts.size()) { return std::string{not_all}; }
        }
        if (parts[i].front() != '(') { return std::string{not_all}; }
        features.push_back(serialize_media_feature_text(parts[i]));
        ++i;
        first = false;
    }
    if (joiner.empty()) { joiner = " and "; }

    std::string out = prefix;
    // "If the query is `all and <features>`, omit the `all and`", but only for
    // a query with no prefix: `not all and (color)` is false whenever `(color)`
    // is true, and dropping the type would change its meaning.
    const bool omit_all = type == "all" && prefix.empty() && !features.empty();
    if (!type.empty() && !omit_all) { out += type; }
    for (const std::string & feature : features) {
        if (!out.empty() && out.back() != ' ') { out += joiner; }
        out += feature;
    }
    return out;
}

/// Parses a comma-separated media query list into serialised queries; an
/// empty or whitespace-only list gives no queries.
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

/// CSSOM §2: "To serialize a comma-separated list, concatenate all items while
/// separating them by a COMMA followed by a SPACE".
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
