//===- rules.cpp - CSSOM rule objects ---------------------------*- C++ -*-===//
//
// CSSOM rules: each rule's cssText, parsing a sheet's source rule by rule
// through parse_one_rule (the same entry point insertRule uses), the CSS
// Nesting helpers, and the per-type parsing of at-rule preludes and blocks.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

#include <ctbrowser/shell/net/url.hpp>
#include <ctbrowser/style/css/calc.hpp>

#include <charconv>

namespace ctbrowser::shell {

using namespace detail;

namespace {

/// Whether an at-rule's block holds rules. The at-keyword alone decides what a
/// block contains: a list of rules, a list of declarations
/// (at_rule_holds_declarations), or no block. Getting it wrong would report
/// `@page`'s margins as child rules or `@media`'s rules as declarations. An
/// at-rule named in neither function keeps the author's text verbatim.
[[nodiscard]] bool at_rule_holds_rules(std::string_view name) {
    return name == "media" || name == "supports" || name == "container" || name == "keyframes" ||
           name == "-webkit-keyframes" || name == "scope" || name == "starting-style" ||
           name == "layer";
}

/// Whether an at-rule's block holds declarations; see at_rule_holds_rules.
[[nodiscard]] bool at_rule_holds_declarations(std::string_view name) {
    return name == "font-face" || name == "page" || name == "counter-style" || name == "property" ||
           name == "font-palette-values" || name == "view-transition";
}

/// Rewrites an `@supports` condition's `at-rule( <at-keyword-token> )` tests
/// as CSS Conditional 5 serialises them: no whitespace inside, the name escaped
/// as an identifier (`at-rule( @--\31 23)` is `at-rule(@--123)`). The rest of
/// the condition keeps the author's text.
[[nodiscard]] std::string serialize_at_rule_tests(std::string_view prelude) {
    std::string out;
    std::size_t done = 0;
    for (std::size_t at = 0; (at = prelude.find('(', at)) != std::string_view::npos; ++at) {
        if (at < 7 || !ascii_iequals(prelude.substr(at - 7, 7), "at-rule") ||
            (at > 7 && style::css::is_name(prelude[at - 8]))) {
            continue;
        }
        const std::size_t close = scan_to(prelude, at + 1, ")");
        if (close >= prelude.size()) { break; }
        const style::css::token_stream s =
            style::css::tokenize(prelude.substr(at + 1, close - at - 1));
        const style::css::css_token * keyword = nullptr;
        bool one = true;
        for (const style::css::css_token & t : s.tokens) {
            if (t.type == style::css::token_type::whitespace ||
                t.type == style::css::token_type::eof) {
                continue;
            }
            one = one && keyword == nullptr && t.type == style::css::token_type::at_keyword;
            keyword = &t;
        }
        if (!one || keyword == nullptr) { continue; }
        out += prelude.substr(done, at + 1 - done);
        out += "@" + style::css::serialize_identifier(s.value_of(*keyword)) + ")";
        done = close + 1;
        at = close;
    }
    return out + std::string{prelude.substr(done)};
}

/// Whether `text` is one `<keyframe-selector>` (CSS Animations 1 §3, Scroll
/// Animations 1 §4.3): `from`, `to`, a percentage within [0%, 100%], or a
/// `<timeline-range-name>` followed by one. A math function is accepted in
/// place of the percentage, since its value is known only when it computes.
/// `-10%` and `120%` are rejected.
[[nodiscard]] bool keyframe_selector_valid(std::string_view text) {
    using style::css::token_type;
    const style::css::token_stream s = style::css::tokenize(text);
    std::vector<const style::css::css_token *> tokens;
    int depth = 0;
    for (const style::css::css_token & t : s.tokens) {
        if (t.type == token_type::eof) { break; }
        // Only the top level is looked at; a math function is taken whole.
        if (depth == 0 && t.type != token_type::whitespace) { tokens.push_back(&t); }
        if (t.type == token_type::function || t.type == token_type::open_paren) { ++depth; }
        if (t.type == token_type::close_paren && depth > 0) { --depth; }
    }
    const auto percentage = [&](const style::css::css_token & t) {
        if (t.type == token_type::percentage) { return t.number >= 0 && t.number <= 100; }
        return t.type == token_type::function && style::css::may_have_math(s.text_of(t));
    };
    if (tokens.size() == 1) {
        const style::css::css_token & t = *tokens[0];
        return (t.type == token_type::ident && ascii_iequals_any(s.text_of(t), {"from", "to"})) ||
               percentage(t);
    }
    return tokens.size() == 2 && tokens[0]->type == token_type::ident &&
           ascii_iequals_any(s.text_of(*tokens[0]), {"cover", "contain", "entry", "exit",
                                                     "entry-crossing", "exit-crossing"}) &&
           percentage(*tokens[1]);
}

} // namespace

namespace detail {

/// The `at_name` marker of a nested declarations rule (CSS Nesting 1 §4): a
/// run of declarations after a nested rule, in a style rule or in a group
/// nested in one. Such a rule has type 0 and no at-keyword, so this marker,
/// private to these files, identifies it.
const std::string_view nested_declarations_name = "nested-declarations";

/// Whether `rule` is inside a style rule, so that its selectors are nested ones
/// and a run of bare declarations in it belongs to that style rule.
[[nodiscard]] bool nested_in_style(
    const std::vector<std::unique_ptr<dom_bindings::css_rule_record>> & store, std::size_t rule) {
    for (std::size_t up = rule < store.size() ? store[rule]->parent : no_index; up < store.size();
         up = store[up]->parent) {
        if (store[up]->type == style_rule) { return true; }
    }
    return false;
}

/// Drops bare declarations from a group that is not nested in a style rule
/// (CSS Syntax 3 §5.4.4), recursively. This is known only once the group has a
/// parent, so it runs after parsing.
void prune_bare_declarations(std::vector<std::unique_ptr<dom_bindings::css_rule_record>> & store,
                             std::size_t rule) {
    if (rule >= store.size() || store[rule]->type == style_rule) { return; }
    std::vector<std::size_t> & children = store[rule]->children;
    std::erase_if(children, [&](std::size_t child) {
        return child < store.size() && store[child]->at_name == nested_declarations_name;
    });
    for (const std::size_t child : children) { prune_bare_declarations(store, child); }
}

} // namespace detail

// --- The record store -------------------------------------------------------

std::string dom_bindings::rule_css_text(const css_rule_record & rule) const {
    // A nested declarations rule serialises as its block alone.
    if (rule.at_name == nested_declarations_name) {
        return style::css::serialize_declaration_block(rule.declarations);
    }
    // An empty one is skipped among its parent's children (CSSOM "serialize a
    // CSS rule"; WPT nested-declarations-cssom-whitespace).
    const auto empty_nested_declarations = [this](std::size_t child) {
        const css_rule_record & one = *css_rule_store_[child];
        return one.at_name == nested_declarations_name && one.declarations.empty();
    };
    // A prelude and a declaration block. A keyframe's prelude is its keyText and
    // a style rule's is its selector; neither carries an at-keyword.
    if (rule.type == style_rule || rule.type == keyframe_rule) {
        const std::string block = style::css::serialize_declaration_block(rule.declarations);
        if (rule.children.empty()) {
            if (block.empty()) { return rule.selector + " { }"; }
            return rule.selector + " { " + block + " }";
        }
        // With child rules, CSSOM's multi-line form: the declarations on one
        // indented line, then each child on its own line, indented by two
        // spaces on its first line only (WPT css-nesting/cssom.html checks this
        // uneven indentation of a nested `@supports` exactly).
        std::string out = rule.selector + " {\n";
        if (!block.empty()) { out += "  " + block + "\n"; }
        for (const std::size_t child : rule.children) {
            if (child >= css_rule_store_.size() || empty_nested_declarations(child)) { continue; }
            out += "  " + rule_css_text(*css_rule_store_[child]) + "\n";
        }
        return out + "}";
    }
    // An at-rule this engine does not model serialises as the author's text;
    // reconstructing it from an unparsed prelude could misreport it. `verbatim`
    // is empty exactly when the block was parsed, so it tells the two apart.
    if (!rule.verbatim.empty()) { return rule.verbatim; }
    // The two rules with no block (CSSOM 6.4.7 and 6.4.9). Both serialise their
    // URL as a `url()` whatever the author wrote: `@import "a.css"` comes back
    // as `@import url("a.css");` in every engine.
    if (rule.type == import_rule) {
        std::string out = "@import " + serialize_url(rule.selector);
        if (!rule.prelude.empty()) { out += " " + rule.prelude; }
        const std::string media = serialize_media_query_list(rule.media_queries);
        if (!media.empty()) { out += " " + media; }
        return out + ";";
    }
    if (rule.type == namespace_rule) {
        std::string out = "@namespace ";
        if (!rule.selector.empty()) {
            out += style::css::serialize_identifier(rule.selector) + " ";
        }
        return out + serialize_url(rule.prelude) + ";";
    }
    // `@font-feature-values <family>#`, whose block holds feature blocks, each
    // a list of `name: <integer>+`. Serialised on one line, blocks in source
    // order.
    if (rule.type == font_feature_values_rule) {
        std::string out = "@font-feature-values " + rule.prelude + " {";
        std::string type;
        for (const css_rule_record::feature_value & f : rule.features) {
            if (f.type != type) {
                if (!type.empty()) { out += " }"; }
                type = f.type;
                out += " @" + type + " {";
            }
            out += " " + style::css::serialize_identifier(f.name) + ":";
            for (const double n : f.numbers) {
                out += " " + std::to_string(static_cast<long long>(n));
            }
            out += ";";
        }
        if (!type.empty()) { out += " }"; }
        return out + " }";
    }
    // An at-rule whose block holds declarations (`@font-face`, `@page`,
    // `@counter-style`, ...). The prelude is `@page`'s page selector and empty
    // for most of them. CSSOM 6.4.5 puts a space on each side of the block
    // even when it is empty: `@page { }`, not `@page {}`.
    if (at_rule_holds_declarations(rule.at_name)) {
        std::string out = "@" + rule.at_name;
        if (!rule.prelude.empty()) { out += " " + rule.prelude; }
        const std::string block = style::css::serialize_declaration_block(rule.declarations);
        return block.empty() ? out + " { }" : out + " { " + block + " }";
    }
    // A grouping rule uses CSSOM's multi-line serialisation, spelled out in
    // CSSOM 6.4.1 for CSSMediaRule: the at-keyword, a space, the media query
    // list, a space, `{`, a newline, then each child rule indented by two
    // spaces and followed by a newline, then `}`. An empty query list
    // therefore leaves a double space in `@media  {`, which
    // css/cssom/serialize-media-rule.html checks byte for byte.
    std::string out = "@" + rule.at_name;
    if (rule.type == media_rule) {
        out += " " + serialize_media_query_list(rule.media_queries);
    } else if (rule.type == keyframes_rule && !rule.prelude.empty()) {
        // A `<keyframes-name>` is a `<custom-ident>` or a `<string>`, and a
        // CSS-wide keyword, `default` or `none` is not a valid custom-ident, so
        // such a name serialises as a string (`@keyframes "none"`), the only
        // spelling that reads back as that name.
        static constexpr std::string_view not_idents[] = {
            "initial", "inherit", "unset", "revert", "revert-layer", "default", "none"};
        bool reserved = false;
        for (const std::string_view word : not_idents) {
            if (ascii_iequals(rule.prelude, word)) { reserved = true; }
        }
        out += " " + (reserved ? quoted_string(rule.prelude) : rule.prelude);
    } else if (!rule.prelude.empty()) {
        out += " " + rule.prelude;
    }
    out += " {\n";
    for (const std::size_t child : rule.children) {
        if (child >= css_rule_store_.size() || empty_nested_declarations(child)) { continue; }
        // Two spaces before the child's first line only: CSSOM appends the
        // serialised child as it is, so a nested group's own lines keep their
        // indentation and nothing accumulates (WPT at-container-serialization,
        // serialize-group-rules-with-decls).
        out += "  " + rule_css_text(*css_rule_store_[child]) + "\n";
    }
    out += '}';
    return out;
}

/// Parses a sheet's source into CSSOM rules through parse_one_rule(), the entry
/// point insertRule uses, so a rule from a `<style>` and the same rule from
/// insertRule are parsed identically. The cascade's parse_stylesheet is not
/// used: it keeps only qualified rules, @media and @font-face, and drops rules
/// with empty blocks, which would shift every cssRules index
/// (css/cssom/cssom-ruleTypeAndOrder.html).
void dom_bindings::parse_sheet_rules(std::size_t sheet, std::string_view css) {
    if (sheet >= css_sheets_.size()) { return; }
    for (const std::size_t old : css_sheets_[sheet]->rules) { detach_rule(css_rule_store_, old); }
    css_sheets_[sheet]->rules.clear();
    for (const std::string_view span : split_top_level_rules(css)) {
        std::string error;
        const std::size_t at = parse_one_rule(sheet, span, error);
        // A span that is not a rule is dropped and parsing continues (CSS
        // Syntax 3 §5.4 error recovery).
        if (at == no_index) { continue; }
        // "replace() and replaceSync() ... remove any @import rules": a
        // constructed sheet fetches nothing and does not report the rule
        // (WPT CSSStyleSheet-constructable-disallow-import).
        if (css_sheets_[sheet]->constructed && css_rule_store_[at]->type == import_rule) {
            css_rule_store_[at]->sheet = no_index;
            continue;
        }
        prune_bare_declarations(css_rule_store_, at);
        css_sheets_[sheet]->rules.push_back(at);
    }
}

namespace detail {

/// Re-spells the selector of a nested style rule, and of its descendants, once
/// the rule has a parent: `& .b` for what was written `.b`, relative selectors
/// anchored on `&`, and `&` itself kept, as CSS Nesting 1 §2.1 specifies and
/// `cssRules[0].cssText` reads back. The parent list is not needed to spell it,
/// so a placeholder stands in for it.
void nest_rule_selector(std::vector<std::unique_ptr<dom_bindings::css_rule_record>> & store,
                        atom_table & atoms,
                        std::span<const style::css::namespace_declaration> namespaces,
                        std::size_t rule) {
    if (rule >= store.size()) { return; }
    dom_bindings::css_rule_record & made = *store[rule];
    if (made.type == style_rule && nested_in_style(store, rule)) {
        bool bad = false;
        const std::vector<style::css::namespace_declaration> known(namespaces.begin(),
                                                                   namespaces.end());
        const std::vector<style::compiled_selector> placeholder(1);
        const style::css::nesting_context nesting{placeholder, false};
        const style::css::stylesheet parsed =
            style::css::parse_selector_text(made.prelude, atoms, bad, &known, &nesting);
        if (!bad && !parsed.selectors.empty()) {
            made.selector = representable(parsed.selectors)
                                ? serialize_selector_list(parsed.selectors, atoms, known)
                                : collapse_whitespace(made.prelude, html_whitespace);
        }
    }
    for (const std::size_t child : made.children) {
        nest_rule_selector(store, atoms, namespaces, child);
    }
}

} // namespace detail

/// The `@namespace` rules a sheet holds, in the form the selector parser takes,
/// so `ns|div` with an undeclared `ns` is the SyntaxError CSSOM 6.3.3 asks for
/// rather than a prefix taken on trust (WPT at-namespace.html).
std::vector<style::css::namespace_declaration> dom_bindings::sheet_namespaces(
    std::size_t sheet) const {
    std::vector<style::css::namespace_declaration> out;
    if (sheet >= css_sheets_.size()) { return out; }
    for (const std::size_t rule : css_sheets_[sheet]->rules) {
        if (rule >= css_rule_store_.size() || css_rule_store_[rule]->type != namespace_rule) {
            continue;
        }
        out.push_back({css_rule_store_[rule]->selector, css_rule_store_[rule]->prelude});
    }
    return out;
}

/// Creates the sheet an `@import` rule names, loads its text from the asset
/// registry and parses it; returns the new sheet's index. An import cycle or a
/// chain deeper than 16 gives an empty sheet.
std::size_t dom_bindings::load_imported_sheet(std::size_t rule, std::string_view href) {
    if (rule >= css_rule_store_.size()) { return no_index; }
    const std::size_t parent = css_rule_store_[rule]->sheet;
    if (parent >= css_sheets_.size()) { return no_index; }
    css_sheets_.push_back(std::make_unique<css_sheet_record>());
    const std::size_t at = css_sheets_.size() - 1;
    css_sheet_record & made = *css_sheets_[at];
    made.owner_rule = rule;
    made.href = resolve_sheet_href(css_sheets_[parent]->href, href);
    made.origin_clean = !parse_absolute(made.href).valid ||
                        location_parts(made.href).origin == location_parts(location_href_).origin;
    // A cycle (`a.css` importing `b.css` importing `a.css`) yields an empty
    // sheet where it closes, as every engine does; so does a chain more than
    // 16 imports deep.
    std::size_t depth = 0;
    for (std::size_t up = parent; up < css_sheets_.size() && depth < 16; ++depth) {
        if (css_sheets_[up]->href == made.href) { return at; }
        const std::size_t via = css_sheets_[up]->owner_rule;
        if (via >= css_rule_store_.size()) { break; }
        up = css_rule_store_[via]->sheet;
    }
    if (depth >= 16 || assets_ == nullptr) { return at; }
    const std::vector<std::byte> bytes = assets_->load(made.href);
    made.source.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    parse_sheet_rules(at, css_sheets_[at]->source);
    return at;
}

namespace {

/// How deep inside style rules the rule being parsed is. A nested style rule's
/// selector may be relative (`> .bar`) and may use `&`, where a top-level one
/// may not (CSS Nesting 1 §2.1). thread_local because parse_one_rule re-enters
/// itself for every nested rule and nothing else shares the counter.
thread_local int nested_style_depth = 0;

} // namespace

/// Parses one rule's text into a new record in the rule store and returns its
/// index, or no_index with `error` set ("SyntaxError") when the text is not
/// exactly one valid rule. Nested rules are parsed recursively.
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

