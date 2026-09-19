#include "internal.hpp"

namespace ctbrowser::style::css::parser_detail {

[[nodiscard]] bool parser::layer_name_of(std::span<const component_value> run,
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

[[nodiscard]] std::uint32_t parser::declare_layer(std::string_view name) {
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

void parser::layer_statement(std::span<const component_value> prelude) {
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

void parser::at_rule_block_of(std::string_view name, std::span<const component_value> prelude,
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

void parser::scope_block_of(std::span<const component_value> prelude,
                            const component_value & block) {
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
        made.limit_count =
            parse_selector_list(sheet_, sheet_.children_of(run.front()), *atoms_, &invalid, &outer);
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

void parser::consume_at_rule() {
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

void parser::record_keyframes(std::span<const component_value> prelude,
                              const component_value & block) {
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
                frame.first_declaration = static_cast<std::uint32_t>(sheet_.declarations.size());
                emit_declarations(sheet_.children_of(v));
                frame.declaration_count = static_cast<std::uint32_t>(sheet_.declarations.size()) -
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

} // namespace ctbrowser::style::css::parser_detail
