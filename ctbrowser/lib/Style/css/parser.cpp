#include <ctbrowser/style/css/parser.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
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
// AT-RULES ARE HANDLED EXACTLY AS THE PREVIOUS FRONT END HANDLED THEM, on purpose.
// @media recurses with its condition IGNORED except for a `print` or `portrait`
// ident, which is wrong - every breakpoint applies at once and the last in source
// order wins - but it is the wrong thing the recorded baselines were measured
// against, and changing two things at once makes a rendering difference
// unattributable. Real media evaluation is its own rung.

namespace ctbrowser::style::css {
namespace {

// The at-rules this recognises. Everything else with a block has its block
// skipped, and everything else without one is consumed to the `;`.
enum class at_kind {
    media,
    media_like,
    font_face,
    property,
    function,
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
    if (ascii_iequals(name, "supports") || ascii_iequals(name, "document") ||
        ascii_iequals(name, "layer")) {
        // "media-like" = a conditional group whose contents are rules. `@layer`
        // with a block is one too; `@layer a;` is a statement and falls out below
        // because it has no block.
        return at_kind::media_like;
    }
    if (ascii_iequals(name, "font-face")) { return at_kind::font_face; }
    if (ascii_iequals(name, "property")) { return at_kind::property; }
    if (ascii_iequals(name, "function")) { return at_kind::function; }
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
                                                const std::vector<namespace_declaration> * ns) {
        if (ns != nullptr) {
            sheet_.namespaces = *ns;
        } else {
            sheet_.prefixes_checked = false;
        }
        std::vector<component_value> run;
        while (!at_eof()) { run.push_back(consume_component_value()); }
        (void)parse_selector_list(sheet_, span_of(run), *atoms_, &invalid);
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
                ++k;
            } else if (prelude[k].kind == cv_kind::function &&
                       (ascii_iequals(text(word), "layer(") ||
                        ascii_iequals(text(word), "supports("))) {
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

    // §5.4.2. The prelude is a selector list; the block is a declaration list.
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

        const std::uint32_t first_selector = static_cast<std::uint32_t>(sheet_.selectors.size());
        const std::uint32_t count = parse_selector_list(sheet_, span_of(prelude), *atoms_);
        const std::uint32_t first_declaration =
            static_cast<std::uint32_t>(sheet_.declarations.size());
        // The block's children were appended to sheet_.values by
        // consume_component_value, so they are addressed rather than copied.
        emit_declarations(sheet_.children_of(block));

        raw_rule r;
        r.first_selector = first_selector;
        r.selector_count = count;
        r.first_declaration = first_declaration;
        r.condition = condition_;
        r.declaration_count =
            static_cast<std::uint32_t>(sheet_.declarations.size()) - first_declaration;
        // A rule with no declarations is kept out: it can never contribute to the
        // cascade, and every consumer would have to skip it.
        if (r.declaration_count != 0 && r.selector_count != 0) { sheet_.rules.push_back(r); }
    }

