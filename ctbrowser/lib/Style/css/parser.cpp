#include <ctbrowser/style/css/parser.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/media.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <ctbrowser/style/css/selector.hpp>

// CSS Syntax Level 3 §5, over the §4 tokens.
//
// The shape is the spec's: consume a list of rules, and within a rule consume a
// list of declarations, and within either consume a component value. Doing it in
// that order is what makes the error recovery fall out rather than being invented:
// a `;` inside `url(...)` is a child of a component value and is never seen by the
// declaration splitter, and a `}` inside a string is a token rather than a brace.
//
// A BLOCK'S CONTENTS ARE DECLARATIONS AND RULES, MIXED (CSS Syntax 3 §5.4.4,
// the nesting revision): a style rule's block may hold nested style rules and
// nested conditional groups, and a conditional group's block may hold bare
// declarations that belong to the enclosing style rule. So every block goes
// through one walk, `consume_block_contents`, and what it may hold is decided
// by the context it runs in - a style rule's selectors, an `@scope`, or
// nothing at all at the top level.
//
// THE CONDITIONAL GROUPS: `@media` records a condition the engine evaluates
// against the environment as it changes; `@supports` is decided HERE, once,
// since what a property table accepts does not change with the window; and
// `@container` is deferred to the engine, which is the only thing that can ask
// layout for the container's size. `@layer` names a cascade layer and `@scope`
// a scoping root, both of which the cascade reads back off the rule.

namespace ctbrowser::style::css {
namespace {

// The at-rules this recognises. Everything else with a block has its block
// skipped, and everything else without one is consumed to the `;`.
enum class at_kind {
    media,
    supports,
    layer,
    scope,
    container,
    media_like,
    font_face,
    property,
    function,
    keyframes,
    statement,
    skip
};

[[nodiscard]] at_kind at_kind_of(std::string_view name) {
    // Vendor prefixes are stripped before the comparison, so `@-webkit-keyframes`
    // is the same at-rule as `@keyframes` - which matters because a sheet that
    // writes both would otherwise have the prefixed one skipped by a different
    // branch than the unprefixed one.
    for (const std::string_view prefix : {"-webkit-", "-moz-", "-ms-", "-o-"}) {
        if (name.size() > prefix.size() && ascii_istarts_with(name, prefix)) {
            name.remove_prefix(prefix.size());
            break;
        }
    }
    if (ascii_iequals(name, "media")) { return at_kind::media; }
    if (ascii_iequals(name, "supports")) { return at_kind::supports; }
    if (ascii_iequals(name, "layer")) { return at_kind::layer; }
    if (ascii_iequals(name, "scope")) { return at_kind::scope; }
    if (ascii_iequals(name, "container")) { return at_kind::container; }
    if (ascii_iequals(name, "document")) {
        // A conditional group whose contents are rules and whose condition
        // this engine cannot read: its rules apply.
        return at_kind::media_like;
    }
    if (ascii_iequals(name, "font-face")) { return at_kind::font_face; }
    if (ascii_iequals(name, "property")) { return at_kind::property; }
    if (ascii_iequals(name, "function")) { return at_kind::function; }
    if (ascii_iequals(name, "keyframes")) { return at_kind::keyframes; }
    if (ascii_iequals(name, "import") || ascii_iequals(name, "charset") ||
        ascii_iequals(name, "namespace")) {
        return at_kind::statement;
    }
    return at_kind::skip;
}

class parser {
public:
    parser(std::string_view css, atom_table & atoms) : atoms_(&atoms) {
        token_stream stream = tokenize(css);
        sheet_.pool = std::move(stream.pool);
        sheet_.tokens = std::move(stream.tokens);
        sheet_.source_length = stream.source_length;
    }

    [[nodiscard]] stylesheet take_stylesheet() {
        consume_rule_list(/*top_level=*/true);
        return std::move(sheet_);
    }

    // A selector list on its own: no braces, no declarations. `invalid` comes back
    // set when the text is not a selector at all.
    [[nodiscard]] stylesheet take_selector_list(bool & invalid,
                                                const std::vector<namespace_declaration> * ns,
                                                const nesting_context * nesting) {
        if (ns != nullptr) {
            sheet_.namespaces = *ns;
        } else {
            sheet_.prefixes_checked = false;
        }
        std::vector<component_value> run;
        while (!at_eof()) { run.push_back(consume_component_value()); }
        (void)parse_selector_list(sheet_, span_of(run), *atoms_, &invalid, nesting);
        return std::move(sheet_);
    }

    // A style attribute: declarations, no braces, no selector.
    [[nodiscard]] stylesheet take_declaration_list() {
        std::vector<component_value> run;
        while (!at_eof()) { run.push_back(consume_component_value()); }
        emit_declarations(run);
        return std::move(sheet_);
    }

private:
    [[nodiscard]] const css_token & here() const { return sheet_.tokens[at_]; }

    // The prefix is an ident and the URL a string or a `url()` - either token
    // shape, since `url("x")` is a function around a string. Anything else is
    // not a namespace rule and declares nothing.
    void record_namespace(std::span<const component_value> prelude) {
        namespace_declaration made;
        bool have_uri = false;
        for (const component_value & v : prelude) {
            if (v.kind == cv_kind::function) {
                const auto inner = sheet_.children_of(v);
                for (const component_value & arg : inner) {
                    if (arg.kind != cv_kind::token) { continue; }
                    const css_token & t = sheet_.tokens[arg.token];
                    if (t.type != token_type::string) { continue; }
                    const std::string_view quoted = sheet_.text_of(t);
                    made.uri = std::string{quoted.substr(1, quoted.size() - 2)};
                    have_uri = true;
                }
                continue;
            }
            if (v.kind != cv_kind::token) { return; }
            const css_token & t = sheet_.tokens[v.token];
            if (t.type == token_type::whitespace) { continue; }
            if (t.type == token_type::ident && !have_uri && made.prefix.empty()) {
                made.prefix = std::string{sheet_.text_of(t)};
            } else if (t.type == token_type::url) {
                made.uri = std::string{sheet_.text_of(t)};
                have_uri = true;
            } else if (t.type == token_type::string) {
                const std::string_view quoted = sheet_.text_of(t);
                made.uri = std::string{quoted.substr(1, quoted.size() - 2)};
                have_uri = true;
            } else {
                return;
            }
        }
        if (have_uri) { sheet_.namespaces.push_back(std::move(made)); }
    }

