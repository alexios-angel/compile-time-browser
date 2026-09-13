// early errors - functions and classes: parameter lists, and the class body
// rules over constructors, `prototype` and private names.
//
// One of six files carved out of a 1,476-line compile/early_errors.cpp on
// 2026-09-08 - the checker was one class with every member inline, as
// compile.cpp was before it became compile/. The class is declared in
// checker.hpp beside this, in ctbrowser::script::detail::early; the bodies are
// where they were, in the file that owns the concern. early_errors.hpp, the
// public face of the pass, did not change.

#include "checker.hpp"

#include <algorithm>
#include <cctype>

namespace ctbrowser::script::detail::early {

// --- functions --------------------------------------------------------------

// Is this a simple parameter list - names only, no default, no rest, no
// pattern? 15.1.3 IsSimpleParameterList, and the answer decides whether a
// duplicate parameter name is an error at all.
[[nodiscard]] bool checker::simple_parameters(std::span<const std::int32_t> params) const {
    for (const std::int32_t p : params) {
        const vp::node & n = at(p);
        if (n.d == 1 || n.a >= 0 || n.b >= 0) { return false; }
    }
    return true;
}

void checker::check_function(std::int32_t idx, frame_kind what, bool super_call_ok) {
    const vp::node & n = at(idx);
    const std::span<const std::int32_t> params = kids(n);

    std::vector<binding> names;
    for (const std::int32_t p : params) { bound_names(p, binding_kind::let_, names); }

    // 15.1.2, 15.2.1, 15.3.1: a duplicate parameter name is an error when
    // the list is not simple, and always for an arrow or a method. A
    // duplicate in a SIMPLE list is an error only in strict mode, which
    // this engine does not have - so `function f(a, a) {}` is accepted, as
    // sloppy JavaScript accepts it.
    // Strictness is decided here, before the frame exists: the enclosing
    // frame's, a class body's, or this body's own directive.
    const bool strict_body = strict() || class_depth_ > 0 ||
                             (at(n.a).kind == nk::block && has_use_strict_directive(n.a));
    const bool must_be_unique = what == frame_kind::arrow || what == frame_kind::method ||
                                strict_body || !simple_parameters(params);
    if (must_be_unique && names.size() > 1) {
        std::unordered_set<std::string_view> seen;
        seen.reserve(names.size());
        for (const binding & b : names) {
            if (!seen.insert(b.name).second) {
                report("parameter " + quoted(b.name) +
                           " is bound twice, which a list with a default, a rest element, a "
                           "pattern, an arrow or a method may not do",
                       b.node);
            }
        }
    }
    // 15.1.1: a rest parameter must be last - and nothing follows it, a
    // comma included (`(...a,)` is not in the grammar; the parser keeps no
    // trace of the comma, so the source is asked, as for a rest element).
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (at(params[i]).d != 1) { continue; }
        if (i + 1 < params.size()) {
            report("a rest parameter must be the last one", params[i]);
        } else if (comma_follows(params[i])) {
            report("a rest parameter may not be followed by a comma", params[i]);
        }
        break;
    }
    // 15.2.1 (and every function form): a "use strict" directive needs a
    // simple parameter list - a default, a rest or a pattern makes it an
    // error, because the directive would have to apply to code that already
    // ran under the other rules.
    if (at(n.a).kind == nk::block && has_use_strict_directive(n.a) && !simple_parameters(params)) {
        report("a function with a default, rest or destructured parameter may not have a "
               "\"use strict\" directive",
               idx);
    }

    const std::int32_t bits = n.c > 0 ? n.c : 0; // see check_class: -1 is neither
    frames_.push_back(
        frame{what, {}, {}, 0, 0, super_call_ok, strict_body, (bits & 1) != 0, (bits & 2) != 0});
    check_strict_bindings(names);
    if (strict_body && n.kind == nk::func_expr) { check_strict_binding(n.text, idx); }
    // 15.5.1 / 15.8.1: `yield` in a generator's parameters and `await` in an
    // async function's are errors - see in_parameters_.
    const bool outer_parameters = in_parameters_;
    in_parameters_ = true;
    for (const std::int32_t p : params) {
        const vp::node & param = at(p);
        if (param.b >= 0) { walk_pattern(param.b); }
        walk_expression(param.a);
    }
    in_parameters_ = false;
    const vp::node & body = at(n.a);
    if (body.kind == nk::block) {
        (void)check_list(kids(body), list_kind::function_body, &names, "parameter");
    } else {
        // A concise arrow body: one expression, no declarations to check.
        walk_expression(n.a);
    }
    in_parameters_ = outer_parameters;
    frames_.pop_back();
}