    // Parses a block's declarations; see parse_declarations_into, which a
    // `cssText` write shares.
    const auto collect_into = [](css_rule_record & into, std::string_view body) {
        parse_declarations_into(into, body);
    };

    // Parses one keyframe. `0%, to { opacity: 0 }` is a qualified rule whose
    // prelude is a `<keyframe-selector>#`, not a selector, so
    // parse_selector_text would refuse it and parsing a `@keyframes` block as
    // ordinary rules would empty it. CSSOM 6.4.10's keyText is that list,
    // comma-separated; `from` and `to` keep the author's spelling.
    const auto make_keyframe = [this, sheet, &collect_into](std::string_view span) -> std::size_t {
        const std::string_view one = trim(span, html_whitespace);
        const std::size_t brace = brace_at(one);
        const std::size_t shut = brace == std::string_view::npos ? brace : block_end(one, brace);
        if (brace == std::string_view::npos || shut == std::string_view::npos) { return no_index; }
        for (const std::string_view part : split_on_commas(one.substr(0, brace))) {
            if (!keyframe_selector_valid(part)) { return no_index; }
        }
        css_rule_store_.push_back(std::make_unique<css_rule_record>());
        css_rule_record & frame = *css_rule_store_.back();
        frame.sheet = sheet;
        frame.type = keyframe_rule;
        for (const std::string_view part : split_on_commas(one.substr(0, brace))) {
            if (!frame.selector.empty()) { frame.selector += ", "; }
            frame.selector += collapse_whitespace(part, html_whitespace);
        }
        collect_into(frame, one.substr(brace + 1, shut - brace - 1));
        return css_rule_store_.size() - 1;
    };