    // `@import <url> [layer | layer(...)]? [supports(...)]? <media-query-list>?`,
    // CSS Cascade 5 §5, at `at_` = its `;` (or EOF). Only while the leading run
    // is open, and only for an `@import` that is a source slice: one rebuilt
    // from an escape has no byte position and cannot be spliced out.
    void record_import(std::size_t at_token, std::span<const component_value> prelude) {
        if (!leading_ || sheet_.tokens[at_token].text >= sheet_.source_length) {
            leading_ = false;
            return;
        }
        import_statement made;
        made.begin = sheet_.tokens[at_token].text;
        made.end = at_eof() ? sheet_.source_length : sheet_.tokens[at_].text + 1;
        std::size_t k = 0;
        const auto skip_whitespace_in = [&] {
            while (k < prelude.size() && prelude[k].kind == cv_kind::token &&
                   sheet_.tokens[prelude[k].token].type == token_type::whitespace) {
                ++k;
            }
        };
        skip_whitespace_in();
        if (k >= prelude.size()) { return; }
        const component_value & first = prelude[k];
        const css_token & t = sheet_.tokens[first.token];
        if (first.kind == cv_kind::token &&
            (t.type == token_type::string || t.type == token_type::url)) {
            made.href = std::string{unquoted(t)};
        } else if (first.kind == cv_kind::function && ascii_iequals(text(t), "url(")) {
            for (const component_value & arg : sheet_.children_of(first)) {
                if (arg.kind != cv_kind::token) { continue; }
                const css_token & inner = sheet_.tokens[arg.token];
                if (inner.type == token_type::string) { made.href = std::string{unquoted(inner)}; }
            }
        } else {
            return;
        }
        ++k;
        // `layer`, `layer(...)` and `supports(...)` are not media queries;
        // whatever follows them is.
        for (;;) {
            skip_whitespace_in();
            if (k >= prelude.size()) { break; }
            const css_token & word = sheet_.tokens[prelude[k].token];
            if (prelude[k].kind == cv_kind::token && word.type == token_type::ident &&
                ascii_iequals(text(word), "layer")) {
                made.layered = true;
                ++k;
            } else if (prelude[k].kind == cv_kind::function &&
                       ascii_iequals(text(word), "layer(")) {
                // `layer(<layer-name>)`: a name that does not parse makes the
                // whole import invalid (CSS Cascade 5 §5.1).
                std::string name;
                if (!layer_name_of(sheet_.trimmed(sheet_.children_of(prelude[k])), name)) {
                    return;
                }
                made.layered = true;
                made.layer = std::move(name);
                ++k;
            } else if (prelude[k].kind == cv_kind::function &&
                       ascii_iequals(text(word), "supports(")) {
                made.supports = text_of_run(sheet_.children_of(prelude[k]));
                ++k;
            } else {
                break;
            }
        }
        if (k < prelude.size()) {
            for (std::size_t m = prelude[k].token; m < at_; ++m) {
                made.media += text(sheet_.tokens[m]);
            }
            made.media = std::string{trim(made.media, html_whitespace)};
        }
        sheet_.imports.push_back(std::move(made));
    }
    // A string token's body, or a url token as it is.
    [[nodiscard]] std::string_view unquoted(const css_token & t) const {
        const std::string_view raw = text(t);
        return t.type == token_type::string && raw.size() >= 2 ? raw.substr(1, raw.size() - 2)
                                                               : raw;
    }
    [[nodiscard]] bool at_eof() const { return here().type == token_type::eof; }
    [[nodiscard]] std::string_view text(const css_token & t) const { return sheet_.text_of(t); }

    void skip_whitespace() {
        while (here().type == token_type::whitespace) { ++at_; }
    }

    // §5.4.1, in both modes. `top_level` only decides whether CDO/CDC are ignored:
    // they are the HTML comment delimiters a 1990s page wrapped its stylesheet in,
    // and inside a rule they are a parse error rather than nothing.
    void consume_rule_list(bool top_level) {
        for (;;) {
            const token_type t = here().type;
            if (t == token_type::eof) { return; }
            if (t == token_type::whitespace) {
                ++at_;
                continue;
            }
            if (t == token_type::close_curly) {
                // The end of a conditional group's block. The caller consumed the
                // opener, so it owns this.
                return;
            }
            if (t == token_type::cdo || t == token_type::cdc) {
                if (top_level) {
                    ++at_;
                    continue;
                }
                consume_qualified_rule();
                continue;
            }
            if (t == token_type::at_keyword) {
                consume_at_rule();
                continue;
            }
            consume_qualified_rule();
        }
    }

    // §5.4.2, at the top level. The prelude is a selector list; the block's
    // contents are declarations and nested rules.
    void consume_qualified_rule() {
        leading_ = false;
        std::vector<component_value> prelude;
        while (!at_eof() && here().type != token_type::open_curly) {
            if (here().type == token_type::close_curly) {
                // A stray `}` before any `{`: the rule has no block, so there is
                // nothing to keep. Consume it so the outer loop advances.
                ++at_;
                return;
            }
            prelude.push_back(consume_component_value());
        }
        if (at_eof()) { return; } // §5.4.2: a prelude with no block is dropped
        const component_value block = consume_component_value(); // the `{...}`
        // A prelude that reads as a custom property declaration - `--x:hover
        // { }` - is no rule (§5.4.2, "consume a qualified rule").
        std::size_t k = 0;
        while (k < prelude.size() && sheet_.is_space(prelude[k])) { ++k; }
        if (k + 1 < prelude.size() && prelude[k].kind == cv_kind::token &&
            sheet_.tokens[prelude[k].token].type == token_type::ident &&
            text(sheet_.tokens[prelude[k].token]).starts_with("--")) {
            std::size_t c = k + 1;
            while (c < prelude.size() && sheet_.is_space(prelude[c])) { ++c; }
            if (c < prelude.size() && prelude[c].kind == cv_kind::token &&
                sheet_.tokens[prelude[c].token].type == token_type::colon) {
                return;
            }
        }
        style_rule(span_of(prelude), block);
    }

    // WHAT A BLOCK IS PARSED INSIDE OF. The enclosing style rule's selectors
    // are what `&` and an implicit `&` stand for and what a bare declaration
    // in a nested `@media` belongs to; inside `@scope` with no style rule
    // between, `&` is `:where(:scope)` and a bare declaration styles the
    // scoping root. Copied rather than viewed: the sheet's selector vector
    // moves while the children are parsed.
    struct block_context {
        std::vector<compiled_selector> parent;
        bool in_scope = false;
    };

