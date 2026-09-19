#include "internal.hpp"

namespace ctbrowser::style::css::substitute_detail {

[[nodiscard]] bool substituter::attribute(const token_stream & outer, const call & found, int depth,
                                          std::string & expansion) {
    // THE HEAD IS SUBSTITUTED ON ITS OWN, and may not grow a comma: a
    // `var()` in the name or type position that expands to `data-foo
    // type(<number>), 10` has not supplied a fallback, it has broken the
    // argument list (attr-argument-grammar). The fallback keeps its own
    // text and is substituted only if it is taken.
    std::string inner;
    if (!run(text_between(outer, found.open + 1, found.comma_at), inner, depth + 1)) {
        return false;
    }
    {
        const token_stream head_tokens = tokenize(inner);
        int depth_here = 0;
        for (const css_token & t : head_tokens.tokens) {
            if (t.type == token_type::function || t.type == token_type::open_paren ||
                t.type == token_type::open_square || t.type == token_type::open_curly) {
                ++depth_here;
            } else if (t.type == token_type::close_paren || t.type == token_type::close_square ||
                       t.type == token_type::close_curly) {
                --depth_here;
            } else if (depth_here == 0 && t.type == token_type::comma) {
                return false;
            }
        }
    }
    if (found.comma_at < found.close) { inner += text_between(outer, found.comma_at, found.close); }
    const token_stream s = tokenize(inner);
    // The head - everything before the first top-level comma - as significant
    // tokens at depth zero, with a `type()` kept whole.
    std::vector<std::size_t> head;
    std::size_t type_open = 0;
    std::size_t type_close = 0;
    call args;
    args.close = s.tokens.size() - 1;
    args.comma_at = args.close;
    {
        int depth_here = 0;
        for (std::size_t i = 0; i + 1 < s.tokens.size(); ++i) {
            const css_token & t = s.tokens[i];
            if (t.type == token_type::whitespace) { continue; }
            if (depth_here == 0 && t.type == token_type::comma) {
                args.comma_at = i;
                break;
            }
            if (depth_here == 0) { head.push_back(i); }
            if (t.type == token_type::function || t.type == token_type::open_paren ||
                t.type == token_type::open_square || t.type == token_type::open_curly) {
                if (depth_here == 0 && is_function_named(s, t, "type")) { type_open = i; }
                ++depth_here;
            } else if (t.type == token_type::close_paren || t.type == token_type::close_square ||
                       t.type == token_type::close_curly) {
                --depth_here;
                if (depth_here == 0 && type_open != 0 && type_close == 0) { type_close = i; }
            }
        }
    }
    if (head.empty() || head.size() > 2 || s.tokens[head[0]].type != token_type::ident) {
        return false;
    }
    const std::string name{s.text_of(s.tokens[head[0]])};
    enum class kind : std::uint8_t {
        raw,
        unit,
        syntax
    };
    kind type = kind::raw;
    bool typed = false; // any <attr-type> at all, `raw-string` included
    std::string unit;
    std::vector<syntax_alternative> syntax;
    if (head.size() == 2) {
        typed = true;
        const css_token & t = s.tokens[head[1]];
        if (t.type == token_type::ident && ascii_iequals(s.text_of(t), "raw-string")) {
            type = kind::raw;
        } else if (t.type == token_type::ident) {
            type = kind::unit;
            unit = ascii_lower_copy(s.text_of(t));
        } else if (t.type == token_type::delim && s.text_of(t) == "%") {
            type = kind::unit;
            unit = "%";
        } else if (head[1] == type_open && type_open != 0) {
            type = kind::syntax;
            const std::size_t end = type_close != 0 ? type_close : s.tokens.size() - 1;
            std::optional<std::vector<syntax_alternative>> parsed =
                parse_syntax(text_between(s, type_open + 1, end));
            if (!parsed) { return false; }
            syntax = std::move(*parsed);
        } else {
            return false;
        }
    }

    // THE ATTRIBUTE. Absent: the fallback, else the empty string when no
    // type was asked for, else invalid.
    const std::optional<std::string> held = (*attributes_)(name);
    if (!held) {
        if (args.comma_at < args.close) { return fallback(s, args, depth, expansion); }
        if (typed) { return false; }
        expansion = "\"\"";
        return true;
    }
    // AN ATTRIBUTE ALREADY BEING SUBSTITUTED IS A CYCLE however it is read
    // again - through `type(*)`, or as the raw string a bare attr() makes
    // of it: `data-foo="attr(data-bar type(*))"` with `data-bar="attr(
    // data-foo)"` is a ring, and every attr() in it is invalid, fallbacks
    // included; only the attr() the declaration itself wrote takes its
    // fallback (attr-cycle 3, 28, 29).
    if (std::ranges::find(attrs_resolving_, name) != attrs_resolving_.end()) {
        attr_cycle_ = true;
        return false;
    }
    std::optional<std::string> value;
    switch (type) {
    case kind::raw: value = quoted(*held); break;
    case kind::unit: {
        // A BARE NUMBER AND NOTHING ELSE - not a calc(), not a var() - which
        // then gains the unit. `number` is the unit that adds nothing.
        const token_stream v = tokenize(*held);
        const std::vector<item> parts = items_of(v, 0);
        if (parts.size() == 1 && parts.front().first == parts.front().last &&
            v.tokens[parts.front().first].type == token_type::number) {
            const std::string number = serialize_number(v.tokens[parts.front().first].number);
            if (unit == "number") {
                value = number;
            } else if (unit == "%" || evaluate_math(number + unit, length_context{}).outcome !=
                                          math_outcome::invalid) {
                value = number + unit;
            }
        }
        break;
    }
    case kind::syntax: {
        // The text is a <declaration-value> with substitutions of its own
        // to perform before it is parsed - `attr(data-x type(*))` may hold
        // a `var()` or another `attr()`, and a cycle through it is a cycle:
        // an attribute already being substituted makes every attr() in the
        // ring invalid, fallbacks included, and only the attr() the
        // declaration itself wrote takes its fallback (attr-cycle 3, 8, 12,
        // 17, 28, 29; attr-all-types 75-79 read one attribute through
        // another without a cycle).
        attrs_resolving_.push_back(name);
        std::string substituted;
        const bool ok = run(*held, substituted, depth + 1);
        attrs_resolving_.pop_back();
        if (attr_cycle_) {
            if (!attrs_resolving_.empty()) { return false; }
            attr_cycle_ = false;
            break;
        }
        if (ok) { value = match_syntax(substituted, syntax); }
        break;
    }
    }
    if (value) {
        expansion = std::move(*value);
        return true;
    }
    return fallback(s, args, depth, expansion);
}

[[nodiscard]] bool substituter::identifier(const token_stream & outer, const call & found,
                                           int depth, std::string & expansion) {
    std::string inner;
    if (!run(text_between(outer, found.open + 1, found.close), inner, depth + 1)) { return false; }
    const token_stream s = tokenize(inner);
    std::string made;
    bool any = false;
    for (std::size_t i = 0; i + 1 < s.tokens.size(); ++i) {
        const css_token & t = s.tokens[i];
        if (t.type == token_type::whitespace) { continue; }
        any = true;
        if (t.type == token_type::string || t.type == token_type::ident) {
            made += s.value_of(t);
            continue;
        }
        if (t.type == token_type::number && (t.flags & flag_integer) != 0) {
            made += serialize_number(t.number);
            continue;
        }
        if (t.type == token_type::function) {
            const std::size_t close = end_of_block(s, i);
            const std::string text = text_between(s, i, close);
            if (!may_have_math(text)) { return false; }
            const math_answer answer = math_of(text);
            if (answer.outcome != math_outcome::resolved || !answer.value.is_number) {
                return false;
            }
            // An <integer> slot rounds a math function's answer (CSS
            // Values 4 §10.10): `ident("a" random(1, 300000))` is a name.
            made += serialize_number(std::floor(answer.value.px + 0.5));
            i = close - 1;
            continue;
        }
        return false;
    }
    if (!any || made.empty()) { return false; }
    // The result is an IDENTIFIER, and is spelled as one: `ident(3 "abc")`
    // is `\33 abc`, because `3abc` would read back as a dimension
    // (ident-function-computed).
    expansion = serialize_identifier(made);
    return true;
}

[[nodiscard]] bool substituter::custom_call(const token_stream & outer, const call & found,
                                            int depth, std::string & expansion) {
    const std::string_view head = outer.text_of(outer.tokens[found.open]);
    const std::string name{head.substr(0, head.size() - 1)};
    const custom_function * fn = conditions_->functions(name);
    if (fn == nullptr) { return false; }
    const std::uint64_t invocation = ++calls_;
    std::vector<std::string> args;
    const std::string inner = text_between(outer, found.open + 1, found.close);
    if (!trim(inner, html_whitespace).empty()) {
        for (const std::string_view arg : split_top_level(inner, ",")) {
            std::string done;
            if (!run(trim(arg, html_whitespace), done, depth + 1)) { return false; }
            args.push_back(std::move(done));
        }
    }
    if (args.size() > fn->parameters.size()) { return false; }
    // The bases a typed slot computes against: the caller's, keyed on the
    // function's slot and on this invocation - which is the calling
    // element, the property the call sits in, and which call of that
    // value this is.
    std::string slot_name;
    const std::uint64_t call_key =
        (conditions_->lengths.element_key ^ (invocation * 0x9E3779B97F4A7C15ull) ^
         (std::hash<std::string_view>{}(conditions_->property) * 0xBF58476D1CE4E5B9ull)) |
        1;
    const auto slot = [&](std::string_view which) {
        slot_name = name + '/' + std::string{which};
        length_context ctx = conditions_->lengths;
        ctx.property = slot_name;
        ctx.element_key = call_key;
        return ctx;
    };
    std::vector<std::pair<std::string, std::string>> scope;
    const auto assign = [&scope](std::string_view key, std::string value) {
        for (auto & [held, text] : scope) {
            if (held == key) {
                text = std::move(value);
                return;
            }
        }
        scope.emplace_back(std::string{key}, std::move(value));
    };
    const auto unset = [&scope](std::string_view key) {
        std::erase_if(scope, [key](const auto & entry) { return entry.first == key; });
    };
    for (std::size_t i = 0; i < fn->parameters.size(); ++i) {
        const custom_function::parameter & param = fn->parameters[i];
        std::string value;
        if (i < args.size() && !trim(args[i], html_whitespace).empty()) {
            value = args[i];
        } else if (param.has_default) {
            if (!run(param.initial, value, depth + 1)) { return false; }
        } else {
            return false;
        }
        if (param.syntax != "*") {
            std::optional<std::string> typed =
                compute_registered(value, param.syntax, slot(param.name));
            if (!typed) { return false; }
            value = std::move(*typed);
        }
        assign(param.name, std::move(value));
    }
    // The function's scope: its parameters and locals, then the caller's.
    const custom_lookup in_scope = [&scope, this](atom key) -> std::optional<std::string_view> {
        const std::string_view want = atoms_->text(key);
        for (const auto & [held, text] : scope) {
            if (held == want) { return std::string_view{text}; }
        }
        return (*lookup_)(key);
    };
    std::string result;
    bool have_result = false;
    for (const auto & [declared, text] : fn->body) {
        substituter body{in_scope, *atoms_, *attributes_, conditions_};
        std::string done;
        const bool ok = body.run(text, done, depth + 1);
        if (declared == "result") {
            if (!ok) { return false; }
            result = std::move(done);
            have_result = true;
            continue;
        }
        if (!ok) {
            unset(declared); // guaranteed-invalid: the local is undefined
            continue;
        }
        // A typed parameter set again in the body is computed like the
        // parameter (random-in-custom-function).
        for (const custom_function::parameter & param : fn->parameters) {
            if (param.name != declared || param.syntax == "*") { continue; }
            std::optional<std::string> typed =
                compute_registered(done, param.syntax, slot(param.name));
            if (!typed) {
                unset(declared);
                done.clear();
                break;
            }
            done = std::move(*typed);
        }
        if (!done.empty()) { assign(declared, std::move(done)); }
    }
    if (!have_result) { return false; }
    if (fn->returns != "*") {
        std::optional<std::string> typed = compute_registered(result, fn->returns, slot("result"));
        if (!typed) { return false; }
        result = std::move(*typed);
    }
    expansion = std::move(result);
    return true;
}

[[nodiscard]] bool substituter::random_item(const token_stream & outer, const call & found,
                                            int depth, std::string & expansion) {
    if (found.comma_at >= found.close) { return false; } // no items at all
    std::string key;
    if (!run(text_between(outer, found.open + 1, found.comma_at), key, depth + 1)) { return false; }
    // The key: `fixed <number>`, or the sharing words `random_base` reads.
    double base = 0.0;
    const std::string_view trimmed = trim(key, html_whitespace);
    if (ascii_istarts_with(trimmed, "fixed")) {
        const math_answer given = evaluate_math(trimmed.substr(5), length_context{});
        if (given.outcome != math_outcome::resolved || !given.value.is_number) { return false; }
        base = std::min(std::max(given.value.px, 0.0), 1.0 - 1e-9);
    } else {
        for (const std::string_view word : split_top_level(trimmed, " \t\n\r\f")) {
            if (!word.starts_with("--") && !ascii_iequals(word, "auto") &&
                !ascii_iequals(word, "element-scoped")) {
                return false;
            }
        }
        length_context ctx = conditions_ != nullptr ? conditions_->lengths : length_context{};
        if (conditions_ != nullptr) { ctx.property = conditions_->property; }
        // `auto` is scoped to the element like a bare `element-scoped`: the
        // base is keyed on the element either way, and a name alone is not.
        // An auto random-item() does NOT share with an auto random() in the
        // same value, where a bare `element-scoped` one does
        // (random-item-computed): its automatic key names the function.
        std::string options{trimmed};
        std::string keyed_property{ctx.property};
        if (ascii_iequals(trimmed, "auto") || trimmed.empty()) {
            options.clear();
            keyed_property = "random-item:" + keyed_property;
            ctx.property = keyed_property;
        }
        base = random_base(options, ctx);
    }
    // The items, split at the top-level commas after the key.
    std::vector<std::pair<std::size_t, std::size_t>> items;
    std::size_t start = found.comma_at + 1;
    int block = 0;
    for (std::size_t i = start; i < found.close; ++i) {
        const css_token & t = outer.tokens[i];
        if (t.type == token_type::function || t.type == token_type::open_paren ||
            t.type == token_type::open_square || t.type == token_type::open_curly) {
            ++block;
        } else if (t.type == token_type::close_paren || t.type == token_type::close_square ||
                   t.type == token_type::close_curly) {
            --block;
        } else if (block == 0 && t.type == token_type::comma) {
            items.emplace_back(start, i);
            start = i + 1;
        }
    }
    items.emplace_back(start, found.close);
    const std::size_t pick = std::min(
        items.size() - 1, static_cast<std::size_t>(base * static_cast<double>(items.size())));
    std::string_view chosen =
        trim(text_between_view(outer, items[pick].first, items[pick].second), html_whitespace);
    if (chosen.size() >= 2 && chosen.front() == '{' && chosen.back() == '}') {
        chosen = trim(chosen.substr(1, chosen.size() - 2), html_whitespace);
    }
    return run(chosen, expansion, depth + 1);
}

} // namespace ctbrowser::style::css::substitute_detail