    // Parses a block's contents, declarations and rules mixed (CSS Syntax 3
    // §5.4.4). A run of declarations up to the first nested rule belongs to
    // the rule itself when `own_first`; every later run, and every run in a
    // group, becomes a nested declarations rule (CSS Nesting 1 §4), a child in
    // its source position. A `{` before the next `;` opens a nested rule,
    // unless the run is a custom property, whose value may hold a block; an
    // at-keyword opens a nested at-rule. The store is addressed by index
    // throughout because every child parse may reallocate it.
    const auto parse_block_contents = [this, sheet](std::size_t owner, std::string_view body,
                                                    bool own_first) {
        std::string run;
        bool first = own_first;
        const auto flush = [&] {
            if (trim(run, html_whitespace).empty()) {
                run.clear();
                return;
            }
            if (first) {
                parse_declarations_into(*css_rule_store_[owner], run);
            } else {
                css_rule_store_.push_back(std::make_unique<css_rule_record>());
                const std::size_t child = css_rule_store_.size() - 1;
                css_rule_store_[child]->type = 0;
                css_rule_store_[child]->at_name = std::string{nested_declarations_name};
                css_rule_store_[child]->sheet = sheet;
                css_rule_store_[child]->parent = owner;
                parse_declarations_into(*css_rule_store_[child], run);
                css_rule_store_[owner]->children.push_back(child);
            }
            first = false;
            run.clear();
        };
        const auto adopt = [&](std::string_view span) {
            flush();
            first = false;
            std::string ignored;
            const bool in_style = css_rule_store_[owner]->type == style_rule ||
                                  nested_in_style(css_rule_store_, owner);
            if (in_style) { ++nested_style_depth; }
            const std::size_t child = parse_one_rule(sheet, span, ignored);
            if (in_style) { --nested_style_depth; }
            if (child == no_index) { return; }
            css_rule_store_[child]->parent = owner;
            css_rule_store_[owner]->children.push_back(child);
            const std::vector<style::css::namespace_declaration> namespaces =
                sheet_namespaces(sheet);
            nest_rule_selector(css_rule_store_, *atoms_, namespaces, child);
        };
        std::size_t at = 0;
        while (at < body.size()) {
            if (html_whitespace.find(body[at]) != std::string_view::npos || body[at] == ';') {
                ++at;
                continue;
            }
            if (body.compare(at, 2, "/*") == 0) {
                const std::size_t close = body.find("*/", at + 2);
                at = close == std::string_view::npos ? body.size() : close + 2;
                continue;
            }
            if (body[at] == '@') {
                std::size_t end = scan_to(body, at, "{;");
                if (end < body.size() && body[end] == '{') { end = scan_to(body, end + 1, "}"); }
                end = std::min(end + 1, body.size());
                adopt(trim(body.substr(at, end - at), html_whitespace));
                at = end;
                continue;
            }
            const std::size_t stop = scan_to(body, at, ";{");
            if (stop >= body.size() || body[stop] == ';') {
                run += body.substr(at, stop - at);
                run += ';';
                at = stop + 1;
                continue;
            }
            const std::string_view head = trim(body.substr(at, stop - at), html_whitespace);
            const std::size_t colon = scan_to(head, 0, ":");
            if (head.starts_with("--") && colon < head.size()) {
                // A custom property whose value holds a block runs to the `;`.
                std::size_t end = scan_to(body, stop + 1, "}");
                end = scan_to(body, std::min(end + 1, body.size()), ";");
                run += body.substr(at, end - at);
                run += ';';
                at = std::min(end + 1, body.size());
                continue;
            }
            std::size_t end = scan_to(body, stop + 1, "}");
            end = std::min(end + 1, body.size());
            adopt(trim(body.substr(at, end - at), html_whitespace));
            at = end;
        }
        flush();
    };