    // A style rule, top-level or nested: compile the selector list in the
    // current context, then walk the block with THIS rule as the context.
    void style_rule(std::span<const component_value> prelude, const component_value & block) {
        const std::uint32_t first_selector = static_cast<std::uint32_t>(sheet_.selectors.size());
        const nesting_context nesting{context_.parent, context_.in_scope};
        const bool nested = !context_.parent.empty() || context_.in_scope;
        const std::uint32_t count =
            parse_selector_list(sheet_, prelude, *atoms_, nullptr, nested ? &nesting : nullptr);
        // The children see this rule's list, dead alternatives included: a
        // rule nested under a selector that cannot match compiles to one that
        // cannot match either, rather than to a top-level rule.
        block_context inner;
        inner.parent.assign(sheet_.selectors.begin() + first_selector,
                            sheet_.selectors.begin() + first_selector + count);
        const block_context saved = std::exchange(context_, std::move(inner));
        consume_block_contents(sheet_.children_of(block), first_selector, count);
        context_ = saved;
    }

    // §5.4.4, "consume a block's contents": declarations and rules, mixed.
    //
    // THE DECLARATIONS ARE EMITTED IN RUNS. `.a { color: red; .b { } color:
    // blue }` is a rule, a nested rule, and a second run of declarations that
    // CSS Nesting 1 §4 calls a nested declarations rule - and the runs must
    // keep their place in source order, because the nested rule's
    // declarations sit between them in the cascade's final tie-break. So each
    // run becomes a raw_rule of its own under the same selectors, with the
    // declaration order counter running through the whole block in sequence.
    //
    // THE RUN IS COPIED, AND THAT COPY IS THE FIX FOR A HEAP-USE-AFTER-FREE.
    // `incoming` is a span INTO `sheet_.values`, and every declaration this
    // emits APPENDS to that vector, so the first reallocation frees the buffer
    // the span points at. It looked harmless because the freed bytes still
    // held the old values; an ASAN build said otherwise, on the UA stylesheet.
    // Measured on bootstrap.css at about four percent of the parse.
    //
    // `first_selector`/`selector_count` are the selectors a run of
    // declarations belongs to; none at the top level or in a bare
    // conditional group, where a bare declaration is dropped (§5.4.4).
    void consume_block_contents(std::span<const component_value> incoming,
                                std::uint32_t first_selector, std::uint32_t selector_count) {
        const std::vector<component_value> owned{incoming.begin(), incoming.end()};
        const std::span<const component_value> run{owned};
        std::uint32_t run_first = static_cast<std::uint32_t>(sheet_.declarations.size());
        const auto flush = [&] {
            const std::uint32_t now = static_cast<std::uint32_t>(sheet_.declarations.size());
            if (now == run_first) { return; }
            if (selector_count != 0) {
                raw_rule r;
                r.first_selector = first_selector;
                r.selector_count = selector_count;
                r.first_declaration = run_first;
                r.declaration_count = now - run_first;
                r.condition = condition_;
                r.layer = layer_;
                r.scope = scope_;
                r.container = container_;
                sheet_.rules.push_back(r);
            } else if (context_.in_scope) {
                // Bare declarations directly inside `@scope` style the
                // scoping root as `:where(:scope)` would (CSS Cascade 6
                // §3.4); the scope compiled that one selector on entry.
                raw_rule r;
                r.first_selector = scope_root_selector_;
                r.selector_count = 1;
                r.first_declaration = run_first;
                r.declaration_count = now - run_first;
                r.condition = condition_;
                r.layer = layer_;
                r.scope = scope_;
                r.container = container_;
                sheet_.rules.push_back(r);
            }
            run_first = now;
        };
        const auto is_token = [&](std::size_t i, token_type type) {
            return i < run.size() && run[i].kind == cv_kind::token &&
                   sheet_.tokens[run[i].token].type == type;
        };
        const auto is_curly = [&](std::size_t i) {
            return i < run.size() && run[i].kind == cv_kind::block && run[i].open == '{';
        };
        std::size_t i = 0;
        while (i < run.size()) {
            if (sheet_.is_space(run[i]) || is_token(i, token_type::semicolon)) {
                ++i;
                continue;
            }
            if (is_token(i, token_type::at_keyword)) {
                const std::string_view name = value_of_at(sheet_.tokens[run[i].token]);
                std::size_t end = i + 1;
                while (end < run.size() && !is_curly(end) &&
                       !is_token(end, token_type::semicolon)) {
                    ++end;
                }
                const std::span<const component_value> prelude = run.subspan(i + 1, end - i - 1);
                if (is_curly(end)) {
                    flush();
                    ++nesting_;
                    at_rule_block_of(name, prelude, run[end]);
                    --nesting_;
                    // The nested rules wrote THEIR declarations after ours:
                    // the next run of ours starts past them.
                    run_first = static_cast<std::uint32_t>(sheet_.declarations.size());
                    i = end + 1;
                } else {
                    // A statement at-rule inside a block: `@layer a;` still
                    // declares its layers there; nothing else means anything.
                    if (at_kind_of(name) == at_kind::layer) { layer_statement(prelude); }
                    i = end + (end < run.size() ? 1 : 0);
                }
                continue;
            }
            // A DECLARATION OR A NESTED STYLE RULE. Try the declaration first,
            // as §5.4.4 does: an ident, a colon, and a value up to the next
            // `;` that holds no `{}` block - unless the property is custom, whose
            // value may hold anything. What fails that is a qualified rule whose
            // prelude runs to the first `{}` block.
            std::size_t end = i;
            while (end < run.size() && !is_token(end, token_type::semicolon)) { ++end; }
            bool declaration = false;
            if (is_token(i, token_type::ident)) {
                std::size_t colon = i + 1;
                while (colon < end && sheet_.is_space(run[colon])) { ++colon; }
                if (is_token(colon, token_type::colon)) {
                    const std::string_view property = text(sheet_.tokens[run[i].token]);
                    declaration = property.starts_with("--");
                    if (!declaration) {
                        declaration = true;
                        for (std::size_t k = colon + 1; k < end; ++k) {
                            if (is_curly(k)) { declaration = false; }
                        }
                    }
                }
            }
            if (declaration) {
                if (selector_count != 0 || context_.in_scope) {
                    emit_one_declaration(run.subspan(i, end - i));
                }
                i = end + 1;
                continue;
            }
            std::size_t open = i;
            while (open < end && !is_curly(open)) { ++open; }
            if (open >= end) {
                i = end + 1; // garbage up to the `;`: dropped, §5.4.4
                continue;
            }
            flush();
            style_rule(run.subspan(i, open - i), run[open]);
            run_first = static_cast<std::uint32_t>(sheet_.declarations.size());
            i = open + 1;
        }
        flush();
    }