// 15.4.1: a getter takes no parameter, a setter exactly one - a
// FormalParameter, so a default or a pattern but never a rest.
void checker::check_accessor_arity(std::int32_t fn, bool setter) {
    const std::span<const std::int32_t> params = kids(at(fn));
    if (!setter && !params.empty()) {
        report("a getter takes no parameter", params.front());
    } else if (setter && (params.size() != 1 || at(params.front()).d == 1)) {
        report("a setter takes exactly one parameter, and not a rest parameter",
               params.empty() ? fn : params.front());
    }
}

// --- classes -----------------------------------------------------------------

// IS THE HERITAGE PARENTHESISED? The parser keeps no parentheses, so
// `class C extends (a, b) {}` and `class C extends a, b {}` are one tree -
// and only the second is the error. The source is asked: the first thing
// after the `extends` keyword.
[[nodiscard]] bool checker::heritage_parenthesised(std::int32_t klass) const {
    const vp::node & n = at(klass);
    if (source_.empty() || n.end <= n.begin || n.end > source_.size()) { return true; }
    const std::string_view text = source_.substr(n.begin, n.end - n.begin);
    std::size_t at_word = 0;
    for (;;) {
        at_word = text.find("extends", at_word);
        if (at_word == std::string_view::npos) { return true; } // not found: do not judge
        const bool starts =
            at_word == 0 || !(std::isalnum(static_cast<unsigned char>(text[at_word - 1])) ||
                              text[at_word - 1] == '_' || text[at_word - 1] == '$');
        const std::size_t after = at_word + 7;
        const bool ends =
            after >= text.size() || !(std::isalnum(static_cast<unsigned char>(text[after])) ||
                                      text[after] == '_' || text[after] == '$');
        if (starts && ends) { break; }
        at_word = after;
    }
    for (std::size_t i = at_word + 7; i < text.size(); ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { continue; }
        // A `(` that IS the heritage's own first token - an arrow's parameter
        // list, `extends () => {}` - is not a parenthesis around it. A function
        // or arrow node carries its span; nothing else that reaches here does.
        const vp::node & heritage = at(n.a);
        if (c == '(' && heritage.begin != 0 && heritage.begin == n.begin + i) { return false; }
        return c == '(';
    }
    return true;
}

