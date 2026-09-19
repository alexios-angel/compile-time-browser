#pragma once

#include "../compiler_impl.hpp"

namespace ctbrowser::script::detail {

// The `${...}` HOLES of a template literal, as raw text.
//
// A template is ONE node carrying its whole source, holes included - the
// parser does not break the substitutions out into child nodes. So every
// walk over the tree is blind to them, and the two walks that decide
// whether a local is BOXED and whether `arguments` is materialised must
// look inside. Nesting is counted so an object literal or a nested
// template inside a hole does not end it early.
template <typename Fn> void compiler_impl::for_each_template_hole(std::string_view raw, Fn && fn) {
    for (std::size_t i = 0; i + 1 < raw.size(); ++i) {
        if (raw[i] != '$' || raw[i + 1] != '{') { continue; }
        if (i > 0 && raw[i - 1] == '\\') { continue; }
        std::size_t depth = 1;
        std::size_t at_char = i + 2;
        const std::size_t start = at_char;
        while (at_char < raw.size() && depth > 0) {
            if (raw[at_char] == '{') { ++depth; }
            if (raw[at_char] == '}') { --depth; }
            if (depth > 0) { ++at_char; }
        }
        fn(raw.substr(start, at_char - start));
        i = at_char;
    }
}

// Every identifier-shaped token in a hole.
//
// Lexical rather than parsed, and deliberately OVER-approximate: it counts
// property names and reserved words as well as variables. Naming something
// that is not really captured only boxes a local that did not need boxing,
// which is correct and slightly slower; MISSING one reads undefined at run
// time with nothing to say so.
template <typename Fn> void compiler_impl::each_name_in_template(std::string_view raw, Fn && add) {
    for_each_template_hole(raw, [&](std::string_view hole) {
        for (std::size_t i = 0; i < hole.size();) {
            const auto begins = [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
            };
            const auto continues = [&](char c) { return begins(c) || (c >= '0' && c <= '9'); };
            if (!begins(hole[i])) {
                ++i;
                continue;
            }
            const std::size_t start = i;
            while (i < hole.size() && continues(hole[i])) { ++i; }
            add(hole.substr(start, i - start));
        }
    });
}

// `var` IS FUNCTION-SCOPED: `if (c) { var x = 1; }` declares `x` in the
// function, not the block, and webpack emits exactly that shape.
//
// Hoisting stops at a nested function, because that function's vars are
// ITS scope's, and does not descend into a declarator's initialiser, which
// is an expression and cannot contain a declaration statement.
template <typename Hoist>
void compiler_impl::hoist_nested_vars(std::int32_t index, const Hoist & hoist) {
    if (index < 0) { return; }
    const vp::node & n = at(index);
    if (is_function_node(n)) { return; }
    if (n.kind == vp::nk::var_decl && n.text == "var") {
        for (const std::int32_t d : kids(n)) {
            if (at(d).b >= 0) {
                std::vector<std::string> names;
                pattern_names(at(d).b, names);
                for (std::string & name : names) { hoist(std::move(name)); }
            } else {
                hoist(std::string{at(d).text});
            }
        }
        return;
    }
    for (const std::int32_t slot : child_slots(n)) { hoist_nested_vars(slot, hoist); }
    for (const std::int32_t k : kids(n)) { hoist_nested_vars(k, hoist); }
}

// ANNEX B.3.3: in sloppy code a function declared in a nested block is
// ALSO a `var` of the enclosing function (or a global of the script),
// written when the declaration is evaluated. This walk names every
// function declaration below a body, at any block depth, stopping at
// function boundaries as hoist_nested_vars does; the caller decides
// which of them may take a var binding (none that a parameter or a
// lexical declaration already names).
// ...unless a binding of the same name is declared BETWEEN the body and
// the declaration (B.3.3.1 step ii, "would not produce any Early Errors":
// `{ let f; { function f() {} } }` gets no var binding), which is what
// `lexical` carries. Every construct that binds lexically counts, not
// only a block's `let`: the head of a `for`, a destructuring catch
// parameter (a SIMPLE one does not - B.3.5 lets a `var` shadow it), a
// `switch` body, and a block's own function declarations, which are
// lexical bindings of that block (14.2.1) and shadow anything nested
// deeper - `{ function f(){} { function f(){} } }` gives the inner one
// no var binding.
// `body_level` says this list is a function body's or a script's own top
// level rather than a Block: its function declarations are vars there,
// so they are neither reported nor shadowing.
template <typename Each>
void compiler_impl::each_block_function(std::int32_t index, const Each & each,
                                        std::vector<std::string> & lexical, bool body_level) {
    if (index < 0) { return; }
    const vp::node & n = at(index);
    if (n.kind == vp::nk::func_decl) {
        if (std::find(lexical.begin(), lexical.end(), n.text) == lexical.end()) {
            each(std::string{n.text}, index);
        }
        return;
    }
    if (is_function_node(n) || n.kind == vp::nk::class_decl) { return; }
    const std::size_t mark = lexical.size();
    if (n.kind == vp::nk::block || n.kind == vp::nk::program) {
        each_block_function_scope(kids(n), each, lexical, body_level);
        lexical.resize(mark);
        return;
    }
    if (n.kind == vp::nk::switch_stmt) {
        // THE WHOLE SWITCH BODY IS ONE SCOPE (14.12.4 - one declarative
        // record for every CaseClause), so a `let` in one clause shadows
        // a function declared in another.
        std::vector<std::int32_t> stmts;
        for (const std::int32_t clause : kids(n)) {
            for (const std::int32_t s : kids(at(clause))) { stmts.push_back(s); }
        }
        each_block_function(n.a, each, lexical);
        each_block_function_scope(stmts, each, lexical, false);
        lexical.resize(mark);
        return;
    }
    // A DESTRUCTURING catch parameter only: B.3.5 relaxes the early error
    // for `catch (e) { var e; }`, so a simple name lets the extension
    // through and a pattern does not.
    if (n.kind == vp::nk::catch_clause && n.b >= 0) { pattern_names(n.b, lexical); }
    // The head of a `for` binds for the body below it, lexically when it
    // said `let`/`const`/`using`.
    if (n.kind == vp::nk::for_stmt && n.a >= 0 && at(n.a).kind == vp::nk::var_decl &&
        at(n.a).text != "var") {
        collect_lexical_names(std::span<const std::int32_t>{&n.a, 1}, lexical);
    }
    // `for (let x of xs)`: d bit0 is `const`, bit1 "nothing to declare",
    // bit3 `let`, bits 4 and 5 the `using` forms (see ctjs's for_stmt).
    if (n.kind == vp::nk::forof_stmt && n.a >= 0 && (n.d & (1 | 8 | 16 | 32)) != 0) {
        const vp::node & decl = at(n.a);
        if (decl.b >= 0) {
            pattern_names(decl.b, lexical);
        } else if (!decl.text.empty()) {
            lexical.emplace_back(decl.text);
        }
    }
    for (const std::int32_t slot : child_slots(n)) { each_block_function(slot, each, lexical); }
    for (const std::int32_t k : kids(n)) { each_block_function(k, each, lexical); }
    lexical.resize(mark);
}

template <typename Each>
void compiler_impl::each_block_function_scope(std::span<const std::int32_t> stmts,
                                              const Each & each, std::vector<std::string> & lexical,
                                              bool body_level) {
    collect_lexical_names(stmts, lexical);
    if (!body_level) {
        for (const std::int32_t s : stmts) {
            if (at(s).kind == vp::nk::func_decl) { each_block_function(s, each, lexical); }
        }
        collect_function_names(stmts, lexical);
    }
    for (const std::int32_t s : stmts) {
        if (at(s).kind != vp::nk::func_decl) { each_block_function(s, each, lexical); }
    }
}

} // namespace ctbrowser::script::detail