    // The token at a run's front, when the front IS a single token.
    [[nodiscard]] const css_token * token_at(std::span<const component_value> run) const {
        if (run.empty() || run.front().kind != cv_kind::token) { return nullptr; }
        return &sheet_.tokens[run.front().token];
    }

    // The text of a run of component values: every token's text, joined.
    [[nodiscard]] std::string text_of_run(std::span<const component_value> run) const {
        std::string out;
        if (run.empty()) { return out; }
        for (std::uint32_t t = run.front().token; t < run.back().end_token; ++t) {
            out += text(sheet_.tokens[t]);
        }
        return out;
    }

    // `<layer-name>`: `<ident> [ '.' <ident> ]*` with nothing between, CSS
    // Cascade 5 §6.4.1. `run` is one comma-separated piece, trimmed.
    [[nodiscard]] bool layer_name_of(std::span<const component_value> run,
                                     std::string & out) const {
        out.clear();
        for (std::size_t i = 0; i < run.size(); ++i) {
            if (run[i].kind != cv_kind::token) { return false; }
            const css_token & t = sheet_.tokens[run[i].token];
            if (i % 2 == 0) {
                if (t.type != token_type::ident) { return false; }
                out += text(t);
            } else {
                if (t.type != token_type::delim || text(t) != ".") { return false; }
                out += '.';
            }
        }
        return !out.empty() && run.size() % 2 == 1;
    }

    // FILE A LAYER, and every ancestor it implies, in first-appearance order:
    // `@layer a.b` declares `a` then `a.b`. Names are relative to the layer
    // being parsed inside of. Returns the 1-based index of the leaf.
    [[nodiscard]] std::uint32_t declare_layer(std::string_view name) {
        std::string full =
            layer_prefix_.empty() ? std::string{name} : layer_prefix_ + "." + std::string{name};
        std::uint32_t leaf = 0;
        for (std::size_t dot = 0;;) {
            const std::size_t next = full.find('.', dot);
            const std::string_view prefix =
                std::string_view{full}.substr(0, next == std::string::npos ? full.size() : next);
            auto found = std::ranges::find(sheet_.layers, prefix);
            if (found == sheet_.layers.end()) {
                sheet_.layers.emplace_back(prefix);
                found = sheet_.layers.end() - 1;
            }
            leaf = static_cast<std::uint32_t>(found - sheet_.layers.begin()) + 1;
            if (next == std::string::npos) { break; }
            dot = next + 1;
        }
        return leaf;
    }

    // `@layer a, b.c;` - every name declared, in order; a bad name makes the
    // whole statement invalid and declares nothing.
    void layer_statement(std::span<const component_value> prelude) {
        std::vector<std::string> names;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= prelude.size(); ++i) {
            const bool comma = i < prelude.size() && prelude[i].kind == cv_kind::token &&
                               sheet_.tokens[prelude[i].token].type == token_type::comma;
            if (i < prelude.size() && !comma) { continue; }
            std::string name;
            if (!layer_name_of(sheet_.trimmed(prelude.subspan(start, i - start)), name)) { return; }
            names.push_back(std::move(name));
            start = i + 1;
        }
        for (const std::string & name : names) { (void)declare_layer(name); }
    }

