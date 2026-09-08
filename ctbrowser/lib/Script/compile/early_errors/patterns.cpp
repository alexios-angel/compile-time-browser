// early errors - destructuring patterns: what an element may be, and the
// rest-element rules.
//
// One of six files carved out of a 1,476-line compile/early_errors.cpp on
// 2026-09-08 - the checker was one class with every member inline, as
// compile.cpp was before it became compile/. The class is declared in
// checker.hpp beside this, in ctbrowser::script::detail::early; the bodies are
// where they were, in the file that owns the concern. early_errors.hpp, the
// public face of the pass, did not change.

#include "checker.hpp"

namespace ctbrowser::script::detail::early {

// A destructuring target: an array or object LITERAL being used as a
// pattern, or a real pattern node. Nothing inside one is an assignment, so
// the assignment-target rules do not apply to its elements - but the
// DEFAULTS inside it are ordinary expressions and are walked.
// ONE ELEMENT OF A DESTRUCTURING ASSIGNMENT, checked as the target it has
// to be.
//
// 13.15.5 reinterprets an ArrayLiteral or an ObjectLiteral on the left of
// `=` as a pattern, and the reinterpretation is not total: every element
// has to be something a value can be assigned to, or another pattern.
// `[[(x, y)]] = [[]]` parses as two array literals and is not one.
//
// Only for the LITERAL forms. `array_pattern` and `object_pattern` are what
// the parser builds in a DECLARATION, where it has already restricted each
// position to a name or a nested pattern - there is nothing left to check
// and asking would only find the parser's own shapes.
void checker::check_pattern_target(std::int32_t idx) {
    if (idx < 0) { return; } // an elision: `[, a] = xs` skips a position
    switch (at(idx).kind) {
    case nk::ident:
    case nk::member:
    case nk::index:
    case nk::array:
    case nk::object:
    case nk::array_pattern:
    case nk::object_pattern: return;
    // A default: `[a = 1] = []`. Only `=` - `[a += 1] = []` is not one.
    case nk::assign:
        if (at(idx).text != "=") { break; }
        check_pattern_target(at(idx).a);
        return;
    case nk::assign_pattern: return;
    default: break;
    }
    report("this is not something a value can be assigned to in a destructuring pattern", idx);
}

void checker::walk_pattern(std::int32_t idx) {
    if (idx < 0 || depth_ >= max_depth) { return; }
    const deeper nesting{depth_};
    const vp::node & n = at(idx);
    switch (n.kind) {
    case nk::array:
    case nk::array_pattern: {
        const std::span<const std::int32_t> elements = kids(n);
        const bool literal = n.kind == nk::array;
        for (std::size_t i = 0; i < elements.size(); ++i) {
            const vp::node & element = at(elements[i]);
            const bool rest = element.kind == nk::rest_element || element.kind == nk::spread;
            // 13.15.5.1 / 8.2.2: a rest element must be the last one, and
            // it may not carry a default - `[...x = 1] = []` is not a
            // pattern with a defaulted rest, it is an error.
            if (rest && i + 1 < elements.size()) {
                report("a rest element must be the last one in the pattern", elements[i]);
            }
            if (rest && at(element.a).kind == nk::assign) {
                report("a rest element may not have a default", elements[i]);
            }
            if (literal) { check_pattern_target(rest ? element.a : elements[i]); }
            walk_pattern(elements[i]);
        }
        return;
    }
    case nk::object:
    case nk::object_pattern: {
        const std::span<const std::int32_t> entries = kids(n);
        const bool literal = n.kind == nk::object;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const vp::node & entry = at(entries[i]);
            if (entry.kind == nk::spread || entry.kind == nk::rest_element) {
                if (i + 1 < entries.size()) {
                    report("a rest element must be the last one in the pattern", entries[i]);
                }
                if (at(entry.a).kind == nk::assign) {
                    report("a rest element may not have a default", entries[i]);
                }
                if (literal) { check_pattern_target(entry.a); }
            } else if (literal && entry.kind == nk::prop) {
                // A METHOD OR AN ACCESSOR IS NOT AN AssignmentProperty.
                // `({ x: { get x() {} } } = o)` reads as an object literal
                // right up to the `=`, and only then is it a pattern -
                // which a getter cannot be part of.
                // c == 1 IS A METHOD AND c == 3 AN ACCESSOR; c == 2 is a
                // SHORTHAND, which is the commonest pattern element there
                // is. The three numbers do not mean the same thing on a
                // class member, where 2 is the accessor - checking the
                // wrong one here reported `({ x } = o)`.
                if (entry.c == 1 || entry.c == 3) {
                    report("a method cannot appear in a destructuring pattern", entries[i]);
                } else if (entry.b >= 0) {
                    check_pattern_target(entry.b);
                }
            }
            walk_pattern(entries[i]);
        }
        return;
    }
    case nk::prop:
        if ((n.d & 1) != 0) { walk_expression(n.a); }
        walk_pattern(n.b);
        return;
    case nk::pattern_prop:
        if ((n.d & 2) != 0) { walk_expression(n.a); }
        walk_pattern(n.b);
        return;
    case nk::assign_pattern:
        walk_pattern(n.a);
        walk_expression(n.b);
        return;
    case nk::assign:
        walk_pattern(n.a);
        walk_expression(n.b);
        return;
    case nk::rest_element:
    case nk::spread: walk_pattern(n.a); return;
    default: walk_expression(idx); return;
    }
}

} // namespace ctbrowser::script::detail::early