void checker::check_class(std::int32_t idx) {
    const vp::node & n = at(idx);
    // ClassHeritage is `extends LeftHandSideExpression` (15.7): an arrow, an
    // assignment, a conditional or an operator there is not in the grammar,
    // whatever the parser let through - unless it was parenthesised, which
    // the tree cannot show and the source can.
    switch (at(n.a).kind) {
    case nk::arrow:
    case nk::assign:
    case nk::ternary:
    case nk::binary:
    case nk::logical:
    case nk::unary:
    case nk::update:
    case nk::seq:
    case nk::yield_expr:
        if (!heritage_parenthesised(idx)) {
            report("`extends` takes a left-hand-side expression", n.a);
        }
        break;
    default: break;
    }
    walk_expression(n.a); // `extends <expr>`
    const bool derived = n.a >= 0;
    // 15.7.1: all parts of a class are strict mode code - its NAME included
    // (`class let {}`, `class yield {}`, `class eval {}` are all errors,
    // 13.1.1 under strict). strict() reads the frame, which a class body
    // does not push, so the strict rules are applied here by hand.
    if (!n.text.empty()) {
        if (reserved_word(n.text) || n.text == "eval" || n.text == "arguments") {
            report(quoted(n.text) + " cannot be the name of a class", idx);
        }
        for (const std::string_view reserved : {"yield", "let", "static", "implements", "interface",
                                                "package", "private", "protected", "public"}) {
            if (n.text == reserved) {
                report(quoted(n.text) + " is a reserved word in strict mode code and cannot "
                                        "name a class",
                       idx);
            }
        }
        check_contextual_name(n.text, idx);
    }
    ++class_depth_;
    // The body's private names, in scope for every member from here on and
    // for nothing before - the heritage above was walked against the OUTER
    // environment (ClassDefinitionEvaluation evaluates it before the class's
    // PrivateEnvironment is entered).
    private_names_.emplace_back();
    for (const std::int32_t m : kids(n)) {
        const vp::node & member = at(m);
        if ((member.d & 2) == 0 && member.text.starts_with('#')) {
            private_names_.back().push_back(member.text);
        }
    }
    const struct leave {
        std::size_t & depth;
        std::vector<std::vector<std::string_view>> & names;
        ~leave() {
            --depth;
            names.pop_back();
        }
    } leaving{class_depth_, private_names_};

    std::vector<private_name> privates;
    std::size_t constructors = 0;
    for (const std::int32_t m : kids(n)) {
        const vp::node & member = at(m);
        if (member.c == 3) {
            // A static block: its statement list is checked as a function
            // body's is (duplicate lexical names, var against let, labels,
            // break and continue), in a frame of its own.
            // is_async, so that `await` as a name is refused as it is in an
            // async body (ClassStaticBlockStatementList is [+Await]).
            frames_.push_back(frame{frame_kind::static_block, {}, {}, 0, 0, false, true, true});
            (void)check_list(kids(at(member.b)), list_kind::function_body, nullptr, "");
            frames_.pop_back();
            continue;
        }
        const bool is_static = (member.d & 1) != 0;
        const bool is_method = member.c == 1;
        const bool is_accessor = member.c == 2;
        const bool is_field = member.c == 0;
        // `c` ON A FUNCTION NODE IS -1 WHEN IT IS NEITHER async NOR a
        // generator, not 0: the parser only writes the field when one of
        // the bits is set. Masking -1 says both, which made every ordinary
        // `constructor` look like a generator.
        const vp::node & body = at(member.b);
        const std::int32_t bits = body.c > 0 ? body.c : 0;
        const bool is_generator = is_method && (bits & 2) != 0;
        const bool is_async = is_method && (bits & 1) != 0;
        // THE PROPERTY NAME AS WRITTEN. The parser files a string-literal
        // key (`'constructor'`) as a computed key holding a str node but
        // keeps the quoted text, and 15.7.1's PropName rules are about the
        // literal, so it is read back here without its quotes. A bracketed
        // key has no text and no PropName (`static ['prototype']` is the
        // runtime TypeError, not this).
        std::string_view literal = member.text;
        bool computed = (member.d & 2) != 0;
        if (computed && !literal.empty() && literal.size() >= 2 &&
            (literal.front() == '\'' || literal.front() == '"')) {
            literal = literal.substr(1, literal.size() - 2);
            computed = literal.find('\\') != std::string_view::npos; // an escape: not read
        }

        // 15.7.1. A class body may define at most one constructor, and
        // "constructor" may not be a getter, a setter, a generator, an
        // async method or a field - a static field included.
        if (!computed && !is_static && literal == "constructor") {
            if (is_method && !is_generator && !is_async) {
                ++constructors;
                if (constructors > 1) { report("a class may define only one `constructor`", m); }
            } else if (is_accessor || is_generator || is_async) {
                report("`constructor` may not be an accessor, a generator or async", m);
            }
        }
        if (!computed && is_field && literal == "constructor") {
            report("a class field may not be named `constructor`", m);
        }
        // 15.7.1: a static member may not be named `prototype`.
        if (!computed && is_static && literal == "prototype") {
            report("a static class member may not be named `prototype`", m);
        }
        // 15.7.1: `#constructor` is not a private name a class may bind.
        if (!computed && member.text == "#constructor") {
            report("`#constructor` is not a name a class member may have", m);
        }
        // 15.7.1: PrivateBoundIdentifiers may not contain a duplicate,
        // unless the pair is one getter and one setter of the same
        // staticness - which is how a private accessor is written.
        if (!computed && member.text.starts_with('#')) {
            private_name * seen = nullptr;
            for (private_name & held : privates) {
                if (held.name == member.text) { seen = &held; }
            }
            if (seen == nullptr) {
                privates.push_back(private_name{member.text, 0, false, false, is_static, false});
                seen = &privates.back();
            }
            const bool getter = is_accessor && (member.d & 4) == 0;
            const bool setter = is_accessor && (member.d & 4) != 0;
            ++seen->seen;
            const bool pairs_up = seen->seen == 2 && seen->is_static == is_static && !seen->other &&
                                  !is_method && !is_field &&
                                  ((seen->getter && setter) || (seen->setter && getter));
            if (seen->seen > 1 && !pairs_up) {
                report("the private name " + quoted(member.text) + " is bound twice in this class",
                       m);
            }
            seen->getter = seen->getter || getter;
            seen->setter = seen->setter || setter;
            seen->other = seen->other || is_method || is_field;
        }

        if (computed) { walk_expression(member.a); }
        if (is_method || is_accessor) {
            // THE ONE PLACE `super(...)` IS ALLOWED: the constructor of a
            // class that has a heritage.
            const bool is_constructor =
                is_method && !is_static && !computed && literal == "constructor";
            if (is_accessor) { check_accessor_arity(member.b, (member.d & 4) != 0); }
            check_function(member.b, frame_kind::method, derived && is_constructor);
        } else {
            frames_.push_back(frame{frame_kind::field_init, {}, {}, 0, 0, false, true});
            walk_expression(member.b);
            frames_.pop_back();
        }
    }
}

void checker::check_private_reference(std::string_view name, std::int32_t node) {
    for (const std::vector<std::string_view> & body : private_names_) {
        if (std::find(body.begin(), body.end(), name) != body.end()) { return; }
    }
    report("the private name " + quoted(name) + " is not declared by an enclosing class", node);
}

} // namespace ctbrowser::script::detail::early
