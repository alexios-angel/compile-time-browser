#include <ctbrowser/style/css/parser.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/media.hpp>
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
    statement,
    skip
};

[[nodiscard]] at_kind at_kind_of(std::string_view name) {
    // Vendor prefixes are stripped before the comparison, so `@-webkit-keyframes`
    // is the same at-rule as `@keyframes` - which matters because a sheet that
    // writes both would otherwise have the prefixed one skipped by a different
    // branch than the unprefixed one.
    for (const std::string_view prefix : {"-webkit-", "-moz-", "-ms-", "-o-"}) {
        if (name.size() > prefix.size() && ascii_iequals(name.substr(0, prefix.size()), prefix)) {
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

    // A style attribute: declarations, no braces, no selector.
    [[nodiscard]] stylesheet take_declaration_list() {
        std::vector<component_value> run;
        while (!at_eof()) { run.push_back(consume_component_value()); }
        emit_declarations(run);
        return std::move(sheet_);
    }

private:
    [[nodiscard]] const css_token & here() const { return sheet_.tokens[at_]; }
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
        const std::string_view name = value_of_at(here());
        ++at_;
        const at_kind kind = at_kind_of(name);
        std::vector<component_value> prelude;
        while (!at_eof() && here().type != token_type::open_curly &&
               here().type != token_type::semicolon) {
            prelude.push_back(consume_component_value());
        }
        if (here().type == token_type::semicolon) {
            ++at_;
            return; // @charset, @import, @namespace, `@layer a;` - all consumed
        }
        if (at_eof()) { return; }
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
            consume_rule_list(/*top_level=*/false);
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
            consume_rule_list(/*top_level=*/false);
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
        // @keyframes, @page, @property, @container, @scope, ... Their block is
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
    void emit_declarations(std::span<const component_value> run) {
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

        d.first_value = static_cast<std::uint32_t>(sheet_.values.size());
        d.value_count = static_cast<std::uint32_t>(value.size());
        sheet_.values.insert(sheet_.values.end(), value.begin(), value.end());
        const auto [text_at, text_len] = source_span(value);
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
        if (contiguous) { return {first.text, last.text + last.length - first.text}; }
        const std::size_t start = sheet_.pool.size();
        for (std::uint32_t i = first_index; i <= last_index; ++i) {
            // Copied out before the append: the pool is what text() reads from, so
            // appending to it while holding a view into it would dangle.
            const std::string piece{text(sheet_.tokens[i])};
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
};

} // namespace

stylesheet parse_stylesheet(std::string_view css, atom_table & atoms) {
    parser p{css, atoms};
    return p.take_stylesheet();
}

stylesheet parse_declaration_list(std::string_view css, atom_table & atoms) {
    parser p{css, atoms};
    return p.take_declaration_list();
}

} // namespace ctbrowser::style::css