    // §5.4.3.
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
            // is too, for whoever can fetch it. The rest - @charset, `@layer a;`
            // - are consumed and that is all.
            if (ascii_iequals(name, "import")) {
                record_import(at_token, prelude);
            } else if (ascii_iequals(name, "namespace")) {
                if (ended) { record_namespace(prelude); }
                leading_ = false;
            } else if (!ascii_iequals(name, "charset") && !ascii_iequals(name, "layer")) {
                leading_ = false;
            }
            if (ended) { ++at_; }
            return;
        }
        leading_ = false;
        if (kind == at_kind::statement) {
            // A statement at-rule that turned out to have a block: skip it, since
            // its contents are not rules.
            (void)consume_component_value();
            return;
        }
        if (kind == at_kind::media && here().type == token_type::open_curly) {
            // A REAL CONDITION. The prelude becomes a query list, the list becomes an
            // entry in the sheet's condition table with the enclosing condition as its
            // parent, and every rule inside records that index. Nothing is evaluated
            // here: a sheet is parsed once and the viewport changes, so the truth of a
            // condition belongs to the engine and not to the parse.
            media_condition condition;
            condition.parent = condition_;
            condition.queries = parse_media_query_list(sheet_, span_of(prelude));
            sheet_.conditions.push_back(std::move(condition));
            const std::uint32_t saved = condition_;
            condition_ = static_cast<std::uint32_t>(sheet_.conditions.size() - 1);
            ++at_; // the `{`
            ++nesting_;
            consume_rule_list(/*top_level=*/false);
            --nesting_;
            if (here().type == token_type::close_curly) { ++at_; }
            condition_ = saved;
            return;
        }
        if (kind == at_kind::media_like) {
            // `@supports`, `@document` and `@layer`: the CONTENTS are rules, and the
            // condition is still ignored. `@supports` needs a property table to answer
            // honestly - and answering `true` for `display: grid` would be worse than
            // answering nothing, because a feature-detecting page would then pick the
            // grid path. That is its own rung.
            ++at_; // the `{`, so the nested rules are parsed in place
            ++nesting_;
            consume_rule_list(/*top_level=*/false);
            --nesting_;
            if (here().type == token_type::close_curly) { ++at_; }
            return;
        }
        if (kind == at_kind::font_face) {
            const component_value block = consume_component_value();
            const std::uint32_t first = static_cast<std::uint32_t>(sheet_.declarations.size());
            emit_declarations(sheet_.children_of(block));
            font_face f;
            f.first_declaration = first;
            f.declaration_count = static_cast<std::uint32_t>(sheet_.declarations.size()) - first;
            if (f.declaration_count != 0) { sheet_.font_faces.push_back(f); }
            return;
        }
        if (kind == at_kind::property || kind == at_kind::function) {
            // Collected like @font-face, prelude included, and only at the top
            // level: one inside a conditional group is discarded as before.
            const component_value block = consume_component_value();
            if (nesting_ != 0) { return; }
            at_rule_block r;
            r.prelude_first = static_cast<std::uint32_t>(sheet_.values.size());
            r.prelude_count = static_cast<std::uint32_t>(prelude.size());
            sheet_.values.insert(sheet_.values.end(), prelude.begin(), prelude.end());
            r.first_declaration = static_cast<std::uint32_t>(sheet_.declarations.size());
            emit_declarations(sheet_.children_of(block));
            r.declaration_count =
                static_cast<std::uint32_t>(sheet_.declarations.size()) - r.first_declaration;
            (kind == at_kind::property ? sheet_.properties : sheet_.functions).push_back(r);
            return;
        }
        // @keyframes, @page, @container, @scope, ... Their block is
        // consumed and discarded. @keyframes is CAPTURED by a later rung - nothing
        // reads it today, so capturing it now would be storage with no consumer.
        (void)consume_component_value();
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
    [[nodiscard]] bool is_whitespace(const component_value & v) const {
        return v.kind == cv_kind::token && sheet_.tokens[v.token].type == token_type::whitespace;
    }

    void emit_one_declaration(std::span<const component_value> run) {
        // Trim whitespace both ends.
        while (!run.empty() && is_whitespace(run.front())) { run = run.subspan(1); }
        while (!run.empty() && is_whitespace(run.back())) { run = run.subspan(0, run.size() - 1); }
        if (run.empty()) { return; }
        // The name must be an ident, and the next non-whitespace thing a colon.
        if (run.front().kind != cv_kind::token) { return; }
        const css_token & name = sheet_.tokens[run.front().token];
        if (name.type != token_type::ident) { return; }
        std::size_t i = 1;
        while (i < run.size() && is_whitespace(run[i])) { ++i; }
        if (i >= run.size() || run[i].kind != cv_kind::token ||
            sheet_.tokens[run[i].token].type != token_type::colon) {
            return; // no colon: not a declaration, and §5.4.5 drops it
        }
        std::span<const component_value> value = run.subspan(i + 1);
        while (!value.empty() && is_whitespace(value.front())) { value = value.subspan(1); }

        raw_declaration d;
        const std::string_view property = text(name);
        // A CUSTOM PROPERTY keeps its case and its value verbatim: `--Foo` and
        // `--foo` are different properties, and the value is a token stream that
        // is never parsed until something reads it through var().
        d.custom = property.size() >= 2 && property[0] == '-' && property[1] == '-';
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
                while (i > 0 && is_whitespace(value[i - 1])) { --i; }
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
        while (!value.empty() && is_whitespace(value.back())) {
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
        const auto [text_at, text_len] = source_span(value);
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
    // How many conditional groups deep the parse is; @property and @function
    // are only collected at 0.
    std::uint32_t nesting_ = 0;
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
                               const std::vector<namespace_declaration> * namespaces) {
    parser p{text, atoms};
    return p.take_selector_list(invalid, namespaces);
}

stylesheet parse_declaration_list(std::string_view css, atom_table & atoms) {
    parser p{css, atoms};
    return p.take_declaration_list();
}

} // namespace ctbrowser::style::css
