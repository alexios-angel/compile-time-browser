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
    const bool must_be_unique =
        what == frame_kind::arrow || what == frame_kind::method || !simple_parameters(params);
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
    // 15.1.1: a rest parameter must be last.
    for (std::size_t i = 0; i + 1 < params.size(); ++i) {
        if (at(params[i]).d == 1) {
            report("a rest parameter must be the last one", params[i]);
            break;
        }
    }

    frames_.push_back(frame{what, {}, {}, 0, 0, super_call_ok});
    for (const std::int32_t p : params) {
        const vp::node & param = at(p);
        if (param.b >= 0) { walk_pattern(param.b); }
        walk_expression(param.a);
    }
    const vp::node & body = at(n.a);
    if (body.kind == nk::block) {
        (void)check_list(kids(body), list_kind::function_body, &names, "parameter");
    } else {
        // A concise arrow body: one expression, no declarations to check.
        walk_expression(n.a);
    }
    frames_.pop_back();
}

// --- classes -----------------------------------------------------------------

void checker::check_class(std::int32_t idx) {
    const vp::node & n = at(idx);
    walk_expression(n.a); // `extends <expr>`
    const bool derived = n.a >= 0;

    std::vector<private_name> privates;
    std::size_t constructors = 0;
    for (const std::int32_t m : kids(n)) {
        const vp::node & member = at(m);
        const bool is_static = (member.d & 1) != 0;
        const bool computed = (member.d & 2) != 0;
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

        // 15.7.1. A class body may define at most one constructor, and
        // "constructor" may not be a getter, a setter, a generator, an
        // async method or a field.
        if (!computed && !is_static && member.text == "constructor") {
            if (is_method && !is_generator && !is_async) {
                ++constructors;
                if (constructors > 1) { report("a class may define only one `constructor`", m); }
            } else if (is_accessor || is_generator || is_async) {
                report("`constructor` may not be an accessor, a generator or async", m);
            } else if (is_field) {
                report("a class field may not be named `constructor`", m);
            }
        }
        // 15.7.1: a static member may not be named `prototype`.
        if (!computed && is_static && member.text == "prototype") {
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
                is_method && !is_static && !computed && member.text == "constructor";
            check_function(member.b, frame_kind::method, derived && is_constructor);
        } else {
            frames_.push_back(frame{frame_kind::field_init, {}, {}, 0, 0, false});
            walk_expression(member.b);
            frames_.pop_back();
        }
    }
}

} // namespace ctbrowser::script::detail::early