    // A BLOCK AT-RULE'S BODY, wherever it sits: the conditional groups walk
    // their contents in the enclosing context - a nested `@media` in a style
    // rule keeps the rule's selectors for `&` and for its bare declarations -
    // and the rest are collected or skipped.
    void at_rule_block_of(std::string_view name, std::span<const component_value> prelude,
                          const component_value & block) {
        const at_kind kind = at_kind_of(name);
        const auto contents = [&] { consume_block_contents(sheet_.children_of(block), 0, 0); };
        // The selectors a bare declaration in the group belongs to: those of the
        // enclosing style rule, or nothing.
        const auto grouped = [&] {
            if (context_.parent.empty() && !context_.in_scope) {
                contents();
                return;
            }
            // A nested group's bare declarations are a nested declarations
            // rule under the parent's own selectors: the parent's list was
            // already compiled, so it is re-filed as one more list of the
            // same alternatives.
            const std::uint32_t first = static_cast<std::uint32_t>(sheet_.selectors.size());
            for (const compiled_selector & s : context_.parent) { sheet_.selectors.push_back(s); }
            consume_block_contents(sheet_.children_of(block), first,
                                   static_cast<std::uint32_t>(context_.parent.size()));
        };
        switch (kind) {
        case at_kind::media: {
            // A REAL CONDITION. The prelude becomes a query list, the list
            // becomes an entry in the sheet's condition table with the enclosing
            // condition as its parent, and every rule inside records that index.
            // Nothing is evaluated here: a sheet is parsed once and the viewport
            // changes, so the truth of a condition belongs to the engine.
            media_condition condition;
            condition.parent = condition_;
            condition.queries = parse_media_query_list(sheet_, prelude);
            push_condition(std::move(condition), grouped);
            return;
        }
        case at_kind::supports: {
            // DECIDED NOW, ONCE: what the property table accepts does not
            // change. A false condition files its rules under a condition
            // that is never true - `not all` - so the block still parses,
            // and its `@layer` names still count.
            if (supports_condition(text_of_run(prelude))) {
                grouped();
                return;
            }
            media_condition never;
            never.parent = condition_;
            media_query q;
            q.malformed = true;
            never.queries.push_back(std::move(q));
            push_condition(std::move(never), grouped);
            return;
        }
        case at_kind::layer: {
            // `@layer <name>? { }`. One name or none; a list is the statement
            // form's alone and makes the block invalid.
            const auto trimmed = sheet_.trimmed(prelude);
            std::string name;
            if (trimmed.empty()) {
                name = "\x01" + std::to_string(++anonymous_layers_);
            } else if (!layer_name_of(trimmed, name)) {
                return;
            }
            const std::uint32_t index = declare_layer(name);
            const std::uint32_t saved_layer = std::exchange(layer_, index);
            const std::string saved_prefix = std::exchange(layer_prefix_, sheet_.layers[index - 1]);
            grouped();
            layer_ = saved_layer;
            layer_prefix_ = saved_prefix;
            return;
        }
        case at_kind::scope: scope_block_of(prelude, block); return;
        case at_kind::container: {
            // `[<container-name>]? <container-condition>`: a leading identifier
            // that is not `not`/`and`/`or` is the name; the rest is the
            // condition, kept as text for the engine. No condition is no rule.
            auto run = sheet_.trimmed(prelude);
            container_condition made;
            made.parent = container_;
            const css_token * first = token_at(run);
            if (first != nullptr && first->type == token_type::ident &&
                !ascii_iequals_any(text(*first), {"not", "and", "or"})) {
                made.name = std::string{text(*first)};
                run = sheet_.trimmed(run.subspan(1));
            }
            if (run.empty()) { return; }
            made.condition = text_of_run(run);
            sheet_.containers.push_back(std::move(made));
            const std::uint32_t saved =
                std::exchange(container_, static_cast<std::uint32_t>(sheet_.containers.size()));
            grouped();
            container_ = saved;
            return;
        }
        case at_kind::media_like: grouped(); return;
        case at_kind::font_face: {
            const std::uint32_t first = static_cast<std::uint32_t>(sheet_.declarations.size());
            emit_declarations(sheet_.children_of(block));
            font_face f;
            f.first_declaration = first;
            f.declaration_count = static_cast<std::uint32_t>(sheet_.declarations.size()) - first;
            if (f.declaration_count != 0) { sheet_.font_faces.push_back(f); }
            return;
        }
        case at_kind::property:
        case at_kind::function: {
            // Collected like @font-face, prelude included, and only at the top
            // level: one inside a conditional group is discarded as before.
            if (nesting_ != 1) { return; }
            at_rule_block r;
            r.prelude_first = static_cast<std::uint32_t>(sheet_.values.size());
            r.prelude_count = static_cast<std::uint32_t>(prelude.size());
            const std::vector<component_value> copy{prelude.begin(), prelude.end()};
            sheet_.values.insert(sheet_.values.end(), copy.begin(), copy.end());
            r.first_declaration = static_cast<std::uint32_t>(sheet_.declarations.size());
            emit_declarations(sheet_.children_of(block));
            r.declaration_count =
                static_cast<std::uint32_t>(sheet_.declarations.size()) - r.first_declaration;
            (kind == at_kind::property ? sheet_.properties : sheet_.functions).push_back(r);
            return;
        }
        case at_kind::keyframes: record_keyframes(prelude, block); return;
        case at_kind::statement:
        case at_kind::skip:
            // @page, @starting-style, ... Their block is discarded.
            return;
        }
    }

    template <typename F> void push_condition(media_condition condition, const F & body) {
        sheet_.conditions.push_back(std::move(condition));
        const std::uint32_t saved = condition_;
        condition_ = static_cast<std::uint32_t>(sheet_.conditions.size() - 1);
        body();
        condition_ = saved;
    }

    // `@scope [(<scope-start>)]? [to (<scope-end>)]? { }`, CSS Cascade 6 §3.
    // The start is compiled in the enclosing context - a nested scope's
    // `:scope` and a relative selector refer to the outer root - and the end
    // relative to this scope's root. Anything else in the prelude makes the
    // rule invalid.
    void scope_block_of(std::span<const component_value> prelude, const component_value & block) {
        auto run = sheet_.trimmed(prelude);
        scope_block made;
        made.parent = scope_;
        const nesting_context outer{{}, true};
        const nesting_context * start_context =
            scope_ != 0 || context_.in_scope || !context_.parent.empty() ? &outer : nullptr;
        bool invalid = false;
        if (!run.empty() && run.front().kind == cv_kind::block && run.front().open == '(') {
            made.first_root = static_cast<std::uint32_t>(sheet_.selectors.size());
            made.root_count = parse_selector_list(sheet_, sheet_.children_of(run.front()), *atoms_,
                                                  &invalid, start_context);
            run = sheet_.trimmed(run.subspan(1));
        }
        if (!run.empty()) {
            const css_token * to =
                run.front().kind == cv_kind::token ? &sheet_.tokens[run.front().token] : nullptr;
            if (to == nullptr || to->type != token_type::ident || !ascii_iequals(text(*to), "to")) {
                return;
            }
            run = sheet_.trimmed(run.subspan(1));
            if (run.size() != 1 || run.front().kind != cv_kind::block || run.front().open != '(') {
                return;
            }
            made.first_limit = static_cast<std::uint32_t>(sheet_.selectors.size());
            made.limit_count = parse_selector_list(sheet_, sheet_.children_of(run.front()), *atoms_,
                                                   &invalid, &outer);
        }
        // `@scope ()`, `@scope to ()`, a selector that is not one: the rule is
        // invalid and its block applies nothing.
        if (invalid) { return; }
        sheet_.scopes.push_back(made);
        const std::uint32_t saved_scope = scope_;
        scope_ = static_cast<std::uint32_t>(sheet_.scopes.size());
        // The rules inside: `&` is `:where(:scope)`, and a bare declaration
        // styles the root through the same one-compound selector, compiled
        // once per scope - by hand, since no token spells it.
        {
            compiled_selector amp;
            compound c;
            c.structural = structural_scope;
            c.nesting = true;
            amp.parts.push_back(std::move(c));
            amp.explicit_scope = true;
            const std::uint32_t at = static_cast<std::uint32_t>(sheet_.selectors.size());
            sheet_.selectors.push_back(std::move(amp));
            block_context inner;
            inner.in_scope = true;
            const std::uint32_t saved_root = std::exchange(scope_root_selector_, at);
            const block_context saved_context = std::exchange(context_, std::move(inner));
            consume_block_contents(sheet_.children_of(block), 0, 0);
            context_ = saved_context;
            scope_root_selector_ = saved_root;
        }
        scope_ = saved_scope;
    }