    if (trimmed.front() == '@') {
        // An at-rule. The name decides the type, which `instanceof CSSMediaRule`
        // checks. Its block is parsed as a list of rules, a list of
        // declarations, or not at all, in which case `cssText` is the author's
        // text for a rule this engine does not model.
        const std::size_t name_end = trimmed.find_first_of(std::string{html_whitespace} + "{;", 1);
        const std::string_view name =
            trimmed.substr(1, (name_end == std::string_view::npos ? trimmed.size() : name_end) - 1);
        made.at_name = ascii_lower_copy(name);
        // `@charset` is not a rule (CSS Syntax 3 §4 consumes it before the rule
        // list is parsed), so a sheet beginning with one has no rule for it,
        // and `insertRule("@charset ...")` is a SyntaxError.
        if (made.at_name == "charset") {
            css_rule_store_.pop_back();
            error = "SyntaxError";
            return no_index;
        }
        made.verbatim = std::string{trimmed};
        {
            // The prelude is what stands between the at-keyword and the block
            // (or the `;` that ends a statement at-rule): `@media` -> `all and
            // (min-width: 10px)`. A `{` inside a bracket is no block:
            // `@supports ({}) {}` has the prelude `({})`.
            const std::size_t after =
                name_end == std::string_view::npos ? trimmed.size() : name_end;
            const std::size_t body = scan_to(trimmed, after, "{;");
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
            made.prelude = serialize_at_rule_tests(made.prelude);
        } else if (ascii_iequals(name, "page")) {
            made.type = page_rule;
        } else if (ascii_iequals(name, "counter-style")) {
            made.type = counter_style_rule;
        } else if (ascii_iequals(name, "keyframes") || ascii_iequals(name, "-webkit-keyframes")) {
            made.type = keyframes_rule;
        } else if (ascii_iequals(name, "font-feature-values")) {
            made.type = font_feature_values_rule;
        } else {
            made.type = 0;
        }

        // The block, if there is one. `@import` and `@namespace` end at a `;`
        // and have none.
        const std::size_t open = brace_at(trimmed);
        const std::size_t close = open == std::string_view::npos ? open : block_end(trimmed, open);
        const std::string_view body =
            open == std::string_view::npos || close == std::string_view::npos
                ? std::string_view{}
                : trimmed.substr(open + 1, close - open - 1);
        // Exactly one rule (CSSOM 6.4.3 "parse a rule"): a block at-rule with
        // no block (`@media bad syntax;`) is invalid, and so is text after the
        // block (WPT conditional-CSSGroupingRule).
        const bool trailing = open != std::string_view::npos && close != std::string_view::npos &&
                              !trim(trimmed.substr(close + 1), html_whitespace).empty();
        const bool blockless =
            open == std::string_view::npos && made.at_name != "layer" &&
            (at_rule_holds_rules(made.at_name) || at_rule_holds_declarations(made.at_name));
        if (trailing || blockless) {
            css_rule_store_.pop_back();
            error = "SyntaxError";
            return no_index;
        }

        if (made.type == media_rule) {
            // The query list is normalised from the author's text, not from
            // the compiled media query, so it serialises exactly.
            made.media_queries = parse_media_query_list(made.prelude);
            made.prelude.clear();
        } else if (made.type == import_rule) {
            // `<url> <layer>? <supports()>? <media-query-list>?`. The URL and
            // the media list get fields of their own because the interface has
            // a reader for each; `layer` and `supports()` stay in `prelude` in
            // the author's order, the only order CSS Cascade 5 allows.
            const std::string written = made.prelude;
            std::size_t after = 0;
            const std::string href = url_value(next_component(written, after));
            std::string extra;
            // A `layer()` that names no <layer-name> (`layer()`, `layer(A B)`)
            // or an empty `supports()` is not a condition: it starts the media
            // query list, as a `<general-enclosed>` that never matches (CSS
            // Cascade 5 §5.1; WPT layer-import-parsing).
            const auto well_formed = [](std::string_view part) {
                if (ascii_iequals(part, "layer")) { return true; }
                const std::size_t open = part.find('(');
                if (open == std::string_view::npos || part.back() != ')') { return false; }
                const std::string_view inner =
                    trim(part.substr(open + 1, part.size() - open - 2), html_whitespace);
                if (inner.empty()) { return false; }
                if (!ascii_iequals(part.substr(0, open), "layer")) { return true; }
                for (const std::string_view piece : split_top_level(inner, ".")) {
                    if (piece.empty() || (piece.front() >= '0' && piece.front() <= '9')) {
                        return false;
                    }
                    for (const char c : piece) {
                        if (!style::css::is_name(c)) { return false; }
                    }
                }
                return inner.front() != '.' && inner.back() != '.' &&
                       inner.find("..") == std::string_view::npos;
            };
            for (std::size_t probe = after;;) {
                const std::string_view part = next_component(written, probe);
                if (part.empty()) { break; }
                const bool layer =
                    ascii_iequals(part, "layer") || ascii_iequals(part.substr(0, 6), "layer(");
                // `scope()` (CSS Cascade 6 §2.2) may be empty: the implicit scope.
                const bool scope = ascii_iequals(part.substr(0, 6), "scope(") && part.back() == ')';
                if (!layer && !scope && !ascii_iequals(part.substr(0, 9), "supports(")) { break; }
                if (!scope && !well_formed(part)) { break; }
                if (!extra.empty()) { extra += ' '; }
                extra += collapse_whitespace(part, html_whitespace);
                after = probe;
            }
            if (!href.empty()) {
                made.selector = href;
                made.media_queries = parse_media_query_list(written.substr(after));
                made.prelude = extra;
                made.verbatim.clear();
                // Load the imported sheet now, except in a constructed sheet:
                // CSSOM's replace() and replaceSync() parse an `@import` and
                // then drop it, and insertRule refuses one outright.
                if (sheet < css_sheets_.size() && !css_sheets_[sheet]->constructed) {
                    made.imported_sheet = load_imported_sheet(at, href);
                }
            }
        } else if (made.type == namespace_rule) {
            // `[<prefix>]? <url>`. A default namespace has no prefix, which is
            // not the same as having an empty one.
            const std::string written = made.prelude;
            std::size_t after = 0;
            const std::string_view first = next_component(written, after);
            const std::string_view second = next_component(written, after);
            const std::string uri = url_value(second.empty() ? first : second);
            if (!uri.empty()) {
                // The prefix is stored decoded, as the selector parser decodes
                // the one in `x\*|test`, so the two match (WPT
                // selectorSerialize.html, "escaped character (*) in element
                // prefix"). cssText re-escapes it with serialize_identifier.
                std::string prefix;
                if (!second.empty()) {
                    const style::css::token_stream tokens = style::css::tokenize(first);
                    prefix = !tokens.tokens.empty() &&
                                     tokens.tokens.front().type == style::css::token_type::ident
                                 ? std::string{tokens.value_of(tokens.tokens.front())}
                                 : std::string{first};
                }
                made.selector = std::move(prefix);
                made.prelude = uri;
                made.verbatim.clear();
            }
        } else if (made.type == page_rule) {
            // A `<page-selector-list>` the grammar refuses makes the rule
            // invalid (CSS Paged Media 3 §3; WPT page-rules-001).
            bool ok = false;
            made.prelude = serialize_page_selector(made.prelude, ok);
            if (!ok) {
                css_rule_store_.pop_back();
                error = "SyntaxError";
                return no_index;
            }
        } else if (made.type == font_feature_values_rule && open != std::string_view::npos &&
                   close != std::string_view::npos) {
            // CSS Fonts 4 §8.9: `<family-name>#`, then feature blocks, each
            // `@<feature-type> { <custom-ident>: <integer>+; ... }`. A block
            // whose name is not one of the seven feature types, and an entry
            // whose value is not integers, are dropped as the parser drops them.
            made.prelude = style::css::serialize_font_family(
                collapse_whitespace(made.prelude, html_whitespace));
            static constexpr std::string_view feature_types[] = {
                "stylistic", "historical-forms", "styleset",  "character-variant",
                "swash",     "ornaments",        "annotation"};
            for (const std::string_view span : split_top_level_rules(body)) {
                const std::string_view one = trim(span, html_whitespace);
                const std::size_t brace = brace_at(one);
                const std::size_t shut =
                    brace == std::string_view::npos ? brace : block_end(one, brace);
                if (one.empty() || one.front() != '@' || brace == std::string_view::npos ||
                    shut == std::string_view::npos) {
                    continue;
                }
                const std::string type =
                    ascii_lower_copy(trim(one.substr(1, brace - 1), html_whitespace));
                if (std::find(std::begin(feature_types), std::end(feature_types), type) ==
                    std::end(feature_types)) {
                    continue;
                }
                const style::css::stylesheet parsed = style::css::parse_declaration_list(
                    one.substr(brace + 1, shut - brace - 1), *atoms_);
                for (const style::css::raw_declaration & d : parsed.declarations) {
                    css_rule_record::feature_value entry;
                    entry.type = type;
                    entry.name = std::string{atoms_->text(d.property)};
                    bool ok = true;
                    for (const std::string_view part :
                         split_top_level(parsed.text_of(d), " \t\n\r\f")) {
                        // Only a plain number token: `random(1, 3)` is a valid
                        // `order` but not a feature value (WPT
                        // random-in-descriptors). `order` is used because its
                        // grammar is an <integer>.
                        const style::css::value_check n = check_declaration("order", part, false);
                        double number = 0.0;
                        const auto [end, ec] = std::from_chars(
                            n.serialized.data(), n.serialized.data() + n.serialized.size(), number);
                        if (!n.valid || ec != std::errc{} ||
                            end != n.serialized.data() + n.serialized.size()) {
                            ok = false;
                            break;
                        }
                        entry.numbers.push_back(number);
                    }
                    if (!ok || entry.numbers.empty()) { continue; }
                    std::erase_if(made.features, [&](const css_rule_record::feature_value & f) {
                        return f.type == entry.type && f.name == entry.name;
                    });
                    made.features.push_back(std::move(entry));
                }
            }
            made.verbatim.clear();
        }
        if (made.type == keyframes_rule) {
            // `<keyframes-name>` = <custom-ident> | <string> (CSS Animations 1
            // §3): one identifier that is not `none`, `default` or a CSS-wide
            // keyword, or a non-empty string. `@keyframes 12`, `one two`,
            // `foo.bar` and `""` are invalid (WPT keyframes-name-invalid).
            {
                const style::css::token_stream name = style::css::tokenize(made.prelude);
                std::vector<const style::css::css_token *> tokens;
                for (const style::css::css_token & t : name.tokens) {
                    if (t.type != style::css::token_type::whitespace &&
                        t.type != style::css::token_type::eof) {
                        tokens.push_back(&t);
                    }
                }
                const bool ok = tokens.size() == 1 &&
                                ((tokens[0]->type == style::css::token_type::ident &&
                                  !ascii_iequals_any(name.text_of(*tokens[0]),
                                                     {"none", "default", "initial", "inherit",
                                                      "unset", "revert", "revert-layer"})) ||
                                 (tokens[0]->type == style::css::token_type::string &&
                                  !name.value_of(*tokens[0]).empty()));
                if (!ok) {
                    css_rule_store_.pop_back();
                    error = "SyntaxError";
                    return no_index;
                }
            }
            // Each keyframe is parsed by make_keyframe, not the sheet parser,
            // which would drop a keyframe with an empty block. The record store
            // holds `unique_ptr`s, so `made` stays valid as the store grows.
            for (const std::string_view span : split_top_level_rules(body)) {
                const std::size_t child = make_keyframe(span);
                if (child == no_index) { continue; }
                css_rule_store_[child]->parent = at;
                made.children.push_back(child);
            }
            made.verbatim.clear();
        } else if (made.at_name == "layer") {
            // `@layer a, b.c;` names its layers; `@layer a { }` names one or
            // none. A `<layer-name>` is identifiers joined by `.` with nothing
            // between (CSS Cascade 5 §6.4.1); anything else makes the rule
            // invalid.
            std::vector<std::string> names;
            const bool anonymous = trim(made.prelude, html_whitespace).empty();
            for (const std::string_view part :
                 anonymous ? std::vector<std::string_view>{} : split_on_commas(made.prelude)) {
                const std::string_view name = trim(part, html_whitespace);
                bool ok = !name.empty();
                for (const std::string_view piece : split_top_level(name, ".")) {
                    ok = ok && !piece.empty() &&
                         piece.find_first_of(std::string{html_whitespace} + ".()[]{},;:\"'") ==
                             std::string_view::npos;
                }
                if (!ok) {
                    css_rule_store_.pop_back();
                    error = "SyntaxError";
                    return no_index;
                }
                names.emplace_back(name);
            }
            const bool statement = open == std::string_view::npos;
            if (statement ? names.empty() : names.size() > 1) {
                css_rule_store_.pop_back();
                error = "SyntaxError";
                return no_index;
            }
            made.prelude.clear();
            for (const std::string & name : names) {
                if (!made.prelude.empty()) { made.prelude += ", "; }
                made.prelude += name;
            }
            if (statement) {
                made.at_name = "layer-statement";
                made.verbatim = "@layer " + made.prelude + ";";
            } else {
                parse_block_contents(at, body, false);
                made.verbatim.clear();
            }
        } else if (at_rule_holds_rules(made.at_name)) {
            // `@starting-style` takes no prelude (CSS Transitions 2 §3.1):
            // `@starting-style div { }` is invalid (WPT starting-style-parsing).
            if (made.at_name == "starting-style" && !trim(made.prelude, html_whitespace).empty()) {
                css_rule_store_.pop_back();
                error = "SyntaxError";
                return no_index;
            }
            if (made.at_name == "scope") {
                // `[(<scope-start>)]? [to (<scope-end>)]?` (CSS Cascade 6 §3):
                // each a selector list that parses and names no pseudo-element,
                // the end one relative to the root; anything else in the
                // prelude makes the rule invalid. Serialised as written, with
                // whitespace collapsed and ` to ` spaced.
                const std::string_view text = trim(made.prelude, html_whitespace);
                std::size_t k = 0;
                std::string start;
                std::string end;
                bool ok = true;
                const auto group = [&](bool relative, std::string & into) {
                    if (k >= text.size() || text[k] != '(') { return false; }
                    const std::size_t close = scan_to(text, k + 1, ")");
                    if (close >= text.size()) { return false; }
                    into = collapse_whitespace(text.substr(k + 1, close - k - 1), html_whitespace);
                    k = close + 1;
                    bool bad = false;
                    const std::vector<style::compiled_selector> placeholder;
                    const style::css::nesting_context nesting{placeholder, true};
                    const style::css::stylesheet parsed = style::css::parse_selector_text(
                        into, *atoms_, bad, nullptr, relative ? &nesting : nullptr);
                    if (bad || into.empty()) { return false; }
                    for (const style::compiled_selector & s : parsed.selectors) {
                        for (const style::compound & c : s.parts) {
                            if (c.pseudo_element) { return false; }
                        }
                    }
                    return true;
                };
                if (k < text.size() && text[k] == '(') { ok = group(false, start); }
                while (ok && k < text.size() &&
                       html_whitespace.find(text[k]) != std::string_view::npos) {
                    ++k;
                }
                if (ok && k < text.size()) {
                    ok = text.compare(k, 2, "to") == 0;
                    k += 2;
                    while (ok && k < text.size() &&
                           html_whitespace.find(text[k]) != std::string_view::npos) {
                        ++k;
                    }
                    ok = ok && group(true, end) && trim(text.substr(k), html_whitespace).empty();
                }
                if (!ok) {
                    css_rule_store_.pop_back();
                    error = "SyntaxError";
                    return no_index;
                }
                made.prelude.clear();
                if (!start.empty()) { made.prelude = "(" + start + ")"; }
                if (!end.empty()) {
                    made.prelude += made.prelude.empty() ? "to (" : " to (";
                    made.prelude += end + ")";
                }
            } else if (made.at_name == "container") {
                // Parsed with the cascade's own prelude parser, so a rule the
                // cascade would drop is not in the object model either (WPT
                // at-container-parsing).
                const auto conditions = style::css::parse_container_conditions(made.prelude);
                if (!conditions) {
                    css_rule_store_.pop_back();
                    error = "SyntaxError";
                    return no_index;
                }
                made.prelude = style::css::serialize_container_conditions(*conditions);
            }
            parse_block_contents(at, body, false);
            // The rule is fully modelled now, so the author's text is dropped
            // and `cssText` serialises the group and its children.
            made.verbatim.clear();
        } else if (at_rule_holds_declarations(made.at_name) && open != std::string_view::npos &&
                   close != std::string_view::npos) {
            collect_into(made, body);
            made.verbatim.clear();
        }
        return at;
    }