    // §5.4.3, at the top level.
    void consume_at_rule() {
        const std::size_t at_token = at_;
        const std::string_view name = value_of_at(here());
        ++at_;
        const at_kind kind = at_kind_of(name);
        std::vector<component_value> prelude;
        while (!at_eof() && here().type != token_type::open_curly &&
               here().type != token_type::semicolon) {
            prelude.push_back(consume_component_value());
        }
        const bool ended = here().type == token_type::semicolon;
        if (ended || at_eof()) {
            // `@namespace [<prefix>]? <url>;` is RECORDED, because it decides
            // which prefixes the selectors after it may use; a LEADING `@import`
            // is too, for whoever can fetch it; `@layer a, b;` declares its
            // layers. `@charset` is consumed and that is all.
            if (ascii_iequals(name, "import")) {
                record_import(at_token, prelude);
            } else if (ascii_iequals(name, "namespace")) {
                if (ended) { record_namespace(prelude); }
                leading_ = false;
            } else if (kind == at_kind::layer) {
                if (ended) { layer_statement(prelude); }
            } else if (!ascii_iequals(name, "charset")) {
                leading_ = false;
            }
            if (ended) { ++at_; }
            return;
        }
        leading_ = false;
        const component_value block = consume_component_value();
        if (kind == at_kind::statement) { return; } // a statement with a block: skipped
        ++nesting_;
        at_rule_block_of(name, span_of(prelude), block);
        --nesting_;
    }

    // CSS Animations 1 §4. The prelude is one `<keyframes-name>` - a custom
    // ident or a string - and the block is a list of keyframe blocks, each a
    // `<keyframe-selector>#` prelude and a declaration block. The rule's
    // `{...}` was consumed as ONE component value, so its children are the
    // keyframe preludes' tokens with each keyframe's `{...}` as a nested block:
    // walked here rather than re-tokenised. A block whose selector list has a
    // token the grammar refuses is dropped, and only that block.
    void record_keyframes(std::span<const component_value> prelude, const component_value & block) {
        keyframes_block made;
        made.condition = condition_;
        for (const component_value & v : prelude) {
            if (v.kind != cv_kind::token) { return; }
            const css_token & t = sheet_.tokens[v.token];
            if (t.type == token_type::whitespace) { continue; }
            if (!made.name.empty()) { return; }
            if (t.type == token_type::ident) {
                made.name = std::string{text(t)};
            } else if (t.type == token_type::string) {
                made.name = std::string{unquoted(t)};
            } else {
                return;
            }
        }
        // `none` and the CSS-wide keywords are not names (§4.2), and neither is
        // an empty one.
        if (made.name.empty() || ascii_iequals(made.name, "none") ||
            ascii_iequals(made.name, "initial") || ascii_iequals(made.name, "inherit") ||
            ascii_iequals(made.name, "unset") || ascii_iequals(made.name, "revert") ||
            ascii_iequals(made.name, "revert-layer") || ascii_iequals(made.name, "default")) {
            return;
        }
        // The children are read through a COPY: emit_declarations appends to
        // sheet_.values, and a span into it would dangle (see emit_declarations).
        const std::span<const component_value> inner = sheet_.children_of(block);
        const std::vector<component_value> children{inner.begin(), inner.end()};
        std::vector<double> offsets;
        bool bad = false;
        bool expect_selector = true;
        for (const component_value & v : children) {
            if (v.kind == cv_kind::block && v.open == '{') {
                if (!bad && !offsets.empty() && !expect_selector) {
                    keyframe_block frame;
                    frame.offsets = offsets;
                    frame.first_declaration =
                        static_cast<std::uint32_t>(sheet_.declarations.size());
                    emit_declarations(sheet_.children_of(v));
                    frame.declaration_count =
                        static_cast<std::uint32_t>(sheet_.declarations.size()) -
                        frame.first_declaration;
                    made.frames.push_back(std::move(frame));
                }
                offsets.clear();
                bad = false;
                expect_selector = true;
                continue;
            }
            if (v.kind != cv_kind::token) {
                bad = true;
                continue;
            }
            const css_token & t = sheet_.tokens[v.token];
            if (t.type == token_type::whitespace) { continue; }
            if (t.type == token_type::comma) {
                if (expect_selector) { bad = true; }
                expect_selector = true;
                continue;
            }
            if (!expect_selector) {
                bad = true;
                continue;
            }
            expect_selector = false;
            if (t.type == token_type::ident && ascii_iequals(text(t), "from")) {
                offsets.push_back(0);
            } else if (t.type == token_type::ident && ascii_iequals(text(t), "to")) {
                offsets.push_back(1);
            } else if (t.type == token_type::percentage && t.number >= 0 && t.number <= 100) {
                offsets.push_back(t.number / 100.0);
            } else {
                bad = true;
            }
        }
        sheet_.keyframes.push_back(std::move(made));
    }

    // §5.4.7. A block or a function owns its children; a preserved token is one
    // component value on its own.
    [[nodiscard]] component_value consume_component_value() {
        const css_token & t = here();
        if (t.type == token_type::open_curly || t.type == token_type::open_paren ||
            t.type == token_type::open_square) {
            return consume_block();
        }
        if (t.type == token_type::function) { return consume_function(); }
        component_value v;
        v.kind = cv_kind::token;
        v.token = static_cast<std::uint32_t>(at_);
        ++at_;
        v.end_token = static_cast<std::uint32_t>(at_);
        return v;
    }

    [[nodiscard]] static token_type closer_for(token_type open) {
        if (open == token_type::open_curly) { return token_type::close_curly; }
        if (open == token_type::open_square) { return token_type::close_square; }
        return token_type::close_paren;
    }

    // §5.4.8. Children are gathered into a scratch vector and then appended to
    // sheet_.values in ONE run, because the vector reallocates while nested blocks
    // are being consumed - so a child's index is only stable once its whole run is
    // in place.
    [[nodiscard]] component_value consume_block() {
        const std::size_t open_index = at_;
        const token_type open = here().type;
        const token_type close = closer_for(open);
        ++at_;
        std::vector<component_value> children;
        while (!at_eof() && here().type != close) { children.push_back(consume_component_value()); }
        if (here().type == close) { ++at_; }
        component_value v;
        v.kind = cv_kind::block;
        v.open = open == token_type::open_curly ? '{' : open == token_type::open_square ? '[' : '(';
        v.token = static_cast<std::uint32_t>(open_index);
        v.end_token = static_cast<std::uint32_t>(at_);
        attach(v, children);
        return v;
    }

    // §5.4.9. `name(` was one token, so the function's name is the token's text
    // minus the trailing `(`.
    [[nodiscard]] component_value consume_function() {
        const std::size_t name_index = at_;
        ++at_;
        std::vector<component_value> children;
        while (!at_eof() && here().type != token_type::close_paren) {
            children.push_back(consume_component_value());
        }
        if (here().type == token_type::close_paren) { ++at_; }
        component_value v;
        v.kind = cv_kind::function;
        v.open = '(';
        v.token = static_cast<std::uint32_t>(name_index);
        v.end_token = static_cast<std::uint32_t>(at_);
        attach(v, children);
        return v;
    }

    void attach(component_value & v, const std::vector<component_value> & children) {
        if (children.empty()) { return; }
        v.first_child = static_cast<std::uint32_t>(sheet_.values.size());
        v.child_count = static_cast<std::uint32_t>(children.size());
        sheet_.values.insert(sheet_.values.end(), children.begin(), children.end());
    }

    [[nodiscard]] std::span<const component_value> span_of(
        const std::vector<component_value> & v) const {
        return std::span<const component_value>{v};
    }

    [[nodiscard]] std::string_view value_of_at(const css_token & t) const {
        std::string_view raw = text(t);
        if (!raw.empty() && raw.front() == '@') { raw.remove_prefix(1); }
        return raw;
    }

    // §5.4.4/§5.4.5, over component values rather than tokens: split on top-level
    // semicolons, then `ident : value`. A `;` inside a function or a block is a
    // CHILD and is invisible here, which is the whole point.
    // THE RUN IS COPIED, AND THAT COPY IS THE FIX FOR A HEAP-USE-AFTER-FREE.
    //
    // `run` arrives as `sheet_.children_of(block)`, a span INTO `sheet_.values`.
    // Every declaration this loop emits APPENDS to that same vector, so the
    // first reallocation frees the buffer `run` points at - and the next
    // iteration's `is_semicolon(run[end])` reads it. The append itself was a
    // self-aliasing `insert` for the same reason, which the standard does not
    // allow either.
    //
    // It looked harmless because it is: the freed bytes still hold the old
    // component values, so every golden matches and no test has ever failed on
    // it. An ASAN build says otherwise, and it fires on the UA stylesheet -
    // which is to say on EVERY browser construction. 29 of the 52 tests in the
    // asan preset could not run.
    //
    // Copying once, here, makes the span stable for the whole loop and makes
    // the append a copy from somewhere else, which fixes both halves.
    //
    // IT IS NOT FREE, and the first draft of this comment said it was. Measured
    // on bootstrap.css - 298 KB, the largest sheet in the corpus - interleaved,
    // five pairs, the copy losing every one: parse 2.44 ms to 2.55, and parse
    // plus file into the engine 2.80 to 2.94. About four percent, for a
    // heap-use-after-free on every browser construction. Cheap, but say the
    // number.
    //
    // Zero-copy is possible and was not done: hold (first, count) indices into
    // `sheet_.values` rather than a span, since indices survive a reallocation
    // and pointers do not. That is index arithmetic through three functions and
    // a separate buffer for the append, in a parser this change had no other
    // business being in.
    void emit_declarations(std::span<const component_value> incoming) {
        const std::vector<component_value> owned{incoming.begin(), incoming.end()};
        const std::span<const component_value> run{owned};
        std::size_t at = 0;
        while (at <= run.size()) {
            std::size_t end = at;
            while (end < run.size() && !is_semicolon(run[end])) { ++end; }
            emit_one_declaration(run.subspan(at, end - at));
            if (end >= run.size()) { break; }
            at = end + 1;
        }
    }

    [[nodiscard]] bool is_semicolon(const component_value & v) const {
        return v.kind == cv_kind::token && sheet_.tokens[v.token].type == token_type::semicolon;
    }

    void emit_one_declaration(std::span<const component_value> run) {
        // Trim whitespace both ends.
        while (!run.empty() && sheet_.is_space(run.front())) { run = run.subspan(1); }
        while (!run.empty() && sheet_.is_space(run.back())) {
            run = run.subspan(0, run.size() - 1);
        }
        if (run.empty()) { return; }
        // The name must be an ident, and the next non-whitespace thing a colon.
        if (run.front().kind != cv_kind::token) { return; }
        const css_token & name = sheet_.tokens[run.front().token];
        if (name.type != token_type::ident) { return; }
        std::size_t i = 1;
        while (i < run.size() && sheet_.is_space(run[i])) { ++i; }
        if (i >= run.size() || run[i].kind != cv_kind::token ||
            sheet_.tokens[run[i].token].type != token_type::colon) {
            return; // no colon: not a declaration, and §5.4.5 drops it
        }
        std::span<const component_value> value = run.subspan(i + 1);
        while (!value.empty() && sheet_.is_space(value.front())) { value = value.subspan(1); }

        raw_declaration d;
        const std::string_view property = text(name);
        // A CUSTOM PROPERTY keeps its case and its value verbatim: `--Foo` and
        // `--foo` are different properties, and the value is a token stream that
        // is never parsed until something reads it through var().
        d.custom = property.size() >= 2 && property[0] == '-' && property[1] == '-';
        // `--` alone is reserved and no property (CSS Variables 1 §2).
        if (property == "--") { return; }
        d.property = d.custom ? atoms_->intern(property) : atoms_->intern_lower(property);

        // `!important`, as a trailing delim `!` and ident `important` with optional
        // whitespace between. Peeled off the VALUE rather than searched for in the
        // text, which is what makes `content: "!important"` a string and not a
        // priority.
        // Walked BACKWARDS, which is the only way that stays readable: skip
        // trailing whitespace, expect `important`, skip whitespace again, expect
        // `!`. Anything else leaves the value exactly as it was.
        {
            std::size_t end = value.size();
            const auto skip_space_back = [&](std::size_t i) {
                while (i > 0 && sheet_.is_space(value[i - 1])) { --i; }
                return i;
            };
            std::size_t i = skip_space_back(end);
            if (i > 0) {
                const component_side word = side_of(value[i - 1]);
                if (word.type == token_type::ident && ascii_iequals(word.text, "important")) {
                    const std::size_t before_word = skip_space_back(i - 1);
                    if (before_word > 0) {
                        const component_side bang = side_of(value[before_word - 1]);
                        if (bang.type == token_type::delim && bang.text == "!") {
                            d.important = true;
                            value = value.subspan(0, before_word - 1);
                        }
                    }
                }
            }
        }
        while (!value.empty() && sheet_.is_space(value.back())) {
            value = value.subspan(0, value.size() - 1);
        }
        if (value.empty()) {
            // `--x: ;` is an EMPTY BUT VALID custom property and must substitute to
            // nothing, so it is kept; an empty ordinary declaration is dropped.
            if (!d.custom) { return; }
        }

        // `value` is a subspan of the copy emit_declarations owns, so this
        // append is not self-aliasing and the span survives it. Read first
        // regardless: it costs nothing and it is one less thing depending on a
        // caller three frames up.
        auto [text_at, text_len] = source_span(value);
        // AN EMPTY CUSTOM PROPERTY IS ONE SPACE (CSS Variables 1 §2): `--x:;`
        // and `--x: ` both read back as " " from every engine, and substitute
        // to whitespace.
        if (d.custom && text_len == 0) {
            text_at = static_cast<std::uint32_t>(sheet_.pool.size());
            text_len = 1;
            sheet_.pool += ' ';
        }
        d.first_value = static_cast<std::uint32_t>(sheet_.values.size());
        d.value_count = static_cast<std::uint32_t>(value.size());
        sheet_.values.insert(sheet_.values.end(), value.begin(), value.end());
        d.text = text_at;
        d.length = text_len;
        d.order = order_++;
        sheet_.declarations.push_back(d);
    }