    // A qualified rule is split at the brace, not run through the sheet
    // parser, because consume_qualified_rule drops a rule with an empty block
    // and `div { }` is valid (addRule() with no arguments produces
    // `undefined { }`). The selector goes through parse_selector_text (as
    // querySelector does) and the block through parse_block_contents.
    const std::size_t open = brace_at(trimmed);
    std::size_t close = open == std::string_view::npos ? open : block_end(trimmed, open);
    // End of input closes an open block (CSS Syntax 3 §5.4.9): a sheet cut off
    // inside `var(--x` still holds its last rule (WPT variable-reference).
    if (open != std::string_view::npos && close == std::string_view::npos) {
        close = trimmed.size();
    }
    if (open == std::string_view::npos ||
        (close + 1 < trimmed.size() && !trim(trimmed.substr(close + 1), html_whitespace).empty())) {
        // No block, or text after the block: CSSOM requires exactly one rule.
        css_rule_store_.pop_back();
        error = "SyntaxError";
        return no_index;
    }
    bool bad = false;
    const std::string_view prelude = trim(trimmed.substr(0, open), html_whitespace);
    // A prelude that reads as a custom property declaration (`--x:hover { }`)
    // is not a rule (CSS Syntax 3 §5.4.2).
    if (prelude.starts_with("--") && scan_to(prelude, 0, ":") < prelude.size()) {
        css_rule_store_.pop_back();
        error = "SyntaxError";
        return no_index;
    }
    const std::vector<style::css::namespace_declaration> namespaces = sheet_namespaces(sheet);
    // Inside a style rule, parse against a stand-in parent: the selector may be
    // relative, which only a nesting context accepts.
    const std::vector<style::compiled_selector> placeholder(1);
    const style::css::nesting_context nesting{placeholder, false};
    const style::css::stylesheet selectors = style::css::parse_selector_text(
        prelude, *atoms_, bad, &namespaces, nested_style_depth > 0 ? &nesting : nullptr);
    if (bad || selectors.selectors.empty()) {
        css_rule_store_.pop_back();
        error = "SyntaxError";
        return no_index;
    }
    made.type = style_rule;
    // The canonical serialisation when the compiled form holds the whole
    // selector, and the author's text when it does not (see representable).
    // Whitespace is collapsed either way, so `span  div  ` is `span div`.
    made.selector = representable(selectors.selectors)
                        ? serialize_selector_list(selectors.selectors, *atoms_, namespaces)
                        : collapse_whitespace(prelude, html_whitespace);
    // The author's selector is kept: a rule that turns out to be nested, once
    // it has a parent, is re-spelled from it with `&` (nest_rule_selector).
    made.prelude = collapse_whitespace(prelude, html_whitespace);
    parse_block_contents(at, trimmed.substr(open + 1, close - open - 1), true);
    return at;
}

} // namespace ctbrowser::shell