    struct component_side {
        token_type type = token_type::eof;
        std::string_view text;
    };
    [[nodiscard]] component_side side_of(const component_value & v) const {
        if (v.kind != cv_kind::token) { return component_side{}; }
        const css_token & t = sheet_.tokens[v.token];
        return component_side{t.type, text(t)};
    }

    // The value's raw source substring, which is what the cascade stores today.
    //
    // [token, end_token) is the value's whole extent, closing brackets included, so
    // the span is the first token's start to the last token's end. A run is a
    // contiguous slice of the pool only when every token came from the SOURCE half -
    // an escape was rebuilt into the tail and appears nowhere in order - and that is
    // rare enough (Bootstrap: never) that the fallback simply concatenates.
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> source_span(
        std::span<const component_value> value) {
        if (value.empty()) { return {0, 0}; }
        const std::uint32_t first_index = value.front().token;
        const std::uint32_t last_index = value.back().end_token - 1;
        const css_token & first = sheet_.tokens[first_index];
        const css_token & last = sheet_.tokens[last_index];
        const bool contiguous = first.text < sheet_.source_length &&
                                last.text < sheet_.source_length &&
                                last.text + last.length >= first.text;
        if (contiguous) {
            // A <url-token>'s text is its BODY: `url(` and `)` are consumed
            // around it and not recorded, so a value beginning or ending with
            // one is widened back over them - `src: url(a.ttf)` is the whole
            // function, not `a.ttf`.
            std::size_t begin = first.text;
            std::size_t end = last.text + last.length;
            const std::string_view source =
                std::string_view{sheet_.pool}.substr(0, sheet_.source_length);
            const auto is_space = [](char c) {
                return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
            };
            if (first.type == token_type::url) {
                while (begin > 0 && is_space(source[begin - 1])) { --begin; }
                if (begin >= 4 && source[begin - 1] == '(' &&
                    ascii_iequals(source.substr(begin - 4, 3), "url")) {
                    begin -= 4;
                }
            }
            if (last.type == token_type::url) {
                while (end < source.size() && is_space(source[end])) { ++end; }
                if (end < source.size() && source[end] == ')') { ++end; }
            }
            return {static_cast<std::uint32_t>(begin), static_cast<std::uint32_t>(end - begin)};
        }
        const std::size_t start = sheet_.pool.size();
        for (std::uint32_t i = first_index; i <= last_index; ++i) {
            // Copied out before the append: the pool is what text() reads from, so
            // appending to it while holding a view into it would dangle.
            const css_token & tok = sheet_.tokens[i];
            std::string piece{text(tok)};
            if (tok.type == token_type::url) { piece = "url(" + piece + ")"; }
            // A REBUILT IDENTIFIER IS DECODED TEXT, and is written back as an
            // identifier: `\33 myident` decoded is `3myident`, which reads as
            // a dimension the second time round (ident-function-computed).
            if (tok.text >= sheet_.source_length && tok.type == token_type::ident) {
                piece = serialize_identifier(piece);
            }
            sheet_.pool += piece;
        }
        return {static_cast<std::uint32_t>(start),
                static_cast<std::uint32_t>(sheet_.pool.size() - start)};
    }

    stylesheet sheet_;
    atom_table * atoms_;
    std::size_t at_ = 0;
    std::int32_t order_ = 0;
    // The `@media` a rule being parsed sits inside. 0 is the unconditional entry.
    std::uint32_t condition_ = 0;
    // How many at-rule blocks deep the parse is; @property and @function are
    // only collected at 1, the top level.
    std::uint32_t nesting_ = 0;
    // The `@layer` a rule being parsed sits inside - 0 for none - and its full
    // name, which a nested `@layer` is relative to.
    std::uint32_t layer_ = 0;
    std::string layer_prefix_;
    std::uint32_t anonymous_layers_ = 0;
    // The `@scope` a rule sits inside, 0 for none, and the index of the
    // `:where(:scope)` selector its bare declarations file under.
    std::uint32_t scope_ = 0;
    std::uint32_t scope_root_selector_ = 0;
    // The `@container` a rule sits inside, 0 for none.
    std::uint32_t container_ = 0;
    // The enclosing style rule, for nesting - see block_context.
    block_context context_;
    // Whether the sheet's leading run of `@charset` / `@layer x;` / `@import`
    // statements is still open: an `@import` after anything else is ignored.
    bool leading_ = true;
};

} // namespace

stylesheet parse_stylesheet(std::string_view css, atom_table & atoms) {
    parser p{css, atoms};
    return p.take_stylesheet();
}

stylesheet parse_selector_text(std::string_view text, atom_table & atoms, bool & invalid,
                               const std::vector<namespace_declaration> * namespaces,
                               const nesting_context * nesting) {
    parser p{text, atoms};
    return p.take_selector_list(invalid, namespaces, nesting);
}

stylesheet parse_declaration_list(std::string_view css, atom_table & atoms) {
    parser p{css, atoms};
    return p.take_declaration_list();
}

} // namespace ctbrowser::style::css
